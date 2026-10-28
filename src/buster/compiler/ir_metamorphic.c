// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded, deterministic IR metamorphic testing. Generated scalar programs have an independent arithmetic/memory
// model. Rebuilding changes representation through public IR columns, never a compiler implementation. Both
// complete verifiers, text consumers, checked execution and each admitted optimizer must retain the observations.
// Layout: generation/model; scalar/SIMD ID remapping and transformations; consumer checks; artifacts/replay; CLI.

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#define BUSTER_IR_META_VERSION 1u
#define BUSTER_IR_META_INSTRUCTIONS 128u
#define BUSTER_IR_META_BLOCKS 16u
#define BUSTER_IR_META_TEXT (64u << 10)
#define BUSTER_IR_META_ARENA (8ull << 20)
#define BUSTER_IR_META_VARIANTS 7u
#define BUSTER_IR_META_PASSES 8u
#define BUSTER_IR_META_CANARY UINT64_C(0x57419a3bc826d0ef)

typedef enum IrMetaFailure
{
    IR_META_OK,
    IR_META_GENERATION,
    IR_META_TRANSFORM,
    IR_META_VERIFY,
    IR_META_TEXT,
    IR_META_OPTIMIZE,
    IR_META_EXECUTION,
    IR_META_UNDEFINED,
    IR_META_SEMANTICS,
    IR_META_MEMORY,
    IR_META_STEPS,
    IR_META_TIME,
    IR_META_UNSUPPORTED,
    IR_META_RESOURCE,
    IR_META_ARTIFACT,
    IR_META_FAILURE_COUNT,
} IrMetaFailure;

typedef struct IrMetaCase IrMetaCase;
struct IrMetaCase
{
    uint64_t seed;
    uint32_t number;
    uint32_t shape;
    uint64_t k;
    uint64_t l;
    uint32_t n;
    uint32_t m;
};

typedef struct IrMetaObservation IrMetaObservation;
struct IrMetaObservation
{
    InterpreterResult execution;
    uint64_t memory;
    bool guards;
};

typedef struct IrMetaOptions IrMetaOptions;
struct IrMetaOptions
{
    uint64_t seed;
    uint64_t max_steps;
    uint64_t max_ms;
    uint32_t first;
    uint32_t count;
    const char* artifacts;
    const char* revision;
    const char* replay;
    bool silent;
    uint32_t inject;
    uint32_t replay_failure;
    uint32_t replay_variant;
    uint32_t replay_pass;
    uint32_t replay_scalar;
    uint64_t replay_input;
};

typedef struct IrMetaTally IrMetaTally;
struct IrMetaTally
{
    uint64_t checks;
    uint64_t cases;
    uint64_t variants;
    uint64_t changed;
    uint64_t executions;
    uint64_t negative;
    uint64_t fingerprint;
    uint64_t failures[IR_META_FAILURE_COUNT];
};

static const char* const ir_meta_failure_names[] =
{
    "ok", "generation", "transform", "verify", "text", "optimize", "execution", "undefined", "semantics", "memory",
    "step-limit", "time-limit", "unsupported", "resource", "artifact",
};

static uint64_t ir_meta_random(uint64_t* state)
{
    uint64_t next = *state;
    next ^= next << 13;
    next ^= next >> 7;
    next ^= next << 17;
    *state = next;
    return next;
}

static uint64_t ir_meta_hash(uint64_t hash, const void* bytes, uint64_t length)
{
    const uint8_t* data = bytes;
    for (uint64_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t ir_meta_milliseconds(void)
{
    struct timespec value = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &value);
    const uint64_t result = (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
    return result;
}

static IrMetaCase ir_meta_case(uint64_t seed, uint32_t number)
{
    uint64_t random = seed ^ (UINT64_C(0x9e3779b97f4a7c15) * ((uint64_t)number + 1));
    random = random != 0 ? random : 1;
    IrMetaCase result = { .seed = seed, .number = number, .shape = number % 6 };
    result.k = ir_meta_random(&random) % 251 + 1;
    result.l = ir_meta_random(&random) % 253 + 1;
    result.n = (uint32_t)(ir_meta_random(&random) % 7) + 1;
    result.m = (uint32_t)(ir_meta_random(&random) % 5) + 1;
    return result;
}

static uint64_t ir_meta_model(const IrMetaCase* test, uint64_t input)
{
    uint64_t result = 0;
    switch (test->shape)
    {
        case 0: result = ((input + test->k) ^ test->l) * 3; break;
        case 1: result = (input & 1) != 0 ? input + test->k : input ^ test->l; break;
        case 2: result = input + test->n * test->k; break;
        case 3: result = input + (uint64_t)test->n * test->m * test->k; break;
        case 4: result = input == 0 ? input + test->k : input == 1 ? input ^ test->l : input == 2 ? input * test->k : input - test->l; break;
        case 5: result = (input + test->k) ^ test->l; break;
        default: break;
    }
    return result;
}

static bool ir_meta_generate(const IrMetaCase* test, char* text, uint64_t capacity, String8* result)
{
    int length = -1;
    const uint64_t k = test->k;
    const uint64_t l = test->l;
    switch (test->shape)
    {
        case 0:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nb0:\n"
                "%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n%%2 = const.i64 0x%" PRIx64 "\n%%3 = const.i64 0x%" PRIx64 "\n"
                "%%4 = const.i64 0x3\n%%5 = add.i64 %%0, %%2\n%%6 = xor.i64 %%5, %%3\n%%7 = mul.i64 %%6, %%4\n"
                "%%8 = store.void %%1, %%7 align=8\n%%9 = load.i64 %%1 align=8\n%%10 = ret.void %%9\n}\n", k, l);
            break;
        case 1:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nb0:\n%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n"
                "%%2 = const.i64 0x%" PRIx64 "\n%%3 = const.i64 0x%" PRIx64 "\n%%4 = const.i64 0x1\n"
                "%%5 = and.i64 %%0, %%4\n%%6 = icmp.i1 %%5, %%4 eq\n%%7 = cbr.void %%6, b1, b2\n"
                "b1:\n%%8 = add.i64 %%0, %%2\n%%9 = br.void b3\n"
                "b2:\n%%10 = xor.i64 %%0, %%3\n%%11 = br.void b3\n"
                "b3:\n%%12 = phi.i64 [%%8, b1], [%%10, b2]\n%%13 = store.void %%1, %%12 align=8\n"
                "%%14 = load.i64 %%1 align=8\n%%15 = ret.void %%14\n}\n", k, l);
            break;
        case 2:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nb0:\n%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n"
                "%%2 = const.i64 0x0\n%%3 = const.i64 0x1\n%%4 = const.i64 0x%x\n%%5 = const.i64 0x%" PRIx64 "\n%%6 = br.void b1\n"
                "b1:\n%%7 = phi.i64 [%%2, b0], [%%12, b2]\n%%8 = phi.i64 [%%0, b0], [%%11, b2]\n"
                "%%9 = icmp.i1 %%7, %%4 ult\n%%10 = cbr.void %%9, b2, b3\n"
                "b2:\n%%11 = add.i64 %%8, %%5\n%%12 = add.i64 %%7, %%3\n%%13 = store.void %%1, %%11 align=8 volatile\n%%14 = br.void b1\n"
                "b3:\n%%15 = store.void %%1, %%8 align=8 volatile\n%%16 = load.i64 %%1 align=8\n%%17 = ret.void %%16\n}\n", test->n, k);
            break;
        case 3:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nb0:\n%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n"
                "%%2 = const.i64 0x0\n%%3 = const.i64 0x1\n%%4 = const.i64 0x%x\n%%5 = const.i64 0x%x\n%%6 = const.i64 0x%" PRIx64 "\n%%7 = br.void b1\n"
                "b1:\n%%8 = phi.i64 [%%2, b0], [%%23, b5]\n%%9 = phi.i64 [%%0, b0], [%%16, b5]\n"
                "%%10 = icmp.i1 %%8, %%4 ult\n%%11 = cbr.void %%10, b2, b6\n"
                "b2:\n%%12 = br.void b3\n"
                "b3:\n%%13 = phi.i64 [%%2, b2], [%%19, b4]\n%%14 = phi.i64 [%%9, b2], [%%18, b4]\n"
                "%%15 = icmp.i1 %%13, %%5 ult\n%%16 = copy.i64 %%14\n%%17 = cbr.void %%15, b4, b5\n"
                "b4:\n%%18 = add.i64 %%14, %%6\n%%19 = add.i64 %%13, %%3\n%%20 = store.void %%1, %%18 align=8 volatile\n"
                "%%21 = br.void b3\n"
                "b5:\n%%22 = copy.i64 %%16\n%%23 = add.i64 %%8, %%3\n%%24 = br.void b1\n"
                "b6:\n%%25 = store.void %%1, %%9 align=8 volatile\n%%26 = load.i64 %%1 align=8\n%%27 = ret.void %%26\n}\n", test->n, test->m, k);
            break;
        case 4:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nb0:\n%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n"
                "%%2 = const.i64 0x%" PRIx64 "\n%%3 = const.i64 0x%" PRIx64 "\n"
                "%%4 = switch.void %%0, default b4, [0x0, b1], [0x1, b2], [0x2, b3]\n"
                "b1:\n%%5 = add.i64 %%0, %%2\n%%6 = br.void b5\n"
                "b2:\n%%7 = xor.i64 %%0, %%3\n%%8 = br.void b5\n"
                "b3:\n%%9 = mul.i64 %%0, %%2\n%%10 = br.void b5\n"
                "b4:\n%%11 = sub.i64 %%0, %%3\n%%12 = br.void b5\n"
                "b5:\n%%13 = phi.i64 [%%5, b1], [%%7, b2], [%%9, b3], [%%11, b4]\n"
                "%%14 = store.void %%1, %%13 align=8\n%%15 = load.i64 %%1 align=8\n%%16 = ret.void %%15\n}\n", k, l);
            break;
        case 5:
            length = snprintf(text, capacity,
                "function (i64) -> i64 {\nslot $0 size=8 align=8 type=i64 flags=0x00\nb0:\n"
                "%%0 = param.i64 0\n%%1 = symbol_addr.ptr @2\n%%2 = const.i64 0x%" PRIx64 "\n%%3 = const.i64 0x%" PRIx64 "\n"
                "%%4 = stack_addr.ptr $0\n%%5 = store.void %%4, %%0 align=8 volatile\n%%6 = load.i64 %%4 align=8 volatile\n"
                "%%7 = add.i64 %%6, %%2\n%%8 = xor.i64 %%7, %%3\n%%9 = store.void %%1, %%8 align=8 volatile\n"
                "%%10 = load.i64 %%1 align=8 volatile\n%%11 = ret.void %%10\n}\n", k, l);
            break;
        default: break;
    }
    const bool valid = length > 0 && (uint64_t)length < capacity;
    if (valid)
    {
        *result = (String8){ .pointer = text, .length = (uint64_t)length };
    }
    return valid;
}

