#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define DECLARE(prefix) \
    unsigned long prefix##common_alloc_flags(const unsigned long *, unsigned, unsigned long); \
    unsigned long prefix##common_nonalloc_flags(const unsigned long *, unsigned, unsigned long); \
    unsigned prefix##common32(const unsigned *, unsigned, unsigned); \
    unsigned prefix##shared32(const unsigned *, unsigned, unsigned, unsigned); \
    unsigned prefix##volatile32(const volatile unsigned *, unsigned, unsigned); \
    unsigned char prefix##common8(const unsigned char *, unsigned, unsigned char); \
    unsigned short prefix##common16(const unsigned short *, unsigned, unsigned short); \
    unsigned prefix##guarded(const unsigned *, const volatile unsigned *, unsigned, unsigned); \
    unsigned prefix##quotient(const unsigned *, unsigned, unsigned)

DECLARE();
DECLARE(clang_);

static unsigned checks;

static void check(const char *name, unsigned long got, unsigned long expected)
{
    ++checks;
    if (got != expected)
    {
        fprintf(stderr, "%s: got %#lx, expected %#lx\n", name, got, expected);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    FILE *elf = argc == 2 ? fopen(argv[1], "rb") : NULL;
    Elf64_Ehdr header;
    if (!elf || fread(&header, sizeof header, 1, elf) != 1 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_shentsize != sizeof(Elf64_Shdr) || !header.e_shnum ||
        fseek(elf, (long)header.e_shoff, SEEK_SET) != 0)
    {
        return 2;
    }
    unsigned long *flags = calloc(header.e_shnum, sizeof *flags);
    if (!flags) return 2;
    unsigned alloc = 0;
    for (unsigned i = 0; i < header.e_shnum; ++i)
    {
        Elf64_Shdr section;
        if (fread(&section, sizeof section, 1, elf) != 1) return 2;
        flags[i] = section.sh_flags;
        alloc += (section.sh_flags & SHF_ALLOC) != 0;
    }
    fclose(elf);
    if (!alloc || alloc == header.e_shnum) return 2;
    const unsigned long seeds[] = { 0, ~0ul, 0x8000000000000000ul, 0xdedbeef0a5533ccful };
    for (unsigned j = 0; j < sizeof seeds / sizeof *seeds; ++j)
    {
        unsigned long a = seeds[j], b = seeds[j];
        for (unsigned i = 0; i <= header.e_shnum; ++i)
        {
            check("allocated (spec)", common_alloc_flags(flags, i, seeds[j]), a);
            check("nonallocated (spec)", common_nonalloc_flags(flags, i, seeds[j]), b);
            check("allocated (Clang)", clang_common_alloc_flags(flags, i, seeds[j]), a);
            check("nonallocated (Clang)", clang_common_nonalloc_flags(flags, i, seeds[j]), b);
            if (i < header.e_shnum)
            {
                if (flags[i] & SHF_ALLOC) a &= flags[i];
                else b &= flags[i];
            }
        }
    }
    printf("ELF section flags: %u sections, %u allocated, %u other\n", header.e_shnum, alloc, header.e_shnum - alloc);
    free(flags);

    unsigned long words[257];
    unsigned dwords[257];
    unsigned short shorts[257];
    unsigned char bytes[257];
    uint64_t state = UINT64_C(0xf123beef415636e9);
    for (unsigned i = 0; i < 257; ++i)
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        uint64_t bits = state * UINT64_C(2685821657736338717);
        if (i % 8 == 0) bits = 0;
        if (i % 8 == 1) bits = UINT64_MAX;
        if (i % 8 == 2) bits = UINT64_C(0x8000000080008080);
        words[i] = bits;
        dwords[i] = (unsigned)bits;
        shorts[i] = (unsigned short)bits;
        bytes[i] = (unsigned char)bits;
    }
    for (unsigned n = 0; n <= 257; ++n)
    {
        unsigned off = (257 - n) / 3;
        for (unsigned j = 0; j < sizeof seeds / sizeof *seeds; ++j)
        {
            unsigned long s = seeds[j];
#define CHECK_FN(name, ...) check(#name, name(__VA_ARGS__), clang_##name(__VA_ARGS__))
            CHECK_FN(common_alloc_flags, words + off, n, s);
            CHECK_FN(common_nonalloc_flags, words + off, n, s);
            CHECK_FN(common32, dwords + off, n, (unsigned)s);
            CHECK_FN(common16, shorts + off, n, (unsigned short)s);
            CHECK_FN(common8, bytes + off, n, (unsigned char)s);
            CHECK_FN(shared32, dwords + off, n, (unsigned)s, (unsigned)~s);
            CHECK_FN(volatile32, dwords + off, n, (unsigned)s);
            CHECK_FN(quotient, dwords + off, n, (unsigned)s);
        }
    }
    unsigned no[8] = { 0 };
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *guard = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (guard == MAP_FAILED) return 2;
    check("untaken volatile access", guarded(no, guard, 8, 0x87654321), 0x87654321);
    check("untaken division", quotient(no, 8, 0xfedcba98), 0xfedcba98);
    if (munmap(guard, page) != 0) return 2;
    printf("%u native value and guard checks passed\n", checks);
    return 0;
}
