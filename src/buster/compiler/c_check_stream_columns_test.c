// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #628's layouts. A typed stream's values in columns and payload rows (check_stream_set_value and its readers), against a
// hand-built stream's whole values; and the reader's output columns on an output arena whose pages are the kernel's zero
// (check_input_compile, which then writes no zeros), against the same columns on one whose pages hold another pattern.
// Include after the checker test helpers (check_test_stream_equal).

// splitmix64's next value.
static uint64_t check_test_stream_next(uint64_t* state)
{
    *state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

// A random value, whose rest (past its type, category, flags, constant kind and object) is zero in about half of them.
static CheckValue check_test_stream_random_value(uint64_t* state)
{
    CheckValue value = { 0 };
    const uint64_t a = check_test_stream_next(state);
    const uint64_t b = check_test_stream_next(state);
    value.type = (TypeId)a;
    value.category = (CheckCategory)(a >> 32 & 3);
    value.flags = (uint32_t)b;
    value.constant_kind = (CheckConstantKind)(b >> 32) % 5;
    value.object = (a >> 40 & 1) != 0 ? (uint32_t)(b >> 8) : 0;
    const uint32_t rest = (uint32_t)(a >> 48) % 12;
    if (rest == 1)
    {
        value.integer = ((unsigned __int128)check_test_stream_next(state) << 64) | check_test_stream_next(state);
    }
    else if (rest == 2)
    {
        value.floating_storage[0] = check_test_stream_next(state);
        value.floating_storage[1] = check_test_stream_next(state) & 0xffff;
    }
    else if (rest == 3)
    {
        value.imaginary_storage[1] = check_test_stream_next(state) & 0xffff;
    }
    else if (rest == 4)
    {
        value.overload_head = (uint32_t)check_test_stream_next(state) | 1;
    }
    else if (rest == 5)
    {
        value.offset = (int64_t)check_test_stream_next(state) | 1;
    }
    else if (rest == 6)
    {
        value.bit_width = 1 + (uint32_t)(check_test_stream_next(state) % 64);
        value.object_bit_offset = (uint32_t)(check_test_stream_next(state) % 8);
    }
    else if (rest == 7)
    {
        value.fault = CHECK_FAULT_SIGNED_OVERFLOW;
        value.fault_node = (uint32_t)check_test_stream_next(state);
    }
    return value;
}

// Whether `value`'s bytes past the columns' fields are all zero, by its fields rather than check_stream_set_value's words.
static bool check_test_stream_rest_zero(const CheckValue* value)
{
    return value->integer == 0 && value->floating_storage[0] == 0 && value->floating_storage[1] == 0 && value->imaginary_storage[0] == 0 &&
           value->imaginary_storage[1] == 0 && value->overload_head == 0 && value->offset == 0 && value->bit_width == 0 && value->fault == 0 &&
           value->fault_node == 0 && value->object_bit_offset == 0 && (uint32_t)value->category < 256 && (uint32_t)value->constant_kind < 256;
}

static __attribute__((noinline)) void check_test_stream_columns(CheckTest* test)
{
    enum { count = 4099 };
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckValue* expected = BUSTER_ARENA_PUSH_ARRAY(&arena, CheckValue, count);
    CheckValue* dense = BUSTER_ARENA_PUSH_ARRAY(&arena, CheckValue, count);
    CheckStream columns = {
        .value_types = BUSTER_ARENA_PUSH_ARRAY(&arena, TypeId, count),
        .value_flags = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count),
        .value_objects = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count),
        .value_categories = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .value_constants = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, count),
        .payload_rows = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count),
        .payloads = BUSTER_ARENA_PUSH_ARRAY(&arena, CheckValue, count),
        .count = count,
    };
    CheckStream whole = { .dense_values = dense, .count = count };
    const bool ready = expected != NULL && dense != NULL && columns.value_types != NULL && columns.value_flags != NULL && columns.value_objects != NULL &&
                       columns.value_categories != NULL && columns.value_constants != NULL && columns.payload_rows != NULL && columns.payloads != NULL;
    check_test_expect(test, ready, "stream columns: storage");
    if (ready)
    {
        // Rows past what the run writes hold a pattern, as an arena's dirty pages would: no reader may take them.
        memset(columns.payloads, 0xa5, (uint64_t)count * sizeof(CheckValue));
        memset(columns.payload_rows, 0, (uint64_t)count * sizeof(uint32_t));
        uint64_t state = test->random ^ UINT64_C(0x628628628);
        uint32_t rows = 0;
        for (uint32_t node = 0; node < count; node += 1)
        {
            expected[node] = check_test_stream_random_value(&state);
            if (node % 97 == 3)
            {
                // A category and a constant kind past a byte, which the columns can't hold: such a value takes a row.
                expected[node].category = (CheckCategory)(0x100 + node);
                expected[node].constant_kind = (CheckConstantKind)(0x200 + node);
            }
            rows += !check_test_stream_rest_zero(&expected[node]);
        }
        // Written in an order other than the nodes', as the vector path's regions write them, each node once.
        for (uint32_t step = 0; step < count; step += 1)
        {
            const uint32_t node = (uint32_t)(((uint64_t)step * 2731) % count);
            check_stream_set_value(&columns, node, &expected[node]);
            check_stream_set_value(&whole, node, &expected[node]);
        }
        check_test_expect(test, columns.payload_count == rows, "stream columns: a row for exactly the values whose rest isn't zero");
        bool same = true;
        bool fields = true;
        for (uint32_t node = 0; node < count; node += 1)
        {
            const CheckValue a = check_stream_value(&columns, node);
            const CheckValue b = check_stream_value(&whole, node);
            same &= memcmp(&a, &expected[node], sizeof(CheckValue)) == 0 && memcmp(&b, &expected[node], sizeof(CheckValue)) == 0;
            for (uint32_t s = 0; s < 2; s += 1)
            {
                const CheckStream* stream = s == 0 ? &columns : &whole;
                fields &= check_stream_type(stream, node) == expected[node].type && check_stream_flags(stream, node) == expected[node].flags &&
                          check_stream_object(stream, node) == expected[node].object &&
                          check_stream_overload_head(stream, node) == expected[node].overload_head &&
                          (check_test_stream_rest_zero(&expected[node]) ?
                           (check_stream_category(stream, node) == expected[node].category && check_stream_constant_kind(stream, node) == expected[node].constant_kind) :
                           true);
            }
        }
        check_test_expect(test, same, "stream columns: every value read back byte for byte, from columns and from whole values");
        check_test_expect(test, fields, "stream columns: the fields' readers agree with the values");
        // A node written again: a value with a rest keeps its row, and one without drops it.
        uint32_t with = 0;
        while (with < count && check_test_stream_rest_zero(&expected[with]))
        {
            with += 1;
        }
        bool rewritten = with < count;
        if (rewritten)
        {
            const uint32_t row = columns.payload_rows[with];
            CheckValue changed = expected[with];
            changed.integer += 1;
            check_stream_set_value(&columns, with, &changed);
            const CheckValue read = check_stream_value(&columns, with);
            rewritten = columns.payload_rows[with] == row && columns.payload_count == rows && memcmp(&read, &changed, sizeof(changed)) == 0;
            const CheckValue plain = { .type = BUSTER_TYPE_INT, .category = CHECK_LVALUE, .flags = 5, .object = 9 };
            check_stream_set_value(&columns, with, &plain);
            const CheckValue cleared = check_stream_value(&columns, with);
            rewritten = rewritten && columns.payload_rows[with] == 0 && memcmp(&cleared, &plain, sizeof(plain)) == 0;
        }
        check_test_expect(test, rewritten, "stream columns: a node written again keeps its row, or drops it without a rest");
        // A row index past the rows written (an arena's dirty value where a failed run wrote none) reads as no row.
        columns.payload_rows[0] = columns.payload_count + 1;
        const CheckValue bounded = check_stream_value(&columns, 0);
        check_test_expect(test, bounded.integer == 0 && bounded.type == columns.value_types[0], "stream columns: a row index past the rows reads none");
    }
    arena_release(&arena);
}