static uint32_t ir_meta_value_mask(const IrFunction* function, uint32_t row)
{
    uint32_t mask = 0;
    switch (function->op[row])
    {
        case IR_COPY: case IR_NEG: case IR_NOT: case IR_TRUNC: case IR_ZEXT: case IR_SEXT:
        case IR_LOAD: case IR_CBR: mask = 1; break;
        case IR_RET: mask = function->a[row] != BUSTER_IR_NONE ? 1 : 0; break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_AND: case IR_OR: case IR_XOR:
        case IR_SHL: case IR_LSHR: case IR_ASHR: case IR_ICMP: case IR_STORE: mask = 3; break;
        case IR_SELECT: mask = 7; break;
        case IR_SWITCH: mask = 1; break;
        case IR_ADDRESS: mask = function->b[row] != BUSTER_IR_NONE ? 3 : 1; break;
        default: break;
    }
    return mask;
}

static void ir_meta_remap_column(uint32_t* column, const uint8_t* masks, uint32_t bit, uint32_t count, const uint32_t* map, bool scalar)
{
    (void)scalar;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t row = 0; row < count; row += 1)
        {
            if ((masks[row] & bit) != 0)
            {
                column[row] = map[column[row]];
            }
        }
    }
    else
    {
#if BUSTER_HOST_VECTOR
        for (uint32_t first = 0; first < count; first += 16)
        {
            const uint32_t lanes = count - first < 16 ? count - first : 16;
            const __mmask16 active = (__mmask16)((1u << lanes) - 1);
            const __m128i bytes = _mm_maskz_loadu_epi8(active, masks + first);
            const __m512i flags = _mm512_cvtepu8_epi32(bytes);
            const __mmask16 selected = _mm512_mask_test_epi32_mask(active, flags, _mm512_set1_epi32((int)bit));
            const __m512i indices = _mm512_maskz_loadu_epi32(selected, column + first);
            const __m512i values = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), selected, indices, map, 4);
            _mm512_mask_storeu_epi32(column + first, selected, values);
        }
#endif
    }
}

