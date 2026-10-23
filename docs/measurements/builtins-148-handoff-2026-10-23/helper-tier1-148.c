// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent #148 consumer harness. Include ONLY the immutable composition
// 19b9454dc0e4fde5596a16efc917f17fa4ce190a (#339 03812fae + #346 73e95edb).
// This file is original C; it borrows production interfaces and actual atomic/rounding leaf emitters, not tests.
// Signature-only helper declarations feed the existing ABI batch; they have no mock executable typed bodies.
// Every CALLER traverses typed lowering, SSA promotion, SCCP/DCE/CFG optimization, dominance verification,
// selection, liveness/allocation/frame emission, branch sizing, both encoders and real symbolic CALL fixups.
// The helpers retain their original native record implementations. Source dispatch/intrinsic lowering and
// protected integration remain separate acceptance work. Single-thread observations do not prove memory ordering.
//
// Compile on the approved 9700X under its normal lock. From the directory containing this file and the immutable
// builtin-tier1-acceptance checkout, the exact relative command is:
// clang -O3 -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function -I./builtin-tier1-acceptance/src ./helper-tier1-148.c -pthread -o ./helper-tier1-148
// -Wno-unused-function permits unused static production entry points in this standalone inclusion unit.
// There is no libm dependency, source mutation, benchmark or runtime dispatch to another compiler tier.

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
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
#include "buster/compiler/c_builtin_atomic_helpers.c"
#include "buster/compiler/c_builtin_rounds_helper.c"
#include "buster/compiler/ir.c"
#include "buster/compiler/ir_lower.c"
#include "buster/compiler/ssa_ir.c"
#include "buster/compiler/ir_optimize.c"
#include "buster/compiler/tier1_select.c"
#include "buster/compiler/tier1_liveness.c"
#include "buster/compiler/tier1_emit.c"
#include "buster/compiler/branch_sizing.c"

#define BUSTER_HELPER148_HELPERS 12u
#define BUSTER_HELPER148_FIXTURES 6u
#define BUSTER_HELPER148_ENTRIES 64u
#define BUSTER_HELPER148_CALLS 6u
#define BUSTER_HELPER148_ARGUMENTS 16u
#define BUSTER_HELPER148_PAGE 4096u
#define BUSTER_HELPER148_ARENAS 8u