static const char check_test_fresh_output_source[] =
    "typedef struct pair { int a; int b; } pair;\n"
    "static int counter;\n"
    "int tentative;\n"
    "int tentative;\n"
    "_Thread_local int per_thread = 3;\n"
    "extern int named __asm__(\"renamed\");\n"
    "enum colour { red, green = 4, blue };\n"
    "__attribute__((aligned(16))) static char buffer[32];\n"
    "static inline int twice(int x) { return x * 2; }\n"
    "__attribute__((noinline)) int sum(const pair* p, int n)\n"
    "{\n"
    "    register int total = 0;\n"
    "    static const int table[] = { 1, 2, 3, red, blue };\n"
    "    for (int i = 0; i < n; i += 1) { total += p[i].a + p[i].b + table[i % 5]; }\n"
    "    switch (n) { case 1: total += 1; break; case 2 ... 4: total -= 1; break; default: break; }\n"
    "    if (total > 100) goto done;\n"
    "    total += twice(counter) + per_thread + (int)sizeof(buffer) + (pair){ 1, 2 }.b;\n"
    "done:\n"
    "    return total + named;\n"
    "}\n"
    "int main(void) { pair p[2] = { { 1, 2 }, { 3, 4 } }; counter = green; return sum(p, 2) - 18; }\n";

