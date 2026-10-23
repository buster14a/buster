// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original real-IR/selector tests for #143's selected-use adapter. The independent oracle searches CFG paths from
// each block for a use before definition, rather than iterating the production bitset equations. Corpus source
// lowering is not represented by these fixtures. Include after dominators.c and tier1_liveness.c.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct Tier1LivenessTest Tier1LivenessTest;
struct Tier1LivenessTest
{
    uint64_t checks;
    uint64_t failures;
    uint32_t seed;
    Arena input;
    Arena left;
    Arena right;
    Arena scratch;
    uint8_t* allowed;
};

static void tier1_liveness_test_expect(Tier1LivenessTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 12)
        {
            fprintf(stderr, "tier1-liveness: %s (seed=%u)\n", what, test->seed);
        }
        test->failures += 1;
    }
}

static void tier1_liveness_test_node(IrFunction* function, uint32_t node, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    function->op[node] = (uint8_t)op;
    function->type[node] = (uint8_t)type;
    function->a[node] = a;
    function->b[node] = b;
    function->c[node] = c;
}

static bool tier1_liveness_test_plan(Tier1LivenessTest* test, const IrFunction* function, Tier1SelectPlan* plan, IrCfg* cfg)
{
    bool valid = ir_build_cfg(function, &test->input, &test->scratch, cfg) == IR_OK;
    Dominators dominators = { 0 };
    if (valid)
    {
        const DominatorCfg graph = {
            .block_count = cfg->block_count,
            .edge_count = cfg->edge_count,
            .entry = 0,
            .successor_offsets = cfg->successor_offsets,
            .successors = cfg->successors,
            .predecessor_offsets = cfg->predecessor_offsets,
            .predecessors = cfg->predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        valid = dominators_analyze(&test->input, &test->scratch, &graph, options, &dominators) == DOMINATOR_OK;
    }
    if (valid)
    {
        const IrDominance view = {
            .enter = dominators.preorder,
            .leave = dominators.subtree_end,
            .block_count = cfg->block_count,
        };
        uint32_t error_node;
        valid = tier1_select_plan(function, &view, &test->input, &test->scratch, true, plan, &error_node) == TIER1_SELECT_OK;
        if (!valid && test->failures < 12)
        {
            fprintf(stderr, "tier1-liveness: planner rejected node %u\n", error_node);
        }
    }
    tier1_liveness_test_expect(test, valid, "real IR, dominators and selector plan accepted");
    return valid;
}

static bool tier1_liveness_test_reaches_use(const Tier1SelectPlan* plan, const IrCfg* cfg, uint32_t value, uint32_t begin, uint8_t* seen, uint32_t* pending)
{
    memset(seen, 0, cfg->block_count);
    uint32_t count = 1;
    pending[0] = begin;
    seen[begin] = 1;
    bool found = false;
    const IrFunction* function = plan->function;
    while (!found && count != 0)
    {
        const uint32_t block = pending[--count];
        // The supported SSA subset has no uses before the definition in its own block and no phi edges.
        if (cfg->instruction_blocks[value] != block || function->op[value] == IR_PARAM)
        {
            for (uint32_t node = function->block_offsets[block]; !found && node < function->block_offsets[block + 1]; node += 1)
            {
                for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
                {
                    found |= plan->effective_use_values[use] == value;
                }
            }
            for (uint32_t edge = cfg->successor_offsets[block]; !found && edge < cfg->successor_offsets[block + 1]; edge += 1)
            {
                const uint32_t next = cfg->successors[edge];
                if (!seen[next])
                {
                    pending[count++] = next;
                    seen[next] = 1;
                }
            }
        }
    }
    return found;
}

static void tier1_liveness_test_oracle(Tier1LivenessTest* test, const Tier1SelectPlan* plan, const IrCfg* cfg, const Tier1Liveness* live)
{
    const IrFunction* function = plan->function;
    uint8_t* seen = malloc(cfg->block_count);
    uint32_t* pending = malloc((uint64_t)cfg->block_count * sizeof(uint32_t));
    const bool allocated = seen && pending;
    tier1_liveness_test_expect(test, allocated, "independent graph-search scratch allocated");
    uint32_t cross_values = 0;
    for (uint32_t interval = 0; allocated && interval < live->allocation.count; interval += 1)
    {
        const uint32_t value = live->allocation.values[interval];
        uint32_t first = function->op[value] == IR_PARAM ? 0 : value * 2 + 1;
        uint32_t end = first + 1;
        uint32_t uses = 0;
        bool cross = false;
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                if (plan->effective_use_values[use] == value)
                {
                    const uint32_t position = node * 2;
                    const uint32_t offset = live->allocation.use_offsets[interval] + uses;
                    tier1_liveness_test_expect(test, offset < live->allocation.use_offsets[interval + 1] && live->allocation.use_positions[offset] == position, "retained CSR is the exact sorted selected-use sequence");
                    uses += 1;
                    end = end > position ? end : position + 1;
                    cross |= cfg->instruction_blocks[value] != cfg->instruction_blocks[node];
                    cross |= function->op[value] == IR_PARAM && cfg->predecessor_offsets[1] != 0;
                }
            }
        }
        tier1_liveness_test_expect(test, uses == live->allocation.use_offsets[interval + 1] - live->allocation.use_offsets[interval], "no invented or dropped uses");
        if (cross)
        {
            cross_values += 1;
            for (uint32_t block = 0; block < cfg->block_count; block += 1)
            {
                if (tier1_liveness_test_reaches_use(plan, cfg, value, block, seen, pending))
                {
                    const uint32_t boundary = function->block_offsets[block] * 2;
                    first = first < boundary ? first : boundary;
                }
                bool out = false;
                for (uint32_t edge = cfg->successor_offsets[block]; !out && edge < cfg->successor_offsets[block + 1]; edge += 1)
                {
                    out = tier1_liveness_test_reaches_use(plan, cfg, value, cfg->successors[edge], seen, pending);
                }
                if (out)
                {
                    const uint32_t boundary = function->block_offsets[block + 1] * 2;
                    end = end > boundary ? end : boundary;
                }
            }
        }
        tier1_liveness_test_expect(test, live->allocation.starts[interval] == first && live->allocation.ends[interval] == end, "interval envelope equals independent path-search liveness");
        if (function->op[value] == IR_PARAM)
        {
            tier1_liveness_test_expect(test, first == 0 && live->allocation.allowed_registers[interval] == tier1_liveness_parameter_mask(function->a[value]), "incoming parameter is live from entry in its SysV register");
        }
    }
    tier1_liveness_test_expect(test, cross_values == live->cross_block_values, "only actual cross-block selected uses enter the dense matrix");
    free(pending);
    free(seen);
}

