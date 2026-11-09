// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typo correction's distances (#1447, c_check_typo.c): the vector path's bit-parallel Levenshtein distances, 16 candidates
// at a time, against the scalar path's dynamic programme, their twin and oracle, on random strings of every typo length
// the lanes hold (1 to 32 bytes) and candidates up to the length filter's longest (43 bytes), over a small alphabet, which
// makes near strings, and a wide one, which makes far ones. The driver's fixtures compare the corrections with clang's
// (c_check_typo_driver_test.c). Include after c_check_input.c.

static void check_test_typo(CheckTest* test)
{
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    uint32_t mismatches = 0;
    uint32_t compared = 0;
    for (uint32_t round = 0; round < 20000; round += 1)
    {
        uint8_t typo[CHECK_TYPO_LANE_BITS];
        uint8_t candidates[16][CHECK_TYPO_LANE_BITS + CHECK_TYPO_LANE_BITS / 3 + 1];
        const uint8_t* lanes[16];
        uint32_t lengths[16];
        const char* alphabet = round % 3 == 0 ? "abcdefgh_XYZ0129" : "abcd";
        const uint32_t letters = (uint32_t)strlen(alphabet);
        state = state * UINT64_C(6364136223846793005) + 1;
        const uint32_t length = 1 + (uint32_t)(state >> 33) % CHECK_TYPO_LANE_BITS;
        for (uint32_t i = 0; i < length; i += 1)
        {
            state = state * UINT64_C(6364136223846793005) + 1;
            typo[i] = (uint8_t)alphabet[(state >> 33) % letters];
        }
        for (uint32_t lane = 0; lane < 16; lane += 1)
        {
            state = state * UINT64_C(6364136223846793005) + 1;
            lengths[lane] = (uint32_t)(state >> 33) % (CHECK_TYPO_LANE_BITS + CHECK_TYPO_LANE_BITS / 3 + 1);
            for (uint32_t j = 0; j < lengths[lane]; j += 1)
            {
                state = state * UINT64_C(6364136223846793005) + 1;
                // Half the candidates start as the typo, so that some are near it.
                candidates[lane][j] = lane % 2 == 0 && j < length && (state >> 40) % 4 != 0 ? typo[j] : (uint8_t)alphabet[(state >> 33) % letters];
            }
            lanes[lane] = candidates[lane];
        }
        uint32_t expected[16];
        for (uint32_t lane = 0; lane < 16; lane += 1)
        {
            expected[lane] = check_typo_distance(typo, length, lanes[lane], lengths[lane]);
        }
#if BUSTER_HOST_VECTOR
        uint32_t peq[256];
        memset(peq, 0, sizeof(peq));
        for (uint32_t i = 0; i < length; i += 1)
        {
            peq[typo[i]] |= 1u << i;
        }
        uint32_t actual[16];
        const uint32_t used = 1 + round % 16;
        check_typo_distances(peq, length, lanes, lengths, used, actual);
        for (uint32_t lane = 0; lane < used; lane += 1)
        {
            mismatches += actual[lane] != expected[lane];
            compared += 1;
        }
#endif
        // The dynamic programme's own invariants: symmetric, and at least the lengths' difference.
        for (uint32_t lane = 0; lane < 16; lane += 1)
        {
            const uint32_t reverse = check_typo_distance(lanes[lane], lengths[lane], typo, length);
            const uint32_t delta = lengths[lane] > length ? lengths[lane] - length : length - lengths[lane];
            mismatches += reverse != expected[lane] || expected[lane] < delta;
        }
    }
    char description[160];
    snprintf(description, sizeof(description), "typo correction: %u bit-parallel distances equal the dynamic programme's, which is symmetric (%u differ)",
             compared, mismatches);
    check_test_expect(test, mismatches == 0, description);
    // Known distances: clang's StringRef::edit_distance with replacements (a transposition is two).
    check_test_expect(test, check_typo_distance((const uint8_t*)"countr", 6, (const uint8_t*)"counter", 7) == 1 &&
                      check_typo_distance((const uint8_t*)"abcz", 4, (const uint8_t*)"abcx", 4) == 1 &&
                      check_typo_distance((const uint8_t*)"recieve", 7, (const uint8_t*)"receive", 7) == 2 &&
                      check_typo_distance((const uint8_t*)"", 0, (const uint8_t*)"abc", 3) == 3,
                      "typo correction: known distances, a transposition counting two");
}
