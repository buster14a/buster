// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The existing preprocessor fixtures as independent lane jobs. Capture both -E printers' modes and the real
// diagnostic renderer. Prime a fresh intern table with unused names, then every nonfixed name from the baseline in
// a different order. Fixed keyword/directive IDs retain their contract; source names must actually change IDs.
// This is a property harness for the callable preprocessor, not the multi-file production driver of #152.

typedef struct PPOrderName PPOrderName;
struct PPOrderName
{
    uint8_t* bytes;
    uint32_t length;
};

typedef struct PPOrderOutput PPOrderOutput;
struct PPOrderOutput
{
    char* text[2];
    size_t length[2];
    char* diagnostics;
    size_t diagnostic_length;
    PPOrderName* names;
    uint32_t name_count;
    uint32_t errors;
    uint32_t warnings;
    bool fatal;
    bool ran;
    bool ready;
};

typedef struct PPOrderInput PPOrderInput;
struct PPOrderInput
{
    char directory[4096];
    char path[4096];
};

typedef struct PPOrderTest PPOrderTest;
struct PPOrderTest
{
    const PPOrderInput* inputs;
    const PPOrderOutput* reference;
    PPOrderOutput* outputs;
    uint64_t seed;
    bool reverse;
    bool scalar;
};

static void pp_order_release(PPOrderOutput* output)
{
    free(output->text[0]);
    free(output->text[1]);
    free(output->diagnostics);
    for (uint32_t i = 0; i < output->name_count; i += 1)
    {
        free(output->names[i].bytes);
    }
    free(output->names);
    *output = (PPOrderOutput){ 0 };
}

static bool pp_order_names(PPOrderOutput* output, const InternTable* table)
{
    const uint32_t count = table->count - C_NAME_COUNT;
    output->names = calloc((uint64_t)count + 1, sizeof(PPOrderName));
    bool result = output->names != 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        PPOrderName* name = &output->names[i];
        name->length = intern_length(table, C_NAME_COUNT + i);
        name->bytes = malloc((uint64_t)name->length + 1);
        result = name->bytes != 0;
        if (result)
        {
            memcpy(name->bytes, intern_bytes(table, C_NAME_COUNT + i), name->length + 1);
            output->name_count += 1;
        }
    }
    return result;
}

static bool pp_order_prime(InternTable* table, const PPOrderOutput* reference, uint64_t seed)
{
    bool result = true;
    const uint32_t padding = 17 + seed % 31;
    for (uint32_t i = 0; result && i < padding; i += 1)
    {
        char text[80];
        const int length = snprintf(text, sizeof(text), "__buster_determinism_unused_%u", i);
        result = intern_find_or_add(table, (const uint8_t*)text, (uint32_t)length) != 0;
    }
    bool changed = false;
    for (uint32_t i = 0; result && i < reference->name_count; i += 1)
    {
        const uint32_t index = (uint32_t)((seed % reference->name_count + reference->name_count - 1ull - i) % reference->name_count);
        const PPOrderName name = reference->names[index];
        const IdentId id = intern_find_or_add(table, name.bytes, name.length);
        result = id != 0;
        changed |= id != C_NAME_COUNT + index;
    }
    return result && (reference->name_count == 0 || changed);
}

static void pp_order_lane(void* argument)
{
    PPOrderTest* test = argument;
    const uint32_t count = BUSTER_ARRAY_LENGTH(pp_test_cases);
    for (uint32_t slot = lane_index(); slot < count; slot += lane_count())
    {
        const uint32_t index = test->reverse ? count - 1 - slot : slot;
        const PPOrderInput* input = &test->inputs[index];
        PPOrderOutput* out = &test->outputs[index];
        char text[16384];
        char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
        const int argc = pp_test_arguments(&pp_test_cases[index], input->directory, input->path, text, sizeof(text), arguments);
        Arena options_arena = arena_reserve(1 << 20);
        PPOptions options;
        const char* path;
        const char* output;
        bool markers;
        // Preprocessor has 64-byte alignment; ordinary malloc/calloc does not promise that alignment.
        Preprocessor* pp = BUSTER_ARENA_PUSH_ARRAY(&options_arena, Preprocessor, 1);
        if (pp != 0)
        {
            *pp = (Preprocessor){ 0 };
        }
        InternTable table = { 0 };
        FILE* diagnostics = open_memstream(&out->diagnostics, &out->diagnostic_length);
        out->ready = pp != 0 && diagnostics != 0 && options_arena.base != 0 &&
                     pp_parse_options(argc + 1, arguments, &options, &options_arena, &path, &output, &markers) && intern_initialize(&table);
        if (out->ready && test->reference != 0)
        {
            out->ready = pp_order_prime(&table, &test->reference[index], test->seed);
        }
        if (out->ready)
        {
            options.scalar = test->scalar;
            options.expansion_starts = true;
            options.diagnostics = diagnostics;
            out->ready = pp_initialize(pp, &options, &table);
        }
        if (out->ready)
        {
            // Vary completion as well as job-to-lane assignment, without changing any source-order key.
            if (test->reverse && index % 11 == 0)
            {
                const struct timespec delay = { .tv_nsec = 100000 };
                nanosleep(&delay, 0);
            }
            out->ran = pp_run(pp, path);
            out->errors = pp->errors;
            out->warnings = pp->warnings;
            out->fatal = pp->fatal;
            for (uint32_t k = 0; k < 2; k += 1)
            {
                out->text[k] = pp_test_print(pp, !test->scalar, k != 0, &out->length[k]);
                out->ready = out->ready && out->text[k] != 0;
            }
            if (test->reference == 0)
            {
                out->ready = pp_order_names(out, &table) && out->ready;
            }
        }
        if (diagnostics != 0)
        {
            out->ready = fclose(diagnostics) == 0 && out->ready;
        }
        if (pp != 0)
        {
            pp_release(pp);
        }
        intern_release(&table);
        arena_release(&options_arena);
    }
}

