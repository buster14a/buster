// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Default in-process libFuzzer entry. Every input owns and releases its state;
// malformed C is normal, while scalar/native lexer disagreement is a crash.
// No generated code is executed and include/embed queries never access files.
// BUSTER_FUZZ_ENTRY can replace this entire callback at build time.
#include <stdlib.h>
#include <string.h>

#define BUSTER_FUZZ_MAX_BYTES (64u << 10)

static void buster_fuzz_lexer(const uint8_t* data, size_t size)
{
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(size);
    uint8_t* kinds[2] = { malloc(capacity), malloc(capacity) };
    uint32_t* starts[2] = { malloc(capacity * sizeof(uint32_t)), malloc(capacity * sizeof(uint32_t)) };
    uint32_t* ends[2] = { malloc(capacity * sizeof(uint32_t)), malloc(capacity * sizeof(uint32_t)) };
    if (kinds[0] == 0 || kinds[1] == 0 || starts[0] == 0 || starts[1] == 0 || ends[0] == 0 || ends[1] == 0)
    {
        abort();
    }
    const int64_t scalar = c_tokenize_scalar(data, size, kinds[0], starts[0], ends[0], 0);
    const int64_t native = c_tokenize(data, size, kinds[1], starts[1], ends[1], 0);
    if (scalar != native || scalar > (int64_t)capacity)
    {
        abort();
    }
    if (scalar > 0 && (memcmp(kinds[0], kinds[1], (size_t)scalar) != 0 ||
                       memcmp(starts[0], starts[1], (size_t)scalar * sizeof(uint32_t)) != 0 ||
                       memcmp(ends[0], ends[1], (size_t)scalar * sizeof(uint32_t)) != 0))
    {
        abort();
    }
    for (unsigned path = 0; path < 2; path += 1)
    {
        free(kinds[path]);
        free(starts[path]);
        free(ends[path]);
    }
}

static void buster_fuzz_check(Parser* parser, Preprocessor* pp)
{
    const uint32_t tokens = parser->tokens.count;
    if (tokens <= (1u << 20))
    {
        const TypeCapacity capacity = {
            .types = tokens * 4 + 64,
            .parameters = tokens,
            .tags = tokens,
            .members = tokens,
        };
        Arena types_arena = arena_reserve(type_capacity_bytes(capacity));
        Arena index_arena = arena_reserve(type_index_capacity_bytes(capacity));
        Arena input_arena = arena_reserve(1ull << 28);
        Arena output_arena = arena_reserve(1ull << 28);
        Arena scratch = arena_reserve(1ull << 28);
        TypeTable types = { 0 };
        CheckInput input = { 0 };
        if (type_initialize(&types, &types_arena, &index_arena, capacity) && input_arena.base != 0 && output_arena.base != 0 && scratch.base != 0 &&
            check_input(&input, parser, pp, &types, &input_arena, &scratch))
        {
            const CheckOptions options = { .verify = true };
            CheckStream stream = { 0 };
            if (check_graph_run(&types, &input.graph, &output_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_input_packed_finalize(&input, parser, &types, &stream, &scratch))
            {
                CheckInitializers initializers = { 0 };
                (void)check_initializers_run(&initializers, &input, &stream, parser, &types, &output_arena, &scratch);
            }
        }
        arena_release(&scratch);
        arena_release(&output_arena);
        arena_release(&input_arena);
        arena_release(&index_arena);
        arena_release(&types_arena);
    }
}

int buster_fuzz_test_input(const uint8_t* data, size_t size)
{
    int result = -1;
    if (size <= BUSTER_FUZZ_MAX_BYTES && (data != 0 || size == 0))
    {
        // Large owning descriptors stay out of libFuzzer's callback stack.
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        identifiers = (InternTable){ 0 };
        pp = (Preprocessor){ 0 };
        parser = (Parser){ 0 };
        buster_fuzz_lexer(data, size);
        const PPOptions options = {
            .language = { .standard = 2023, .gnu = true },
            .no_standard_includes = true,
            .no_external_files = true,
            .semantic_pragmas = true,
            .quiet = true,
            .error_limit = 20,
        };
        const ParseOptions parse_options = { .extension_metadata = true };
        if (intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) &&
            pp_run_source(&pp, "fuzz.c", data, (uint32_t)size) && parse_translation_unit(&parser, &pp))
        {
            buster_fuzz_check(&parser, &pp);
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        scratch_release();
        result = 0;
    }
    return result;
}