static bool tier1_liveness_test_compare(Tier1LivenessTest* test, const Tier1SelectPlan* plan, const IrCfg* cfg, Tier1Liveness* scalar, Tier1Liveness* vector)
{
    arena_pop_to(&test->left, 0);
    arena_pop_to(&test->right, 0);
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1LivenessError left = tier1_liveness_build(plan, &test->left, &test->scratch, false, scalar);
    tier1_liveness_test_expect(test, test->scratch.length == scratch_mark, "scalar adapter restores scratch");
    const Tier1LivenessError right = tier1_liveness_build(plan, &test->right, &test->scratch, true, vector);
    tier1_liveness_test_expect(test, test->scratch.length == scratch_mark, "vector adapter restores scratch");
    bool valid = left == TIER1_LIVENESS_OK && right == TIER1_LIVENESS_OK;
    tier1_liveness_test_expect(test, valid, "both adapters accept the real selected plan");
    if (valid)
    {
        const uint64_t bytes = (uint64_t)scalar->allocation.count * sizeof(uint32_t);
        valid = scalar->allocation.count == vector->allocation.count && scalar->allocation.use_count == vector->allocation.use_count &&
                scalar->cross_block_values == vector->cross_block_values && scalar->iterations == vector->iterations &&
                scalar->matrix_bytes == vector->matrix_bytes && scalar->retained_bytes == vector->retained_bytes;
        tier1_liveness_test_expect(test, valid, "scalar/vector sizes, dense values and fixpoint iterations agree");
        if (valid)
        {
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.starts, vector->allocation.starts, bytes) == 0, "scalar/vector interval starts agree");
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.ends, vector->allocation.ends, bytes) == 0, "scalar/vector interval ends agree");
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.values, vector->allocation.values, bytes) == 0, "scalar/vector value identities agree");
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.allowed_registers, vector->allocation.allowed_registers, bytes) == 0, "scalar/vector register constraints agree");
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.use_offsets, vector->allocation.use_offsets, (uint64_t)(scalar->allocation.count + 1) * 4) == 0, "scalar/vector use offsets agree");
            tier1_liveness_test_expect(test, memcmp(scalar->allocation.use_positions, vector->allocation.use_positions, (uint64_t)scalar->allocation.use_count * 4) == 0, "scalar/vector use positions agree");
            tier1_liveness_test_oracle(test, plan, cfg, vector);
        }
    }
    return valid;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_liveness_test_execute(const uint8_t* code, uint64_t a, uint64_t b)
{
    return ((uint64_t (*)(uint64_t, uint64_t))code)(a, b);
}