static IrMetaFailure ir_meta_transform(const IrFunction* source, uint32_t variant, bool scalar, Arena* output, Arena* scratch, IrFunction* result, bool* changed)
{
    const uint64_t mark = output->length;
    IrMetaFailure failure = IR_META_OK;
    if (source->instruction_count >= BUSTER_IR_META_INSTRUCTIONS - 2 || source->block_count >= BUSTER_IR_META_BLOCKS - 1)
    {
        failure = IR_META_RESOURCE;
    }
    else if (source->call_count != 0)
    {
        failure = IR_META_UNSUPPORTED;
    }
    else if (variant >= BUSTER_IR_META_VARIANTS)
    {
        failure = IR_META_TRANSFORM;
    }
    bool valid = failure == IR_META_OK;
    const IrCheck verified = valid ? ir_verify_complete(source, scratch, !scalar) : (IrCheck){ .error = IR_ERROR_LIMIT };
    if (valid && verified.error != IR_OK)
    {
        failure = verified.error == IR_ERROR_MEMORY || verified.error == IR_ERROR_LIMIT ? IR_META_RESOURCE : IR_META_VERIFY;
    }
    valid = valid && verified.error == IR_OK;
    uint32_t blocks[BUSTER_IR_META_BLOCKS];
    uint32_t values[BUSTER_IR_META_INSTRUCTIONS];
    uint32_t order[BUSTER_IR_META_INSTRUCTIONS];
    uint8_t masks[BUSTER_IR_META_INSTRUCTIONS] = { 0 };
    uint32_t swap = BUSTER_IR_NONE;
    uint32_t split = BUSTER_IR_NONE;
    uint32_t target = BUSTER_IR_NONE;
    *changed = false;
    for (uint32_t b = 0; valid && b < source->block_count; b += 1)
    {
        blocks[b] = variant == 1 && b != 0 ? source->block_count - b : b;
        *changed |= blocks[b] != b;
        const uint32_t last = source->block_offsets[b + 1] - 1;
        if (variant == 4 && split == BUSTER_IR_NONE && source->op[last] == IR_BR)
        {
            split = b;
            target = source->a[last];
            *changed = true;
        }
        for (uint32_t row = source->block_offsets[b]; valid && row <= last; row += 1)
        {
            const uint32_t op = source->op[row];
            valid = op == IR_CONST || op == IR_PARAM || op == IR_COPY || op == IR_ADD || op == IR_SUB || op == IR_MUL ||
                    op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_ICMP || op == IR_SELECT || op == IR_NEG || op == IR_NOT ||
                    op == IR_STACK_ADDR || op == IR_SYMBOL_ADDR || op == IR_ADDRESS || op == IR_LOAD || op == IR_STORE ||
                    op == IR_PHI || op == IR_BR || op == IR_CBR || op == IR_SWITCH || op == IR_RET || op == IR_UNREACHABLE;
            if (!valid)
            {
                failure = IR_META_UNSUPPORTED;
            }
            if (variant == 2 && swap == BUSTER_IR_NONE && row < last && op == IR_CONST && source->op[row + 1] == IR_CONST)
            {
                swap = row;
                *changed = true;
            }
        }
    }
    const bool dead_block = variant == 6;
    const uint32_t added = variant == 3 || dead_block ? 2 : split != BUSTER_IR_NONE ? 1 : 0;
    IrSizes sizes = {
        .instruction_count = (uint64_t)source->instruction_count + added,
        .block_count = (uint64_t)source->block_count + (split != BUSTER_IR_NONE || dead_block),
        .extra_count = source->extra_count,
        .constant_count = (uint64_t)source->constant_count + (variant == 3 || dead_block),
        .slot_count = source->slot_count,
        .address_count = source->address_count,
        .parameter_count = source->parameter_count,
    };
    IrFunction built = { 0 };
    if (valid)
    {
        const IrError created = ir_create(output, &sizes, &built);
        failure = created == IR_OK ? IR_META_OK : created == IR_ERROR_MEMORY || created == IR_ERROR_LIMIT ? IR_META_RESOURCE : IR_META_TRANSFORM;
        valid = failure == IR_META_OK;
    }
    if (valid)
    {
        built.result_type = source->result_type;
        built.symbol_count = source->symbol_count;
        if (source->extra_count != 0)
        {
            memcpy(built.extra, source->extra, (uint64_t)source->extra_count * sizeof(uint32_t));
        }
        if (source->constant_count != 0)
        {
            memcpy(built.constants, source->constants, (uint64_t)source->constant_count * sizeof(IrConstant));
        }
        if (source->slot_count != 0)
        {
            memcpy(built.slots, source->slots, (uint64_t)source->slot_count * sizeof(IrSlot));
        }
        if (source->address_count != 0)
        {
            memcpy(built.addresses, source->addresses, (uint64_t)source->address_count * sizeof(IrAddress));
        }
        if (source->parameter_count != 0)
        {
            memcpy(built.parameter_types, source->parameter_types, source->parameter_count);
        }
        uint32_t next = 0;
        for (uint32_t b = 0; b < source->block_count; b += 1)
        {
            const uint32_t old = variant == 1 && b != 0 ? source->block_count - b : b;
            built.block_offsets[b] = next;
            for (uint32_t row = source->block_offsets[old]; row < source->block_offsets[old + 1]; row += 1)
            {
                if (variant == 3 && b == 0 && row + 1 == source->block_offsets[old + 1])
                {
                    order[next++] = BUSTER_IR_NONE;
                    order[next++] = BUSTER_IR_NONE;
                }
                const uint32_t selected = row == swap ? row + 1 : swap != BUSTER_IR_NONE && row == swap + 1 ? row - 1 : row;
                order[next] = selected;
                values[selected] = next++;
            }
        }
        if (split != BUSTER_IR_NONE)
        {
            built.block_offsets[source->block_count] = next;
            order[next++] = BUSTER_IR_NONE;
        }
        if (dead_block)
        {
            built.block_offsets[source->block_count] = next;
            order[next++] = BUSTER_IR_NONE;
            order[next++] = BUSTER_IR_NONE;
        }
        built.block_offsets[built.block_count] = next;
        valid = next == built.instruction_count;
        for (uint32_t row = 0; valid && row < built.instruction_count; row += 1)
        {
            const uint32_t old = order[row];
            if (old != BUSTER_IR_NONE)
            {
                built.op[row] = source->op[old];
                built.type[row] = source->type[old];
                built.flags[row] = source->flags[old];
                built.alignment_log2[row] = source->alignment_log2[old];
                built.a[row] = source->a[old];
                built.b[row] = source->b[old];
                built.c[row] = source->c[old];
                masks[row] = (uint8_t)ir_meta_value_mask(source, old);
                if (variant == 5 && (built.op[row] == IR_ADD || built.op[row] == IR_MUL || built.op[row] == IR_AND ||
                                     built.op[row] == IR_OR || built.op[row] == IR_XOR))
                {
                    const uint32_t a = built.a[row];
                    built.a[row] = built.b[row];
                    built.b[row] = a;
                    *changed |= built.a[row] != built.b[row];
                }
            }
            else
            {
                built.flags[row] = 0;
                built.alignment_log2[row] = 0;
                if (variant == 3 || dead_block)
                {
                    const bool first = row + 1 < built.instruction_count && order[row + 1] == BUSTER_IR_NONE;
                    built.op[row] = first ? IR_CONST : dead_block ? IR_RET : IR_XOR;
                    built.type[row] = !first && dead_block ? IR_VOID : IR_I64;
                    built.a[row] = first ? source->constant_count : row - 1;
                    built.b[row] = first || dead_block ? BUSTER_IR_NONE : row - 1;
                    built.constants[source->constant_count] = (IrConstant){ .low = UINT64_C(0xa719e5) };
                    *changed = true;
                }
                else
                {
                    built.op[row] = IR_BR;
                    built.type[row] = IR_VOID;
                    built.a[row] = target;
                }
            }
        }
        ir_meta_remap_column(built.a, masks, 1, built.instruction_count, values, scalar);
        ir_meta_remap_column(built.b, masks, 2, built.instruction_count, values, scalar);
        ir_meta_remap_column(built.c, masks, 4, built.instruction_count, values, scalar);
        for (uint32_t row = 0; row < built.instruction_count; row += 1)
        {
            if (built.op[row] == IR_BR)
            {
                built.a[row] = split != BUSTER_IR_NONE && order[row] != BUSTER_IR_NONE &&
                               order[row] == source->block_offsets[split + 1] - 1 ? source->block_count : blocks[built.a[row]];
            }
            else if (built.op[row] == IR_CBR)
            {
                built.b[row] = blocks[built.b[row]];
                built.c[row] = blocks[built.c[row]];
            }
            else if (built.op[row] == IR_PHI)
            {
                const uint32_t old_block = order[row] != BUSTER_IR_NONE ? source->block_offsets[target != BUSTER_IR_NONE ? target : 0] : 0;
                for (uint32_t p = 0; p < built.b[row]; p += 1)
                {
                    const uint32_t at = built.a[row] + p * 2;
                    const uint32_t predecessor = source->extra[at];
                    const bool replaced = split != BUSTER_IR_NONE && predecessor == split && order[row] >= old_block &&
                                          order[row] < source->block_offsets[target + 1];
                    built.extra[at] = replaced ? source->block_count : blocks[predecessor];
                    built.extra[at + 1] = values[source->extra[at + 1]];
                }
            }
            else if (built.op[row] == IR_SWITCH)
            {
                const uint32_t at = built.b[row];
                built.extra[at] = blocks[source->extra[at]];
                for (uint32_t p = 0; p < built.c[row]; p += 1)
                {
                    built.extra[at + 2 + p * 2] = blocks[source->extra[at + 2 + p * 2]];
                }
            }
        }
        if (valid)
        {
            const IrCheck check = ir_verify_complete(&built, scratch, !scalar);
            failure = check.error == IR_OK ? IR_META_OK : check.error == IR_ERROR_MEMORY || check.error == IR_ERROR_LIMIT ? IR_META_RESOURCE : IR_META_TRANSFORM;
            valid = failure == IR_META_OK;
        }
    }
    if (valid)
    {
        *result = built;
    }
    else
    {
        arena_pop_to(output, mark);
        failure = failure == IR_META_OK ? IR_META_TRANSFORM : failure;
    }
    return failure;
}

