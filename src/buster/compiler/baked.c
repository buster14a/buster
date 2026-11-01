// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The backend's baked tables (#590, baked.h). `bake-tables OUTPUT` writes this executable with its buster_baked section
// as the backend's initializers leave it, so that a compile starts with x86_encode's templates, the x86 semantics'
// tables, Tier 0's (with the Zen 5 costs it reads) and the unwind templates built. build.sh runs it on each executable
// it links. It runs in the checker's unit, whose Tier 0, unwind and Zen 5 copies the C driver uses (main.c): it zeroes
// the section, runs the four initializers, checks that they wrote nothing in the image's writable memory outside the
// section (but the GOT, which the loader may fill as functions are first called), and copies the section's bytes over
// its place in /proc/self/exe's. A PIE, whose section would hold its load address's pointers if any table had one, and
// a link without the section exit with status 77, and build.sh keeps that executable as it is: its initializers build
// the tables when it runs, as they did before.
//
// `test-baked-tables` makes the same build and checks in any executable, a PIE too (the sanitizers' builds, which
// aren't baked): nothing written outside the section, a second build the same bytes as the first, and in a baked
// executable the first the bytes it started with, which are the file's.
//
// Entry points: run_bake_command and run_baked_tables_test.

// OS-dependent includes
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUSTER_BAKE_PAGE 4096
#define BUSTER_BAKE_REPORTED 16
#define BUSTER_BAKE_RANGES 16
#define BUSTER_BAKE_SKIPPED 77

extern char __start_buster_baked[] __attribute__((weak));
extern char __stop_buster_baked[] __attribute__((weak));

typedef struct BakeRange BakeRange;
struct BakeRange
{
    uintptr_t begin;
    uintptr_t end;
};

typedef struct BakeImage BakeImage;
struct BakeImage
{
    uint8_t* bytes;
    uint64_t length;
    uint64_t section_offset;
    bool position_independent;
    // The image's writable memory, at this process's addresses, and what the check leaves out of it: the section and
    // the GOT.
    BakeRange writable[BUSTER_BAKE_RANGES];
    uint32_t writable_count;
    BakeRange skipped[BUSTER_BAKE_RANGES];
    uint32_t skipped_count;
};

static uint64_t bake_section_size(void)
{
    return (uint64_t)(__stop_buster_baked - __start_buster_baked);
}

// Whether the byte at `address` is one the check leaves out.
static __attribute__((no_sanitize("address", "memory", "thread"))) bool bake_skipped(const BakeImage* image, uintptr_t address)
{
    bool skipped = false;
    for (uint32_t range = 0; range < image->skipped_count; range += 1)
    {
        skipped |= address >= image->skipped[range].begin && address < image->skipped[range].end;
    }
    return skipped;
}

// A hash of the bytes of [begin, end) that the check doesn't leave out, a page or less of the image's memory: 8 bytes
// at a time where the page has none it leaves out. It reads every byte, the sanitizers' redzones between globals in
// their builds among them, so they don't instrument it.
static __attribute__((no_sanitize("address", "memory", "thread"))) uint64_t bake_hash(const BakeImage* image, uintptr_t begin, uintptr_t end)
{
    bool overlaps = false;
    for (uint32_t range = 0; range < image->skipped_count; range += 1)
    {
        overlaps |= begin < image->skipped[range].end && end > image->skipped[range].begin;
    }
    uint64_t hash = 0x9e3779b97f4a7c15u;
    uintptr_t address = begin;
    while (address < end)
    {
        const bool word = !overlaps && (address & 7) == 0 && end - address >= 8;
        uint64_t value = 0;
        if (word)
        {
            value = *(const uint64_t*)address;
        }
        else
        {
            value = bake_skipped(image, address) ? 0 : *(const uint8_t*)address;
        }
        hash = (hash ^ value) * 0x100000001b3u;
        address += word ? 8 : 1;
    }
    return hash;
}

// The hash of each page's part of the writable ranges, in `hashes`, or with `before` the pages whose hash differs from
// it reported. Returns how many pages there are, or with `before` how many differ.
static uint64_t bake_hashes(const BakeImage* image, uint64_t* hashes, const uint64_t* before)
{
    uint64_t count = 0;
    uint64_t changed = 0;
    for (uint32_t range = 0; range < image->writable_count; range += 1)
    {
        uintptr_t begin = image->writable[range].begin;
        while (begin < image->writable[range].end)
        {
            const uintptr_t page_end = (begin & ~(uintptr_t)(BUSTER_BAKE_PAGE - 1)) + BUSTER_BAKE_PAGE;
            const uintptr_t end = page_end < image->writable[range].end ? page_end : image->writable[range].end;
            const uint64_t hash = hashes || before ? bake_hash(image, begin, end) : 0;
            if (hashes)
            {
                hashes[count] = hash;
            }
            if (before && before[count] != hash && changed < BUSTER_BAKE_REPORTED)
            {
                fprintf(stderr, "bake-tables: the initializers wrote the page at 0x%llx, outside buster_baked\n", (unsigned long long)begin);
            }
            changed += before && before[count] != hash;
            count += 1;
            begin = end;
        }
    }
    return before ? changed : count;
}