static void tier1_liveness_test_emit(Tier1LivenessTest* test, const Tier1SelectPlan* plan, const Tier1Liveness* live, Arena* arena, bool vector, bool execute)
{
    Tier1Allocation allocation;
    const Tier1AllocationError allocated = vector ? tier1_allocate(&live->allocation, arena, &allocation) : tier1_allocate_scalar(&live->allocation, arena, &allocation);
    const bool valid = allocated == TIER1_ALLOCATION_OK && tier1_allocation_verify(&live->allocation, &allocation);
    tier1_liveness_test_expect(test, valid, "actual allocator verifies the selected-use intervals");
    if (valid)
    {
        Tier1SelectCode code;
        uint32_t error_node;
        const Tier1SelectError emitted = tier1_select_emit(plan, allocation.value_registers, test->allowed, arena, &code, &error_node);
        tier1_liveness_test_expect(test, emitted == TIER1_SELECT_OK && x86_validate(code.records, code.count) == code.count, "actual selector consumes allocated registers");
        if (execute && emitted == TIER1_SELECT_OK)
        {
            const uint64_t capacity = 8192;
            uint8_t* machine = mmap(0, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            uint8_t scalar[8192];
            bool ready = machine != MAP_FAILED && code.branch_count == 0 && BUSTER_X86_ENCODER_CAPACITY(code.count) <= capacity;
            if (ready)
            {
                const int64_t length = x86_encode(code.records, code.count, machine, capacity, 0, 0);
                const int64_t reference_length = x86_encode_scalar(code.records, code.count, scalar, capacity, 0, 0);
                ready = length > 0 && length == reference_length && memcmp(machine, scalar, (uint64_t)length) == 0 && mprotect(machine, capacity, PROT_READ | PROT_EXEC) == 0;
            }
            tier1_liveness_test_expect(test, ready, "both encoders agree on the allocated folded function");
            for (uint64_t i = 0; ready && i < 64; i += 1)
            {
                const uint64_t a = UINT64_C(11939) * i;
                const uint64_t b = UINT64_C(7777771) * (i + 7);
                tier1_liveness_test_expect(test, tier1_liveness_test_execute(machine, a, b) == ~a + (b << 3), "selected-use allocation preserves executed folded arithmetic");
            }
            if (machine != MAP_FAILED)
            {
                munmap(machine, capacity);
            }
        }
    }
}

static void tier1_liveness_test_fold(Tier1LivenessTest* test)
{
    arena_pop_to(&test->input, 0);
    const IrSizes sizes = { .instruction_count = 7, .block_count = 1, .constant_count = 1, .parameter_count = 2 };
    IrFunction function;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_I64;
        function.parameter_types[1] = IR_I64;
        function.constants[0].low = 3;
        function.block_offsets[1] = 7;
        tier1_liveness_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 3, IR_SHL, IR_I64, 1, 2, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 4, IR_NOT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 5, IR_ADD, IR_I64, 4, 3, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        Tier1SelectPlan plan;
        IrCfg cfg;
        valid = tier1_liveness_test_plan(test, &function, &plan, &cfg);
        if (valid)
        {
            Tier1Liveness scalar;
            Tier1Liveness vector;
            valid = tier1_liveness_test_compare(test, &plan, &cfg, &scalar, &vector);
            tier1_liveness_test_expect(test, plan.covered[3] != 0, "fixture really folds the shift into its later consumer");
            if (valid)
            {
                tier1_liveness_test_expect(test, vector.cross_block_values == 0 && vector.matrix_bytes == 0, "local-only values allocate no bitset rows");
                for (uint32_t interval = 0; interval < vector.allocation.count; interval += 1)
                {
                    const uint32_t value = vector.allocation.values[interval];
                    tier1_liveness_test_expect(test, value != 3, "covered shift has no physical definition");
                    if (value == 1)
                    {
                        tier1_liveness_test_expect(test, vector.allocation.ends[interval] == 11, "folded shift source stays live until the ADD use");
                    }
                }
                tier1_liveness_test_emit(test, &plan, &scalar, &test->left, false, true);
                tier1_liveness_test_emit(test, &plan, &vector, &test->right, true, true);
                const uint64_t mark = test->input.length;
                Tier1Liveness same_arena;
                tier1_liveness_test_expect(test, tier1_liveness_build(&plan, &test->input, &test->scratch, true, &same_arena) == TIER1_LIVENESS_OK, "retained plan and output may occupy successive arena regions");
                arena_pop_to(&test->input, mark);
            }
            Tier1Liveness sentinel;
            memset(&sentinel, 0x5a, sizeof(sentinel));
            Tier1Liveness rejected = sentinel;
            const uint64_t mark = test->right.length;
            const uint32_t saved = plan.effective_use_offsets[7];
            plan.effective_use_offsets[7] = UINT32_MAX;
            tier1_liveness_test_expect(test, tier1_liveness_build(&plan, &test->right, &test->scratch, true, &rejected) == TIER1_LIVENESS_INVALID, "malformed use offsets fail closed");
            plan.effective_use_offsets[7] = saved;
            tier1_liveness_test_expect(test, test->right.length == mark && memcmp(&sentinel, &rejected, sizeof(sentinel)) == 0, "invalid input is transactional");
            const uint32_t saved_value = plan.effective_use_values[0];
            plan.effective_use_values[0] = 3;
            tier1_liveness_test_expect(test, tier1_liveness_build(&plan, &test->right, &test->scratch, true, &rejected) == TIER1_LIVENESS_INVALID, "a selected use of a covered value fails closed");
            plan.effective_use_values[0] = saved_value;
            Arena tiny = arena_reserve(64);
            tier1_liveness_test_expect(test, tier1_liveness_build(&plan, &tiny, &test->scratch, true, &rejected) == TIER1_LIVENESS_MEMORY && tiny.length == 0, "allocation exhaustion restores the arena");
            arena_release(&tiny);
            tier1_liveness_test_expect(test, tier1_liveness_build(&plan, &test->right, &test->right, true, &rejected) == TIER1_LIVENESS_INVALID, "same output and scratch arena is rejected");
            IrFunction oversized = function;
            oversized.instruction_count = BUSTER_TIER1_ALLOCATE_MAX_INTERVALS + 1;
            Tier1SelectPlan oversized_plan = plan;
            oversized_plan.function = &oversized;
            tier1_liveness_test_expect(test, tier1_liveness_build(&oversized_plan, &test->right, &test->scratch, true, &rejected) == TIER1_LIVENESS_LIMIT, "oversized instruction count fails before reading borrowed columns");
        }
    }
    tier1_liveness_test_expect(test, valid, "fold fixture completed");
}