// The reader's output columns whose empty value is zero, of `input` over `count + 1` tokens, against `other`'s: equal
// bytes, and for the attributes, the same records wherever each arena holds them.
static bool check_test_fresh_columns_equal(const CheckInput* input, const CheckInput* other, uint32_t count)
{
    const uint64_t slots = (uint64_t)count + 1;
    bool equal = memcmp(input->declared_types, other->declared_types, slots * sizeof(TypeId)) == 0 &&
                 memcmp(input->declaration_kinds, other->declaration_kinds, slots) == 0 &&
                 memcmp(input->constant_warnings, other->constant_warnings, slots) == 0 &&
                 memcmp(input->constant_warning_ignored, other->constant_warning_ignored, slots) == 0 &&
                 memcmp(input->defined, other->defined, slots) == 0 && memcmp(input->tentative, other->tentative, slots) == 0 &&
                 memcmp(input->static_objects, other->static_objects, slots) == 0 && memcmp(input->registers, other->registers, slots) == 0 &&
                 memcmp(input->thread_local, other->thread_local, slots) == 0 && memcmp(input->compound_objects, other->compound_objects, slots) == 0 &&
                 memcmp(input->function_flags, other->function_flags, slots) == 0 && memcmp(input->explicit_abis, other->explicit_abis, slots) == 0 &&
                 memcmp(input->expression_regions, other->expression_regions, slots * sizeof(uint32_t)) == 0 &&
                 memcmp(input->linkage, other->linkage, slots) == 0 && memcmp(input->objects, other->objects, slots * sizeof(uint32_t)) == 0 &&
                 (input->asm_ids == NULL) == (other->asm_ids == NULL) &&
                 (input->asm_ids == NULL || memcmp(input->asm_ids, other->asm_ids, slots * sizeof(uint32_t)) == 0) &&
                 input->graph.capacity == other->graph.capacity &&
                 memcmp(input->graph.warning_suppressed, other->graph.warning_suppressed, input->graph.capacity) == 0;
    for (uint32_t token = 0; equal && token <= count; token += 1)
    {
        const CheckInputAttributes* a = input->attributes[token];
        const CheckInputAttributes* b = other->attributes[token];
        equal = (a == NULL) == (b == NULL) && (a == NULL || a->count == b->count);
    }
    return equal;
}

static __attribute__((noinline)) void check_test_fresh_output(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { .tree_only = true };
    const bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) &&
                       pp_run_source(&pp, "fresh-output.c", (const uint8_t*)check_test_fresh_output_source, sizeof(check_test_fresh_output_source) - 1) &&
                       parse_translation_unit(&parser, &pp);
    check_test_expect(test, ready, "fresh output: source setup");
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        parser.options.scalar = path == 0;
        CheckInput inputs[2];
        CheckStream streams[2];
        CheckTestTypes types[2] = { 0 };
        Arena arenas[2] = { arena_reserve(UINT64_C(1) << 28), arena_reserve(UINT64_C(1) << 28) };
        Arena outputs[2] = { arena_reserve(UINT64_C(1) << 26), arena_reserve(UINT64_C(1) << 26) };
        Arena scratch = arena_reserve(UINT64_C(1) << 28);
        bool checked[2] = { false, false };
        bool untouched[2] = { false, false };
        for (uint32_t layout = 0; layout < 2; layout += 1)
        {
            // Layout 1's pages hold a pattern where the columns go: something pushed, written and popped before.
            uint8_t* used = layout == 1 ? arena_push(&arenas[1], UINT64_C(48) << 20, 64) : NULL;
            if (used != NULL)
            {
                memset(used, 0xa5, UINT64_C(48) << 20);
                arena_pop_to(&arenas[1], 0);
            }
            untouched[layout] = arena_untouched(&arenas[layout]);
            checked[layout] = check_test_types_open(&types[layout]) &&
                              check_input_compile(&inputs[layout], &parser, &pp, &types[layout].table, &arenas[layout], &scratch, true, parser.tokens.count, NULL) &&
                              check_graph_run(&types[layout].table, &inputs[layout].graph, &outputs[layout], &scratch, &(const CheckOptions){ .scalar = path == 0 },
                                              &streams[layout]);
        }
        char description[160];
        snprintf(description, sizeof(description), "fresh output: %s path, a fresh arena is untouched and a reused one isn't", path == 0 ? "scalar" : "vector");
        check_test_expect(test, untouched[0] && !untouched[1], description);
        snprintf(description, sizeof(description), "fresh output: %s path, both readers check the source", path == 0 ? "scalar" : "vector");
        check_test_expect(test, checked[0] && checked[1] && streams[0].report_count == 0, description);
        if (checked[0] && checked[1])
        {
            snprintf(description, sizeof(description), "fresh output: %s path, the columns without clearing are the cleared ones", path == 0 ? "scalar" : "vector");
            check_test_expect(test, inputs[0].token_count == inputs[1].token_count && check_test_fresh_columns_equal(&inputs[0], &inputs[1], inputs[0].token_count),
                              description);
            snprintf(description, sizeof(description), "fresh output: %s path, the typed streams are the same", path == 0 ? "scalar" : "vector");
            check_test_expect(test, check_test_stream_equal(&streams[0], &streams[1], inputs[0].graph.argument_count), description);
        }
        for (uint32_t layout = 0; layout < 2; layout += 1)
        {
            check_test_types_close(&types[layout]);
            arena_release(&arenas[layout]);
            arena_release(&outputs[layout]);
        }
        arena_release(&scratch);
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
}
