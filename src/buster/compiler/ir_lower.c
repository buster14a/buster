// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Lower the existing typed stack stream into Tier 1's all-in-memory IR. Include after tier0.c and ir.c.
// ir_lower_typed and ir_lower_typed_scalar validate the input, plan block/stack shapes, count output, then emit directly
// into exact-sized columns. Locals and expression values crossing edges use stack slots for #134 to promote. A separate
// preheader initializes parameters once, even when source entry zero is a loop target. Pointer-valued U64 entries are
// converted explicitly at pointer operations. ABI locations never enter the IR. An explicit semantic return type is
// required because Tier0Function.result only describes the ABI. See docs/ir.md for limits and pending source integration.
// Layout: IrTypedPlan/ir_typed_plan; ir_typed_classify; IrTypedSink/ir_typed_emit_function; ir_lower_typed_path.

typedef struct IrTypedPlan IrTypedPlan;
struct IrTypedPlan
{
    // Source-entry ranges, including a final empty range for the stream's end.
    uint32_t* block_firsts;
    uint32_t* entry_blocks;
    uint32_t block_count;
    // A block's incoming stack shape, oldest value first. Offsets are into the
    // same scratch arena; UINT32_MAX depth means no incoming shape recorded yet.
    uint32_t* stack_depths;
    uint64_t* stack_offsets;
    uint32_t maximum_depth;
    uint32_t maximum_spill_depth;
    uint32_t error_entry;
    const char* error;
    IrError error_code;
};

static bool ir_typed_plan_arrive(IrTypedPlan* plan, Arena* scratch, uint32_t block, const uint8_t* stack, uint32_t depth, uint32_t entry)
{
    bool ok = true;
    if (plan->stack_depths[block] == UINT32_MAX)
    {
        // Allocate one byte even for an empty stack, avoiding a null empty-slice
        // pointer when an empty arena is used to exercise failure handling.
        uint8_t* saved = arena_push(scratch, depth == 0 ? 1 : depth, 1);
        if (saved == 0)
        {
            plan->error = "no scratch memory for a typed stack shape";
            plan->error_code = IR_ERROR_MEMORY;
            ok = false;
        }
        else
        {
            memcpy(saved, stack, depth);
            plan->stack_offsets[block] = (uint64_t)(saved - scratch->base);
            plan->stack_depths[block] = depth;
        }
    }
    else if (plan->stack_depths[block] != depth || memcmp(scratch->base + plan->stack_offsets[block], stack, depth) != 0)
    {
        plan->error = "a typed target is reached with different stack shapes";
        plan->error_code = IR_ERROR_STRUCTURE;
        ok = false;
    }
    if (!ok)
    {
        plan->error_entry = entry;
    }
    else if (depth > plan->maximum_spill_depth)
    {
        plan->maximum_spill_depth = depth;
    }
    return ok;
}

static bool ir_typed_control(uint8_t kind)
{
    return kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_RETURN;
}