static uint32_t tier1_liveness_test_random(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void tier1_liveness_test_empty(Tier1LivenessTest* test)
{
    arena_pop_to(&test->input, 0);
    arena_pop_to(&test->left, 0);
    arena_pop_to(&test->right, 0);
    const IrSizes sizes = { 0 };
    IrFunction function;
    Tier1SelectPlan plan;
    uint32_t error_node;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK &&
                 tier1_select_plan(&function, 0, &test->input, &test->scratch, true, &plan, &error_node) == TIER1_SELECT_OK;
    Tier1Liveness scalar;
    Tier1Liveness vector;
    valid = valid && tier1_liveness_build(&plan, &test->left, &test->scratch, false, &scalar) == TIER1_LIVENESS_OK &&
            tier1_liveness_build(&plan, &test->right, &test->scratch, true, &vector) == TIER1_LIVENESS_OK;
    tier1_liveness_test_expect(test, valid, "empty real IR and selector plan accepted");
    if (valid)
    {
        tier1_liveness_test_expect(test, scalar.allocation.count == 0 && vector.allocation.count == 0 && scalar.allocation.use_count == 0 && vector.allocation.use_count == 0 &&
                                 scalar.matrix_bytes == 0 && vector.matrix_bytes == 0 && scalar.iterations == vector.iterations, "empty input has no intervals, uses or matrices");
    }
}

static void tier1_liveness_test_entry_loop(Tier1LivenessTest* test)
{
    arena_pop_to(&test->input, 0);
    const IrSizes sizes = { .instruction_count = 5, .block_count = 1, .parameter_count = 2 };
    IrFunction function;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_VOID;
        function.parameter_types[0] = IR_I64;
        function.parameter_types[1] = IR_I64;
        function.block_offsets[1] = 5;
        tier1_liveness_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 2, IR_ADD, IR_I64, 0, 1, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 3, IR_NOT, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 4, IR_BR, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        Tier1SelectPlan plan;
        IrCfg cfg;
        valid = tier1_liveness_test_plan(test, &function, &plan, &cfg);
        if (valid)
        {
            Tier1Liveness scalar;
            Tier1Liveness vector;
            valid = tier1_liveness_test_compare(test, &plan, &cfg, &scalar, &vector);
            if (valid)
            {
                tier1_liveness_test_expect(test, vector.cross_block_values == 2 && vector.allocation.ends[0] == 10 && vector.allocation.ends[1] == 10, "PARAMs stay live through the entry backedge despite no later local use");
                tier1_liveness_test_emit(test, &plan, &scalar, &test->left, false, false);
                tier1_liveness_test_emit(test, &plan, &vector, &test->right, true, false);
            }
        }
    }
    tier1_liveness_test_expect(test, valid, "entry-self-loop parameter fixture completed");
}

