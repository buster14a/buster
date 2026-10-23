// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent #148 atomic RMW consumer harness. Requires the immutable composition of
// frozen RMW c47c23293d0e1a307dee34a8a69119d58e692fb7 and direct-CALL backend 73e95edbb2d6acf7645c2d15c7f8bc584333f273.
// Frozen leaf-module SHA256: b0f5a79fb49853f6fdc30a64c09960e11e781f5f664f70a224bc1a629d8bff1d.
// This file is original C; it borrows production interfaces and actual RMW leaf emitters, not tests.
// Signature-only helper declarations feed the existing ABI batch; they have no mock executable typed bodies.
// Every CALLER traverses typed lowering, SSA promotion, SCCP/DCE/CFG optimization, dominance verification,
// selection, liveness/allocation/frame emission, branch sizing, both encoders and real symbolic CALL fixups.
// The helpers retain their original native record implementations. Source dispatch/intrinsic lowering and
// protected integration remain separate acceptance work. Each caller has a discarded first call, a second used
// call and nonconstant arguments/seed live across clobbers. Every native image is separately encoded and executed
// by each encoder, with one real callee declaration/body per fixture and actual callee-symbol rel32 resolution.
// All 30 IDs and 140 supported width/format combinations run. A six-parameter caller computes a live XOR seed
// before two calls. Non-CAS RMW first operands toggle their low bit, preserving canonical BOOL values while
// independently checking old/new return distinctions. CAS uses the original fixed arguments on both calls.
//
// Compile on the approved 9700X under its normal lock. From the directory containing this file and the immutable
// builtin-tier1-acceptance checkout, the exact relative command is:
// clang -O3 -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function -I./builtin-tier1-acceptance/src ./helper-tier1-atomic-rmw-148.c -pthread -o ./helper-tier1-atomic-rmw-148
// -Wno-unused-function permits unused static production entry points in this standalone inclusion unit.
// Exact host Clang 23.1.1 __atomic/__sync calls are independent oracles. Typed reference objects avoid conflicting
// declared access types. Native object/expected slices have exact guarded widths; GNU CAS also exercises aliases
// and read-only successful expected values. A finite two-thread actual Tier1 fetch-add consumer checks all old
// values and the final count. All native work stays in a signal-bounded child. No memory-model proof, source
// acceptance, benchmark, mock helper body or runtime dispatch to another compiler tier is claimed.

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <immintrin.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/dataflow.c"
#include "buster/compiler/dominators.c"
#include "buster/compiler/ssa.c"
#include "buster/compiler/tier1_allocate.c"
#include "buster/compiler/tier1_rematerialize.c"
#include "buster/compiler/link.c"
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_target_forms.c"
#include "buster/compiler/x86_zen5.c"
#include "buster/compiler/c_types.c"
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/tier1_frame.c"
#include "buster/compiler/tier1_frame_emit.c"
#include "buster/compiler/tier0.c"
#include "buster/compiler/tier0_generate.c"
#include "buster/compiler/c_builtin_catalog.c"
#include "buster/compiler/c_builtin_atomic_rmw_helpers.c"
#include "buster/compiler/ir.c"
#include "buster/compiler/ir_lower.c"
#include "buster/compiler/ssa_ir.c"
#include "buster/compiler/ir_optimize.c"
#include "buster/compiler/tier1_select.c"
#include "buster/compiler/tier1_liveness.c"
#include "buster/compiler/tier1_emit.c"
#include "buster/compiler/branch_sizing.c"

#define BUSTER_HELPER148_HELPERS 1u
#define BUSTER_HELPER148_FIXTURES 140u
#define BUSTER_HELPER148_ENTRIES 64u
#define BUSTER_HELPER148_CALLS 2u
#define BUSTER_HELPER148_ARGUMENTS 16u
#define BUSTER_HELPER148_PAGE 4096u
#define BUSTER_HELPER148_ARENAS 8u
#define BUSTER_HELPER148_CAS 0x01u
#define BUSTER_HELPER148_GNU 0x02u
#define BUSTER_HELPER148_BOOL 0x04u
#define BUSTER_HELPER148_RELEASE 0x08u
#define BUSTER_HELPER148_THREADS 2u
#define BUSTER_HELPER148_ROUNDS 128u

typedef struct Helper148Leaf Helper148Leaf;
struct Helper148Leaf
{
    SymbolId symbol;
    CBuiltinId id;
    uint32_t flags;
    uint32_t bytes;
    CBuiltinAtomicRmwFormat format;
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    uint8_t parameter_classes[6];
};

