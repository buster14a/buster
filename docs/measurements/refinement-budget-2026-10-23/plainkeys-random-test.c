// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define main prepare_boundary_main
#include "chunk-prepare-boundaries.c"
#undef main

// Independent whole-file scalar preparation checks the bitmap proof on mixed identifiers and physical boundaries.
int main(void)
{
    static const char* const parts[] = {
        "ordinary_name ", "short+", "x;\n", "alpha_beta_gamma_abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789 ",
        "joined\\\nidentifier ", "joined\\\r\nidentifier ", "joined\\ \nidentifier ",
        "ucn_\\u00E9 ", "\\U0001F42C_suffix ", "\\N{LATIN SMALL LETTER E WITH ACUTE} ", "caf\xC3\xA9 ",
        "a\\\n+b ", "a \\\n+b ", "x\\\n\\\r\n+y ", "/* comment \\u00E9 \\ */ ", "// comment \\u00E9\n",
        "\"string\\n\\t\" ", "R\"tag(raw\n\\u00E9)tag\" ", "1.23e+4 ", "0x123'456 ", "#define F\\\n(x) x\n",
    };
    static const uint64_t windows[] = { 31, 64, 127, 512, 1024, 4096, 131072 };
    PrepareTest state = { 0 };
    bool passed = true;
    for (uint32_t side = 0; side < 2; side += 1)
    {
        passed = intern_initialize(&state.tables[side]) && passed;
        state.arenas[side] = arena_reserve(1ull << 30);
        passed = state.arenas[side].base != 0 && passed;
        for (uint32_t column = 0; column < 3; column += 1)
        {
            state.columns[side][column] = arena_reserve(1ull << 30);
            passed = state.columns[side][column].base != 0 && passed;
        }
    }
    uint8_t bytes[65536];
    uint64_t random = 116;
    uint32_t cases = 0;
    for (; passed && cases < 2000; cases += 1)
    {
        uint32_t length = 0;
        // Ordinary prefixes cover several full 1024-token blocks before the first exceptional identifier.
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
        passed = prepare_test_one(&state, bytes, length, flags, 0, windows[cases % BUSTER_ARRAY_LENGTH(windows)]);
    }
    printf("mixed handoff: %u cases, %s\n", cases, passed ? "pass" : "FAIL");
    for (uint32_t side = 0; side < 2; side += 1)
    {
        intern_release(&state.tables[side]);
        arena_release(&state.arenas[side]);
        for (uint32_t column = 0; column < 3; column += 1)
        {
            arena_release(&state.columns[side][column]);
        }
    }
    scratch_release();
    return passed ? 0 : 1;
}
