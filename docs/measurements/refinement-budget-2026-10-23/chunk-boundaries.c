// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "chunk-refinement.c"
#include "buster/compiler/c_refine_test.c"

int main(int argc, char** argv)
{
    static const uint64_t windows[] = { 1, 2, 3, 7, 16, 31, 63, 64, 127, 512, 1024, 65536 };
    static const char* const suffixes[] = {
        "#define F(x) x\\\n+x\nF(2)\n",
        "#include <an_extremely_long_header_name_that_cannot_be_cut_at_the_end_of_the_current_window.h>\n",
        "identifier_with_a_splice_\\\n_and_a_ucn_\\u00E9\n+z\n",
        "x\n/* a comment\n with several\n physical lines */ y\n",
        "x\n// a line comment with a splice \\\n still the same comment\ny\n",
        "x\n\xEF\xBB\xBF" "bom_inside_a_file\ny\n",
        "x\n\"a string with an escaped \\\" quote\"\n'\\n'\ny\n",
        "x\n1.0e+10 .0 0x1.fp+3 + /* unterminated\n comment",
        "x\n# define T ?" "?/\nfirst?" "?/\nsecond\nT\n",
        "x\nR\"tag(raw\nstring contents\n)tag\"\ny\n",
        "x\n\"unterminated\ny\n'broken\nz\n",
        "x\r\nlast\\\r\nidentifier\r\n\n",
        "x\nR\"tag(raw\nstring contents\n)tag\"\ny\n",
        "x\nu8R\"0123456789abcdef(raw\nstring contents\n)0123456789abcdef\"\ny\n",
        "x\nLR\"bad delimiter(raw\ntext)bad delimiter\"\ny\n",
        "x\nUR\"tag(unterminated\nraw string\ny\n",
    };
    static RefineTestState state;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 6 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool passed = mapping != MAP_FAILED && mprotect(mapping + page, 4 * page, PROT_READ | PROT_WRITE) == 0;
    passed = passed && intern_initialize(&state.scalar_table) && refine_oracle_initialize(&state.oracle) && refine_test_initialize_variants(&state);
    uint64_t checked = 0;
    for (uint32_t w = 0; passed && w < BUSTER_ARRAY_LENGTH(windows); w += 1)
    {
        refine_test_chunk_bytes = windows[w];
        for (uint32_t s = 0; passed && s < BUSTER_ARRAY_LENGTH(suffixes); s += 1)
        {
            for (uint32_t alignment = 0; passed && alignment < 64; alignment += 1)
            {
                const uint64_t head = 2400 + alignment;
                const uint64_t length = head + strlen(suffixes[s]);
                uint8_t* source = mapping + 5 * page - length - alignment;
                for (uint64_t i = 0; i < 800; i += 1)
                {
                    memcpy(source + 3 * i, "x;\n", 3);
                }
                memset(source + 2400, ' ', alignment);
                memcpy(source + head, suffixes[s], strlen(suffixes[s]));
                const uint32_t flags = s == 8 ? C_TOKENIZER_TRIGRAPHS : s == 9 ? C_TOKENIZER_CPLUSPLUS : s >= 12 ? C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS : 0;
                passed = refine_test_compare(&state, source, length, flags, "chunk boundary");
                if (!passed)
                {
                    printf("window %lu suffix %u alignment %u\n", windows[w], s, alignment);
                }
                checked += 1;
            }
        }
    }
    printf("chunk boundaries: %lu guarded inputs, %s\n", checked, passed ? "pass" : "FAIL");
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 6 * page);
    }
    intern_release(&state.scalar_table);
    for (uint32_t i = 0; i < REFINE_VARIANT_COUNT; i += 1)
    {
        intern_release(&state.variant_tables[i]);
    }
    arena_release(&state.oracle.arena);
    arena_release(&state.arena);
    return passed ? (argc > 1 ? run_refinement_tests(argc - 1, argv + 1) : 0) : 1;
}