static IrMetaFailure ir_meta_print(const IrFunction* function, Arena* text, Arena* scratch, String8* result)
{
    const uint64_t mark = text->length;
    FILE* file = tmpfile();
    const IrError printed = file != NULL ? ir_print(function, scratch, file) : IR_ERROR_IO;
    IrMetaFailure failure = printed == IR_OK ? IR_META_OK : printed == IR_ERROR_MEMORY || printed == IR_ERROR_LIMIT || printed == IR_ERROR_IO ?
                            IR_META_RESOURCE : IR_META_TEXT;
    bool valid = failure == IR_META_OK && fseek(file, 0, SEEK_END) == 0;
    const long length = valid ? ftell(file) : -1;
    valid = valid && length >= 0 && (uint64_t)length < BUSTER_IR_META_TEXT && fseek(file, 0, SEEK_SET) == 0;
    char* bytes = valid ? BUSTER_ARENA_PUSH_ARRAY(text, char, (uint64_t)length + 1) : NULL;
    valid = valid && bytes != NULL && fread(bytes, 1, (uint64_t)length, file) == (uint64_t)length && ferror(file) == 0;
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    if (valid)
    {
        bytes[length] = 0;
        *result = (String8){ .pointer = bytes, .length = (uint64_t)length };
    }
    else
    {
        arena_pop_to(text, mark);
        failure = failure == IR_META_OK ? IR_META_RESOURCE : failure;
    }
    return failure;
}

static bool ir_meta_same_text(String8 left, String8 right)
{
    const bool same = left.length == right.length && (left.length == 0 || memcmp(left.pointer, right.pointer, left.length) == 0);
    return same;
}

static IrMetaFailure ir_meta_validate(const IrFunction* function, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const IrCheck vector = ir_verify_complete(function, scratch, true);
    const IrCheck scalar = ir_verify_complete(function, scratch, false);
    const bool same = vector.error == scalar.error && vector.instruction == scalar.instruction && vector.detail == scalar.detail &&
                      vector.dominance_checked == scalar.dominance_checked && scratch->length == mark;
    const IrMetaFailure failure = !same ? IR_META_VERIFY : vector.error == IR_ERROR_MEMORY || vector.error == IR_ERROR_LIMIT ?
        IR_META_RESOURCE : vector.error != IR_OK ? IR_META_VERIFY : IR_META_OK;
    return failure;
}