static IrTypedPlan ir_typed_plan(const Tier0Function* input, Arena* scratch)
{
    IrTypedPlan plan = {0};
    const uint32_t count = input->count;
    uint8_t* leaders = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    uint8_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    plan.block_firsts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 2);
    plan.entry_blocks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    plan.stack_depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    plan.stack_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1);
    bool ok = leaders != 0 && stack != 0 && plan.block_firsts != 0 && plan.entry_blocks != 0 && plan.stack_depths != 0 && plan.stack_offsets != 0;
    if (!ok)
    {
        plan.error = "no scratch memory for typed block planning";
        plan.error_code = IR_ERROR_MEMORY;
    }
    if (ok)
    {
        memset(leaders, 0, (uint64_t)count + 1);
        leaders[0] = 1;
        leaders[count] = 1;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            if (ir_typed_control(kind))
            {
                leaders[i + 1] = 1;
                if (kind != BUSTER_TYPED_RETURN)
                {
                    // tier0_validate already established values[i] <= count.
                    leaders[input->values[i]] = 1;
                }
            }
        }
        for (uint32_t i = 0; i <= count; i += 1)
        {
            if (leaders[i] != 0)
            {
                plan.block_firsts[plan.block_count] = i;
                plan.stack_depths[plan.block_count] = UINT32_MAX;
                plan.block_count += 1;
            }
            plan.entry_blocks[i] = plan.block_count - 1;
        }
        plan.block_firsts[plan.block_count] = count;

        uint32_t depth = 0;
        bool reachable = true;
        for (uint32_t i = 0; i <= count && ok; i += 1)
        {
            const uint32_t block = plan.entry_blocks[i];
            if (leaders[i] != 0)
            {
                if (!reachable)
                {
                    depth = 0;
                    if (plan.stack_depths[block] != UINT32_MAX)
                    {
                        depth = plan.stack_depths[block];
                        memcpy(stack, scratch->base + plan.stack_offsets[block], depth);
                    }
                    // The Tier0 contract checks otherwise unreachable code as
                    // a fresh statement-level region. Keep that convention.
                    reachable = true;
                }
                ok = ir_typed_plan_arrive(&plan, scratch, block, stack, depth, i);
            }
            if (i < count && ok)
            {
                const uint8_t kind = input->kinds[i];
                const uint8_t class = input->classes[i];
                const uint64_t value = input->values[i];
                switch (kind)
                {
                case BUSTER_TYPED_CONST:
                case BUSTER_TYPED_LOCAL:
                case BUSTER_TYPED_LOCAL_ADDRESS:
                case BUSTER_TYPED_GLOBAL:
                case BUSTER_TYPED_GLOBAL_ADDRESS:
                case BUSTER_TYPED_DUP:
                    stack[depth] = class;
                    depth += 1;
                    break;
                case BUSTER_TYPED_NEG:
                case BUSTER_TYPED_NOT:
                case BUSTER_TYPED_CONVERT:
                case BUSTER_TYPED_LOAD:
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_STORE_LOCAL:
                case BUSTER_TYPED_STORE_GLOBAL:
                    // Consume and reproduce the value at the same depth.
                    break;
                case BUSTER_TYPED_SET_LOCAL:
                case BUSTER_TYPED_SET_GLOBAL:
                case BUSTER_TYPED_DISCARD:
                    depth -= 1;
                    break;
                case BUSTER_TYPED_STORE:
                    depth -= 1;
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_SET:
                    depth -= 2;
                    break;
                case BUSTER_TYPED_CALL:
                    depth -= input->call_argument_counts[value];
                    if (class != BUSTER_TYPED_CLASS_VOID)
                    {
                        stack[depth] = class;
                        depth += 1;
                    }
                    break;
                case BUSTER_TYPED_BRANCH_FALSE:
                case BUSTER_TYPED_BRANCH_TRUE:
                    depth -= 1;
                    ok = ir_typed_plan_arrive(&plan, scratch, plan.entry_blocks[value], stack, depth, i);
                    break;
                case BUSTER_TYPED_JUMP:
                case BUSTER_TYPED_JUMP_VALUE:
                    // JUMP_VALUE keeps its arm's result on the outgoing stack.
                    // Its next source block is independent; resetting/restoring
                    // it above is what prevents the other arm reusing this id.
                    ok = ir_typed_plan_arrive(&plan, scratch, plan.entry_blocks[value], stack, depth, i);
                    reachable = false;
                    break;
                case BUSTER_TYPED_RETURN:
                    depth -= class != BUSTER_TYPED_CLASS_VOID;
                    reachable = false;
                    break;
                default:
                    // Every remaining kind is a binary arithmetic/compare.
                    depth -= 1;
                    stack[depth - 1] = class;
                    break;
                }
                if (depth > plan.maximum_depth)
                {
                    plan.maximum_depth = depth;
                }
            }
        }
    }
    return plan;
}

// Emission algorithm:
//
// 1. Create one stack slot per source local, plus maximum_spill_depth slots of size 8
//    and alignment 8. Source slots keep the typed local's actual size/alignment;
//    expression slots are untyped bytes and may hold different classes in
//    disjoint regions. Mark all address-taken locals so #134 never promotes them.
// 2. In a preheader, materialize each slot address and each PARAM value, then
//    store parameters into their local slots and branch to source block zero.
//    The preheader matters when source entry zero is a backedge target: placing
//    parameter stores directly in that block would reinitialize locals on loops.
// 3. Each source block begins by LOADing its incoming stack shape from the depth
//    slots, producing fresh SSA ids local to the block. Lower entries with an
//    explicit stack of ids. All local reads/writes use their fixed slots.
// 4. Before any outgoing terminator (including an implicit fallthrough jump),
//    STORE each surviving stack id to its depth slot. A conditional first pops
//    its predicate; JUMP_VALUE preserves the top. All outgoing successors see
//    exactly this same stack shape. Emit the branch last.
// 5. Source target count maps to the final source block. Its semantic result
//    type must be provided separately: Tier0Function.result is an ABI location,
//    not a full semantic return class. For void, end with RET void. Nonvoid
//    fallthrough must either emit an explicit undef return under an agreed
//    contract or diagnose unsupported; never silently map void fallthrough to
//    UNREACHABLE.
// 6. Verify the IR and build compact CFG arrays after construction. #134 can
//    promote both ordinary local slots and the expression-depth slots later.

