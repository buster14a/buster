// Issue #2454 owner-requested diagnostic: Wasm string-record lookup cost.
// Rebuilds the context-wide Wasm64StringRecord array in collection order
// (module, function, instruction) and resolves every record once, emitting
// each function's constants in reverse instruction-ID order. It times the
// pre-#2483 linear scan (from wasm.c at 2f00f692) against the current
// lower-bound search (wasm64_string_record_find on main). Both must return
// the same record for every lookup. This isolates the lookup only; it is not
// an end-to-end emit measurement.
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOOKUP_MODULES 4u
#define LOOKUP_STRINGS_PER_FUNCTION 4u

typedef struct LookupFunction LookupFunction;
struct LookupFunction
{
    uint32_t instruction_count;
    uint32_t pad;
};

// Field order and widths match Wasm64StringRecord on main.
typedef struct LookupRecord LookupRecord;
struct LookupRecord
{
    LookupFunction* function;
    uint32_t instruction;
    uint8_t* literal_pointer;
    uint64_t literal_length;
    uint64_t offset;
    uint32_t module_index;
    uint32_t function_index;
};

static uint64_t lookup_now_nanoseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static LookupRecord* lookup_linear(LookupRecord* records, uint32_t count, LookupFunction* function, uint32_t instruction, uint64_t* probes)
{
    LookupRecord* result = 0;
    for (uint32_t index = 0; index < count && !result; index += 1)
    {
        LookupRecord* record = records + index;
        *probes += 1;
        if (record->function == function && record->instruction == instruction)
        {
            result = record;
        }
    }
    return result;
}

static LookupRecord* lookup_binary(LookupRecord* records, uint32_t count, LookupFunction* function, uint32_t module_index, uint32_t function_index,
                                   uint32_t instruction, uint64_t* probes)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        LookupRecord* record = records + middle;
        *probes += 1;
        int before = record->module_index != module_index       ? record->module_index < module_index
                     : record->function_index != function_index ? record->function_index < function_index
                                                                : record->instruction < instruction;
        if (before)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    LookupRecord* result = 0;
    if (low < count)
    {
        LookupRecord* record = records + low;
        *probes += 1;
        if (record->function == function && record->instruction == instruction)
        {
            result = record;
        }
    }
    return result;
}

int main(void)
{
    static uint8_t literal[] = "shared";
    uint32_t sizes[] = {1024u, 4096u, 16384u, 32768u};
    int passed = 1;
    printf("model modules=%u strings_per_function=%u emission=reverse-instruction-order\n", LOOKUP_MODULES, LOOKUP_STRINGS_PER_FUNCTION);
    for (uint32_t size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]) && passed; size_index += 1)
    {
        uint32_t strings = sizes[size_index];
        uint32_t functions_per_module = strings / (LOOKUP_MODULES * LOOKUP_STRINGS_PER_FUNCTION);
        uint32_t function_total = functions_per_module * LOOKUP_MODULES;
        LookupFunction* functions = calloc(function_total, sizeof(*functions));
        LookupRecord* records = calloc(strings, sizeof(*records));
        passed = functions && records;
        uint32_t count = 0;
        uint64_t cursor = 0;
        for (uint32_t module_index = 0; module_index < LOOKUP_MODULES && passed; module_index += 1)
        {
            for (uint32_t function_index = 0; function_index < functions_per_module; function_index += 1)
            {
                LookupFunction* function = functions + module_index * functions_per_module + function_index;
                function->instruction_count = LOOKUP_STRINGS_PER_FUNCTION + 1;
                for (uint32_t instruction = 0; instruction < LOOKUP_STRINGS_PER_FUNCTION; instruction += 1)
                {
                    records[count] = (LookupRecord){.function = function, .instruction = instruction, .literal_pointer = literal,
                                                    .literal_length = sizeof(literal) - 1, .offset = cursor,
                                                    .module_index = module_index, .function_index = function_index};
                    cursor += sizeof(literal);
                    count += 1;
                }
            }
        }
        uint64_t linear_probes = 0;
        uint64_t linear_checksum = 0;
        uint64_t linear_start = lookup_now_nanoseconds();
        for (uint32_t function_slot = 0; function_slot < function_total && passed; function_slot += 1)
        {
            for (uint32_t step = 0; step < LOOKUP_STRINGS_PER_FUNCTION; step += 1)
            {
                uint32_t instruction = LOOKUP_STRINGS_PER_FUNCTION - 1 - step;
                LookupRecord* found = lookup_linear(records, count, functions + function_slot, instruction, &linear_probes);
                passed &= found != 0;
                linear_checksum += found ? found->offset : 0;
            }
        }
        uint64_t linear_nanoseconds = lookup_now_nanoseconds() - linear_start;
        // Repeat the fast path so its interval is well above clock resolution.
        uint32_t repeats = 256;
        uint64_t binary_probes = 0;
        uint64_t binary_checksum = 0;
        uint32_t mismatches = 0;
        uint64_t binary_start = lookup_now_nanoseconds();
        for (uint32_t repeat = 0; repeat < repeats && passed; repeat += 1)
        {
            for (uint32_t function_slot = 0; function_slot < function_total; function_slot += 1)
            {
                uint32_t module_index = function_slot / functions_per_module;
                uint32_t function_index = function_slot % functions_per_module;
                for (uint32_t step = 0; step < LOOKUP_STRINGS_PER_FUNCTION; step += 1)
                {
                    uint32_t instruction = LOOKUP_STRINGS_PER_FUNCTION - 1 - step;
                    LookupRecord* found = lookup_binary(records, count, functions + function_slot, module_index, function_index, instruction,
                                                        &binary_probes);
                    LookupRecord* expected = records + function_slot * LOOKUP_STRINGS_PER_FUNCTION + instruction;
                    mismatches += found != expected;
                    binary_checksum += found ? found->offset : 0;
                }
            }
        }
        uint64_t binary_nanoseconds = lookup_now_nanoseconds() - binary_start;
        passed &= mismatches == 0 && binary_checksum == linear_checksum * repeats;
        uint64_t binary_probes_per_pass = binary_probes / repeats;
        double binary_per_pass = (double)binary_nanoseconds / repeats;
        printf("strings=%u linear_probes=%llu linear_ns=%llu linear_ns_per_lookup=%.2f binary_probes=%llu binary_ns=%.0f "
               "binary_ns_per_lookup=%.2f quadratic_bound=%llu speedup=%.1f\n",
               count, (unsigned long long)linear_probes, (unsigned long long)linear_nanoseconds, (double)linear_nanoseconds / count,
               (unsigned long long)binary_probes_per_pass, binary_per_pass, binary_per_pass / count,
               (unsigned long long)count * (count + 1) / 2, binary_per_pass > 0 ? (double)linear_nanoseconds / binary_per_pass : 0.0);
        free(records);
        free(functions);
    }
    printf("self-check %s\n", passed ? "ok" : "FAILED");
    return passed ? 0 : 1;
}
