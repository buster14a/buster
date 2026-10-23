// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include <sys/mman.h>

// Complete scalar preparation is the oracle for bounded lexer/refinement handoff, including guarded boundaries and
// allocation failures. The normal preprocessor fixtures separately compare emitted tokens and locations with Clang.

typedef struct PPRefineHandoffTest PPRefineHandoffTest;
struct PPRefineHandoffTest
{
    InternTable tables[2];
    Arena columns[2][3];
    Arena arenas[2];
};

static bool pp_refine_handoff_test_same(const PPFileData* a, const PPFileData* b)
{
    const uint64_t count = a->token_count;
    const uint64_t directives = a->directive_count;
    bool same = a->prepared && b->prepared && count == b->token_count && directives == b->directive_count &&
                a->guard == b->guard && a->guard_opener == b->guard_opener && a->note_count == b->note_count &&
                a->unterminated_comment == b->unterminated_comment && a->unterminated_comment_starts_line == b->unterminated_comment_starts_line &&
                a->raw_error_count == b->raw_error_count;
    same = same && memcmp(a->kinds, b->kinds, count) == 0 && memcmp(a->starts, b->starts, count * sizeof(uint32_t)) == 0 &&
           memcmp(a->values, b->values, count * sizeof(uint32_t)) == 0;
    for (uint64_t word = 0; same && word < (count + 63) / 64; word += 1)
    {
        const uint64_t mask = count - 64 * word >= 64 ? ~0ull : _bzhi_u64(~0ull, count - 64 * word);
        same = ((a->space[word] ^ b->space[word]) & mask) == 0;
    }
    same = same && memcmp(a->directive_hash, b->directive_hash, directives * sizeof(uint32_t)) == 0 &&
           memcmp(a->directive_end, b->directive_end, directives * sizeof(uint32_t)) == 0 && memcmp(a->directive_kind, b->directive_kind, directives) == 0 &&
           memcmp(a->directive_next, b->directive_next, directives * sizeof(uint32_t)) == 0;
    for (uint32_t i = 0; same && i < a->note_count; i += 1)
    {
        same = a->note_directives[i] == b->note_directives[i] && a->note_kinds[i] == b->note_kinds[i];
    }
    for (uint32_t i = 0; same && i < a->raw_error_count; i += 1)
    {
        same = a->raw_errors[i].token == b->raw_errors[i].token && a->raw_errors[i].offset == b->raw_errors[i].offset && a->raw_errors[i].kind == b->raw_errors[i].kind;
    }
    return same;
}

static bool pp_refine_handoff_test_one(PPRefineHandoffTest* state, const uint8_t* bytes, uint32_t length, uint32_t flags, uint64_t split, uint64_t window)
{
    PPPrepare prepare = {
        .refine_flags = flags,
        .split_kinds = split,
        .vectorized = false,
    };
    PPFileData data[2] = {
        { .bytes = bytes, .length = length },
        { .bytes = bytes, .length = length },
    };
    for (uint32_t side = 0; side < 2; side += 1)
    {
        arena_pop_to(&state->arenas[side], 0);
        for (uint32_t c = 0; c < 3; c += 1)
        {
            arena_pop_to(&state->columns[side][c], 0);
        }
    }
    const PPPrepareStatus whole = pp_prepare_into_whole(&prepare, &state->tables[0], state->columns[0], &state->arenas[0], &data[0], 0);
    prepare.vectorized = true;
    const PPPrepareStatus chunks = pp_prepare_chunks(&prepare, &state->tables[1], state->columns[1], &state->arenas[1], &data[1], 0, window);
    bool same = whole == PP_PREPARE_DONE && chunks == PP_PREPARE_DONE && pp_refine_handoff_test_same(&data[0], &data[1]) && state->tables[0].count == state->tables[1].count;
    for (IdentId i = C_NAME_COUNT; same && i < state->tables[0].count; i += 1)
    {
        same = intern_length(&state->tables[0], i) == intern_length(&state->tables[1], i) &&
               memcmp(intern_bytes(&state->tables[0], i), intern_bytes(&state->tables[1], i), intern_length(&state->tables[0], i)) == 0;
    }
    if (!same)
    {
        printf("preparation differs: statuses %u/%u, tokens %u/%u, window %lu, flags %u, split %lu\n", whole, chunks, data[0].token_count, data[1].token_count, window, flags, split);
    }
    return same;
}

