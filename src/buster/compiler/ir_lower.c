// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Lower the existing typed stack stream into Tier 1's all-in-memory IR. Include after tier0.c and ir.c.
// ir_lower_typed and ir_lower_typed_scalar validate the input, plan block/stack shapes, count output, then emit directly
// into exact-sized columns. Locals and expression values crossing edges use stack slots for #134 to promote. A separate
// preheader initializes parameters once, even when source entry zero is a loop target. Pointer-valued U64 entries are
// converted explicitly at pointer operations. ABI locations never enter the IR. An explicit semantic return type is
// required because Tier0Function.result only describes the ABI. Frame objects become address-taken slots of their own
// size and alignment, and COPY loads and stores of integer pieces. See docs/ir.md for limits and pending integration.
// Layout: IrTypedPlan/ir_typed_plan; ir_typed_classify; IrTypedSink and count/fill replay in ir_lower_emit.c;
// ir_lower_typed_path.

// Preserve the typed validator's width-qualified stack classes. The byte classifier remains unchanged for scalar
// entries; only VECTOR entries need this sparse metadata lookup. DUP and control-only entries inherit their stack.
static uint32_t ir_typed_vector_bytes(const Tier0Function* input, uint32_t entry)
{
    const uint64_t value = input->values[entry];
    uint32_t bytes = 0;
    switch (input->kinds[entry])
    {
        case BUSTER_TYPED_CONST: bytes = input->vector_constant_bytes[value]; break;
        case BUSTER_TYPED_LOCAL:
        case BUSTER_TYPED_STORE_LOCAL:
        case BUSTER_TYPED_SET_LOCAL: bytes = input->local_sizes[value]; break;
        case BUSTER_TYPED_LOAD:
        case BUSTER_TYPED_STORE:
        case BUSTER_TYPED_SET: bytes = (uint32_t)value; break;
        case BUSTER_TYPED_X86: bytes = input->operations[value].result_bytes; break;
        default: break;
    }
    return bytes;
}

// A MASK entry's bytes (#179), as tier0_validate follows them from depth to depth (tier0_stack_mask): its value's (a
// conversion's, a load's, a store's, the logic's), its object's or its row's result's; DUP, DISCARD and JUMP_VALUE have
// none of their own and keep their operand's.
static uint32_t ir_typed_mask_bytes(const Tier0Function* input, uint32_t entry)
{
    const uint64_t value = input->values[entry];
    uint32_t bytes = 0;
    switch (input->kinds[entry])
    {
        case BUSTER_TYPED_LOCAL:
        case BUSTER_TYPED_STORE_LOCAL:
        case BUSTER_TYPED_SET_LOCAL: bytes = input->local_sizes[value]; break;
        case BUSTER_TYPED_X86: bytes = input->operations[value].result_bytes; break;
        case BUSTER_TYPED_CONVERT:
        case BUSTER_TYPED_LOAD:
        case BUSTER_TYPED_STORE:
        case BUSTER_TYPED_SET:
        case BUSTER_TYPED_AND:
        case BUSTER_TYPED_OR:
        case BUSTER_TYPED_XOR:
        case BUSTER_TYPED_NOT: bytes = (uint32_t)value; break;
        default: break;
    }
    return bytes;
}

// An entry's class on the replay's stack: a VECTOR's or a MASK's by its bytes, as tier0_validate's stack has them
// (BUSTER_TIER0_STACK_VECTOR, tier0_stack_mask); any other's its class.
static uint8_t ir_typed_stack_class(const Tier0Function* input, uint32_t entry)
{
    const uint8_t class = input->classes[entry];
    const uint32_t bytes = class == BUSTER_TYPED_CLASS_VECTOR ? ir_typed_vector_bytes(input, entry) : class == BUSTER_TYPED_CLASS_MASK ? ir_typed_mask_bytes(input, entry) : 0;
    return bytes == 0 ? class : class == BUSTER_TYPED_CLASS_VECTOR ? (uint8_t)BUSTER_TIER0_STACK_VECTOR(bytes) : (uint8_t)tier0_stack_mask(bytes);
}

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
    // The depth slots' bytes and alignment: at least 8, 16 for scalar memory classes, or the widest admitted vector.
    uint32_t spill_bytes;
    const IrSimdTypedOperation* operations;
    // The goto *s (the JUMP_INDIRECT entries no jump table has, #785), which all go through the function's one dispatch
    // block (the emitter's last); a jump table's goes to its table's labels from its own block.
    uint32_t indirect;
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