typedef struct Helper148Leaf Helper148Leaf;
struct Helper148Leaf
{
    SymbolId symbol;
    CBuiltinId id;
    uint32_t bytes;
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    uint8_t parameter_classes[3];
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
    uint8_t* mapping;
    uint8_t* entry;
    uint64_t mapping_bytes;
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

static bool helper148_expect(Helper148Report* report, bool condition, const char* message, uint32_t item)
{
    report->checks += 1;
    if (!condition)
    {
        if (report->failures < 24)
        {
            fprintf(stderr, "helper-tier1-148: %s (item=%u)\n", message, item);
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

static bool helper148_leaves(Helper148Context* context)
{
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_HELPER148_HELPERS; i += 1)
    {
        Helper148Leaf* leaf = context->helpers + i;
        leaf->symbol = 1 + i;
        leaf->bytes = i < 8 ? 1u << (i % 4) : 0;
        leaf->id = i < 4 ? CBUILTIN_ATOMIC_LOAD_N : i < 8 ? CBUILTIN_ATOMIC_STORE_N : i == 8 ? CBUILTIN_ATOMIC_THREAD_FENCE :
            i == 9 ? CBUILTIN_ATOMIC_SIGNAL_FENCE : i == 10 ? CBUILTIN_SYNC_SYNCHRONIZE : CBUILTIN_FLT_ROUNDS;
        if (i < 11)
        {
            const CBuiltinAtomicHelper emitted = cbuiltin_atomic_helper_emit(leaf->id, leaf->bytes, &context->leaves);
            leaf->code = emitted.code;
            leaf->parameter_count = emitted.parameter_count;
            leaf->result_class = emitted.result_class;
            memcpy(leaf->parameter_classes, emitted.parameter_classes, sizeof(leaf->parameter_classes));
            ready &= helper148_expect(&context->report, emitted.compiler_barrier && emitted.required_alignment == leaf->bytes, "atomic helper alignment/barrier contract", i);
        }
        else
        {
            const CBuiltinRoundsHelper emitted = cbuiltin_rounds_helper(leaf->id, &context->leaves);
            leaf->code = emitted.code;
            leaf->parameter_count = emitted.parameter_count;
            leaf->result_class = emitted.result_class;
            ready &= helper148_expect(&context->report, emitted.compiler_barrier && emitted.reads_fp_environment, "rounds helper environment/barrier contract", i);
        }
        ready &= helper148_expect(&context->report, leaf->code.error == TIER0_OK && leaf->code.count != 0 && leaf->code.branch_count == 0 &&
            leaf->code.reference_count == 0 && x86_target_validate(context->allowed, leaf->code.records, leaf->code.count) == leaf->code.count,
            "actual native leaf validates at baseline", i);
    }
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
    uint32_t parameters = kind < 4 ? 5 : kind == 4 ? 0 : 1;
    const uint32_t result_class = kind == 4 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
    if (kind < 4)
    {
        const uint8_t locals[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I32,
            BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
        memcpy(fixture->local_classes + first_local, locals, sizeof(locals));
        program->local_count += BUSTER_ARRAY_LENGTH(locals);
        const uint32_t load[] = { 0, 3 };
        const uint32_t store[] = { 0, 1, 2 };
        const uint32_t release[] = { 2 };
        const uint32_t acquire[] = { 3 };
        helper148_call(fixture, leaves, kind, load);
        tier0_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
        helper148_call(fixture, leaves, 4 + kind, store);
        helper148_call(fixture, leaves, 8, release);
        helper148_call(fixture, leaves, 9, acquire);
        helper148_call(fixture, leaves, 10, NULL);
        helper148_call(fixture, leaves, kind, load);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
        tier0_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4);
        tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
    else
    {
        if (kind == 5)
        {
            fixture->local_classes[first_local] = BUSTER_TYPED_CLASS_U64;
            program->local_count += 1;
            helper148_call(fixture, leaves, 11, NULL);
            tier0_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U32, 0);
        }
        helper148_call(fixture, leaves, 11, NULL);
        if (kind == 5)
        {
            tier0_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    tier0_emit(program, BUSTER_TYPED_RETURN, result_class, 0);
    program->functions[BUSTER_HELPER148_HELPERS] = (Tier0GeneratedFunction)
    {
        .entry_count = program->entry_count,
        .first_local = first_local,
        .local_count = program->local_count - first_local,
        .parameter_count = parameters,
        .call_count = program->call_count,
        .result_class = (uint8_t)result_class,
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
    native->mapping = mmap(NULL, native->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scalar = malloc(capacity);
    uint32_t* offsets = malloc(((uint64_t)maximum + 1) * sizeof(uint32_t));
    uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_HELPER148_CALLS)] = { 0 };
    uint32_t scalar_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_HELPER148_CALLS)] = { 0 };
    bool ready = helper148_expect(&context->report, capacity < INT32_MAX && native->mapping != MAP_FAILED && scalar != NULL && offsets != NULL &&
        emitted->reference_count == expected_calls && expected_calls <= BUSTER_HELPER148_CALLS, "allocate bounded real-leaf native image", 0);
    for (uint32_t body = 0; ready && body < bodies; body += 1)
    {
        X86Instruction* records = body < BUSTER_HELPER148_HELPERS ? context->helpers[body].code.records : emitted->code.records;
        const uint32_t count = body < BUSTER_HELPER148_HELPERS ? context->helpers[body].code.count : emitted->code.count;
        const uint32_t* branches = body < BUSTER_HELPER148_HELPERS ? NULL : emitted->code.branches;
        const uint32_t* targets = body < BUSTER_HELPER148_HELPERS ? NULL : emitted->code.targets;
        const uint32_t branch_count = body < BUSTER_HELPER148_HELPERS ? 0 : emitted->code.branch_count;
        uint32_t marked = 0;
        for (uint32_t record = 0; record < count; record += 1)
        {
            if ((records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
            {
                ready &= helper148_expect(&context->report, body == BUSTER_HELPER148_HELPERS && marked < emitted->reference_count &&
                    records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) && emitted->references[marked].instruction == record,
                    "every marked CALL has exactly one ordered reference before encoding", record);
                marked += 1;
            }
        }
        ready &= helper148_expect(&context->report, marked == (body == BUSTER_HELPER148_HELPERS ? emitted->reference_count : 0),
            "marked-record count is bounded by encoded fixup storage", body);
        const BranchSizingCheck checked = branch_sizing_validate(records, count, branches, targets, branch_count);
        ready &= helper148_expect(&context->report, checked.error == BRANCH_SIZING_OK, "native branch table is valid", body);
        if (ready)
        {
            const BranchSizing sized = size_branches(records, count, branches, targets, branch_count, offsets);
            ready = helper148_expect(&context->report, sized.error == BRANCH_SIZING_OK && x86_target_validate(context->allowed, records, count) == count,
                "branch-sized complete image obeys baseline", body);
        }
        if (ready)
        {
            const int64_t vector_bytes = x86_encode(records, count, native->mapping + starts[body], capacities[body], NULL, ends);
            const int64_t scalar_bytes = x86_encode_scalar(records, count, scalar + starts[body], capacities[body], NULL, scalar_ends);
            ready = helper148_expect(&context->report, vector_bytes > 0 && vector_bytes == scalar_bytes && (uint64_t)vector_bytes <= capacities[body] &&
                memcmp(native->mapping + starts[body], scalar + starts[body], (uint64_t)vector_bytes) == 0, "both encoders agree on actual leaf/caller bytes", body);
            if (ready && body == BUSTER_HELPER148_HELPERS)
            {
                ready &= helper148_expect(&context->report, marked == emitted->reference_count && memcmp(ends, scalar_ends, marked * sizeof(uint32_t)) == 0,
                    "call references and encoded field ends agree", body);
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
                        fixup->field == X86_FIXUP_DISPLACEMENT && fixup->addend == 0, "resolve source SymbolId through actual helper map", reference);
                    if (ready)
                    {
                        const uint32_t end = ends[reference];
                        const uint32_t distance = x86_fixup_distance(records[fixup->instruction].form, fixup->field);
                        const int64_t displacement = (int64_t)starts[helper] - (int64_t)(starts[body] + end);
                        ready = helper148_expect(&context->report, distance == 4 && end >= distance && end <= (uint64_t)vector_bytes &&
                            displacement >= INT32_MIN && displacement <= INT32_MAX, "helper rel32 field and displacement are bounded", reference);
                        if (ready)
                        {
                            const int32_t relative = (int32_t)displacement;
                            memcpy(native->mapping + starts[body] + end - distance, &relative, sizeof(relative));
                            memcpy(scalar + starts[body] + end - distance, &relative, sizeof(relative));
                            context->report.calls += 1;
                        }
                    }
                }
            }
        }
    }
    if (ready)
    {
        native->entry = native->mapping + starts[BUSTER_HELPER148_HELPERS];
        ready = helper148_expect(&context->report, mprotect(native->mapping, pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(native->mapping + pages, BUSTER_HELPER148_PAGE, PROT_NONE) == 0, "make patched native image RX with a guard", 0);
    }
    free(scalar);
    free(offsets);
    return ready;
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
            .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_R8),
            .frame_flags = BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE,
            .vector = vector,
            .rematerialize = true,
        };
        const Tier1EmitError error = tier1_emit_function(&plan, &options, context->allowed, context->arenas + 5, context->arenas + 7, &emitted, &node);
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

static __attribute__((no_sanitize("function"))) uint64_t helper148_invoke(const Helper148Native* native, uintptr_t address, uint64_t value, int store_order, int load_order, uint64_t seed)
{
    uint64_t (*call)(uint64_t, uint64_t, int, int, uint64_t) = (uint64_t (*)(uint64_t, uint64_t, int, int, uint64_t))(void*)native->entry;
    const uint64_t result = call(address, value, store_order, load_order, seed);
    return result;
}

static void helper148_memory(Helper148Report* report, Helper148Native programs[2][BUSTER_HELPER148_FIXTURES])
{
    static const uint64_t initial[] = { 0, UINT64_C(0x8080808080808080), UINT64_MAX, UINT64_C(0x0123456789abcdef) };
    static const uint64_t written[] = { UINT64_C(0x89abcdef01234567), 1, 0, UINT64_MAX };
    static const int stores[] = { __ATOMIC_RELAXED, __ATOMIC_RELEASE, __ATOMIC_SEQ_CST };
    static const int loads[] = { __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE, __ATOMIC_SEQ_CST };
    uint8_t* mapping = mmap(NULL, BUSTER_HELPER148_PAGE * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED;
    const bool writable = mapped && mprotect(mapping + BUSTER_HELPER148_PAGE, BUSTER_HELPER148_PAGE, PROT_READ | PROT_WRITE) == 0;
    if (helper148_expect(report, mapped && writable, "guard both sides of naturally aligned native objects", 0))
    {
        uint8_t* page = mapping + BUSTER_HELPER148_PAGE;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t width = 0; width < 4; width += 1)
            {
                const uint32_t bytes = 1u << width;
                const uint64_t mask = UINT64_MAX >> (64 - bytes * 8);
                const uint32_t positions[] = { 0, 64 + bytes, BUSTER_HELPER148_PAGE - bytes };
                for (uint32_t position = 0; position < BUSTER_ARRAY_LENGTH(positions); position += 1)
                {
                    const uint32_t at = positions[position];
                    for (uint32_t pattern = 0; pattern < BUSTER_ARRAY_LENGTH(initial); pattern += 1)
                    {
                        for (uint32_t store = 0; store < BUSTER_ARRAY_LENGTH(stores); store += 1)
                        {
                            for (uint32_t load = 0; load < BUSTER_ARRAY_LENGTH(loads); load += 1)
                            {
                                memset(page, 0xa5, BUSTER_HELPER148_PAGE);
                                uint8_t expected[BUSTER_HELPER148_PAGE];
                                memset(expected, 0xa5, sizeof(expected));
                                for (uint32_t byte = 0; byte < bytes; byte += 1)
                                {
                                    page[at + byte] = (uint8_t)(initial[pattern] >> (byte * 8));
                                    expected[at + byte] = (uint8_t)(written[pattern] >> (byte * 8));
                                }
                                const uint64_t seed = UINT64_C(0xfedcba9876543210) + pattern * 17 + position;
                                const uint64_t answer = ((initial[pattern] & mask) ^ (written[pattern] & mask)) + seed;
                                const uint64_t actual = helper148_invoke(&programs[path][width], (uintptr_t)(page + at), written[pattern], stores[store], loads[load], seed);
                                helper148_expect(report, actual == answer && memcmp(page, expected, sizeof(expected)) == 0,
                                    "actual Tier1 leaf CALL sequence returns literal raw bits and writes only object bytes", path * 10000 + width * 1000 + position * 100 + pattern * 10 + store);
                                report->native_results += 1;
                            }
                        }
                    }
                }
            }
        }
    }
    if (mapped)
    {
        munmap(mapping, BUSTER_HELPER148_PAGE * 3);
    }
}

static uint16_t helper148_control(void)
{
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
    return control;
}

static __attribute__((noinline)) int helper148_clang_rounds(void)
{
    __asm__ volatile("" : : : "memory");
    const int result = __builtin_flt_rounds();
    return result;
}

static __attribute__((no_sanitize("function"))) void helper148_rounds(Helper148Report* report, Helper148Native programs[2][BUSTER_HELPER148_FIXTURES])
{
    const uint16_t original_control = helper148_control();
    const uint32_t original_mxcsr = _mm_getcsr();
    static const uint32_t expected[] = { 1, 3, 2, 0 };
    for (uint32_t x87 = 0; x87 < 4; x87 += 1)
    {
        const uint16_t control = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | (x87 << 10));
        for (uint32_t sse = 0; sse < 4; sse += 1)
        {
            const uint32_t mxcsr = (original_mxcsr & ~UINT32_C(0x6000)) | (sse << 13);
            __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
            _mm_setcsr(mxcsr);
            const int oracle = helper148_clang_rounds();
            helper148_expect(report, oracle == (int)expected[x87], "Clang x87 rounding oracle under divergent controls", x87 * 4 + sse);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                uint32_t (*plain)(void) = (uint32_t (*)(void))(void*)programs[path][4].entry;
                uint64_t (*live)(uint64_t) = (uint64_t (*)(uint64_t))(void*)programs[path][5].entry;
                const uint64_t seed = UINT64_C(0xfffffffeddccbbaa) + x87;
                const uint32_t result = plain();
                const uint64_t survived = live(seed);
                helper148_expect(report, result == expected[x87] && survived == seed + expected[x87] && result == (uint32_t)oracle,
                    "real Tier1 I32 return and live-across repeated rounding CALLs", path * 100 + x87 * 4 + sse);
                helper148_expect(report, helper148_control() == control && _mm_getcsr() == mxcsr, "native callers preserve both control settings", path);
                report->native_results += 2;
            }
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(original_control) : "memory");
    _mm_setcsr(original_mxcsr);
    helper148_expect(report, helper148_control() == original_control && _mm_getcsr() == original_mxcsr, "native child restores its original controls", 0);
}

