// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Diagnostic-only reduction of buster14a/buster at cb608c1f616f8fe26234bcaaa869caccc571de63.
// check_common_enum_coerced and check_stream_type retain the production helper bodies;
// surrounding structures retain only their accessed fields. The generator and reverse
// scalar oracle are original. No sanitizer suppression or unpoisoning is performed.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#endif

// Diagnostic switches leave input generation and the independent scalar oracle unchanged.
// BUSTER_PROBE_VECTORIZE requests vectorization; verify emitted IR and compiler remarks.
#ifndef BUSTER_PROBE_INLINE
#define BUSTER_PROBE_INLINE 0
#endif
#ifndef BUSTER_PROBE_VECTORIZE
#define BUSTER_PROBE_VECTORIZE 0
#endif
#if BUSTER_PROBE_INLINE
#define BUSTER_PROBE_HELPER_ATTRIBUTES inline __attribute__((always_inline))
#else
#define BUSTER_PROBE_HELPER_ATTRIBUTES __attribute__((noinline))
#endif

#define BUSTER_CHECK_NONE UINT32_MAX
#define C_BINDING_ENUMERATOR 3u

typedef uint32_t TypeId;
typedef enum CheckOp
{
    CHECK_OP_VALUE,
    CHECK_OP_OTHER,
} CheckOp;

typedef struct CheckInputEnumDefinition
{
    TypeId type;
    uint32_t open;
    uint32_t close;
} CheckInputEnumDefinition;

typedef struct CheckGraph
{
    const uint32_t* tokens;
    const CheckOp* ops;
} CheckGraph;

typedef struct CheckInput
{
    CheckGraph graph;
    uint32_t token_count;
    const uint32_t* declarations;
    const uint8_t* declaration_kinds;
    const CheckInputEnumDefinition* enum_definitions;
    uint32_t enum_definition_count;
} CheckInput;

typedef struct CheckValue
{
    TypeId type;
} CheckValue;

typedef struct CheckStream
{
    const CheckValue* dense_values;
    const TypeId* value_types;
} CheckStream;

_Static_assert(sizeof(CheckInputEnumDefinition) == 3 * sizeof(uint32_t), "check only initialized record fields");

static volatile uint32_t probe_runtime_seed = UINT32_C(0x61a18b37);

static TypeId check_stream_type(const CheckStream* stream, uint32_t node)
{
    return stream->dense_values != NULL ? stream->dense_values[node].type : stream->value_types[node];
}

// This retains the complete production helper body. The default keeps its existing
// noinline annotation; BUSTER_PROBE_INLINE exposes the body to the query-loop caller.
static BUSTER_PROBE_HELPER_ATTRIBUTES TypeId check_common_enum_coerced(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    TypeId result = check_stream_type(stream, node);
    const uint32_t token = graph->tokens[node];
    const uint32_t declaration = graph->ops[node] == CHECK_OP_VALUE && token < input->token_count && input->declarations != NULL ?
                                 input->declarations[token] : BUSTER_CHECK_NONE;
    if (declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR)
    {
#if BUSTER_PROBE_VECTORIZE
#pragma clang loop vectorize(enable)
#endif
        // The last enclosing completed definition wins. Reduce one-based indices so every unmatched lane is zero,
        // then load the selected type once; a conditional TypeId recurrence leaves inactive vector lanes undefined.
        uint32_t selected = 0;
        for (uint32_t i = 0; i < input->enum_definition_count; i += 1)
        {
            const CheckInputEnumDefinition* definition = input->enum_definitions + i;
            const uint32_t candidate = definition->open < declaration && declaration < definition->close && token > definition->close ? i + 1 : 0;
            selected = candidate > selected ? candidate : selected;
        }
        result = selected != 0 ? input->enum_definitions[selected - 1].type : result;
    }
    return result;
}

// The last source-order enclosing, completed definition wins. Reverse traversal
// stops at its first match. Volatile field loads keep this independent oracle scalar.
static __attribute__((noinline)) TypeId probe_reference(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    TypeId result = check_stream_type(stream, node);
    const uint32_t token = input->graph.tokens[node];
    if (input->graph.ops[node] == CHECK_OP_VALUE && token < input->token_count && input->declarations != NULL)
    {
        const uint32_t declaration = input->declarations[token];
        if (declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR)
        {
            const volatile CheckInputEnumDefinition* definitions = input->enum_definitions;
            for (uint32_t remaining = input->enum_definition_count; remaining != 0; remaining -= 1)
            {
                const uint32_t index = remaining - 1;
                const uint32_t open = definitions[index].open;
                const uint32_t close = definitions[index].close;
                if (open < declaration && declaration < close && close < token)
                {
                    result = definitions[index].type;
                    break;
                }
            }
        }
    }
    return result;
}

static __attribute__((noinline)) void probe_initialize(CheckInputEnumDefinition* definitions, uint32_t count, uint32_t shape)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        // Shape 0: disjoint source ranges. Shape 1: properly nested ranges.
        // Shape 2: overlapping sorted ranges, exercising last-match semantics.
        const uint32_t open = shape == 0 ? 4 * i + 2 : i + 1;
        const uint32_t close = shape == 0 ? open + 2 : shape == 1 ? 2 * count + 2 - i : i + count + 3;
        definitions[i] = (CheckInputEnumDefinition){
            .type = (i + 2) * 16,
            .open = open,
            .close = close,
        };
    }
}

static void probe_shadow(const void* data, size_t bytes)
{
#if __has_feature(memory_sanitizer)
    if (bytes != 0)
    {
        __msan_check_mem_is_initialized(data, bytes);
    }
#else
    (void)data;
    (void)bytes;
#endif
}

