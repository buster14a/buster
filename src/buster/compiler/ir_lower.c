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

// Whether a typed LOAD's, STORE's or SET's `value` is its class's (#1059): any for a class that reads it as bytes (a
// vector's or a mask's), and for an integer class 0 or an atomic access's marker (BUSTER_TYPED_ACCESS_ATOMIC) with an
// order its kind takes, as the IR's verifier does (ir_access_flags_valid): a load's relaxed, consume, acquire or seq_cst,
// a store's relaxed, release or seq_cst.
static bool ir_typed_access_valid(uint8_t kind, uint8_t class, uint64_t value)
{
    const bool integer = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
    const uint32_t orders = kind == BUSTER_TYPED_LOAD ? 0x27u : 0x29u;
    const uint64_t order = value & BUSTER_TYPED_FENCE_ORDER;
    // A segment override (#1447) goes to the access's flags (ir_typed_access); the rest is the class's.
    value &= ~BUSTER_TYPED_ACCESS_SEGMENT;
    return !integer || value == 0 || ((value & ~(uint64_t)(BUSTER_TYPED_ACCESS_ATOMIC | BUSTER_TYPED_FENCE_ORDER)) == 0 && (value & BUSTER_TYPED_ACCESS_ATOMIC) != 0 && (orders >> order & 1) != 0);
}

