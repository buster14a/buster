// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The function sections of a unit's native parts that no live code refers to (docs/commands/build.md, "Units in
// parts"):
//
//   unreferenced SCRIPT PART...
//
// writes SCRIPT, the lines of a linker script's /DISCARD/ that the join (BusterCodegenMerge.cmake) gives its
// relocatable link: "PART"(SECTION), each a section of PART that holds a function the unit defined locally, which
// presplit (tools/codegen/presplit.c) left in its part because another part named it, through its alias
// (NAME.buster_split, which the part's optimizer gives the function itself where no other name of it is left), or
// that its part's optimizer kept. The whole unit's optimizer drops a local function once every
// call is inlined or folded away; a part can't know that the other parts did so too, and its alias keeps the function:
// the checker's unit kept 311 bodies, 150 KB, that nothing called (tier1_select_cover_munch, 65 KB: its one call folds
// away after inlining), among the code that runs.
//
// A section is live when a live section refers to it: every allocated section other than a candidate is (the unit's
// global functions and data, a group's, one SHF_GNU_RETAIN keeps or SHF_LINK_ORDER ties, the module's own assembly's),
// and .eh_frame, whose entries name every function, isn't. A candidate holds no global symbol but presplit's aliases,
// in no group and not retained: a code section named after a function it defines (.text.NAME, .text.unlikely.NAME), or
// a data section the code generator named for one object's own (.rodata.NAME, .data.NAME, .bss.NAME, .rodata.str1.1:
// a variable's, a function's jump tables, constants and strings), which goes with the code that alone named it. A
// reference by symbol resolves in the part that defines it, a local or section symbol in its own part. The relocatable
// link then drops each dead section, its symbols, its unwind entry and its debug information's references (an address
// of 0, as --gc-sections leaves). Writes an empty SCRIPT where nothing is dead. Fails, saying why, for a file that isn't a 64-bit little-endian relocatable ELF
// object, or one with a path a linker script can't quote. Build tooling outside the compiler's compile path, with no
// vector path.

#include <elf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNREFERENCED_ALIAS_SUFFIX ".buster_split"
// SHF_GNU_RETAIN, which older elf.h headers lack.
#define UNREFERENCED_RETAIN 0x200000

typedef struct UnreferencedFile UnreferencedFile;
struct UnreferencedFile
{
    const char* path;
    uint8_t* data;
    uint64_t size;
    uint64_t section_count;
    const Elf64_Shdr* sections;
    const char* section_names;
    uint64_t section_names_size;
    const Elf64_Sym* symbols;
    uint64_t symbol_count;
    const char* symbol_names;
    uint64_t symbol_names_size;
    // SHT_SYMTAB_SHNDX's entries, where a symbol's section index is SHN_XINDEX.
    const uint32_t* extended;
    // Per section: its relocation section (0 for none), whether it's a candidate, and whether it's live.
    uint64_t* relocations;
    bool* candidate;
    bool* live;
};

// A global definition: its name, file and section.
typedef struct UnreferencedGlobal UnreferencedGlobal;
struct UnreferencedGlobal
{
    const char* name;
    uint32_t file;
    uint64_t section;
};

typedef struct UnreferencedItem UnreferencedItem;
struct UnreferencedItem
{
    uint32_t file;
    uint64_t section;
};

static bool unreferenced_read(const char* path, UnreferencedFile* file)
{
    FILE* stream = fopen(path, "rb");
    bool read = false;
    if (stream != 0 && fseek(stream, 0, SEEK_END) == 0)
    {
        const long size = ftell(stream);
        if (size > 0 && fseek(stream, 0, SEEK_SET) == 0)
        {
            file->size = (uint64_t)size;
            file->data = malloc(file->size);
            read = file->data != 0 && fread(file->data, 1, file->size, stream) == file->size;
        }
    }
    if (stream != 0)
    {
        fclose(stream);
    }
    if (!read)
    {
        fprintf(stderr, "unreferenced: can't read %s\n", path);
    }
    file->path = path;
    return read;
}

// Whether the `size` bytes at `offset` lie within the file.
static bool unreferenced_inside(const UnreferencedFile* file, uint64_t offset, uint64_t size)
{
    return offset <= file->size && size <= file->size - offset;
}

static const char* unreferenced_string(const char* table, uint64_t size, uint32_t offset)
{
    return offset < size && memchr(table + offset, 0, size - offset) != 0 ? table + offset : 0;
}