static IrMetaFailure ir_meta_parse_pair(String8 text, Arena* output, Arena* twin, Arena* scratch, IrFunction* result,
                                       IrFunction* result_twin, IrMetaFailure invalid)
{
    const uint64_t output_mark = output->length;
    const uint64_t twin_mark = twin->length;
    const uint64_t scratch_mark = scratch->length;
    IrFunction parsed[2] = { 0 };
    const IrParseResult vector = ir_parse(text, output, scratch, parsed);
    const IrParseResult scalar = ir_parse_scalar(text, twin, scratch, parsed + 1);
    IrMetaFailure failure = vector.error == IR_PARSE_OK && scalar.error == IR_PARSE_OK && vector.consumed == text.length && scalar.consumed == text.length ?
                            IR_META_OK : vector.error == IR_PARSE_MEMORY || vector.error == IR_PARSE_LIMIT || scalar.error == IR_PARSE_MEMORY || scalar.error == IR_PARSE_LIMIT ?
                            IR_META_RESOURCE : invalid;
    failure = scratch->length == scratch_mark ? failure : IR_META_VERIFY;
    if (failure == IR_META_OK)
    {
        *result = parsed[0];
        *result_twin = parsed[1];
    }
    else
    {
        arena_pop_to(twin, twin_mark);
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    return failure;
}

static IrMetaFailure ir_meta_roundtrip(const IrFunction* function, Arena* text, Arena* output, Arena* twin, Arena* scratch, uint64_t* fingerprint)
{
    const uint64_t text_mark = text->length;
    const uint64_t output_mark = output->length;
    const uint64_t twin_mark = twin->length;
    String8 printed = { 0 };
    IrMetaFailure failure = ir_meta_print(function, text, scratch, &printed);
    IrFunction parsed[2] = { 0 };
    if (failure == IR_META_OK)
    {
        failure = ir_meta_parse_pair(printed, output, twin, scratch, parsed, parsed + 1, IR_META_TEXT);
    }
    for (uint32_t path = 0; failure == IR_META_OK && path < 2; path += 1)
    {
        String8 again = { 0 };
        failure = ir_meta_validate(parsed + path, scratch);
        if (failure == IR_META_OK)
        {
            failure = ir_meta_print(parsed + path, text, scratch, &again);
        }
        if (failure == IR_META_OK && !ir_meta_same_text(printed, again))
        {
            failure = IR_META_TEXT;
        }
    }
    if (failure == IR_META_OK && fingerprint != NULL)
    {
        *fingerprint = ir_meta_hash(*fingerprint, printed.pointer, printed.length);
    }
    arena_pop_to(twin, twin_mark);
    arena_pop_to(output, output_mark);
    arena_pop_to(text, text_mark);
    return failure;
}

static IrMetaObservation ir_meta_execute(const IrFunction* function, uint64_t argument, uint64_t max_steps, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint64_t memory[3] = { BUSTER_IR_META_CANARY, 0, ~BUSTER_IR_META_CANARY };
    const InterpreterBinding binding = { .symbol = 2, .bytes = (uint8_t*)(memory + 1), .size = 8, .writable = true };
    const IrInterpreterFunction entry = { .symbol = 1, .function = function };
    const IrInterpreterProgram program = { .functions = &entry, .function_count = 1, .bindings = &binding, .binding_count = 1 };
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = max_steps;
    options.max_frames = 4;
    options.max_values = BUSTER_IR_META_INSTRUCTIONS * 4;
    options.max_locals = 16;
    options.max_memories = 16;
    const InterpreterValue input = { .low = argument };
    IrMetaObservation result = { .execution = ir_interpret(&program, 1, &input, 1, &options, scratch) };
    result.memory = memory[1];
    result.guards = memory[0] == BUSTER_IR_META_CANARY && memory[2] == ~BUSTER_IR_META_CANARY && scratch->length == mark;
    return result;
}

static IrMetaFailure ir_meta_observation(const IrMetaObservation* actual, uint64_t expected)
{
    const InterpreterStatus status = actual->execution.status;
    IrMetaFailure failure = IR_META_OK;
    if (!actual->guards || status == INTERPRETER_MEMORY)
    {
        failure = IR_META_MEMORY;
    }
    else if (status == INTERPRETER_STEP_LIMIT || status == INTERPRETER_FRAME_LIMIT || status == INTERPRETER_VALUE_LIMIT)
    {
        failure = IR_META_STEPS;
    }
    else if (status == INTERPRETER_RESOURCE)
    {
        failure = IR_META_RESOURCE;
    }
    else if (status == INTERPRETER_NATIVE || status == INTERPRETER_SYMBOL)
    {
        failure = IR_META_UNSUPPORTED;
    }
    else if (status == INTERPRETER_UNDEFINED)
    {
        failure = IR_META_UNDEFINED;
    }
    else if (status != INTERPRETER_OK)
    {
        failure = IR_META_EXECUTION;
    }
    else if (actual->execution.value.low != expected || actual->execution.value.high != 0 || actual->memory != expected)
    {
        failure = IR_META_SEMANTICS;
    }
    return failure;
}

static IrMetaFailure ir_meta_optimize(const IrFunction* function, uint32_t pass, bool scalar, Arena* output, Arena* scratch, IrFunction* result)
{
    const uint64_t mark = output->length;
    IrCheck check = { .error = IR_ERROR_LIMIT };
    if (pass == 0)
    {
        *result = *function;
        check.error = IR_OK;
    }
    else if (pass <= 4)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.scalar = scalar;
        options.passes = pass == 1 ? BUSTER_IR_OPTIMIZE_SCCP : pass == 2 ? BUSTER_IR_OPTIMIZE_DCE :
                         pass == 3 ? BUSTER_IR_OPTIMIZE_CFG : BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG;
        IrOptimizeResult optimized = { 0 };
        check = ir_optimize(function, output, scratch, options, &optimized);
        if (check.error == IR_OK)
        {
            *result = optimized.function;
        }
    }
    else if (pass == 5)
    {
        IrGvnOptions options = ir_gvn_options();
        options.scalar = scalar;
        IrGvnResult optimized = { 0 };
        check = ir_optimize_gvn(function, output, scratch, options, &optimized);
        if (check.error == IR_OK)
        {
            *result = optimized.function;
        }
    }
    else if (pass == 6)
    {
        IrLicmOptions options = ir_licm_options();
        options.scalar = scalar;
        IrLicmResult optimized = { 0 };
        check = ir_optimize_licm(function, output, scratch, options, &optimized);
        if (check.error == IR_OK)
        {
            *result = optimized.function;
        }
    }
    else if (pass == 7)
    {
        const IrSroaOptions options = { .scalar = scalar };
        IrSroaResult optimized = { 0 };
        check = ir_optimize_sroa(function, output, scratch, options, &optimized);
        if (check.error == IR_OK)
        {
            *result = optimized.function;
        }
    }
    const IrMetaFailure failure = check.error == IR_OK ? IR_META_OK : check.error == IR_ERROR_MEMORY || check.error == IR_ERROR_LIMIT ? IR_META_RESOURCE : IR_META_OPTIMIZE;
    if (failure != IR_META_OK)
    {
        arena_pop_to(output, mark);
    }
    return failure;
}

typedef struct IrMetaFailureRecord IrMetaFailureRecord;
struct IrMetaFailureRecord
{
    IrMetaCase test;
    uint32_t variant;
    uint32_t pass;
    uint32_t scalar;
    uint64_t input;
    uint64_t expected;
    IrMetaFailure failure;
    IrMetaObservation actual;
    String8 source;
    const IrFunction* original;
    const IrFunction* transformed;
    IrFunction transformed_view;
    const IrFunction* variant_input;
    IrFunction variant_view;
};

static bool ir_meta_artifact(const IrMetaOptions* options, const IrMetaFailureRecord* failure, Arena* scratch);
static IrMetaFailure ir_meta_contracts(IrMetaTally* tally, Arena* source, Arena* output, Arena* scratch);
static bool ir_meta_replay_one(const char* directory, const char* name, const IrFunction* function, String8 raw, Arena* scratch);
static bool ir_meta_number(const char* text, uint64_t* result);

