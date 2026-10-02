// Independently authored complete SHA-1/SHA-256 intrinsic composition probe.
// Expected digests: NIST example-values sha1.pdf and sha256.pdf, abc and the
// 56-byte two-block example. No implementation code is copied from NIST.
typedef unsigned int U32;
typedef unsigned char U8;
typedef int V4 __attribute__((vector_size(16)));
extern int puts(const char*);
extern int dprintf(int, const char*, ...);

// Asymmetric volatile operands keep primitive checks independent of folding.
// Expected lanes were calculated from the Intel SDM scalar operation equations;
// the paired Clang SHA instruction run independently checks these fixed tables.
static volatile U32 primitive_input[3][4] = {
    {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u},
    {0x0f1e2d3cu, 0x4b5a6978u, 0x8796a5b4u, 0xc3d2e1f0u},
    {0x10293847u, 0x56473829u, 0xdeadbeefu, 0x76543210u},
};

static int sha_primitive_check(const char* operation, V4 const* actual, U32 const* expected)
{
    int result = 0;
    for (U32 lane = 0; lane < 4; lane += 1)
    {
        U32 value = (U32)(*actual)[lane];
        if (value != expected[lane])
        {
            dprintf(2, "sha primitive %s lane=%u got=%08x expected=%08x\n", operation, lane, value, expected[lane]);
            result = 1;
        }
    }
    return result;
}

__attribute__((target("sha,ssse3,sse4.1")))
static int sha_primitive_probe(void)
{
    static U32 const expected[10][4] = {
        {0x86b5e0d3u, 0x4a792c1fu, 0xffffffffu, 0xffffffffu},
        {0xc54cd45du, 0x0d6bc1a7u, 0x6b0da7c1u, 0xe3852f49u},
        {0x0f1e2d3cu, 0x4b5a6978u, 0x8796a5b4u, 0xe167ee74u},
        {0x9ca1dae1u, 0x7cfa715cu, 0xca766be2u, 0x94db1ab9u},
        {0xdce1d06bu, 0xca313780u, 0xae2146fau, 0x6b8829c4u},
        {0x69c82bb2u, 0x4ee8abf4u, 0x182d1ceeu, 0x1d14e611u},
        {0x33c405f9u, 0xfd5a1eb8u, 0x39aa8b81u, 0x29c3017du},
        {0x3e8111b3u, 0x8a2bdf80u, 0x217eee4bu, 0x69072c4au},
        {0x87707bf7u, 0xb6a25b1au, 0x3181a9e0u, 0xdd17d723u},
        {0xfef5a4d2u, 0x4d17c2fcu, 0x3ce4f1ceu, 0xf12a5687u},
    };
    V4 first = {(int)primitive_input[0][0], (int)primitive_input[0][1], (int)primitive_input[0][2], (int)primitive_input[0][3]};
    V4 second = {(int)primitive_input[1][0], (int)primitive_input[1][1], (int)primitive_input[1][2], (int)primitive_input[1][3]};
    V4 words = {(int)primitive_input[2][0], (int)primitive_input[2][1], (int)primitive_input[2][2], (int)primitive_input[2][3]};
    int result = 0;
    V4 actual;
    actual = __builtin_ia32_sha1msg1(first, second);
    result |= sha_primitive_check("sha1msg1", &actual, expected[0]);
    actual = __builtin_ia32_sha1msg2(first, second);
    result |= sha_primitive_check("sha1msg2", &actual, expected[1]);
    actual = __builtin_ia32_sha1nexte(first, second);
    result |= sha_primitive_check("sha1nexte", &actual, expected[2]);
    actual = __builtin_ia32_sha1rnds4(first, second, 0);
    result |= sha_primitive_check("sha1rnds4/0", &actual, expected[3]);
    actual = __builtin_ia32_sha1rnds4(first, second, 1);
    result |= sha_primitive_check("sha1rnds4/1", &actual, expected[4]);
    actual = __builtin_ia32_sha1rnds4(first, second, 2);
    result |= sha_primitive_check("sha1rnds4/2", &actual, expected[5]);
    actual = __builtin_ia32_sha1rnds4(first, second, 3);
    result |= sha_primitive_check("sha1rnds4/3", &actual, expected[6]);
    actual = __builtin_ia32_sha256msg1(first, second);
    result |= sha_primitive_check("sha256msg1", &actual, expected[7]);
    actual = __builtin_ia32_sha256msg2(first, second);
    result |= sha_primitive_check("sha256msg2", &actual, expected[8]);
    actual = __builtin_ia32_sha256rnds2(first, second, words);
    result |= sha_primitive_check("sha256rnds2", &actual, expected[9]);
    return result;
}