int main(int argc, char** argv)
{
    (void)argv;
    Helper148Context context = { 0 };
    bool ready = argc == 1 && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && tier0_initialize() && tier0_tables_open(&context.tables);
    context.leaves = arena_reserve(1u << 20);
    for (uint32_t arena = 0; arena < BUSTER_HELPER148_ARENAS; arena += 1)
    {
        context.arenas[arena] = arena_reserve(8u << 20);
        ready = context.arenas[arena].base != NULL && ready;
    }
    ready = helper148_expect(&context.report, ready && context.leaves.base != NULL, "initialize standalone production pipeline", 0);
    const X86Target baseline = { .architecture = X86_ARCH_BASELINE };
    x86_target_forms(&baseline, context.allowed);
    ready = ready && helper148_leaves(&context);
    Helper148Native programs[2][BUSTER_HELPER148_FIXTURES] = { 0 };
    for (uint32_t fixture = 0; ready && fixture < BUSTER_HELPER148_FIXTURES; fixture += 1)
    {
        Helper148Fixture input;
        helper148_fixture(&input, context.helpers, fixture);
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            ready = helper148_compile(&context, &input, path != 0, &programs[path][fixture]);
        }
    }
    Helper148Report* shared = MAP_FAILED;
    if (ready)
    {
        shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        ready = helper148_expect(&context.report, shared != MAP_FAILED, "allocate native result report", 0);
    }
    if (ready)
    {
        fflush(NULL);
        const pid_t child = fork();
        if (child == 0)
        {
            const struct rlimit no_core = { 0 };
            sigset_t unblocked;
            sigemptyset(&unblocked);
            if (signal(SIGALRM, SIG_DFL) == SIG_ERR || sigprocmask(SIG_SETMASK, &unblocked, NULL) != 0 || setrlimit(RLIMIT_CORE, &no_core) != 0)
            {
                _exit(98);
            }
            alarm(15);
            helper148_memory(shared, programs);
            helper148_rounds(shared, programs);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        } while (waited < 0 && errno == EINTR);
        helper148_expect(&context.report, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "bounded actual-helper native child completes", (uint32_t)status);
    }
    if (shared != MAP_FAILED)
    {
        context.report.checks += shared->checks;
        context.report.failures += shared->failures;
        context.report.native_results += shared->native_results;
        munmap(shared, sizeof(*shared));
    }
    helper148_expect(&context.report, context.report.native_results == 2 * 4 * 3 * 4 * 3 * 4 + 2 * 16 * 2 && context.report.calls == 2 * (4 * 6 + 1 + 2),
        "all original helper calls and independent native cases completed", 0);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t fixture = 0; fixture < BUSTER_HELPER148_FIXTURES; fixture += 1)
        {
            if (programs[path][fixture].mapping != NULL && programs[path][fixture].mapping != MAP_FAILED)
            {
                munmap(programs[path][fixture].mapping, programs[path][fixture].mapping_bytes);
            }
        }
    }
    for (uint32_t arena = 0; arena < BUSTER_HELPER148_ARENAS; arena += 1)
    {
        arena_release(context.arenas + arena);
    }
    tier0_tables_close(&context.tables);
    arena_release(&context.leaves);
    printf("helper-tier1-148: %" PRIu64 " checks, %" PRIu64 " failures; native=%" PRIu64 " resolved-calls=%" PRIu64 "; composition=19b9454dc0e4fde5596a16efc917f17fa4ce190a\n",
        context.report.checks, context.report.failures, context.report.native_results, context.report.calls);
    const int result = context.report.failures != 0;
    return result;
}