static __attribute__((noinline)) bool probe_case(uint32_t count, uint32_t shape, bool shadow_checks, uint64_t* checks, uint64_t* checksum)
{
    const uint32_t token_count = count * 4 + 64;
    CheckInputEnumDefinition* definitions = count != 0 ? malloc((size_t)count * sizeof(*definitions)) : NULL;
    uint32_t* declarations = malloc((size_t)token_count * sizeof(*declarations));
    uint8_t* declaration_kinds = malloc(token_count);
    bool valid = (count == 0 || definitions != NULL) && declarations != NULL && declaration_kinds != NULL;
    if (valid)
    {
        probe_initialize(definitions, count, shape);
        memset(declarations, 0xff, (size_t)token_count * sizeof(*declarations));
        memset(declaration_kinds, 0, token_count);
        uint32_t tokens[1] = { 0 };
        CheckOp ops[1] = { CHECK_OP_VALUE };
        TypeId types[1] = { UINT32_C(0x00100000) };
        CheckValue dense[1] = { { .type = UINT32_C(0x00100000) } };
        CheckInput input = {
            .graph = { .tokens = tokens, .ops = ops },
            .token_count = token_count,
            .declarations = declarations,
            .declaration_kinds = declaration_kinds,
            .enum_definitions = definitions,
            .enum_definition_count = count,
        };
        CheckStream stream = { .value_types = types };
        if (shadow_checks)
        {
            probe_shadow(definitions, (size_t)count * sizeof(*definitions));
            probe_shadow(declarations, (size_t)token_count * sizeof(*declarations));
            probe_shadow(declaration_kinds, token_count);
            probe_shadow(types, sizeof(types));
            probe_shadow(dense, sizeof(dense));
            probe_shadow(&input.enum_definition_count, sizeof(input.enum_definition_count));
        }
        uint32_t state = probe_runtime_seed;
        for (uint32_t layout = 0; valid && layout < 2; layout += 1)
        {
            stream.dense_values = layout != 0 ? dense : NULL;
            for (uint32_t query = 0; valid && query < 256; query += 1)
            {
                state = state * UINT32_C(1664525) + UINT32_C(1013904223);
                const uint32_t row = count != 0 ? state % count : 0;
                const uint32_t open = count != 0 ? definitions[row].open : 0;
                const uint32_t close = count != 0 ? definitions[row].close : 0;
                uint32_t declaration = open + 1;
                uint32_t token = token_count - 1;
                switch (query & 7)
                {
                    case 1: declaration = 0; break;
                    case 2: declaration = token_count - 2; break;
                    case 3: declaration = open; break;
                    case 4: declaration = close; break;
                    case 5: token = close; break;
                    case 6: token = close + 1; break;
                    case 7:
                    {
                        declaration = (state >> 5) % token_count;
                        token = (state >> 13) % token_count;
                    } break;
                    default: break;
                }
                tokens[0] = token;
                declarations[token] = declaration;
                declaration_kinds[declaration] = query % 19 == 0 ? 0 : C_BINDING_ENUMERATOR;
                ops[0] = query % 23 == 0 ? CHECK_OP_OTHER : CHECK_OP_VALUE;
                input.declarations = query % 29 == 0 ? NULL : declarations;
                if (shadow_checks)
                {
                    probe_shadow(tokens, sizeof(tokens));
                    probe_shadow(ops, sizeof(ops));
                    probe_shadow(declarations + token, sizeof(*declarations));
                    probe_shadow(declaration_kinds + declaration, sizeof(*declaration_kinds));
                }
                const TypeId expected = probe_reference(&input, &stream, 0);
                const TypeId actual = check_common_enum_coerced(&input, &stream, 0);
                if (shadow_checks)
                {
                    probe_shadow(&actual, sizeof(actual));
                    probe_shadow(&expected, sizeof(expected));
                }
                *checks += 1;
                *checksum += actual;
                valid = actual == expected;
                if (!valid)
                {
                    fprintf(stderr, "enum probe mismatch: count=%u shape=%u layout=%u query=%u declaration=%u token=%u actual=%u expected=%u\n",
                            count, shape, layout, query, declaration, token, actual, expected);
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "enum probe allocation failure: count=%u shape=%u\n", count, shape);
    }
    free(definitions);
    free(declarations);
    free(declaration_kinds);
    return valid;
}

int main(int argc, char** argv)
{
    fprintf(stderr, "enum probe modes: inline=%u vectorize_requested=%u\n", (unsigned)BUSTER_PROBE_INLINE, (unsigned)BUSTER_PROBE_VECTORIZE);
    const bool shadow_checks = argc < 2 || strcmp(argv[1], "no-shadow-check") != 0;
    uint64_t checks = 0;
    uint64_t checksum = 0;
    bool valid = true;
    for (uint32_t count_case = 0; valid && count_case < 132; count_case += 1)
    {
        const uint32_t count = count_case < 130 ? count_case : count_case == 130 ? 257 : 8193;
        for (uint32_t shape = 0; valid && shape < 3; shape += 1)
        {
            fprintf(stderr, "enum probe begin: count=%u shape=%u shadow_checks=%u\n", count, shape, (unsigned)shadow_checks);
            fflush(stderr);
            valid = probe_case(count, shape, shadow_checks, &checks, &checksum);
        }
    }
    printf("enum probe: checks=%llu checksum=%llu result=%s\n", (unsigned long long)checks, (unsigned long long)checksum, valid ? "PASS" : "FAIL");
    return valid ? 0 : 1;
}
