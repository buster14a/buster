// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent #148 math consumer harness. Include ONLY the immutable composition
// 22c92dd44476c4e51f371ad29fd9ec06fcf84953 (19b9454d + qualified math 8470e13).
// This file is original C; it borrows production interfaces and actual math leaf emitters, not tests.
// Signature-only helper declarations feed the existing ABI batch; they have no mock executable typed bodies.
// Every CALLER traverses typed lowering, SSA promotion, SCCP/DCE/CFG optimization, dominance verification,
// selection, liveness/allocation/frame emission, branch sizing, both encoders and real symbolic CALL fixups.
// The helpers retain their original native record implementations. Source dispatch/intrinsic lowering and
// protected integration remain separate acceptance work. Each caller has a discarded first call, a second used
// call and nonconstant arguments/seed live across clobbers. Every native image is separately encoded and executed
// by each encoder, with all fourteen original leaf bodies and actual callee-symbol rel32 resolution.
//
// Compile on the approved 9700X under its normal lock. From the directory containing this file and the immutable
// builtin-tier1-acceptance checkout, the exact relative command is:
// clang -O3 -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function -I./builtin-tier1-acceptance/src ./helper-tier1-math-148.c -pthread -ldl -o ./helper-tier1-math-148
// -Wno-unused-function permits unused static production entry points in this standalone inclusion unit.
// Host libm is opened only as the independent native numeric oracle; generated helpers call no host function.
// FP changes stay in a signal-bounded child. Main sqrt acceptance supplies the current thread's actual &errno.
// Additional raw-ABI memory diagnostics use real int32 cells at guarded page edges; those do not claim source
// TLS binding. No benchmark, source mutation, mock helper body or runtime dispatch to another compiler tier runs.

#include <assert.h>
#include <dlfcn.h>
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
#include "buster/compiler/c_builtin_math_helpers.c"
#include "buster/compiler/ir.c"
#include "buster/compiler/ir_lower.c"
#include "buster/compiler/ssa_ir.c"
#include "buster/compiler/ir_optimize.c"
#include "buster/compiler/tier1_select.c"
#include "buster/compiler/tier1_liveness.c"
#include "buster/compiler/tier1_emit.c"
#include "buster/compiler/branch_sizing.c"

#define BUSTER_HELPER148_HELPERS 14u
#define BUSTER_HELPER148_FIXTURES 14u
#define BUSTER_HELPER148_ENTRIES 64u
#define BUSTER_HELPER148_CALLS 2u
#define BUSTER_HELPER148_ARGUMENTS 16u
#define BUSTER_HELPER148_PAGE 4096u
#define BUSTER_HELPER148_ARENAS 8u
#define BUSTER_HELPER148_STATUS UINT32_C(0x003f)
#define BUSTER_HELPER148_PRECISION UINT32_C(0x0020)
#define BUSTER_HELPER148_MASKS UINT32_C(0x1f80)

_Static_assert(sizeof(int) == 4, "Linux SysV errno cell width");

typedef struct Helper148Leaf Helper148Leaf;
struct Helper148Leaf
{
    SymbolId symbol;
    CBuiltinId id;
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
    uint8_t* mapping[2];
    uint8_t* entry[2];
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
    void* library;
    void* oracles[BUSTER_HELPER148_HELPERS];
};

static const char* helper148_names[BUSTER_HELPER148_HELPERS] =
{
    "sqrt",
    "sqrtf",
    "copysign",
    "copysignf",
    "ceil",
    "ceilf",
    "floor",
    "floorf",
    "trunc",
    "truncf",
    "nearbyint",
    "nearbyintf",
    "rint",
    "rintf",
};