// The plan's and the emitter's default cases take every kind they don't name for a binary operation, so a kind added
// (before X86, the last) must be lowered (or refused, as FRAME_ADDRESS and TAIL_CALL are, and the X86 rows past the
// SIMD subset) here before tier0_validate accepts it.
_Static_assert(BUSTER_TYPED_KINDS == 49 && BUSTER_TYPED_KINDS == BUSTER_TYPED_X86 + 1, "ir_lower.c lowers or refuses every typed kind");

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
            if (ir_typed_control(kind) || kind == BUSTER_TYPED_JUMP_INDIRECT)
            {
                leaders[i + 1] = 1;
                if (kind != BUSTER_TYPED_RETURN && kind != BUSTER_TYPED_JUMP_INDIRECT)
                {
                    // tier0_validate already established values[i] <= count.
                    leaders[input->values[i]] = 1;
                }
            }
        }
        // Every label starts a block, which an indirect jump may reach (tier0_validate: each entry at most count).
        for (uint32_t k = 0; k < input->label_count; k += 1)
        {
            leaders[input->label_entries[k]] = 1;
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
        uint32_t table = 0;
        plan.spill_bytes = 8;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t bytes = input->classes[i] == BUSTER_TYPED_CLASS_VECTOR ? ir_typed_vector_bytes(input, i) : tier0_class_memory(input->classes[i]) ? 16 : 8;
            plan.spill_bytes = bytes > plan.spill_bytes ? bytes : plan.spill_bytes;
        }
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
                const uint8_t class = ir_typed_stack_class(input, i);
                const uint64_t value = input->values[i];
                switch (kind)
                {
                case BUSTER_TYPED_CONST:
                case BUSTER_TYPED_LOCAL:
                case BUSTER_TYPED_LOCAL_ADDRESS:
                case BUSTER_TYPED_GLOBAL:
                case BUSTER_TYPED_GLOBAL_ADDRESS:
                case BUSTER_TYPED_STACK_SAVE:
                    stack[depth] = class;
                    depth += 1;
                    break;
                case BUSTER_TYPED_DUP:
                    stack[depth] = stack[depth - 1];
                    depth += 1;
                    break;
                case BUSTER_TYPED_X86:
                    depth -= input->operations[value].operand_count;
                    stack[depth] = class;
                    depth += 1;
                    break;
                case BUSTER_TYPED_NEG:
                case BUSTER_TYPED_NOT:
                case BUSTER_TYPED_CONVERT:
                case BUSTER_TYPED_LOAD:
                case BUSTER_TYPED_ALLOCA:
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_STORE_LOCAL:
                case BUSTER_TYPED_STORE_GLOBAL:
                    // Consume and reproduce the value at the same depth.
                    break;
                case BUSTER_TYPED_SET_LOCAL:
                case BUSTER_TYPED_SET_GLOBAL:
                case BUSTER_TYPED_DISCARD:
                case BUSTER_TYPED_STACK_RESTORE:
                    depth -= 1;
                    break;
                case BUSTER_TYPED_STORE:
                    depth -= 1;
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_SET:
                case BUSTER_TYPED_COPY:
                    depth -= 2;
                    break;
                case BUSTER_TYPED_CALL:
                    // An indirect call's address (a calls table's target 0) is under its arguments.
                    depth -= input->call_argument_counts[value] + (input->call_targets[value] == 0);
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
                case BUSTER_TYPED_JUMP_INDIRECT:
                {
                    // At statement level: the address popped. A jump table's jump arrives at its table's labels with the
                    // stacks empty, and every goto * goes to the dispatch block, which arrives at the labels of no table
                    // so, once (table_jumps is in entry order: `table` its next).
                    depth -= 1;
                    const bool tabled = table < input->table_count && input->table_jumps[table] == i;
                    for (uint32_t k = 0; ok && (tabled || plan.indirect == 0) && k < input->label_count; k += 1)
                    {
                        const uint32_t owner = input->table_count != 0 ? input->label_tables[k] : 0;
                        if (owner == (tabled ? table + 1 : 0))
                        {
                            ok = ir_typed_plan_arrive(&plan, scratch, plan.entry_blocks[input->label_entries[k]], stack, depth, i);
                        }
                    }
                    plan.indirect += tabled ? 0 : 1;
                    table += tabled ? 1 : 0;
                    reachable = false;
                } break;
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
//    and alignment 8. Source slots keep the typed local's actual size/alignment
//    (an object's from local_sizes and local_alignments, with type VOID);
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
// 6. A goto * stores its address in the dispatch slot and branches to the
//    dispatch block, the last, which loads it and is the br_indirect of every
//    label no jump table has, so that its CFG has an edge per jump and per
//    label. A jump table's jump is a br_indirect of its own table's labels.
// 7. Verify the IR and build compact CFG arrays after construction. #134 can
//    promote both ordinary local slots and the expression-depth slots later.

// ---------------------------------------------------------------------------------------------------------------------
// Classification and the two-pass emission sink

// Each class's IR type: long double's F80 and the 128-bit integers' I128 and U128 are values in the IR, which Tier 0 keeps
// in its frame slots. VECTOR is resolved separately from its width metadata by the explicit-target entry points.
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
    IR_F80,
    IR_I128,
    IR_I128,
};

// Which classes are signed and which floating, as byte tables both classifications read: Tier 0's own predicates leave
// out the memory classes (I128 signed, F80 floating), which it compiles apart.
static IrType ir_typed_stack_type(uint8_t class)
{
    // A mask's (#179) m8 to m64 by its bytes, 1 to 8 (tier0_stack_mask's order).
    const IrType type = tier0_stack_is_mask(class)            ? (IrType)(IR_M8 + (class - BUSTER_TIER0_STACK_MASKS)) :
                        class >= BUSTER_TYPED_CLASS_LIMIT     ? class == BUSTER_TIER0_STACK_VECTOR(16) ? IR_V4I32 : class == BUSTER_TIER0_STACK_VECTOR(32) ? IR_V8I32 : IR_V16I32 :
                                                                (IrType)ir_typed_types[class];
    return type;
}

