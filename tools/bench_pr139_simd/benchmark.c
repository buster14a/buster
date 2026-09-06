// Opt-in validation of the exact PR139 kernels extracted by prepare.py.
// This isolates transformation cost: the quoted decoder receives a reused
// output buffer and the interner is replaced by an ordered-index recorder.
#include <buster/lib/simd.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct Arena Arena;
struct Arena { u8* data; };
#define arena_allocate(arena, type, count) ((void)(count), (type*)(arena)->data)
typedef struct CToken CToken;
struct CToken { u32 offset; u32 symbol; u16 length; u8 kind; u8 punctuator; };
typedef u8 CTokenShape;
enum { C_TOKEN_IDENTIFIER = 2 };
typedef struct CSymbolTable CSymbolTable;
struct CSymbolTable { u32* indices; u64 count; char8 const* base; };
static String8 c_token_spelling(char8 const* base, CToken token)
{
    return (String8){(char8*)base + token.offset, token.length};
}
static u32 c_symbol_intern(CSymbolTable* table, String8 spelling)
{
    u32 index = (u32)(spelling.pointer - table->base);
    table->indices[table->count++] = index;
    return index + 1;
}
#include "kernels.inc"

typedef struct Record Record;
struct Record { u8* bytes; u64 length; };
typedef struct Corpus Corpus;
struct Corpus { Record* records; u32 count; u64 bytes; };
typedef struct Run Run;
struct Run { CToken* tokens; u8* shapes; u64 count; };
static u64 gate_count;
static u64 failures;
static volatile u64 sink;

static void check(bool condition, char const* message)
{
    gate_count += 1;
    if (!condition)
    {
        failures += 1;
        if (failures < 10) fprintf(stderr, "FAIL %s at gate %llu\n", message, (unsigned long long)gate_count);
    }
}

static void require(bool condition, char const* message)
{
    check(condition, message);
    if (!condition)
    {
        exit(EXIT_FAILURE);
    }
}

static u64 clock_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (u64)value.tv_sec * 1000000000u + (u64)value.tv_nsec;
}