static bool helper148_expect(Helper148Report* report, bool condition, const char* message, uint32_t item)
{
    report->checks += 1;
    if (!condition)
    {
        if (report->failures < 24)
        {
            fprintf(stderr, "helper-tier1-math-148: %s (item=%u)\n", message, item);
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
        leaf->id = (CBuiltinId)((uint32_t)CBUILTIN_SQRT + i);
        const CBuiltinMathHelper emitted = cbuiltin_math_helper(leaf->id, &context->leaves);
        leaf->code = emitted.code;
        leaf->parameter_count = emitted.parameter_count;
        leaf->result_class = emitted.result_class;
        memcpy(leaf->parameter_classes, emitted.parameter_classes, sizeof(emitted.parameter_classes));
        const bool copy_sign = i == 2 || i == 3;
        ready &= helper148_expect(&context->report, emitted.parameter_count == (i < 4 ? 2u : 1u) &&
            emitted.result_class == BUSTER_TYPED_CLASS_U64 && emitted.parameter_classes[0] == BUSTER_TYPED_CLASS_U64 &&
            emitted.parameter_classes[1] == (i < 4 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID) &&
            emitted.compiler_barrier && emitted.reads_fp_environment == !copy_sign && emitted.may_raise_fp_exceptions == !copy_sign &&
            emitted.may_raise_inexact == (i < 2 || i >= 12) && emitted.requires_errno_address == (i < 2),
            "actual raw-U64 helper signature and FP/errno effect contract", i);
        ready &= helper148_expect(&context->report, leaf->code.error == TIER0_OK && leaf->code.count != 0 &&
            leaf->code.count <= BUSTER_CBUILTIN_MATH_RECORDS && leaf->code.branch_count <= BUSTER_CBUILTIN_MATH_BRANCHES &&
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
    const uint32_t parameters = 3;
    const uint8_t locals[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
    };
    assert(kind < BUSTER_HELPER148_HELPERS && first_local + BUSTER_ARRAY_LENGTH(locals) <= program->local_capacity);
    memcpy(fixture->local_classes + first_local, locals, sizeof(locals));
    program->local_count += BUSTER_ARRAY_LENGTH(locals);
    const uint32_t arguments[] =
    {
        0,
        1,
    };
    helper148_call(fixture, leaves, kind, arguments);
    tier0_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
    helper148_call(fixture, leaves, kind, arguments);
    tier0_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
    tier0_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
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

static __attribute__((no_sanitize("function"))) uint64_t helper148_invoke(const Helper148Native* native, uint32_t encoder, uint64_t bits, uint64_t second, uint64_t seed)
{
    uint64_t (*call)(uint64_t, uint64_t, uint64_t) = (uint64_t (*)(uint64_t, uint64_t, uint64_t))(void*)native->entry[encoder];
    const uint64_t result = call(bits, second, seed);
    return result;
}

static uint16_t helper148_control(void)
{
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
    return control;
}

static __attribute__((noinline)) uint64_t helper148_oracle(const Helper148Context* context, uint32_t helper, uint64_t bits, uint64_t sign, int* error)
{
    const int saved = errno;
    errno = *error;
    uint64_t result = 0;
    if ((helper & 1) != 0)
    {
        const uint32_t input_bits = (uint32_t)bits;
        const uint32_t sign_bits = (uint32_t)sign;
        float input;
        float sign_input;
        memcpy(&input, &input_bits, sizeof(input));
        memcpy(&sign_input, &sign_bits, sizeof(sign_input));
        const float output = helper == 3 ? ((float (*)(float, float))context->oracles[helper])(input, sign_input)
                                         : ((float (*)(float))context->oracles[helper])(input);
        uint32_t output_bits;
        memcpy(&output_bits, &output, sizeof(output_bits));
        result = output_bits;
    }
    else
    {
        double input;
        double sign_input;
        memcpy(&input, &bits, sizeof(input));
        memcpy(&sign_input, &sign, sizeof(sign_input));
        const double output = helper == 2 ? ((double (*)(double, double))context->oracles[helper])(input, sign_input)
                                          : ((double (*)(double))context->oracles[helper])(input);
        memcpy(&result, &output, sizeof(result));
    }
    *error = errno;
    errno = saved;
    return result;
}

static void helper148_observe(Helper148Report* report, uint32_t helper, uint32_t item, uint64_t actual, uint64_t expected, int actual_errno, int expected_errno,
    uint32_t state, uint32_t expected_state, uint32_t environment, uint16_t control)
{
    // C exposes five FP flags; the architecture-specific denormal-operand flag is outside that oracle contract.
    // Ceil/floor/trunc may additionally raise precision. Nearbyint's prohibition and rint/sqrt precision remain exact.
    const uint32_t mask = (BUSTER_HELPER148_STATUS & ~UINT32_C(0x02)) &
        (helper >= 4 && helper <= 9 ? ~BUSTER_HELPER148_PRECISION : UINT32_MAX);
    const bool value = actual == expected && actual_errno == expected_errno;
    const bool controls = (state & ~BUSTER_HELPER148_STATUS) == (environment & ~BUSTER_HELPER148_STATUS) && helper148_control() == control;
    const bool flags = (state & mask) == (expected_state & mask) && (state & (environment & BUSTER_HELPER148_STATUS)) == (environment & BUSTER_HELPER148_STATUS);
    if ((!value || !controls || !flags) && report->failures < 24)
    {
        fprintf(stderr, "helper-tier1-math-148: %s item=%u result=%016" PRIx64 "/%016" PRIx64 " errno=%d/%d mxcsr=%04x/%04x control=%04x\n",
            helper148_names[helper], item, actual, expected, actual_errno, expected_errno, state, expected_state, control);
    }
    helper148_expect(report, value, "libm raw result plus preserved live seed and exact errno", item);
    helper148_expect(report, controls && flags, "actual Tier1 caller preserves FP controls, old flags and required new exceptions", item);
    if (helper == 10 || helper == 11)
    {
        helper148_expect(report, (state & BUSTER_HELPER148_PRECISION) == (environment & BUSTER_HELPER148_PRECISION),
            "nearbyint repeated calls never add or clear precision", item);
    }
    report->native_results += 1;
}

static void helper148_numeric(const Helper148Context* context, Helper148Report* report, Helper148Native programs[2][BUSTER_HELPER148_FIXTURES])
{
    static const struct
    {
        uint32_t single;
        uint64_t wide;
    } values[] =
    {
        { UINT32_C(0x00000000), UINT64_C(0x0000000000000000) },
        { UINT32_C(0x80000000), UINT64_C(0x8000000000000000) },
        { UINT32_C(0x3f000000), UINT64_C(0x3fe0000000000000) },
        { UINT32_C(0xbf000000), UINT64_C(0xbfe0000000000000) },
        { UINT32_C(0x40000000), UINT64_C(0x4000000000000000) },
        { UINT32_C(0xc0200000), UINT64_C(0xc004000000000000) },
        { UINT32_C(0x3fc00000), UINT64_C(0x3ff8000000000000) },
        { UINT32_C(0x80000001), UINT64_C(0x8000000000000001) },
        { UINT32_C(0x7fc12345), UINT64_C(0x7ff8123456789abc) },
        { UINT32_C(0x7f812345), UINT64_C(0x7ff0123456789abc) },
    };
    const uint16_t original_control = helper148_control();
    const uint32_t original_mxcsr = _mm_getcsr();
    const int original_errno = errno;
    uint8_t* mapping = mmap(NULL, 3 * BUSTER_HELPER148_PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED;
    const bool guarded = mapped && mprotect(mapping + BUSTER_HELPER148_PAGE, BUSTER_HELPER148_PAGE, PROT_READ | PROT_WRITE) == 0;
    helper148_expect(report, guarded, "guard both page edges around independent errno-cell memory diagnostics", 0);
    for (uint32_t rc = 0; rc < 4; rc += 1)
    {
        // Deliberately disagree with MXCSR for every case: these raw binary32/binary64 leaves use SSE control.
        const uint16_t control = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | (((rc + 1) & 3) << 10) | UINT16_C(0x003f));
        __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            const uint32_t old_flags = ((rc + mode) & 1) != 0 ? UINT32_C(0x21) : 0;
            const uint32_t environment = (original_mxcsr & ~(BUSTER_HELPER148_STATUS | UINT32_C(0x6000) | UINT32_C(0x8040))) |
                BUSTER_HELPER148_MASKS | (rc << 13) | ((mode & 1) != 0 ? UINT32_C(0x40) : 0) |
                ((mode & 2) != 0 ? UINT32_C(0x8000) : 0) | old_flags;
            for (uint32_t helper = 0; helper < BUSTER_HELPER148_HELPERS; helper += 1)
            {
                const bool single = (helper & 1) != 0;
                for (uint32_t value = 0; value < BUSTER_ARRAY_LENGTH(values); value += 1)
                {
                    const uint64_t bits = single ? UINT64_C(0xfeedbeef00000000) | values[value].single : values[value].wide;
                    const uint64_t sign = single ? UINT64_C(0xdeadbeef00000000) | ((value & 1) != 0 ? UINT32_C(0x00000000) : UINT32_C(0x80000000))
                                                 : (value & 1) != 0 ? UINT64_C(0x0000000000000000) : UINT64_C(0x8000000000000000);
                    int expected_errno = 0x1357;
                    _mm_setcsr(environment);
                    (void)helper148_oracle(context, helper, bits, sign, &expected_errno);
                    const uint64_t expected_bits = helper148_oracle(context, helper, bits, sign, &expected_errno);
                    const uint32_t expected_state = _mm_getcsr();
                    helper148_expect(report, helper148_control() == control, "host libm oracle retains divergent x87 control", helper);
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        for (uint32_t encoder = 0; encoder < 2; encoder += 1)
                        {
                            const uint32_t item = helper * 100000 + rc * 10000 + mode * 1000 + value * 10 + path * 2 + encoder;
                            const uint64_t seed = UINT64_C(0x8877665544332211) + item;
                            errno = 0x1357;
                            const uint64_t second = helper < 2 ? (uint64_t)(uintptr_t)&errno : sign;
                            _mm_setcsr(environment);
                            const uint64_t actual = helper148_invoke(&programs[path][helper], encoder, bits, second, seed);
                            const uint32_t state = _mm_getcsr();
                            const int actual_errno = errno;
                            helper148_observe(report, helper, item, actual, expected_bits + seed, actual_errno, expected_errno,
                                state, expected_state, environment, control);
                            // Separate address-boundary checks do not substitute for the actual TLS errno calls above.
                            if (guarded && helper < 2 && rc == 0 && mode == 0 && (value == 3 || value == 4 || value == 8))
                            {
                                uint8_t* page = mapping + BUSTER_HELPER148_PAGE;
                                const uint32_t positions[] = { 0, BUSTER_HELPER148_PAGE - sizeof(int) };
                                for (uint32_t edge = 0; edge < BUSTER_ARRAY_LENGTH(positions); edge += 1)
                                {
                                    uint8_t expected_page[BUSTER_HELPER148_PAGE];
                                    memset(page, 0xa5, BUSTER_HELPER148_PAGE);
                                    memset(expected_page, 0xa5, sizeof(expected_page));
                                    int* cell = (int*)(page + positions[edge]);
                                    *cell = 0x1357;
                                    memcpy(expected_page + positions[edge], &expected_errno, sizeof(expected_errno));
                                    errno = 0x2468;
                                    _mm_setcsr(environment);
                                    const uint64_t guarded_result = helper148_invoke(&programs[path][helper], encoder, bits, (uint64_t)(uintptr_t)cell, seed);
                                    const uint32_t guarded_state = _mm_getcsr();
                                    helper148_observe(report, helper, item, guarded_result, expected_bits + seed, *cell, expected_errno,
                                        guarded_state, expected_state, environment, control);
                                    helper148_expect(report, errno == 0x2468 && memcmp(page, expected_page, sizeof(expected_page)) == 0,
                                        "sqrt writes only the supplied four-byte guarded errno cell and preserves unrelated TLS errno", item);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(original_control) : "memory");
    _mm_setcsr(original_mxcsr);
    errno = original_errno;
    helper148_expect(report, helper148_control() == original_control && _mm_getcsr() == original_mxcsr && errno == original_errno,
        "native child restores its original FP controls, status and errno", 0);
    if (mapped)
    {
        munmap(mapping, 3 * BUSTER_HELPER148_PAGE);
    }
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
    context.library = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    ready = helper148_expect(&context.report, context.library != NULL, "load independent host libm oracle", 0) && ready;
    for (uint32_t helper = 0; ready && helper < BUSTER_HELPER148_HELPERS; helper += 1)
    {
        context.oracles[helper] = dlsym(context.library, helper148_names[helper]);
        ready = helper148_expect(&context.report, context.oracles[helper] != NULL, "resolve exact host math oracle symbol", helper);
    }
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
            helper148_numeric(&context, shared, programs);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        } while (child > 0 && waited < 0 && errno == EINTR);
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
    helper148_expect(&context.report, context.report.native_results == 2 * 2 * 14 * 4 * 4 * 10 + 2 * 2 * 2 * 3 * 2 &&
        context.report.calls == 2 * 2 * 14 * 2,
        "all original helper calls and independent native cases completed", 0);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t fixture = 0; fixture < BUSTER_HELPER148_FIXTURES; fixture += 1)
        {
            for (uint32_t encoder = 0; encoder < 2; encoder += 1)
            {
                if (programs[path][fixture].mapping[encoder] != NULL && programs[path][fixture].mapping[encoder] != MAP_FAILED)
                {
                    munmap(programs[path][fixture].mapping[encoder], programs[path][fixture].mapping_bytes);
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
    if (context.library != NULL)
    {
        dlclose(context.library);
    }
    printf("helper-tier1-math-148: %" PRIu64 " checks, %" PRIu64 " failures; native=%" PRIu64 " resolved-calls=%" PRIu64 "; composition=22c92dd44476c4e51f371ad29fd9ec06fcf84953\n",
        context.report.checks, context.report.failures, context.report.native_results, context.report.calls);
    const int result = context.report.failures != 0;
    return result;
}