// Reads /proc/self/exe and finds the section's place in it, its writable segments and its GOT. Returns 0, 1 for an
// error, or BUSTER_BAKE_SKIPPED for a link without the section.
static int bake_read_image(BakeImage* image)
{
    int result = 0;
    const uintptr_t start = (uintptr_t)__start_buster_baked;
    const int file = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (start == 0 || bake_section_size() == 0)
    {
        fprintf(stderr, "bake-tables: this executable has no buster_baked section\n");
        result = BUSTER_BAKE_SKIPPED;
    }
    else if (file < 0 || fstat(file, &status) != 0 || status.st_size < (off_t)sizeof(Elf64_Ehdr))
    {
        fprintf(stderr, "bake-tables: can't read /proc/self/exe\n");
        result = 1;
    }
    else
    {
        image->length = (uint64_t)status.st_size;
        image->bytes = malloc(image->length);
        uint64_t done = 0;
        while (image->bytes && done < image->length)
        {
            const ssize_t got = pread(file, image->bytes + done, image->length - done, (off_t)done);
            done = got > 0 ? done + (uint64_t)got : image->length + 1;
        }
        result = image->bytes && done == image->length ? 0 : 1;
    }
    if (file >= 0)
    {
        close(file);
    }
    const Elf64_Ehdr* header = result == 0 ? (const Elf64_Ehdr*)image->bytes : NULL;
    const bool headers_fit = header && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                             (header->e_type == ET_EXEC || header->e_type == ET_DYN) && header->e_shentsize == sizeof(Elf64_Shdr) &&
                             header->e_phentsize == sizeof(Elf64_Phdr) && header->e_shnum != 0 && header->e_shstrndx < header->e_shnum &&
                             header->e_shoff <= image->length && (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= image->length - header->e_shoff &&
                             header->e_phoff <= image->length && (uint64_t)header->e_phnum * sizeof(Elf64_Phdr) <= image->length - header->e_phoff;
    if (result == 0 && !headers_fit)
    {
        fprintf(stderr, "bake-tables: /proc/self/exe isn't an executable this command reads\n");
        result = 1;
    }
    const Elf64_Shdr* sections = result == 0 ? (const Elf64_Shdr*)(image->bytes + header->e_shoff) : NULL;
    const Elf64_Shdr* section = NULL;
    for (uint32_t index = 0; sections && index < header->e_shnum; index += 1)
    {
        const Elf64_Shdr* names = &sections[header->e_shstrndx];
        const bool named = names->sh_offset <= image->length && sections[index].sh_name < image->length - names->sh_offset &&
                           memchr(image->bytes + names->sh_offset + sections[index].sh_name, 0, image->length - names->sh_offset - sections[index].sh_name);
        const char* name = named ? (const char*)image->bytes + names->sh_offset + sections[index].sh_name : "";
        section = strcmp(name, "buster_baked") == 0 ? &sections[index] : section;
    }
    // The load bias: 0 at the fixed address an executable has, and where a PIE's loader put it.
    const uintptr_t bias = section ? start - section->sh_addr : 0;
    if (result == 0 && !(section && section->sh_type == SHT_PROGBITS && section->sh_size == bake_section_size() && section->sh_offset <= image->length &&
                         section->sh_size <= image->length - section->sh_offset))
    {
        fprintf(stderr, "bake-tables: /proc/self/exe's buster_baked section isn't the one this process maps\n");
        result = 1;
    }
    if (result == 0)
    {
        image->section_offset = section->sh_offset;
        image->position_independent = header->e_type == ET_DYN;
        image->skipped[0] = (BakeRange){ start, start + bake_section_size() };
        image->skipped_count = 1;
        for (uint32_t index = 0; index < header->e_shnum; index += 1)
        {
            const Elf64_Shdr* names = &sections[header->e_shstrndx];
            const bool named = names->sh_offset <= image->length && sections[index].sh_name < image->length - names->sh_offset &&
                               memchr(image->bytes + names->sh_offset + sections[index].sh_name, 0, image->length - names->sh_offset - sections[index].sh_name);
            const char* name = named ? (const char*)image->bytes + names->sh_offset + sections[index].sh_name : "";
            if (strncmp(name, ".got", 4) == 0 && sections[index].sh_addr != 0 && image->skipped_count < BUSTER_BAKE_RANGES)
            {
                image->skipped[image->skipped_count] = (BakeRange){ bias + sections[index].sh_addr, bias + sections[index].sh_addr + sections[index].sh_size };
                image->skipped_count += 1;
            }
        }
        const Elf64_Phdr* segments = (const Elf64_Phdr*)(image->bytes + header->e_phoff);
        for (uint32_t index = 0; index < header->e_phnum; index += 1)
        {
            if (segments[index].p_type == PT_LOAD && (segments[index].p_flags & PF_W) && image->writable_count < BUSTER_BAKE_RANGES)
            {
                image->writable[image->writable_count] = (BakeRange){ bias + segments[index].p_vaddr, bias + segments[index].p_vaddr + segments[index].p_memsz };
                image->writable_count += 1;
            }
        }
    }
    return result;
}

// Zeroes the section and runs the four initializers, which must succeed and write nothing in the image's writable
// memory outside it (but the GOT). Returns whether they did.
static bool bake_build(const BakeImage* image)
{
    const uint64_t pages = bake_hashes(image, NULL, NULL);
    uint64_t* before = malloc((pages + 1) * sizeof(uint64_t));
    bool valid = before != NULL;
    if (valid)
    {
        bake_hashes(image, before, NULL);
        memset(__start_buster_baked, 0, bake_section_size());
        const bool built = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
        const uint64_t changed = bake_hashes(image, NULL, before);
        valid = built && changed == 0;
        if (!valid)
        {
            fprintf(stderr, "bake-tables: %s\n", built ? "the initializers' state isn't all in buster_baked" : "an initializer failed");
        }
    }
    free(before);
    return valid;
}

// Writes `length` bytes to a new file at `path` with the mode `mode`.
static bool bake_write(const char* path, const uint8_t* bytes, uint64_t length, mode_t mode)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    uint64_t done = 0;
    while (file >= 0 && done < length)
    {
        const ssize_t wrote = write(file, bytes + done, length - done);
        done = wrote > 0 ? done + (uint64_t)wrote : length + 1;
    }
    const bool closed = file >= 0 && close(file) == 0;
    return closed && done == length && chmod(path, mode) == 0;
}

