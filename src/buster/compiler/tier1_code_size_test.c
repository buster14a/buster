// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Small hand-written ELF objects keep the size oracle independent of the compiler's object writer. Nothing runs:
// exact sizes, aliases, section padding, malformed reports and the corpus gate are checked by c-testsuite's self-test.

#include <elf.h>

typedef struct Tier1SizeTestElf Tier1SizeTestElf;
struct Tier1SizeTestElf
{
    uint8_t bytes[2048];
    uint64_t length;
};

static void tier1_code_size_test_object(Tier1SizeTestElf* result, const char* first, uint64_t size, uint64_t text_size, bool executable, bool empty)
{
    *result = (Tier1SizeTestElf){ .length = 1024 + 5 * sizeof(Elf64_Shdr) };
    const Elf64_Ehdr header = {
        .e_ident = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV },
        .e_type = ET_REL,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_shoff = 1024,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = 5,
        .e_shstrndx = 4,
    };
    memcpy(result->bytes, &header, sizeof(header));
    memset(result->bytes + 64, 0x90, (size_t)text_size);
    const char* names[] = { first, "fallback", "helper", "alias" };
    const uint64_t offsets[] = { 0, 16, 32, 0 };
    const uint64_t sizes[] = { size, 12, 5, size };
    const uint32_t count = empty ? 0 : 4;
    uint32_t string_bytes = 1;
    for (uint32_t s = 0; s < count; s += 1)
    {
        const uint32_t length = (uint32_t)strlen(names[s]) + 1;
        memcpy(result->bytes + 256 + string_bytes, names[s], length);
        const Elf64_Sym symbol = {
            .st_name = string_bytes,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
            .st_shndx = 1,
            .st_value = offsets[s],
            .st_size = sizes[s],
        };
        memcpy(result->bytes + 512 + (s + 1) * sizeof(Elf64_Sym), &symbol, sizeof(symbol));
        string_bytes += length;
    }
    static const char section_names[] = "\0.text\0.symtab\0.strtab\0.shstrtab";
    memcpy(result->bytes + 896, section_names, sizeof(section_names));
    const Elf64_Shdr sections[] = {
        { 0 },
        { .sh_name = 1, .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | (executable ? SHF_EXECINSTR : 0), .sh_offset = 64,
          .sh_size = text_size, .sh_addralign = 16 },
        { .sh_name = 7, .sh_type = SHT_SYMTAB, .sh_offset = 512, .sh_size = (count + 1) * sizeof(Elf64_Sym), .sh_link = 3,
          .sh_info = 1, .sh_addralign = 8, .sh_entsize = sizeof(Elf64_Sym) },
        { .sh_name = 15, .sh_type = SHT_STRTAB, .sh_offset = 256, .sh_size = string_bytes, .sh_addralign = 1 },
        { .sh_name = 23, .sh_type = SHT_STRTAB, .sh_offset = 896, .sh_size = sizeof(section_names), .sh_addralign = 1 },
    };
    memcpy(result->bytes + 1024, sections, sizeof(sections));
}

static void tier1_code_size_test_expect(uint32_t* checks, uint32_t* failures, bool valid, const char* name)
{
    *checks += 1;
    if (!valid)
    {
        *failures += 1;
        fprintf(stderr, "test-c-testsuite: code-size self-test: %s FAILED\n", name);
    }
}

static bool tier1_code_size_test_compare(Tier1CodeSize* sizes, const Tier1SizeTestElf* ours, const Tier1SizeTestElf* theirs, const char* report, Arena* arena)
{
    const uint64_t mark = arena->length;
    char detail[256];
    const bool valid = tier1_code_size_compare(sizes, (String8){ .pointer = (char*)ours->bytes, .length = ours->length },
                                             (String8){ .pointer = (char*)theirs->bytes, .length = theirs->length },
                                             (String8){ .pointer = (char*)report, .length = strlen(report) }, arena, detail, sizeof(detail));
    arena_pop_to(arena, mark);
    return valid;
}

#define BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-sizes\tversion=1\tfunctions=2\n"
#define BUSTER_TIER1_SIZE_TEST_GOOD "tier1-function-size\ttier=tier1\tbytes=8\tname=676f6f64\n"
#define BUSTER_TIER1_SIZE_TEST_FALLBACK "tier1-function-size\ttier=tier0\tbytes=12\tname=66616c6c6261636b\n"
#define BUSTER_TIER1_SIZE_TEST_END "tier1-function-sizes-end\tfunctions=2\n"
#define BUSTER_TIER1_SIZE_TEST_SUMMARY "tier1\tfunctions=2\trequested=2\tcompiled=1\tfallbacks=1\n"

