// SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception) AND MIT
// Original material Copyright (c) 2026 David Gonzalez Martin.
// Counter/timing/sort helpers follow the encoder/lexer harness and retain MIT and the upstream notice:
// Copyright (c) 2023 Niles Salter. See THIRD_PARTY_NOTICES.md and LICENSES/Accelerated-Zig-Parser.txt.
//
// Original decoder benchmark. Generation and initialization are excluded. Twenty-one alternating passes measure
// raw-byte decoding, prefix classification, and decoding plus Intel formatting. Each retained batch lasts >= 2 ms.
// The synthetic compiler and vector streams are separate; --file accepts real raw .text bytes with coverage errors.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_64_disassembler.c"

#define BUSTER_X86_DECODE_BENCH_COUNT 16384
#define BUSTER_X86_DECODE_BENCH_PASSES 21

static volatile uint64_t x86_decode_benchmark_sink;

// These counter/timing/sort helpers retain the existing benchmark harness's MIT terms.
static uint64_t x86_decode_benchmark_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

static int x86_decode_benchmark_counter(uint64_t event)
{
    struct perf_event_attr attributes =
    {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(attributes),
        .config = event,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, 0);
}

static uint64_t x86_decode_benchmark_read(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static int x86_decode_benchmark_compare(const void* left, const void* right)
{
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

// The disassembly workloads, phases and state-conditioned measurement loop are original project code.
static uint64_t x86_decode_benchmark_pass(const uint8_t* source, uint32_t size, X86Decoded* decoded, uint32_t capacity, uint32_t phase, uint32_t variant, uint32_t repeats)
{
    uint64_t checksum = 0;
    for (uint32_t repeat = 0; repeat < repeats; repeat += 1)
    {
        if (phase == 1)
        {
            for (uint32_t at = 0; at < size; at += 64)
            {
                const uint32_t length = size - at < 64 ? size - at : 64;
                checksum += variant == 1 || variant == 2 ? x86_decode_classify(source + at, length) : x86_decode_classify_scalar(source + at, length);
            }
        }
        else
        {
            const X86DecodeResult result = variant == 3 ? x86_disassemble_impl(source, size, decoded, capacity, false, true) : variant == 2 ? x86_disassemble(source, size, decoded, capacity) : variant == 1 ? x86_disassemble_impl(source, size, decoded, capacity, true, true) : x86_disassemble_scalar(source, size, decoded, capacity);
            // Retain every published record store even in the decode-only phase.
            __asm__ volatile("" : : "r"(decoded) : "memory");
            checksum += result.consumed + result.count + result.status;
            if (phase == 2 || phase == 3)
            {
                for (uint64_t i = 0; i < result.count; i += 1)
                {
                    char text[512];
                    const int64_t length = phase == 3 ? x86_disassembly_listing(&decoded[i], source, 0, text, sizeof(text), variant == 0) : variant == 0 ? x86_disassembly_format_scalar(&decoded[i], text, sizeof(text)) : x86_disassembly_format(&decoded[i], text, sizeof(text));
                    // The full rendered text is observable, including bytes beyond the checksum's first byte.
                    __asm__ volatile("" : : "r"(text) : "memory");
                    checksum += (uint64_t)length + (uint8_t)text[0];
                }
            }
        }
    }
    return checksum;
}

// Keep the core busy with the scalar decoder before each retained sample. Sleeping would measure an idle-state
// transition instead. The scalar decoder performs no vector prefix classification or candidate comparisons.
static void x86_decode_benchmark_after_scalar(const uint8_t* source, uint32_t size, X86Decoded* decoded, uint32_t capacity, uint32_t milliseconds)
{
    const uint64_t end = x86_decode_benchmark_time() + (uint64_t)milliseconds * 1000000;
    while (x86_decode_benchmark_time() < end)
    {
        const X86DecodeResult result = x86_disassemble_scalar(source, size, decoded, capacity);
        x86_decode_benchmark_sink = result.count + result.consumed;
    }
}

static uint32_t x86_decode_benchmark_stream(uint8_t* bytes, bool vectors)
{
    // The ordinary mix includes common arithmetic, stack addressing, load/store, branches and SSE. The vector mix
    // alternates AVX-512 loads, stores and arithmetic with the ordinary instructions around a vectorized loop.
    static const X86Instruction patterns[] =
    {
        {.form = X86_FORM_MOV_R64_RM64, .reg = 8, .rm = 9},
        {.form = X86_FORM_MOV_RM64_R64, .reg = 10, .rm = 4, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64},
        {.form = X86_FORM_MOV_R64_RM64, .reg = 2, .rm = 4, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -32},
        {.form = X86_FORM_LEA_R64_M, .reg = 3, .rm = 8, .index = 9, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 127},
        {.form = X86_FORM_ADD_RM64_IMM8, .rm = 0, .immediate = 1},
        {.form = X86_FORM_CMP_RM64_R64, .rm = 0, .reg = 1},
        {.form = X86_FORM_JNE_REL8, .displacement = -24},
        {.form = X86_FORM_CALL_REL32, .displacement = 4096},
        {.form = X86_FORM_XOR_RM32_R32, .reg = 0, .rm = 0},
        {.form = X86_FORM_ADDSD_X_XM64, .reg = 2, .rm = 3},
        {.form = X86_FORM_PUSH_R64, .rm = 12},
        {.form = X86_FORM_RET},
    };
    static const X86Instruction vector_patterns[] =
    {
        {.form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = 17, .rm = 8, .index = 9, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 128},
        {.form = X86_FORM_VMOVDQU64_ZM512_Z, .reg = 18, .rm = 8, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .decorations = 2},
        {.form = X86_FORM_VMOVDQU8_Z_ZM512, .reg = 23, .rm = 24, .decorations = 0x81},
        {.form = X86_FORM_VMOVDQU8_ZM512_Z, .reg = 22, .rm = 23},
        {.form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 17, .vvvv = 18, .rm = 24, .decorations = 0x83},
        {.form = X86_FORM_VADDPS_Z_Z_M32BCST, .reg = 19, .vvvv = 20, .rm = 4, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 12, .decorations = 2},
        {.form = X86_FORM_VFMADD231PS_Z_Z_ZM512, .reg = 21, .vvvv = 22, .rm = 8, .index = 9, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 256},
        {.form = X86_FORM_VFMADD231PS_Z_Z_Z_ER, .reg = 21, .vvvv = 22, .rm = 23, .decorations = 0x83},
        {.form = X86_FORM_VPCMPUB_K_Z_ZM512_IMM8, .reg = 3, .vvvv = 17, .rm = 25, .immediate = 4, .decorations = 2},
        {.form = X86_FORM_VPERMT2D_Z_Z_ZM512, .reg = 20, .vvvv = 21, .rm = 22, .decorations = 1},
        {.form = X86_FORM_VPCOMPRESSB_ZM512_Z, .reg = 18, .rm = 8, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -16, .decorations = 3},
        {.form = X86_FORM_VPADDD_Y_Y_YM256, .reg = 9, .vvvv = 10, .rm = 11},
        {.form = X86_FORM_VFMADD231PD_Y_Y_YM256, .reg = 9, .vvvv = 10, .rm = 11},
        {.form = X86_FORM_VMOVDQU_Y_YM256, .reg = 9, .rm = 12, .index = 13, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 128},
        {.form = X86_FORM_ANDN_R64_R64_RM64, .reg = 8, .vvvv = 9, .rm = 10},
        {.form = X86_FORM_SHLX_R64_RM64_R64, .reg = 8, .vvvv = 9, .rm = 10},
        {.form = X86_FORM_KORTESTQ_K_K, .reg = 1, .rm = 2},
        {.form = X86_FORM_VZEROUPPER},
    };
    uint32_t length = 0;
    uint64_t state = 0x83b57d41;
    for (uint32_t i = 0; i < BUSTER_X86_DECODE_BENCH_COUNT; i += 1)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const X86Instruction* instruction = vectors && (i & 3) != 0 ? &vector_patterns[(state >> 32) % BUSTER_ARRAY_LENGTH(vector_patterns)] : &patterns[(state >> 32) % BUSTER_ARRAY_LENGTH(patterns)];
        length += encode_one(instruction, bytes + length, X86_ENCODER_MODE_64);
    }
    return length;
}

int run_x86_disassembler_benchmark(int argc, char** argv)
{
    bool vectors = false;
    bool valid = true;
    const char* file = 0;
    int only = -1;
    int only_phase = -1;
    uint32_t iterations = 0;
    uint32_t after_scalar = 0;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--vector") == 0)
        {
            vectors = true;
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            i += 1;
            file = argv[i];
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "avx512") == 0 ? 1 : strcmp(argv[i], "batched") == 0 ? 2 : strcmp(argv[i], "compact") == 0 ? 3 : -1;
            valid = valid && only >= 0;
        }
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
        {
            i += 1;
            only_phase = strcmp(argv[i], "decode") == 0 ? 0 : strcmp(argv[i], "classify") == 0 ? 1 : strcmp(argv[i], "format") == 0 ? 2 : strcmp(argv[i], "listing") == 0 ? 3 : -1;
            valid = valid && only_phase >= 0;
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            const unsigned long value = strtoul(argv[i], &end, 10);
            valid = valid && argv[i][0] >= '0' && argv[i][0] <= '9' && *end == 0 && value > 0 && value <= (1u << 24);
            iterations = value > 0 && value <= (1u << 24) ? (uint32_t)value : 0;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            const unsigned long value = strtoul(argv[i], &end, 10);
            valid = valid && argv[i][0] >= '0' && argv[i][0] <= '9' && *end == 0 && value <= 1000;
            after_scalar = value <= 1000 ? (uint32_t)value : 0;
        }
        else
        {
            valid = false;
        }
    }
    uint32_t capacity = BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_DECODE_BENCH_COUNT);
    struct stat info;
    if (file != 0)
    {
        valid = valid && stat(file, &info) == 0 && info.st_size > 0 && info.st_size <= 64000000;
        capacity = valid ? (uint32_t)info.st_size : capacity;
    }
    uint8_t* bytes = malloc(capacity);
    X86Decoded* decoded = malloc((uint64_t)capacity * sizeof(*decoded));
    X86Decoded* reference = malloc((uint64_t)capacity * sizeof(*reference));
    valid = valid && bytes != 0 && decoded != 0 && reference != 0;
    const uint64_t initialization_start = x86_decode_benchmark_time();
    valid = valid && (x86_encoder_initialized || x86_encoder_initialize());
    const uint64_t normal_initialized = x86_decode_benchmark_time();
    valid = valid && (x86_encoder_initialized_address32 || x86_encoder_initialize_address32());
    const uint64_t addresses_initialized = x86_decode_benchmark_time();
    valid = valid && x86_disassembler_initialize();
    const uint64_t initialization_ns = x86_decode_benchmark_time() - initialization_start;
    uint32_t size = 0;
    if (valid && file != 0)
    {
        FILE* input = fopen(file, "rb");
        valid = input != 0 && fread(bytes, 1, capacity, input) == capacity;
        if (input != 0)
        {
            fclose(input);
        }
        size = capacity;
    }
    else if (valid)
    {
        size = x86_decode_benchmark_stream(bytes, vectors);
    }
    X86DecodeResult result = {0};
    if (valid)
    {
        result = x86_disassemble(bytes, size, decoded, capacity);
        const X86DecodeResult scalar = x86_disassemble_scalar(bytes, size, reference, capacity);
        valid = result.status == X86_DECODE_OK && scalar.status == X86_DECODE_OK && result.count == scalar.count && memcmp(decoded, reference, result.count * sizeof(*decoded)) == 0;
        const X86DecodeResult basic = x86_disassemble_impl(bytes, size, reference, capacity, true, true);
        valid = valid && basic.status == X86_DECODE_OK && result.count == basic.count && memcmp(decoded, reference, result.count * sizeof(*decoded)) == 0;
        const X86DecodeResult compact = x86_disassemble_impl(bytes, size, reference, capacity, false, true);
        valid = valid && compact.status == X86_DECODE_OK && result.count == compact.count && memcmp(decoded, reference, result.count * sizeof(*decoded)) == 0;
        if (!valid)
        {
            fprintf(stderr, "benchmark decoder coverage/error at %llu/%u bytes, status=%u, decoded=%llu\n", (unsigned long long)result.consumed, size, result.status, (unsigned long long)result.count);
        }
    }
    if (valid)
    {
        const int cycles = x86_decode_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES);
        const int instructions = x86_decode_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS);
        printf("disassembler stream=%s bytes=%u instructions=%llu passes=21 after-scalar=%ums fixed-iterations=%u\n", file != 0 ? file : vectors ? "synthetic-vector" : "synthetic-compiler", size, (unsigned long long)result.count, after_scalar, iterations);
        const uint64_t table_bytes = sizeof(x86_decode_buckets) + x86_decode_storage_bytes + sizeof(x86_decode_indices) + (uint64_t)x86_decode_index_count * sizeof(*x86_decode_index_rows) +
                                     sizeof(x86_decode_length_table) + sizeof(x86_decode_checks) + sizeof(x86_decode_simple) + sizeof(x86_disassembly_plans) + sizeof(x86_disassembly_registers);
        printf("initialization=%.3fms (encoder64=%.3fms address32=%.3fms decoder+format=%.3fms) decoder-tables=%llu bytes entries=%u index-cells=%u\n", initialization_ns / 1000000.0,
               (normal_initialized - initialization_start) / 1000000.0, (addresses_initialized - normal_initialized) / 1000000.0, (initialization_ns - (addresses_initialized - initialization_start)) / 1000000.0,
               (unsigned long long)table_bytes, x86_decode_entry_count, x86_decode_index_count);
        for (uint32_t phase = only_phase < 0 ? 0 : (uint32_t)only_phase; phase < (only_phase < 0 ? 4 : (uint32_t)only_phase + 1); phase += 1)
        {
            double samples[4][3][BUSTER_X86_DECODE_BENCH_PASSES];
            uint32_t repeats = iterations != 0 ? iterations : 1;
            uint64_t duration = 0;
            do
            {
                duration = UINT64_MAX;
                for (uint32_t variant = 0; variant < 4; variant += 1)
                {
                    if (only < 0 || only == (int)variant)
                    {
                        const uint64_t start = x86_decode_benchmark_time();
                        x86_decode_benchmark_sink = x86_decode_benchmark_pass(bytes, size, decoded, capacity, phase, variant, repeats);
                        const uint64_t elapsed = x86_decode_benchmark_time() - start;
                        duration = elapsed < duration ? elapsed : duration;
                    }
                }
                repeats = duration < 2000000 && iterations == 0 ? repeats * 2 : repeats;
            } while (duration < 2000000 && repeats < (1u << 24) && iterations == 0);
            for (uint32_t pass = 0; pass < BUSTER_X86_DECODE_BENCH_PASSES; pass += 1)
            {
                for (uint32_t order = 0; order < 4; order += 1)
                {
                    const uint32_t variant = (pass + order) % 4;
                    if (only < 0 || only == (int)variant)
                    {
                        if (after_scalar != 0)
                        {
                            x86_decode_benchmark_after_scalar(bytes, size, decoded, capacity, after_scalar);
                        }
                        const uint64_t c0 = x86_decode_benchmark_read(cycles);
                        const uint64_t i0 = x86_decode_benchmark_read(instructions);
                        const uint64_t t0 = x86_decode_benchmark_time();
                        x86_decode_benchmark_sink = x86_decode_benchmark_pass(bytes, size, decoded, capacity, phase, variant, repeats);
                        const uint64_t elapsed = x86_decode_benchmark_time() - t0;
                        const uint64_t c1 = x86_decode_benchmark_read(cycles);
                        const uint64_t i1 = x86_decode_benchmark_read(instructions);
                        const double items = (double)repeats * (phase == 1 ? size : result.count);
                        samples[variant][0][pass] = elapsed / items;
                        samples[variant][1][pass] = (c1 - c0) / items;
                        samples[variant][2][pass] = (i1 - i0) / items;
                    }
                }
            }
            for (uint32_t variant = 0; variant < 4; variant += 1)
            {
                if (only < 0 || only == (int)variant)
                {
                    for (uint32_t metric = 0; metric < 3; metric += 1)
                    {
                        qsort(samples[variant][metric], BUSTER_X86_DECODE_BENCH_PASSES, sizeof(double), x86_decode_benchmark_compare);
                    }
                    const double ns = samples[variant][0][10];
                    const double cy = samples[variant][1][10];
                    const double ins = samples[variant][2][10];
                    printf("%-13s %-7s ns/%s=%.3f [%.3f,%.3f] cycles=%.3f instructions=%.3f IPC=%.3f\n", phase == 0 ? "decode" : phase == 1 ? "classify" : phase == 2 ? "decode+format" : "decode+listing", variant == 3 ? "compact" : variant == 2 ? "batched" : variant == 1 ? "avx512" : "scalar", phase == 1 ? "byte" : "insn", ns, samples[variant][0][0], samples[variant][0][20], cy, ins, cy != 0 ? ins / cy : 0);
                }
            }
        }
        if (cycles >= 0)
        {
            close(cycles);
        }
        if (instructions >= 0)
        {
            close(instructions);
        }
    }
    free(reference);
    free(decoded);
    free(bytes);
    return valid ? 0 : 1;
}

#else
int run_x86_disassembler_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
