// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The comdat flags of a unit's joined native parts (docs/commands/build.md, "Parallel native generation"):
//
//   comdat OBJECT PART...
//
// sets GRP_COMDAT again on each section group of OBJECT whose signature names a comdat group of one of the PARTs, the
// code generator's objects. BusterCodegenMerge.cmake rewrites the parts and their join with llvm-objcopy, which clears
// GRP_COMDAT on every group whose signature symbol is local, deliberately (a localized group is meant to stay apart).
// The code generator's groups with a local signature are a sanitizer's: AddressSanitizer's module constructor and
// destructor, internal functions in comdats of their own names, which every unit has and the linker keeps once. As
// plain groups, the split units' would run beside the first unit's. Fails, saying why, for a file that isn't a 64-bit
// little-endian relocatable ELF object or that it can't write. Build tooling outside the compiler's compile path, with
// no vector path.

#include <elf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct ComdatFile ComdatFile;
struct ComdatFile
{
    uint8_t* data;
    uint64_t size;
};

// Signatures of the parts' comdat groups, sorted once they are all in (comdat_sort), which comdat_named then searches:
// an AddressSanitizer unit has a comdat for every global it describes, tens of thousands.
typedef struct ComdatNames ComdatNames;
struct ComdatNames
{
    char** names;
    uint64_t count;
    uint64_t capacity;
};

static bool comdat_read(const char* path, ComdatFile* file)
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
        fprintf(stderr, "comdat: can't read %s\n", path);
    }
    return read;
}

// The number of sections: e_shnum, or the first section header's size where there are too many for it (an
// AddressSanitizer unit's objects have a few sections a global).
static uint64_t comdat_sections(const ComdatFile* file)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file->data;
    return header->e_shnum != 0 ? header->e_shnum : ((const Elf64_Shdr*)(file->data + header->e_shoff))->sh_size;
}

static const Elf64_Shdr* comdat_section(const ComdatFile* file, uint64_t index)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file->data;
    return index < comdat_sections(file) ? (const Elf64_Shdr*)(file->data + header->e_shoff) + index : 0;
}

// Whether `file` is a relocatable x86-64 ELF object whose section headers lie within it.
static bool comdat_valid(const ComdatFile* file, const char* path)
{
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file->data;
    bool valid = file->size >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                 header->e_ident[EI_CLASS] == ELFCLASS64 && header->e_ident[EI_DATA] == ELFDATA2LSB && header->e_type == ET_REL &&
                 header->e_shentsize == sizeof(Elf64_Shdr);
    valid = valid && header->e_shoff <= file->size && sizeof(Elf64_Shdr) <= file->size - header->e_shoff;
    valid = valid && comdat_sections(file) <= (file->size - header->e_shoff) / sizeof(Elf64_Shdr);
    if (!valid)
    {
        fprintf(stderr, "comdat: %s isn't a 64-bit little-endian relocatable ELF object\n", path);
    }
    return valid;
}

// The signature of group section `group`, or 0 when its symbol or name lies outside the file.
static const char* comdat_signature(const ComdatFile* file, const Elf64_Shdr* group)
{
    const Elf64_Shdr* symbols = comdat_section(file, group->sh_link);
    const Elf64_Shdr* strings = symbols != 0 ? comdat_section(file, symbols->sh_link) : 0;
    const char* name = 0;
    const uint64_t end = ((uint64_t)group->sh_info + 1) * sizeof(Elf64_Sym);
    if (strings != 0 && symbols->sh_offset <= file->size && symbols->sh_size <= file->size - symbols->sh_offset && end <= symbols->sh_size &&
        strings->sh_offset <= file->size && strings->sh_size <= file->size - strings->sh_offset)
    {
        const Elf64_Sym* symbol = (const Elf64_Sym*)(file->data + symbols->sh_offset) + group->sh_info;
        const char* table = (const char*)file->data + strings->sh_offset;
        if (symbol->st_name < strings->sh_size && memchr(table + symbol->st_name, 0, strings->sh_size - symbol->st_name) != 0)
        {
            name = table + symbol->st_name;
        }
    }
    return name;
}

