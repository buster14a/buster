/* First-party byte storage and file integrity for offline performance tools.
 * Only the C/POSIX runtime is required. Formats are read as bounded slices.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define BUSTER_PERF_BYTE_LIMIT (64u * 1024u * 1024u)
#define BUSTER_PERF_NODE_LIMIT 262144u
#define BUSTER_PERF_DEPTH_LIMIT 128u

typedef struct PerfBytes PerfBytes;
struct PerfBytes
{
    const unsigned char *data;
    size_t size;
};

typedef struct PerfBuffer PerfBuffer;
struct PerfBuffer
{
    unsigned char *data;
    size_t size;
    size_t capacity;
};

static PerfBytes perf_literal(const char *text)
{
    PerfBytes result = {
        .data = (const unsigned char *)text,
        .size = strlen(text)
    };
    return result;
}

static PerfBytes perf_buffer_bytes(const PerfBuffer *buffer)
{
    PerfBytes result = {
        .data = buffer->data,
        .size = buffer->size
    };
    return result;
}

static bool perf_bytes_equal(PerfBytes left, PerfBytes right)
{
    bool result = left.size == right.size && (left.size == 0 || memcmp(left.data, right.data, left.size) == 0);
    return result;
}

static bool perf_buffer_append(PerfBuffer *buffer, PerfBytes bytes)
{
    bool result = buffer->size <= BUSTER_PERF_BYTE_LIMIT && bytes.size <= BUSTER_PERF_BYTE_LIMIT - buffer->size;
    size_t needed = result ? buffer->size + bytes.size : 0;
    if (result && needed > buffer->capacity)
    {
        size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
        while (capacity < needed)
        {
            capacity = capacity > BUSTER_PERF_BYTE_LIMIT / 2 ? BUSTER_PERF_BYTE_LIMIT : capacity * 2;
        }
        unsigned char *grown = realloc(buffer->data, capacity);
        result = grown != NULL;
        if (result)
        {
            buffer->data = grown;
            buffer->capacity = capacity;
        }
    }
    if (result && bytes.size != 0)
    {
        memcpy(buffer->data + buffer->size, bytes.data, bytes.size);
        buffer->size = needed;
    }
    return result;
}

static bool perf_buffer_byte(PerfBuffer *buffer, unsigned char byte)
{
    PerfBytes bytes = {
        .data = &byte,
        .size = 1
    };
    bool result = perf_buffer_append(buffer, bytes);
    return result;
}

static bool perf_utf8(PerfBytes text)
{
    bool result = true;
    for (size_t i = 0; i < text.size && result;)
    {
        unsigned int first = text.data[i++];
        unsigned int width = first < 0x80 ? 0 : first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 4;
        uint32_t point = first & (width == 1 ? 31 : width == 2 ? 15 : width == 3 ? 7 : 127);
        result = width <= 3 && width <= text.size - i;
        for (unsigned int c = 0; c < width && result; ++c)
        {
            unsigned char next = text.data[i++];
            result = (next & 0xc0) == 0x80;
            point = (point << 6) | (next & 63);
        }
        result = result && (width == 0 || point >= (width == 1 ? 0x80u : width == 2 ? 0x800u : 0x10000u)) && point <= 0x10ffff && (point < 0xd800 || point > 0xdfff);
    }
    return result;
}

static bool perf_json_quote(PerfBuffer *buffer, PerfBytes text)
{
    static const char hex[] = "0123456789abcdef";
    bool result = perf_utf8(text) && perf_buffer_byte(buffer, '"');
    for (size_t i = 0; i < text.size && result; ++i)
    {
        unsigned char byte = text.data[i];
        if (byte == '"' || byte == '\\')
        {
            result = perf_buffer_byte(buffer, '\\') && perf_buffer_byte(buffer, byte);
        }
        else if (byte < 32)
        {
            unsigned char escaped[] = {'\\', 'u', '0', '0', (unsigned char)hex[byte >> 4], (unsigned char)hex[byte & 15]};
            result = perf_buffer_append(buffer, (PerfBytes){.data = escaped, .size = sizeof(escaped)});
        }
        else
        {
            result = perf_buffer_byte(buffer, byte);
        }
    }
    result = result && perf_buffer_byte(buffer, '"');
    return result;
}

static bool perf_read_file(const char *path, PerfBuffer *buffer)
{
    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat information;
    bool result = descriptor >= 0 && fstat(descriptor, &information) == 0 && S_ISREG(information.st_mode) && information.st_size >= 0 && information.st_size <= BUSTER_PERF_BYTE_LIMIT;
    if (result)
    {
        unsigned char block[65536];
        bool done = false;
        while (result && !done)
        {
            ssize_t size = read(descriptor, block, sizeof(block));
            if (size > 0)
            {
                result = perf_buffer_append(buffer, (PerfBytes){.data = block, .size = (size_t)size});
            }
            else if (size == 0)
            {
                done = true;
            }
            else
            {
                result = errno == EINTR;
            }
        }
    }
    if (descriptor >= 0)
    {
        result = close(descriptor) == 0 && result;
    }
    return result;
}

static uint32_t perf_rotate_right(uint32_t value, unsigned int count)
{
    uint32_t result = (value >> count) | (value << (32 - count));
    return result;
}

typedef struct PerfSha256 PerfSha256;
struct PerfSha256
{
    uint32_t state[8];
    uint64_t bytes;
    unsigned char tail[64];
    size_t tail_size;
};

static void perf_sha256_block(PerfSha256 *hash, const unsigned char block[64])
{
    /* FIPS 180-4 section 4.2.2 constants are mathematical format facts. This
     * round implementation is original code, not an imported crypto library. */
    static const uint32_t constants[64] = {
        0x428a2f98,
        0x71374491,
        0xb5c0fbcf,
        0xe9b5dba5,
        0x3956c25b,
        0x59f111f1,
        0x923f82a4,
        0xab1c5ed5,
        0xd807aa98,
        0x12835b01,
        0x243185be,
        0x550c7dc3,
        0x72be5d74,
        0x80deb1fe,
        0x9bdc06a7,
        0xc19bf174,
        0xe49b69c1,
        0xefbe4786,
        0x0fc19dc6,
        0x240ca1cc,
        0x2de92c6f,
        0x4a7484aa,
        0x5cb0a9dc,
        0x76f988da,
        0x983e5152,
        0xa831c66d,
        0xb00327c8,
        0xbf597fc7,
        0xc6e00bf3,
        0xd5a79147,
        0x06ca6351,
        0x14292967,
        0x27b70a85,
        0x2e1b2138,
        0x4d2c6dfc,
        0x53380d13,
        0x650a7354,
        0x766a0abb,
        0x81c2c92e,
        0x92722c85,
        0xa2bfe8a1,
        0xa81a664b,
        0xc24b8b70,
        0xc76c51a3,
        0xd192e819,
        0xd6990624,
        0xf40e3585,
        0x106aa070,
        0x19a4c116,
        0x1e376c08,
        0x2748774c,
        0x34b0bcb5,
        0x391c0cb3,
        0x4ed8aa4a,
        0x5b9cca4f,
        0x682e6ff3,
        0x748f82ee,
        0x78a5636f,
        0x84c87814,
        0x8cc70208,
        0x90befffa,
        0xa4506ceb,
        0xbef9a3f7,
        0xc67178f2
    };
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i)
    {
        words[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 | block[4 * i + 3];
    }
    for (size_t i = 16; i < 64; ++i)
    {
        uint32_t a = words[i - 15];
        uint32_t b = words[i - 2];
        uint32_t low = perf_rotate_right(a, 7) ^ perf_rotate_right(a, 18) ^ (a >> 3);
        uint32_t high = perf_rotate_right(b, 17) ^ perf_rotate_right(b, 19) ^ (b >> 10);
        words[i] = words[i - 16] + low + words[i - 7] + high;
    }
    uint32_t a = hash->state[0];
    uint32_t b = hash->state[1];
    uint32_t c = hash->state[2];
    uint32_t d = hash->state[3];
    uint32_t e = hash->state[4];
    uint32_t f = hash->state[5];
    uint32_t g = hash->state[6];
    uint32_t h = hash->state[7];
    for (size_t i = 0; i < 64; ++i)
    {
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t sum_a = perf_rotate_right(a, 2) ^ perf_rotate_right(a, 13) ^ perf_rotate_right(a, 22);
        uint32_t sum_e = perf_rotate_right(e, 6) ^ perf_rotate_right(e, 11) ^ perf_rotate_right(e, 25);
        uint32_t first = h + sum_e + choose + constants[i] + words[i];
        uint32_t second = sum_a + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
    hash->state[5] += f;
    hash->state[6] += g;
    hash->state[7] += h;
}

static PerfSha256 perf_sha256_start(void)
{
    PerfSha256 result = {
        .state = {
            0x6a09e667,
            0xbb67ae85,
            0x3c6ef372,
            0xa54ff53a,
            0x510e527f,
            0x9b05688c,
            0x1f83d9ab,
            0x5be0cd19
        }
    };
    return result;
}

static bool perf_sha256_add(PerfSha256 *hash, PerfBytes bytes)
{
    bool result = bytes.size <= UINT64_MAX / 8 - hash->bytes;
    if (result)
    {
        hash->bytes += bytes.size;
        size_t offset = 0;
        while (offset < bytes.size)
        {
            size_t size = 64 - hash->tail_size;
            if (size > bytes.size - offset)
            {
                size = bytes.size - offset;
            }
            memcpy(hash->tail + hash->tail_size, bytes.data + offset, size);
            hash->tail_size += size;
            offset += size;
            if (hash->tail_size == 64)
            {
                perf_sha256_block(hash, hash->tail);
                hash->tail_size = 0;
            }
        }
    }
    return result;
}

static void perf_sha256_finish(PerfSha256 *hash, char hex[65])
{
    uint64_t bits = hash->bytes * 8;
    hash->tail[hash->tail_size++] = 0x80;
    if (hash->tail_size > 56)
    {
        memset(hash->tail + hash->tail_size, 0, 64 - hash->tail_size);
        perf_sha256_block(hash, hash->tail);
        hash->tail_size = 0;
    }
    memset(hash->tail + hash->tail_size, 0, 56 - hash->tail_size);
    for (size_t i = 0; i < 8; ++i)
    {
        hash->tail[63 - i] = (unsigned char)(bits >> (8 * i));
    }
    perf_sha256_block(hash, hash->tail);
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i)
    {
        unsigned char byte = (unsigned char)(hash->state[i / 4] >> (24 - 8 * (i % 4)));
        hex[2 * i] = digits[byte >> 4];
        hex[2 * i + 1] = digits[byte & 15];
    }
    hex[64] = 0;
}

static bool perf_sha256(PerfBytes bytes, char hex[65])
{
    PerfSha256 hash = perf_sha256_start();
    bool result = perf_sha256_add(&hash, bytes);
    if (result)
    {
        perf_sha256_finish(&hash, hex);
    }
    return result;
}