static const uint8_t ir_typed_signed_classes[16] = {
    [BUSTER_TYPED_CLASS_I8] = 1,
    [BUSTER_TYPED_CLASS_I16] = 1,
    [BUSTER_TYPED_CLASS_I32] = 1,
    [BUSTER_TYPED_CLASS_I64] = 1,
    [BUSTER_TYPED_CLASS_I128] = 1,
};

static const uint8_t ir_typed_floating_classes[16] = {
    [BUSTER_TYPED_CLASS_F32] = 1,
    [BUSTER_TYPED_CLASS_F64] = 1,
    [BUSTER_TYPED_CLASS_F80] = 1,
};

static bool ir_typed_signed(uint8_t class)
{
    return ir_typed_signed_classes[class & 15] != 0;
}

static bool ir_typed_floating(uint8_t class)
{
    return ir_typed_floating_classes[class & 15] != 0;
}

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
    (void)vector;

    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i integer_ops = _mm512_loadu_si512(ir_typed_integer_ops);
        const __m512i signed_ops = _mm512_loadu_si512(ir_typed_signed_ops);
        const __m512i floating_ops = _mm512_loadu_si512(ir_typed_floating_ops);
        const __m128i type_table = _mm_loadu_si128((const __m128i*)ir_typed_types);
        const __m128i signed_table = _mm_loadu_si128((const __m128i*)ir_typed_signed_classes);
        const __m128i floating_table = _mm_loadu_si128((const __m128i*)ir_typed_floating_classes);
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
            const __mmask16 is_signed = _mm_test_epi8_mask(_mm_shuffle_epi8(signed_table, classes), _mm_set1_epi8(1));
            const __mmask16 is_floating = _mm_test_epi8_mask(_mm_shuffle_epi8(floating_table, classes), _mm_set1_epi8(1));
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
            ops[i] = ir_typed_floating(class) ? ir_typed_floating_ops[kind] : ir_typed_signed(class) ? ir_typed_signed_ops[kind] : ir_typed_integer_ops[kind];
        }
    }
}

typedef struct IrTypedSink IrTypedSink;
struct IrTypedSink
{
    IrFunction* output;
    IrSizes sizes;
};

// Keep constructor updates visible in both replay instantiations: count erases column writes and fill erases null
// checks. This preserves the baseline leaf-inlining decisions without forcing the whole replay into its caller.
static __attribute__((always_inline)) inline uint32_t ir_typed_emit(IrTypedSink* sink, IrOp op, IrType type, uint32_t a, uint32_t b, uint32_t c, uint8_t alignment_log2)
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