static bool pp_order_same(const PPOrderOutput* a, const PPOrderOutput* b)
{
    bool result = a->ready && b->ready && a->ran == b->ran && a->errors == b->errors && a->warnings == b->warnings && a->fatal == b->fatal &&
                  a->diagnostic_length == b->diagnostic_length && memcmp(a->diagnostics, b->diagnostics, a->diagnostic_length) == 0;
    for (uint32_t k = 0; result && k < 2; k += 1)
    {
        result = a->length[k] == b->length[k] && memcmp(a->text[k], b->text[k], a->length[k]) == 0;
    }
    return result;
}

static bool pp_test_id_order(const char* directory, uint64_t seed)
{
    static const uint32_t counts[] =
    {
        1,
        2,
        3,
        8,
    };
    const uint32_t count = BUSTER_ARRAY_LENGTH(pp_test_cases);
    PPOrderInput* inputs = calloc(count, sizeof(PPOrderInput));
    PPOrderOutput* reference = calloc(count, sizeof(PPOrderOutput));
    PPOrderOutput* outputs = calloc(count, sizeof(PPOrderOutput));
    bool result = inputs != 0 && reference != 0 && outputs != 0;
    uint32_t made = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const int length = snprintf(inputs[i].directory, sizeof(inputs[i].directory), "%s/order-%u", directory, i);
        result = length > 0 && (uint64_t)length < sizeof(inputs[i].directory) && mkdir(inputs[i].directory, 0700) == 0;
        if (result)
        {
            made += 1;
            result = pp_test_write_case(&pp_test_cases[i], inputs[i].directory, inputs[i].path, sizeof(inputs[i].path));
        }
    }
    PPOrderTest test =
    {
        .inputs = inputs,
        .outputs = reference,
    };
    result = result && lane_run(1, pp_order_lane, &test, 0);
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        result = reference[i].ready && reference[i].ran != pp_test_cases[i].fails;
    }
    test.reference = reference;
    test.outputs = outputs;
    test.seed = seed;
    for (uint32_t mode = 0; result && mode < 4; mode += 1)
    {
        test.scalar = mode >= 2;
        test.reverse = (mode & 1) != 0;
        for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            result = lane_run(counts[c], pp_order_lane, &test, 0);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool same = pp_order_same(&reference[i], &outputs[i]);
                if (!same)
                {
                    printf("ID order: %s differs (%u lanes, mode %u, seed %llu)\n", pp_test_cases[i].name, counts[c], mode, (unsigned long long)seed);
                }
                result = same && result;
                pp_order_release(&outputs[i]);
            }
        }
    }
    for (uint32_t i = 0; i < made; i += 1)
    {
        pp_test_remove_case(&pp_test_cases[i], inputs[i].directory);
        unlink(inputs[i].path);
        rmdir(inputs[i].directory);
    }
    for (uint32_t i = 0; reference != 0 && i < count; i += 1)
    {
        pp_order_release(&reference[i]);
    }
    free(inputs);
    free(reference);
    free(outputs);
    printf("preprocessor determinism: %u fixtures, 1/2/3/8 lanes, both kernels/orders, ID seed %llu: %s\n", count, (unsigned long long)seed, result ? "pass" : "FAIL");
    return result;
}