typedef struct Helper148Fixture Helper148Fixture;
struct Helper148Fixture
{
    Tier0Program program;
    uint8_t kinds[BUSTER_HELPER148_ENTRIES];
    uint8_t classes[BUSTER_HELPER148_ENTRIES];
    uint64_t values[BUSTER_HELPER148_ENTRIES];
    uint8_t local_classes[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint8_t local_flags[BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t call_callees[BUSTER_HELPER148_CALLS];
    uint32_t call_first[BUSTER_HELPER148_CALLS];
    uint32_t call_argument_counts[BUSTER_HELPER148_CALLS];
    uint8_t argument_classes[BUSTER_HELPER148_ARGUMENTS];
};

typedef struct Helper148Native Helper148Native;
struct Helper148Native
{
    uint8_t* mapping[2];
    uint8_t* entry[2];
    uint64_t mapping_bytes;
};

typedef struct Helper148Spec Helper148Spec;
struct Helper148Spec
{
    CBuiltinId id;
    uint32_t flags;
    uint32_t parameters;
};

typedef struct Helper148Case Helper148Case;
struct Helper148Case
{
    CBuiltinId id;
    uint32_t flags;
    uint32_t bytes;
    CBuiltinAtomicRmwFormat format;
    Helper148Native programs[2];
};

typedef struct Helper148Report Helper148Report;
struct Helper148Report
{
    uint64_t checks;
    uint64_t failures;
    uint64_t native_results;
    uint64_t calls;
};

typedef struct Helper148Context Helper148Context;
struct Helper148Context
{
    Helper148Report report;
    Tier0Tables tables;
    Arena leaves;
    Arena arenas[BUSTER_HELPER148_ARENAS];
    Helper148Leaf helpers[BUSTER_HELPER148_HELPERS];
    uint8_t allowed[X86_FORM_COUNT];
};

static const Helper148Spec helper148_specs[] =
{
    { CBUILTIN_ATOMIC_EXCHANGE_N, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL | BUSTER_HELPER148_CAS, 6 },
    { CBUILTIN_ATOMIC_FETCH_ADD, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_ATOMIC_FETCH_SUB, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_ATOMIC_FETCH_AND, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_FETCH_OR, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_FETCH_XOR, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_FETCH_NAND, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_ATOMIC_ADD_FETCH, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_ATOMIC_SUB_FETCH, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_ATOMIC_AND_FETCH, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_OR_FETCH, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_XOR_FETCH, BUSTER_HELPER148_GNU | BUSTER_HELPER148_BOOL, 3 },
    { CBUILTIN_ATOMIC_NAND_FETCH, BUSTER_HELPER148_GNU, 3 },
    { CBUILTIN_SYNC_FETCH_AND_ADD, 0, 2 },
    { CBUILTIN_SYNC_FETCH_AND_SUB, 0, 2 },
    { CBUILTIN_SYNC_FETCH_AND_AND, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_FETCH_AND_OR, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_FETCH_AND_XOR, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_FETCH_AND_NAND, 0, 2 },
    { CBUILTIN_SYNC_ADD_AND_FETCH, 0, 2 },
    { CBUILTIN_SYNC_SUB_AND_FETCH, 0, 2 },
    { CBUILTIN_SYNC_AND_AND_FETCH, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_OR_AND_FETCH, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_XOR_AND_FETCH, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_NAND_AND_FETCH, 0, 2 },
    { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, BUSTER_HELPER148_BOOL | BUSTER_HELPER148_CAS, 3 },
    { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, BUSTER_HELPER148_BOOL | BUSTER_HELPER148_CAS, 3 },
    { CBUILTIN_SYNC_LOCK_TEST_AND_SET, BUSTER_HELPER148_BOOL, 2 },
    { CBUILTIN_SYNC_LOCK_RELEASE, BUSTER_HELPER148_BOOL | BUSTER_HELPER148_RELEASE, 1 },
};

static bool helper148_expect(Helper148Report* report, bool condition, const char* message, uint32_t item)
{
    report->checks += 1;
    if (!condition)
    {
        if (report->failures < 24)
        {
            fprintf(stderr, "helper-tier1-atomic-rmw-148: %s (item=%u)\n", message, item);
        }
        report->failures += 1;
    }
    return condition;
}

static IrType helper148_ir_class(uint32_t class)
{
    const IrType result = class == BUSTER_TYPED_CLASS_VOID ? IR_VOID : class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_I32 ? IR_I32 : IR_I64;
    return result;
}

static const Helper148Leaf* helper148_symbol(const Helper148Context* context, SymbolId symbol)
{
    const Helper148Leaf* result = NULL;
    for (uint32_t i = 0; i < BUSTER_HELPER148_HELPERS; i += 1)
    {
        if (context->helpers[i].symbol == symbol)
        {
            result = context->helpers + i;
        }
    }
    return result;
}

static bool helper148_leaf(Helper148Context* context, const Helper148Spec* spec, uint32_t bytes, CBuiltinAtomicRmwFormat format, uint32_t item)
{
    Helper148Leaf* leaf = context->helpers;
    memset(leaf, 0, sizeof(*leaf));
    leaf->symbol = 1;
    leaf->id = spec->id;
    leaf->flags = spec->flags;
    leaf->bytes = bytes;
    leaf->format = format;
    const CBuiltinAtomicRmwHelper emitted = cbuiltin_atomic_rmw_helper_emit(spec->id, bytes, format, &context->leaves);
    leaf->code = emitted.code;
    leaf->parameter_count = emitted.parameter_count;
    leaf->result_class = emitted.result_class;
    memcpy(leaf->parameter_classes, emitted.parameter_classes, sizeof(leaf->parameter_classes));
    const uint32_t result_class = (spec->flags & BUSTER_HELPER148_RELEASE) != 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
    bool ready = helper148_expect(&context->report, emitted.parameter_count == spec->parameters && emitted.result_class == result_class &&
        emitted.compiler_barrier && emitted.required_alignment == bytes, "actual RMW raw-bit signature, alignment and compiler barrier", item);
    for (uint32_t p = 0; p < spec->parameters; p += 1)
    {
        const uint32_t first_integer = spec->id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? 3 : 2;
        const uint32_t expected = (spec->flags & BUSTER_HELPER148_GNU) != 0 && p >= first_integer ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
        ready &= helper148_expect(&context->report, emitted.parameter_classes[p] == expected, "independent fixed parameter machine classes", item);
    }
    ready &= helper148_expect(&context->report, leaf->code.error == TIER0_OK && leaf->code.count != 0 &&
        leaf->code.count <= BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS && leaf->code.branch_count <= 1 &&
        leaf->code.reference_count == 0 && x86_target_validate(context->allowed, leaf->code.records, leaf->code.count) == leaf->code.count,
        "actual original RMW leaf validates at baseline", item);
    return ready;
}

static void helper148_call(Helper148Fixture* fixture, const Helper148Leaf* leaves, uint32_t helper, const uint32_t* locals)
{
    Tier0Program* program = &fixture->program;
    const uint32_t call = program->call_count;
    const Helper148Leaf* leaf = leaves + helper;
    assert(call < BUSTER_HELPER148_CALLS && program->argument_count + leaf->parameter_count <= BUSTER_HELPER148_ARGUMENTS);
    fixture->call_callees[call] = helper;
    fixture->call_first[call] = program->argument_count;
    fixture->call_argument_counts[call] = leaf->parameter_count;
    for (uint32_t argument = 0; argument < leaf->parameter_count; argument += 1)
    {
        fixture->argument_classes[program->argument_count++] = leaf->parameter_classes[argument];
        tier0_emit(program, BUSTER_TYPED_LOCAL, leaf->parameter_classes[argument], locals[argument]);
    }
    tier0_emit(program, BUSTER_TYPED_CALL, leaf->result_class, call);
    program->call_count += 1;
}

static void helper148_fixture(Helper148Fixture* fixture, const Helper148Leaf* leaves, uint32_t kind)
{
    memset(fixture, 0, sizeof(*fixture));
    Tier0Program* program = &fixture->program;
    *program = (Tier0Program)
    {
        .kinds = fixture->kinds,
        .classes = fixture->classes,
        .values = fixture->values,
        .entry_capacity = BUSTER_HELPER148_ENTRIES,
        .local_classes = fixture->local_classes,
        .local_flags = fixture->local_flags,
        .local_capacity = BUSTER_TIER0_GENERATE_MAX_LOCALS,
        .call_callees = fixture->call_callees,
        .call_first = fixture->call_first,
        .call_argument_counts = fixture->call_argument_counts,
        .call_capacity = BUSTER_HELPER148_CALLS,
        .argument_classes = fixture->argument_classes,
        .argument_capacity = BUSTER_HELPER148_ARGUMENTS,
        .function_count = BUSTER_HELPER148_HELPERS + 1,
        .defined = true,
    };
    for (uint32_t i = 0; i < BUSTER_HELPER148_HELPERS; i += 1)
    {
        program->functions[i] = (Tier0GeneratedFunction)
        {
            .first_local = program->local_count,
            .local_count = leaves[i].parameter_count,
            .parameter_count = leaves[i].parameter_count,
            .result_class = (uint8_t)leaves[i].result_class,
        };
        memcpy(fixture->local_classes + program->local_count, leaves[i].parameter_classes, leaves[i].parameter_count);
        program->local_count += leaves[i].parameter_count;
    }
    const uint32_t first_local = program->local_count;
    const uint32_t parameters = 6;
    uint8_t locals[8];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(locals); i += 1)
    {
        locals[i] = i < leaves[0].parameter_count ? leaves[0].parameter_classes[i] : BUSTER_TYPED_CLASS_U64;
    }
    assert(kind < BUSTER_HELPER148_HELPERS && first_local + BUSTER_ARRAY_LENGTH(locals) <= program->local_capacity);
    memcpy(fixture->local_classes + first_local, locals, sizeof(locals));
    program->local_count += BUSTER_ARRAY_LENGTH(locals);
    const uint32_t arguments[] =
    {
        0,
        1,
        2,
        3,
        4,
        5,
    };
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
    tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
    tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 6);
    uint32_t first_arguments[BUSTER_ARRAY_LENGTH(arguments)];
    memcpy(first_arguments, arguments, sizeof(arguments));
    const bool release = (leaves[0].flags & BUSTER_HELPER148_RELEASE) != 0;
    const bool alternate = (leaves[0].flags & (BUSTER_HELPER148_CAS | BUSTER_HELPER148_RELEASE)) == 0;
    if (alternate)
    {
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 7);
        first_arguments[1] = 7;
    }
    helper148_call(fixture, leaves, kind, first_arguments);
    if (!release)
    {
        tier0_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
    }
    helper148_call(fixture, leaves, kind, arguments);
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 6);
    if (!release)
    {
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
    tier0_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    program->functions[BUSTER_HELPER148_HELPERS] = (Tier0GeneratedFunction)
    {
        .entry_count = program->entry_count,
        .first_local = first_local,
        .local_count = program->local_count - first_local,
        .parameter_count = parameters,
        .call_count = program->call_count,
        .result_class = BUSTER_TYPED_CLASS_U64,
    };
}