static bool pp_refine_handoff_test_failures(PPRefineHandoffTest* state)
{
    static const uint8_t bytes[] = "#ifndef GUARD\n#define GUARD\nnew_failure_name\n#endif\n";
    const PPPrepare prepare = { .vectorized = true };
    bool passed = true;
    Scratch scratch = scratch_begin(0);
    const uint64_t scratch_capacity = scratch.arena->capacity;
    for (uint32_t failure = 0; passed && failure < 5; failure += 1)
    {
        Arena columns[3];
        for (uint32_t i = 0; i < 3; i += 1)
        {
            columns[i] = state->columns[1][i];
            columns[i].length = 0;
            columns[i].capacity = failure == i ? 0 : columns[i].capacity;
        }
        Arena arena = state->arenas[1];
        arena.length = 0;
        arena.capacity = failure == 3 ? 0 : arena.capacity;
        scratch.arena->capacity = failure == 4 ? scratch.arena->length : scratch_capacity;
        PPFileData data = { .bytes = bytes, .length = sizeof(bytes) - 1 };
        const PPPrepareStatus status = pp_prepare_chunks(&prepare, &state->tables[1], columns, &arena, &data, 0, 16);
        passed = !data.prepared && status == (failure < 3 ? PP_PREPARE_NO_COLUMNS : PP_PREPARE_NO_MEMORY);
    }
    scratch.arena->capacity = scratch_capacity;
    scratch_end(scratch);
    printf("preparation allocation failures: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static bool pp_refine_handoff_test_mixed(PPRefineHandoffTest* state)
{
    static const char* const parts[] = {
        "ordinary_name ",
        "short+",
        "x;\n",
        "alpha_beta_gamma_abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789 ",
        "joined\\\nidentifier ",
        "joined\\\r\nidentifier ",
        "joined\\ \nidentifier ",
        "ucn_\\u00E9 ",
        "\\U0001F42C_suffix ",
        "\\N{LATIN SMALL LETTER E WITH ACUTE} ",
        "caf\xC3\xA9 ",
        "a\\\n+b ",
        "a \\\n+b ",
        "x\\\n\\\r\n+y ",
        "/* comment \\u00E9 \\ */ ",
        "// comment \\u00E9\n",
        "\"string\\n\\t\" ",
        "R\"tag(raw\n\\u00E9)tag\" ",
        "1.23e+4 ",
        "0x123'456 ",
        "#define F\\\n(x) x\n",
    };
    static const uint64_t windows[] = { 31, 64, 127, 512, 1024, 4096, 131072 };
    uint8_t bytes[65536];
    uint64_t random = 116;
    bool passed = true;
    uint32_t cases = 0;
    for (; passed && cases < 2000; cases += 1)
    {
        uint32_t length = 0;
        // Several ordinary blocks before the first exceptional identifier exercise the bitmap's negative proof.
        for (uint32_t token = 0; token < cases % 2048; token += 1)
        {
            bytes[length++] = 'p';
            bytes[length++] = '+';
        }
        for (uint32_t piece = 0; piece < 512; piece += 1)
        {
            random = random * 6364136223846793005ull + 1442695040888963407ull;
            const char* part = parts[(random >> 32) % BUSTER_ARRAY_LENGTH(parts)];
            const uint32_t size = (uint32_t)strlen(part);
            memcpy(bytes + length, part, size);
            length += size;
        }
        const uint32_t flags = cases % 2 == 0 ? C_TOKENIZER_NO_DIGIT_SEPARATORS : C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS;
        passed = pp_refine_handoff_test_one(state, bytes, length, flags, 0, windows[cases % BUSTER_ARRAY_LENGTH(windows)]);
    }
    printf("mixed handoff: %u cases, %s\n", cases, passed ? "pass" : "FAIL");
    return passed;
}

static bool pp_refine_handoff_test_partial_failure(PPRefineHandoffTest* state)
{
    uint8_t bytes[8192];
    for (uint32_t i = 0; i < sizeof(bytes); i += 2)
    {
        bytes[i] = 's';
        bytes[i + 1] = '\n';
    }
    Arena columns[3];
    for (uint32_t c = 0; c < 3; c += 1)
    {
        columns[c] = state->columns[1][c];
        columns[c].length = 0;
    }
    Arena arena = state->arenas[1];
    arena.length = 0;
    // A 256-byte window keeps 127 complete one-token lines. Twenty words allow the first full refinement block,
    // but cannot grow for the following window; the first word proves that failure happened after useful work.
    arena.capacity = 20 * sizeof(uint64_t);
    memset(arena.base, 0, arena.capacity);
    const PPPrepare prepare = { .vectorized = true };
    PPFileData data = { .bytes = bytes, .length = sizeof(bytes) };
    const PPPrepareStatus status = pp_prepare_chunks(&prepare, &state->tables[1], columns, &arena, &data, 0, 256);
    const bool passed = status == PP_PREPARE_NO_MEMORY && !data.prepared && ((const uint64_t*)arena.base)[0] == ~1ull;
    printf("partial handoff allocation failure: %s\n", passed ? "pass" : "FAIL");
    return passed;
}

static bool pp_test_refine_handoff(void)
{
    static const uint64_t windows[] = { 1, 2, 3, 7, 16, 31, 63, 64, 127, 512, 1024, 65536, 262144 };
    static const char* const sources[] = {
        "", "\xEF\xBB\xBF", "x", "/*comment*/", "/*unterminated\n", "x\n/*unterminated\n", "x /*unterminated\n",
        "#ifndef GUARD\n#define GUARD\n#if 0\n#warning note\n#else\ntext\n#endif\n#endif\n",
        "#define F(x) x\\\n+x\nF(2)\n",
        "#include <an_extremely_long_header_name_that_cannot_be_cut_at_the_end_of_the_current_window.h>\n",
        "identifier_with_a_splice_\\\n_and_a_ucn_\\u00E9\n+z\n",
        "x\n/* a comment\n with several\n physical lines */ y\n",
        "x\n// a line comment with a splice \\\n still the same comment\ny\n",
        "x\n\xEF\xBB\xBF" "bom_inside_a_file\ny\n",
        "x\n\"a string with an escaped \\\" quote\"\n'\\n'\ny\n",
        "x\n1.0e+10 .0 0x1.fp+3 + /* unterminated\n comment",
        "x\nR\"tag(raw\nstring contents\n)tag\"\ny\n",
        "x\n\"unterminated\ny\n'broken\nz\n",
        "x\r\nlast\\\r\nidentifier\r\n\n",
        "x\nu8R\"0123456789abcdef(raw\nstring contents\n)0123456789abcdef\"\ny\n",
        "x\nLR\"bad delimiter(raw\ntext)bad delimiter\"\ny\n",
        "x\nUR\"tag(unterminated\nraw string\ny\n",
        "u\"text\" U'c' u8'c' L'c' u\\\n8\"text\" u8'' u'' U'\nx\n",
        "x\\\n+y\\\r\n+z\\\n\\\n+t\n",
        "x \\\n+y\\\n +z\n",
        "x\\ \n+y\\\t\n+z\n",
        "#define F\\\n(x) x\n#define G\\\r\n(x) x\nF(2) G(3)\n",
    };
    PPRefineHandoffTest state = { 0 };
    bool passed = true;
    for (uint32_t side = 0; side < 2; side += 1)
    {
        passed = intern_initialize(&state.tables[side]) && passed;
        state.arenas[side] = arena_reserve(1ull << 30);
        passed = state.arenas[side].base != 0 && passed;
        for (uint32_t c = 0; c < 3; c += 1)
        {
            state.columns[side][c] = arena_reserve(1ull << 30);
            passed = state.columns[side][c].base != 0 && passed;
        }
    }
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 6 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    passed = passed && mapping != MAP_FAILED && mprotect(mapping + page, 4 * page, PROT_READ | PROT_WRITE) == 0;
    uint64_t cases = 0;
    for (uint32_t w = 0; passed && w < BUSTER_ARRAY_LENGTH(windows); w += 1)
    {
        for (uint32_t s = 0; passed && s < BUSTER_ARRAY_LENGTH(sources); s += 1)
        {
            for (uint32_t mode = 0; passed && mode < 6; mode += 1)
            {
                const uint32_t flags = (mode % 3 == 2 ? C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS : 0) |
                    (mode >= 3 ? C_TOKENIZER_NO_DIGIT_SEPARATORS : 0);
                const uint64_t split = pp_split_kinds((CLanguage){ .standard = mode % 3 == 0 ? 1999 : mode % 3 == 1 ? 2011 : 2023 });
                for (uint32_t alignment = 0; passed && alignment < 64; alignment += 1)
                {
                    const uint32_t prefix = s < 8 ? 0 : 2400;
                    const uint32_t length = prefix + (uint32_t)strlen(sources[s]);
                    uint8_t* bytes = mapping + 5 * page - length - alignment;
                    for (uint32_t i = 0; i < prefix / 3; i += 1)
                    {
                        memcpy(bytes + 3 * i, "x;\n", 3);
                    }
                    memcpy(bytes + prefix, sources[s], strlen(sources[s]));
                    passed = pp_refine_handoff_test_one(&state, bytes, length, flags, split, windows[w]);
                    if (!passed)
                    {
                        printf("fixture %u mode %u alignment %u\n", s, mode, alignment);
                    }
                    cases += 1;
                }
            }
        }
    }
    printf("chunk preparation: %lu guarded cases, %s\n", cases, passed ? "pass" : "FAIL");
    static const uint32_t splice_positions[] = { 4095, 4096, 4097, 4159, 4160, 4161, 4607, 4608, 4609, 8191, 8192, 8193 };
    static const uint64_t splice_windows[] = { 63, 127, 512, 1024, 131072 };
    uint64_t splice_cases = 0;
    for (uint32_t s = 0; passed && s < BUSTER_ARRAY_LENGTH(sources); s += 1)
    {
        const char* first_backslash = strchr(sources[s], '\\');
        for (uint32_t p = 0; passed && first_backslash != 0 && p < BUSTER_ARRAY_LENGTH(splice_positions); p += 1)
        {
            const uint32_t prefix = splice_positions[p] - (uint32_t)(first_backslash - sources[s]);
            const uint32_t length = prefix + (uint32_t)strlen(sources[s]);
            uint8_t* bytes = mapping + 5 * page - length;
            for (uint32_t i = 0; i < prefix; i += 1)
            {
                bytes[i] = "x;\n"[i % 3];
            }
            bytes[prefix - 1] = '\n';
            memcpy(bytes + prefix, sources[s], strlen(sources[s]));
            for (uint32_t mode = 0; passed && mode < 2; mode += 1)
            {
                const uint32_t flags = C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS |
                    (mode != 0 ? C_TOKENIZER_NO_DIGIT_SEPARATORS : 0);
                for (uint32_t w = 0; passed && w < BUSTER_ARRAY_LENGTH(splice_windows); w += 1)
                {
                    passed = pp_refine_handoff_test_one(&state, bytes, length, flags, 0, splice_windows[w]);
                    if (!passed)
                    {
                        printf("splice boundary fixture %u position %u mode %u\n", s, splice_positions[p], mode);
                    }
                    splice_cases += 1;
                }
            }
        }
    }
    printf("splice source boundaries: %lu guarded cases, %s\n", splice_cases, passed ? "pass" : "FAIL");
    passed = passed && pp_refine_handoff_test_mixed(&state);
    passed = passed && pp_refine_handoff_test_failures(&state);
    passed = passed && pp_refine_handoff_test_partial_failure(&state);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 6 * page);
    }
    for (uint32_t side = 0; side < 2; side += 1)
    {
        intern_release(&state.tables[side]);
        arena_release(&state.arenas[side]);
        for (uint32_t c = 0; c < 3; c += 1)
        {
            arena_release(&state.columns[side][c]);
        }
    }
    return passed;
}
