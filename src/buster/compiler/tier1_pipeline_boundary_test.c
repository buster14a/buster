// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #789's typed-stream acceptance, included by tier1_pipeline_test.c. Both backends consume the SAME frozen
// Tier0Function columns. There is no Tier 0 fallback for a refused Tier 1 function. The native results must agree
// with each other and with unsigned C formulas, not just with another consumer of the optimized IR.

#define BUSTER_TIER1_BOUNDARY_CASES 5u
#define BUSTER_TIER1_BOUNDARY_FUNCTIONS 2u
#define BUSTER_TIER1_BOUNDARY_ENTRIES 256u
#define BUSTER_TIER1_BOUNDARY_LOCALS 10u

typedef struct Tier1PipelineBoundaryFixture Tier1PipelineBoundaryFixture;
struct Tier1PipelineBoundaryFixture
{
    Tier0Program streams[BUSTER_TIER1_BOUNDARY_FUNCTIONS];
    Tier0Function functions[BUSTER_TIER1_BOUNDARY_FUNCTIONS];
    uint8_t kinds[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_ENTRIES];
    uint8_t classes[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_ENTRIES];
    uint64_t values[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_ENTRIES];
    uint8_t local_classes[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_LOCALS];
    uint8_t local_flags[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_LOCALS];
    uint32_t local_sizes[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_LOCALS];
    uint8_t local_alignments[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_LOCALS];
    AbiLocation parameters[BUSTER_TIER1_BOUNDARY_FUNCTIONS][BUSTER_TIER1_BOUNDARY_LOCALS];
    AbiCall incoming;
    AbiCall call;
    AbiLocation arguments[2];
    uint8_t argument_classes[2];
    uint32_t argument_sizes[2];
    SymbolId call_target;
    uint32_t call_first;
    uint32_t call_arguments;
    uint32_t call_result;
    uint8_t call_variadic;
    uint32_t labels[2];
    uint32_t count;
};