static bool helper148_calls(Helper148Context* context, const IrFunction* function, const Tier0Function* source, uint32_t stage)
{
    uint32_t found = 0;
    bool ready = true;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        if (function->op[node] == IR_CALL)
        {
            const Helper148Leaf* leaf = helper148_symbol(context, function->a[node]);
            const bool bounded = function->b[node] < function->call_count;
            const IrCall* call = bounded ? function->calls + function->b[node] : NULL;
            ready &= helper148_expect(&context->report, found < source->call_count && leaf != NULL && bounded && function->flags[node] == 0 &&
                function->a[node] == source->call_targets[found], "unknown-effect calls retain original order and symbols", stage * 100 + found);
            if (leaf != NULL && call != NULL)
            {
                const bool signature = call->argument_count == leaf->parameter_count && call->parameter_count == leaf->parameter_count &&
                    call->variadic == 0 && call->result_type == helper148_ir_class(leaf->result_class) &&
                    call->type_first <= function->call_type_count && call->argument_count <= function->call_type_count - call->type_first;
                ready &= helper148_expect(&context->report, signature,
                    "call preserves actual helper signature", stage * 100 + found);
                for (uint32_t argument = 0; signature && argument < leaf->parameter_count; argument += 1)
                {
                    ready &= helper148_expect(&context->report, function->call_types[call->type_first + argument] == helper148_ir_class(leaf->parameter_classes[argument]),
                        "ordered argument machine types survive each stage", stage * 100 + found);
                }
            }
            found += 1;
        }
    }
    ready &= helper148_expect(&context->report, found == source->call_count, "no helper call disappears or appears", stage);
    return ready;
}

static bool helper148_upstream(Helper148Context* context, const Tier0Function* source, IrType result_type, bool vector, IrFunction* optimized, IrDominance* dominance)
{
    Arena* scratch = context->arenas + 7;
    IrFunction lowered;
    IrFunction promoted;
    IrCheck check = vector ? ir_lower_typed(source, result_type, BUSTER_HELPER148_HELPERS + 2, context->arenas, scratch, &lowered)
                           : ir_lower_typed_scalar(source, result_type, BUSTER_HELPER148_HELPERS + 2, context->arenas, scratch, &lowered);
    bool ready = helper148_expect(&context->report, check.error == IR_OK, "typed caller lowers with its explicit semantic return type", check.instruction);
    if (ready)
    {
        ready = helper148_calls(context, &lowered, source, 0);
    }
    if (ready)
    {
        check = vector ? ssa_ir_promote(&lowered, context->arenas + 1, scratch, &promoted) : ssa_ir_promote_scalar(&lowered, context->arenas + 1, scratch, &promoted);
        ready = helper148_expect(&context->report, check.error == IR_OK && promoted.slot_count == 0, "real SSA promotion eliminates private locals", check.instruction);
    }
    if (ready)
    {
        ready = helper148_calls(context, &promoted, source, 1);
    }
    if (ready)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.scalar = !vector;
        IrOptimizeResult result;
        check = ir_optimize(&promoted, context->arenas + 2, scratch, options, &result);
        ready = helper148_expect(&context->report, check.error == IR_OK, "real SCCP DCE CFG pipeline succeeds", check.instruction);
        if (ready)
        {
            *optimized = result.function;
            ready = helper148_calls(context, optimized, source, 2);
        }
    }
    IrCfg cfg;
    if (ready)
    {
        ready = helper148_expect(&context->report, ir_build_cfg(optimized, context->arenas + 3, scratch, &cfg) == IR_OK, "rebuild optimized CFG", 0);
    }
    if (ready)
    {
        const DominatorCfg graph =
        {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.path = vector ? DOMINATOR_VECTOR : DOMINATOR_SCALAR;
        options.outputs = 0;
        Dominators result;
        ready = helper148_expect(&context->report, dominators_analyze(context->arenas + 3, scratch, &graph, options, &result) == DOMINATOR_OK, "rebuild optimized dominance", 0);
        if (ready)
        {
            *dominance = (IrDominance)
            {
                .enter = result.preorder,
                .leave = result.subtree_end,
                .block_count = result.block_count,
            };
            const IrCheck scalar = ir_verify_scalar(optimized, scratch, dominance);
            const IrCheck simd = ir_verify(optimized, scratch, dominance);
            ready = helper148_expect(&context->report, scalar.error == IR_OK && simd.error == IR_OK && scalar.dominance_checked && simd.dominance_checked,
                "both IR verifiers accept the actual dominance view", 0);
        }
    }
    ready &= helper148_expect(&context->report, scratch->length == 0, "upstream restores scratch", 0);
    return ready;
}

