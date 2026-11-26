// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compiled only by the external C compiler: the helper's whole-buffer checksum, call count and write-back, checked
// against the values each scope must produce. The subject cannot inline the helper or see its memory effects.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int tier1_frame_scopes(int a);

static uint64_t calls;
static uint64_t hash;

void h_bytes(unsigned char* bytes, unsigned count)
{
    calls += 1;
    for (unsigned k = 0; k < count; k += 1)
    {
        hash = (hash ^ (bytes[k] + k * 257u)) * UINT64_C(0x100000001b3);
    }
    bytes[0] = (unsigned char)(bytes[0] + bytes[31] + 17u);
}

int main(void)
{
    static const int inputs[] = { INT_MIN, -1, 0, 1, 31, 32, 255, 256, 511, 512, 1023, 1024, 1534, 1535, 1536, INT_MAX };
    int result = 0;
    uint64_t combined = 0;
    for (unsigned sample = 0; sample < sizeof(inputs) / sizeof(inputs[0]); sample += 1)
    {
        const int a = inputs[sample];
        const unsigned visited = a >= 0 && a < 1536 ? (unsigned)a + 1 : 1536;
        const int expected = a >= 0 && a < 1536 ? (int)((unsigned char)((unsigned)a * 2u) + (unsigned char)a + 17u) % 256 : -7;
        uint64_t wanted_hash = 0;
        for (unsigned scope = 0; scope < visited; scope += 1)
        {
            for (unsigned k = 0; k < 32; k += 1)
            {
                const unsigned byte = k == 0 ? (unsigned char)((unsigned)a + scope) : k == 31 ? (unsigned char)scope : 0;
                wanted_hash = (wanted_hash ^ (byte + k * 257u)) * UINT64_C(0x100000001b3);
            }
        }
        calls = 0;
        hash = 0;
        const int actual = tier1_frame_scopes(a);
        if (actual != expected || calls != visited || hash != wanted_hash)
        {
            fprintf(stderr, "frame scopes a=%d actual=%d expected=%d calls=%llu expected_calls=%u hash=%llx expected_hash=%llx\n", a, actual, expected,
                    (unsigned long long)calls, visited, (unsigned long long)hash, (unsigned long long)wanted_hash);
            result = 1;
        }
        combined = combined * 31 + hash + (unsigned)actual;
    }
    printf("frame-scopes %016llx\n", (unsigned long long)combined);
    return result;
}
