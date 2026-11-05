// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// measure-frames (#177): the frames of the functions of x86-64 ELF objects or executables, as their code makes them, to
// compare one build's frames with another's (Tier 1's with clang -O2's, with and without the objects' lifetimes).
//
//   build/compiler measure-frames [--functions] LABEL=FILE...
//
// Each FILE's functions are its symbol table's STT_FUNC symbols of a size in an executable section, each one decoded
// (x86_disassemble) from its first byte to its size. A function's frame is the most bytes it takes below its return
// address on the way through its code in address order: each push and each `sub rsp, imm` (or `lea rsp, [rsp - imm]`)
// adds, each pop and `add rsp, imm` takes back, and the frame is the most the sum reaches (so that a prologue after an
// early return, as clang's shrink-wrapping places it, counts, and so do several epilogues). A realignment (`and rsp,
// -imm`) is its alignment apart; whether the function sets a frame pointer (`mov rbp, rsp`) too. A function whose code
// doesn't decode whole counts what decoded, and is reported as partial. With --functions, a line per function:
//
//   frame LABEL NAME BYTES ALIGNMENT POINTER PARTIAL
//
// Then a line per FILE against the first: `frames LABEL`, its functions and their frames' sum, and over the functions
// whose names both have once each (a name twice in a file is left out, as two static functions of an executable), how
// many, both sums and their ratio, the median of the functions' ratios ((bytes + 8) / (first's bytes + 8): the return
// address counted, so that two empty frames are 1), and how many are larger, equal and smaller. Tab-separated.

#include <elf.h>

#define BUSTER_FRAME_MEASURE_FILES 8
#define BUSTER_FRAME_MEASURE_DECODED 1024

typedef struct FrameMeasureFunction FrameMeasureFunction;
struct FrameMeasureFunction
{
    const char* name;
    uint64_t bytes;
    uint32_t alignment;
    bool pointer;
    bool partial;
    // Whether the name is the file's once (frame_measure_unique).
    bool unique;
};

typedef struct FrameMeasureFile FrameMeasureFile;
struct FrameMeasureFile
{
    const char* label;
    const char* path;
    uint8_t* bytes;
    uint64_t size;
    FrameMeasureFunction* functions;
    uint32_t count;
};

// The frame of `size` bytes of code at `code` (the file's comment).
static FrameMeasureFunction frame_measure_code(const uint8_t* code, uint64_t size)
{
    FrameMeasureFunction result = { 0 };
    static X86Decoded decoded[BUSTER_FRAME_MEASURE_DECODED];
    int64_t depth = 0;
    int64_t most = 0;
    uint64_t consumed = 0;
    bool decoding = true;
    while (decoding && consumed < size)
    {
        const X86DecodeResult step = x86_disassemble(code + consumed, size - consumed, decoded, BUSTER_FRAME_MEASURE_DECODED);
        for (uint64_t i = 0; i < step.count; i += 1)
        {
            const X86Instruction* instruction = &decoded[i].instruction;
            const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
            const bool direct = instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->rm == X86_REGISTER_RSP;
            const bool sub = direct && (form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32);
            const bool add = direct && (form == X86_FORM_ADD_RM64_IMM8 || form == X86_FORM_ADD_RM64_IMM32);
            const bool align = direct && (form == X86_FORM_AND_RM64_IMM8 || form == X86_FORM_AND_RM64_IMM32) && instruction->immediate < 0;
            const bool lea = form == X86_FORM_LEA_R64_M && instruction->reg == X86_REGISTER_RSP && instruction->mode == BUSTER_X86_MEMORY_BASE &&
                             instruction->rm == X86_REGISTER_RSP;
            depth += form == X86_FORM_PUSH_R64 ? 8 : form == X86_FORM_POP_R64 ? -8 : 0;
            depth += sub ? instruction->immediate : add ? -(int64_t)instruction->immediate : lea ? -(int64_t)instruction->displacement : 0;
            most = depth > most ? depth : most;
            const uint32_t alignment = align ? (uint32_t)-(int64_t)instruction->immediate : 0;
            result.alignment = alignment > result.alignment ? alignment : result.alignment;
            result.pointer |= form == X86_FORM_MOV_RM64_R64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->rm == X86_REGISTER_RBP &&
                              instruction->reg == X86_REGISTER_RSP;
            result.pointer |= form == X86_FORM_MOV_R64_RM64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->reg == X86_REGISTER_RBP &&
                              instruction->rm == X86_REGISTER_RSP;
        }
        consumed += step.consumed;
        decoding = step.status == X86_DECODE_OK && step.consumed != 0;
    }
    result.bytes = (uint64_t)most;
    result.partial = consumed < size;
    return result;
}

