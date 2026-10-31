// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original bounded evaluator for Clang builtin feature expressions. Grammar facts, not implementation, from:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Basic/BuiltinTargetFeatures.h
// Comma binds more tightly than pipe; parentheses group expressions. Empty input means no requirement. Malformed
// input is refused even when an earlier alternative is available. Unknown atoms in any branch stay unsupported.
//
// The producer supplies a complete, already closed, case-sensitive feature-name table with explicit true/false
// availability. An absent name is unsupported, never an implicit false. Duplicate names, including equal-valued
// duplicates, are invalid. This module does not compute CPU/default/target-attribute closure, inspect the host,
// authorize source builtins, or establish instruction/runtime support. Null/count pairs and feature names are checked.
// Names are nonempty byte strings excluding NUL, ASCII whitespace/control bytes, comma, pipe and parentheses.
// Other bytes are literal, with no negation, aliases or normalization. Inputs require no terminator or padding.
//
// Lengths/counts/capacities are uint32_t, up to UINT32_MAX; each slice must actually contain that many elements.
// frames has one writable entry per simultaneously open parenthesis, with no entry needed for the root expression.
// Null is accepted only with a zero corresponding length/count/capacity. Scratch and all input slices must be
// disjoint and naturally aligned; scratch contents are unspecified after any result. No allocation or recursion.
// Invalid syntax/table takes precedence over unknown names; insufficient nesting scratch returns CAPACITY without
// accepting the expression or inspecting the remaining suffix. Zero-initialized statuses are INVALID.
//
// Metadata parsing uses a small ordered stack. Name validation/lookup is scalar and makes no throughput claim;
// table validation is quadratic in feature_count, and each atom scans the table. The batch validates the table once,
// then reuses scratch in source order. Legal batch partitions preserve statuses. Its input/output slices are disjoint;
// a zero batch accepts null slices. A missing batch input/output slice returns INVALID without publishing anything.
// Target-host producer/consumer measurements and integration are the catalog consumer's follow-up.
#ifndef BUSTER_C_BUILTIN_INTRINSIC_FEATURES_INCLUDED
#define BUSTER_C_BUILTIN_INTRINSIC_FEATURES_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum CBuiltinIntrinsicFeatureStatus
{
    CBUILTIN_INTRINSIC_FEATURE_INVALID,
    CBUILTIN_INTRINSIC_FEATURE_AVAILABLE,
    CBUILTIN_INTRINSIC_FEATURE_MISSING,
    CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED,
    CBUILTIN_INTRINSIC_FEATURE_CAPACITY,
} CBuiltinIntrinsicFeatureStatus;

typedef struct CBuiltinIntrinsicFeature CBuiltinIntrinsicFeature;
struct CBuiltinIntrinsicFeature
{
    const char* name;
    uint32_t name_length;
    bool available;
};

typedef struct CBuiltinIntrinsicFeatureFrame CBuiltinIntrinsicFeatureFrame;
struct CBuiltinIntrinsicFeatureFrame
{
    bool conjunction;
    bool disjunction;
};

typedef struct CBuiltinIntrinsicFeatureExpression CBuiltinIntrinsicFeatureExpression;
struct CBuiltinIntrinsicFeatureExpression
{
    const char* characters;
    uint32_t length;
};

static bool cbuiltin_intrinsic_feature_name_byte(uint8_t value)
{
    const bool result = value > 32 && value != 127 && value != ',' && value != '|' && value != '(' && value != ')';
    return result;
}

static bool cbuiltin_intrinsic_feature_table_valid(const CBuiltinIntrinsicFeature* features, uint32_t feature_count)
{
    bool valid = features != 0 || feature_count == 0;
    for (uint32_t i = 0; valid && i < feature_count; i += 1)
    {
        const CBuiltinIntrinsicFeature* feature = features + i;
        valid = feature->name != 0 && feature->name_length != 0;
        for (uint32_t j = 0; valid && j < feature->name_length; j += 1)
        {
            valid = cbuiltin_intrinsic_feature_name_byte((uint8_t)feature->name[j]);
        }
        for (uint32_t j = 0; valid && j < i; j += 1)
        {
            valid = features[j].name_length != feature->name_length || memcmp(features[j].name, feature->name, feature->name_length) != 0;
        }
    }
    return valid;
}