__attribute__((target("sha,ssse3,sse4.1")))
static void sha_blocks(const U8* bytes, U32 length, U32* sha1, U32* sha256)
{
    static U32 const constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    U8 padded[128] = {0};
    U32 padded_size = length < 56 ? 64 : 128;
    for (U32 index = 0; index < length; index += 1)
        padded[index] = bytes[index];
    padded[length] = 0x80;
    padded[padded_size - 2] = (U8)((length * 8) >> 8);
    padded[padded_size - 1] = (U8)(length * 8);
    for (U32 block = 0; block < padded_size; block += 64)
    {
        V4 schedule1[20];
        V4 schedule256[16];
        for (U32 group = 0; group < 4; group += 1)
        {
            for (U32 lane = 0; lane < 4; lane += 1)
            {
                U32 at = block + group * 16 + lane * 4;
                U32 word = ((U32)padded[at] << 24) | ((U32)padded[at + 1] << 16) |
                           ((U32)padded[at + 2] << 8) | (U32)padded[at + 3];
                schedule1[group][3 - lane] = (int)word;
                schedule256[group][lane] = (int)word;
            }
        }
        for (U32 group = 4; group < 20; group += 1)
        {
            V4 partial = __builtin_ia32_sha1msg1(schedule1[group - 4], schedule1[group - 3]);
            for (U32 lane = 0; lane < 4; lane += 1)
                partial[lane] ^= schedule1[group - 2][lane];
            schedule1[group] = __builtin_ia32_sha1msg2(partial, schedule1[group - 1]);
        }
        V4 state = {(int)sha1[3], (int)sha1[2], (int)sha1[1], (int)sha1[0]};
        V4 previous = state;
        for (U32 group = 0; group < 20; group += 1)
        {
            V4 message = schedule1[group];
            if (group)
                message = __builtin_ia32_sha1nexte(previous, message);
            else
                message[3] = (int)((U32)message[3] + sha1[4]);
            previous = state;
            if (group < 5)
                state = __builtin_ia32_sha1rnds4(state, message, 0);
            else if (group < 10)
                state = __builtin_ia32_sha1rnds4(state, message, 1);
            else if (group < 15)
                state = __builtin_ia32_sha1rnds4(state, message, 2);
            else
                state = __builtin_ia32_sha1rnds4(state, message, 3);
        }
        V4 saved_e = {0, 0, 0, (int)sha1[4]};
        V4 final_e = __builtin_ia32_sha1nexte(previous, saved_e);
        for (U32 index = 0; index < 4; index += 1)
            sha1[index] += (U32)state[3 - index];
        sha1[4] = (U32)final_e[3];

        for (U32 group = 4; group < 16; group += 1)
        {
            V4 partial = __builtin_ia32_sha256msg1(schedule256[group - 4], schedule256[group - 3]);
            for (U32 lane = 0; lane < 4; lane += 1)
            {
                U32 seventh = lane < 3 ? (U32)schedule256[group - 2][lane + 1] : (U32)schedule256[group - 1][0];
                partial[lane] = (int)((U32)partial[lane] + seventh);
            }
            schedule256[group] = __builtin_ia32_sha256msg2(partial, schedule256[group - 1]);
        }
        V4 abef = {(int)sha256[5], (int)sha256[4], (int)sha256[1], (int)sha256[0]};
        V4 cdgh = {(int)sha256[7], (int)sha256[6], (int)sha256[3], (int)sha256[2]};
        for (U32 group = 0; group < 16; group += 1)
        {
            V4 first = {(int)((U32)schedule256[group][0] + constants[group * 4]),
                        (int)((U32)schedule256[group][1] + constants[group * 4 + 1]), 0x12345678, -1};
            cdgh = __builtin_ia32_sha256rnds2(cdgh, abef, first);
            V4 second = {(int)((U32)schedule256[group][2] + constants[group * 4 + 2]),
                         (int)((U32)schedule256[group][3] + constants[group * 4 + 3]), -1, 0x76543210};
            abef = __builtin_ia32_sha256rnds2(abef, cdgh, second);
        }
        sha256[0] += (U32)abef[3];
        sha256[1] += (U32)abef[2];
        sha256[2] += (U32)cdgh[3];
        sha256[3] += (U32)cdgh[2];
        sha256[4] += (U32)abef[1];
        sha256[5] += (U32)abef[0];
        sha256[6] += (U32)cdgh[1];
        sha256[7] += (U32)cdgh[0];
    }
    return;
}

int main(void)
{
    static U32 const expected1[2][5] = {
        {0xa9993e36u, 0x4706816au, 0xba3e2571u, 0x7850c26cu, 0x9cd0d89du},
        {0x84983e44u, 0x1c3bd26eu, 0xbaae4aa1u, 0xf95129e5u, 0xe54670f1u},
    };
    static U32 const expected256[2][8] = {
        {0xba7816bfu, 0x8f01cfeau, 0x414140deu, 0x5dae2223u, 0xb00361a3u, 0x96177a9cu, 0xb410ff61u, 0xf20015adu},
        {0x248d6a61u, 0xd20638b8u, 0xe5c02693u, 0x0c3e6039u, 0xa33ce459u, 0x64ff2167u, 0xf6ecedd4u, 0x19db06c1u},
    };
    static U8 const first[] = "abc";
    static U8 const second[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    int result = sha_primitive_probe();
    for (U32 test = 0; test < 2; test += 1)
    {
        U32 sha1[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
        U32 sha256[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        sha_blocks(test ? second : first, test ? 56 : 3, sha1, sha256);
        for (U32 index = 0; index < 5; index += 1)
        {
            if (sha1[index] != expected1[test][index])
            {
                dprintf(2, "sha NIST sha1 test=%u word=%u got=%08x expected=%08x\n", test, index, sha1[index], expected1[test][index]);
                result = 1;
            }
        }
        for (U32 index = 0; index < 8; index += 1)
        {
            if (sha256[index] != expected256[test][index])
            {
                dprintf(2, "sha NIST sha256 test=%u word=%u got=%08x expected=%08x\n", test, index, sha256[index], expected256[test][index]);
                result = 1;
            }
        }
    }
    if (!result)
        puts("sha1/sha256 NIST vectors ok");
    return result;
}