// Reads the file's headers, symbol table and relocation sections; whether it is a relocatable x86-64 ELF object whose
// tables lie within it.
static bool unreferenced_parse(UnreferencedFile* file)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file->data;
    bool valid = file->size >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                 header->e_ident[EI_CLASS] == ELFCLASS64 && header->e_ident[EI_DATA] == ELFDATA2LSB && header->e_type == ET_REL &&
                 header->e_shentsize == sizeof(Elf64_Shdr) && unreferenced_inside(file, header->e_shoff, sizeof(Elf64_Shdr));
    if (valid)
    {
        file->sections = (const Elf64_Shdr*)(file->data + header->e_shoff);
        file->section_count = header->e_shnum != 0 ? header->e_shnum : file->sections[0].sh_size;
        valid = file->section_count <= (file->size - header->e_shoff) / sizeof(Elf64_Shdr);
    }
    uint64_t names = 0;
    if (valid)
    {
        names = header->e_shstrndx != SHN_XINDEX ? header->e_shstrndx : file->sections[0].sh_link;
        valid = names < file->section_count && unreferenced_inside(file, file->sections[names].sh_offset, file->sections[names].sh_size);
    }
    if (valid)
    {
        file->section_names = (const char*)file->data + file->sections[names].sh_offset;
        file->section_names_size = file->sections[names].sh_size;
        file->relocations = calloc(file->section_count, sizeof(uint64_t));
        file->candidate = calloc(file->section_count, sizeof(bool));
        file->live = calloc(file->section_count, sizeof(bool));
        valid = file->relocations != 0 && file->candidate != 0 && file->live != 0;
    }
    for (uint64_t i = 0; valid && i < file->section_count; i += 1)
    {
        const Elf64_Shdr* section = &file->sections[i];
        valid = section->sh_type == SHT_NOBITS || unreferenced_inside(file, section->sh_offset, section->sh_size);
        if (valid && section->sh_type == SHT_SYMTAB)
        {
            valid = file->symbols == 0 && section->sh_link < file->section_count && section->sh_entsize == sizeof(Elf64_Sym) &&
                    unreferenced_inside(file, file->sections[section->sh_link].sh_offset, file->sections[section->sh_link].sh_size);
            if (valid)
            {
                file->symbols = (const Elf64_Sym*)(file->data + section->sh_offset);
                file->symbol_count = section->sh_size / sizeof(Elf64_Sym);
                file->symbol_names = (const char*)file->data + file->sections[section->sh_link].sh_offset;
                file->symbol_names_size = file->sections[section->sh_link].sh_size;
            }
        }
        else if (valid && section->sh_type == SHT_SYMTAB_SHNDX)
        {
            file->extended = (const uint32_t*)(file->data + section->sh_offset);
        }
        else if (valid && section->sh_type == SHT_RELA)
        {
            valid = section->sh_info < file->section_count && section->sh_entsize == sizeof(Elf64_Rela) &&
                    file->relocations[section->sh_info] == 0;
            if (valid)
            {
                file->relocations[section->sh_info] = i;
            }
        }
        else if (valid && section->sh_type == SHT_REL)
        {
            valid = false;
        }
    }
    valid = valid && file->symbols != 0;
    if (!valid)
    {
        fprintf(stderr, "unreferenced: %s isn't a 64-bit little-endian relocatable ELF object with one symbol table and RELA relocations\n",
                file->path);
    }
    return valid;
}

static const char* unreferenced_section_name(const UnreferencedFile* file, uint64_t section)
{
    return unreferenced_string(file->section_names, file->section_names_size, file->sections[section].sh_name);
}

static const char* unreferenced_symbol_name(const UnreferencedFile* file, uint64_t symbol)
{
    return unreferenced_string(file->symbol_names, file->symbol_names_size, file->symbols[symbol].st_name);
}

// The section that defines `symbol`, or 0 for an undefined, absolute or common one.
static uint64_t unreferenced_symbol_section(const UnreferencedFile* file, uint64_t symbol)
{
    const uint16_t index = file->symbols[symbol].st_shndx;
    uint64_t section = index == SHN_XINDEX && file->extended != 0 ? file->extended[symbol] : index;
    if (index == SHN_UNDEF || (index >= SHN_LORESERVE && index != SHN_XINDEX) || section >= file->section_count)
    {
        section = 0;
    }
    return section;
}

static bool unreferenced_ends_with(const char* text, const char* suffix)
{
    const size_t length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    return length >= suffix_length && memcmp(text + length - suffix_length, suffix, suffix_length) == 0;
}

