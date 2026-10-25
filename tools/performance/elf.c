/* Original bounded ELF64 function/build-ID reader for matching sample code.
 * Symbols are used only from the supplied exact binary. No libelf, libbfd,
 * DWARF engine or external symbolizer is required. Missing source/line info is
 * retained as unknown; this reader does not invent it from a symbol spelling.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <elf.h>

typedef struct ProfileElfSymbol ProfileElfSymbol;
struct ProfileElfSymbol
{
    uint64_t address;
    uint64_t size;
    PerfBytes name;
};

typedef struct ProfileElf ProfileElf;
struct ProfileElf
{
    PerfBuffer bytes;
    Elf64_Ehdr header;
    ProfileElfSymbol *symbols;
    size_t symbol_count;
    PerfBytes build_id;
    char sha256[65];
    char path[4096];
};

static bool profile_elf_section(const ProfileElf *elf, uint32_t index, Elf64_Shdr *section)
{
    uint64_t offset = elf->header.e_shoff + (uint64_t)index * sizeof(*section);
    bool result = index < elf->header.e_shnum && perf_range(perf_buffer_bytes(&elf->bytes), offset, sizeof(*section));
    if (result)
    {
        memcpy(section, elf->bytes.data + offset, sizeof(*section));
        result = section->sh_type == SHT_NOBITS || perf_range(perf_buffer_bytes(&elf->bytes), section->sh_offset, section->sh_size);
    }
    return result;
}

static int profile_elf_symbol_compare(const void *left, const void *right)
{
    const ProfileElfSymbol *a = left;
    const ProfileElfSymbol *b = right;
    int result = a->address < b->address ? -1 : a->address > b->address ? 1 : a->size > b->size ? -1 : a->size < b->size ? 1 : perf_json_key_compare(&a->name, &b->name);
    return result;
}

static bool profile_elf_load(const char *path, ProfileElf *elf)
{
    bool result = realpath(path, elf->path) != NULL && perf_read_file(elf->path, &elf->bytes) && elf->bytes.size >= sizeof(elf->header);
    if (result)
    {
        memcpy(&elf->header, elf->bytes.data, sizeof(elf->header));
        result = memcmp(elf->header.e_ident, ELFMAG, SELFMAG) == 0 && elf->header.e_ident[EI_CLASS] == ELFCLASS64 && elf->header.e_ident[EI_DATA] == ELFDATA2LSB && elf->header.e_machine == EM_X86_64 && (elf->header.e_type == ET_DYN || elf->header.e_type == ET_EXEC) && elf->header.e_shentsize == sizeof(Elf64_Shdr) && elf->header.e_shnum <= BUSTER_PERF_FIELD_LIMIT && elf->header.e_phentsize == sizeof(Elf64_Phdr) && elf->header.e_phnum <= BUSTER_PERF_FIELD_LIMIT && perf_range(perf_buffer_bytes(&elf->bytes), elf->header.e_phoff, (uint64_t)elf->header.e_phnum * sizeof(Elf64_Phdr)) && perf_sha256(perf_buffer_bytes(&elf->bytes), elf->sha256);
    }
    size_t capacity = 0;
    size_t inspected = 0;
    for (uint32_t s = 0; s < elf->header.e_shnum && result; ++s)
    {
        Elf64_Shdr section;
        result = profile_elf_section(elf, s, &section);
        if (!result)
        {
            break;
        }
        if (section.sh_type == SHT_NOTE)
        {
            uint64_t cursor = section.sh_offset;
            uint64_t end = cursor + section.sh_size;
            while (cursor < end && result)
            {
                result = ++inspected <= BUSTER_PERF_NODE_LIMIT * 4;
                Elf64_Nhdr note;
                result = result && end - cursor >= sizeof(note);
                if (!result)
                {
                    break;
                }
                memcpy(&note, elf->bytes.data + cursor, sizeof(note));
                cursor += sizeof(note);
                uint64_t names = ((uint64_t)note.n_namesz + 3) & ~UINT64_C(3);
                uint64_t bytes = ((uint64_t)note.n_descsz + 3) & ~UINT64_C(3);
                result = names <= end - cursor && bytes <= end - cursor - names;
                if (result && note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && memcmp(elf->bytes.data + cursor, "GNU", 4) == 0)
                {
                    elf->build_id = (PerfBytes){.data = elf->bytes.data + cursor + names, .size = note.n_descsz};
                }
                cursor += names + bytes;
            }
        }
        if (section.sh_type == SHT_SYMTAB || section.sh_type == SHT_DYNSYM)
        {
            Elf64_Shdr strings;
            result = section.sh_entsize == sizeof(Elf64_Sym) && section.sh_size % sizeof(Elf64_Sym) == 0 && profile_elf_section(elf, section.sh_link, &strings) && strings.sh_type == SHT_STRTAB;
            for (uint64_t n = 0; n < section.sh_size / sizeof(Elf64_Sym) && result; ++n)
            {
                result = ++inspected <= BUSTER_PERF_NODE_LIMIT * 4;
                if (!result)
                {
                    break;
                }
                Elf64_Sym symbol;
                memcpy(&symbol, elf->bytes.data + section.sh_offset + n * sizeof(symbol), sizeof(symbol));
                if (ELF64_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_shndx == SHN_UNDEF || symbol.st_size == 0)
                {
                    continue;
                }
                result = symbol.st_name < strings.sh_size && symbol.st_size <= UINT64_MAX - symbol.st_value;
                const unsigned char *name = result ? elf->bytes.data + strings.sh_offset + symbol.st_name : NULL;
                const unsigned char *end = result ? memchr(name, 0, (size_t)(strings.sh_size - symbol.st_name)) : NULL;
                result = result && end != NULL;
                if (result && elf->symbol_count == capacity)
                {
                    size_t next = capacity == 0 ? 256 : capacity * 2;
                    result = next <= BUSTER_PERF_NODE_LIMIT;
                    ProfileElfSymbol *grown = result ? realloc(elf->symbols, next * sizeof(*grown)) : NULL;
                    result = result && grown != NULL;
                    if (result)
                    {
                        elf->symbols = grown;
                        capacity = next;
                    }
                }
                if (result)
                {
                    PerfBytes text = {.data = name, .size = (size_t)(end - name)};
                    result = perf_utf8(text);
                    elf->symbols[elf->symbol_count++] = (ProfileElfSymbol){.address = symbol.st_value, .size = symbol.st_size, .name = text};
                }
            }
        }
    }
    if (result && elf->symbol_count > 1)
    {
        qsort(elf->symbols, elf->symbol_count, sizeof(*elf->symbols), profile_elf_symbol_compare);
    }
    return result;
}

static const ProfileElfSymbol *profile_elf_symbol(const ProfileElf *elf, uint64_t address)
{
    size_t low = 0;
    size_t high = elf->symbol_count;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        if (elf->symbols[middle].address <= address)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const ProfileElfSymbol *result = low != 0 && address - elf->symbols[low - 1].address < elf->symbols[low - 1].size ? &elf->symbols[low - 1] : NULL;
    return result;
}

static bool profile_elf_pc(const ProfileElf *elf, uint64_t file_offset, uint64_t *address, uint64_t *load_bias)
{
    bool result = false;
    for (uint32_t i = 0; i < elf->header.e_phnum && !result; ++i)
    {
        Elf64_Phdr program;
        memcpy(&program, elf->bytes.data + elf->header.e_phoff + (uint64_t)i * sizeof(program), sizeof(program));
        if (program.p_type == PT_LOAD && (program.p_flags & PF_X) != 0 && file_offset >= program.p_offset && file_offset - program.p_offset < program.p_filesz)
        {
            uint64_t relative = file_offset - program.p_offset;
            result = relative <= UINT64_MAX - program.p_vaddr && program.p_vaddr >= program.p_offset;
            if (result)
            {
                *address = program.p_vaddr + relative;
                *load_bias = program.p_vaddr - program.p_offset;
            }
        }
    }
    return result;
}

static void profile_elf_free(ProfileElf *elf)
{
    free(elf->symbols);
    free(elf->bytes.data);
}