// ---------------------------------------------------------------------------------------------------------------------
// Classification and the two-pass emission sink

static const uint8_t ir_typed_types[16] = {
    IR_VOID,
    IR_I8,
    IR_I8,
    IR_I16,
    IR_I16,
    IR_I32,
    IR_I32,
    IR_I64,
    IR_I64,
    IR_F32,
    IR_F64,
};

static const uint8_t ir_typed_integer_ops[64] = {
    [BUSTER_TYPED_NEG] = IR_NEG,
    [BUSTER_TYPED_NOT] = IR_NOT,
    [BUSTER_TYPED_ADD] = IR_ADD,
    [BUSTER_TYPED_SUB] = IR_SUB,
    [BUSTER_TYPED_MUL] = IR_MUL,
    [BUSTER_TYPED_DIV] = IR_UDIV,
    [BUSTER_TYPED_MOD] = IR_UREM,
    [BUSTER_TYPED_AND] = IR_AND,
    [BUSTER_TYPED_OR] = IR_OR,
    [BUSTER_TYPED_XOR] = IR_XOR,
    [BUSTER_TYPED_SHL] = IR_SHL,
    [BUSTER_TYPED_SHR] = IR_LSHR,
};

static const uint8_t ir_typed_signed_ops[64] = {
    [BUSTER_TYPED_NEG] = IR_NEG,
    [BUSTER_TYPED_NOT] = IR_NOT,
    [BUSTER_TYPED_ADD] = IR_ADD,
    [BUSTER_TYPED_SUB] = IR_SUB,
    [BUSTER_TYPED_MUL] = IR_MUL,
    [BUSTER_TYPED_DIV] = IR_SDIV,
    [BUSTER_TYPED_MOD] = IR_SREM,
    [BUSTER_TYPED_AND] = IR_AND,
    [BUSTER_TYPED_OR] = IR_OR,
    [BUSTER_TYPED_XOR] = IR_XOR,
    [BUSTER_TYPED_SHL] = IR_SHL,
    [BUSTER_TYPED_SHR] = IR_ASHR,
};

static const uint8_t ir_typed_floating_ops[64] = {
    [BUSTER_TYPED_NEG] = IR_FNEG,
    [BUSTER_TYPED_ADD] = IR_FADD,
    [BUSTER_TYPED_SUB] = IR_FSUB,
    [BUSTER_TYPED_MUL] = IR_FMUL,
    [BUSTER_TYPED_DIV] = IR_FDIV,
};

// The vector path classifies sixteen source entries per batch, before the
// ordered stack replay. Tail accesses cover only the logical source length.
// That replay is shared with its scalar twin, including count/place/fill.
static void ir_typed_classify(const Tier0Function* input, uint8_t* ops, uint8_t* types, bool vector)
{
    if (vector)
    {
        const __m512i integer_ops = _mm512_loadu_si512(ir_typed_integer_ops);
        const __m512i signed_ops = _mm512_loadu_si512(ir_typed_signed_ops);
        const __m512i floating_ops = _mm512_loadu_si512(ir_typed_floating_ops);
        const __m128i type_table = _mm_loadu_si128((const __m128i*)ir_typed_types);
        for (uint32_t i = 0; i < input->count; i += 16)
        {
            const uint32_t remaining = input->count - i;
            const uint32_t active_count = remaining < 16 ? remaining : 16;
            const __mmask16 active = (__mmask16)((1u << active_count) - 1);
            const __m128i kinds = _mm_maskz_loadu_epi8(active, input->kinds + i);
            const __m128i classes = _mm_maskz_loadu_epi8(active, input->classes + i);
            const __m512i indices = _mm512_zextsi128_si512(kinds);
            const __m128i integer = _mm512_castsi512_si128(_mm512_permutexvar_epi8(indices, integer_ops));
            const __m128i signed_integer = _mm512_castsi512_si128(_mm512_permutexvar_epi8(indices, signed_ops));
            const __m128i floating = _mm512_castsi512_si128(_mm512_permutexvar_epi8(indices, floating_ops));
            const __mmask16 is_signed = _mm_cmpneq_epi8_mask(_mm_and_si128(classes, _mm_set1_epi8(1)), _mm_setzero_si128());
            const __mmask16 is_floating = _mm_cmpgt_epu8_mask(classes, _mm_set1_epi8(BUSTER_TYPED_CLASS_U64));
            const __m128i integral = _mm_mask_blend_epi8(is_signed, integer, signed_integer);
            _mm_mask_storeu_epi8(ops + i, active, _mm_mask_blend_epi8(is_floating, integral, floating));
            _mm_mask_storeu_epi8(types + i, active, _mm_shuffle_epi8(type_table, classes));
        }
    }
    else
    {
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint8_t class = input->classes[i];
            types[i] = ir_typed_types[class];
            ops[i] = tier0_class_floating(class) ? ir_typed_floating_ops[kind]
                   : tier0_class_signed(class) ? ir_typed_signed_ops[kind] : ir_typed_integer_ops[kind];
        }
    }
}