static bool helper148_map(Helper148Context* context, Tier1EmitResult* emitted, uint32_t expected_calls, Helper148Native* native)
{
    const uint32_t bodies = BUSTER_HELPER148_HELPERS + 1;
    uint64_t starts[BUSTER_HELPER148_HELPERS + 1];
    uint64_t capacities[BUSTER_HELPER148_HELPERS + 1];
    uint64_t capacity = 0;
    uint32_t maximum = emitted->code.count;
    for (uint32_t body = 0; body < bodies; body += 1)
    {
        const uint32_t count = body < BUSTER_HELPER148_HELPERS ? context->helpers[body].code.count : emitted->code.count;
        starts[body] = (capacity + 15) & ~UINT64_C(15);
        capacities[body] = BUSTER_X86_ENCODER_CAPACITY(count);
        capacity = starts[body] + capacities[body];
        maximum = count > maximum ? count : maximum;
    }
    const uint64_t pages = (capacity + BUSTER_HELPER148_PAGE - 1) & ~(uint64_t)(BUSTER_HELPER148_PAGE - 1);
    native->mapping_bytes = pages + BUSTER_HELPER148_PAGE;
    X86Instruction* records[2] = {0};
    uint32_t* offsets[2] = {0};
    bool allocated = true;
    for (uint32_t encoder = 0; encoder < 2; encoder += 1)
    {
        native->mapping[encoder] = mmap(NULL, native->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        records[encoder] = malloc((uint64_t)maximum * sizeof(X86Instruction));
        offsets[encoder] = malloc(((uint64_t)maximum + 1) * sizeof(uint32_t));
        allocated = allocated && native->mapping[encoder] != MAP_FAILED && records[encoder] != NULL && offsets[encoder] != NULL;
    }
    bool ready = helper148_expect(&context->report, capacity < INT32_MAX && allocated &&
        emitted->reference_count == expected_calls && expected_calls <= BUSTER_HELPER148_CALLS, "allocate bounded real-leaf native image", 0);
    for (uint32_t body = 0; ready && body < bodies; body += 1)
    {
        const Tier0Code code_view = body < BUSTER_HELPER148_HELPERS ? context->helpers[body].code : (Tier0Code)
        {
            .records = emitted->code.records,
            .branches = emitted->code.branches,
            .targets = emitted->code.targets,
            .count = emitted->code.count,
            .branch_count = emitted->code.branch_count,
        };
        const Tier0Code* code = &code_view;
        const uint32_t count = code->count;
        uint32_t marked = 0;
        for (uint32_t record = 0; record < count; record += 1)
        {
            if ((code->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
            {
                ready &= helper148_expect(&context->report, body == BUSTER_HELPER148_HELPERS && marked < emitted->reference_count &&
                    code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) && emitted->references[marked].instruction == record,
                    "every marked CALL has exactly one ordered reference before encoding", record);
                marked += 1;
            }
        }
        ready &= helper148_expect(&context->report, marked == (body == BUSTER_HELPER148_HELPERS ? emitted->reference_count : 0),
            "marked-record count is bounded by encoded fixup storage", body);
        const BranchSizingCheck checked = branch_sizing_validate(code->records, count, code->branches, code->targets, code->branch_count);
        ready &= helper148_expect(&context->report, checked.error == BRANCH_SIZING_OK, "native branch table is valid", body);
        uint32_t ends[2][BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_HELPER148_CALLS)] = {0};
        int64_t encoded[2] = {0};
        for (uint32_t encoder = 0; ready && encoder < 2; encoder += 1)
        {
            memcpy(records[encoder], code->records, (uint64_t)count * sizeof(X86Instruction));
            const BranchSizing sized = encoder == 0 ? size_branches_scalar(records[encoder], count, code->branches, code->targets, code->branch_count, offsets[encoder])
                                                    : size_branches(records[encoder], count, code->branches, code->targets, code->branch_count, offsets[encoder]);
            ready = helper148_expect(&context->report, sized.error == BRANCH_SIZING_OK &&
                x86_target_validate(context->allowed, records[encoder], count) == count, "both branch-sizing paths preserve baseline leaf/caller forms", body);
            if (ready)
            {
                encoded[encoder] = encoder == 0 ? x86_encode_scalar(records[encoder], count, native->mapping[encoder] + starts[body], capacities[body], NULL, ends[encoder])
                                                : x86_encode(records[encoder], count, native->mapping[encoder] + starts[body], capacities[body], NULL, ends[encoder]);
                ready = helper148_expect(&context->report, encoded[encoder] > 0 && (uint64_t)encoded[encoder] <= capacities[body],
                    "encode complete actual leaf/caller body", body);
            }
        }
        if (ready)
        {
            ready = helper148_expect(&context->report, encoded[0] == encoded[1] &&
                memcmp(native->mapping[0] + starts[body], native->mapping[1] + starts[body], (uint64_t)encoded[0]) == 0 &&
                memcmp(ends[0], ends[1], marked * sizeof(uint32_t)) == 0,
                "both encoders agree on actual branch-sized bytes and call field ends", body);
        }
        for (uint32_t encoder = 0; ready && body == BUSTER_HELPER148_HELPERS && encoder < 2; encoder += 1)
        {
            for (uint32_t reference = 0; ready && reference < marked; reference += 1)
            {
                const LinkReference* fixup = emitted->references + reference;
                const Helper148Leaf* leaf = helper148_symbol(context, fixup->target);
                uint32_t helper = 0;
                while (helper < BUSTER_HELPER148_HELPERS && context->helpers + helper != leaf)
                {
                    helper += 1;
                }
                ready = helper148_expect(&context->report, leaf != NULL && helper < BUSTER_HELPER148_HELPERS && fixup->kind == BUSTER_LINK_FIXUP_CALL &&
                    fixup->field == X86_FIXUP_DISPLACEMENT && fixup->addend == 0 && fixup->instruction < count,
                    "resolve source SymbolId through actual helper map", reference);
                if (ready)
                {
                    const uint32_t end = ends[encoder][reference];
                    const uint32_t distance = x86_fixup_distance(records[encoder][fixup->instruction].form, fixup->field);
                    const int64_t displacement = (int64_t)starts[helper] - (int64_t)(starts[body] + end);
                    ready = helper148_expect(&context->report, distance == 4 && end >= distance && end <= (uint64_t)encoded[encoder] &&
                        displacement >= INT32_MIN && displacement <= INT32_MAX, "helper rel32 field and displacement are bounded", reference);
                    if (ready)
                    {
                        const int32_t relative = (int32_t)displacement;
                        memcpy(native->mapping[encoder] + starts[body] + end - distance, &relative, sizeof(relative));
                        context->report.calls += 1;
                    }
                }
            }
        }
    }
    for (uint32_t encoder = 0; ready && encoder < 2; encoder += 1)
    {
        native->entry[encoder] = native->mapping[encoder] + starts[BUSTER_HELPER148_HELPERS];
        ready = helper148_expect(&context->report, mprotect(native->mapping[encoder], pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(native->mapping[encoder] + pages, BUSTER_HELPER148_PAGE, PROT_NONE) == 0, "make both patched native images RX with a guard", 0);
    }
    for (uint32_t encoder = 0; encoder < 2; encoder += 1)
    {
        free(records[encoder]);
        free(offsets[encoder]);
    }
    return ready;
}

static void helper148_emit_diagnostic(Helper148Context* context, const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Tier1EmitError error, uint32_t node)
{
    if (context->report.failures < 24)
    {
        static const char* emit_names[] = { "OK", "INVALID", "UNSUPPORTED", "MEMORY", "ALLOCATION", "FRAME", "TARGET", "LIMIT" };
        static const char* format_names[] = { "INTEGER_BITS", "BOOL_BITS", "F32_BITS", "F64_BITS" };
        const Helper148Leaf* helper = context->helpers;
        fprintf(stderr, "helper-tier1-atomic-rmw-148: emit diagnostic id=%u width=%u format=%s(%u) path=%s emit=%s(%u) node=%u caller-parameters=%u callee-parameters=%u register-mask=%08x\n",
            (uint32_t)helper->id, helper->bytes,
            (uint32_t)helper->format < BUSTER_ARRAY_LENGTH(format_names) ? format_names[helper->format] : "UNKNOWN", (uint32_t)helper->format,
            options->vector ? "vector" : "scalar", (uint32_t)error < BUSTER_ARRAY_LENGTH(emit_names) ? emit_names[error] : "UNKNOWN", (uint32_t)error,
            node, plan->function->parameter_count, helper->parameter_count, options->register_mask);
        // The transactional emitter preserves its result on failure. Replay only the public preparation and
        // allocation stages in its restored arenas to expose their actual statuses, then restore both lengths.
        Arena* output = context->arenas + 5;
        Arena* scratch = context->arenas + 7;
        const uint64_t output_mark = output->length;
        const uint64_t scratch_mark = scratch->length;
        Tier1Liveness liveness = {0};
        Tier1EmitPrepared prepared = {0};
        Tier1Allocation allocation = {0};
        const Tier1LivenessError live = tier1_liveness_build(plan, output, scratch, options->vector, &liveness);
        int prepare_status = -1;
        int allocation_status = -1;
        uint32_t empty = 0;
        if (live == TIER1_LIVENESS_OK)
        {
            prepare_status = tier1_emit_prepare(plan, &liveness, options, output, &prepared);
            if (prepare_status == TIER1_EMIT_OK)
            {
                for (uint32_t interval = 0; interval < prepared.allocation.count; interval += 1)
                {
                    if (prepared.allocation.allowed_registers[interval] == 0)
                    {
                        if (empty < 8)
                        {
                            const uint32_t value = prepared.allocation.values[interval];
                            const uint32_t parameter = plan->function->op[value] == IR_PARAM ? plan->function->a[value] : BUSTER_IR_NONE;
                            fprintf(stderr, "helper-tier1-atomic-rmw-148: zero allowed interval=%u value=%u parameter=%u original-mask=%08x effective-mask=%08x\n",
                                interval, value, parameter, liveness.allocation.allowed_registers[interval], prepared.allocation.registers[0]);
                        }
                        empty += 1;
                    }
                }
                allocation_status = options->vector ? tier1_allocate(&prepared.allocation, output, &allocation) : tier1_allocate_scalar(&prepared.allocation, output, &allocation);
            }
        }
        fprintf(stderr, "helper-tier1-atomic-rmw-148: stage diagnostic liveness=%u prepare=%d allocation=%d intervals=%u zero-allowed=%u (unreached=-1; allocation OK=0 INVALID=1 CAPACITY=2 CONSTRAINT=3)\n",
            (uint32_t)live, prepare_status, allocation_status, prepared.allocation.count, empty);
        arena_pop_to(scratch, scratch_mark);
        arena_pop_to(output, output_mark);
    }
}

static bool helper148_compile(Helper148Context* context, const Helper148Fixture* fixture, bool vector, Helper148Native* native)
{
    for (uint32_t arena = 0; arena < BUSTER_HELPER148_ARENAS; arena += 1)
    {
        arena_pop_to(context->arenas + arena, 0);
    }
    Helper148Fixture before;
    memcpy(&before, fixture, sizeof(before));
    Tier0Functions functions;
    bool ready = helper148_expect(&context->report, !fixture->program.overflow && tier0_program_functions(&fixture->program, &context->tables, context->arenas + 6, &functions),
        "production ABI batch prepares signature declarations and caller", 0);
    const Tier0Function* source = ready ? functions.functions + BUSTER_HELPER148_HELPERS : NULL;
    IrFunction function;
    IrDominance dominance;
    if (ready)
    {
        ready = helper148_expect(&context->report, tier0_validate(source).error == TIER0_OK, "original typed caller validates", 0);
    }
    if (ready)
    {
        const IrType result_type = helper148_ir_class(fixture->program.functions[BUSTER_HELPER148_HELPERS].result_class);
        ready = helper148_upstream(context, source, result_type, vector, &function, &dominance);
    }
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    if (ready)
    {
        const Tier1SelectError error = tier1_select_plan(&function, &dominance, context->arenas + 4, context->arenas + 7, vector, &plan, &node);
        ready = helper148_expect(&context->report, error == TIER1_SELECT_OK, "select actual optimized SSA caller", node);
    }
    Tier1EmitResult emitted;
    if (ready)
    {
        const Tier1EmitOptions options =
        {
            // Every live incoming parameter has its fixed SysV register, including the sixth CAS argument in R9.
            .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_R8) | (1u << X86_REGISTER_R9),
            .frame_flags = BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE,
            .vector = vector,
            .rematerialize = true,
        };
        const Tier1EmitError error = tier1_emit_function(&plan, &options, context->allowed, context->arenas + 5, context->arenas + 7, &emitted, &node);
        if (error != TIER1_EMIT_OK)
        {
            helper148_emit_diagnostic(context, &plan, &options, error, node);
        }
        ready = helper148_expect(&context->report, error == TIER1_EMIT_OK, "real liveness allocation frame and CALL emission", node);
    }
    if (ready)
    {
        ready = helper148_expect(&context->report, emitted.reference_count == source->call_count && !emitted.frame.red_zone &&
            tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation) && emitted.call_stores == emitted.call_loads,
            "every unknown-effect caller reference and balanced live save survives", 0);
        if (source->parameter_count != 0)
        {
            ready &= helper148_expect(&context->report, emitted.call_stores != 0, "nonconstant caller values actually survive helper clobbers", 0);
        }
    }
    if (ready)
    {
        ready = helper148_map(context, &emitted, source->call_count, native);
    }
    ready &= helper148_expect(&context->report, memcmp(&before, fixture, sizeof(before)) == 0 && context->arenas[7].length == 0,
        "pipeline preserves all original typed columns and restores scratch", 0);
    return ready;
}

