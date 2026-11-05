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
// address on any path through its code (#1415's review). Each instruction's depth on entry is its predecessors' along
// the control flow, from 0 at the first byte. The edges come from x86_effects' kinds: on to the next instruction, a
// jump's or a branch's target within the function, none after a jump, a return or a trap. In an object, a jump whose
// displacement is a relocation's (a tail call's, which reads as a jump to the next instruction before it is linked)
// leaves the function. Each push and `sub rsp, imm`
// (or `lea rsp, [rsp - imm]`) adds, each pop and `add rsp, imm` takes back, and `mov rsp, rbp` (or `lea rsp, [rbp +
// imm]`, or `leave`) goes back to the depth `mov rbp, rsp` set rbp at. The frame is the most a depth reaches, so a
// prologue after an early return (as clang's shrink-wrapping places it) counts, and an early return's epilogue takes
// nothing from another path's frame. A dynamic allocation (`sub rsp, reg`) and the moves of rsp that restore one count
// nothing; a realignment (`and rsp, -imm`) is its alignment apart; whether the function sets a frame pointer (`mov rbp,
// rsp`) too.
//
// A function's frame has a state:
// - resolved: every reached instruction's depth is known and agrees;
// - assumed: the function has an indirect jump, and code no edge reaches (a jump table's targets) took the deepest depth
//   an indirect jump leaves; without an indirect jump, such code is unreachable (padding, or after a call that doesn't
//   return) and counts nothing;
// - unresolved: two predecessors leave different depths, a path pops past its return address, or it moves rsp back to an
//   rbp it never set. Its frame is the most it found.
// A function whose code doesn't decode whole is also partial, its frame what decoded. With --functions, a line per
// function:
//
//   frame LABEL NAME BYTES ALIGNMENT POINTER PARTIAL STATE
//
// Then a line per FILE against the first: `frames LABEL`, its functions, their frames' sum and how many are assumed,
// unresolved and partial; and over the functions whose names both have once each (a name twice in a file is left out, as
// two static functions of an executable), but those unresolved or partial in either (`excluded`), how many, both sums and
// their ratio, the median of the functions' ratios ((bytes + 8) / (first's bytes + 8): the return address counted, so
// that two empty frames are 1), and how many are larger, equal and smaller. Tab-separated.

#include <elf.h>

#define BUSTER_FRAME_MEASURE_FILES 8
// No depth yet: an instruction no path has reached.
#define BUSTER_FRAME_MEASURE_UNSET INT64_MIN

typedef enum FrameMeasureState
{
    FRAME_MEASURE_RESOLVED,
    FRAME_MEASURE_ASSUMED,
    FRAME_MEASURE_UNRESOLVED,
} FrameMeasureState;

static const char* const frame_measure_states[3] = { "resolved", "assumed", "unresolved" };