// A constant of `type`: `bits` its low 64 bits, `high` the rest (an i128's high half, an f80's sign and exponent).
static __attribute__((always_inline)) inline uint32_t ir_typed_wide_constant(IrTypedSink* sink, IrType type, uint64_t bits, uint64_t high)
{
    const uint32_t constant = (uint32_t)sink->sizes.constant_count;
    if (sink->output != 0)
    {
        const uint32_t width = ir_type_bits[type];
        sink->output->constants[constant] = (IrConstant){
            .low = width < 64 ? bits & ((1ull << width) - 1) : bits,
            .high = width > 64 ? high & (width < 128 ? (1ull << (width - 64)) - 1 : UINT64_MAX) : 0,
        };
    }
    sink->sizes.constant_count += 1;
    return ir_typed_emit(sink, IR_CONST, type, constant, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
}

static __attribute__((always_inline)) inline uint32_t ir_typed_constant(IrTypedSink* sink, IrType type, uint64_t bits)
{
    return ir_typed_wide_constant(sink, type, bits, 0);
}

// Sparse payloads are canonicalized: unused bytes in a producer's 64-byte constant row are not IR value bits.
static __attribute__((always_inline)) inline uint32_t ir_typed_vector_constant(IrTypedSink* sink, IrType type, const uint64_t* words)
{
    const uint32_t constant = (uint32_t)sink->sizes.vector_constant_count;
    if (sink->output != 0)
    {
        IrSimdValue value = {0};
        memcpy(value.words, words, ir_type_bits[type] / 8);
        sink->output->vector_constants[constant] = value;
    }
    sink->sizes.vector_constant_count += 1;
    return ir_typed_emit(sink, IR_VCONST, type, constant, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
}

static __attribute__((always_inline)) inline uint32_t ir_typed_vector_operation(IrTypedSink* sink, const IrSimdTypedOperation* operation, IrType type, const uint32_t* operands)
{
    static const uint8_t ops[IR_SIMD_OPERATION_COUNT] = {
        [IR_SIMD_ADD] = IR_ADD,
        [IR_SIMD_SUB] = IR_SUB,
        [IR_SIMD_MUL] = IR_MUL,
        [IR_SIMD_AND] = IR_AND,
        [IR_SIMD_OR] = IR_OR,
        [IR_SIMD_XOR] = IR_XOR,
        [IR_SIMD_NOT] = IR_NOT,
    };
    uint32_t value = BUSTER_IR_NONE;
    if (operation->expand_mask)
    {
        const IrType mask_type = type == IR_V16I32 ? IR_M16 : IR_M8;
        const uint32_t mask = ir_typed_emit(sink, IR_VCMP, mask_type, operands[0], operands[1], operation->predicate, 0);
        const uint64_t zero[8] = {0};
        const uint64_t ones[8] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
        const uint32_t active = ir_typed_vector_constant(sink, type, ones);
        const uint32_t inactive = ir_typed_vector_constant(sink, type, zero);
        value = ir_typed_emit(sink, IR_VSELECT, type, mask, active, inactive, 0);
    }
    else
    {
        value = ir_typed_emit(sink, (IrOp)ops[operation->operation], type, operands[0], operation->operand_count == 2 ? operands[1] : BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return value;
}

// A mask's bits as an integer of at least 32 bits, which Tier 1's arithmetic takes (an m8's or m16's zero-extended past
// its bitcast), and back: the mask of `mask`'s width of that integer's low bits.
static __attribute__((always_inline)) inline uint32_t ir_typed_mask_integer(IrTypedSink* sink, uint32_t value, IrType mask)
{
    const IrType bits = mask == IR_M8 ? IR_I8 : mask == IR_M16 ? IR_I16 : mask == IR_M32 ? IR_I32 : IR_I64;
    uint32_t integer = ir_typed_emit(sink, IR_BITCAST, bits, value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    if (bits == IR_I8 || bits == IR_I16)
    {
        integer = ir_typed_emit(sink, IR_ZEXT, IR_I32, integer, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return integer;
}

static __attribute__((always_inline)) inline uint32_t ir_typed_integer_mask(IrTypedSink* sink, uint32_t integer, IrType mask)
{
    const IrType bits = mask == IR_M8 ? IR_I8 : mask == IR_M16 ? IR_I16 : mask == IR_M32 ? IR_I32 : IR_I64;
    if (bits == IR_I8 || bits == IR_I16)
    {
        integer = ir_typed_emit(sink, IR_TRUNC, bits, integer, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return ir_typed_emit(sink, IR_BITCAST, mask, integer, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
}

// A row of opmask instructions alone (#179, ir_simd_typed_mask_row) as IR: each step over its masks' registers, the
// operands' values first in theirs, at the masks' width. and, or, xor and not are the masks' own; xnor and andn their
// compositions; add, the shifts and unpack the masks' bits as an integer (ir_typed_mask_integer: no arithmetic is a
// mask's), whose low bits are the instruction's; a shift by the masks' bits or more the zero mask, as kshiftl and kshiftr
// give. A move copies the value. A flags test's row (ir_simd_typed_mask_test) is its one step's int. Returns the
// result's value.
static __attribute__((always_inline)) inline uint32_t ir_typed_mask_row(IrTypedSink* sink, const IrSimdTypedOperation* operation, const uint32_t* operands)
{
    const IrType mask = operation->mask_bytes == 1 ? IR_M8 : operation->mask_bytes == 2 ? IR_M16 : operation->mask_bytes == 4 ? IR_M32 : IR_M64;
    const IrType wide = mask == IR_M64 ? IR_I64 : IR_I32;
    const uint32_t width = 8u * operation->mask_bytes;
    uint32_t registers[BUSTER_IR_SIMD_MASK_REGISTERS];
    for (uint32_t r = 0; r < BUSTER_IR_SIMD_MASK_REGISTERS; r += 1)
    {
        registers[r] = r < operation->operand_count ? operands[r] : BUSTER_IR_NONE;
    }
    for (uint32_t k = 0; k < operation->mask_step_count; k += 1)
    {
        const IrSimdTypedMaskStep* step = &operation->mask_steps[k];
        const uint32_t left = registers[step->left];
        const uint32_t right = registers[step->right];
        uint32_t value = left;
        switch (step->kind)
        {
            case IR_SIMD_MASK_AND: value = ir_typed_emit(sink, IR_AND, mask, left, right, BUSTER_IR_NONE, 0); break;
            case IR_SIMD_MASK_OR: value = ir_typed_emit(sink, IR_OR, mask, left, right, BUSTER_IR_NONE, 0); break;
            case IR_SIMD_MASK_XOR: value = ir_typed_emit(sink, IR_XOR, mask, left, right, BUSTER_IR_NONE, 0); break;
            case IR_SIMD_MASK_XNOR:
            {
                const uint32_t either = ir_typed_emit(sink, IR_XOR, mask, left, right, BUSTER_IR_NONE, 0);
                value = ir_typed_emit(sink, IR_NOT, mask, either, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            } break;
            case IR_SIMD_MASK_ANDN:
            {
                const uint32_t complement = ir_typed_emit(sink, IR_NOT, mask, left, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                value = ir_typed_emit(sink, IR_AND, mask, complement, right, BUSTER_IR_NONE, 0);
            } break;
            case IR_SIMD_MASK_NOT: value = ir_typed_emit(sink, IR_NOT, mask, left, BUSTER_IR_NONE, BUSTER_IR_NONE, 0); break;
            case IR_SIMD_MASK_MOVE: break;
            case IR_SIMD_MASK_TEST:
            case IR_SIMD_MASK_ORTEST:
            {
                // The mask that is 0 exactly where the flag is set (ktest's left & right or ~left & right, kortest's
                // left | right or its complement), its bits' integer compared with 0, and that int.
                const bool carry = step->count != 0;
                uint32_t tested = BUSTER_IR_NONE;
                if (step->kind == IR_SIMD_MASK_TEST)
                {
                    const uint32_t first = carry ? ir_typed_emit(sink, IR_NOT, mask, left, BUSTER_IR_NONE, BUSTER_IR_NONE, 0) : left;
                    tested = ir_typed_emit(sink, IR_AND, mask, first, right, BUSTER_IR_NONE, 0);
                }
                else
                {
                    const uint32_t either = ir_typed_emit(sink, IR_OR, mask, left, right, BUSTER_IR_NONE, 0);
                    tested = carry ? ir_typed_emit(sink, IR_NOT, mask, either, BUSTER_IR_NONE, BUSTER_IR_NONE, 0) : either;
                }
                const uint32_t integer = ir_typed_mask_integer(sink, tested, mask);
                const uint32_t zero = ir_typed_constant(sink, wide, 0);
                const uint32_t flag = ir_typed_emit(sink, IR_ICMP, IR_I1, integer, zero, IR_EQ, 0);
                value = ir_typed_emit(sink, IR_ZEXT, IR_I32, flag, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            } break;
            case IR_SIMD_MASK_SHIFT_LEFT:
            case IR_SIMD_MASK_SHIFT_RIGHT:
                if (step->count >= width)
                {
                    value = ir_typed_constant(sink, mask, 0);
                }
                else
                {
                    // The widened integer's bits past the mask's are zero, so a right shift brings none in.
                    const uint32_t integer = ir_typed_mask_integer(sink, left, mask);
                    const uint32_t count = ir_typed_constant(sink, wide, step->count);
                    const uint32_t shifted = ir_typed_emit(sink, step->kind == IR_SIMD_MASK_SHIFT_LEFT ? IR_SHL : IR_LSHR, wide, integer, count, BUSTER_IR_NONE, 0);
                    value = ir_typed_integer_mask(sink, shifted, mask);
                }
                break;
            default:
            {
                // add, and unpack: left's low half above right's.
                const uint32_t a = ir_typed_mask_integer(sink, left, mask);
                const uint32_t b = ir_typed_mask_integer(sink, right, mask);
                uint32_t integer = BUSTER_IR_NONE;
                if (step->kind == IR_SIMD_MASK_ADD)
                {
                    integer = ir_typed_emit(sink, IR_ADD, wide, a, b, BUSTER_IR_NONE, 0);
                }
                else
                {
                    const uint32_t half = width / 2;
                    const uint32_t low = ir_typed_constant(sink, wide, (UINT64_C(1) << half) - 1);
                    const uint32_t high_half = ir_typed_emit(sink, IR_AND, wide, a, low, BUSTER_IR_NONE, 0);
                    const uint32_t low_half = ir_typed_emit(sink, IR_AND, wide, b, low, BUSTER_IR_NONE, 0);
                    const uint32_t shift = ir_typed_constant(sink, wide, half);
                    const uint32_t raised = ir_typed_emit(sink, IR_SHL, wide, high_half, shift, BUSTER_IR_NONE, 0);
                    integer = ir_typed_emit(sink, IR_OR, wide, raised, low_half, BUSTER_IR_NONE, 0);
                }
                value = ir_typed_integer_mask(sink, integer, mask);
            } break;
        }
        registers[step->destination] = value;
    }
    return registers[operation->mask_result];
}

// A stack class's natural alignment's log2: a mask's its bytes' (#179), a vector's its own, any other its class's bytes'.
static uint8_t ir_typed_alignment(uint8_t class)
{
    const bool mask = tier0_stack_is_mask(class);
    const bool vector = class >= BUSTER_TYPED_CLASS_LIMIT && !mask;
    const uint32_t bytes = vector ? (class - BUSTER_TYPED_CLASS_LIMIT) * 4u : mask ? 0 : tier0_class_bytes(class);
    return mask ? (uint8_t)(class - BUSTER_TIER0_STACK_MASKS) : vector ? (bytes == 64 ? 6 : bytes == 32 ? 5 : 4) : bytes == 8 ? 3 : bytes == 4 ? 2 : bytes == 2 ? 1 : 0;
}

// A conversion between an integer class and a mask's stack class (#179), as Tier 0's CONVERT is: to a mask, the integer
// truncated, or extended by its signedness, to the mask's bits and their bitcast; from one, the bitcast to an integer of
// its bits and that unsigned integer's truncation or zero extension.
static __attribute__((always_inline)) inline uint32_t ir_typed_convert_mask(IrTypedSink* sink, uint32_t value, uint8_t from_class, uint8_t to_class)
{
    const bool to_mask = tier0_stack_is_mask(to_class);
    const IrType mask = ir_typed_stack_type(to_mask ? to_class : from_class);
    const IrType bits = mask == IR_M8 ? IR_I8 : mask == IR_M16 ? IR_I16 : mask == IR_M32 ? IR_I32 : IR_I64;
    const IrType other = (IrType)ir_typed_types[to_mask ? from_class : to_class];
    uint32_t result = value;
    if (to_mask)
    {
        if (other != bits)
        {
            const IrOp op = ir_type_bits[other] > ir_type_bits[bits] ? IR_TRUNC : ir_typed_signed(from_class) ? IR_SEXT : IR_ZEXT;
            result = ir_typed_emit(sink, op, bits, result, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        }
        result = ir_typed_emit(sink, IR_BITCAST, mask, result, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    else
    {
        result = ir_typed_emit(sink, IR_BITCAST, bits, result, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        if (other != bits)
        {
            result = ir_typed_emit(sink, ir_type_bits[other] > ir_type_bits[bits] ? IR_ZEXT : IR_TRUNC, other, result, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        }
    }
    return result;
}

static __attribute__((always_inline)) inline uint32_t ir_typed_convert(IrTypedSink* sink, uint32_t value, uint8_t from_class, uint8_t to_class)
{
    const bool masks = tier0_stack_is_mask(from_class) || tier0_stack_is_mask(to_class);
    const IrType from = masks ? IR_VOID : (IrType)ir_typed_types[from_class];
    const IrType to = masks ? IR_VOID : (IrType)ir_typed_types[to_class];
    IrOp op = IR_INVALID;
    if (masks)
    {
        value = ir_typed_convert_mask(sink, value, from_class, to_class);
    }
    else if (from != to)
    {
        if (ir_integer(from) && ir_integer(to))
        {
            op = ir_type_bits[from] > ir_type_bits[to] ? IR_TRUNC : ir_typed_signed(from_class) ? IR_SEXT : IR_ZEXT;
        }
        else if (ir_floating(from) && ir_floating(to))
        {
            op = ir_type_bits[from] > ir_type_bits[to] ? IR_FTRUNC : IR_FEXT;
        }
        else if (ir_integer(from))
        {
            op = ir_typed_signed(from_class) ? IR_SITOFP : IR_UITOFP;
        }
        else
        {
            op = ir_typed_signed(to_class) ? IR_FPTOSI : IR_FPTOUI;
        }
        value = ir_typed_emit(sink, op, to, value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return value;
}

static __attribute__((always_inline)) inline void ir_typed_spill(IrTypedSink* sink, const uint32_t* stack, const uint8_t* classes, uint32_t depth, uint32_t local_count)
{
    for (uint32_t d = 0; d < depth; d += 1)
    {
        // All slot addresses are emitted first in the preheader, so an address
        // value's instruction ID is exactly its slot ID.
        ir_typed_emit(sink, IR_STORE, IR_VOID, local_count + d, stack[d], BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
    }
}

static __attribute__((always_inline)) inline uint32_t ir_typed_address(IrTypedSink* sink, uint32_t base_bits, uint32_t index, uint64_t scale)
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

// A pointer `offset` bytes past `base` (a pointer), with no index.
static __attribute__((always_inline)) inline uint32_t ir_typed_offset(IrTypedSink* sink, uint32_t base, uint64_t offset)
{
    uint32_t pointer = base;
    if (offset != 0)
    {
        const uint32_t address = (uint32_t)sink->sizes.address_count;
        sink->sizes.address_count += 1;
        if (sink->output != 0)
        {
            sink->output->addresses[address] = (IrAddress){
                .offset = (int64_t)offset,
                .scale = 1,
            };
        }
        pointer = ir_typed_emit(sink, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, address, 0);
    }
    return pointer;
}

// COPY's `bytes` (at most BUSTER_TIER0_MAX_COPY_BYTES) as a load and a store per piece, each the widest integer that
// fits what remains: 32 pairs of i64 at most. The typed stream's ranges are the same or apart, so each piece's load
// before its store reads the source's bytes either way. Their alignment is unknown, as LOAD's is.
static __attribute__((always_inline)) inline void ir_typed_copy_pointers(IrTypedSink* sink, uint32_t destination, uint32_t source, uint64_t bytes)
{
    uint64_t offset = 0;
    while (offset < bytes)
    {
        const uint64_t remaining = bytes - offset;
        const uint32_t piece = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
        const IrType type = piece == 8 ? IR_I64 : piece == 4 ? IR_I32 : piece == 2 ? IR_I16 : IR_I8;
        const uint32_t loaded = ir_typed_emit(sink, IR_LOAD, type, ir_typed_offset(sink, source, offset), BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        ir_typed_emit(sink, IR_STORE, IR_VOID, ir_typed_offset(sink, destination, offset), loaded, BUSTER_IR_NONE, 0);
        offset += piece;
    }
}

static __attribute__((always_inline)) inline void ir_typed_copy(IrTypedSink* sink, uint32_t destination_bits, uint32_t source_bits, uint64_t bytes)
{
    const uint32_t destination = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, destination_bits, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    const uint32_t source = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, source_bits, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    ir_typed_copy_pointers(sink, destination, source, bytes);
}

// Whether the function takes or returns an aggregate by value (an object parameter, a result in registers from its bytes
// or through a hidden pointer): its IR function then has its own ABI records.
static bool ir_typed_own_records(const Tier0Function* input)
{
    bool own = input->result_size != 0 || input->result.kind == BUSTER_ABI_RESULT_MEMORY;
    for (uint32_t p = 0; p < input->parameter_count; p += 1)
    {
        own |= tier0_is_object(input, p);
    }
    return own;
}

// Whether call `call` passes or returns an aggregate by value (Tier0Function.argument_sizes, call_result_objects): its
// IR call then has ABI records.
static bool ir_typed_aggregate_call(const Tier0Function* input, uint32_t call)
{
    bool aggregate = input->call_result_objects != 0 && input->call_result_objects[call] != 0;
    for (uint32_t a = 0; input->argument_sizes != 0 && a < input->call_argument_counts[call]; a += 1)
    {
        aggregate |= input->argument_sizes[input->call_first[call] + a] != 0;
    }
    return aggregate;
}

// Call `call` of the calls table: a direct one of its target's symbol, or with `callee` a pointer value (not
// BUSTER_IR_NONE) an indirect one through it. An aggregate argument (its address on the stack) is a ptr, converted in
// place in `arguments` (the stack's, which the call pops), and a call that passes or returns an aggregate has its ABI
// records: each argument's location and size, the call's totals and result, and its result object's slot address as the
// destination (c) of an aggregate result, its bytes the object's.
static __attribute__((always_inline)) inline uint32_t ir_typed_call(IrTypedSink* sink, const Tier0Function* input, uint32_t call, IrType result_type, uint32_t* arguments, uint32_t callee)
{
    const uint32_t descriptor = (uint32_t)sink->sizes.call_count;
    const uint32_t count = input->call_argument_counts[call];
    const uint32_t first = (uint32_t)sink->sizes.extra_count;
    const uint32_t type_first = (uint32_t)sink->sizes.call_type_count;
    const bool aggregate = ir_typed_aggregate_call(input, call);
    const uint32_t abi = aggregate ? (uint32_t)sink->sizes.abi_call_count : BUSTER_IR_NONE;
    const uint32_t passed = (uint32_t)sink->sizes.passed_count;
    const uint32_t object = input->call_result_objects != 0 ? input->call_result_objects[call] : 0;
    for (uint32_t a = 0; input->argument_sizes != 0 && a < count; a += 1)
    {
        if (input->argument_sizes[input->call_first[call] + a] != 0)
        {
            arguments[a] = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, arguments[a], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        }
    }
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
            .abi = aggregate ? abi + 1 : 0,
        };
        for (uint32_t a = 0; a < count; a += 1)
        {
            const uint32_t argument = input->call_first[call] + a;
            const uint32_t size = input->argument_sizes != 0 ? input->argument_sizes[argument] : 0;
            output->extra[first + a] = arguments[a];
            output->call_types[type_first + a] = size != 0 ? IR_PTR : ir_typed_types[input->argument_classes[argument]];
            if (aggregate)
            {
                output->passed[passed + a] = (IrPassed){
                    .location = input->argument_locations[argument],
                    .size = size,
                };
            }
        }
        if (aggregate)
        {
            output->abi_calls[abi] = (IrAbiCall){
                .call = input->calls[call],
                .passed = passed,
                .result_size = object != 0 ? input->local_sizes[object - 1] : 0,
            };
        }
    }
    sink->sizes.call_count += 1;
    sink->sizes.extra_count += count;
    sink->sizes.call_type_count += count;
    sink->sizes.abi_call_count += aggregate;
    sink->sizes.passed_count += aggregate ? count : 0;
    // The result object's slot address is its local's preheader instruction.
    return ir_typed_emit(sink, callee == BUSTER_IR_NONE ? IR_CALL : IR_CALL_INDIRECT, result_type, callee == BUSTER_IR_NONE ? input->call_targets[call] : callee, descriptor,
                         object != 0 ? object - 1 : BUSTER_IR_NONE, 0);
}

// A br_indirect's labels (#785), into extra: jump table t's for `owner` 1 + t, or with 0 those of no table; the count. A
// function without tables lists none, its br_indirect taking every label.
static __attribute__((always_inline)) inline uint32_t ir_typed_labels(IrTypedSink* sink, const Tier0Function* input, uint32_t owner)
{
    uint32_t count = 0;
    for (uint32_t k = 0; input->table_count != 0 && k < input->label_count; k += 1)
    {
        if (input->label_tables[k] == owner)
        {
            if (sink->output != 0)
            {
                sink->output->extra[sink->sizes.extra_count] = k;
            }
            sink->sizes.extra_count += 1;
            count += 1;
        }
    }
    return count;
}

// Expose the existing count/fill output contracts before the compiler makes inlining decisions. All construction
// logic stays in one C body, and both scalar/vector paths use these same two instantiations.
#define BUSTER_IR_TYPED_EMIT_NAME ir_typed_count_function
#define BUSTER_IR_TYPED_EMIT_COUNT 1
#include "ir_lower_emit.c"
#undef BUSTER_IR_TYPED_EMIT_COUNT
#undef BUSTER_IR_TYPED_EMIT_NAME

#define BUSTER_IR_TYPED_EMIT_NAME ir_typed_emit_function
#define BUSTER_IR_TYPED_EMIT_COUNT 0
#include "ir_lower_emit.c"
#undef BUSTER_IR_TYPED_EMIT_COUNT
#undef BUSTER_IR_TYPED_EMIT_NAME

// ---------------------------------------------------------------------------------------------------------------------
// Entry points

static IrCheck ir_lower_typed_target_path(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result, bool vector, uint64_t target_features)
{
    (void)vector;

    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrCheck check = {0};
    const bool supported_result = result_type == IR_VOID || result_type == IR_I8 || result_type == IR_I16 || result_type == IR_I32 || result_type == IR_I64 || result_type == IR_F32 || result_type == IR_F64 ||
                                  result_type == IR_F80;
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
             (input->call_count != 0 && (input->call_targets == 0 || input->call_first == 0 || input->call_argument_counts == 0 || input->call_variadic == 0 || input->calls == 0)) ||
             (input->argument_count != 0 && (input->argument_classes == 0 || input->argument_locations == 0)))
    {
        check.error = IR_ERROR_STRUCTURE;
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
    IrSimdTypedOperation* operations = 0;
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
        else if (kind == BUSTER_TYPED_CALL && input->call_targets[value] >= symbol_count)
        {
            check.error = IR_ERROR_OPERAND;
            check.instruction = i;
        }
        else if (kind == BUSTER_TYPED_FRAME_ADDRESS || kind == BUSTER_TYPED_TAIL_CALL)
        {
            // A frame's address (#148) and a tail call (#147's musttail), which this adapter doesn't lower and the plan
            // and the emitter would take for a binary operation: no function of the corpora has one (docs/ir.md).
            check.error = IR_ERROR_OPCODE;
            check.instruction = i;
        }
        else if (kind == BUSTER_TYPED_X86)
        {
            if (operations == 0)
            {
                operations = BUSTER_ARENA_PUSH_ARRAY(scratch, IrSimdTypedOperation, input->operation_count);
            }
            const IrSimdTypedStatus status = operations != 0 ? ir_simd_typed_row(input->operations + value, target_features, operations + value) : IR_SIMD_TYPED_INVALID;
            if (operations == 0 || status != IR_SIMD_TYPED_OK)
            {
                check.error = operations == 0 ? IR_ERROR_MEMORY : IR_ERROR_UNSUPPORTED;
                check.instruction = i;
                check.detail = status;
            }
        }
        else if (input->classes[i] == BUSTER_TYPED_CLASS_VECTOR)
        {
            const uint32_t bytes = ir_typed_vector_bytes(input, i);
            const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | (bytes < 64 ? BUSTER_X86_FEATURE(AVX512VL) : 0);
            // DUP, DISCARD and JUMP_VALUE have no own width; their source has already been admitted. All real
            // transports must be full 128/256/512-bit values. In particular, the 4/8-byte SSE carrier forms refuse.
            const bool inherited = kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_JUMP_VALUE;
            if (!inherited && bytes != 16 && bytes != 32 && bytes != 64)
            {
                check.error = IR_ERROR_UNSUPPORTED;
                check.detail = IR_SIMD_TYPED_UNSUPPORTED;
                check.instruction = i;
            }
            else if (!inherited && (target_features & required) != required)
            {
                check.error = IR_ERROR_UNSUPPORTED;
                check.detail = IR_SIMD_TYPED_TARGET;
                check.instruction = i;
            }
        }
        else if (input->classes[i] == BUSTER_TYPED_CLASS_MASK)
        {
            // A mask's (#179) m8 to m64, whose kmov, loads, stores and logic Tier 1 selects at its width: AVX-512F, and DQ
            // for a byte's, BW for a doubleword's and a quadword's. DUP, DISCARD and JUMP_VALUE keep their operand's.
            const uint32_t bytes = ir_typed_mask_bytes(input, i);
            const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | (bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
            if (bytes != 0 && (target_features & required) != required)
            {
                check.error = IR_ERROR_UNSUPPORTED;
                check.detail = IR_SIMD_TYPED_TARGET;
                check.instruction = i;
            }
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
        plan.operations = operations;
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
        ir_typed_classify(input, ops, types, BUSTER_VECTOR_PATH(vector));
        if (input->vectors)
        {
            // A vector's and a mask's (#179) IR types are their widths', which the class doesn't carry.
            for (uint32_t i = 0; i < input->count; i += 1)
            {
                if (input->classes[i] == BUSTER_TYPED_CLASS_VECTOR || input->classes[i] == BUSTER_TYPED_CLASS_MASK)
                {
                    const uint8_t class = ir_typed_stack_class(input, i);
                    types[i] = class >= BUSTER_TYPED_CLASS_LIMIT ? ir_typed_stack_type(class) : IR_VOID;
                }
            }
        }
        const IrSizes sizes = ir_typed_count_function(input, result_type, &plan, scratch, ops, types, stack, classes, 0);
        check.error = ir_create(output, &sizes, &candidate);
        if (check.error == IR_OK)
        {
            ir_typed_emit_function(input, result_type, &plan, scratch, ops, types, stack, classes, &candidate);
            candidate.symbol_count = symbol_count;
            check = BUSTER_VECTOR_PATH(vector) ? ir_verify(&candidate, scratch, 0) : ir_verify_scalar(&candidate, scratch, 0);
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

// Preserve scalar clients of the internal path as well as the public entry points.
static IrCheck ir_lower_typed_path(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result, bool vector)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, vector, 0);
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
static __attribute__((unused)) IrCheck ir_lower_typed_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result);

static __attribute__((unused)) IrCheck ir_lower_typed(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result)
{
#if !BUSTER_HOST_VECTOR
    return ir_lower_typed_scalar(input, result_type, symbol_count, output, scratch, result);
#else
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, true, 0);
#endif
}

static __attribute__((unused)) IrCheck ir_lower_typed_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, false, 0);
}

// Output features are explicit. The old entry points intentionally grant none: a vector producer must retain its
// selected target, not borrow the compiler host's AVX-512 capabilities. IR_SIMD_TYPED_* in detail distinguishes
// unsupported forms from missing target features on IR_ERROR_UNSUPPORTED.
static __attribute__((unused)) IrCheck ir_lower_typed_target(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, BUSTER_HOST_VECTOR != 0, target_features);
}

static __attribute__((unused)) IrCheck ir_lower_typed_target_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, false, target_features);
}