typedef union Helper148Storage Helper148Storage;
union Helper148Storage
{
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
};

static void* helper148_storage(Helper148Storage* storage, const Helper148Case* item)
{
    void* result = item->format == CBUILTIN_ATOMIC_RMW_F32_BITS ? (void*)&storage->f32 :
        item->format == CBUILTIN_ATOMIC_RMW_F64_BITS ? (void*)&storage->f64 : item->bytes == 1 ? (void*)&storage->u8 :
        item->bytes == 2 ? (void*)&storage->u16 : item->bytes == 4 ? (void*)&storage->u32 : (void*)&storage->u64;
    return result;
}

// The legacy warning describes the intended modern NAND semantics used by this native oracle.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsync-fetch-and-nand-semantics-changed"

#define BUSTER_HELPER148_ORACLE_CASES(Type) \
    switch (item->id) \
    { \
    case CBUILTIN_ATOMIC_EXCHANGE_N: result = __atomic_exchange_n(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N: result = __atomic_compare_exchange_n(object, expected, (Type)value, weak, order, failure); break; \
    case CBUILTIN_ATOMIC_FETCH_ADD: result = __atomic_fetch_add(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_SUB: result = __atomic_fetch_sub(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_AND: result = __atomic_fetch_and(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_OR: result = __atomic_fetch_or(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_XOR: result = __atomic_fetch_xor(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_NAND: result = __atomic_fetch_nand(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_ADD_FETCH: result = __atomic_add_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_SUB_FETCH: result = __atomic_sub_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_AND_FETCH: result = __atomic_and_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_OR_FETCH: result = __atomic_or_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_XOR_FETCH: result = __atomic_xor_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_NAND_FETCH: result = __atomic_nand_fetch(object, (Type)value, order); break; \
    case CBUILTIN_SYNC_FETCH_AND_ADD: result = __sync_fetch_and_add(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_SUB: result = __sync_fetch_and_sub(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_AND: result = __sync_fetch_and_and(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_OR: result = __sync_fetch_and_or(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_XOR: result = __sync_fetch_and_xor(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_NAND: result = __sync_fetch_and_nand(object, (Type)value); break; \
    case CBUILTIN_SYNC_ADD_AND_FETCH: result = __sync_add_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_SUB_AND_FETCH: result = __sync_sub_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_AND_AND_FETCH: result = __sync_and_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_OR_AND_FETCH: result = __sync_or_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_XOR_AND_FETCH: result = __sync_xor_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_NAND_AND_FETCH: result = __sync_nand_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP: result = __sync_bool_compare_and_swap(object, (Type)expected_value, (Type)value); break; \
    case CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP: result = __sync_val_compare_and_swap(object, (Type)expected_value, (Type)value); break; \
    case CBUILTIN_SYNC_LOCK_TEST_AND_SET: result = __sync_lock_test_and_set(object, (Type)value); break; \
    case CBUILTIN_SYNC_LOCK_RELEASE: __sync_lock_release(object); result = 0; break; \
    default: abort(); \
    }

static __attribute__((noinline)) uint64_t helper148_oracle(const Helper148Case* item, Helper148Storage* object_storage, Helper148Storage* expected_storage,
    uint64_t expected_value, uint64_t value, bool weak, int order, int failure)
{
    uint64_t result = 0;
    void* object_pointer = helper148_storage(object_storage, item);
    void* expected_pointer = helper148_storage(expected_storage, item);
    if (item->format == CBUILTIN_ATOMIC_RMW_F32_BITS)
    {
        uint32_t bits = (uint32_t)value;
        float desired;
        memcpy(&desired, &bits, sizeof(desired));
        const float previous = __atomic_exchange_n((volatile float*)object_pointer, desired, order);
        memcpy(&bits, &previous, sizeof(bits));
        result = bits;
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_F64_BITS)
    {
        double desired;
        memcpy(&desired, &value, sizeof(desired));
        const double previous = __atomic_exchange_n((volatile double*)object_pointer, desired, order);
        memcpy(&result, &previous, sizeof(result));
    }
    else if (item->bytes == 1)
    {
        volatile uint8_t* object = object_pointer;
        uint8_t* expected = expected_pointer;
        BUSTER_HELPER148_ORACLE_CASES(uint8_t);
    }
    else if (item->bytes == 2)
    {
        volatile uint16_t* object = object_pointer;
        uint16_t* expected = expected_pointer;
        BUSTER_HELPER148_ORACLE_CASES(uint16_t);
    }
    else if (item->bytes == 4)
    {
        volatile uint32_t* object = object_pointer;
        uint32_t* expected = expected_pointer;
        BUSTER_HELPER148_ORACLE_CASES(uint32_t);
    }
    else
    {
        volatile uint64_t* object = object_pointer;
        uint64_t* expected = expected_pointer;
        BUSTER_HELPER148_ORACLE_CASES(uint64_t);
    }
    return result;
}

#undef BUSTER_HELPER148_ORACLE_CASES
#pragma clang diagnostic pop

static void helper148_arguments(const Helper148Case* item, void* object, void* expected, uint64_t expected_value, uint64_t value,
    bool weak, int order, int failure, uint64_t arguments[6])
{
    const uint64_t initial[6] =
    {
        (uintptr_t)object,
        value,
        (uint32_t)order,
        0,
        0,
        0,
    };
    memcpy(arguments, initial, sizeof(initial));
    if ((item->flags & BUSTER_HELPER148_CAS) != 0)
    {
        arguments[1] = item->id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? (uintptr_t)expected : expected_value;
        arguments[2] = value;
        arguments[3] = weak;
        arguments[4] = (uint32_t)order;
        arguments[5] = (uint32_t)failure;
    }
}

static __attribute__((no_sanitize("function"))) uint64_t helper148_invoke(const Helper148Native* native, uint32_t encoder, const uint64_t arguments[6])
{
    uint64_t (*call)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) =
        (uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))(void*)native->entry[encoder];
    const uint64_t result = call(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5]);
    return result;
}

static uint16_t helper148_control(void)
{
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
    return control;
}

static void helper148_sample(Helper148Report* report, const Helper148Case* item, uint32_t path, uint32_t encoder, uint8_t* object_page, uint8_t* expected_page,
    uint32_t position, uint32_t alias_mode, uint64_t initial, uint64_t value, uint64_t expected_value, bool weak, int order, int failure,
    uint32_t environment, uint16_t control, uint32_t ordinal)
{
    const uint32_t at = position == 0 ? 0 : BUSTER_HELPER148_PAGE - item->bytes;
    const uint32_t expected_at = position == 0 ? BUSTER_HELPER148_PAGE - item->bytes : 0;
    uint8_t* object = object_page + at;
    uint8_t* expected = expected_page + expected_at;
    memset(object_page, 0xa5, BUSTER_HELPER148_PAGE);
    memset(expected_page, 0xa5, BUSTER_HELPER148_PAGE);
    memcpy(object, &initial, item->bytes);
    memcpy(expected, &expected_value, item->bytes);
    Helper148Storage reference = {0};
    Helper148Storage reference_expected = {0};
    memcpy(helper148_storage(&reference, item), &initial, item->bytes);
    memcpy(helper148_storage(&reference_expected, item), &expected_value, item->bytes);
    Helper148Storage* oracle_expected = alias_mode == 1 ? &reference : &reference_expected;
    const bool alternate = (item->flags & (BUSTER_HELPER148_CAS | BUSTER_HELPER148_RELEASE)) == 0;
    _mm_setcsr(environment);
    (void)helper148_oracle(item, &reference, oracle_expected, expected_value, alternate ? value ^ 1 : value, weak, order, failure);
    const uint64_t raw = helper148_oracle(item, &reference, oracle_expected, expected_value, value, weak, order, failure);
    helper148_expect(report, _mm_getcsr() == environment && helper148_control() == control, "host raw-bit atomic oracle preserves FP state", ordinal);
    uint8_t wanted_object[BUSTER_HELPER148_PAGE];
    uint8_t wanted_expected[BUSTER_HELPER148_PAGE];
    memset(wanted_object, 0xa5, sizeof(wanted_object));
    memset(wanted_expected, 0xa5, sizeof(wanted_expected));
    memcpy(wanted_object + at, helper148_storage(&reference, item), item->bytes);
    memcpy(wanted_expected + expected_at, helper148_storage(&reference_expected, item), item->bytes);
    uint64_t arguments[6];
    helper148_arguments(item, object, alias_mode == 1 ? object : expected, expected_value, value, weak, order, failure, arguments);
    const uint64_t seed = arguments[0] ^ arguments[1];
    const bool read_only = alias_mode != 2 || mprotect(expected_page, BUSTER_HELPER148_PAGE, PROT_READ) == 0;
    helper148_expect(report, read_only, "successful expected-value storage becomes read-only", ordinal);
    if (read_only)
    {
        errno = 0x148;
        _mm_setcsr(environment);
        const uint64_t actual = helper148_invoke(&item->programs[path], encoder, arguments);
        const uint32_t state = _mm_getcsr();
        const int actual_errno = errno;
        const bool values = actual == raw + seed && memcmp(object_page, wanted_object, sizeof(wanted_object)) == 0 &&
            memcmp(expected_page, wanted_expected, sizeof(wanted_expected)) == 0;
        if (!values && report->failures < 24)
        {
            fprintf(stderr, "helper-tier1-atomic-rmw-148: id=%u bytes=%u format=%u path=%u enc=%u alias=%u result=%016" PRIx64 "/%016" PRIx64 "\n",
                (uint32_t)item->id, item->bytes, (uint32_t)item->format, path, encoder, alias_mode, actual, raw + seed);
        }
        helper148_expect(report, values, "two actual Tier1 calls match Clang results, object/expected bytes and preserved live seed", ordinal);
        helper148_expect(report, state == environment && helper148_control() == control && actual_errno == 0x148, "raw atomic callers preserve FP state and errno", ordinal);
        report->native_results += 1;
    }
    if (alias_mode == 2)
    {
        if (!helper148_expect(report, mprotect(expected_page, BUSTER_HELPER148_PAGE, PROT_READ | PROT_WRITE) == 0, "restore expected-value storage permissions", ordinal))
        {
            _exit(93);
        }
    }
}

typedef struct Helper148Worker Helper148Worker;
struct Helper148Worker
{
    const Helper148Native* native;
    uint64_t* counter;
    uint64_t values[BUSTER_HELPER148_ROUNDS];
    uint32_t encoder;
};

static void* helper148_worker(void* opaque)
{
    Helper148Worker* worker = opaque;
    const uint64_t arguments[6] =
    {
        (uintptr_t)worker->counter,
        1,
        __ATOMIC_SEQ_CST,
        0,
        0,
        0,
    };
    const uint64_t seed = arguments[0] ^ arguments[1];
    for (uint32_t round = 0; round < BUSTER_HELPER148_ROUNDS; round += 1)
    {
        // The caller's first fetch-add operand is 1^1==0; its second is 1, returning each old counter value once.
        worker->values[round] = helper148_invoke(worker->native, worker->encoder, arguments) - seed;
    }
    return NULL;
}

static void helper148_counter(Helper148Report* report, const Helper148Case* items, uint32_t count)
{
    const Helper148Case* item = NULL;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (items[i].id == CBUILTIN_ATOMIC_FETCH_ADD && items[i].bytes == 8 && items[i].format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS)
        {
            item = items + i;
        }
    }
    if (helper148_expect(report, item != NULL, "counter fixture resolves the actual U64 fetch-add consumer", 0))
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t encoder = 0; encoder < 2; encoder += 1)
            {
                uint64_t counter = 0;
                Helper148Worker workers[BUSTER_HELPER148_THREADS] = {0};
                pthread_t threads[BUSTER_HELPER148_THREADS];
                uint32_t created = 0;
                for (uint32_t i = 0; i < BUSTER_HELPER148_THREADS && created == i; i += 1)
                {
                    workers[i].native = &item->programs[path];
                    workers[i].counter = &counter;
                    workers[i].encoder = encoder;
                    created += pthread_create(threads + i, NULL, helper148_worker, workers + i) == 0;
                }
                for (uint32_t i = 0; i < created; i += 1)
                {
                    if (pthread_join(threads[i], NULL) != 0)
                    {
                        _exit(94);
                    }
                }
                const uint32_t total = BUSTER_HELPER148_THREADS * BUSTER_HELPER148_ROUNDS;
                bool seen[BUSTER_HELPER148_THREADS * BUSTER_HELPER148_ROUNDS] = {0};
                bool correct = created == BUSTER_HELPER148_THREADS && counter == total;
                uint64_t sum = 0;
                for (uint32_t i = 0; i < created; i += 1)
                {
                    for (uint32_t round = 0; round < BUSTER_HELPER148_ROUNDS; round += 1)
                    {
                        const uint64_t old = workers[i].values[round];
                        correct = correct && old < total;
                        if (old < total)
                        {
                            correct = correct && !seen[old];
                            seen[old] = true;
                        }
                        sum += old;
                        report->native_results += 1;
                    }
                }
                helper148_expect(report, correct && sum == (uint64_t)total * (total - 1) / 2,
                    "contended actual Tier1 calls return every old value exactly once and the complete final count", path * 2 + encoder);
            }
        }
    }
}

static void helper148_values(Helper148Report* report, const Helper148Case* items, uint32_t count)
{
    const uint16_t original_control = helper148_control();
    const uint32_t original_mxcsr = _mm_getcsr();
    const int original_errno = errno;
    const uint16_t control = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | UINT16_C(0x0800) | UINT16_C(0x003f));
    const uint32_t environment = (original_mxcsr & ~UINT32_C(0x603f)) | UINT32_C(0xbfd5);
    __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
    _mm_setcsr(environment);
    uint8_t* mapping = mmap(NULL, 6 * BUSTER_HELPER148_PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED;
    const bool ready = mapped && mprotect(mapping + BUSTER_HELPER148_PAGE, BUSTER_HELPER148_PAGE, PROT_READ | PROT_WRITE) == 0 &&
        mprotect(mapping + 4 * BUSTER_HELPER148_PAGE, BUSTER_HELPER148_PAGE, PROT_READ | PROT_WRITE) == 0;
    if (helper148_expect(report, ready, "separate naturally aligned guarded object/expected pages", 0))
    {
        uint8_t* object_page = mapping + BUSTER_HELPER148_PAGE;
        uint8_t* expected_page = mapping + 4 * BUSTER_HELPER148_PAGE;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const Helper148Case* item = items + i;
            const uint64_t mask = UINT64_MAX >> (64 - item->bytes * 8);
            const uint32_t orders = (item->flags & BUSTER_HELPER148_GNU) != 0 ? 2 : 1;
            const uint32_t matches = (item->flags & BUSTER_HELPER148_CAS) != 0 ? 2 : 1;
            for (uint32_t pattern = 0; pattern < 2; pattern += 1)
            {
                uint64_t initial = pattern == 0 ? UINT64_C(0x0123456789abcdef) : UINT64_MAX;
                uint64_t value = pattern == 0 ? UINT64_C(0xfedcba9876543210) : 1;
                if (item->format == CBUILTIN_ATOMIC_RMW_BOOL_BITS)
                {
                    initial = pattern;
                    value = pattern ^ 1;
                }
                else if (item->format == CBUILTIN_ATOMIC_RMW_F32_BITS)
                {
                    initial = pattern == 0 ? UINT64_C(0x7f812345) : UINT64_C(0x80000000);
                    value = UINT64_C(0xfeedbeef00000000) | (pattern == 0 ? UINT32_C(0xffc54321) : UINT32_C(0x00000001));
                }
                else if (item->format == CBUILTIN_ATOMIC_RMW_F64_BITS)
                {
                    initial = pattern == 0 ? UINT64_C(0x7ff0123456789abc) : UINT64_C(0x8000000000000000);
                    value = pattern == 0 ? UINT64_C(0xfff854321abcdef0) : 1;
                }
                initial &= mask;
                for (uint32_t order = 0; order < orders; order += 1)
                {
                    const int success_order = order == 0 ? __ATOMIC_SEQ_CST : __ATOMIC_RELAXED;
                    const int failure_order = order == 0 ? __ATOMIC_ACQUIRE : __ATOMIC_RELAXED;
                    for (uint32_t match = 0; match < matches; match += 1)
                    {
                        const uint64_t expected = initial ^ (match == 0 ? 0 : 1);
                        for (uint32_t position = 0; position < 2; position += 1)
                        {
                            for (uint32_t path = 0; path < 2; path += 1)
                            {
                                for (uint32_t encoder = 0; encoder < 2; encoder += 1)
                                {
                                    helper148_sample(report, item, path, encoder, object_page, expected_page, position, 0, initial, value, expected,
                                        pattern != 0, success_order, failure_order, environment, control, i);
                                }
                            }
                        }
                    }
                }
                if (item->id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N)
                {
                    for (uint32_t position = 0; position < 2; position += 1)
                    {
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            for (uint32_t encoder = 0; encoder < 2; encoder += 1)
                            {
                                helper148_sample(report, item, path, encoder, object_page, expected_page, position, 1, initial, value, initial,
                                    pattern != 0, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, environment, control, i);
                                helper148_sample(report, item, path, encoder, object_page, expected_page, position, 2, value & mask, value, value & mask,
                                    pattern != 0, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, environment, control, i);
                            }
                        }
                    }
                }
            }
        }
    }
    helper148_counter(report, items, count);
    __asm__ volatile("fldcw %0" : : "m"(original_control) : "memory");
    _mm_setcsr(original_mxcsr);
    errno = original_errno;
    helper148_expect(report, helper148_control() == original_control && _mm_getcsr() == original_mxcsr && errno == original_errno,
        "native child restores its original FP state and errno", 0);
    if (mapped)
    {
        munmap(mapping, 6 * BUSTER_HELPER148_PAGE);
    }
}

int main(int argc, char** argv)
{
    (void)argv;
    Helper148Context context = {0};
    bool ready = helper148_expect(&context.report, argc == 1 && __clang_major__ == 23 && __clang_minor__ == 1 && __clang_patchlevel__ == 1,
        "standalone exact Clang23.1.1 host-oracle command takes no arguments", 0);
    ready = ready && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && tier0_initialize() && tier0_tables_open(&context.tables);
    context.leaves = arena_reserve(1u << 20);
    for (uint32_t arena = 0; arena < BUSTER_HELPER148_ARENAS; arena += 1)
    {
        context.arenas[arena] = arena_reserve(8u << 20);
        ready = context.arenas[arena].base != NULL && ready;
    }
    ready = helper148_expect(&context.report, ready && context.leaves.base != NULL, "initialize standalone production RMW pipeline", 0);
    const X86Target baseline = { .architecture = X86_ARCH_BASELINE };
    x86_target_forms(&baseline, context.allowed);
    Helper148Case items[BUSTER_HELPER148_FIXTURES] = {0};
    uint32_t count = 0;
    for (uint32_t spec = 0; ready && spec < BUSTER_ARRAY_LENGTH(helper148_specs); spec += 1)
    {
        const Helper148Spec* description = helper148_specs + spec;
        const uint32_t variants = 4 + ((description->flags & BUSTER_HELPER148_BOOL) != 0) + (description->id == CBUILTIN_ATOMIC_EXCHANGE_N ? 2u : 0u);
        for (uint32_t variant = 0; ready && variant < variants; variant += 1)
        {
            ready = helper148_expect(&context.report, count < BUSTER_HELPER148_FIXTURES, "bounded supported helper inventory", count);
            if (ready)
            {
                Helper148Case* item = items + count;
                item->id = description->id;
                item->flags = description->flags;
                item->bytes = variant < 4 ? 1u << variant : variant == 4 ? 1 : variant == 5 ? 4 : 8;
                item->format = variant < 4 ? CBUILTIN_ATOMIC_RMW_INTEGER_BITS : variant == 4 ? CBUILTIN_ATOMIC_RMW_BOOL_BITS :
                    variant == 5 ? CBUILTIN_ATOMIC_RMW_F32_BITS : CBUILTIN_ATOMIC_RMW_F64_BITS;
                ready = helper148_leaf(&context, description, item->bytes, item->format, count);
                Helper148Fixture input;
                if (ready)
                {
                    helper148_fixture(&input, context.helpers, 0);
                }
                for (uint32_t path = 0; ready && path < 2; path += 1)
                {
                    ready = helper148_compile(&context, &input, path != 0, &item->programs[path]);
                }
                count += 1;
            }
        }
    }
    helper148_expect(&context.report, BUSTER_ARRAY_LENGTH(helper148_specs) == 30 && count == 140, "all30 IDs and140 supported width/format combinations prepared", count);
    Helper148Report* shared = MAP_FAILED;
    if (ready)
    {
        shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        ready = helper148_expect(&context.report, shared != MAP_FAILED, "allocate native accounting report", 0);
    }
    if (ready)
    {
        fflush(NULL);
        const pid_t child = fork();
        if (child == 0)
        {
            const struct rlimit no_core = {0};
            sigset_t unblocked;
            sigemptyset(&unblocked);
            if (signal(SIGALRM, SIG_DFL) == SIG_ERR || sigprocmask(SIG_SETMASK, &unblocked, NULL) != 0 || setrlimit(RLIMIT_CORE, &no_core) != 0)
            {
                _exit(98);
            }
            alarm(30);
            helper148_values(shared, items, count);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        } while (child > 0 && waited < 0 && errno == EINTR);
        helper148_expect(&context.report, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "alarm-bounded real Tier1 RMW native child completes", (uint32_t)status);
    }
    if (shared != MAP_FAILED)
    {
        context.report.checks += shared->checks;
        context.report.failures += shared->failures;
        context.report.native_results += shared->native_results;
        munmap(shared, sizeof(*shared));
    }
    helper148_expect(&context.report, context.report.native_results == 4800 && context.report.calls == 1120,
        "all independent native cases and real SymbolId CALL patches completed", 0);
    for (uint32_t item = 0; item < count; item += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t encoder = 0; encoder < 2; encoder += 1)
            {
                if (items[item].programs[path].mapping[encoder] != NULL && items[item].programs[path].mapping[encoder] != MAP_FAILED)
                {
                    munmap(items[item].programs[path].mapping[encoder], items[item].programs[path].mapping_bytes);
                }
            }
        }
    }
    for (uint32_t arena = 0; arena < BUSTER_HELPER148_ARENAS; arena += 1)
    {
        arena_release(context.arenas + arena);
    }
    tier0_tables_close(&context.tables);
    arena_release(&context.leaves);
    printf("helper-tier1-atomic-rmw-148: %" PRIu64 " checks, %" PRIu64 " failures; native=%" PRIu64 " resolved-calls=%" PRIu64 "; leaf=c47c23293d0e1a307dee34a8a69119d58e692fb7 backend=73e95edbb2d6acf7645c2d15c7f8bc584333f273\n",
        context.report.checks, context.report.failures, context.report.native_results, context.report.calls);
    const int result = context.report.failures != 0;
    return result;
}