// The section of index `index` of the ELF image `file` (whose header frame_measure_open checked), or null.
static const Elf64_Shdr* frame_measure_section(const FrameMeasureFile* file, uint32_t index)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file->bytes;
    const bool valid = index < header->e_shnum && header->e_shoff + (uint64_t)(index + 1) * sizeof(Elf64_Shdr) <= file->size;
    return valid ? (const Elf64_Shdr*)(file->bytes + header->e_shoff) + index : NULL;
}

// The file mapped and its functions' frames measured, in `arena`. Returns false, with a message, where it can't be.
static bool frame_measure_open(FrameMeasureFile* file, Arena* arena)
{
    const int fd = open(file->path, O_RDONLY | O_CLOEXEC);
    struct stat info;
    bool valid = fd >= 0 && fstat(fd, &info) == 0 && info.st_size >= (off_t)sizeof(Elf64_Ehdr);
    file->size = valid ? (uint64_t)info.st_size : 0;
    file->bytes = valid ? mmap(NULL, file->size, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
    valid = valid && file->bytes != MAP_FAILED;
    if (fd >= 0)
    {
        close(fd);
    }
    const Elf64_Ehdr* header = valid ? (const Elf64_Ehdr*)file->bytes : NULL;
    valid = valid && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 && header->e_ident[EI_DATA] == ELFDATA2LSB &&
            header->e_machine == EM_X86_64 && header->e_shentsize == sizeof(Elf64_Shdr) && header->e_shoff + (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= file->size;
    const Elf64_Shdr* symbols = NULL;
    for (uint32_t s = 0; valid && symbols == NULL && s < header->e_shnum; s += 1)
    {
        const Elf64_Shdr* section = frame_measure_section(file, s);
        symbols = section->sh_type == SHT_SYMTAB ? section : NULL;
    }
    const Elf64_Shdr* strings = symbols != NULL ? frame_measure_section(file, symbols->sh_link) : NULL;
    valid = valid && symbols != NULL && strings != NULL && symbols->sh_entsize == sizeof(Elf64_Sym) && symbols->sh_offset + symbols->sh_size <= file->size &&
            strings->sh_offset + strings->sh_size <= file->size;
    const uint64_t count = valid ? symbols->sh_size / sizeof(Elf64_Sym) : 0;
    file->functions = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, FrameMeasureFunction, count + 1) : NULL;
    valid = valid && file->functions != NULL;
    file->count = 0;
    for (uint64_t i = 0; valid && i < count; i += 1)
    {
        const Elf64_Sym* symbol = (const Elf64_Sym*)(file->bytes + symbols->sh_offset) + i;
        const Elf64_Shdr* section = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_size != 0 && symbol->st_shndx != SHN_UNDEF && symbol->st_shndx < SHN_LORESERVE ?
                                        frame_measure_section(file, symbol->st_shndx) : NULL;
        const bool code = section != NULL && (section->sh_flags & SHF_EXECINSTR) != 0 && section->sh_type == SHT_PROGBITS && symbol->st_name < strings->sh_size;
        // An object's symbol is an offset in its section, an executable's an address in its segment.
        const uint64_t start = code ? (header->e_type == ET_REL ? symbol->st_value : symbol->st_value - section->sh_addr) : 0;
        if (code && start + symbol->st_size <= section->sh_size && section->sh_offset + start + symbol->st_size <= file->size)
        {
            FrameMeasureFunction* function = &file->functions[file->count];
            *function = frame_measure_code(file->bytes + section->sh_offset + start, symbol->st_size);
            function->name = (const char*)(file->bytes + strings->sh_offset + symbol->st_name);
            file->count += 1;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "measure-frames: %s is no x86-64 ELF file with a symbol table\n", file->path);
    }
    return valid;
}

static int frame_measure_names(const void* left, const void* right)
{
    return strcmp(((const FrameMeasureFunction*)left)->name, ((const FrameMeasureFunction*)right)->name);
}

static int frame_measure_doubles(const void* left, const void* right)
{
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

// The file's functions sorted by name, each one marked whether its name is the file's once.
static void frame_measure_unique(FrameMeasureFile* file)
{
    qsort(file->functions, file->count, sizeof(FrameMeasureFunction), frame_measure_names);
    for (uint32_t i = 0; i < file->count; i += 1)
    {
        const bool before = i > 0 && strcmp(file->functions[i - 1].name, file->functions[i].name) == 0;
        const bool after = i + 1 < file->count && strcmp(file->functions[i + 1].name, file->functions[i].name) == 0;
        file->functions[i].unique = !before && !after;
    }
}

// The line of `file` against `first` (the file's comment), its functions' ratios' room in `ratios`.
static void frame_measure_compare(const FrameMeasureFile* first, const FrameMeasureFile* file, double* ratios)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < file->count; i += 1)
    {
        sum += file->functions[i].bytes;
    }
    uint32_t common = 0;
    uint64_t first_sum = 0;
    uint64_t file_sum = 0;
    uint32_t larger = 0;
    uint32_t equal = 0;
    uint32_t smaller = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    while (a < first->count && b < file->count)
    {
        const FrameMeasureFunction* left = &first->functions[a];
        const FrameMeasureFunction* right = &file->functions[b];
        const int order = strcmp(left->name, right->name);
        if (order == 0 && left->unique && right->unique)
        {
            ratios[common] = (double)(right->bytes + 8) / (double)(left->bytes + 8);
            common += 1;
            first_sum += left->bytes;
            file_sum += right->bytes;
            larger += right->bytes > left->bytes;
            equal += right->bytes == left->bytes;
            smaller += right->bytes < left->bytes;
        }
        a += order <= 0 ? 1 : 0;
        b += order >= 0 ? 1 : 0;
    }
    qsort(ratios, common, sizeof(double), frame_measure_doubles);
    const double median = common == 0 ? 0.0 : (common % 2 != 0 ? ratios[common / 2] : (ratios[common / 2 - 1] + ratios[common / 2]) / 2.0);
    printf("frames\t%s\tfunctions=%u\tbytes=%" PRIu64 "\tagainst=%s\tcommon=%u\tbytes_%s=%" PRIu64 "\tbytes_%s=%" PRIu64 "\tsum_ratio=%.4f\tmedian_ratio=%.4f\tlarger=%u\tequal=%u\tsmaller=%u\n",
           file->label, file->count, sum, first->label, common, first->label, first_sum, file->label, file_sum, first_sum == 0 ? 0.0 : (double)file_sum / (double)first_sum,
           median, larger, equal, smaller);
}