static u32 read_u32(FILE* file)
{
    u8 bytes[4] = {0};
    require(fread(bytes, 1, 4, file) == 4, "corpus u32");
    return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

static Corpus read_corpus(FILE* file)
{
    Corpus result = {0};
    result.count = read_u32(file);
    result.records = calloc(result.count, sizeof(Record));
    require(result.count == 0 || result.records != 0, "corpus records allocation");
    for (u32 index = 0; index < result.count; index += 1)
    {
        Record* record = result.records + index;
        record->length = read_u32(file);
        record->bytes = malloc(record->length + 1);
        require(record->bytes != 0, "corpus bytes allocation");
        require(fread(record->bytes, 1, record->length, file) == record->length, "corpus bytes");
        result.bytes += record->length;
    }
    return result;
}

static u64 random_step(u64* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void base64_case(u8 const* input, u64 length, u64 groups, u8* left, u8* right)
{
    memset(left, 0xa5, groups * 3 + 16);
    memset(right, 0xa5, groups * 3 + 16);
    buster_x86_metadata_decode_base64_chunk_scalar(left, (char8 const*)input, length, groups);
    buster_x86_metadata_decode_base64_chunk_avx512(right, (char8 const*)input, length, groups);
    check(memcmp(left, right, groups * 3 + 16) == 0, "base64 differential");
    for (u64 index = groups * 3; index < groups * 3 + 16; index += 1)
    {
        check(right[index] == 0xa5, "base64 tail canary");
    }
}

static void quoted_case(u8 const* input, u64 length, u8* left, u8* right)
{
    Arena a = {left};
    Arena b = {right};
    ByteSlice scalar = {0};
    ByteSlice vector = {0};
    u64 count = 0;
    memset(left, 0xa5, length + 16);
    memset(right, 0xa5, length + 16);
    String8 spelling = {(char8*)input, length};
    bool scalar_ok = c_ir_decode_quoted_reference(&a, spelling, '"', &scalar);
    bool vector_ok = c_ir_decode_quoted(&b, spelling, '"', &vector);
    bool count_ok = c_ir_count_quoted(spelling, '"', &count);
    check(scalar_ok == vector_ok && scalar_ok == count_ok, "quoted acceptance");
    if (scalar_ok && vector_ok)
    {
        check(scalar.length == vector.length && vector.length == count, "quoted size");
        check(scalar.length == vector.length && memcmp(scalar.pointer, vector.pointer, scalar.length) == 0, "quoted bytes");
    }
    for (u64 index = length; index < length + 16; index += 1)
    {
        check(right[index] == 0xa5, "quoted tail canary");
    }
}

static void intern_case(u8 const* shapes, u64 count, CToken* left, CToken* right, u32* indices_left, u32* indices_right, char8 const* base)
{
    for (u64 index = 0; index < count; index += 1)
    {
        left[index] = (CToken){(u32)index, 0, 1, shapes[index] == 2 ? 2 : 6, 0};
        right[index] = left[index];
    }
    CSymbolTable a = {indices_left, 0, base};
    CSymbolTable b = {indices_right, 0, base};
    c_symbols_intern_tokens(&a, base, left, 0, count);
    c_symbols_intern_tokens(&b, base, right, shapes, count);
    check(a.count == b.count && memcmp(indices_left, indices_right, a.count * sizeof(u32)) == 0, "identifier stable indices");
    check(memcmp(left, right, count * sizeof(CToken)) == 0, "identifier token writes");
}

static u64 bench_base64(Corpus corpus, u8* output, u32 repeats, bool vector)
{
    u64 total = 0;
    if (vector)
    {
        for (u32 repeat = 0; repeat < repeats; repeat += 1)
        {
            for (u32 index = 0; index < corpus.count; index += 1)
            {
                Record record = corpus.records[index];
                buster_x86_metadata_decode_base64_chunk_avx512(output, (char8*)record.bytes, record.length, record.length / 4);
                total += output[record.length / 4 * 3 - 1];
            }
        }
    }
    else
    {
        for (u32 repeat = 0; repeat < repeats; repeat += 1)
        {
            for (u32 index = 0; index < corpus.count; index += 1)
            {
                Record record = corpus.records[index];
                buster_x86_metadata_decode_base64_chunk_scalar(output, (char8*)record.bytes, record.length, record.length / 4);
                total += output[record.length / 4 * 3 - 1];
            }
        }
    }
    sink = total;
    return total;
}

static u64 bench_quoted(Corpus corpus, u8* output, u32 repeats, bool vector)
{
    u64 total = 0;
    Arena arena = {output};
    if (vector)
    {
        for (u32 repeat = 0; repeat < repeats; repeat += 1)
        {
            for (u32 index = 0; index < corpus.count; index += 1)
            {
                Record record = corpus.records[index];
                ByteSlice decoded = {0};
                bool accepted = c_ir_decode_quoted(&arena, (String8){(char8*)record.bytes, record.length}, '"', &decoded);
                total += accepted ? decoded.length + (decoded.length ? decoded.pointer[decoded.length - 1] : 0) : 1;
            }
        }
    }
    else
    {
        for (u32 repeat = 0; repeat < repeats; repeat += 1)
        {
            for (u32 index = 0; index < corpus.count; index += 1)
            {
                Record record = corpus.records[index];
                ByteSlice decoded = {0};
                bool accepted = c_ir_decode_quoted_reference(&arena, (String8){(char8*)record.bytes, record.length}, '"', &decoded);
                total += accepted ? decoded.length + (decoded.length ? decoded.pointer[decoded.length - 1] : 0) : 1;
            }
        }
    }
    sink = total;
    return total;
}

static u64 bench_intern(Run* runs, u32 count, u32* output, char8 const* base, u32 repeats, bool vector)
{
    u64 total = 0;
    for (u32 repeat = 0; repeat < repeats; repeat += 1)
    {
        for (u32 index = 0; index < count; index += 1)
        {
            Run run = runs[index];
            CSymbolTable table = {output, 0, base};
            c_symbols_intern_tokens(&table, base, run.tokens, vector ? run.shapes : 0, run.count);
            total += table.count + (table.count ? output[table.count - 1] : 0);
        }
    }
    sink = total;
    return total;
}

int main(int argc, char** argv)
{
    int result = 1;
    if (argc != 3)
    {
        fprintf(stderr, "usage: benchmark corpus.bin [check|timing]\n");
    }
    else
    {
        FILE* file = fopen(argv[1], "rb");
        require(file != 0, "open corpus");
        Corpus base64 = read_corpus(file);
        Corpus quoted = read_corpus(file);
        Corpus shapes = read_corpus(file);
        fclose(file);
        buster_x86_metadata_fill_base64_values();
        u64 maximum_count = 131072;
        for (u32 index = 0; index < shapes.count; index += 1) maximum_count = BUSTER_MAX(maximum_count, shapes.records[index].length);
        u64 buffer_capacity = 131072;
        for (u32 index = 0; index < base64.count; index += 1) buffer_capacity = BUSTER_MAX(buffer_capacity, base64.records[index].length + 64);
        for (u32 index = 0; index < quoted.count; index += 1) buffer_capacity = BUSTER_MAX(buffer_capacity, quoted.records[index].length + 64);
        buffer_capacity = (buffer_capacity + 63) & ~(u64)63;
        u8* left = aligned_alloc(64, buffer_capacity);
        u8* right = aligned_alloc(64, buffer_capacity);
        CToken* tokens_left = calloc(maximum_count, sizeof(CToken));
        CToken* tokens_right = calloc(maximum_count, sizeof(CToken));
        u32* indices_left = calloc(maximum_count, sizeof(u32));
        u32* indices_right = calloc(maximum_count, sizeof(u32));
        char8* spelling_base = malloc(maximum_count);
        Run* runs = calloc(shapes.count, sizeof(Run));
        require(left && right && tokens_left && tokens_right && indices_left && indices_right && spelling_base && (runs || shapes.count == 0), "benchmark buffers allocation");
        for (u32 index = 0; index < shapes.count; index += 1)
        {
            Record record = shapes.records[index];
            runs[index] = (Run){calloc(record.length, sizeof(CToken)), record.bytes, record.length};
            require(record.length == 0 || runs[index].tokens != 0, "token run allocation");
            for (u64 token = 0; token < record.length; token += 1) runs[index].tokens[token] = (CToken){(u32)token, 0, 1, record.bytes[token] == 2 ? 2 : 6, 0};
        }
        if (strcmp(argv[2], "check") == 0)
        {
            for (u32 index = 0; index < base64.count; index += 1)
            {
                Record record = base64.records[index];
                base64_case(record.bytes, record.length, record.length / 4, left, right);
            }
            for (u32 index = 0; index < quoted.count; index += 1)
            {
                Record record = quoted.records[index];
                quoted_case(record.bytes, record.length, left, right);
            }
            for (u32 index = 0; index < shapes.count; index += 1)
            {
                Record record = shapes.records[index];
                intern_case(record.bytes, record.length, tokens_left, tokens_right, indices_left, indices_right, spelling_base);
            }
            u8* input = malloc(131072);
            require(input != 0, "synthetic input allocation");
            u64 state = 0x51ecbeef12345u;
            for (u64 index = 0; index < 131072; index += 1) input[index] = (u8)random_step(&state);
            for (u64 phase = 0; phase < 64; phase += 1)
            {
                for (u64 length = 0; length < 261; length += 1) base64_case(input + phase, length, (length + 3) / 4 + (length % 5), left, right);
            }
            char const* escapes[] = {"\\0", "\\377", "\\400", "\\n", "\\t", "\\e", "\\\\", "\\\"", "\\x7f", "\\x100", "\\x", "\\u0041", "\\uD800", "\\U0001F600", "\\U00110000", "\\z", "\\"};
            for (u64 position = 0; position <= 200; position += 1)
            {
                for (u64 escape = 0; escape < BUSTER_ARRAY_LENGTH(escapes); escape += 1)
                {
                    u64 length = strlen(escapes[escape]);
                    memset(input, 'q', 512);
                    input[0] = '"';
                    memcpy(input + position + 1, escapes[escape], length);
                    input[position + length + 3] = '"';
                    quoted_case(input, position + length + 4, left, right);
                }
            }
            u64 lengths[] = {0,1,2,31,32,33,63,64,65,127,128,129,255,256,257,4095,4096,4097,70000};
            for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(lengths); index += 1)
            {
                u64 length = lengths[index];
                memset(input, 'r', length + 2);
                input[0] = '"';
                input[length + 1] = '"';
                quoted_case(input, length + 2, left, right);
            }
            for (u32 iteration = 0; iteration < 10000; iteration += 1)
            {
                u64 length = random_step(&state) % 4098;
                for (u64 index = 0; index < length; index += 1) input[index + 1] = (u8)random_step(&state);
                input[0] = '"';
                input[length + 1] = '"';
                quoted_case(input, length + 2, left, right);
            }
            for (u64 length = 0; length <= 257; length += 1)
            {
                for (u64 phase = 0; phase < 64; phase += 1)
                {
                    for (u64 index = 0; index < length; index += 1) input[phase + index] = (u8)(random_step(&state) % 5 == 0 ? 2 : 128);
                    intern_case(input + phase, length, tokens_left, tokens_right, indices_left, indices_right, spelling_base);
                }
            }
            // Guard the immediately following page: masked source tails must
            // not read a full vector past the final valid byte.
            u64 page = (u64)sysconf(_SC_PAGESIZE);
            u8* guarded = mmap(0, page * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            require(guarded != MAP_FAILED && mprotect(guarded + page * 2, page, PROT_NONE) == 0, "guard setup");
            for (u64 length = 0; length < 260; length += 1)
            {
                u8* ending = guarded + page * 2 - length;
                memset(ending, 'A', length);
                base64_case(ending, length, (length + 3) / 4 + 3, left, right);
                memset(ending, 128, length);
                if (length) ending[length - 1] = 2;
                intern_case(ending, length, tokens_left, tokens_right, indices_left, indices_right, spelling_base);
                if (length >= 2)
                {
                    memset(ending, 'a', length);
                    ending[0] = '"';
                    ending[length - 1] = '"';
                    quoted_case(ending, length, left, right);
                }
            }
            printf("DIFFERENTIAL gates=%llu failures=%llu\n", (unsigned long long)gate_count, (unsigned long long)failures);
        }
        else
        {
            for (u32 pair = 0; pair < 7; pair += 1)
            {
                for (u32 part = 0; part < 2; part += 1)
                {
                    bool vector = (part ^ (pair & 1)) != 0;
                    u32 repeats = 64;
                    u64 start = clock_ns();
                    u64 sum = bench_base64(base64, left, repeats, vector);
                    u64 elapsed = clock_ns() - start;
                    printf("TIMING base64 pair=%u vector=%u ns=%llu units=%llu sum=%llu\n", pair, vector, (unsigned long long)elapsed, (unsigned long long)(base64.bytes * repeats), (unsigned long long)sum);
                    start = clock_ns();
                    sum = bench_quoted(quoted, left, repeats, vector);
                    elapsed = clock_ns() - start;
                    printf("TIMING quoted pair=%u vector=%u ns=%llu units=%llu sum=%llu\n", pair, vector, (unsigned long long)elapsed, (unsigned long long)(quoted.bytes * repeats), (unsigned long long)sum);
                    start = clock_ns();
                    sum = bench_intern(runs, shapes.count, indices_left, spelling_base, repeats, vector);
                    elapsed = clock_ns() - start;
                    printf("TIMING identifier pair=%u vector=%u ns=%llu units=%llu sum=%llu\n", pair, vector, (unsigned long long)elapsed, (unsigned long long)(shapes.bytes * repeats), (unsigned long long)sum);
                    fflush(stdout);
                }
            }
        }
        result = failures ? 1 : 0;
    }
    return result;
}