typedef struct FrameMeasureFunction FrameMeasureFunction;
struct FrameMeasureFunction
{
    const char* name;
    uint64_t bytes;
    uint32_t alignment;
    bool pointer;
    bool partial;
    FrameMeasureState state;
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

// The depth after one instruction from `depth` before it (the file's comment). *rbp is the depth `mov rbp, rsp` set
// rbp at (UNSET before), *unresolved set where rsp goes back to an unset rbp or rbp is set at two depths.
static int64_t frame_measure_step(const X86Instruction* instruction, int64_t depth, int64_t* rbp, bool* unresolved, FrameMeasureFunction* function)
{
    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
    const bool direct = instruction->mode == BUSTER_X86_REGISTER_DIRECT;
    const bool rsp = direct && instruction->rm == X86_REGISTER_RSP;
    const bool sub = rsp && (form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32);
    const bool add = rsp && (form == X86_FORM_ADD_RM64_IMM8 || form == X86_FORM_ADD_RM64_IMM32);
    const bool align = rsp && (form == X86_FORM_AND_RM64_IMM8 || form == X86_FORM_AND_RM64_IMM32) && instruction->immediate < 0;
    const bool lea = form == X86_FORM_LEA_R64_M && instruction->reg == X86_REGISTER_RSP && instruction->mode == BUSTER_X86_MEMORY_BASE;
    const bool set_rbp = direct && ((form == X86_FORM_MOV_RM64_R64 && instruction->rm == X86_REGISTER_RBP && instruction->reg == X86_REGISTER_RSP) ||
                                    (form == X86_FORM_MOV_R64_RM64 && instruction->reg == X86_REGISTER_RBP && instruction->rm == X86_REGISTER_RSP));
    const bool mov_from_rbp = direct && ((form == X86_FORM_MOV_RM64_R64 && instruction->rm == X86_REGISTER_RSP && instruction->reg == X86_REGISTER_RBP) ||
                                         (form == X86_FORM_MOV_R64_RM64 && instruction->reg == X86_REGISTER_RSP && instruction->rm == X86_REGISTER_RBP));
    const bool from_rbp = mov_from_rbp || (lea && instruction->rm == X86_REGISTER_RBP) || form == X86_FORM_LEAVE;
    // Every 64-bit push and pop moves rsp by 8: a register's, an immediate's (a stack argument's, as clang pushes them),
    // memory's and the flags'.
    const bool push = form == X86_FORM_PUSH_R64 || form == X86_FORM_PUSH_IMM8 || form == X86_FORM_PUSH_IMM32 || form == X86_FORM_PUSH_RM64 || form == X86_FORM_PUSHFQ;
    const bool pop = form == X86_FORM_POP_R64 || form == X86_FORM_POPFQ;
    int64_t result = depth;
    result += push ? 8 : pop ? -8 : 0;
    result += sub ? instruction->immediate : add ? -(int64_t)instruction->immediate : lea && instruction->rm == X86_REGISTER_RSP ? -(int64_t)instruction->displacement : 0;
    if (set_rbp)
    {
        *unresolved |= *rbp != BUSTER_FRAME_MEASURE_UNSET && *rbp != depth;
        *rbp = depth;
        function->pointer = true;
    }
    if (from_rbp)
    {
        // leave is `mov rsp, rbp` then `pop rbp`; `lea rsp, [rbp + imm]` is rbp's depth less imm.
        *unresolved |= *rbp == BUSTER_FRAME_MEASURE_UNSET;
        result = *rbp == BUSTER_FRAME_MEASURE_UNSET ? depth : form == X86_FORM_LEAVE ? *rbp - 8 : lea ? *rbp - instruction->displacement : *rbp;
    }
    const uint32_t realignment = align ? (uint32_t)-(int64_t)instruction->immediate : 0;
    function->alignment = realignment > function->alignment ? realignment : function->alignment;
    return result;
}

// The index of the instruction at `offset` among `count` decoded at ascending offsets, or `count` for none.
static uint32_t frame_measure_find(const X86Decoded* decoded, uint32_t count, int64_t offset)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = (int64_t)decoded[middle].offset < offset;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < count && (int64_t)decoded[low].offset == offset ? low : count;
}

// The depth `depth` given to instruction `s` (below `count`), which work's `pending` entries list until they are
// stepped: where it already has another, the function is unresolved and the deeper one goes on.
static void frame_measure_reach(int64_t* depths, uint32_t* work, uint32_t* pending, uint32_t s, uint32_t count, int64_t depth, bool* unresolved)
{
    if (s < count && depths[s] == BUSTER_FRAME_MEASURE_UNSET)
    {
        depths[s] = depth;
        work[*pending] = s;
        *pending += 1;
    }
    else if (s < count && depths[s] != depth)
    {
        *unresolved = true;
        if (depth > depths[s])
        {
            depths[s] = depth;
            work[*pending] = s;
            *pending += 1;
        }
    }
}

// Whether `field`, a jump's displacement's place as a relocation key (its section's index in the high bits, its offset
// in the section in the low), is a relocation's, among `count` keys in ascending order: in an object, a jump to another
// function (a tail call) has its displacement there, and leaves the function wherever its unrelocated bytes point.
static bool frame_measure_relocated(const uint64_t* relocations, uint32_t count, uint64_t field)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = relocations[middle] < field;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < count && relocations[low] == field;
}