// The group's flag word, or 0 when it lies outside the file.
static uint32_t* comdat_flags(const ComdatFile* file, const Elf64_Shdr* group)
{
    const bool inside = group->sh_offset <= file->size && file->size - group->sh_offset >= sizeof(uint32_t) && group->sh_size >= sizeof(uint32_t);
    return inside ? (uint32_t*)(file->data + group->sh_offset) : 0;
}

static int comdat_compare(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static void comdat_sort(ComdatNames* names)
{
    if (names->count != 0)
    {
        qsort(names->names, names->count, sizeof(char*), comdat_compare);
    }
}

// Whether the sorted `names` hold `name`.
static bool comdat_named(const ComdatNames* names, const char* name)
{
    return names->count != 0 && bsearch(&name, names->names, names->count, sizeof(char*), comdat_compare) != 0;
}

// Adds the signatures of `file`'s comdat groups to `names`, unsorted.
static bool comdat_collect(const ComdatFile* file, const char* path, ComdatNames* names)
{
    bool collected = true;
    for (uint64_t i = 0; i < comdat_sections(file) && collected; i += 1)
    {
        const Elf64_Shdr* section = comdat_section(file, i);
        if (section->sh_type == SHT_GROUP)
        {
            const char* signature = comdat_signature(file, section);
            const uint32_t* flags = comdat_flags(file, section);
            collected = signature != 0 && flags != 0;
            if (collected && (*flags & GRP_COMDAT) != 0)
            {
                if (names->count == names->capacity)
                {
                    names->capacity = names->capacity == 0 ? 64 : names->capacity * 2;
                    names->names = realloc(names->names, names->capacity * sizeof(char*));
                }
                names->names[names->count] = strdup(signature);
                names->count += 1;
            }
        }
    }
    if (!collected)
    {
        fprintf(stderr, "comdat: %s has a group whose signature or flags lie outside it\n", path);
    }
    return collected;
}

int main(int argc, char** argv)
{
    int status = 1;
    if (argc < 3)
    {
        fprintf(stderr, "usage: comdat OBJECT PART...\n");
        status = 2;
    }
    else
    {
        ComdatNames names = { 0 };
        bool ready = true;
        for (int i = 2; i < argc && ready; i += 1)
        {
            ComdatFile part = { 0 };
            ready = comdat_read(argv[i], &part) && comdat_valid(&part, argv[i]) && comdat_collect(&part, argv[i], &names);
            free(part.data);
        }
        comdat_sort(&names);
        ComdatFile object = { 0 };
        ready = ready && comdat_read(argv[1], &object) && comdat_valid(&object, argv[1]);
        // The flag words to set, by their offsets in the file, which is written there alone.
        const uint64_t sections = ready ? comdat_sections(&object) : 0;
        uint64_t* offsets = calloc(sections + 1, sizeof(uint64_t));
        uint64_t restored = 0;
        for (uint64_t i = 0; ready && i < sections; i += 1)
        {
            const Elf64_Shdr* section = comdat_section(&object, i);
            if (section->sh_type == SHT_GROUP)
            {
                const char* signature = comdat_signature(&object, section);
                uint32_t* flags = comdat_flags(&object, section);
                ready = signature != 0 && flags != 0;
                if (ready && (*flags & GRP_COMDAT) == 0 && comdat_named(&names, signature))
                {
                    *flags |= GRP_COMDAT;
                    offsets[restored] = section->sh_offset;
                    restored += 1;
                }
            }
        }
        if (ready && restored != 0)
        {
            FILE* stream = fopen(argv[1], "r+b");
            ready = stream != 0;
            for (uint64_t i = 0; ready && i < restored; i += 1)
            {
                ready = fseeko(stream, (off_t)offsets[i], SEEK_SET) == 0 && fwrite(object.data + offsets[i], sizeof(uint32_t), 1, stream) == 1;
            }
            ready = stream != 0 && fclose(stream) == 0 && ready;
            if (!ready)
            {
                fprintf(stderr, "comdat: can't write %s\n", argv[1]);
            }
        }
        free(offsets);
        status = ready ? 0 : 1;
        free(object.data);
        for (uint64_t i = 0; i < names.count; i += 1)
        {
            free(names.names[i]);
        }
        free(names.names);
    }
    return status;
}