static void tier1_pipeline_boundary_address(Tier0Program* stream, uint32_t local, uint32_t offset)
{
    tier0_emit(stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
    if (offset != 0)
    {
        tier0_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset);
        tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void tier1_pipeline_boundary_read(Tier0Program* stream, uint32_t local, uint32_t offset, uint8_t class)
{
    tier1_pipeline_boundary_address(stream, local, offset);
    tier0_emit(stream, BUSTER_TYPED_LOAD, class, 0);
    if (class != BUSTER_TYPED_CLASS_U64)
    {
        tier0_emit(stream, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void tier1_pipeline_boundary_fixture(Tier1PipelineBoundaryFixture* fixture, uint32_t shape)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->count = shape == 1 ? 2 : 1;
    const AbiReturn integer_result = {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
    };
    for (uint32_t f = 0; f < fixture->count; f += 1)
    {
        fixture->streams[f] = (Tier0Program){
            .kinds = fixture->kinds[f],
            .classes = fixture->classes[f],
            .values = fixture->values[f],
            .entry_capacity = BUSTER_TIER1_BOUNDARY_ENTRIES,
        };
        memset(fixture->local_classes[f], BUSTER_TYPED_CLASS_U64, sizeof(fixture->local_classes[f]));
        for (uint32_t p = 0; p < BUSTER_TIER1_BOUNDARY_LOCALS; p += 1)
        {
            fixture->parameters[f][p] = (AbiLocation){
                .stack_offset = p < BUSTER_ABI_INTEGER_ARGUMENTS ? 0 : (p - BUSTER_ABI_INTEGER_ARGUMENTS) * 8,
                .registers = { p < BUSTER_ABI_INTEGER_ARGUMENTS ? abi_integer_arguments[p] : BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
                .kind = p < BUSTER_ABI_INTEGER_ARGUMENTS ? BUSTER_ABI_LOCATION_REGISTERS : BUSTER_ABI_LOCATION_STACK,
            };
        }
        fixture->functions[f] = (Tier0Function){
            .kinds = fixture->kinds[f],
            .classes = fixture->classes[f],
            .values = fixture->values[f],
            .local_classes = fixture->local_classes[f],
            .local_flags = fixture->local_flags[f],
            .local_sizes = fixture->local_sizes[f],
            .local_alignments = fixture->local_alignments[f],
            .local_count = 4,
            .parameter_count = 3,
            .parameters = fixture->parameters[f],
            .result = integer_result,
            .symbol_count = fixture->count + 3,
        };
    }
    Tier0Program* stream = &fixture->streams[0];
    Tier0Function* function = &fixture->functions[0];
    if (shape == 0)
    {
        // A runtime-selected label, carried through a local and SSA's phi, then one indirect dispatch.
        tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
        tier0_emit(stream, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        const uint32_t otherwise = tier0_emit(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(stream, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        const uint32_t join = tier0_emit(stream, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
        fixture->values[0][otherwise] = stream->entry_count;
        tier0_emit(stream, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        fixture->values[0][join] = stream->entry_count;
        tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_emit(stream, BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0);
        for (uint32_t label = 0; label < 2; label += 1)
        {
            fixture->labels[label] = stream->entry_count;
            tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
            tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
            tier0_emit(stream, label == 0 ? BUSTER_TYPED_ADD : BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        function->label_entries = fixture->labels;
        function->label_count = 2;
    }
    else if (shape == 1)
    {
        // struct { uint64_t x, y; } helper(struct pair p, uint64_t c): mutate the callee's copy and return it.
        function->parameter_count = 2;
        function->local_count = 2;
        fixture->local_classes[0][0] = BUSTER_TYPED_CLASS_VOID;
        fixture->local_flags[0][0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        fixture->local_sizes[0][0] = 16;
        fixture->local_alignments[0][0] = 3;
        fixture->parameters[0][0].registers[1] = BUSTER_ABI_RSI;
        fixture->parameters[0][1].registers[0] = BUSTER_ABI_RDX;
        function->result.registers[1] = BUSTER_ABI_RDX;
        function->result_size = 16;
        for (uint32_t field = 0; field < 2; field += 1)
        {
            tier1_pipeline_boundary_address(stream, 0, field * 8);
            tier0_emit(stream, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
            tier0_emit(stream, field == 0 ? BUSTER_TYPED_ADD : BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
            tier0_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
        tier1_pipeline_boundary_address(stream, 0, 0);
        tier0_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        fixture->call = (AbiCall){
            .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
            .integer_registers = 3,
            .result = function->result,
        };
        fixture->arguments[0] = fixture->parameters[0][0];
        fixture->arguments[1] = fixture->parameters[0][1];
        fixture->argument_classes[0] = BUSTER_TYPED_CLASS_U64;
        fixture->argument_classes[1] = BUSTER_TYPED_CLASS_U64;
        fixture->argument_sizes[0] = 16;
        fixture->call_target = 1;
        fixture->call_arguments = 2;
        fixture->call_result = 5;
        function = &fixture->functions[1];
        stream = &fixture->streams[1];
        function->local_count = 5;
        for (uint32_t local = 3; local < 5; local += 1)
        {
            fixture->local_classes[1][local] = BUSTER_TYPED_CLASS_VOID;
            fixture->local_flags[1][local] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            fixture->local_sizes[1][local] = 16;
            fixture->local_alignments[1][local] = 3;
        }
        function->call_targets = &fixture->call_target;
        function->call_first = &fixture->call_first;
        function->call_argument_counts = &fixture->call_arguments;
        function->call_variadic = &fixture->call_variadic;
        function->calls = &fixture->call;
        function->argument_locations = fixture->arguments;
        function->argument_classes = fixture->argument_classes;
        function->argument_sizes = fixture->argument_sizes;
        function->call_result_objects = &fixture->call_result;
        function->call_count = 1;
        function->argument_count = 2;
        for (uint32_t field = 0; field < 2; field += 1)
        {
            tier1_pipeline_boundary_address(stream, 3, field * 8);
            tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, field);
            tier0_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
        tier1_pipeline_boundary_address(stream, 3, 0);
        tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
        tier1_pipeline_boundary_read(stream, 4, 0, BUSTER_TYPED_CLASS_U64);
        tier1_pipeline_boundary_read(stream, 4, 8, BUSTER_TYPED_CLASS_U64);
        tier0_emit(stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        // The argument object must retain its original bytes after the callee writes its own by-value copy.
        tier1_pipeline_boundary_read(stream, 3, 0, BUSTER_TYPED_CLASS_U64);
        tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier1_pipeline_boundary_read(stream, 3, 8, BUSTER_TYPED_CLASS_U64);
        tier0_emit(stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    else
    {
        // GP-only (%al=0), mixed GP/SSE (%al=8), and overflow after a named argument already on the stack.
        const uint32_t named = shape == 4 ? 7 : 1;
        function->parameter_count = named;
        function->local_count = named + 2;
        fixture->incoming = (AbiCall){
            .stack_bytes = named > BUSTER_ABI_INTEGER_ARGUMENTS ? (named - BUSTER_ABI_INTEGER_ARGUMENTS) * 8 : 0,
            .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
            .integer_registers = named > BUSTER_ABI_INTEGER_ARGUMENTS ? BUSTER_ABI_INTEGER_ARGUMENTS : named,
            .result = integer_result,
        };
        function->incoming_variadic = &fixture->incoming;
        fixture->local_flags[0][named] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA;
        fixture->local_flags[0][named + 1] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA;
        tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        for (uint32_t p = 1; p < named; p += 1)
        {
            tier0_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, p);
            tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        for (uint32_t gp = named; gp < BUSTER_ABI_INTEGER_ARGUMENTS; gp += 1)
        {
            tier1_pipeline_boundary_read(stream, named, gp * 8, BUSTER_TYPED_CLASS_U64);
            tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        for (uint32_t slot = 0; slot < 2; slot += 1)
        {
            tier1_pipeline_boundary_read(stream, named + 1, slot * 8, BUSTER_TYPED_CLASS_U64);
            tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (shape == 3)
        {
            for (uint32_t xmm = 0; xmm < BUSTER_ABI_VECTOR_ARGUMENTS; xmm += 1)
            {
                tier1_pipeline_boundary_read(stream, named, BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + xmm * BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES, BUSTER_TYPED_CLASS_F64);
                tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier1_pipeline_boundary_read(stream, named + 1, 16, BUSTER_TYPED_CLASS_F64);
            tier0_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        tier0_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    for (uint32_t f = 0; f < fixture->count; f += 1)
    {
        fixture->functions[f].count = fixture->streams[f].entry_count;
    }
}

// This adapter only describes records: it does not emit instructions or share a backend's selection decisions.
static bool tier1_pipeline_boundary_tier1(Tier1PipelineTest* test, const Tier0Function* source, Tier0Code* body)
{
    Arena* scratch = &test->arenas[TIER1_PIPELINE_SCRATCH];
    IrFunction lowered;
    IrFunction promoted;
    IrOptimizeResult optimized;
    IrCheck check = test->vector ? ir_lower_typed(source, IR_I64, source->symbol_count, &test->arenas[TIER1_PIPELINE_LOWERED], scratch, &lowered) :
                                  ir_lower_typed_scalar(source, IR_I64, source->symbol_count, &test->arenas[TIER1_PIPELINE_LOWERED], scratch, &lowered);
    bool ok = tier1_pipeline_expect(test, check.error == IR_OK, "boundary typed stream lowers");
    if (ok)
    {
        check = test->vector ? ssa_ir_promote(&lowered, &test->arenas[TIER1_PIPELINE_PROMOTED], scratch, &promoted) :
                               ssa_ir_promote_scalar(&lowered, &test->arenas[TIER1_PIPELINE_PROMOTED], scratch, &promoted);
        ok = tier1_pipeline_expect(test, check.error == IR_OK, "boundary locals promote without discarding address-taken objects");
    }
    if (ok)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.path = test->vector ? BUSTER_IR_OPTIMIZE_VECTOR : BUSTER_IR_OPTIMIZE_SCALAR;
        check = ir_optimize(&promoted, &test->arenas[TIER1_PIPELINE_OPTIMIZED], scratch, options, &optimized);
        ok = tier1_pipeline_expect(test, check.error == IR_OK, "boundary SSA optimizes");
    }
    IrCfg cfg;
    IrDominance dominance;
    if (ok)
    {
        ok = tier1_pipeline_expect(test, ir_build_cfg(&optimized.function, &test->arenas[TIER1_PIPELINE_DERIVED], scratch, &cfg) == IR_OK, "boundary optimized CFG rebuilds");
    }
    if (ok)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.path = test->vector ? DOMINATOR_VECTOR : DOMINATOR_SCALAR;
        options.outputs = 0;
        Dominators result;
        ok = tier1_pipeline_expect(test, dominators_analyze(&test->arenas[TIER1_PIPELINE_DERIVED], scratch, &graph, options, &result) == DOMINATOR_OK, "boundary dominance rebuilds");
        if (ok)
        {
            dominance = (IrDominance){
                .enter = result.preorder,
                .leave = result.subtree_end,
                .block_count = result.block_count,
            };
            const IrCheck vector = ir_verify(&optimized.function, scratch, &dominance);
            const IrCheck scalar = ir_verify_scalar(&optimized.function, scratch, &dominance);
            ok = tier1_pipeline_expect(test, vector.error == IR_OK && scalar.error == IR_OK && vector.dominance_checked && scalar.dominance_checked, "both verifiers accept boundary optimized SSA");
        }
    }
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    if (ok)
    {
        const Tier1SelectError error = tier1_select_plan_verified(&optimized.function, &dominance, 0, 0, &test->arenas[TIER1_PIPELINE_SELECTED], scratch, test->vector, &plan, &node);
        ok = tier1_pipeline_expect(test, error == TIER1_SELECT_OK, "boundary selection succeeds without fallback");
    }
    if (ok)
    {
        const Tier1EmitOptions options = {
            .certify = true,
            .register_mask = tier1_liveness_register_mask(),
            .frame_flags = test->rematerialize ? BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE : 0,
            .vector = test->vector,
            .rematerialize = test->rematerialize,
            .symbol_count = source->symbol_count,
        };
        Tier1EmitResult emitted;
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->arenas[TIER1_PIPELINE_EMITTED], scratch, &emitted, &node);
        ok = tier1_pipeline_expect(test, error == TIER1_EMIT_OK, "boundary native emission succeeds without fallback");
        if (ok)
        {
            ok = tier1_pipeline_expect(test, emitted.certified && (source->label_count == 0 || emitted.general), "boundary allocation is certified, with indirect dispatch through the CFG consumer");
            *body = (Tier0Code){
                .records = emitted.code.records,
                .count = emitted.code.count,
                .branches = emitted.code.branches,
                .targets = emitted.code.targets,
                .branch_count = emitted.code.branch_count,
                .references = emitted.references,
                .reference_count = emitted.reference_count,
                .label_records = emitted.label_records,
                .label_count = emitted.label_count,
            };
            if (source->label_count != 0)
            {
                uint32_t indirect = 0;
                for (uint32_t r = 0; r < body->count; r += 1)
                {
                    indirect += (body->records[r].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_JMP_RM64;
                }
                ok = tier1_pipeline_expect(test, body->label_count == source->label_count && indirect == 1, "boundary labels survive with exactly one indirect dispatch") && ok;
            }
            if (source->call_count != 0)
            {
                ok = tier1_pipeline_expect(test, body->reference_count == 1 && body->references[0].kind == BUSTER_LINK_FIXUP_CALL, "aggregate call remains a native symbolic call") && ok;
            }
        }
        if (!ok)
        {
            fprintf(stderr, "  boundary emission error=%u node=%u\n", error, node);
        }
    }
    ok = tier1_pipeline_expect(test, scratch->length == 0, "boundary compilation returns scratch") && ok;
    return ok;
}

// Function symbols are 1..count; only the labels fixture has further symbols, its two labels in function 0.
// Record offsets, fixup distances and encoder ends are checked before any executable mapping is published.
static uint8_t* tier1_pipeline_boundary_load(Tier1PipelineTest* test, Tier0Code* bodies, uint32_t count, uint64_t* mapped, uint64_t* entry)
{
    uint64_t starts[BUSTER_TIER1_BOUNDARY_FUNCTIONS] = {0};
    uint64_t sizes[BUSTER_TIER1_BOUNDARY_FUNCTIONS] = {0};
    uint32_t* offsets[BUSTER_TIER1_BOUNDARY_FUNCTIONS] = {0};
    uint32_t* ends[BUSTER_TIER1_BOUNDARY_FUNCTIONS] = {0};
    uint64_t symbols[BUSTER_TIER1_BOUNDARY_FUNCTIONS + 3] = {0};
    bool defined[BUSTER_TIER1_BOUNDARY_FUNCTIONS + 3] = {false};
    uint64_t capacity = 0;
    for (uint32_t f = 0; f < count; f += 1)
    {
        starts[f] = (capacity + 15) & ~UINT64_C(15);
        capacity = starts[f] + BUSTER_X86_ENCODER_CAPACITY(bodies[f].count);
        offsets[f] = malloc(((uint64_t)bodies[f].count + 1) * sizeof(uint32_t));
        ends[f] = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(bodies[f].reference_count) * sizeof(uint32_t));
    }
    *mapped = (capacity + 4095) & ~UINT64_C(4095);
    uint8_t* machine = capacity != 0 && capacity < INT32_MAX ? mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* scalar = malloc(capacity);
    bool ok = tier1_pipeline_expect(test, machine != MAP_FAILED && scalar != 0, "boundary native buffers allocate");
    for (uint32_t f = 0; ok && f < count; f += 1)
    {
        Tier0Code* body = &bodies[f];
        uint32_t* scalar_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(body->reference_count) * sizeof(uint32_t));
        ok = offsets[f] != 0 && ends[f] != 0 && scalar_ends != 0;
        ok = ok && branch_sizing_validate(body->records, body->count, body->branches, body->targets, body->branch_count).error == BRANCH_SIZING_OK;
        ok = ok && size_branches(body->records, body->count, body->branches, body->targets, body->branch_count, offsets[f]).error == BRANCH_SIZING_OK;
        ok = ok && x86_target_validate(test->allowed, body->records, body->count) == body->count;
        uint32_t marked = 0;
        for (uint32_t record = 0; ok && record < body->count; record += 1)
        {
            if ((body->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
            {
                ok = marked < body->reference_count && body->references[marked].instruction == record;
                marked += 1;
            }
        }
        ok = ok && marked == body->reference_count;
        const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(body->count);
        const int64_t vector_size = ok ? x86_encode(body->records, body->count, machine + starts[f], room, 0, ends[f]) : -1;
        const int64_t scalar_size = ok ? x86_encode_scalar(body->records, body->count, scalar + starts[f], room, 0, scalar_ends) : -1;
        ok = ok && vector_size > 0 && (uint64_t)vector_size <= room && vector_size == scalar_size && memcmp(machine + starts[f], scalar + starts[f], (size_t)vector_size) == 0;
        ok = ok && memcmp(ends[f], scalar_ends, (uint64_t)body->reference_count * sizeof(uint32_t)) == 0;
        ok = tier1_pipeline_expect(test, ok, "boundary branches size and baseline encoders agree on bytes and fixup ends");
        if (ok)
        {
            sizes[f] = (uint64_t)vector_size;
            symbols[f + 1] = starts[f];
            defined[f + 1] = true;
            ok = body->label_count == 0 || (f == 0 && count == 1 && body->label_count == 2);
            for (uint32_t label = 0; ok && label < body->label_count; label += 1)
            {
                // An absent label keeps the driver's entry placeholder; every other label must name a real record.
                const uint32_t record = body->label_records[label];
                const uint32_t offset = record < body->count ? offsets[f][record] : 0;
                ok = (record == UINT32_MAX || record < body->count) && offset < sizes[f];
                if (ok)
                {
                    symbols[count + 1 + label] = starts[f] + offset;
                    defined[count + 1 + label] = true;
                }
            }
            ok = tier1_pipeline_expect(test, ok, "boundary label symbols name their emitted records");
        }
        free(scalar_ends);
    }
    for (uint32_t f = 0; ok && f < count; f += 1)
    {
        const Tier0Code* body = &bodies[f];
        for (uint32_t r = 0; ok && r < body->reference_count; r += 1)
        {
            const LinkReference* reference = &body->references[r];
            ok = reference->instruction < body->count && reference->target < BUSTER_ARRAY_LENGTH(symbols) && defined[reference->target] && reference->addend == 0;
            const uint32_t distance = ok ? x86_fixup_distance(body->records[reference->instruction].form, reference->field) : 0;
            const uint32_t kind = reference->target <= count ? BUSTER_LINK_FIXUP_CALL : BUSTER_LINK_FIXUP_PC32;
            ok = ok && reference->kind == kind && distance == sizeof(int32_t) && ends[f][r] >= distance && ends[f][r] <= sizes[f];
            const int64_t relative = ok ? (int64_t)symbols[reference->target] - (int64_t)(starts[f] + ends[f][r]) : 0;
            ok = ok && relative >= INT32_MIN && relative <= INT32_MAX;
            ok = tier1_pipeline_expect(test, ok, "boundary call/label relocation has a defined target and a bounded rel32 field");
            if (ok)
            {
                const int32_t field = (int32_t)relative;
                memcpy(machine + starts[f] + ends[f][r] - distance, &field, sizeof(field));
            }
        }
    }
    ok = ok && mprotect(machine, *mapped, PROT_READ | PROT_EXEC) == 0;
    tier1_pipeline_expect(test, ok, "boundary code becomes executable only after complete relocation");
    if (!ok && machine != MAP_FAILED)
    {
        munmap(machine, *mapped);
        machine = MAP_FAILED;
    }
    for (uint32_t f = 0; f < count; f += 1)
    {
        free(offsets[f]);
        free(ends[f]);
    }
    free(scalar);
    *entry = starts[count - 1];
    return machine;
}

static uint64_t tier1_pipeline_boundary_expected(uint32_t shape, const uint64_t* input)
{
    const uint64_t a = input[0];
    const uint64_t b = input[1];
    const uint64_t c = input[2];
    uint64_t result = a + b + c + (a ^ b) + (b ^ c) + (c ^ a) + (a + 7) + (b + 11);
    if (shape == 0)
    {
        result = (a & 1) != 0 ? b + c : b ^ c;
    }
    else if (shape == 1)
    {
        result = ((((a + c) ^ (b ^ c)) + a) ^ b) + c;
    }
    else if (shape == 3)
    {
        result += 45;
    }
    else if (shape == 4)
    {
        result += c + 13;
    }
    return result;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_pipeline_boundary_call(const void* code, uint32_t shape, const uint64_t* input)
{
    const uint64_t a = input[0];
    const uint64_t b = input[1];
    const uint64_t c = input[2];
    uint64_t result = 0;
    if (shape < 2)
    {
        result = tier1_pipeline_call(code, input);
    }
    else if (shape == 4)
    {
        uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, ...);
        memcpy(&function, &code, sizeof(function));
        result = function(a, b, c, a ^ b, b ^ c, c ^ a, a + 7, b + 11, c + 13);
    }
    else
    {
        uint64_t (*function)(uint64_t, ...);
        memcpy(&function, &code, sizeof(function));
        if (shape == 2)
        {
            result = function(a, b, c, a ^ b, b ^ c, c ^ a, a + 7, b + 11);
        }
        else
        {
            result = function(a, b, c, a ^ b, b ^ c, c ^ a, a + 7, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, b + 11, 9.0);
        }
    }
    return result;
}

static void tier1_pipeline_boundaries(Tier1PipelineTest* test)
{
    uint32_t pairs = 0;
    for (uint32_t shape = 0; shape < BUSTER_TIER1_BOUNDARY_CASES; shape += 1)
    {
        Tier1PipelineBoundaryFixture fixture;
        Tier1PipelineBoundaryFixture original;
        tier1_pipeline_boundary_fixture(&fixture, shape);
        memcpy(&original, &fixture, sizeof(original));
        test->fixture = BUSTER_TIER1_PIPELINE_CASES + shape;
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            test->vector = (variant & 1) != 0;
            test->rematerialize = (variant & 2) != 0;
            uint8_t* machine[2] = { MAP_FAILED, MAP_FAILED };
            uint64_t mapped[2] = {0};
            uint64_t entry[2] = {0};
            bool ok = true;
            for (uint32_t tier = 0; ok && tier < 2; tier += 1)
            {
                tier1_pipeline_reset(test);
                Tier0Code bodies[BUSTER_TIER1_BOUNDARY_FUNCTIONS] = {0};
                for (uint32_t f = 0; ok && f < fixture.count; f += 1)
                {
                    const Tier0Function* source = &fixture.functions[f];
                    const Tier0Check checked = tier0_validate(source);
                    ok = tier1_pipeline_expect(test, !fixture.streams[f].overflow && checked.error == TIER0_OK, "boundary fixture is a valid bounded typed stream");
                    if (!ok && checked.error != TIER0_OK)
                    {
                        fprintf(stderr, "  boundary typed error at %u: %s\n", checked.node, checked.rule);
                    }
                    if (ok && tier == 0)
                    {
                        bodies[f] = test->vector ? tier0_compile(source, &test->arenas[TIER1_PIPELINE_EMITTED]) : tier0_compile_scalar(source, &test->arenas[TIER1_PIPELINE_EMITTED]);
                        ok = tier1_pipeline_expect(test, bodies[f].error == TIER0_OK, "boundary Tier 0 oracle compiles the original stream");
                    }
                    else if (ok)
                    {
                        ok = tier1_pipeline_boundary_tier1(test, source, &bodies[f]);
                    }
                }
                ok = tier1_pipeline_expect(test, memcmp(&fixture, &original, sizeof(fixture)) == 0, "boundary compilation preserves every frozen source column and descriptor") && ok;
                if (ok)
                {
                    machine[tier] = tier1_pipeline_boundary_load(test, bodies, fixture.count, &mapped[tier], &entry[tier]);
                    ok = machine[tier] != MAP_FAILED;
                }
            }
            if (ok)
            {
                fflush(0);
                const pid_t child = fork();
                if (child == 0)
                {
                    alarm(5);
                    bool agrees = true;
                    for (uint32_t sample = 0; agrees && sample < BUSTER_TIER1_PIPELINE_INPUTS; sample += 1)
                    {
                        const uint64_t expected = tier1_pipeline_boundary_expected(shape, test->inputs[sample]);
                        const uint64_t tier0 = tier1_pipeline_boundary_call(machine[0] + entry[0], shape, test->inputs[sample]);
                        const uint64_t tier1 = tier1_pipeline_boundary_call(machine[1] + entry[1], shape, test->inputs[sample]);
                        agrees = tier0 == expected && tier1 == expected;
                        if (!agrees)
                        {
                            fprintf(stderr, "boundary shape=%u variant=%u sample=%u tier0=%016" PRIx64 " tier1=%016" PRIx64 " expected=%016" PRIx64 "\n", shape, variant, sample, tier0, tier1, expected);
                        }
                    }
                    _exit(agrees ? 0 : 1);
                }
                ok = tier1_pipeline_expect(test, tier1_pipeline_wait(child), "boundary Tier 1 and Tier 0 native results equal the independent formula within the deadline");
                pairs += ok ? BUSTER_TIER1_PIPELINE_INPUTS : 0;
            }
            for (uint32_t tier = 0; tier < 2; tier += 1)
            {
                if (machine[tier] != MAP_FAILED)
                {
                    munmap(machine[tier], mapped[tier]);
                }
            }
        }
    }
    tier1_pipeline_expect(test, pairs == BUSTER_TIER1_BOUNDARY_CASES * 4 * BUSTER_TIER1_PIPELINE_INPUTS, "every boundary fixture executes both backends on every input and option combination");
    printf("tier1 pipeline boundaries: %u native differential pairs (%u native results)\n", pairs, pairs * 2);
}