// Whether entry `i`'s segment override (#1447, BUSTER_TYPED_ACCESS_SEGMENT), if it has one, is one the IR carries
// (BUSTER_IR_SEGMENT): FS's, GS's or SS's, on a LOAD, STORE or SET of an integer, a float, a double or a vector (a 128-bit
// integer's halves, ir_wide.c, and a wide vector's pieces, ir_vector_split.c, keep their access's flags). An atomic
// read/modify/write's (ATOMIC_RMW, ATOMIC_CAS), whose IR flags hold its operation where a load's hold the segment, and an
// access of a long double or a mask, which Tier 1 moves through rows of their own (the x87 stack's, kmov's), are Tier 0's.
static bool ir_typed_segment_valid(const Tier0Function* input, uint32_t i)
{
    const uint8_t kind = input->kinds[i];
    const uint8_t class = input->classes[i];
    const uint32_t segment = (uint32_t)(input->values[i] >> BUSTER_TYPED_ACCESS_SEGMENT_SHIFT);
    const bool access = kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET;
    const bool atomic = kind == BUSTER_TYPED_ATOMIC_RMW || kind == BUSTER_TYPED_ATOMIC_CAS;
    const bool carried = class != BUSTER_TYPED_CLASS_F80 && class != BUSTER_TYPED_CLASS_MASK;
    return segment == 0 || (!atomic && (!access || carried));
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
    // Depth d's values of IR type t are in depth slot depth_slots[d * IR_TYPE_COUNT + t] (of depth_slot_count, after the
    // locals), each (depth, type) that some block's incoming shape has in a slot of its own, in that order, so that every
    // depth slot holds one type and promotes (#1189). One slot for all of a depth's values held several types in a
    // function whose `&&`, `||` and `?:` results differ in type at one depth (luaV_execute's), and stayed in memory: each
    // truth value a store, a load and the slot's address. Every block is entered with one shape (ir_typed_plan_arrive),
    // so the stores at a block's end and its successors' loads agree on each depth's type.
    uint32_t* depth_slots;
    uint32_t depth_slot_count;
    const IrSimdTypedOperation* operations;
    // The goto *s (the JUMP_INDIRECT entries no jump table has, #785), which all go through the function's one dispatch
    // block (the emitter's last); a jump table's goes to its table's labels from its own block.
    uint32_t indirect;
    // Whether a call argument is one the convention ignores (#141, ir_typed_ignored_arguments).
    bool ignored_arguments;
    // The vectors' widths a whole-vector aggregate parameter's entry copy may take (#1282, ir_typed_vector_whole): bit 0
    // 16 bytes, bit 1 32, bit 2 64; none in a function without vectors, or for a target without AVX-512.
    uint8_t vector_copies;
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
// must be lowered (or refused, as TAIL_CALL is, and the X86 rows past the SIMD subset) here before tier0_validate accepts
// it. A FRAME_ADDRESS is an IR_STACK_SAVE of BUSTER_IR_STACK_SAVE_FRAME (#1847). A FENCE (#1059) is an IR_FENCE of its value, which is the IR's operand as it is, and an
// ATOMIC_RMW an IR_ATOMIC_RMW of its operation and order, and an ATOMIC_CAS one of the compare-exchange.
_Static_assert(BUSTER_TYPED_KINDS == 52 && BUSTER_TYPED_FENCE == BUSTER_TYPED_X86 + 1 && BUSTER_TYPED_ATOMIC_RMW == BUSTER_TYPED_FENCE + 1 &&
                   BUSTER_TYPED_ATOMIC_CAS == BUSTER_TYPED_ATOMIC_RMW + 1 && BUSTER_TYPED_KINDS == BUSTER_TYPED_ATOMIC_CAS + 1,
               "ir_lower.c lowers or refuses every typed kind");
_Static_assert(BUSTER_TYPED_FENCE_ORDER == BUSTER_IR_FENCE_ORDER && BUSTER_TYPED_FENCE_SIGNAL == BUSTER_IR_FENCE_SIGNAL &&
               BUSTER_TYPED_FENCE_SEQ_CST == BUSTER_IR_FENCE_SEQ_CST, "a typed fence's value is its IR_FENCE's operand");

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
                case BUSTER_TYPED_FRAME_ADDRESS:
                    stack[depth] = class;
                    depth += 1;
                    break;
                case BUSTER_TYPED_DUP:
                    stack[depth] = stack[depth - 1];
                    depth += 1;
                    break;
                case BUSTER_TYPED_X86:
                    // A row of no result (#141, a machine row's, a masked store's) leaves nothing, as a void call.
                    depth -= input->operations[value].operand_count;
                    if (class != BUSTER_TYPED_CLASS_VOID)
                    {
                        stack[depth] = class;
                        depth += 1;
                    }
                    break;
                case BUSTER_TYPED_LOAD:
                    if (!ir_typed_access_valid(kind, class, value))
                    {
                        plan.error = "an atomic access's marker isn't an integer access's with an order its kind takes";
                        plan.error_code = IR_ERROR_METADATA;
                        ok = false;
                    }
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_NEG:
                case BUSTER_TYPED_NOT:
                case BUSTER_TYPED_CONVERT:
                case BUSTER_TYPED_ALLOCA:
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_STORE_LOCAL:
                case BUSTER_TYPED_STORE_GLOBAL:
                    // Consume and reproduce the value at the same depth.
                    break;
                case BUSTER_TYPED_ATOMIC_RMW:
                case BUSTER_TYPED_ATOMIC_CAS:
                    // An address and an operand, or an expected value and a desired one, and the old value in their place
                    // (#1059).
                    depth -= 1;
                    stack[depth - 1] = class;
                    break;
                case BUSTER_TYPED_FENCE:
                    // Neither takes nor leaves a value.
                    break;
                case BUSTER_TYPED_SET_LOCAL:
                case BUSTER_TYPED_SET_GLOBAL:
                case BUSTER_TYPED_DISCARD:
                case BUSTER_TYPED_STACK_RESTORE:
                    depth -= 1;
                    break;
                case BUSTER_TYPED_STORE:
                case BUSTER_TYPED_SET:
                    if (!ir_typed_access_valid(kind, class, value))
                    {
                        plan.error = "an atomic access's marker isn't an integer access's with an order its kind takes";
                        plan.error_code = IR_ERROR_METADATA;
                        ok = false;
                    }
                    depth -= kind == BUSTER_TYPED_STORE ? 1 : 2;
                    if (kind == BUSTER_TYPED_STORE)
                    {
                        stack[depth - 1] = class;
                    }
                    break;
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
// 1. Create one stack slot per source local, plus a slot of spill_bytes for each
//    (depth, IR type) that a block's incoming shape has (IrTypedPlan.depth_slots).
//    Source slots keep the typed local's actual size/alignment (an object's from
//    local_sizes and local_alignments, with type VOID); expression slots are
//    untyped temporaries, each of which one type's values reach, so that #134
//    promotes it. Mark all address-taken locals so #134 never promotes them.
// 2. In a preheader, materialize each slot address and each PARAM value, then
//    store parameters into their local slots, materialize the explicitly listed
//    direct table addresses (#1217), and branch to source block zero.
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

// A vector's type is its width's container: v4i32, v8i32 or v16i32, and for an SSE carrier of 4 or 8 bytes (#1280: the
// narrow vectors of shuffles and conversions, MMX's __m64) f32 or f64, its bits in the vector class's registers. No new
// type for them: the f32 and f64 values' loads, stores, constants, copies, phis and spills are bit moves already (movss
// and movsd, movaps, xorps, vmovd and vmovq), the IR's passes have no floating arithmetic to fold them by, and the
// lowering gives them no floating operation (only a row reads one), so their bits stay exact. A new type would have
// taken its own moves and spills, and a place in the type masks and the SIMD model's shapes. A row reads one
// zero-extended into its role, as Tier 0's (tier1_emit_cfg_row), whatever made it.
static IrType ir_typed_stack_type(uint8_t class)
{
    static const uint8_t vectors[17] = {
        [1] = IR_F32,
        [2] = IR_F64,
        [4] = IR_V4I32,
        [8] = IR_V8I32,
        [16] = IR_V16I32,
    };
    // A mask's (#179) m8 to m64 by its bytes, 1 to 8 (tier0_stack_mask's order).
    const IrType type = tier0_stack_is_mask(class)        ? (IrType)(IR_M8 + (class - BUSTER_TIER0_STACK_MASKS)) :
                        class >= BUSTER_TYPED_CLASS_LIMIT ? (IrType)vectors[class - BUSTER_TYPED_CLASS_LIMIT] :
                                                            (IrType)ir_typed_types[class];
    return type;
}

// Which classes are signed and which floating, as byte tables both classifications read: Tier 0's own predicates leave
// out the memory classes (I128 signed, F80 floating), which it compiles apart.
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

// A narrow integer class (a char's, a short's, a _Bool's), which a call boundary widens to 32 bits (#141): an argument is
// extended by its class, sign or zero, as clang's callers extend it, and a parameter arrives as an I32 that the callee
// truncates, relying on no caller's extension (ir_typed_boundary_class).
static bool ir_typed_narrow(uint8_t class)
{
    return class == BUSTER_TYPED_CLASS_I8 || class == BUSTER_TYPED_CLASS_U8 || class == BUSTER_TYPED_CLASS_I16 || class == BUSTER_TYPED_CLASS_U16;
}

// The class a value of `class` crosses a call boundary in: a narrow one's 32-bit class of its signedness, any other its own.
static uint8_t ir_typed_boundary_class(uint8_t class)
{
    return ir_typed_narrow(class) ? (ir_typed_signed(class) ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U32) : class;
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
    // The plan's ignored_arguments: whether a call's arguments' locations need reading (ir_typed_aggregate_call).
    bool ignored_arguments;
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

// An integer class's typed access's IR flags (#1059) onto its load or store `id`: an atomic access's marker as
// BUSTER_IR_ATOMIC and its order, and its natural alignment, which the checker's direct access has proven and the
// verifier requires of an atomic access. The flags column starts zeroed, which an ordinary access keeps.
// A segment override (#1447, BUSTER_TYPED_ACCESS_SEGMENT) of any class it admits (ir_typed_segment_class) goes to
// BUSTER_IR_SEGMENT, numbered as the typed stream numbers it.
static __attribute__((always_inline)) inline void ir_typed_access(IrTypedSink* sink, uint32_t id, uint8_t class, uint64_t value)
{
    const bool integer = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
    const uint8_t segment = (uint8_t)((value >> BUSTER_TYPED_ACCESS_SEGMENT_SHIFT) << BUSTER_IR_SEGMENT_SHIFT);
    if (sink->output != 0 && integer && (value & BUSTER_TYPED_ACCESS_ATOMIC) != 0)
    {
        sink->output->flags[id] = (uint8_t)(BUSTER_IR_ATOMIC | (value & BUSTER_TYPED_FENCE_ORDER) << BUSTER_IR_ORDER_SHIFT);
        sink->output->alignment_log2[id] = (uint8_t)__builtin_ctz(tier0_class_bytes(class));
    }
    if (sink->output != 0 && __builtin_expect(segment != 0, 0))
    {
        sink->output->flags[id] |= segment;
    }
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

// A vector operation's lanes (#141's element types) onto instruction `id`, its flags, which start zeroed (I32's).
static __attribute__((always_inline)) inline void ir_typed_lanes(IrTypedSink* sink, uint32_t id, IrSimdLane lane)
{
    if (sink->output != 0)
    {
        sink->output->flags[id] = (uint8_t)lane;
    }
}

// `unmasked`: a comparison into a mask alone (#1623, IrSimdTypedOperation.compare_mask) whose writemask the typed stream
// gives every lane of (ir_typed_unmasked), so that its mask is the vcmp's alone, with no AND.
static __attribute__((always_inline)) inline uint32_t ir_typed_vector_operation(IrTypedSink* sink, const IrSimdTypedOperation* operation, IrType type, const uint32_t* operands, bool unmasked)
{
    static const uint8_t ops[IR_SIMD_OPERATION_COUNT] = {
        [IR_SIMD_ADD] = IR_ADD,
        [IR_SIMD_SUB] = IR_SUB,
        [IR_SIMD_MUL] = IR_MUL,
        [IR_SIMD_AND] = IR_AND,
        [IR_SIMD_OR] = IR_OR,
        [IR_SIMD_XOR] = IR_XOR,
        [IR_SIMD_NOT] = IR_NOT,
        [IR_SIMD_SHL] = IR_SHL,
        [IR_SIMD_LSHR] = IR_LSHR,
        [IR_SIMD_ASHR] = IR_ASHR,
        [IR_SIMD_FADD] = IR_FADD,
        [IR_SIMD_FSUB] = IR_FSUB,
        [IR_SIMD_FMUL] = IR_FMUL,
        [IR_SIMD_FDIV] = IR_FDIV,
    };
    uint32_t value = BUSTER_IR_NONE;
    if (operation->compare_mask)
    {
        // The lanes' mask, `type` (the decoder made the row's result bytes the lanes' mask's), and the writemask's AND.
        const uint32_t mask = ir_typed_emit(sink, IR_VCMP, type, operands[0], operands[1], operation->predicate, 0);
        ir_typed_lanes(sink, mask, operation->lane);
        value = unmasked ? mask : ir_typed_emit(sink, IR_AND, type, mask, operands[2], BUSTER_IR_NONE, 0);
    }
    else if (operation->expand_mask)
    {
        // The comparison's mask, a bit per lane, and -1 or 0 in each lane by it, at the same lanes' width.
        const IrType mask_type = (IrType)ir_lane_mask(type, operation->lane);
        const uint32_t mask = ir_typed_emit(sink, IR_VCMP, mask_type, operands[0], operands[1], operation->predicate, 0);
        ir_typed_lanes(sink, mask, operation->lane);
        const uint64_t zero[8] = {0};
        const uint64_t ones[8] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
        const uint32_t active = ir_typed_vector_constant(sink, type, ones);
        const uint32_t inactive = ir_typed_vector_constant(sink, type, zero);
        value = ir_typed_emit(sink, IR_VSELECT, type, mask, active, inactive, 0);
        ir_typed_lanes(sink, value, operation->lane);
    }
    else if (operation->convert != 0)
    {
        // A conversion (#1840), its lanes and mode in its flags; a masked truncation's merged into its second operand under
        // its third where the typed stream's writemask hasn't every lane.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], BUSTER_IR_NONE, operation->convert - 1u, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
        if (operation->merge_mask && !unmasked)
        {
            value = ir_typed_emit(sink, IR_VSELECT, type, operands[2], value, operands[1], 0);
            ir_typed_lanes(sink, value, operation->lane);
        }
    }
    else if (operation->mask_expand)
    {
        // A mask to a vector (#1840): all ones in each lane whose bit is set, zeros elsewhere.
        const uint64_t zero[8] = {0};
        const uint64_t ones[8] = { UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
        const uint32_t active = ir_typed_vector_constant(sink, type, ones);
        const uint32_t inactive = ir_typed_vector_constant(sink, type, zero);
        value = ir_typed_emit(sink, IR_VSELECT, type, operands[0], active, inactive, 0);
        ir_typed_lanes(sink, value, operation->lane);
    }
    else if (operation->sign_mask)
    {
        // A vector's sign bits as a mask (#1840): its signed lanes less than zero, at the vector's width.
        const uint64_t zero[8] = {0};
        const IrType vector = (IrType)(IR_V4I32 + (operation->shape - IR_SIMD_V4I32));
        const uint32_t inactive = ir_typed_vector_constant(sink, vector, zero);
        value = ir_typed_emit(sink, IR_VCMP, type, operands[0], inactive, IR_LT, 0);
        ir_typed_lanes(sink, value, operation->lane);
    }
    else if (operation->lane_op)
    {
        // A lane operation (#1840): its lanes and operation in its flags.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], BUSTER_IR_INTRINSIC_LANE_OP, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->shift)
    {
        // A shift by a scalar count (#1840), its lanes and shift in its flags.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], BUSTER_IR_INTRINSIC_SHIFT, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->aes)
    {
        // An AES round (#1840) of the state by the round key, its round in its flags.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], BUSTER_IR_INTRINSIC_AES, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->fround)
    {
        // A floating operation at its own rounding (#1840): its operation, lanes and rounding in its flags.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], BUSTER_IR_INTRINSIC_FROUND, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->lane_move != 0)
    {
        // One lane of a vector or its lanes' sign bits (#1840), its flags the lane: an extraction's I32 or I64 narrowed to
        // the row's integer, and an insertion's integer widened to its I32 first.
        const uint32_t kind = operation->lane_move - 1u;
        const uint32_t size = operation->immediate & 3;
        const IrType scalar = size == 3 ? IR_I64 : IR_I32;
        if (kind == BUSTER_IR_INTRINSIC_LANE_INSERT)
        {
            const IrType given = (IrType)ir_typed_types[operation->operand_classes[1]];
            const uint32_t inserted = given == scalar ? operands[1] : ir_typed_emit(sink, IR_ZEXT, scalar, operands[1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], inserted, kind, 0);
            ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
        }
        else
        {
            const IrType produced = kind == BUSTER_IR_INTRINSIC_MOVEMASK ? IR_I32 : scalar;
            value = ir_typed_emit(sink, IR_INTRINSIC, produced, operands[0], BUSTER_IR_NONE, kind, 0);
            ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
            if (type != produced)
            {
                value = ir_typed_emit(sink, IR_TRUNC, type, value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
        }
    }
    else if (operation->masked_memory != 0)
    {
        // A masked load or store (#1840) of the vector at the address, the first operand, under the third, the mask: a
        // load's passthrough the second operand, a store's vector the second. Under the typed stream's mask of every lane
        // it is the plain load or store of the whole vector, which reads or writes every lane anyway.
        const bool store = operation->masked_memory == 2;
        const uint32_t address = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, operands[0], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        const IrOp op = unmasked ? (store ? IR_STORE : IR_LOAD) : store ? IR_MASKED_STORE : IR_MASKED_LOAD;
        const uint32_t b = store ? operands[1] : unmasked ? BUSTER_IR_NONE : operands[2];
        const uint32_t c = unmasked ? BUSTER_IR_NONE : store ? operands[2] : operands[1];
        value = ir_typed_emit(sink, op, type, address, b, c, operation->immediate);
        if (!unmasked)
        {
            ir_typed_lanes(sink, value, operation->lane);
        }
    }
    else if (operation->compress != 0)
    {
        // A compress or an expand (#1840) of the first operand under the third, the mask, merged into the second, the
        // third operand in a slot of the extra array; its lanes' bytes' log2 in its flags.
        const uint32_t slot = (uint32_t)sink->sizes.extra_count;
        if (sink->output != 0)
        {
            sink->output->extra[slot] = operands[1];
        }
        sink->sizes.extra_count += 1;
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[2], ir_intrinsic_ternary_c(operation->compress - 1u, slot), 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->ternlog)
    {
        // A bitwise function of three vectors (#1840): its truth table in its flags, the third operand in a slot of the
        // extra array; merged into its first operand under its fourth, the writemask, at its lanes, where the typed
        // stream's writemask hasn't every lane.
        const uint32_t slot = (uint32_t)sink->sizes.extra_count;
        if (sink->output != 0)
        {
            sink->output->extra[slot] = operands[2];
        }
        sink->sizes.extra_count += 1;
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_TERNLOG, slot), 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
        if (operation->merge_mask && !unmasked)
        {
            const uint64_t zero[8] = {0};
            const uint32_t inactive = operation->ternlog_zero ? ir_typed_vector_constant(sink, type, zero) : operands[0];
            value = ir_typed_emit(sink, IR_VSELECT, type, operands[3], value, inactive, 0);
            ir_typed_lanes(sink, value, operation->lane);
        }
    }
    else if (operation->permute2)
    {
        // A permute of two tables (#1840): the first table, the indices and the second table, the third operand in a
        // slot of the extra array that its `c` names; its lanes' bytes' log2 in its flags. A table narrower than the
        // result is the IR's widen of it first (ir_simd_typed_permute2).
        const uint32_t indices = operands[operation->permute2_indices];
        uint32_t tables[2] = { operands[0], operands[operation->permute2_table] };
        const uint32_t places[2] = { 0, operation->permute2_table };
        for (uint32_t t = 0; t < 2; t += 1)
        {
            if (operation->operand_bytes[places[t]] < ir_type_bits[type] / 8)
            {
                tables[t] = ir_typed_emit(sink, IR_INTRINSIC, type, tables[t], BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_WIDEN, 0);
            }
        }
        const uint32_t first = tables[0];
        const uint32_t table = tables[1];
        const uint32_t slot = (uint32_t)sink->sizes.extra_count;
        if (sink->output != 0)
        {
            sink->output->extra[slot] = table;
        }
        sink->sizes.extra_count += 1;
        value = ir_typed_emit(sink, IR_INTRINSIC, type, first, indices, ir_intrinsic_ternary_c(BUSTER_IR_INTRINSIC_PERMUTE2, slot), 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->permute != 0)
    {
        // A permute (#1840), its immediate in its flags.
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], operands[1], operation->permute - 1u, 0);
        ir_typed_lanes(sink, value, (IrSimdLane)operation->immediate);
    }
    else if (operation->width_change != 0)
    {
        // A width change (#1840), its part in its flags; a masked extract's merged into its second operand under its third
        // where the typed stream's writemask hasn't every lane.
        const uint32_t kind = operation->width_change - 1u;
        value = ir_typed_emit(sink, IR_INTRINSIC, type, operands[0], kind == BUSTER_IR_INTRINSIC_INSERT ? operands[1] : BUSTER_IR_NONE, kind, 0);
        // An intrinsic's flags are its immediate, here the part, not lanes: the lanes' setter writes the flags column.
        ir_typed_lanes(sink, value, (IrSimdLane)operation->part);
        if (operation->merge_mask && !unmasked)
        {
            value = ir_typed_emit(sink, IR_VSELECT, type, operands[2], value, operands[1], 0);
            ir_typed_lanes(sink, value, operation->lane);
        }
    }
    else if (operation->operation == IR_SIMD_SELECT)
    {
        // A masked move's select (#1840): the second operand's lanes where the mask's bits are set, the third's elsewhere.
        value = ir_typed_emit(sink, IR_VSELECT, type, operands[0], operands[1], operands[2], 0);
        ir_typed_lanes(sink, value, operation->lane);
    }
    else if (operation->zero)
    {
        // An undefined vector's row (#1840): the zero vector it gives.
        const uint64_t zero[8] = {0};
        value = ir_typed_vector_constant(sink, type, zero);
    }
    else
    {
        value = ir_typed_emit(sink, (IrOp)ops[operation->operation], type, operands[0], operation->operand_count == 2 ? operands[1] : BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        ir_typed_lanes(sink, value, operation->lane);
    }
    return value;
}

// Whether entry `i`, a comparison into a mask alone (#1623), or a masked extract, truncation, load or store (#1840), has a
// writemask of every lane: a constant whose bits for the lanes are all set, converted to the mask, just before it, as
// the comparison intrinsics' rows without a mask operand have it (_mm512_cmpeq_epi8_mask's -1). The typed stream decides
// it, so both lowering passes agree.
static bool ir_typed_unmasked(const Tier0Function* input, const IrSimdTypedOperation* operation, uint32_t i)
{
    // A comparison's lanes, or a masked extract's or truncation's (#1840): its result's, the merged operand's.
    const uint32_t lanes = operation->compare_mask                                    ? 8u * operation->operand_bytes[0] / ir_simd_lane_bits(operation->lane) :
                           operation->merge_mask || operation->masked_memory != 0 ? 8u * operation->operand_bytes[1] / ir_simd_lane_bits(operation->lane) :
                                                                                     0;
    const uint64_t every = lanes >= 64 ? UINT64_MAX : (UINT64_C(1) << lanes) - 1;
    return lanes != 0 && i >= 2 && input->kinds[i - 1] == BUSTER_TYPED_CONVERT && input->classes[i - 1] == BUSTER_TYPED_CLASS_MASK && input->kinds[i - 2] == BUSTER_TYPED_CONST &&
           input->classes[i - 2] >= BUSTER_TYPED_CLASS_I8 && input->classes[i - 2] <= BUSTER_TYPED_CLASS_U64 && (input->values[i - 2] & every) == every;
}

// A scalar BMI instruction's row (#141, ir_simd_typed_intrinsic) as its IR intrinsic of the operands its record places;
// a bit builtin's (#1374, ir_simd_typed_bits) as its intrinsic of its operand's type, a count of 64 bits narrowed to its
// result's type; and a signed overflow builtin's flag (#1385, ir_simd_typed_overflow) as its intrinsic of its operands'
// type, a long's 0 or 1 narrowed to its int.
static __attribute__((always_inline)) inline uint32_t ir_typed_intrinsic(IrTypedSink* sink, const IrSimdTypedOperation* operation, IrType type, const uint32_t* operands)
{
    const IrType source = (IrType)ir_typed_types[operation->operand_classes[0]];
    const uint32_t b = operation->operand_count == 1 ? BUSTER_IR_NONE : operands[operation->intrinsic_operands[1]];
    uint32_t value = ir_typed_emit(sink, IR_INTRINSIC, source, operands[operation->intrinsic_operands[0]], b, operation->intrinsic - 1u, 0);
    if (source != type)
    {
        value = ir_typed_emit(sink, IR_TRUNC, type, value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return value;
}

static __attribute__((always_inline)) inline uint32_t ir_typed_splat(IrTypedSink* sink, const IrSimdTypedOperation* operation, IrType type, uint32_t scalar)
{
    const IrType source = (IrType)ir_typed_types[operation->operand_classes[0]];
    const uint32_t bits = 8u * operation->splat_bytes;
    if (ir_type_bits[source] != bits)
    {
        const IrType narrowed = bits == 8 ? IR_I8 : bits == 16 ? IR_I16 : IR_I32;
        scalar = ir_typed_emit(sink, IR_TRUNC, narrowed, scalar, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
    }
    return ir_typed_emit(sink, IR_INTRINSIC, type, scalar, BUSTER_IR_NONE, BUSTER_IR_INTRINSIC_SPLAT, 0);
}

// Any other X86 row (#141, ir_simd_typed_machine_row) as a row call of the operands on the stack, in their order: the row
// itself, the IR's machine row (IrFunction.rows), whose call's types are its classes' (ir_row_type).
static __attribute__((always_inline)) inline uint32_t ir_typed_machine_row(IrTypedSink* sink, const Tier0Operation* row, IrType type, const uint32_t* operands)
{
    const uint32_t descriptor = (uint32_t)sink->sizes.call_count;
    const uint32_t first = (uint32_t)sink->sizes.extra_count;
    const uint32_t type_first = (uint32_t)sink->sizes.call_type_count;
    const uint32_t index = (uint32_t)sink->sizes.row_count;
    const uint32_t count = row->operand_count;
    if (sink->output != 0)
    {
        // The row's used fields alone, its unused records and operands zero: equal rows are equal bytes.
        IrFunction* output = sink->output;
        Tier0Operation* copy = &output->rows[index];
        memset(copy, 0, sizeof(*copy));
        memcpy(copy->records, row->records, (uint64_t)row->record_count * sizeof(X86Instruction));
        memcpy(copy->operand_classes, row->operand_classes, count);
        memcpy(copy->operand_bytes, row->operand_bytes, count);
        copy->record_count = row->record_count;
        copy->operand_count = row->operand_count;
        copy->result_class = row->result_class;
        copy->result_bytes = row->result_bytes;
        output->calls[descriptor] = (IrCall){
            .argument_first = first,
            .argument_count = count,
            .type_first = type_first,
            .parameter_count = count,
            .result_type = (uint8_t)type,
            .row = index + 1,
        };
        for (uint32_t j = 0; j < count; j += 1)
        {
            output->extra[first + j] = operands[j];
            output->call_types[type_first + j] = (uint8_t)ir_row_type(row->operand_classes[j], row->operand_bytes[j]);
        }
    }
    sink->sizes.call_count += 1;
    sink->sizes.extra_count += count;
    sink->sizes.call_type_count += count;
    sink->sizes.row_count += 1;
    return ir_typed_emit(sink, IR_CALL, type, 0, descriptor, BUSTER_IR_NONE, 0);
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
    return mask ? (uint8_t)(class - BUSTER_TIER0_STACK_MASKS) : vector ? (bytes == 64 ? 6 : bytes == 32 ? 5 : bytes == 8 ? 3 : bytes == 4 ? 2 : 4) : bytes == 8 ? 3 : bytes == 4 ? 2 : bytes == 2 ? 1 : 0;
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

// Depth d's slot for a value of stack class `class`: the plan's (depth, type) slot after the locals.
static uint32_t ir_typed_depth_slot(const IrTypedPlan* plan, uint32_t local_count, uint32_t d, uint8_t class)
{
    return local_count + plan->depth_slots[d * IR_TYPE_COUNT + ir_typed_stack_type(class)];
}

static __attribute__((always_inline)) inline void ir_typed_spill(IrTypedSink* sink, const IrTypedPlan* plan, const uint32_t* stack, const uint8_t* classes, uint32_t depth, uint32_t local_count)
{
    for (uint32_t d = 0; d < depth; d += 1)
    {
        // All slot addresses are emitted first in the preheader, so an address
        // value's instruction ID is exactly its slot ID.
        ir_typed_emit(sink, IR_STORE, IR_VOID, ir_typed_depth_slot(plan, local_count, d, classes[d]), stack[d], BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
    }
}

// The plan's depth slots (IrTypedPlan.depth_slots): each (depth, type) a block's incoming shape has, depth by depth and
// each depth's types in IR type order. Returns false if scratch has no room for the map.
static bool ir_typed_plan_depth_slots(IrTypedPlan* plan, Arena* scratch)
{
    const uint64_t cells = (uint64_t)plan->maximum_spill_depth * IR_TYPE_COUNT;
    plan->depth_slots = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, cells + 1);
    plan->depth_slot_count = 0;
    const bool ok = plan->depth_slots != 0;
    if (ok)
    {
        memset(plan->depth_slots, 0, (cells + 1) * sizeof(uint32_t));
        for (uint32_t b = 0; b < plan->block_count; b += 1)
        {
            const uint32_t depth = plan->stack_depths[b];
            const uint8_t* shape = scratch->base + plan->stack_offsets[b];
            for (uint32_t d = 0; depth != UINT32_MAX && d < depth; d += 1)
            {
                plan->depth_slots[d * IR_TYPE_COUNT + ir_typed_stack_type(shape[d])] = 1;
            }
        }
        for (uint64_t cell = 0; cell < cells; cell += 1)
        {
            const uint32_t used = plan->depth_slots[cell];
            plan->depth_slots[cell] = used != 0 ? plan->depth_slot_count : UINT32_MAX;
            plan->depth_slot_count += used;
        }
    }
    return ok;
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

// The vector type that copies an aggregate of `size` bytes at `location` whole (#1282): one that a vector register holds
// whole (REGISTERS, a vector register alone, neither eightbyte alone: tier1_select_parts_of's "whole"), of 16, 32 or 64
// bytes, a width `allowed` (IrTypedPlan.vector_copies) has; else IR_VOID, for a copy by integers. Its bytes are a
// vector's, or a struct's whose high eightbyte is padding, which the copy keeps as padding: either way, one load and one
// store of a vector move them, where eight of i64 each kept every aggregate parameter of a vector in memory.
static IrType ir_typed_vector_whole(AbiLocation location, uint64_t size, uint8_t allowed)
{
    const bool whole = location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.registers[0] != BUSTER_ABI_REGISTER_NONE &&
                       (location.registers[0] & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && location.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                       (location.flags & (BUSTER_ABI_FLAG_HIGH | BUSTER_ABI_FLAG_LOW)) == 0;
    const uint32_t width = size == 16 ? 0 : size == 32 ? 1 : size == 64 ? 2 : 3;
    const IrType types[4] = { IR_V4I32, IR_V8I32, IR_V16I32, IR_VOID };
    return whole && width < 3 && ((allowed >> width) & 1) != 0 ? types[width] : IR_VOID;
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

// Whether any of the function's call arguments is one the convention ignores (#141: an empty struct, of no bytes) or a
// static chain in r10 (#148, __builtin_call_with_static_chain's), whose call then has ABI records (ir_typed_aggregate_call):
// once a function, so that no call reads its arguments' locations where none is. The vector path tests the kinds of 8
// locations a load (AbiLocation's byte 6, IGNORE 0), and their kinds with their first registers (bytes 6 and 4) for
// REGISTERS in r10; its scalar twin one at a time, which must agree.
static bool ir_typed_ignored_arguments(const Tier0Function* input, bool vector)
{
    (void)vector;

    const uint32_t count = input->argument_locations != 0 ? input->argument_count : 0;
    bool ignored = false;
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; !ignored && first < count; first += 8)
        {
            const uint32_t lanes = count - first < 8 ? count - first : 8;
            const __mmask8 active = (__mmask8)((1u << lanes) - 1);
            const __m512i locations = _mm512_maskz_loadu_epi64(active, input->argument_locations + first);
            const __m512i chain = _mm512_set1_epi64((int64_t)((uint64_t)BUSTER_ABI_LOCATION_REGISTERS << 48 | (uint64_t)BUSTER_ABI_R10 << 32));
            const __m512i kind_register = _mm512_and_si512(locations, _mm512_set1_epi64((int64_t)UINT64_C(0x00ff00ff00000000)));
            ignored = (_mm512_mask_testn_epi64_mask(active, locations, _mm512_set1_epi64((int64_t)UINT64_C(0x00ff000000000000))) != 0) ||
                      (_mm512_mask_cmpeq_epi64_mask(active, kind_register, chain) != 0);
        }
    }
    else
    {
        for (uint32_t a = 0; !ignored && a < count; a += 1)
        {
            const AbiLocation location = input->argument_locations[a];
            ignored = location.kind == BUSTER_ABI_LOCATION_IGNORE || (location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.registers[0] == BUSTER_ABI_R10);
        }
    }
    return ignored;
}

// Whether call `call` passes or returns an aggregate by value (Tier0Function.argument_sizes, call_result_objects): its
// IR call then has ABI records. So does one that passes an aggregate the convention ignores (#141: an empty struct, of
// no bytes), which a call without records would place by its type, in a register of its own, and one that passes a
// static chain in r10 (#148), which a call without records would place in the next argument register; only a function
// with one (`ignored`, ir_typed_ignored_arguments) reads its calls' locations. And so does one that passes an aggregate
// of no size on the stack (#1879: a record of a flexible array member alone, MEMORY, which clang passes byval in an
// 8-byte slot), whose value, its address, a call without records would place in the next general register: in a
// function with aggregates (argument_sizes), an argument of a general class on the stack while the call's locations
// before it leave a general register free. And so does one whose value is on the stack past the next 8-byte slot (#1844:
// a transparent union's member, which takes the union's own slot, aligned to 16, where clang has no register for it).
static bool ir_typed_aggregate_call(const Tier0Function* input, uint32_t call, bool ignored)
{
    bool aggregate = input->call_result_objects != 0 && input->call_result_objects[call] != 0;
    const bool located = ignored || (input->argument_sizes != 0 && input->argument_locations != 0);
    uint32_t general = 0;
    uint32_t slots = 0;
    for (uint32_t a = 0; (input->argument_sizes != 0 || ignored) && a < input->call_argument_counts[call]; a += 1)
    {
        const uint32_t argument = input->call_first[call] + a;
        // Without locations, a stand-in in registers, which counts as two general ones (registers {0, 0}, rax) and is never
        // on the stack, so it never asks for records.
        const AbiLocation location = located ? input->argument_locations[argument] : (AbiLocation){ .kind = BUSTER_ABI_LOCATION_REGISTERS };
        const uint8_t class = input->argument_classes[argument];
        const bool general_class = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
        aggregate |= (input->argument_sizes != 0 && input->argument_sizes[argument] != 0) || location.kind == BUSTER_ABI_LOCATION_IGNORE ||
                     (location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.registers[0] == BUSTER_ABI_R10) ||
                     (location.kind == BUSTER_ABI_LOCATION_STACK && general_class && general < BUSTER_ABI_INTEGER_ARGUMENTS) ||
                     (located && location.kind == BUSTER_ABI_LOCATION_STACK && location.stack_offset != slots * BUSTER_ABI_SLOT_BYTES);
        slots += location.kind == BUSTER_ABI_LOCATION_STACK ? 1 : 0;
        general += location.kind != BUSTER_ABI_LOCATION_STACK && location.registers[0] < 16 ? 1 : 0;
        general += location.kind != BUSTER_ABI_LOCATION_STACK && location.registers[1] < 16 ? 1 : 0;
    }
    return aggregate;
}

// An asm body's call (#1839, Tier0Function.call_asm): the body into IrFunction.asm_bodies, and a body call (IrCall.body)
// whose first argument is its operand block's address, the block local's preheader IR_STACK_ADDR (none for a body that
// reads no block), and whose others are its call row's, its register operands' values (#2144), each in its register
// (IrAsmBody.locations), a narrow integer extended as a call's argument is. Every pass takes the call as an unknown
// callee's of that pointer: the block escapes into it, its stores stay before it, and no access moves across it.
static __attribute__((always_inline)) inline uint32_t ir_typed_asm_body(IrTypedSink* sink, const Tier0Function* input, uint32_t call, uint32_t* arguments)
{
    const Tier0AsmBody* body = &input->asm_bodies[input->call_asm[call] - 1];
    const uint32_t descriptor = (uint32_t)sink->sizes.call_count;
    const uint32_t first = (uint32_t)sink->sizes.extra_count;
    const uint32_t type_first = (uint32_t)sink->sizes.call_type_count;
    const uint32_t index = (uint32_t)sink->sizes.asm_body_count;
    const uint32_t block = body->block != BUSTER_TIER0_ASM_NO_BLOCK ? 1 : 0;
    const uint32_t passed = input->call_argument_counts[call];
    const uint32_t count = block + passed;
    for (uint32_t a = 0; a < passed; a += 1)
    {
        const uint8_t class = input->argument_classes[input->call_first[call] + a];
        arguments[a] = ir_typed_convert(sink, arguments[a], class, ir_typed_boundary_class(class));
    }
    if (sink->output != 0)
    {
        IrFunction* output = sink->output;
        output->asm_bodies[index] = (IrAsmBody){
            .records = body->records,
            .branches = body->branches,
            .targets = body->targets,
            .count = body->count,
            .branch_count = body->branch_count,
            .touched = body->touched,
            .masks = body->masks,
            .stack = body->stack,
            .locations = passed != 0 ? input->argument_locations + input->call_first[call] : 0,
            .passed = passed,
        };
        output->calls[descriptor] = (IrCall){
            .argument_first = first,
            .argument_count = count,
            .type_first = type_first,
            .parameter_count = count,
            .result_type = IR_VOID,
            .body = index + 1,
        };
        if (block != 0)
        {
            output->extra[first] = body->block;
            output->call_types[type_first] = IR_PTR;
        }
        for (uint32_t a = 0; a < passed; a += 1)
        {
            output->extra[first + block + a] = arguments[a];
            output->call_types[type_first + block + a] = ir_typed_types[ir_typed_boundary_class(input->argument_classes[input->call_first[call] + a])];
        }
    }
    sink->sizes.call_count += 1;
    sink->sizes.extra_count += count;
    sink->sizes.call_type_count += count;
    sink->sizes.asm_body_count += 1;
    return ir_typed_emit(sink, IR_CALL, IR_VOID, 0, descriptor, BUSTER_IR_NONE, 0);
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
    const bool aggregate = ir_typed_aggregate_call(input, call, sink->ignored_arguments);
    const uint32_t abi = aggregate ? (uint32_t)sink->sizes.abi_call_count : BUSTER_IR_NONE;
    const uint32_t passed = (uint32_t)sink->sizes.passed_count;
    const uint32_t object = input->call_result_objects != 0 ? input->call_result_objects[call] : 0;
    // An aggregate's argument is its address; a narrow one is extended to 32 bits by its class (ir_typed_narrow).
    for (uint32_t a = 0; a < count; a += 1)
    {
        const uint32_t argument = input->call_first[call] + a;
        const uint8_t class = input->argument_classes[argument];
        if (input->argument_sizes != 0 && input->argument_sizes[argument] != 0)
        {
            arguments[a] = ir_typed_emit(sink, IR_INTTOPTR, IR_PTR, arguments[a], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        }
        else
        {
            arguments[a] = ir_typed_convert(sink, arguments[a], class, ir_typed_boundary_class(class));
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
            output->call_types[type_first + a] = size != 0 ? IR_PTR : ir_typed_types[ir_typed_boundary_class(input->argument_classes[argument])];
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
#define BUSTER_IR_TYPED_EMIT_SOURCES 0
#include "ir_lower_emit.c"
#undef BUSTER_IR_TYPED_EMIT_SOURCES
#undef BUSTER_IR_TYPED_EMIT_COUNT
#undef BUSTER_IR_TYPED_EMIT_NAME

#define BUSTER_IR_TYPED_EMIT_NAME ir_typed_emit_function
#define BUSTER_IR_TYPED_EMIT_COUNT 0
#define BUSTER_IR_TYPED_EMIT_SOURCES 0
#include "ir_lower_emit.c"
#undef BUSTER_IR_TYPED_EMIT_SOURCES
#undef BUSTER_IR_TYPED_EMIT_COUNT
#undef BUSTER_IR_TYPED_EMIT_NAME

#define BUSTER_IR_TYPED_EMIT_NAME ir_typed_emit_sources_function
#define BUSTER_IR_TYPED_EMIT_COUNT 0
#define BUSTER_IR_TYPED_EMIT_SOURCES 1
#include "ir_lower_emit.c"
#undef BUSTER_IR_TYPED_EMIT_SOURCES
#undef BUSTER_IR_TYPED_EMIT_COUNT
#undef BUSTER_IR_TYPED_EMIT_NAME

// ---------------------------------------------------------------------------------------------------------------------
// Entry points

// Keep every direct source assignment, even when its value is unavailable. Entry-block admission is a property of
// the whole original local: a write in a branch/loop, an address escape or a volatile declaration disables it. The
// events themselves stay present so an unknown later write can never silently extend an earlier constant binding.
static bool ir_typed_debug_bindings(const Tier0Function* input, const IrTypedPlan* plan, IrFunction* function, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    uint64_t count = input->parameter_count;
    bool entry_once = true;
    bool indirect = false;
    for (uint32_t entry = 0; entry < input->count; entry += 1)
    {
        const uint8_t kind = input->kinds[entry];
        count += kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
        entry_once &= !(kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE && input->values[entry] == 0);
        indirect |= kind == BUSTER_TYPED_JUMP_INDIRECT;
    }
    for (uint32_t label = 0; indirect && label < input->label_count; label += 1)
    {
        entry_once &= input->label_entries[label] != 0;
    }
    bool ok = count <= UINT32_MAX;
    IrDebugBinding* bindings = ir_allocate(output, count, sizeof(IrDebugBinding), &ok);
    uint8_t* flags = ir_allocate(output, input->local_count, 1, &ok);
    uint32_t* entries = ir_allocate(scratch, input->count, 4, &ok);
    const uint32_t first_end = plan->block_count != 0 ? plan->block_firsts[1] : 0;
    if (ok)
    {
        for (uint32_t local = 0; local < input->local_count; local += 1)
        {
            const uint8_t class = input->local_classes[local];
            flags[local] = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 && input->local_flags[local] == 0 ? BUSTER_IR_DEBUG_BIND_ENTRY : 0;
        }
        for (uint32_t local = 0; local < input->parameter_count; local += 1)
        {
            const uint8_t class = input->local_classes[local];
            const bool scalar = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
            bindings[local] = (IrDebugBinding)
            {
                .local = local,
                .entry = BUSTER_IR_NONE,
                .value = BUSTER_IR_NONE,
                .anchor = BUSTER_IR_NONE,
                .kind = IR_DEBUG_BIND_UNAVAILABLE,
                .bytes = scalar ? (uint8_t)tier0_class_bytes(class) : 0,
                .type = scalar ? ir_typed_types[class] : IR_VOID,
            };
        }
        uint32_t at = input->parameter_count;
        for (uint32_t entry = 0; entry < input->count; entry += 1)
        {
            const uint8_t kind = input->kinds[entry];
            entries[entry] = BUSTER_IR_NONE;
            if (kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_ATOMIC_CAS)
            {
                const uint32_t local = (uint32_t)(input->values[entry] >> (kind == BUSTER_TYPED_ATOMIC_CAS ? BUSTER_TYPED_CAS_LOCAL_SHIFT : 0));
                flags[local] = 0;
            }
            if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
            {
                const uint32_t local = (uint32_t)input->values[entry];
                const uint8_t class = input->local_classes[local];
                const bool scalar = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
                flags[local] = entry < first_end && entry_once ? flags[local] : flags[local] != 0 ? BUSTER_IR_DEBUG_BIND_REGIONS : 0;
                entries[entry] = at;
                bindings[at++] = (IrDebugBinding)
                {
                    .local = local,
                    .entry = entry,
                    .value = BUSTER_IR_NONE,
                    .anchor = BUSTER_IR_NONE,
                    .kind = IR_DEBUG_BIND_UNAVAILABLE,
                    .bytes = scalar ? (uint8_t)tier0_class_bytes(class) : 0,
                    .type = scalar ? ir_typed_types[class] : IR_VOID,
                };
            }
        }
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            if (function->op[node] == IR_STORE && function->op[function->a[node]] == IR_STACK_ADDR)
            {
                const uint32_t local = function->a[function->a[node]];
                const uint32_t entry = function->debug->origins[node];
                const uint32_t event = entry < input->count ? entries[entry] : local < input->parameter_count ? local : BUSTER_IR_NONE;
                const uint32_t value = function->b[node];
                if (event != BUSTER_IR_NONE && bindings[event].local == local && bindings[event].bytes != 0 && function->type[value] == bindings[event].type)
                {
                    IrDebugBinding* binding = bindings + event;
                    binding->anchor = value;
                    binding->value = value;
                    binding->kind = IR_DEBUG_BIND_VALUE;
                    if (function->op[value] == IR_CONST)
                    {
                        const uint8_t bytes = binding->bytes;
                        const uint64_t literal = function->constants[function->a[value]].low;
                        binding->literal = bytes == 8 ? literal : literal & ((UINT64_C(1) << (bytes * 8)) - 1);
                        binding->value = BUSTER_IR_NONE;
                        binding->kind = IR_DEBUG_BIND_CONSTANT;
                    }
                }
            }
        }
        function->debug->bindings = bindings;
        function->debug->binding_count = (uint32_t)count;
        function->debug->binding_flags = flags;
        function->debug->first_block_end = first_end;
        function->arena_bytes += output->length - mark;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return ok;
}

typedef enum IrTypedDebugEventKind
{
    IR_TYPED_DEBUG_READ,
    IR_TYPED_DEBUG_WRITE,
    IR_TYPED_DEBUG_START,
    IR_TYPED_DEBUG_END,
} IrTypedDebugEventKind;

typedef struct IrTypedDebugEvent IrTypedDebugEvent;
struct IrTypedDebugEvent
{
    uint32_t entry;
    uint32_t local;
    uint32_t payload;
    uint8_t kind;
};

static void ir_typed_debug_region_end(IrDebugRegion* regions, uint32_t capacity, uint32_t* count, IrDebugRegion region,
                                      uint32_t end, uint32_t next, bool witnessed, bool* ok)
{
    if (*ok && witnessed && region.begin < end)
    {
        *ok = *count < capacity;
        if (*ok)
        {
            region.end = end;
            region.next = next;
            regions[(*count)++] = region;
        }
    }
}

// Real LOCAL reads and direct assignments witness a source value only within their original typed block and
// between writes/lifetime boundaries. A stable local counting pass groups these sparse events without a local by
// block matrix. Repeated reads in one unchanged window choose one witness; no debug read is inserted into the IR.
static bool ir_typed_debug_regions(const Tier0Function* input, const IrTypedPlan* plan, IrFunction* function, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrDebugInfo* debug = function->debug;
    uint64_t event_count = 0;
    uint32_t capacity = 0;
    for (uint32_t entry = 0; entry < input->count; entry += 1)
    {
        const uint8_t kind = input->kinds[entry];
        const bool access = kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
        const uint32_t local = access ? (uint32_t)input->values[entry] : BUSTER_IR_NONE;
        capacity += access && debug->binding_flags[local] == BUSTER_IR_DEBUG_BIND_REGIONS;
    }
    event_count = capacity;
    for (uint32_t life = 0; life < input->lifetime_count; life += 1)
    {
        const uint32_t local = input->lifetime_locals[life] & ~BUSTER_TIER0_LIFETIME_END;
        event_count += debug->binding_flags[local] == BUSTER_IR_DEBUG_BIND_REGIONS;
    }
    bool ok = event_count <= UINT32_MAX;
    IrDebugRegion* regions = capacity != 0 ? ir_allocate(output, capacity, sizeof(IrDebugRegion), &ok) : NULL;
    IrTypedDebugEvent* events = capacity != 0 ? ir_allocate(scratch, event_count, sizeof(IrTypedDebugEvent), &ok) : NULL;
    uint32_t* order = capacity != 0 ? ir_allocate(scratch, event_count, 4, &ok) : NULL;
    uint32_t* offsets = capacity != 0 ? ir_allocate(scratch, (uint64_t)input->local_count + 1, 4, &ok) : NULL;
    uint32_t* cursors = capacity != 0 ? ir_allocate(scratch, input->local_count, 4, &ok) : NULL;
    uint8_t* scoped = capacity != 0 ? ir_allocate(scratch, input->local_count, 1, &ok) : NULL;
    uint32_t* reads = capacity != 0 ? ir_allocate(scratch, input->count, 4, &ok) : NULL;
    uint32_t* writes = capacity != 0 ? ir_allocate(scratch, input->count, 4, &ok) : NULL;
    uint32_t count = 0;
    if (ok && capacity != 0)
    {
        memset(reads, 0xff, (uint64_t)input->count * 4);
        memset(writes, 0xff, (uint64_t)input->count * 4);
        for (uint32_t binding = 0; binding < debug->binding_count; binding += 1)
        {
            const uint32_t entry = debug->bindings[binding].entry;
            if (entry != BUSTER_IR_NONE)
            {
                writes[entry] = binding;
            }
        }
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            if (function->op[node] == IR_LOAD && function->op[function->a[node]] == IR_STACK_ADDR)
            {
                const uint32_t local = function->a[function->a[node]];
                const uint32_t entry = debug->origins[node];
                if (local < input->local_count && entry < input->count && debug->binding_flags[local] == BUSTER_IR_DEBUG_BIND_REGIONS &&
                    input->kinds[entry] == BUSTER_TYPED_LOCAL && input->values[entry] == local && function->type[node] == ir_typed_types[input->local_classes[local]])
                {
                    reads[entry] = node;
                }
            }
        }
        uint32_t at = 0;
        uint32_t life = 0;
        for (uint32_t entry = 0; entry < input->count; entry += 1)
        {
            while (life < input->lifetime_count && input->lifetime_entries[life] == entry)
            {
                const uint32_t encoded = input->lifetime_locals[life++];
                const uint32_t local = encoded & ~BUSTER_TIER0_LIFETIME_END;
                if (debug->binding_flags[local] == BUSTER_IR_DEBUG_BIND_REGIONS)
                {
                    scoped[local] = 1;
                    events[at++] = (IrTypedDebugEvent){ .entry = entry, .local = local, .payload = BUSTER_IR_NONE,
                                                      .kind = (encoded & BUSTER_TIER0_LIFETIME_END) != 0 ? IR_TYPED_DEBUG_END : IR_TYPED_DEBUG_START };
                    offsets[local + 1] += 1;
                }
            }
            const uint8_t kind = input->kinds[entry];
            const bool access = kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
            const uint32_t local = access ? (uint32_t)input->values[entry] : BUSTER_IR_NONE;
            if (access && debug->binding_flags[local] == BUSTER_IR_DEBUG_BIND_REGIONS)
            {
                events[at++] = (IrTypedDebugEvent){ .entry = entry, .local = local,
                                                  .payload = kind == BUSTER_TYPED_LOCAL ? reads[entry] : writes[entry],
                                                  .kind = kind == BUSTER_TYPED_LOCAL ? IR_TYPED_DEBUG_READ : IR_TYPED_DEBUG_WRITE };
                offsets[local + 1] += 1;
            }
        }
        ok = at == event_count;
        for (uint32_t local = 0; local < input->local_count; local += 1)
        {
            offsets[local + 1] += offsets[local];
            cursors[local] = offsets[local];
        }
        for (uint32_t event = 0; event < at; event += 1)
        {
            order[cursors[events[event].local]++] = event;
        }
        for (uint32_t local = 0; ok && local < input->local_count; local += 1)
        {
            bool alive = scoped[local] == 0;
            uint32_t event = offsets[local];
            while (ok && event < offsets[local + 1])
            {
                const uint32_t first_event = event;
                const uint32_t first_region = count;
                const uint32_t block = plan->entry_blocks[events[order[event]].entry];
                const uint32_t end = plan->block_firsts[block + 1];
                IrDebugRegion region = {
                    .local = local,
                    .block = block + 1,
                    .begin = plan->block_firsts[block],
                    .block_end = end,
                    .value = BUSTER_IR_NONE,
                    .next = BUSTER_IR_NONE,
                    .kind = IR_DEBUG_BIND_UNAVAILABLE,
                    .bytes = (uint8_t)tier0_class_bytes(input->local_classes[local]),
                    .type = ir_typed_types[input->local_classes[local]],
                };
                bool witnessed = false;
                while (ok && event < offsets[local + 1] && events[order[event]].entry < end)
                {
                    const IrTypedDebugEvent current = events[order[event++]];
                    if (current.kind == IR_TYPED_DEBUG_READ)
                    {
                        if (alive && (!witnessed || (region.kind == IR_DEBUG_BIND_UNAVAILABLE && current.payload != BUSTER_IR_NONE)))
                        {
                            region.value = current.payload;
                            region.kind = current.payload != BUSTER_IR_NONE ? IR_DEBUG_BIND_VALUE : IR_DEBUG_BIND_UNAVAILABLE;
                            witnessed = true;
                        }
                    }
                    else if (current.kind == IR_TYPED_DEBUG_WRITE)
                    {
                        ir_typed_debug_region_end(regions, capacity, &count, region, current.entry, current.payload, alive && witnessed, &ok);
                        const IrDebugBinding binding = debug->bindings[current.payload];
                        region.begin = current.entry + 1;
                        region.literal = binding.literal;
                        region.value = binding.value;
                        region.kind = binding.kind;
                        witnessed = alive;
                    }
                    else
                    {
                        ir_typed_debug_region_end(regions, capacity, &count, region, current.entry, BUSTER_IR_NONE, alive && witnessed, &ok);
                        alive = current.kind == IR_TYPED_DEBUG_START;
                        region.begin = current.entry;
                        region.value = BUSTER_IR_NONE;
                        region.kind = IR_DEBUG_BIND_UNAVAILABLE;
                        witnessed = false;
                    }
                }
                ir_typed_debug_region_end(regions, capacity, &count, region, end, BUSTER_IR_NONE, alive && witnessed, &ok);
                // Even across a lifetime gap, completing a later assignment must prevent an earlier source origin
                // displaced by scheduling from reviving the old value. This reverse sweep visits each event once.
                uint32_t reverse = event;
                uint32_t next = BUSTER_IR_NONE;
                for (uint32_t r = count; r > first_region; r -= 1)
                {
                    while (reverse > first_event && events[order[reverse - 1]].entry >= regions[r - 1].end)
                    {
                        const IrTypedDebugEvent later = events[order[--reverse]];
                        if (later.kind == IR_TYPED_DEBUG_WRITE)
                        {
                            next = later.payload;
                        }
                    }
                    regions[r - 1].next = next;
                }
            }
        }
    }
    if (ok)
    {
        debug->regions = regions;
        debug->region_count = count;
        function->arena_bytes += output->length - output_mark;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return ok;
}

// Whole values whose identity needs no later SSA reconstruction. A parameter must never be assigned or exposed by
// address. A literal local must have exactly one initializing store in the first source block, before its control
// transfer or any incoming label. That store dominates every later source entry. The debug consumer still restricts
// its ranges to origins after the retained completion entry; a constant is not available before initialization.
static bool ir_typed_debug_scalars(const Tier0Function* input, const IrTypedPlan* plan, IrFunction* function, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    bool ok = true;
    const uint32_t locals = input->local_count;
    IrDebugScalar* candidates = ir_allocate(scratch, locals, sizeof(IrDebugScalar), &ok);
    uint8_t* writes = ir_allocate(scratch, locals, 1, &ok);
    const uint32_t first_end = plan->block_count != 0 ? plan->block_firsts[1] : 0;
    if (ok)
    {
        for (uint32_t local = 0; local < locals; local += 1)
        {
            candidates[local] = (IrDebugScalar)
            {
                .local = local,
                .entry = BUSTER_IR_NONE,
            };
            // Every volatile/address-taken object stays a storage binding, never a cached value recipe.
            writes[local] = input->local_flags[local] != 0 ? 2 : 0;
        }
        const uint8_t* const kinds = input->kinds;
        const uint64_t* const values = input->values;
        for (uint32_t entry = 0; entry < input->count; entry += 1)
        {
            const uint8_t kind = kinds[entry];
            const bool store = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
            const bool addressed = kind == BUSTER_TYPED_LOCAL_ADDRESS;
            const bool exchange = kind == BUSTER_TYPED_ATOMIC_CAS;
            if (store || addressed || exchange)
            {
                const uint32_t local = exchange ? (uint32_t)(values[entry] >> BUSTER_TYPED_CAS_LOCAL_SHIFT) : (uint32_t)values[entry];
                candidates[local].entry = writes[local] == 0 ? entry : candidates[local].entry;
                writes[local] = addressed || exchange || writes[local] != 0 ? 2 : 1;
            }
        }
        for (uint32_t local = 0; local < input->parameter_count; local += 1)
        {
            const uint8_t class = input->local_classes[local];
            if (writes[local] == 0 && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64)
            {
                candidates[local].kind = IR_DEBUG_PARAMETER;
                candidates[local].bytes = (uint8_t)tier0_class_bytes(class);
            }
        }
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            if (function->op[node] == IR_STORE && function->op[function->a[node]] == IR_STACK_ADDR)
            {
                const uint32_t local = function->a[function->a[node]];
                const uint32_t value = function->b[node];
                if (local >= input->parameter_count && local < locals && writes[local] == 1 && candidates[local].entry < first_end &&
                    function->debug->origins[node] == candidates[local].entry && function->op[value] == IR_CONST)
                {
                    const uint8_t class = input->local_classes[local];
                    if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 && function->type[value] == ir_typed_types[class])
                    {
                        const uint8_t bytes = (uint8_t)tier0_class_bytes(class);
                        const IrConstant constant = function->constants[function->a[value]];
                        candidates[local].kind = IR_DEBUG_CONSTANT;
                        candidates[local].bytes = bytes;
                        candidates[local].value = bytes == 8 ? constant.low : constant.low & ((UINT64_C(1) << (bytes * 8)) - 1);
                    }
                }
            }
        }
        uint32_t count = 0;
        for (uint32_t local = 0; local < locals; local += 1)
        {
            count += candidates[local].kind != 0;
        }
        IrDebugScalar* scalars = ir_allocate(output, count, sizeof(IrDebugScalar), &ok);
        if (ok)
        {
            uint32_t at = 0;
            for (uint32_t local = 0; local < locals; local += 1)
            {
                if (candidates[local].kind != 0)
                {
                    scalars[at++] = candidates[local];
                }
            }
            function->debug->scalars = scalars;
            function->debug->scalar_count = count;
            function->arena_bytes += output->length - mark;
        }
    }
    if (!ok)
    {
        arena_pop_to(output, mark);
    }
    return ok;
}

// `pieces`: a vector wider than the target moves whole (ir_typed_vector_piece) is taken as its pieces' moves would be, for
// a caller that splits it into them before selection (#1624, ir_vector_split.c); otherwise it is refused.
static IrCheck ir_lower_typed_target_path(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result, bool vector, uint64_t target_features,
                                          bool pieces, bool sources)
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
             (input->argument_count != 0 && (input->argument_classes == 0 || input->argument_locations == 0)) ||
             (input->lifetime_count != 0 && (input->lifetime_entries == 0 || input->lifetime_locals == 0)))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    // The objects' lifetime events (#177), which Tier 0 doesn't read: each of a local, in entry order, before an entry.
    for (uint32_t k = 0; check.error == IR_OK && k < input->lifetime_count; k += 1)
    {
        const uint32_t entry = input->lifetime_entries[k];
        if (entry >= input->count || (input->lifetime_locals[k] & ~BUSTER_TIER0_LIFETIME_END) >= input->local_count || (k != 0 && entry < input->lifetime_entries[k - 1]))
        {
            check.error = IR_ERROR_STRUCTURE;
            check.instruction = entry;
        }
    }
    bool leaves = false;
    for (uint32_t k = 0; check.error == IR_OK && k < input->asm_body_count && input->asm_bodies != 0; k += 1)
    {
        // A body's labels (#1839), which the IR's body call doesn't carry, are Tier 0's too.
        leaves |= input->asm_bodies[k].leaves || input->asm_bodies[k].label_count != 0;
    }
    if (check.error == IR_OK && (input->unwind_init || input->returns_twice || tier0_preserving(input) || input->realign_stack || leaves))
    {
        // An asm body that returns from its function (#1839, Tier0AsmBody.leaves), whose function keeps no local and no
        // value in a callee-saved register, as __builtin_setjmp's keeps none, is Tier 0's too.
        // __builtin_unwind_init's frame, which saves every callee-saved register (Tier0Function.unwind_init),
        // __builtin_setjmp's, which keeps nothing in one (Tier0Function.returns_twice), a preserving function's, which
        // keeps registers for its caller past them (#147, Tier0Function.preserve_general), and a force_align_arg_pointer
        // function's, which realigns its stack (#1447, Tier0Function.realign_stack), are Tier 0's: ordinary Tier 1 falls
        // back to them. A function with segment overrides (#1447, Tier0Function.segments) is Tier 1's where each is an
        // access the IR carries (ir_typed_segment_valid). A caller of setjmp's library kin lowers
        // (Tier0Function.calls_returns_twice): only Tier 1's admission leaves it to Tier 0 (check_tier1_compile, #1847).
        check.error = IR_ERROR_UNSUPPORTED;
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
        else if (kind == BUSTER_TYPED_CALL && input->call_targets[value] >= symbol_count && (input->call_asm == 0 || input->call_asm[value] == 0))
        {
            check.error = IR_ERROR_OPERAND;
            check.instruction = i;
        }
        else if (kind == BUSTER_TYPED_CALL && input->call_targets[value] == 0 && input->call_variadic[value] != 0 && input->call_argument_counts[value] != 0 &&
                 input->argument_locations[input->call_first[value]].kind == BUSTER_ABI_LOCATION_REGISTERS &&
                 input->argument_locations[input->call_first[value]].registers[0] == BUSTER_ABI_R10)
        {
            // An indirect variadic call with a static chain (#148): its chain takes r10, the moves' scratch r11 and its al
            // rax, which Tier 1 calls through otherwise (tier1_select_callee_register); Tier 1 falls back to Tier 0.
            check.error = IR_ERROR_UNSUPPORTED;
            check.instruction = i;
        }
        else if (__builtin_expect(input->segments, 0) && !ir_typed_segment_valid(input, i))
        {
            // A segment override the IR doesn't carry: Tier 0 compiles the function.
            check.error = IR_ERROR_UNSUPPORTED;
            check.instruction = i;
        }
        else if (kind == BUSTER_TYPED_TAIL_CALL)
        {
            // A valid typed operation, but this adapter does not lower it. Refuse before planning so it doesn't reach
            // the default binary-operation case; ordinary Tier 1 may fall back to Tier 0.
            check.error = IR_ERROR_UNSUPPORTED;
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
            // Tier 1's moves of it (ir_typed_vector_moves, #1281): AVX512F's of a zmm register; of an xmm or a ymm one, or
            // an SSE carrier's, VL's EVEX ones, or else AVX's VEX ones. A vector wider than the target moves whole, where
            // the caller splits it (#1624), its pieces' (ir_typed_vector_piece: x86-64's xmm, AVX's ymm).
            const uint32_t piece = ir_typed_vector_piece(target_features);
            const uint64_t required = ir_typed_vector_moves(pieces && bytes > piece && bytes <= 64 ? piece : bytes, target_features);
            // DUP, DISCARD and JUMP_VALUE have no own width; their source has already been admitted. Every real
            // transport is a full 128/256/512-bit value or an SSE carrier of 4 or 8 bytes (#1280), an f32 or f64 of its
            // bits (ir_typed_stack_type).
            const bool inherited = kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_JUMP_VALUE;
            if (!inherited && bytes != 4 && bytes != 8 && bytes != 16 && bytes != 32 && bytes != 64)
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
        plan.ignored_arguments = ir_typed_ignored_arguments(input, BUSTER_VECTOR_PATH(vector));
        // A function with vectors copies its whole-vector aggregate parameters by vectors of the widths its target has
        // (#1282): the lowering admits those widths alone (AVX-512F, and VL for 16 and 32 bytes, as a vector entry's).
        const uint64_t evex = BUSTER_X86_FEATURE(AVX512F);
        const uint64_t narrow = evex | BUSTER_X86_FEATURE(AVX512VL);
        plan.vector_copies = !input->vectors || (target_features & evex) != evex ? 0 : (target_features & narrow) == narrow ? 7 : 4;
        if (plan.error == 0 && !ir_typed_plan_depth_slots(&plan, scratch))
        {
            plan.error = "no scratch memory for the depth slots";
            plan.error_code = IR_ERROR_MEMORY;
        }
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
        if (check.error == IR_OK && sources)
        {
            const IrDebugInfo source = { .entry_count = input->count, .local_count = input->local_count };
            check.error = ir_debug_create(output, &candidate, &source) ? IR_OK : IR_ERROR_MEMORY;
        }
        if (check.error == IR_OK)
        {
            if (sources)
            {
                ir_typed_emit_sources_function(input, result_type, &plan, scratch, ops, types, stack, classes, &candidate);
                for (uint32_t local = 0; local < input->local_count; local += 1)
                {
                    candidate.debug->locals[local] = local;
                }
                check.error = ir_typed_debug_bindings(input, &plan, &candidate, output, scratch) &&
                              ir_typed_debug_regions(input, &plan, &candidate, output, scratch) &&
                              ir_typed_debug_scalars(input, &plan, &candidate, output, scratch) ? IR_OK : IR_ERROR_MEMORY;
            }
            else
            {
                ir_typed_emit_function(input, result_type, &plan, scratch, ops, types, stack, classes, &candidate);
            }
            candidate.symbol_count = symbol_count;
            if (check.error == IR_OK)
            {
                check = BUSTER_VECTOR_PATH(vector) ? ir_verify(&candidate, scratch, 0) : ir_verify_scalar(&candidate, scratch, 0);
            }
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
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, vector, 0, false, false);
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
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, true, 0, false, false);
#endif
}

static __attribute__((unused)) IrCheck ir_lower_typed_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, false, 0, false, false);
}

// Output features are explicit. The old entry points intentionally grant none: a vector producer must retain its
// selected target, not borrow the compiler host's AVX-512 capabilities. IR_SIMD_TYPED_* in detail distinguishes
// unsupported forms from missing target features on IR_ERROR_UNSUPPORTED.
static __attribute__((unused)) IrCheck ir_lower_typed_target(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, BUSTER_HOST_VECTOR != 0, target_features, false, false);
}

static __attribute__((unused)) IrCheck ir_lower_typed_target_scalar(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch, IrFunction* result)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, false, target_features, false, false);
}

// The C driver's --tier1 (#1624): ir_lower_typed_target, but a vector wider than the target moves whole is taken, for
// ir_vector_split to split into the target's widest vectors after promotion; by the vector path, or by its scalar twin.
static __attribute__((unused)) IrCheck ir_lower_typed_target_pieces(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch,
                                                                    IrFunction* result, bool scalar)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, !scalar && BUSTER_HOST_VECTOR != 0, target_features, true, false);
}

// The same code-producing replay, with optional source identity columns. The ordinary entry points keep their
// source-less instantiation, so collecting these identities adds no per-entry work to a compile without -g.
static __attribute__((unused)) IrCheck ir_lower_typed_target_pieces_sources(const Tier0Function* input, IrType result_type, uint32_t symbol_count, uint64_t target_features, Arena* output, Arena* scratch,
                                                                            IrFunction* result, bool scalar)
{
    return ir_lower_typed_target_path(input, result_type, symbol_count, output, scratch, result, !scalar && BUSTER_HOST_VECTOR != 0, target_features, true, true);
}
