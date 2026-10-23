#include <assert.h>
#include "buster/compiler/x86_64_encoder_benchmark.c"

__attribute__((noinline, aligned(64))) static int64_t width_measure(const X86Instruction* records, uint64_t count, uint32_t* offsets)
{
    return x86_measure(records, count, offsets);
}

static uint64_t width_run(const X86Instruction* records, uint64_t count, uint64_t section, uint32_t* offsets)
{
    uint64_t sum = 0;
    for (uint64_t i = 0; i < count; i += section)
    {
        const uint64_t size = count - i < section ? count - i : section;
        sum += (uint64_t)width_measure(records + i, size, offsets + i);
    }
    __asm__ volatile("" : : "r"(offsets) : "memory");
    return sum;
}

int main(int argc, char** argv)
{
    const uint64_t count = 262144;
    const uint64_t section = argc > 1 ? strtoull(argv[1], 0, 10) : count;
    const bool vector = argc > 2 && strcmp(argv[2], "vector") == 0;
    X86Instruction* records = malloc(count * sizeof(*records));
    uint32_t* offsets = malloc(count * sizeof(*offsets));
    uint32_t* oracle = malloc(count * sizeof(*oracle));
    const X86BenchmarkPattern* patterns = vector ? x86_benchmark_vector_patterns : x86_benchmark_patterns;
    const uint64_t pattern_count = vector ? BUSTER_ARRAY_LENGTH(x86_benchmark_vector_patterns) : BUSTER_ARRAY_LENGTH(x86_benchmark_patterns);
    if (!records || !offsets || !oracle || section == 0 || !x86_encoder_initialize() || !make_stream(records, count, patterns, pattern_count, false, 305)) return 1;
    uint64_t expected = 0;
    for (uint64_t i = 0; i < count; i += section)
    {
        const uint64_t size = count - i < section ? count - i : section;
        expected += (uint64_t)x86_measure_scalar(records + i, size, oracle + i);
    }
    const uint64_t first = width_run(records, count, section, offsets);
    if (first != expected || memcmp(offsets, oracle, count * sizeof(*offsets))) return 2;
    for (unsigned i = 0; i < 3; i += 1) if (width_run(records, count, section, offsets) != expected) return 3;
    double ns[15];
    for (unsigned sample = 0; sample < 15; sample += 1)
    {
        const uint64_t start = x86_benchmark_nanoseconds();
        uint64_t sum = 0;
        for (unsigned repeat = 0; repeat < 8; repeat += 1) sum += width_run(records, count, section, offsets);
        const uint64_t end = x86_benchmark_nanoseconds();
        if (sum != expected * 8) return 4;
        ns[sample] = (double)(end - start) / (count * 8);
    }
    for (unsigned i = 1; i < 15; i += 1)
    {
        const double value = ns[i];
        unsigned j = i;
        while (j && ns[j - 1] > value) { ns[j] = ns[j - 1]; j -= 1; }
        ns[j] = value;
    }
    printf("%s section=%llu count=%llu ns/record=%.6f range=%.6f..%.6f sum=%llu\n", vector ? "vector" : "compiler", (unsigned long long)section, (unsigned long long)count, ns[7], ns[0], ns[14], (unsigned long long)first);
    free(oracle);
    free(offsets);
    free(records);
    return 0;
}
