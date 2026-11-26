// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3644: 1,536 initialized buffers with calls and early exits. On the old selector their entry-defined addresses
// exceeded the unchanged interval cap. The external callee observes every initialized byte and changes the returned
// byte. Clang compiles the compact loop for a reference; Tier 1 compiles the expanded, separate lexical scopes.

void h_bytes(unsigned char* bytes, unsigned count);

#define BUSTER_FRAME_SCOPE(i) \
    { \
        unsigned char bytes[32] = {0}; \
        bytes[0] = (unsigned char)((unsigned)a + (unsigned)(i)); \
        bytes[31] = (unsigned char)(i); \
        h_bytes(bytes, 32u); \
        if (a == (int)(i)) \
        { \
            return (int)bytes[0]; \
        } \
    }

#define BUSTER_FRAME_SCOPES_2(i) BUSTER_FRAME_SCOPE(i) BUSTER_FRAME_SCOPE((i) + 1)
#define BUSTER_FRAME_SCOPES_4(i) BUSTER_FRAME_SCOPES_2(i) BUSTER_FRAME_SCOPES_2((i) + 2)
#define BUSTER_FRAME_SCOPES_8(i) BUSTER_FRAME_SCOPES_4(i) BUSTER_FRAME_SCOPES_4((i) + 4)
#define BUSTER_FRAME_SCOPES_16(i) BUSTER_FRAME_SCOPES_8(i) BUSTER_FRAME_SCOPES_8((i) + 8)
#define BUSTER_FRAME_SCOPES_32(i) BUSTER_FRAME_SCOPES_16(i) BUSTER_FRAME_SCOPES_16((i) + 16)
#define BUSTER_FRAME_SCOPES_64(i) BUSTER_FRAME_SCOPES_32(i) BUSTER_FRAME_SCOPES_32((i) + 32)
#define BUSTER_FRAME_SCOPES_128(i) BUSTER_FRAME_SCOPES_64(i) BUSTER_FRAME_SCOPES_64((i) + 64)
#define BUSTER_FRAME_SCOPES_256(i) BUSTER_FRAME_SCOPES_128(i) BUSTER_FRAME_SCOPES_128((i) + 128)
#define BUSTER_FRAME_SCOPES_512(i) BUSTER_FRAME_SCOPES_256(i) BUSTER_FRAME_SCOPES_256((i) + 256)
#define BUSTER_FRAME_SCOPES_1024(i) BUSTER_FRAME_SCOPES_512(i) BUSTER_FRAME_SCOPES_512((i) + 512)

int tier1_frame_scopes(int a)
{
#ifdef BUSTER_FRAME_REFERENCE
    for (int i = 0; i < 1536; i += 1)
    {
        BUSTER_FRAME_SCOPE(i)
    }
#else
    BUSTER_FRAME_SCOPES_1024(0)
    BUSTER_FRAME_SCOPES_512(1024)
#endif
    return -7;
}