static bool tier1_code_size_self_test(void)
{
    uint32_t checks = 0;
    uint32_t failures = 0;
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    Tier1SizeTestElf ours;
    Tier1SizeTestElf theirs;
    Tier1CodeSize sizes = { 0 };
    tier1_code_size_test_object(&ours, "good", 8, 64, true, false);
    tier1_code_size_test_object(&theirs, "good", 10, 64, true, false);
    static const char report[] = BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK
                                 BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY;
    bool valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && sizes.units == 1 && sizes.compiled == 1 && sizes.matched == 1 && sizes.fallbacks == 1 &&
                               sizes.tier1_bytes == 8 && sizes.clang_bytes == 10 && sizes.fallback_bytes == 12 && sizes.other_bytes == 44 &&
                               sizes.text_bytes == 64 && sizes.clang_text_bytes == 64 && tier1_code_size_qualified(&sizes),
                               "exact function sizes, fallback exclusion and uncounted helper/alias/padding bytes");
    tier1_code_size_test_object(&ours, "good", 8, 80, true, false);
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && sizes.tier1_bytes == 8 && sizes.clang_bytes == 10 && sizes.text_bytes == 80 &&
                               sizes.other_bytes == 60, "padding affects section totals, not function sizes");
    tier1_code_size_test_object(&ours, "good", 8, 64, true, false);
    tier1_code_size_test_object(&theirs, "good", 8, 64, true, false);
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && tier1_code_size_qualified(&sizes), "equal byte totals pass");
    tier1_code_size_test_object(&theirs, "good", 7, 64, true, false);
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && !tier1_code_size_qualified(&sizes), "a one-byte regression fails");
    tier1_code_size_test_object(&theirs, "missing", 10, 64, true, false);
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && sizes.compiled == 1 && sizes.matched == 0 && sizes.unmatched == 1 && sizes.unmatched_bytes == 8 &&
                               sizes.fallback_bytes == 12 && !tier1_code_size_qualified(&sizes), "missing counterparts stay visible and cannot pass");
    tier1_code_size_test_object(&theirs, "good", 10, 64, true, false);
    Elf64_Sym aliased;
    memcpy(&aliased, theirs.bytes + 512 + 2 * sizeof(Elf64_Sym), sizeof(aliased));
    aliased.st_value = 0;
    memcpy(theirs.bytes + 512 + 2 * sizeof(Elf64_Sym), &aliased, sizeof(aliased));
    static const char oracle_alias[] = BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD
                                       "tier1-function-size\ttier=tier1\tbytes=12\tname=66616c6c6261636b\n" BUSTER_TIER1_SIZE_TEST_END
                                       "tier1\tfunctions=2\trequested=2\tcompiled=2\tfallbacks=0\n";
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, oracle_alias, &arena);
    tier1_code_size_test_expect(&checks, &failures, !valid, "overlapping Clang counterparts cannot inflate the denominator");
    tier1_code_size_test_object(&theirs, "good", 10, 64, true, false);
    static const struct
    {
        const char* name;
        const char* text;
    } rejected[] = {
        { "missing manifest", BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "unknown manifest version", "tier1-function-sizes\tversion=2\tfunctions=2\n" BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK
                                      BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "missing footer", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "missing summary", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END },
        { "extra summary", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK
                           BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "duplicate summary field", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END
                                     "tier1\tfunctions=2\trequested=2\tcompiled=1\tcompiled=1\tfallbacks=1\n" },
        { "summary count mismatch", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END
                                     "tier1\tfunctions=2\trequested=2\tcompiled=2\tfallbacks=0\n" },
        { "decimal overflow", "tier1-function-sizes\tversion=1\tfunctions=18446744073709551616\n" BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "invalid decimal byte count", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=+8\tname=676f6f64\n"
                                        BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "ELF byte count mismatch", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=9\tname=676f6f64\n"
                                     BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "invalid hexadecimal name", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=8\tname=6g6f6f64\n"
                                      BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "odd hexadecimal name", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=8\tname=676f6f6\n"
                                  BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "NUL in symbol name", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=8\tname=006f6f64\n"
                                 BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "unknown required body", BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=8\tname=67686f7374\n"
                                    BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "duplicate manifest body", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_END
                                      "tier1\tfunctions=2\trequested=2\tcompiled=2\tfallbacks=0\n" },
        { "overlapping required body ranges", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD
                                               "tier1-function-size\ttier=tier1\tbytes=8\tname=616c696173\n" BUSTER_TIER1_SIZE_TEST_END
                                               "tier1\tfunctions=2\trequested=2\tcompiled=2\tfallbacks=0\n" },
        { "extra document", BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK
                             BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_HEADER BUSTER_TIER1_SIZE_TEST_SUMMARY },
        { "diagnostic inside document", BUSTER_TIER1_SIZE_TEST_HEADER "unexpected line\n" BUSTER_TIER1_SIZE_TEST_GOOD BUSTER_TIER1_SIZE_TEST_FALLBACK
                                         BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY },
    };
    for (uint32_t i = 0; arena.base != NULL && i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        valid = tier1_code_size_test_compare(&sizes, &ours, &theirs, rejected[i].text, &arena);
        tier1_code_size_test_expect(&checks, &failures, !valid && sizes.invalid, rejected[i].name);
    }
    tier1_code_size_test_object(&ours, "good", 0, 64, true, false);
    static const char zero[] = BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=0\tname=676f6f64\n"
                               BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY;
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, zero, &arena);
    tier1_code_size_test_expect(&checks, &failures, !valid, "zero-sized required ELF function fails");
    tier1_code_size_test_object(&ours, "good", 8, 64, false, false);
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, !valid, "required function in a non-executable section fails");
    tier1_code_size_test_object(&ours, "good", 8, 64, true, false);
    ours.length = sizeof(Elf64_Ehdr) - 1;
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, report, &arena);
    tier1_code_size_test_expect(&checks, &failures, !valid, "truncated object fails");
    tier1_code_size_test_object(&ours, "good", 8, 64, true, false);
    static const char fallback[] = "tier1-function-sizes\tversion=1\tfunctions=1\n"
                                   "tier1-function-size\ttier=tier0\tbytes=8\tname=676f6f64\n"
                                   "tier1-function-sizes-end\tfunctions=1\n"
                                   "tier1\tfunctions=1\trequested=1\tcompiled=0\tfallbacks=1\n";
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, fallback, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && sizes.fallbacks == 1 && sizes.fallback_bytes == 8 && sizes.matched == 0 &&
                               !tier1_code_size_qualified(&sizes), "fallback-only unit is measured but cannot qualify alone");
    tier1_code_size_test_object(&ours, "good", 0, 0, true, true);
    tier1_code_size_test_object(&theirs, "good", 0, 0, true, true);
    static const char empty[] = "tier1-function-sizes\tversion=1\tfunctions=0\n"
                                "tier1-function-sizes-end\tfunctions=0\n"
                                "tier1\tfunctions=0\trequested=0\tcompiled=0\tfallbacks=0\n";
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, empty, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && sizes.units == 1 && sizes.text_bytes == 0 && sizes.compiled == 0 &&
                               !tier1_code_size_qualified(&sizes), "declaration-only unit has an explicit empty manifest");
    tier1_code_size_test_object(&ours, "odd\tlabel\n", 8, 64, true, false);
    tier1_code_size_test_object(&theirs, "odd\tlabel\n", 10, 64, true, false);
    static const char odd[] = BUSTER_TIER1_SIZE_TEST_HEADER "tier1-function-size\ttier=tier1\tbytes=8\tname=6f6464096c6162656c0a\n"
                              BUSTER_TIER1_SIZE_TEST_FALLBACK BUSTER_TIER1_SIZE_TEST_END BUSTER_TIER1_SIZE_TEST_SUMMARY;
    valid = arena.base != NULL && tier1_code_size_test_compare(&sizes, &ours, &theirs, odd, &arena);
    tier1_code_size_test_expect(&checks, &failures, valid && tier1_code_size_qualified(&sizes), "hexadecimal names preserve tabs and newlines");
    Tier1CodeSize total = { .units = 1, .compiled = 1, .matched = 1, .tier1_bytes = UINT64_MAX, .clang_bytes = UINT64_MAX };
    const Tier1CodeSize more = { .units = 1, .compiled = 1, .matched = 1, .tier1_bytes = 1, .clang_bytes = 1 };
    tier1_code_size_add(&total, &more);
    tier1_code_size_test_expect(&checks, &failures, total.invalid && !tier1_code_size_qualified(&total), "aggregate overflow is sticky and cannot qualify");
    tier1_code_size_add(&total, &(Tier1CodeSize){ 0 });
    tier1_code_size_test_expect(&checks, &failures, total.invalid, "later empty units cannot clear overflow");
    arena_release(&arena);
    printf("test-c-testsuite: code-size self-test: %u checks, %u failed\n", checks, failures);
    return failures == 0 && checks >= 30;
}

#undef BUSTER_TIER1_SIZE_TEST_HEADER
#undef BUSTER_TIER1_SIZE_TEST_GOOD
#undef BUSTER_TIER1_SIZE_TEST_FALLBACK
#undef BUSTER_TIER1_SIZE_TEST_END
#undef BUSTER_TIER1_SIZE_TEST_SUMMARY
