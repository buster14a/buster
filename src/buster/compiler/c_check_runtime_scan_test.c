// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Runtime declaration lookup: reuse the existing compact producer, retain an independent full-token traversal,
// and check both against a runtime-first oracle, which reads every runtime function's name where the lookup reads the
// list of the spelled ones (#148). Include after check_test_program_guarded.
#ifndef BUSTER_C_CHECK_RUNTIME_SCAN_TEST_INCLUDED
#define BUSTER_C_CHECK_RUNTIME_SCAN_TEST_INCLUDED

static void check_test_runtime_scan(CheckTest* test)
{
    static const uint32_t larger[] = { 255, 256, 257, 1023 };
    uint64_t state = UINT64_C(0x877002);
    for (uint32_t c = 0; c <= 129 + BUSTER_ARRAY_LENGTH(larger); c += 1)
    {
        const uint32_t count = c <= 129 ? c : larger[c - 130];
        for (uint32_t density = 0; density < 4; density += 1)
        {
            uint8_t* maps[8] = { 0 };
            uint64_t sizes[8] = { 0 };
            uint32_t* declarations = (uint32_t*)check_test_program_guarded(&maps[0], &sizes[0], (uint64_t)count * sizeof(uint32_t));
            uint32_t* nodes = (uint32_t*)check_test_program_guarded(&maps[1], &sizes[1], (uint64_t)count * sizeof(uint32_t));
            uint32_t* objects = (uint32_t*)check_test_program_guarded(&maps[2], &sizes[2], (uint64_t)count * sizeof(uint32_t));
            IdentId* names = (IdentId*)check_test_program_guarded(&maps[3], &sizes[3], (uint64_t)count * sizeof(IdentId));
            uint8_t* linkage = check_test_program_guarded(&maps[4], &sizes[4], count);
            SymbolId* object_symbols = (SymbolId*)check_test_program_guarded(&maps[5], &sizes[5], ((uint64_t)count + 1) * sizeof(SymbolId));
            uint32_t* tokens = (uint32_t*)check_test_program_guarded(&maps[6], &sizes[6], ((uint64_t)count + 16) * sizeof(uint32_t));
            bool ready = declarations != NULL && nodes != NULL && objects != NULL && names != NULL && linkage != NULL && object_symbols != NULL && tokens != NULL;
            IdentId identifiers[CHECK_LOWER_RUNTIMES] = { 0 };
            SymbolId initial[CHECK_LOWER_RUNTIMES];
            for (uint32_t r = 0; r < CHECK_LOWER_RUNTIMES; r += 1)
            {
                identifiers[r] = r % 4 != 0 ? 100 + r : 0;
                initial[r] = r == CHECK_LOWER_RUNTIME_ERRNO || r % 7 == 0 ? 900 + r : 0;
            }
            IdentId spelled_identifiers[CHECK_LOWER_RUNTIMES];
            uint16_t spelled_runtimes[CHECK_LOWER_RUNTIMES];
            uint32_t spelled = 0;
            for (uint32_t r = CHECK_LOWER_RUNTIME_ERRNO + 1; r < CHECK_LOWER_RUNTIMES; r += 1)
            {
                spelled_identifiers[spelled] = identifiers[r];
                spelled_runtimes[spelled] = (uint16_t)r;
                spelled += identifiers[r] != 0;
            }
            for (uint32_t t = 0; ready && t < count; t += 1)
            {
                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                const uint32_t random = (uint32_t)(state >> 32);
                const bool declared = density == 3 || (density != 0 && random % (density == 1 ? 16 : 2) == 0);
                declarations[t] = declared ? t : BUSTER_CHECK_NONE;
                nodes[t] = density != 0 && (random >> 4) % 3 == 0 ? 0 : BUSTER_CHECK_NONE;
                objects[t] = (random >> 6) % (count + 3);
                names[t] = (random >> 12) % 5 == 0 ? 0 : 100 + (random >> 16) % (CHECK_LOWER_RUNTIMES + 1);
                linkage[t] = (uint8_t)((random >> 24) % 3);
            }
            for (uint32_t o = 0; ready && o <= count; o += 1)
            {
                object_symbols[o] = o % 5 == 0 ? 0 : 1000 + o;
            }
            Parser parser = { 0 };
            parser.tokens.names = names;
            const CheckInput input = { .token_count = count, .declarations = declarations, .objects = objects, .linkage = linkage };
            const CheckProgram program = { .input = &input, .parser = &parser, .object_count = count + 1, .object_symbols = object_symbols };
            SymbolId expected[CHECK_LOWER_RUNTIMES];
            memcpy(expected, initial, sizeof(expected));
            // Independently select each runtime's earliest externally linked declaration with a nonzero symbol.
            for (uint32_t r = CHECK_LOWER_RUNTIME_ERRNO + 1; ready && r < CHECK_LOWER_RUNTIMES; r += 1)
            {
                for (uint32_t t = 0; identifiers[r] != 0 && expected[r] == 0 && t < count; t += 1)
                {
                    const uint32_t object = objects[t];
                    if (declarations[t] == t && names[t] == identifiers[r] && linkage[t] == 2 && object > 0 && object <= count)
                    {
                        expected[r] = object_symbols[object];
                    }
                }
            }
            for (uint32_t producer = 0; ready && producer < 2; producer += 1)
            {
                const uint32_t declared = check_program_declaring(declarations, nodes, 1, count, tokens, producer == 0);
                uint32_t* compact = (uint32_t*)check_test_program_guarded(&maps[7], &sizes[7], (uint64_t)declared * sizeof(uint32_t));
                ready &= compact != NULL;
                if (ready)
                {
                    if (declared != 0)
                    {
                        memcpy(compact, tokens, (uint64_t)declared * sizeof(uint32_t));
                    }
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        SymbolId actual[CHECK_LOWER_RUNTIMES];
                        memcpy(actual, initial, sizeof(actual));
                        check_lower_declared_runtimes(&program, path == 0 ? NULL : compact, declared, spelled_identifiers, spelled_runtimes, spelled, actual,
                                                      path == 0);
                        check_test_expect(test, memcmp(actual, expected, sizeof(actual)) == 0,
                                          "compact and full runtime scans preserve first symbols, linkage, absent names and preseeded slots at protected tails");
                    }
                }
                if (maps[7] != NULL)
                {
                    munmap(maps[7], sizes[7]);
                    maps[7] = NULL;
                }
            }
            check_test_expect(test, ready, "runtime declaration columns and exact-length compact list have protected tails");
            for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(maps); m += 1)
            {
                if (maps[m] != NULL)
                {
                    munmap(maps[m], sizes[m]);
                }
            }
        }
    }
    // Initializer-only matches, local linkage, invalid objects and a symbol-less declaration must not hide
    // the first usable declaration; a later declaration of the same name must not replace it.
    uint32_t declarations[] = { BUSTER_CHECK_NONE, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t nodes[] = { 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
    uint32_t objects[] = { 1, 1, 0, 4, 2, 1, 3, 3 };
    IdentId names[] = { 77, 77, 77, 77, 77, 77, 77, 0 };
    uint8_t linkage[] = { 2, 0, 2, 2, 2, 2, 2, 2 };
    SymbolId object_symbols[] = { 0, 51, 0, 61 };
    uint32_t tokens[24];
    Parser parser = { 0 };
    parser.tokens.names = names;
    const CheckInput input = { .token_count = 8, .declarations = declarations, .objects = objects, .linkage = linkage };
    const CheckProgram program = { .input = &input, .parser = &parser, .object_count = 4, .object_symbols = object_symbols };
    const IdentId identifiers[1] = { 77 };
    const uint16_t runtimes[1] = { CHECK_LOWER_RUNTIME_ERRNO + 1 };
    const uint32_t declared = check_program_declaring(declarations, nodes, 1, 8, tokens, false);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        SymbolId actual[CHECK_LOWER_RUNTIMES] = { 0 };
        actual[CHECK_LOWER_RUNTIME_ERRNO] = 123;
        check_lower_declared_runtimes(&program, tokens, declared, identifiers, runtimes, 1, actual, path == 0);
        check_test_expect(test, actual[CHECK_LOWER_RUNTIME_ERRNO + 1] == 51 && actual[CHECK_LOWER_RUNTIME_ERRNO] == 123,
                          "runtime lookup keeps the first nonzero external symbol and does not touch errno");
    }
}

#endif
