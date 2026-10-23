#include <assert.h>
#include "buster/compiler/x86_64_encoder_benchmark.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/branch_sizing.c"

__attribute__((noinline, aligned(64))) static uint64_t tiny_measure(const X86Instruction* records, uint64_t count, uint32_t* offsets, uint8_t* encoded)
{
#ifdef BUSTER305_BASELINE
    return branch_sizing_lengths(records, count, offsets, encoded, true);
#else
    (void)encoded;
    return (uint64_t)x86_measure(records, count, offsets);
#endif
}

static uint64_t tiny_run(const X86Instruction* records, uint64_t count, uint64_t calls, uint32_t* offsets, uint8_t* encoded)
{
    uint64_t sum = 0;
    for (uint64_t i = 0; i < calls; i += 1)
    {
        sum += tiny_measure(records + i * count, count, offsets + i * count, encoded);
    }
    __asm__ volatile("" : : "r"(offsets), "r"(encoded) : "memory");
    return sum;
}

int main(int argc, char** argv)
{
    const uint64_t count = argc > 1 ? strtoull(argv[1], 0, 10) : 40;
    const uint64_t calls = 4096;
    const uint64_t records_count = count ? count * calls : 1;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(1024);
    X86Instruction* records = malloc(records_count * sizeof(*records));
    uint32_t* offsets = malloc(records_count * sizeof(*offsets));
    uint32_t* oracle = malloc(records_count * sizeof(*oracle));
    uint8_t* encoded = malloc(capacity);
    if (count > 40 || !records || !offsets || !oracle || !encoded || !x86_encoder_initialize() ||
        !make_stream(records, records_count, x86_benchmark_patterns, BUSTER_ARRAY_LENGTH(x86_benchmark_patterns), false, 305)) return 1;
    uint64_t expected = 0;
    for (uint64_t i = 0; i < calls; i += 1)
    {
        const int64_t length = x86_encode_scalar(records + i * count, count, encoded, capacity, oracle + i * count, 0);
        if (length < 0) return 2;
        expected += (uint64_t)length;
    }
    const uint64_t first = tiny_run(records, count, calls, offsets, encoded);
    if (first != expected || memcmp(offsets, oracle, count * calls * sizeof(*offsets))) return 3;
    for (unsigned i = 0; i < 3; i += 1)
    {
        if (tiny_run(records, count, calls, offsets, encoded) != expected) return 4;
    }
    double ns[15];
    for (unsigned sample = 0; sample < 15; sample += 1)
    {
        const uint64_t start = x86_benchmark_nanoseconds();
        uint64_t sum = 0;
        for (unsigned repeat = 0; repeat < 64; repeat += 1)
        {
            sum += tiny_run(records, count, calls, offsets, encoded);
        }
        const uint64_t end = x86_benchmark_nanoseconds();
        if (sum != expected * 64) return 5;
        ns[sample] = (double)(end - start) / (calls * 64);
    }
    for (unsigned i = 1; i < 15; i += 1)
    {
        const double value = ns[i];
        unsigned j = i;
        while (j && ns[j - 1] > value)
        {
            ns[j] = ns[j - 1];
            j -= 1;
        }
        ns[j] = value;
    }
#ifdef BUSTER305_BASELINE
    const char* variant = "before";
#else
    const char* variant = "after";
#endif
    printf("%s count=%llu calls=%llu ns/call=%.6f range=%.6f..%.6f sum=%llu\n", variant,
        (unsigned long long)count, (unsigned long long)calls, ns[7], ns[0], ns[14], (unsigned long long)first);
    free(encoded);
    free(oracle);
    free(offsets);
    free(records);
    return 0;
}