// The frame of `size` bytes of code at `code` (the file's comment), whose first byte's relocation key is `start` (its
// section's index in the high bits, its offset in the section in the low), among the `relocation_count` keys at
// `relocations` (an object's, ascending; none in an executable). Its decoded instructions and their depths are in
// `scratch`, given back after.
static FrameMeasureFunction frame_measure_code(const uint8_t* code, uint64_t size, uint64_t start, const uint64_t* relocations, uint32_t relocation_count, Arena* scratch)
{
    FrameMeasureFunction result = { 0 };
    const uint64_t mark = scratch->length;
    X86Decoded* decoded = BUSTER_ARENA_PUSH_ARRAY(scratch, X86Decoded, size + 1);
    int64_t* depths = BUSTER_ARENA_PUSH_ARRAY(scratch, int64_t, size + 1);
    // The work list: each step takes one entry and gives at most two, and the steps stop past 2 * size + 2, so it never
    // holds more than 2 * size + 4 (a seed's among them).
    uint32_t* work = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 4 * size + 8);
    const bool ready = decoded != NULL && depths != NULL && work != NULL;
    uint32_t count = 0;
    uint64_t consumed = 0;
    bool decoding = ready;
    while (decoding && consumed < size)
    {
        const X86DecodeResult step = x86_disassemble(code + consumed, size - consumed, decoded + count, size + 1 - count);
        for (uint64_t i = 0; i < step.count; i += 1)
        {
            decoded[count + i].offset += (uint32_t)consumed;
        }
        count += (uint32_t)step.count;
        consumed += step.consumed;
        decoding = step.status == X86_DECODE_OK && step.consumed != 0;
    }
    result.partial = !ready || consumed < size;
    for (uint32_t i = 0; i < count; i += 1)
    {
        depths[i] = BUSTER_FRAME_MEASURE_UNSET;
    }
    int64_t most = 0;
    int64_t rbp = BUSTER_FRAME_MEASURE_UNSET;
    // The deepest depth an indirect jump leaves, which code no edge reaches takes.
    int64_t indirect = BUSTER_FRAME_MEASURE_UNSET;
    bool unresolved = false;
    bool assumed = false;
    uint32_t pending = 0;
    uint32_t steps = 0;
    frame_measure_reach(depths, work, &pending, 0, count, 0, &unresolved);
    uint32_t unreached = 0;
    // A depth only rises, and a function is unresolved once one does, so the steps are bounded; past twice the edges, the
    // work stops, unresolved.
    while (pending != 0 && steps <= 2 * size + 2)
    {
        pending -= 1;
        steps += 1;
        const uint32_t i = work[pending];
        const X86Instruction* instruction = &decoded[i].instruction;
        const int64_t after = frame_measure_step(instruction, depths[i], &rbp, &unresolved, &result);
        unresolved |= after < 0;
        most = after > most ? after : most;
        const uint32_t control = x86_effects(instruction->form)->kinds >> 4;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool relative = x86_form_descriptions[form].relative_bytes != 0;
        const bool transfer = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
        const bool on = control != BUSTER_X86_CONTROL_JUMP && control != BUSTER_X86_CONTROL_RETURN && control != BUSTER_X86_CONTROL_TRAP;
        if (control == BUSTER_X86_CONTROL_JUMP && !relative)
        {
            indirect = after > indirect ? after : indirect;
        }
        // A target outside the function (a tail call's) is no edge of it, nor a relocated displacement's.
        const int64_t goal = (int64_t)decoded[i].offset + decoded[i].length + instruction->displacement;
        const uint64_t field = start + decoded[i].offset + decoded[i].length - x86_form_descriptions[form].relative_bytes;
        const bool external = relative && frame_measure_relocated(relocations, relocation_count, field);
        frame_measure_reach(depths, work, &pending, transfer && relative && !external ? frame_measure_find(decoded, count, goal) : count, count, after, &unresolved);
        frame_measure_reach(depths, work, &pending, on ? i + 1 : count, count, after, &unresolved);
        // Once every path is done, code no edge reached takes an indirect jump's depth, a run at a time; padding (nops,
        // `xchg ax, ax` the 2-byte one among them, and traps, as after an unconditional jump to align a loop's head)
        // takes none, whatever it falls into.
        while (pending == 0 && indirect != BUSTER_FRAME_MEASURE_UNSET && unreached < count)
        {
            const X86Instruction* gap = &decoded[unreached].instruction;
            const X86Effects* effects = x86_effects(gap->form);
            const bool xchg_nop = (gap->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_XCHG_R16_AX && gap->rm == X86_REGISTER_RAX && gap->reg == X86_REGISTER_RAX;
            const bool padding = (effects->kinds >> 4) == BUSTER_X86_CONTROL_TRAP || effects->operation == X86_OPERATION_NOP || xchg_nop;
            if (depths[unreached] == BUSTER_FRAME_MEASURE_UNSET && !padding)
            {
                frame_measure_reach(depths, work, &pending, unreached, count, indirect, &unresolved);
                assumed = true;
            }
            unreached += 1;
        }
    }
    unresolved |= pending != 0;
    result.bytes = (uint64_t)most;
    result.state = unresolved ? FRAME_MEASURE_UNRESOLVED : assumed ? FRAME_MEASURE_ASSUMED : FRAME_MEASURE_RESOLVED;
    arena_pop_to(scratch, mark);
    return result;
}