// Marks the file's candidates: code sections named after a local function they define (.text.NAME or
// .text.PREFIX.NAME), holding no global symbol but presplit's aliases, in no group and not retained.
static void unreferenced_candidates(UnreferencedFile* file)
{
    bool* global = calloc(file->section_count, sizeof(bool));
    bool* named = calloc(file->section_count, sizeof(bool));
    for (uint64_t i = 1; i < file->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &file->symbols[i];
        const uint64_t section = unreferenced_symbol_section(file, i);
        const char* name = unreferenced_symbol_name(file, i);
        if (section != 0 && name != 0 && ELF64_ST_TYPE(symbol->st_info) != STT_SECTION)
        {
            if (ELF64_ST_BIND(symbol->st_info) != STB_LOCAL && !unreferenced_ends_with(name, UNREFERENCED_ALIAS_SUFFIX))
            {
                global[section] = true;
            }
            else if (ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_value == 0)
            {
                // A local function, or one its part's optimizer gave its alias's name (its only name left there).
                // .text.NAME, or the code generator's prefixed section for it.
                static const char* const prefixes[] = { ".text.", ".text.unlikely.", ".text.hot.", ".text.startup.", ".text.exit.", ".text.split." };
                const char* section_name = unreferenced_section_name(file, section);
                for (uint32_t p = 0; section_name != 0 && p < sizeof(prefixes) / sizeof(prefixes[0]); p += 1)
                {
                    const size_t length = strlen(prefixes[p]);
                    named[section] = named[section] || (strncmp(section_name, prefixes[p], length) == 0 && strcmp(section_name + length, name) == 0);
                }
            }
        }
    }
    // The code generator's own data sections of one object (a variable's, a function's jump tables, its constants and
    // strings), which only that object's relocations can name.
    static const char* const data[] = { ".rodata.", ".data.", ".bss.", ".tdata.", ".tbss.", ".lrodata.", ".ldata.", ".lbss." };
    for (uint64_t i = 1; i < file->section_count; i += 1)
    {
        const Elf64_Shdr* section = &file->sections[i];
        const char* name = unreferenced_section_name(file, i);
        const bool plain = !global[i] && (section->sh_flags & SHF_ALLOC) != 0 && (section->sh_flags & SHF_GROUP) == 0 &&
                           (section->sh_flags & UNREFERENCED_RETAIN) == 0 && (section->sh_flags & SHF_LINK_ORDER) == 0;
        bool local_data = false;
        for (uint32_t p = 0; name != 0 && p < sizeof(data) / sizeof(data[0]); p += 1)
        {
            const size_t length = strlen(data[p]);
            local_data = local_data || (strncmp(name, data[p], length) == 0 && name[length] != 0);
        }
        const bool code = named[i] && section->sh_type == SHT_PROGBITS && (section->sh_flags & SHF_EXECINSTR) != 0;
        local_data = local_data && (section->sh_type == SHT_PROGBITS || section->sh_type == SHT_NOBITS) && (section->sh_flags & SHF_EXECINSTR) == 0;
        file->candidate[i] = plain && (code || local_data);
    }
    free(global);
    free(named);
}

static int unreferenced_compare(const void* a, const void* b)
{
    return strcmp(((const UnreferencedGlobal*)a)->name, ((const UnreferencedGlobal*)b)->name);
}

// Whether the path can stand quoted in a linker script.
static bool unreferenced_quotable(const char* path)
{
    return strpbrk(path, "\"\n") == 0;
}