static IrMetaFailure ir_meta_campaign(const IrMetaOptions* options, IrMetaTally* tally)
{
    Arena original = arena_reserve(BUSTER_IR_META_ARENA);
    Arena transformed = arena_reserve(BUSTER_IR_META_ARENA);
    Arena twin = arena_reserve(BUSTER_IR_META_ARENA);
    Arena optimized = arena_reserve(BUSTER_IR_META_ARENA);
    Arena printed = arena_reserve(BUSTER_IR_META_ARENA);
    Arena parsed = arena_reserve(BUSTER_IR_META_ARENA);
    Arena parsed_twin = arena_reserve(BUSTER_IR_META_ARENA);
    Arena scratch = arena_reserve(BUSTER_IR_META_ARENA);
    IrMetaFailure failure = original.base != NULL && transformed.base != NULL && twin.base != NULL && optimized.base != NULL &&
                            printed.base != NULL && parsed.base != NULL && parsed_twin.base != NULL && scratch.base != NULL ? IR_META_OK : IR_META_RESOURCE;
    if (failure == IR_META_OK)
    {
        failure = ir_meta_contracts(tally, &original, &transformed, &scratch);
    }
    if (failure != IR_META_OK)
    {
        tally->failures[failure] += 1;
        fprintf(stderr, "ir-metamorphic: setup/contract %s\n", ir_meta_failure_names[failure]);
    }
    const uint64_t began = ir_meta_milliseconds();
    for (uint32_t number = 0; failure == IR_META_OK && number < options->count; number += 1)
    {
        original.length = 0;
        transformed.length = 0;
        twin.length = 0;
        IrMetaFailureRecord record = { .test = ir_meta_case(options->seed, options->first + number) };
        char source_text[BUSTER_IR_META_TEXT];
        IrFunction function = { 0 };
        IrFunction function_twin = { 0 };
        if (!ir_meta_generate(&record.test, source_text, sizeof(source_text), &record.source))
        {
            failure = IR_META_GENERATION;
        }
        else
        {
            failure = ir_meta_parse_pair(record.source, &original, &twin, &scratch, &function, &function_twin, IR_META_GENERATION);
        }
        record.original = failure == IR_META_OK ? &function : NULL;
        if (failure == IR_META_OK && options->replay != NULL && !ir_meta_replay_one(options->replay, "original.ir", &function, record.source, &scratch))
        {
            failure = IR_META_ARTIFACT;
        }
        tally->checks += 1;
        if (failure == IR_META_OK)
        {
            failure = ir_meta_roundtrip(&function, &printed, &parsed, &parsed_twin, &scratch, &tally->fingerprint);
            tally->checks += 1;
        }
        tally->cases += failure == IR_META_OK;
        for (uint32_t variant = 0; failure == IR_META_OK && variant < BUSTER_IR_META_VARIANTS; variant += 1)
        {
            transformed.length = 0;
            twin.length = 0;
            record.variant = variant;
            record.pass = 0;
            record.scalar = 0;
            record.input = 0;
            record.expected = ir_meta_model(&record.test, 0);
            record.actual = (IrMetaObservation){ 0 };
            IrFunction changed[2] = { 0 };
            bool alteration[2] = { 0 };
            const IrMetaFailure vector = ir_meta_transform(&function, variant, false, &transformed, &scratch, changed, alteration);
            const IrMetaFailure scalar = ir_meta_transform(&function, variant, true, &twin, &scratch, changed + 1, alteration + 1);
            failure = vector != IR_META_OK ? vector : scalar != IR_META_OK ? scalar : alteration[0] == alteration[1] ? IR_META_OK : IR_META_TRANSFORM;
            record.transformed_view = changed[0];
            record.transformed = vector == IR_META_OK ? &record.transformed_view : NULL;
            record.variant_view = changed[0];
            record.variant_input = vector == IR_META_OK ? &record.variant_view : NULL;
            String8 texts[2] = { 0 };
            for (uint32_t path = 0; failure == IR_META_OK && path < 2; path += 1)
            {
                failure = ir_meta_print(changed + path, &printed, &scratch, texts + path);
            }
            if (failure == IR_META_OK && !ir_meta_same_text(texts[0], texts[1]))
            {
                failure = IR_META_TRANSFORM;
            }
            if (failure == IR_META_OK)
            {
                tally->fingerprint = ir_meta_hash(tally->fingerprint, texts[0].pointer, texts[0].length);
            }
            printed.length = 0;
            tally->checks += 1;
            tally->variants += failure == IR_META_OK;
            tally->changed += alteration[0];
            for (uint32_t pass = 0; failure == IR_META_OK && pass < BUSTER_IR_META_PASSES; pass += 1)
            {
                record.pass = pass;
                for (uint32_t scalar_path = 0; failure == IR_META_OK && scalar_path < 2; scalar_path += 1)
                {
                    optimized.length = 0;
                    IrFunction candidate = { 0 };
                    record.scalar = scalar_path;
                    record.variant_view = changed[scalar_path];
                    record.variant_input = &record.variant_view;
                    record.transformed_view = changed[scalar_path];
                    record.transformed = &record.transformed_view;
                    record.input = 0;
                    record.expected = ir_meta_model(&record.test, 0);
                    record.actual = (IrMetaObservation){ 0 };
                    failure = ir_meta_optimize(changed + scalar_path, pass, scalar_path != 0, &optimized, &scratch, &candidate);
                    tally->checks += 1;
                    // Explicit fault injection exercises the failure/artifact protocol. It is recorded in replay
                    // metadata and never enabled by the default campaign or interpreted as an organic compiler bug.
                    bool injecting = failure == IR_META_OK && options->inject != 0 && variant == 0 && pass == 0 && scalar_path == 0;
                    if (injecting)
                    {
                        IrFunction private_candidate = { 0 };
                        bool altered = false;
                        failure = ir_meta_transform(&candidate, 0, scalar_path != 0, &optimized, &scratch, &private_candidate, &altered);
                        injecting = failure == IR_META_OK;
                        if (injecting)
                        {
                            candidate = private_candidate;
                        }
                    }
                    if (injecting && options->inject == IR_META_SEMANTICS && candidate.constant_count != 0)
                    {
                        candidate.constants[0].low ^= 1;
                    }
                    if (injecting && options->inject == IR_META_VERIFY)
                    {
                        for (uint32_t row = 0; row < candidate.instruction_count; row += 1)
                        {
                            if (candidate.op[row] == IR_ADD)
                            {
                                candidate.a[row] = candidate.instruction_count;
                                break;
                            }
                        }
                    }
                    if (failure == IR_META_OK)
                    {
                        record.transformed_view = candidate;
                        record.transformed = &record.transformed_view;
                        failure = ir_meta_validate(&candidate, &scratch);
                        tally->checks += 1;
                    }
                    if (failure == IR_META_OK)
                    {
                        failure = ir_meta_roundtrip(&candidate, &printed, &parsed, &parsed_twin, &scratch, NULL);
                        tally->checks += 1;
                    }
                    static const uint64_t inputs[] = { 0, 1, 2, 3, 15, UINT64_MAX, UINT64_C(0x8000000000000000) };
                    for (uint32_t i = 0; failure == IR_META_OK && i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
                    {
                        record.input = inputs[i];
                        record.expected = ir_meta_model(&record.test, record.input);
                        const uint64_t steps = injecting && options->inject == IR_META_STEPS ? 1 : options->max_steps;
                        record.actual = ir_meta_execute(&candidate, record.input, steps, &scratch);
                        failure = ir_meta_observation(&record.actual, record.expected);
                        tally->checks += 1;
                        tally->executions += 1;
                    }
                    if (failure == IR_META_OK && ir_meta_milliseconds() - began >= options->max_ms)
                    {
                        failure = IR_META_TIME;
                    }
                }
            }
        }
        if (failure != IR_META_OK)
        {
            record.failure = failure;
            tally->failures[failure] += 1;
            fprintf(stderr, "ir-metamorphic: %s seed=%" PRIu64 " case=%u shape=%u variant=%u pass=%u scalar=%u input=%" PRIu64
                            " expected=%016" PRIx64 " actual=%016" PRIx64 "%016" PRIx64 " memory=%016" PRIx64 " status=%u\n",
                    ir_meta_failure_names[failure], record.test.seed, record.test.number, record.test.shape, record.variant, record.pass, record.scalar,
                    record.input, record.expected, record.actual.execution.value.high, record.actual.execution.value.low, record.actual.memory, record.actual.execution.status);
            if (!ir_meta_artifact(options, &record, &scratch))
            {
                tally->failures[IR_META_ARTIFACT] += 1;
            }
            if (options->replay != NULL && (record.failure != options->replay_failure || record.variant != options->replay_variant ||
                record.pass != options->replay_pass || record.scalar != options->replay_scalar || record.input != options->replay_input))
            {
                fprintf(stderr, "ir-metamorphic: replay changed failure category or semantic consumer location\n");
                failure = IR_META_ARTIFACT;
                tally->failures[IR_META_ARTIFACT] += 1;
            }
            else if (options->replay != NULL && (!ir_meta_replay_one(options->replay, "transformed.ir", record.transformed, (String8){ 0 }, &scratch) ||
                     !ir_meta_replay_one(options->replay, "variant.ir", record.variant_input, (String8){ 0 }, &scratch)))
            {
                fprintf(stderr, "ir-metamorphic: replay changed the retained consumer input/output\n");
                failure = IR_META_ARTIFACT;
                tally->failures[IR_META_ARTIFACT] += 1;
            }
        }
    }
    arena_release(&scratch);
    arena_release(&parsed_twin);
    arena_release(&parsed);
    arena_release(&printed);
    arena_release(&optimized);
    arena_release(&twin);
    arena_release(&transformed);
    arena_release(&original);
    return failure;
}

static bool ir_meta_artifact_directory(char* directory, uint64_t capacity, const char* root, const IrMetaFailureRecord* record)
{
    const int length = snprintf(directory, capacity, "%s/seed-%016" PRIx64 "-case-%010u-variant-%u-pass-%u-scalar-%u-%s", root,
                                record->test.seed, record->test.number, record->variant, record->pass, record->scalar,
                                ir_meta_failure_names[record->failure]);
    const bool valid = length > 0 && (uint64_t)length < capacity;
    return valid;
}

static bool ir_meta_render_ir(FILE* file, const IrFunction* function, String8 raw, Arena* scratch)
{
    bool valid = file != NULL;
    if (valid)
    {
        const IrError printed = function != NULL ? ir_print(function, scratch, file) : IR_ERROR_STRUCTURE;
        if (printed != IR_OK)
        {
            fprintf(file, "; view was not published as valid canonical IR; regenerate with the recorded seed and tool revision\n");
            if (raw.length != 0)
            {
                valid = fwrite(raw.pointer, 1, raw.length, file) == raw.length;
            }
        }
        valid = !ferror(file) && valid;
    }
    return valid;
}

static bool ir_meta_write_ir(const char* path, const IrFunction* function, String8 raw, Arena* scratch)
{
    FILE* file = fopen(path, "wx");
    bool valid = ir_meta_render_ir(file, function, raw, scratch);
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool ir_meta_replay_one(const char* directory, const char* name, const IrFunction* function, String8 raw, Arena* scratch)
{
    char path[PATH_MAX];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* expected = length > 0 && (uint64_t)length < sizeof(path) ? fopen(path, "r") : NULL;
    FILE* actual = tmpfile();
    bool same = expected != NULL && actual != NULL && ir_meta_render_ir(actual, function, raw, scratch) && fseek(actual, 0, SEEK_SET) == 0;
    bool done = false;
    while (same && !done)
    {
        const int a = fgetc(actual);
        const int b = fgetc(expected);
        same = a == b;
        done = a == EOF || b == EOF;
    }
    same = same && ferror(actual) == 0 && ferror(expected) == 0;
    if (expected != NULL)
    {
        same = fclose(expected) == 0 && same;
    }
    if (actual != NULL)
    {
        same = fclose(actual) == 0 && same;
    }
    return same;
}

static void ir_meta_quote(FILE* file, const char* text)
{
    fputc('\'', file);
    for (const char* at = text; *at != 0; at += 1)
    {
        if (*at == '\'')
        {
            fputs("'\\''", file);
        }
        else
        {
            fputc(*at, file);
        }
    }
    fputc('\'', file);
}

static bool ir_meta_binary(uint64_t* hash, uint64_t* size)
{
    FILE* file = fopen("/proc/self/exe", "rb");
    bool valid = file != NULL;
    *hash = UINT64_C(14695981039346656037);
    *size = 0;
    uint8_t buffer[4096];
    while (valid && !feof(file))
    {
        const uint64_t read = fread(buffer, 1, sizeof(buffer), file);
        valid = !ferror(file) && read <= UINT64_MAX - *size;
        if (valid)
        {
            *hash = ir_meta_hash(*hash, buffer, read);
            *size += read;
        }
    }
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool ir_meta_artifact(const IrMetaOptions* options, const IrMetaFailureRecord* record, Arena* scratch)
{
    bool valid = true;
    if (options->artifacts == NULL && !options->silent)
    {
        fprintf(stderr, "ir-metamorphic: replay ./build/compiler test-ir-metamorphic --seed %" PRIu64 " --first %u --count 1 --max-steps %" PRIu64 " --max-ms %" PRIu64,
                record->test.seed, record->test.number, options->max_steps, options->max_ms);
        if (options->inject != 0)
        {
            fprintf(stderr, " --inject %s", ir_meta_failure_names[options->inject]);
        }
        fputc('\n', stderr);
        if (record->source.length != 0)
        {
            fprintf(stderr, "--- generated input (version%u) ---\n%.*s", BUSTER_IR_META_VERSION, (int)record->source.length, record->source.pointer);
        }
        if (record->transformed != NULL)
        {
            fputs("--- failing consumer input/output ---\n", stderr);
            valid = ir_meta_render_ir(stderr, record->transformed, (String8){ 0 }, scratch);
        }
    }
    else if (options->artifacts != NULL)
    {
        char directory[PATH_MAX];
        valid = ir_meta_artifact_directory(directory, sizeof(directory), options->artifacts, record);
        if (valid)
        {
            valid = mkdir(options->artifacts, 0700) == 0 || errno == EEXIST;
            valid = valid && mkdir(directory, 0700) == 0;
        }
        char path[PATH_MAX];
        const int original = valid ? snprintf(path, sizeof(path), "%s/original.ir", directory) : -1;
        valid = valid && original > 0 && (uint64_t)original < sizeof(path) && ir_meta_write_ir(path, record->original, record->source, scratch);
        const int transformed = valid ? snprintf(path, sizeof(path), "%s/transformed.ir", directory) : -1;
        valid = valid && transformed > 0 && (uint64_t)transformed < sizeof(path) && ir_meta_write_ir(path, record->transformed, (String8){ 0 }, scratch);
        const int variant = valid ? snprintf(path, sizeof(path), "%s/variant.ir", directory) : -1;
        valid = valid && variant > 0 && (uint64_t)variant < sizeof(path) && ir_meta_write_ir(path, record->variant_input, (String8){ 0 }, scratch);
        const int metadata = valid ? snprintf(path, sizeof(path), "%s/run.txt", directory) : -1;
        FILE* file = valid && metadata > 0 && (uint64_t)metadata < sizeof(path) ? fopen(path, "wx") : NULL;
        valid = valid && file != NULL;
        if (file != NULL)
        {
            uint64_t binary_hash = 0;
            uint64_t binary_size = 0;
            valid = ir_meta_binary(&binary_hash, &binary_size) && valid;
            fprintf(file, "ir-metamorphic %u\nseed %" PRIu64 "\ncase %u\nvariant %u\npass %u\nscalar %u\nfailure %u\ninput %" PRIu64 "\nmax-steps %" PRIu64 "\nmax-ms %" PRIu64 "\ninject %u\n",
                    BUSTER_IR_META_VERSION, record->test.seed, record->test.number, record->variant, record->pass, record->scalar, record->failure,
                    record->input, options->max_steps, options->max_ms, options->inject);
            fprintf(file, "failure-name %s\nexpected %016" PRIx64 "\nactual %016" PRIx64 "%016" PRIx64 "\nmemory %016" PRIx64 "\nstatus %u\nsteps %" PRIu64 "\nguards %u\n",
                    ir_meta_failure_names[record->failure], record->expected, record->actual.execution.value.high, record->actual.execution.value.low,
                    record->actual.memory, record->actual.execution.status, record->actual.execution.steps, record->actual.guards);
            fprintf(file, "source-revision %s\ntool-fnv64 %016" PRIx64 "\ntool-bytes %" PRIu64 "\nhost-compiler %s\n",
                    options->revision != NULL ? options->revision : "unrecorded", binary_hash, binary_size, __VERSION__);
            fputs("replay ./build/compiler test-ir-metamorphic --replay ", file);
            ir_meta_quote(file, directory);
            fputc('\n', file);
            valid = !ferror(file) && valid;
            valid = fclose(file) == 0 && valid;
        }
        if (valid && !options->silent)
        {
            fprintf(stderr, "ir-metamorphic: retained %s\n", directory);
        }
        else if (!valid && !options->silent)
        {
            fprintf(stderr, "ir-metamorphic: artifact creation failed; existing paths are never replaced\n");
        }
    }
    return valid;
}

static bool ir_meta_load_replay(const char* directory, IrMetaOptions* options)
{
    char path[PATH_MAX];
    const int length = snprintf(path, sizeof(path), "%s/run.txt", directory);
    FILE* file = length > 0 && (uint64_t)length < sizeof(path) ? fopen(path, "r") : NULL;
    bool valid = file != NULL;
    static const char* const names[] =
    {
        "ir-metamorphic", "seed", "case", "variant", "pass", "scalar", "failure", "input", "max-steps", "max-ms", "inject",
    };
    uint64_t values[BUSTER_ARRAY_LENGTH(names)] = { 0 };
    for (uint32_t field = 0; valid && field < BUSTER_ARRAY_LENGTH(names); field += 1)
    {
        char line[256];
        valid = fgets(line, sizeof(line), file) != NULL;
        const uint64_t size = valid ? strlen(line) : 0;
        const uint64_t prefix = strlen(names[field]);
        valid = valid && size > prefix + 2 && line[size - 1] == '\n' && memcmp(line, names[field], prefix) == 0 && line[prefix] == ' ';
        if (valid)
        {
            line[size - 1] = 0;
            valid = ir_meta_number(line + prefix + 1, values + field);
        }
    }
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    valid = valid && values[0] == BUSTER_IR_META_VERSION && values[2] < UINT32_MAX && values[3] < BUSTER_IR_META_VARIANTS &&
            values[4] < BUSTER_IR_META_PASSES && values[5] < 2 && values[6] > IR_META_OK && values[6] < IR_META_FAILURE_COUNT &&
            values[8] != 0 && values[8] <= UINT64_C(1000000000) && values[9] != 0 && values[9] <= 600000 &&
            (values[10] == 0 || values[10] == IR_META_SEMANTICS || values[10] == IR_META_VERIFY || values[10] == IR_META_STEPS);
    if (valid)
    {
        options->seed = values[1];
        options->first = (uint32_t)values[2];
        options->replay_variant = (uint32_t)values[3];
        options->replay_pass = (uint32_t)values[4];
        options->replay_scalar = (uint32_t)values[5];
        options->replay_failure = (uint32_t)values[6];
        options->replay_input = values[7];
        options->max_steps = values[8];
        options->max_ms = values[9];
        options->inject = (uint32_t)values[10];
        options->count = 1;
        options->replay = directory;
    }
    return valid;
}

static bool ir_meta_number(const char* text, uint64_t* result)
{
    errno = 0;
    char* end = NULL;
    const unsigned long long value = strtoull(text, &end, 0);
    const bool valid = text[0] >= '0' && text[0] <= '9' && errno == 0 && end != NULL && *end == 0;
    if (valid)
    {
        *result = (uint64_t)value;
    }
    return valid;
}

static bool ir_meta_revision(const char* revision)
{
    bool valid = revision != NULL && revision[0] != 0;
    for (uint64_t at = 0; valid && revision[at] != 0; at += 1)
    {
        const uint8_t byte = (uint8_t)revision[at];
        valid = byte >= 32 && byte != 127 && at < 255;
    }
    return valid;
}

#include "ir_metamorphic_test.c"

static __attribute__((unused)) int run_ir_metamorphic_tests(int argc, char** argv)
{
    IrMetaOptions options = { .seed = 731, .count = 6, .max_steps = 100000, .max_ms = 30000, .revision = getenv("GITHUB_SHA") };
    bool valid = true;
    const char* replay = NULL;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool numeric = strcmp(argument, "--seed") == 0 || strcmp(argument, "--count") == 0 || strcmp(argument, "--first") == 0 ||
                             strcmp(argument, "--max-steps") == 0 || strcmp(argument, "--max-ms") == 0;
        const bool textual = strcmp(argument, "--artifacts") == 0 || strcmp(argument, "--revision") == 0 || strcmp(argument, "--replay") == 0 ||
                             strcmp(argument, "--inject") == 0;
        valid = (numeric || textual) && i + 1 < argc;
        if (valid)
        {
            const char* value = argv[++i];
            uint64_t number = 0;
            valid = !numeric || ir_meta_number(value, &number);
            if (valid)
            {
                if (strcmp(argument, "--seed") == 0)
                {
                    options.seed = number;
                }
                else if (strcmp(argument, "--count") == 0)
                {
                    valid = number >= 1 && number <= 100000;
                    options.count = (uint32_t)number;
                }
                else if (strcmp(argument, "--first") == 0)
                {
                    valid = number < UINT32_MAX;
                    options.first = (uint32_t)number;
                }
                else if (strcmp(argument, "--max-steps") == 0)
                {
                    valid = number >= 1 && number <= UINT64_C(1000000000);
                    options.max_steps = number;
                }
                else if (strcmp(argument, "--max-ms") == 0)
                {
                    valid = number >= 1 && number <= 600000;
                    options.max_ms = number;
                }
                else if (strcmp(argument, "--artifacts") == 0)
                {
                    options.artifacts = value;
                }
                else if (strcmp(argument, "--revision") == 0)
                {
                    options.revision = value;
                }
                else if (strcmp(argument, "--inject") == 0)
                {
                    valid = strcmp(value, "semantics") == 0 || strcmp(value, "verify") == 0 || strcmp(value, "step-limit") == 0;
                    options.inject = strcmp(value, "semantics") == 0 ? IR_META_SEMANTICS : strcmp(value, "verify") == 0 ? IR_META_VERIFY : IR_META_STEPS;
                }
                else
                {
                    replay = value;
                }
            }
        }
    }
    valid = valid && (uint64_t)options.first + options.count <= UINT32_MAX &&
            (options.artifacts == NULL || (options.artifacts[0] != 0 && ir_meta_revision(options.revision))) &&
            (replay == NULL || replay[0] != 0);
    valid = valid && (replay == NULL || ir_meta_load_replay(replay, &options));
    int result = 2;
    if (!valid)
    {
        fputs("usage: test-ir-metamorphic [--seed N] [--first N] [--count N] [--max-steps N] [--max-ms N] "
              "[--artifacts DIRECTORY --revision SOURCE_REVISION] [--replay ARTIFACT_DIRECTORY] [--inject semantics|verify|step-limit]\n", stderr);
    }
    else
    {
        IrMetaTally tally = { .fingerprint = UINT64_C(14695981039346656037) };
        const IrMetaFailure failure = ir_meta_campaign(&options, &tally);
        printf("ir-metamorphic: version%u seed=%" PRIu64 " cases=%" PRIu64 " variants=%" PRIu64 " changed=%" PRIu64
               " executions=%" PRIu64 " negatives=%" PRIu64 " checks=%" PRIu64 " fingerprint=%016" PRIx64 " injected=%s status=%s\n",
               BUSTER_IR_META_VERSION, options.seed, tally.cases, tally.variants, tally.changed, tally.executions, tally.negative,
               tally.checks, tally.fingerprint, options.inject != 0 ? ir_meta_failure_names[options.inject] : "none", ir_meta_failure_names[failure]);
        result = failure == IR_META_OK ? 0 : tally.failures[IR_META_ARTIFACT] != 0 || failure == IR_META_STEPS || failure == IR_META_TIME ||
                 failure == IR_META_UNSUPPORTED || failure == IR_META_RESOURCE ? 2 : 1;
    }
    return result;
}