typedef struct IrTypedSink IrTypedSink;
struct IrTypedSink
{
    IrFunction* output;
    IrSizes sizes;
};

static uint32_t ir_typed_emit(IrTypedSink* sink, IrOp op, IrType type, uint32_t a, uint32_t b, uint32_t c, uint8_t alignment_log2)
{
    const uint32_t id = (uint32_t)sink->sizes.instruction_count;
    if (sink->output != 0)
    {
        IrFunction* output = sink->output;
        output->op[id] = (uint8_t)op;
        output->type[id] = (uint8_t)type;
        output->a[id] = a;
        output->b[id] = b;
        output->c[id] = c;
        output->alignment_log2[id] = alignment_log2;
    }
    sink->sizes.instruction_count += 1;
    return id;
}

static uint32_t ir_typed_constant(IrTypedSink* sink, IrType type, uint64_t bits)
{
    const uint32_t constant = (uint32_t)sink->sizes.constant_count;
    if (sink->output != 0)
    {
        const uint32_t width = ir_type_bits[type];
        sink->output->constants[constant] = (IrConstant){
            .low = width < 64 ? bits & ((1ull << width) - 1) : bits,
            .high = 0,
        };
    }
    sink->sizes.constant_count += 1;
    return ir_typed_emit(sink, IR_CONST, type, constant, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
}

static uint8_t ir_typed_alignment(uint8_t class)
{
    const uint32_t bytes = tier0_class_bytes(class);
    return bytes == 8 ? 3 : bytes == 4 ? 2 : bytes == 2 ? 1 : 0;
}

static uint32_t ir_typed_convert(IrTypedSink* sink, uint32_t value, uint8_t from_class, uint8_t to_class)
{
    const IrType from = (IrType)ir_typed_types[from_class];
    const IrType to = (IrType)ir_typed_types[to_class];
    IrOp op = IR_INVALID;
    if (from != to)
    {
        if (ir_integer(from) && ir_integer(to))
        {
            op = ir_type_bits[from] > ir_type_bits[to] ? IR_TRUNC : tier0_class_signed(from_class) ? IR_SEXT : IR_ZEXT;
        }
        else if (ir_floating(from) && ir_floating(to))
        {
            op = ir_type_bits[from] > ir_type_bits[to] ? IR_FTRUNC : IR_FEXT;
        }
        else if (ir_integer(from))
        {
            op = tier0_class_signed(from_class) ? IR_SITOFP : IR_UITOFP;
        }
        else
        {
            op = tier0_class_signed(to_class) ? IR_FPTOSI : IR_FPTOUI;
        }
        value = ir_typed_emit(sink, op, to, value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return value;
}

static void ir_typed_spill(IrTypedSink* sink, const uint32_t* stack, const uint8_t* classes, uint32_t depth, uint32_t local_count)
{
    for (uint32_t d = 0; d < depth; d += 1)
    {
        // All slot addresses are emitted first in the preheader, so an address
        // value's instruction ID is exactly its slot ID.
        ir_typed_emit(sink, IR_STORE, IR_VOID, local_count + d, stack[d], BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
    }
}

static uint32_t ir_typed_address(IrTypedSink* sink, uint32_t base_bits, uint32_t index, uint64_t scale)
{
    const uint32_t base = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, base_bits, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
    {
        const uint32_t factor = ir_typed_constant(sink, IR_I64, scale);
        index = ir_typed_emit(sink, IR_MUL, IR_I64, index, factor, BUSTER_IR_NONE, 0);
        scale = 1;
    }
    const uint32_t address = (uint32_t)sink->sizes.address_count;
    sink->sizes.address_count += 1;
    if (sink->output != 0)
    {
        sink->output->addresses[address] = (IrAddress){
            .offset = 0,
            .scale = (uint8_t)scale,
        };
    }
    const uint32_t pointer = ir_typed_emit(sink, IR_ADDRESS, IR_PTR, base, index, address, 0);
    return ir_typed_emit(sink, IR_PTRTOINT, IR_I64, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
}

static uint32_t ir_typed_call(IrTypedSink* sink, const Tier0Function* input, uint32_t call, IrType result_type, const uint32_t* arguments)
{
    const uint32_t descriptor = (uint32_t)sink->sizes.call_count;
    const uint32_t count = input->call_argument_counts[call];
    const uint32_t first = (uint32_t)sink->sizes.extra_count;
    const uint32_t type_first = (uint32_t)sink->sizes.call_type_count;
    if (sink->output != 0)
    {
        IrFunction* output = sink->output;
        output->calls[descriptor] = (IrCall){
            .argument_first = first,
            .argument_count = count,
            .type_first = type_first,
            .parameter_count = count,
            .result_type = (uint8_t)result_type,
            .variadic = input->call_variadic[call] != 0,
        };
        for (uint32_t a = 0; a < count; a += 1)
        {
            output->extra[first + a] = arguments[a];
            output->call_types[type_first + a] = ir_typed_types[input->argument_classes[input->call_first[call] + a]];
        }
    }
    sink->sizes.call_count += 1;
    sink->sizes.extra_count += count;
    sink->sizes.call_type_count += count;
    return ir_typed_emit(sink, IR_CALL, result_type, input->call_targets[call], descriptor, BUSTER_IR_NONE, 0);
}

static IrSizes ir_typed_emit_function(const Tier0Function* input, IrType result_type, const IrTypedPlan* plan, const Arena* scratch, const uint8_t* ops, const uint8_t* types, uint32_t* stack, uint8_t* classes, IrFunction* output)
{
    IrTypedSink sink = {
        .output = output,
        .sizes = {
            .block_count = (uint64_t)plan->block_count + 1,
            .slot_count = (uint64_t)input->local_count + plan->maximum_spill_depth,
            .parameter_count = input->parameter_count,
        },
    };
    const uint32_t slots = input->local_count + plan->maximum_spill_depth;
    // Block zero is a genuine preheader. A source backedge to entry zero goes
    // to block one and never executes parameter initialization again.
    if (output != 0)
    {
        output->block_offsets[0] = 0;
        output->result_type = (uint8_t)result_type;
    }
    for (uint32_t s = 0; s < slots; s += 1)
    {
        ir_typed_emit(&sink, IR_STACK_ADDR, IR_PTR, s, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        if (output != 0)
        {
            const bool local = s < input->local_count;
            const uint8_t class = local ? input->local_classes[s] : BUSTER_TYPED_CLASS_U64;
            output->slots[s] = (IrSlot){
                .size = tier0_class_bytes(class),
                .alignment = 1u << ir_typed_alignment(class),
                .type = local ? ir_typed_types[class] : IR_VOID,
                .flags = local ? ((input->local_flags[s] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 ? BUSTER_IR_SLOT_ADDRESS_TAKEN : 0) : BUSTER_IR_SLOT_TEMPORARY,
            };
        }
    }
    for (uint32_t p = 0; p < input->parameter_count; p += 1)
    {
        const uint8_t class = input->local_classes[p];
        const IrType type = (IrType)ir_typed_types[class];
        if (output != 0)
        {
            output->parameter_types[p] = (uint8_t)type;
        }
        const uint32_t parameter = ir_typed_emit(&sink, IR_PARAM, type, p, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        ir_typed_emit(&sink, IR_STORE, IR_VOID, p, parameter, BUSTER_IR_NONE, ir_typed_alignment(class));
    }
    ir_typed_emit(&sink, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);

    for (uint32_t b = 0; b < plan->block_count; b += 1)
    {
        if (output != 0)
        {
            output->block_offsets[b + 1] = (uint32_t)sink.sizes.instruction_count;
        }
        const uint32_t first = plan->block_firsts[b];
        const uint32_t end = plan->block_firsts[b + 1];
        uint32_t depth = plan->stack_depths[b];
        memcpy(classes, scratch->base + plan->stack_offsets[b], depth);
        // Values at the typed endpoint are discarded, so it needs no reloads.
        if (first < input->count)
        {
            for (uint32_t d = 0; d < depth; d += 1)
            {
                stack[d] = ir_typed_emit(&sink, IR_LOAD, (IrType)ir_typed_types[classes[d]], input->local_count + d, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
            }
        }
        bool terminated = false;
        for (uint32_t i = first; i < end; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint8_t class = input->classes[i];
            const IrType type = (IrType)types[i];
            const uint64_t value = input->values[i];
            switch (kind)
            {
            case BUSTER_TYPED_CONST:
                stack[depth] = ir_typed_constant(&sink, type, value);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_LOCAL:
                stack[depth] = ir_typed_emit(&sink, IR_LOAD, type, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(class));
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_LOCAL_ADDRESS:
                stack[depth] = ir_typed_emit(&sink, IR_PTRTOINT, IR_I64, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_GLOBAL:
            case BUSTER_TYPED_GLOBAL_ADDRESS:
            {
                const uint32_t address = ir_typed_emit(&sink, IR_SYMBOL_ADDR, IR_PTR, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                stack[depth] = ir_typed_emit(&sink, kind == BUSTER_TYPED_GLOBAL ? IR_LOAD : IR_PTRTOINT, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth] = class;
                depth += 1;
                break;
            }
            case BUSTER_TYPED_DUP:
                stack[depth] = stack[depth - 1];
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_NEG:
            case BUSTER_TYPED_NOT:
                stack[depth - 1] = ir_typed_emit(&sink, (IrOp)ops[i], type, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                break;
            case BUSTER_TYPED_CONVERT:
                stack[depth - 1] = ir_typed_convert(&sink, stack[depth - 1], classes[depth - 1], class);
                classes[depth - 1] = class;
                break;
            case BUSTER_TYPED_LOAD:
            {
                const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                stack[depth - 1] = ir_typed_emit(&sink, IR_LOAD, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth - 1] = class;
                break;
            }
            case BUSTER_TYPED_PTR_ADD:
                depth -= 1;
                stack[depth - 1] = ir_typed_address(&sink, stack[depth - 1], stack[depth], value);
                classes[depth - 1] = class;
                break;
            case BUSTER_TYPED_STORE:
            case BUSTER_TYPED_SET:
            {
                const uint32_t stored = stack[depth - 1];
                const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth - 2], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_STORE, IR_VOID, address, stored, BUSTER_IR_NONE, 0);
                depth -= 2;
                if (kind == BUSTER_TYPED_STORE)
                {
                    stack[depth] = stored;
                    classes[depth] = class;
                    depth += 1;
                }
                break;
            }
            case BUSTER_TYPED_STORE_LOCAL:
            case BUSTER_TYPED_SET_LOCAL:
            case BUSTER_TYPED_STORE_GLOBAL:
            case BUSTER_TYPED_SET_GLOBAL:
            {
                const bool local = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
                const uint32_t address = local ? (uint32_t)value : ir_typed_emit(&sink, IR_SYMBOL_ADDR, IR_PTR, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_STORE, IR_VOID, address, stack[depth - 1], BUSTER_IR_NONE, local ? ir_typed_alignment(class) : 0);
                if (kind == BUSTER_TYPED_SET_LOCAL || kind == BUSTER_TYPED_SET_GLOBAL)
                {
                    depth -= 1;
                }
                break;
            }
            case BUSTER_TYPED_CALL:
            {
                depth -= input->call_argument_counts[value];
                const uint32_t result = ir_typed_call(&sink, input, (uint32_t)value, type, stack + depth);
                if (class != BUSTER_TYPED_CLASS_VOID)
                {
                    stack[depth] = result;
                    classes[depth] = class;
                    depth += 1;
                }
                break;
            }
            case BUSTER_TYPED_DISCARD:
                depth -= 1;
                break;
            case BUSTER_TYPED_BRANCH_FALSE:
            case BUSTER_TYPED_BRANCH_TRUE:
            {
                depth -= 1;
                const uint32_t zero = ir_typed_constant(&sink, type, 0);
                const uint32_t condition = ir_typed_emit(&sink, IR_ICMP, IR_I1, stack[depth], zero, IR_NE, 0);
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                const uint32_t target = plan->entry_blocks[value] + 1;
                const uint32_t next = b + 2;
                ir_typed_emit(&sink, IR_CBR, IR_VOID, condition, kind == BUSTER_TYPED_BRANCH_TRUE ? target : next, kind == BUSTER_TYPED_BRANCH_TRUE ? next : target, 0);
                terminated = true;
                break;
            }
            case BUSTER_TYPED_JUMP:
            case BUSTER_TYPED_JUMP_VALUE:
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                ir_typed_emit(&sink, IR_BR, IR_VOID, plan->entry_blocks[value] + 1, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                terminated = true;
                break;
            case BUSTER_TYPED_RETURN:
            {
                const uint32_t returned = class == BUSTER_TYPED_CLASS_VOID ? BUSTER_IR_NONE : stack[--depth];
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                terminated = true;
                break;
            }
            default:
            {
                const uint32_t right = stack[depth - 1];
                const uint32_t left = stack[depth - 2];
                const uint8_t operand_class = classes[depth - 2];
                const uint8_t right_class = classes[depth - 1];
                depth -= 1;
                if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
                {
                    uint32_t predicate = kind - BUSTER_TYPED_EQ;
                    if (predicate >= IR_LT && !tier0_class_floating(operand_class) && !tier0_class_signed(operand_class))
                    {
                        predicate += IR_ULT - IR_LT;
                    }
                    const uint32_t comparison = ir_typed_emit(&sink, tier0_class_floating(operand_class) ? IR_FCMP : IR_ICMP, IR_I1, left, right, predicate, 0);
                    stack[depth - 1] = ir_typed_emit(&sink, IR_ZEXT, IR_I32, comparison, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                }
                else
                {
                    const bool shift = kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR;
                    const uint32_t converted = shift ? ir_typed_convert(&sink, right, right_class, class) : right;
                    stack[depth - 1] = ir_typed_emit(&sink, (IrOp)ops[i], type, left, converted, BUSTER_IR_NONE, 0);
                }
                classes[depth - 1] = class;
                break;
            }
            }
        }
        if (!terminated)
        {
            if (first == input->count)
            {
                // The typed endpoint denotes an epilogue. An explicit semantic
                // result type is required from the producer; nonvoid fallthrough
                // returns undef, as the old stream leaves its result unspecified.
                const uint32_t returned = result_type == IR_VOID ? BUSTER_IR_NONE : ir_typed_emit(&sink, IR_UNDEF, result_type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
            else
            {
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                ir_typed_emit(&sink, IR_BR, IR_VOID, b + 2, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
        }
    }
    if (output != 0)
    {
        output->block_offsets[plan->block_count + 1] = (uint32_t)sink.sizes.instruction_count;
    }
    return sink.sizes;
}

// ---------------------------------------------------------------------------------------------------------------------
// Entry points

static IrCheck ir_lower_typed_path(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result, bool vector)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrCheck check = {0};
    const bool supported_result = result_type == IR_VOID || result_type == IR_I8 || result_type == IR_I16 || result_type == IR_I32 || result_type == IR_I64 || result_type == IR_F32 || result_type == IR_F64;
    if (output == scratch || result == 0 || input == 0 || !supported_result || symbol_count == 0)
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    else if (input->count > BUSTER_TIER0_MAX_ENTRIES || input->local_count > BUSTER_TIER0_MAX_LOCALS || input->parameter_count > input->local_count)
    {
        check.error = IR_ERROR_LIMIT;
    }
    else if ((input->count != 0 && (input->kinds == 0 || input->classes == 0 || input->values == 0)) ||
             (input->local_count != 0 && (input->local_classes == 0 || input->local_flags == 0)) ||
             (input->parameter_count != 0 && input->parameters == 0) ||
             (input->call_count != 0 && (input->call_targets == 0 || input->call_first == 0 || input->call_argument_counts == 0 || input->call_variadic == 0)) ||
             (input->argument_count != 0 && input->argument_classes == 0))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    // An incoming variadic frame (its pointer compared, never read), a local
    // flag other than ADDRESS_TAKEN (BUSTER_TYPED_LOCAL_ADDRESS_ONLY's anchors
    // and va_list objects, or a later role) and an object (a local with a
    // size in local_sizes: an array, a struct or a union, of class VOID) are
    // frame state this adapter doesn't model: lowering a role or an object as
    // an ordinary slot would size and place it wrongly (an object's slot would
    // take VOID's bytes and overlap its neighbours), so they are refused
    // before the stream is validated or anything is allocated.
    if (check.error == IR_OK && input->incoming_variadic != 0)
    {
        check.error = IR_ERROR_METADATA;
        check.instruction = BUSTER_IR_NONE;
        check.detail = BUSTER_IR_NONE;
    }
    for (uint32_t l = 0; check.error == IR_OK && l < input->local_count; l += 1)
    {
        if ((input->local_flags[l] & ~BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 || (input->local_sizes != 0 && input->local_sizes[l] != 0))
        {
            check.error = IR_ERROR_METADATA;
            check.instruction = BUSTER_IR_NONE;
            check.detail = l;
        }
    }
    if (check.error == IR_OK)
    {
        const Tier0Check typed = tier0_validate(input);
        if (typed.error != TIER0_OK)
        {
            check.error = IR_ERROR_STRUCTURE;
            check.instruction = typed.node;
        }
    }
    for (uint32_t i = 0; check.error == IR_OK && i < input->count; i += 1)
    {
        const uint8_t kind = input->kinds[i];
        const uint64_t value = input->values[i];
        const bool symbol = kind == BUSTER_TYPED_GLOBAL || kind == BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL;
        if ((symbol && (value == 0 || value >= symbol_count)) ||
            (kind == BUSTER_TYPED_RETURN && ir_typed_types[input->classes[i]] != result_type))
        {
            check.error = IR_ERROR_TYPE;
            check.instruction = i;
        }
        else if (kind == BUSTER_TYPED_CALL && (input->call_targets[value] == 0 || input->call_targets[value] >= symbol_count))
        {
            check.error = IR_ERROR_OPERAND;
            check.instruction = i;
        }
        else if (kind > BUSTER_TYPED_RETURN)
        {
            // A kind this adapter doesn't lower (COPY, and those after it),
            // which the plan and the emitter would take for a binary operation.
            check.error = IR_ERROR_OPCODE;
            check.instruction = i;
        }
    }
    IrTypedPlan plan = {0};
    uint8_t* ops = 0;
    uint8_t* types = 0;
    uint8_t* classes = 0;
    uint32_t* stack = 0;
    if (check.error == IR_OK)
    {
        plan = ir_typed_plan(input, scratch);
        if (plan.error != 0)
        {
            check.error = plan.error_code;
            check.instruction = plan.error_entry;
        }
        else
        {
            ops = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)input->count + 1);
            types = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)input->count + 1);
            classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)plan.maximum_depth + 1);
            stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)plan.maximum_depth + 1);
            if (ops == 0 || types == 0 || classes == 0 || stack == 0)
            {
                check.error = IR_ERROR_MEMORY;
            }
        }
    }
    IrFunction candidate = {0};
    if (check.error == IR_OK)
    {
        ir_typed_classify(input, ops, types, vector);
        const IrSizes sizes = ir_typed_emit_function(input, result_type, &plan, scratch, ops, types, stack, classes, 0);
        check.error = ir_create(output, &sizes, &candidate);
        if (check.error == IR_OK)
        {
            ir_typed_emit_function(input, result_type, &plan, scratch, ops, types, stack, classes, &candidate);
            candidate.symbol_count = symbol_count;
            check = vector ? ir_verify(&candidate, scratch, 0) : ir_verify_scalar(&candidate, scratch, 0);
        }
    }
    if (check.error == IR_OK)
    {
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

// The legacy typed stream erases pointer categories into U64, so its i64 values
// cross explicit ptrtoint/inttoptr instructions at memory/address boundaries.
// result_type is mandatory semantic information; it is not inferred from the
// ABI's return-location record. Calls retain all supplied argument classes and
// the variadic bit, but the stream does not carry the named-parameter count.
// Consequently each current argument is recorded as a checked parameter; a
// future complete checker adapter must provide its actual prototype boundary.
// Both paths preserve the input and caller's result on failure and reclaim all
// scratch. Output, scratch, and input storage must be disjoint.
static __attribute__((unused)) IrCheck ir_lower_typed(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_path(input, result_type, symbol_count, output, scratch, result, true);
}

static __attribute__((unused)) IrCheck ir_lower_typed_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_path(input, result_type, symbol_count, output, scratch, result, false);
}