int main(int argc, char** argv)
{
    int status = 1;
    if (argc < 3)
    {
        fprintf(stderr, "usage: unreferenced SCRIPT PART...\n");
        status = 2;
    }
    else
    {
        const uint32_t count = (uint32_t)(argc - 2);
        UnreferencedFile* files = calloc(count, sizeof(UnreferencedFile));
        bool ready = files != 0;
        uint64_t global_count = 0;
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            ready = unreferenced_quotable(argv[f + 2]) && unreferenced_read(argv[f + 2], &files[f]) && unreferenced_parse(&files[f]);
            if (ready)
            {
                unreferenced_candidates(&files[f]);
                global_count += files[f].symbol_count;
            }
            else if (!unreferenced_quotable(argv[f + 2]))
            {
                fprintf(stderr, "unreferenced: a linker script can't quote %s\n", argv[f + 2]);
            }
        }
        // The parts' global definitions, sorted by name.
        UnreferencedGlobal* globals = ready ? calloc(global_count + 1, sizeof(UnreferencedGlobal)) : 0;
        uint64_t defined = 0;
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            for (uint64_t i = 1; i < files[f].symbol_count; i += 1)
            {
                const uint64_t section = unreferenced_symbol_section(&files[f], i);
                const char* name = unreferenced_symbol_name(&files[f], i);
                if (section != 0 && name != 0 && ELF64_ST_BIND(files[f].symbols[i].st_info) != STB_LOCAL)
                {
                    globals[defined] = (UnreferencedGlobal){
                        .name = name,
                        .file = f,
                        .section = section,
                    };
                    defined += 1;
                }
            }
        }
        if (ready && defined != 0)
        {
            qsort(globals, defined, sizeof(UnreferencedGlobal), unreferenced_compare);
        }
        // The roots, then what their relocations reach, to a fixed point.
        uint64_t item_count = 0;
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            item_count += files[f].section_count;
        }
        UnreferencedItem* items = ready ? calloc(item_count + 1, sizeof(UnreferencedItem)) : 0;
        uint64_t pending = 0;
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            for (uint64_t i = 1; i < files[f].section_count; i += 1)
            {
                const char* name = unreferenced_section_name(&files[f], i);
                const bool unwind = files[f].sections[i].sh_type == SHT_X86_64_UNWIND || (name != 0 && strcmp(name, ".eh_frame") == 0);
                if ((files[f].sections[i].sh_flags & SHF_ALLOC) != 0 && !files[f].candidate[i] && !unwind)
                {
                    files[f].live[i] = true;
                    items[pending] = (UnreferencedItem){
                        .file = f,
                        .section = i,
                    };
                    pending += 1;
                }
            }
        }
        while (ready && pending != 0)
        {
            pending -= 1;
            const UnreferencedFile* file = &files[items[pending].file];
            const uint64_t relocations = file->relocations[items[pending].section];
            const Elf64_Rela* entries = relocations != 0 ? (const Elf64_Rela*)(file->data + file->sections[relocations].sh_offset) : 0;
            const uint64_t entry_count = relocations != 0 ? file->sections[relocations].sh_size / sizeof(Elf64_Rela) : 0;
            const uint32_t at = items[pending].file;
            for (uint64_t r = 0; r < entry_count; r += 1)
            {
                const uint64_t symbol = ELF64_R_SYM(entries[r].r_info);
                uint32_t target_file = at;
                uint64_t target = 0;
                if (symbol != 0 && symbol < file->symbol_count)
                {
                    target = unreferenced_symbol_section(file, symbol);
                    const char* name = unreferenced_symbol_name(file, symbol);
                    if (target == 0 && name != 0 && ELF64_ST_BIND(file->symbols[symbol].st_info) != STB_LOCAL && defined != 0)
                    {
                        const UnreferencedGlobal key = {
                            .name = name,
                        };
                        const UnreferencedGlobal* found = bsearch(&key, globals, defined, sizeof(UnreferencedGlobal), unreferenced_compare);
                        if (found != 0)
                        {
                            target_file = found->file;
                            target = found->section;
                        }
                    }
                }
                if (target != 0 && !files[target_file].live[target])
                {
                    files[target_file].live[target] = true;
                    items[pending] = (UnreferencedItem){
                        .file = target_file,
                        .section = target,
                    };
                    pending += 1;
                }
            }
        }
        // The dead candidates, by file and section name; a name another of the file's sections shares and that is live
        // stays, as the script names sections by name.
        FILE* script = ready ? fopen(argv[1], "w") : 0;
        ready = ready && script != 0;
        uint64_t dead = 0;
        uint64_t bytes = 0;
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            for (uint64_t i = 1; i < files[f].section_count; i += 1)
            {
                if (files[f].candidate[i] && !files[f].live[i])
                {
                    const char* name = unreferenced_section_name(&files[f], i);
                    bool shared = name == 0;
                    for (uint64_t j = 1; j < files[f].section_count && !shared; j += 1)
                    {
                        const char* other = unreferenced_section_name(&files[f], j);
                        shared = j != i && (files[f].live[j] || !files[f].candidate[j]) && other != 0 && strcmp(other, name) == 0;
                    }
                    if (!shared)
                    {
                        fprintf(script, "        \"%s\"(%s)\n", files[f].path, name);
                        dead += 1;
                        bytes += files[f].sections[i].sh_size;
                    }
                }
            }
        }
        ready = script != 0 && fclose(script) == 0 && ready;
        if (!ready && script != 0)
        {
            fprintf(stderr, "unreferenced: can't write %s\n", argv[1]);
        }
        if (ready && getenv("BUSTER_UNREFERENCED_REPORT") != 0)
        {
            fprintf(stderr, "unreferenced: %llu sections, %llu bytes\n", (unsigned long long)dead, (unsigned long long)bytes);
        }
        status = ready ? 0 : 1;
        free(items);
        free(globals);
        for (uint32_t f = 0; files != 0 && f < count; f += 1)
        {
            free(files[f].data);
            free(files[f].relocations);
            free(files[f].candidate);
            free(files[f].live);
        }
        free(files);
    }
    return status;
}