static void tier1_liveness_test_loop_definition(Tier1LivenessTest* test)
{
    arena_pop_to(&test->input, 0);
    const IrSizes sizes =
    {
        .instruction_count = 8,
        .block_count = 4,
        .parameter_count = 1,
    };
    IrFunction function;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_I64;
        function.block_offsets[1] = 2;
        function.block_offsets[2] = 4;
        function.block_offsets[3] = 7;
        function.block_offsets[4] = 8;
        tier1_liveness_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 2, IR_NOT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 3, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 4, IR_NOT, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 5, IR_ICMP, IR_I1, 0, 2, IR_ULT);
        tier1_liveness_test_node(&function, 6, IR_CBR, IR_VOID, 5, 1, 3);
        tier1_liveness_test_node(&function, 7, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        Tier1SelectPlan plan;
        IrCfg cfg;
        valid = tier1_liveness_test_plan(test, &function, &plan, &cfg);
        if (valid)
        {
            Tier1Liveness scalar;
            Tier1Liveness vector;
            valid = tier1_liveness_test_compare(test, &plan, &cfg, &scalar, &vector);
            if (valid)
            {
                uint8_t seen[4];
                uint32_t pending[4];
                tier1_liveness_test_expect(test, !tier1_liveness_test_reaches_use(&plan, &cfg, 2, 1, seen, pending) && tier1_liveness_test_reaches_use(&plan, &cfg, 2, 2, seen, pending), "loop definition kills the old value on the backedge before its next use");
                bool found = false;
                for (uint32_t interval = 0; interval < vector.allocation.count; interval += 1)
                {
                    if (vector.allocation.values[interval] == 2)
                    {
                        found = true;
                        tier1_liveness_test_expect(test, vector.allocation.starts[interval] == 5 && vector.allocation.ends[interval] == 13, "cross-block loop value begins at its definition and ends at its last selected use");
                    }
                }
                tier1_liveness_test_expect(test, found && plan.covered[5] != 0, "loop fixture retains the nonparameter definition and folds the compare into its branch");
                tier1_liveness_test_emit(test, &plan, &scalar, &test->left, false, false);
                tier1_liveness_test_emit(test, &plan, &vector, &test->right, true, false);
            }
        }
    }
    tier1_liveness_test_expect(test, valid, "cyclic nonparameter-definition fixture completed");
}