static CBuiltinIntrinsicFeatureStatus cbuiltin_intrinsic_feature_expression(const char* expression, uint32_t expression_length,
                                                                          const CBuiltinIntrinsicFeature* features, uint32_t feature_count,
                                                                          CBuiltinIntrinsicFeatureFrame* frames, uint32_t frame_capacity)
{
    CBuiltinIntrinsicFeatureStatus status = CBUILTIN_INTRINSIC_FEATURE_AVAILABLE;
    CBuiltinIntrinsicFeatureFrame current = {
        .conjunction = true,
    };
    uint32_t position = 0;
    uint32_t depth = 0;
    bool needs_operand = true;
    bool unknown = false;
    if ((expression == 0 && expression_length != 0) || (frames == 0 && frame_capacity != 0))
    {
        status = CBUILTIN_INTRINSIC_FEATURE_INVALID;
    }
    while (status == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE && position < expression_length)
    {
        const uint8_t value = (uint8_t)expression[position];
        if (needs_operand && value == '(')
        {
            if (depth == frame_capacity)
            {
                status = CBUILTIN_INTRINSIC_FEATURE_CAPACITY;
            }
            else
            {
                frames[depth] = current;
                depth += 1;
                current = (CBuiltinIntrinsicFeatureFrame){
                    .conjunction = true,
                };
                position += 1;
            }
        }
        else if (needs_operand && cbuiltin_intrinsic_feature_name_byte(value))
        {
            const uint32_t first = position;
            do
            {
                position += 1;
            } while (position < expression_length && cbuiltin_intrinsic_feature_name_byte((uint8_t)expression[position]));
            const uint32_t length = position - first;
            bool found = false;
            bool available = false;
            for (uint32_t i = 0; i < feature_count; i += 1)
            {
                const CBuiltinIntrinsicFeature* feature = features + i;
                if (feature->name_length == length && memcmp(expression + first, feature->name, length) == 0)
                {
                    found = true;
                    available = feature->available;
                }
            }
            unknown |= !found;
            current.conjunction &= available;
            needs_operand = false;
        }
        else if (!needs_operand && value == ',')
        {
            position += 1;
            needs_operand = true;
        }
        else if (!needs_operand && value == '|')
        {
            current.disjunction |= current.conjunction;
            current.conjunction = true;
            position += 1;
            needs_operand = true;
        }
        else if (!needs_operand && value == ')' && depth != 0)
        {
            const bool grouped = current.disjunction | current.conjunction;
            depth -= 1;
            current = frames[depth];
            current.conjunction &= grouped;
            position += 1;
        }
        else
        {
            status = CBUILTIN_INTRINSIC_FEATURE_INVALID;
        }
    }
    if (status == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE && expression_length != 0)
    {
        if (needs_operand || depth != 0)
        {
            status = CBUILTIN_INTRINSIC_FEATURE_INVALID;
        }
        else if (unknown)
        {
            status = CBUILTIN_INTRINSIC_FEATURE_UNSUPPORTED;
        }
        else if (!(current.disjunction | current.conjunction))
        {
            status = CBUILTIN_INTRINSIC_FEATURE_MISSING;
        }
    }
    return status;
}

static __attribute__((unused)) CBuiltinIntrinsicFeatureStatus c_builtin_intrinsic_features_evaluate(const char* expression, uint32_t expression_length,
                                                                                                  const CBuiltinIntrinsicFeature* features,
                                                                                                  uint32_t feature_count,
                                                                                                  CBuiltinIntrinsicFeatureFrame* frames,
                                                                                                  uint32_t frame_capacity)
{
    CBuiltinIntrinsicFeatureStatus status = CBUILTIN_INTRINSIC_FEATURE_INVALID;
    if (cbuiltin_intrinsic_feature_table_valid(features, feature_count))
    {
        status = cbuiltin_intrinsic_feature_expression(expression, expression_length, features, feature_count, frames, frame_capacity);
    }
    return status;
}

// Whether a table is one c_builtin_intrinsic_features_evaluate takes, which a caller that evaluates many expressions
// against tables of the same names establishes once (code generation's check of target features, #148).
static __attribute__((unused)) bool c_builtin_intrinsic_features_table_valid(const CBuiltinIntrinsicFeature* features, uint32_t feature_count)
{
    return cbuiltin_intrinsic_feature_table_valid(features, feature_count);
}

// c_builtin_intrinsic_features_evaluate of a table its caller found valid (c_builtin_intrinsic_features_table_valid),
// whose availabilities may have changed since: its status without the table's quadratic validation.
static __attribute__((unused)) CBuiltinIntrinsicFeatureStatus c_builtin_intrinsic_features_evaluate_valid(const char* expression, uint32_t expression_length,
                                                                                                        const CBuiltinIntrinsicFeature* features,
                                                                                                        uint32_t feature_count,
                                                                                                        CBuiltinIntrinsicFeatureFrame* frames,
                                                                                                        uint32_t frame_capacity)
{
    return cbuiltin_intrinsic_feature_expression(expression, expression_length, features, feature_count, frames, frame_capacity);
}

static __attribute__((unused)) CBuiltinIntrinsicFeatureStatus c_builtin_intrinsic_features_evaluate_batch(const CBuiltinIntrinsicFeatureExpression* expressions,
                                                                                                        uint32_t expression_count,
                                                                                                        const CBuiltinIntrinsicFeature* features,
                                                                                                        uint32_t feature_count,
                                                                                                        CBuiltinIntrinsicFeatureFrame* frames,
                                                                                                        uint32_t frame_capacity,
                                                                                                        CBuiltinIntrinsicFeatureStatus* statuses)
{
    CBuiltinIntrinsicFeatureStatus status = CBUILTIN_INTRINSIC_FEATURE_AVAILABLE;
    if ((expression_count != 0 && (expressions == 0 || statuses == 0)) || (frames == 0 && frame_capacity != 0) ||
        !cbuiltin_intrinsic_feature_table_valid(features, feature_count))
    {
        status = CBUILTIN_INTRINSIC_FEATURE_INVALID;
    }
    else
    {
        for (uint32_t i = 0; i < expression_count; i += 1)
        {
            statuses[i] = cbuiltin_intrinsic_feature_expression(expressions[i].characters, expressions[i].length, features, feature_count, frames, frame_capacity);
        }
    }
    return status;
}

#endif