static int frame_measure_keys(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
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
    // An object's relocations' places, as keys of their section's index in the high bits and the offset in the low, sorted
    // (frame_measure_relocated takes a section's run of them).
    uint64_t relocation_count = 0;
    for (uint32_t s = 0; valid && header->e_type == ET_REL && s < header->e_shnum; s += 1)
    {
        const Elf64_Shdr* section = frame_measure_section(file, s);
        const bool rela = section->sh_type == SHT_RELA && section->sh_entsize == sizeof(Elf64_Rela) && section->sh_offset + section->sh_size <= file->size;
        relocation_count += rela ? section->sh_size / sizeof(Elf64_Rela) : 0;
    }
    uint64_t* relocations = relocation_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, relocation_count) : NULL;
    valid = valid && (relocation_count == 0 || relocations != NULL);
    uint64_t relocated = 0;
    for (uint32_t s = 0; valid && relocation_count != 0 && s < header->e_shnum; s += 1)
    {
        const Elf64_Shdr* section = frame_measure_section(file, s);
        const bool rela = section->sh_type == SHT_RELA && section->sh_entsize == sizeof(Elf64_Rela) && section->sh_offset + section->sh_size <= file->size;
        for (uint64_t r = 0; rela && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            const Elf64_Rela* relocation = (const Elf64_Rela*)(file->bytes + section->sh_offset) + r;
            relocations[relocated] = (uint64_t)section->sh_info << 40 | (relocation->r_offset & ((UINT64_C(1) << 40) - 1));
            relocated += 1;
        }
    }
    if (relocated != 0)
    {
        qsort(relocations, relocated, sizeof(uint64_t), frame_measure_keys);
    }
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
            // Its section's relocations are the sorted keys with its index in the high bits.
            FrameMeasureFunction* function = &file->functions[file->count];
            *function = frame_measure_code(file->bytes + section->sh_offset + start, symbol->st_size, (uint64_t)symbol->st_shndx << 40 | start, relocations,
                                           (uint32_t)relocated, arena);
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

// Whether a function's frame is one to compare: resolved or assumed, and decoded whole.
static bool frame_measure_comparable(const FrameMeasureFunction* function)
{
    return function->state != FRAME_MEASURE_UNRESOLVED && !function->partial;
}

// The line of `file` against `first` (the file's comment), its functions' ratios' room in `ratios`.
static void frame_measure_compare(const FrameMeasureFile* first, const FrameMeasureFile* file, double* ratios)
{
    uint64_t sum = 0;
    uint32_t assumed = 0;
    uint32_t unresolved = 0;
    uint32_t partial = 0;
    for (uint32_t i = 0; i < file->count; i += 1)
    {
        sum += file->functions[i].bytes;
        assumed += file->functions[i].state == FRAME_MEASURE_ASSUMED;
        unresolved += file->functions[i].state == FRAME_MEASURE_UNRESOLVED;
        partial += file->functions[i].partial;
    }
    uint32_t common = 0;
    uint32_t excluded = 0;
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
        const bool both = order == 0 && left->unique && right->unique;
        const bool compared = both && frame_measure_comparable(left) && frame_measure_comparable(right);
        excluded += both && !compared;
        if (compared)
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
    printf("frames\t%s\tfunctions=%u\tbytes=%" PRIu64 "\tassumed=%u\tunresolved=%u\tpartial=%u\tagainst=%s\tcommon=%u\texcluded=%u\tbytes_%s=%" PRIu64
           "\tbytes_%s=%" PRIu64 "\tsum_ratio=%.4f\tmedian_ratio=%.4f\tlarger=%u\tequal=%u\tsmaller=%u\n",
           file->label, file->count, sum, assumed, unresolved, partial, first->label, common, excluded, first->label, first_sum, file->label, file_sum,
           first_sum == 0 ? 0.0 : (double)file_sum / (double)first_sum, median, larger, equal, smaller);
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
    valid = valid && arena.base != NULL && x86_disassembler_initialize() && (x86_semantics_initialized || x86_semantics_initialize());
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
            printf("frame\t%s\t%s\t%" PRIu64 "\t%u\t%u\t%u\t%s\n", files[f].label, function->name, function->bytes, function->alignment, function->pointer, function->partial,
                   frame_measure_states[function->state]);
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