static void tier1_liveness_test_cfg(Tier1LivenessTest* test, uint32_t blocks, uint32_t* state)
{
    arena_pop_to(&test->input, 0);
    const IrSizes sizes = { .instruction_count = 3 + (blocks - 1) * 4, .block_count = blocks, .parameter_count = 2 };
    IrFunction function;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_I64;
        function.parameter_types[1] = IR_I64;
        tier1_liveness_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, 2, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t block = 1; block < blocks; block += 1)
        {
            const uint32_t first = 3 + (block - 1) * 4;
            function.block_offsets[block] = first;
            tier1_liveness_test_node(&function, first, IR_NOT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_liveness_test_node(&function, first + 1, IR_ADD, IR_I64, first, 1, BUSTER_IR_NONE);
            tier1_liveness_test_node(&function, first + 2, IR_ICMP, IR_I1, 0, 1, IR_ULT);
            if (block + 1 < blocks)
            {
                const uint32_t other = 1 + tier1_liveness_test_random(state) % (blocks - 1);
                tier1_liveness_test_node(&function, first + 3, IR_CBR, IR_VOID, first + 2, block + 1, other);
            }
            else
            {
                tier1_liveness_test_node(&function, first + 3, IR_RET, IR_VOID, first + 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
        }
        function.block_offsets[blocks] = function.instruction_count;
        Tier1SelectPlan plan;
        IrCfg cfg;
        valid = tier1_liveness_test_plan(test, &function, &plan, &cfg);
        if (valid)
        {
            Tier1Liveness scalar;
            Tier1Liveness vector;
            valid = tier1_liveness_test_compare(test, &plan, &cfg, &scalar, &vector);
            if (valid)
            {
                tier1_liveness_test_emit(test, &plan, &scalar, &test->left, false, false);
                tier1_liveness_test_emit(test, &plan, &vector, &test->right, true, false);
            }
        }
    }
    tier1_liveness_test_expect(test, valid, "random accepted cyclic CFG completed");
}

static void tier1_liveness_test_many_values(Tier1LivenessTest* test, uint32_t count)
{
    arena_pop_to(&test->input, 0);
    const IrSizes sizes = { .instruction_count = count * 2 + 2, .block_count = 2, .constant_count = count };
    IrFunction function;
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_I64;
        function.block_offsets[1] = count + 1;
        function.block_offsets[2] = function.instruction_count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            function.constants[i].low = i + 17;
            tier1_liveness_test_node(&function, i, IR_CONST, IR_I64, i, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_liveness_test_node(&function, count + 1 + i, IR_NOT, IR_I64, i, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        tier1_liveness_test_node(&function, count, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_liveness_test_node(&function, count * 2 + 1, IR_RET, IR_VOID, count * 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        Tier1SelectPlan plan;
        IrCfg cfg;
        valid = tier1_liveness_test_plan(test, &function, &plan, &cfg);
        if (valid)
        {
            Tier1Liveness scalar;
            Tier1Liveness vector;
            valid = tier1_liveness_test_compare(test, &plan, &cfg, &scalar, &vector);
            tier1_liveness_test_expect(test, valid && vector.cross_block_values == count, "actual selected values cross dense word/vector boundaries");
        }
    }
    tier1_liveness_test_expect(test, valid, "dense-value boundary fixture completed");
}

static void tier1_liveness_test_guards(Tier1LivenessTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool allocated = mapping != MAP_FAILED && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0;
    tier1_liveness_test_expect(test, allocated, "guarded kernel input allocated");
    if (allocated)
    {
        for (uint32_t count = 0; count <= 65; count += 1)
        {
            uint8_t* cross = mapping + page - count;
            uint32_t left_ids[65];
            uint32_t right_ids[65];
            uint32_t left_values[65];
            uint32_t right_values[65];
            memset(left_values, 0x5a, sizeof(left_values));
            memset(right_values, 0x5a, sizeof(right_values));
            for (uint32_t i = 0; i < count; i += 1)
            {
                cross[i] = i % 3 != 0;
            }
            const uint32_t left = tier1_liveness_dense(cross, count, left_ids, left_values, false);
            const uint32_t right = tier1_liveness_dense(cross, count, right_ids, right_values, true);
            tier1_liveness_test_expect(test, left == right && memcmp(left_ids, right_ids, count * 4) == 0 && memcmp(left_values, right_values, sizeof(left_values)) == 0, "guarded dense compaction and untouched output tail");
        }
        for (uint32_t count = 0; count <= 16; count += 1)
        {
            uint32_t* ids = (uint32_t*)(mapping + page - count * 4);
            uint64_t left[16] = { 0 };
            uint64_t right[16] = { 0 };
            for (uint32_t i = 0; i < count; i += 1)
            {
                ids[i] = i % 5 == 0 ? BUSTER_IR_NONE : i * 3;
            }
            tier1_liveness_set_row(left, ids, count, false);
            tier1_liveness_set_row(right, ids, count, true);
            tier1_liveness_test_expect(test, memcmp(left, right, sizeof(left)) == 0, "guarded collision-combining use/def builder");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static int run_tier1_liveness_tests(int argc, char** argv)
{
    Tier1LivenessTest test = { .seed = 143 };
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const unsigned long value = strtoul(argv[i + 1], &end, 10);
            valid = errno == 0 && end != argv[i + 1] && *end == 0 && value <= UINT32_MAX;
            test.seed = (uint32_t)value;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    if (valid)
    {
        test.input = arena_reserve(64ull << 20);
        test.left = arena_reserve(64ull << 20);
        test.right = arena_reserve(64ull << 20);
        test.scratch = arena_reserve(64ull << 20);
        test.allowed = malloc(X86_FORM_COUNT);
        valid = test.input.capacity && test.left.capacity && test.right.capacity && test.scratch.capacity && test.allowed;
    }
    if (valid)
    {
        const X86Target target = { .architecture = X86_ARCH_BASELINE };
        x86_target_forms(&target, test.allowed);
        tier1_liveness_test_fold(&test);
        tier1_liveness_test_empty(&test);
        tier1_liveness_test_entry_loop(&test);
        tier1_liveness_test_loop_definition(&test);
        uint32_t state = test.seed;
        for (uint32_t trial = 0; trial < 96; trial += 1)
        {
            tier1_liveness_test_cfg(&test, 2 + tier1_liveness_test_random(&state) % 12, &state);
        }
        const uint32_t boundaries[] = { 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 511, 512, 513 };
        for (uint32_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i += 1)
        {
            tier1_liveness_test_many_values(&test, boundaries[i]);
        }
        tier1_liveness_test_guards(&test);
    }
    else
    {
        fprintf(stderr, "test-tier1-liveness: invalid arguments, initialization or allocation failure\n");
    }
    arena_release(&test.input);
    arena_release(&test.left);
    arena_release(&test.right);
    arena_release(&test.scratch);
    free(test.allowed);
    printf("Tier 1 selected-use liveness: %" PRIu64 " checks, %" PRIu64 " failures, seed=%u; generated IR only\n", test.checks, test.failures, test.seed);
    return valid && test.failures == 0 ? 0 : 1;
}