static int run_bake_command(int argc, char** argv)
{
    BakeImage image;
    memset(&image, 0, sizeof(image));
    int result = argc == 2 ? bake_read_image(&image) : 1;
    if (argc != 2)
    {
        fprintf(stderr, "usage: compiler bake-tables OUTPUT\n");
    }
    else if (result == 0 && image.position_independent)
    {
        fprintf(stderr, "bake-tables: a position-independent executable builds its tables when it runs\n");
        result = BUSTER_BAKE_SKIPPED;
    }
    result = result == 0 && !bake_build(&image) ? 1 : result;
    if (result == 0)
    {
        memcpy(image.bytes + image.section_offset, __start_buster_baked, bake_section_size());
        if (!bake_write(argv[1], image.bytes, image.length, 0755))
        {
            fprintf(stderr, "bake-tables: can't write %s\n", argv[1]);
            result = 1;
        }
    }
    free(image.bytes);
    return result;
}

static int run_baked_tables_test(int argc, char** argv)
{
    (void)argv;
    BakeImage image;
    memset(&image, 0, sizeof(image));
    int result = argc == 1 ? bake_read_image(&image) : 1;
    // Whether this executable is baked: its tables built, as its file has them.
    const bool baked = result == 0 && x86_encoder_initialized && tier0_initialized;
    uint8_t* first = result == 0 ? malloc(bake_section_size()) : NULL;
    if (argc != 1)
    {
        fprintf(stderr, "usage: compiler test-baked-tables\n");
    }
    else if (result == 0 && !first)
    {
        fprintf(stderr, "test-baked-tables: out of memory\n");
        result = 1;
    }
    if (result == 0)
    {
        const bool built_once = bake_build(&image);
        memcpy(first, __start_buster_baked, bake_section_size());
        const bool built_twice = built_once && bake_build(&image);
        const bool same = built_twice && memcmp(first, __start_buster_baked, bake_section_size()) == 0;
        const bool current = !baked || memcmp(image.bytes + image.section_offset, first, bake_section_size()) == 0;
        printf("baked tables: %llu bytes, %s; built twice, %s; nothing written outside them: %s\n", (unsigned long long)bake_section_size(),
               baked ? (current ? "baked, the same as built" : "BAKED, BUT NOT AS BUILT") : "not baked (built when it runs)",
               same ? "the same bytes" : "DIFFERENT BYTES", built_twice ? "yes" : "NO");
        result = built_twice && same && current ? 0 : 1;
    }
    free(first);
    free(image.bytes);
    return result;
}