static int run_frame_measure(int argc, char** argv)
{
    FrameMeasureFile files[BUSTER_FRAME_MEASURE_FILES];
    uint32_t count = 0;
    bool listing = false;
    bool valid = argc > 1;
    for (int i = 1; valid && i < argc; i += 1)
    {
        char* equals = strchr(argv[i], '=');
        if (strcmp(argv[i], "--functions") == 0)
        {
            listing = true;
        }
        else if (equals != NULL && equals != argv[i] && equals[1] != 0 && count < BUSTER_FRAME_MEASURE_FILES)
        {
            *equals = 0;
            files[count] = (FrameMeasureFile){ .label = argv[i], .path = equals + 1 };
            count += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && count != 0;
    if (!valid)
    {
        fprintf(stderr, "usage: compiler measure-frames [--functions] LABEL=FILE... (at most %u files)\n", BUSTER_FRAME_MEASURE_FILES);
    }
    Arena arena = valid ? arena_reserve(UINT64_C(1) << 34) : (Arena){ 0 };
    valid = valid && arena.base != NULL && x86_disassembler_initialize();
    uint64_t most = 0;
    for (uint32_t f = 0; valid && f < count; f += 1)
    {
        valid = frame_measure_open(&files[f], &arena);
        if (valid)
        {
            frame_measure_unique(&files[f]);
            most = files[f].count > most ? files[f].count : most;
        }
        for (uint32_t i = 0; valid && listing && i < files[f].count; i += 1)
        {
            const FrameMeasureFunction* function = &files[f].functions[i];
            printf("frame\t%s\t%s\t%" PRIu64 "\t%u\t%u\t%u\n", files[f].label, function->name, function->bytes, function->alignment, function->pointer, function->partial);
        }
    }
    double* ratios = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, double, most + 1) : NULL;
    for (uint32_t f = 0; valid && ratios != NULL && f < count; f += 1)
    {
        frame_measure_compare(&files[0], &files[f], ratios);
    }
    for (uint32_t f = 0; f < count; f += 1)
    {
        if (files[f].bytes != NULL && files[f].bytes != MAP_FAILED)
        {
            munmap(files[f].bytes, files[f].size);
        }
    }
    arena_release(&arena);
    return valid && ratios != NULL ? 0 : 1;
}
