// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's machine consumer for a general CFG (#140): a selected plan whose blocks branch anywhere, with loops, and
// whose joins have phis. tier1_emit.c includes it after its straight-line helpers, and tier1_emit_function sends here
// every function that isn't a phi-free straight-line chain, which keeps the straight-line consumer.
//
// The plan becomes tier1_cfg.c's problem. Node n's effective uses are at base + 2n and its definition at the next
// position, with a required use there (the instruction writes a register). A phi is defined at its block's start, its
// operands in the predecessors' order. Parameters are defined at position 0, in their incoming registers. When the
// options ask, constants, symbols' addresses and frame objects' addresses are rematerializable (#178): a fragment the
// allocator takes out of a register recomputes its value where it needs one again, with no slot, no store and no
// reload; a symbol's copy is one more reference. tier1_cfg's entry has no predecessors, so where the IR's entry has some
// (a loop's header), an empty block goes before it and base is 2. tier1_cfg's one-call order (#2538,
// tier1_emit_cfg_one_call) then allocates the selected uses over the CFG: the intervals once, the plain problem's verdict,
// and the problem that prefers callee-saved registers for values live across a call (tier1_emit_cfg_preference).
//
// The records, block by block in layout order: the entry's prologue, then at each block's start its single incoming
// edge's moves where tier1_cfg placed them there; each node as the straight-line consumer emits it (its fragment
// transitions at both phases, its selected records, a call's saves and argument moves, a return's epilogue); then the
// block's terminator. A jump's edge moves go before it. A conditional branch's go at its successors' starts or, for a
// split critical edge, in a stub after the branch: the edge's moves, then a jump to the successor. A jump to the
// record that follows it is left out. A branch is inverted to reach a lone stub inline or the next block directly;
// with two stubs, the next block's stub goes last and falls into it. An indirect jump's (#789) go at its labels' blocks'
// starts or in stubs after it, and each label's symbol where that jump enters it, its stub or its block (tier1_emit_cfg_labels); one that
// can go to one label alone jumps there. Branch targets are record indices, which branch sizing takes.
//
// At every block's start, the emitter's view of where each value is becomes the block's entry state: the intervals
// that cover its start, at tier1_cfg_entry_location, so that no predecessor's exit state leaks into another block. Move
// locations are tier1_cfg's: registers, the values' spill slots and the classes' temporary slots, frame rows after the
// call saves'. A spill slot's frame lifetime is its value's intervals' hull, a phi's widened to its predecessors' ends,
// where its edges write it; two slots share a frame row only where their values are never live at once. Fixed ranges
// and splits don't arise: calls and the fixed registers' sequences save live registers in rows of their own, as in the
// straight-line consumer.
//
// Its values are general registers' and AVX-512's masks (#179), the opmask class's, in k1 to k7 with k0 the class's
// scratch for moves, which kmov makes at the mask's width: between opmask registers and to and from their slots. No
// opmask register survives a call, so a call saves each mask live past it in a row of the opmask registers' own and
// reloads it after, by kmov of its width; a value of the class lives in no other class's register, and no fixed
// register's sequence takes one.
//
// The frame's rows, in order: the spilled values' slots, the general registers' call-save rows (the fixed registers'
// among them), the opmask registers' (one per register a mask holds across a call), the general class's temporary slot
// and the opmask class's where moves use them, then the frame objects. A variadic definition's register save area
// (#789) is the frame's own past the rows (BUSTER_TIER1_FRAME_VARIADIC), which the prologue fills from the argument
// registers, and the vector ones where al isn't zero, before anything else writes them; its overflow area is in the
// caller's frame, past the incoming stack arguments (tier1_emit_cfg_incoming).
//
// Borrowed inputs are immutable; the arenas are disjoint. tier1_emit_function restores both arenas' lengths on failure
// and leaves the result unchanged.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// A pending jump's goal: a block, or with this bit an edge's stub.
#define BUSTER_TIER1_EMIT_CFG_STUB 0x80000000u
// The longest chain of copies and constant displacements a recipe is found through (tier1_emit_cfg_recipes).
#define BUSTER_TIER1_EMIT_CFG_RECIPE_DEPTH 8
// The most records tier1_emit_cfg_x87 appends for one long double node (#141): an unsigned 64-bit conversion's 20, its
// control words' 6 and the 2^63 adjustment's 14. The records' capacity counts it per such node (#1204's review), and the
// emitter refuses a node past it.
#define BUSTER_TIER1_EMIT_X87_RECORDS 20
// The most records tier1_emit_cfg_row writes for one machine row (#141): a move into place of each operand, the row's own
// records and its result's move, and a store and a load of each of k1 to k7 it borrows, past every node's share.
#define BUSTER_TIER1_EMIT_ROW_RECORDS (BUSTER_TIER0_OPERATION_OPERANDS + BUSTER_TIER0_OPERATION_RECORDS + 1 + 2 * 7)

// Whether the function is a straight-line chain without phis, the straight-line consumer's: every block but the last
// jumps to the next, and the last returns, traps or is an empty self-loop.
static bool tier1_emit_cfg_chain(const Tier1SelectPlan* plan)
{
    const IrFunction* function = plan->function;
    bool chain = true;
    for (uint32_t block = 0; chain && block < function->block_count; block += 1)
    {
        const uint32_t last = function->block_offsets[block + 1] - 1;
        const bool final = block + 1 == function->block_count;
        const bool sink = function->op[last] == IR_BR && function->a[last] == block && function->block_offsets[block] == last;
        const bool terminal = function->op[last] == IR_RET || function->op[last] == IR_UNREACHABLE || sink;
        chain = final ? terminal : function->op[last] == IR_BR && function->a[last] == block + 1;
    }
    // A phi needs edge moves, a frame object's address the frame's layout of it, a symbol's its reference, a narrow
    // parameter or one after other instructions the entry's definitions: the CFG consumer's. A division and a shift by a
    // register stay the CFG consumer's too, which keeps their functions' code as it was before both consumers saved
    // fixed registers (tier1_emit_fixed); a division by a constant's reciprocal takes them in either consumer.
    // Labels' symbols, which the CFG consumer defines at their blocks (tier1_emit_cfg_labels), make a function its own too,
    // and so does a variadic definition, whose prologue fills its register save area (tier1_emit_cfg_frame_flags).
    chain &= function->label_count == 0 && !function->variadic && function->parameter_passed == 0;
    // A vector (#141) needs the vector class's allocation at its width: a function with one is the CFG consumer's, which
    // one scan of it says, by the path that planned it.
    chain &= !ir_function_has_vectors(function);
    bool prefix = true;
    for (uint32_t node = 0; chain && node < function->instruction_count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t type = function->type[node];
        const bool narrow = type == IR_I1 || type == IR_I8 || type == IR_I16;
        const uint8_t rule = plan->covered[node] ? TIER1_RULE_NONE : plan->patterns[node].rule;
        // A mask, and a mask's store, need the opmask class's allocation (#179), and a floating value, its operations and
        // its comparison the vector class's (#141).
        const bool mask = tier1_select_mask_width(type) != 0 || tier1_select_mask_rule(rule);
        // A long double's (#141) needs the CFG consumer's frame objects of values.
        const bool floating = tier1_select_float_width(type) != 0 || tier1_select_float_rule(rule) || tier1_select_x87_node(function, node);
        // Arguments past their classes' registers, and parameters past them, which the CFG consumer's entry loads, are its
        // own (#141).
        const bool stacked = (op == IR_CALL && tier1_select_call_stack_bytes(function, &function->calls[function->b[node]]) != 0) ||
                             (op == IR_PARAM && tier1_select_parameter_place(function, function->a[node]).kind == BUSTER_TIER1_PLACE_STACK);
        // The dynamic stack's operations need the CFG consumer's frame pointer and call areas (#141).
        const bool dynamic = op == IR_ALLOCA || op == IR_STACK_SAVE || op == IR_STACK_RESTORE;
        // A machine row (#141) is the CFG consumer's (tier1_emit_cfg_row).
        chain = op != IR_PHI && op != IR_STACK_ADDR && op != IR_LIFETIME && op != IR_SYMBOL_ADDR && op != IR_CALL_INDIRECT && (op != IR_PARAM || (prefix && !narrow)) && rule != TIER1_RULE_DIVIDE && !dynamic &&
                (op != IR_CALL || (function->calls[function->b[node]].abi == 0 && function->calls[function->b[node]].row == 0 && function->calls[function->b[node]].body == 0)) &&
                rule != TIER1_RULE_SHIFT && !mask && !floating && !stacked;
        prefix &= op == IR_PARAM;
    }
    return chain;
}

// The callee-saved registers that values live across a call may take (tier1_emit_cfg_preference): rbp among them where the
// frame keeps no frame pointer in it (#1116), which the frame's reservations say before allocation.
#define BUSTER_TIER1_EMIT_CFG_CALLEE ((1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R12) | (1u << X86_REGISTER_R13) | (1u << X86_REGISTER_R14) | (1u << X86_REGISTER_R15))

// The caller-saved registers a recomputable value read on every iteration of a loop may hold across its calls besides the
// callee-saved ones (#2574, tier1_emit_cfg_preference): those no call takes but past its fourth argument, nor any
// instruction implicitly (rax a result's and a division's, rcx a shift's, rdx a division's), as clang keeps Lua's
// dispatch table in r10. A call evicts the value from them (the preferred problem's clobbers): it is recomputed where it
// is next needed, never stored.
#define BUSTER_TIER1_EMIT_CFG_RESIDENT ((1u << X86_REGISTER_R8) | (1u << X86_REGISTER_R9) | (1u << X86_REGISTER_R10))
// The most edges the every-iteration test visits per block of its function, all values and loops together.
#define BUSTER_TIER1_EMIT_CFG_ITERATION_VISITS 64u

// A register the CFG consumer's values may be in: the selector's caller-saved ones, and the callee-saved ones that values
// live across a call take (tier1_emit_cfg_preference); never rsp or the scratch register, and rbp only where the frame
// doesn't reserve it.
static bool tier1_emit_cfg_register(uint8_t reg)
{
    return tier1_select_register_allowed(reg) || (reg < 16 && ((BUSTER_TIER1_EMIT_CFG_CALLEE >> reg) & 1) != 0);
}

// Whether a location is an allocatable vector register. Each value's actual allowed mask also excludes target-illegal
// registers and the function's row/call reservations; xmm15 remains the class's scratch.
static bool tier1_emit_cfg_vector_location(uint32_t location)
{
    const uint32_t x = location - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
    return x < 32 && ((BUSTER_TIER1_EMIT_VECTOR_REGISTERS >> x) & 1) != 0;
}

// Whether node's value is in a register of its class at `location` (the emitter's view, by location): a mask in k1 to
// k7 (#179), a floating value in xmm0 to xmm14 (#141), any other value in tier1_emit_cfg_register's.
static bool tier1_emit_cfg_holds(const Tier1EmitResult* emitted, uint32_t node, uint8_t location)
{
    const uint32_t class = emitted->prepared.classes != 0 ? emitted->prepared.classes[node] : BUSTER_TIER1_ALLOCATE_GPR;
    const uint32_t k = (uint32_t)location - BUSTER_TIER1_EMIT_MASK_LOCATION;
    return class == BUSTER_TIER1_ALLOCATE_MASK ? k < 8 && ((BUSTER_TIER1_EMIT_MASK_REGISTERS >> k) & 1) != 0 : class == BUSTER_TIER1_ALLOCATE_VECTOR ? tier1_emit_cfg_vector_location(location) : tier1_emit_cfg_register(location);
}

// Whether a location is a register the emitter's view keeps an owner for: a general register, an xmm one or an opmask one.
static bool tier1_emit_cfg_register_location(uint32_t location)
{
    return location < 16 || (location >= BUSTER_TIER1_EMIT_VECTOR_LOCATION && location < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 32) || (location >= BUSTER_TIER1_EMIT_MASK_LOCATION && location < BUSTER_TIER1_EMIT_MASK_LOCATION + 8);
}

// The frame's rows past the spilled values' slots (see the header): the opmask registers' call-save row of k, the xmm
// registers' of x (#141), the temporary slot of `class` (the classes' in their order), and frame object `object`'s.
static uint32_t tier1_emit_cfg_mask_save_row(const Tier1EmitResult* emitted, uint32_t k)
{
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    return emitted->cfg.allocation.spill_count + (uint32_t)__builtin_popcount(prepared->call_save_mask) + (uint32_t)__builtin_popcount(prepared->mask_save_mask & ((1u << k) - 1));
}

static uint32_t tier1_emit_cfg_vector_save_row(const Tier1EmitResult* emitted, uint32_t x)
{
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    return emitted->cfg.allocation.spill_count + (uint32_t)__builtin_popcount(prepared->call_save_mask) + (uint32_t)__builtin_popcount(prepared->mask_save_mask) +
           (uint32_t)__builtin_popcount(prepared->vector_save_mask & ((1u << x) - 1));
}

static uint32_t tier1_emit_cfg_temporary_row(const Tier1EmitResult* emitted, uint32_t class)
{
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    const uint32_t before = (uint32_t)__builtin_popcount(emitted->cfg.temp_slots & ((1u << class) - 1));
    return emitted->cfg.allocation.spill_count + (uint32_t)__builtin_popcount(prepared->call_save_mask) + (uint32_t)__builtin_popcount(prepared->mask_save_mask) +
           (uint32_t)__builtin_popcount(prepared->vector_save_mask) + before;
}

// Storage row `row`'s bytes, below the frame objects' rows, as tier1_emit_cfg_frame sizes their columns: a spilled
// value's its own, a general register's call save 8, an opmask register's and an xmm register's (#141) theirs, a temporary
// slot its class's. Past them, 0. The code's certificate (#1284) takes the rows' bytes as frame storage.
static uint32_t tier1_emit_cfg_row_bytes(const Tier1EmitResult* emitted, uint32_t row)
{
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    const uint32_t count = prepared->function->instruction_count;
    const uint32_t spills = emitted->cfg.allocation.spill_count;
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_mask);
    const uint32_t call_columns = (uint32_t)__builtin_popcount(prepared->call_save_columns);
    const uint32_t mask_slots = (uint32_t)__builtin_popcount(prepared->mask_save_mask);
    const uint32_t vector_slots = (uint32_t)__builtin_popcount(prepared->vector_save_mask);
    const uint32_t temporaries = emitted->cfg.temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1);
    const uint32_t temporary_columns = call_columns + prepared->mask_columns + prepared->vector_columns;
    // The n-th set bit of a register mask, and its column: a general register's the n-th of the columns' mask (#177).
    uint32_t column = UINT32_MAX;
    uint32_t index = row;
    uint32_t mask = 0;
    uint32_t base = 0;
    bool general = false;
    if (index < spills)
    {
        column = emitted->value_nodes[emitted->cfg.allocation.spill_values[index]];
    }
    else if ((index -= spills) < call_slots)
    {
        mask = prepared->call_save_mask;
        base = count;
        general = true;
    }
    else if ((index -= call_slots) < mask_slots)
    {
        mask = prepared->mask_save_mask;
        base = count + call_columns;
    }
    else if ((index -= mask_slots) < vector_slots)
    {
        mask = prepared->vector_save_mask;
        base = count + call_columns + prepared->mask_columns;
    }
    else if ((index -= vector_slots) < (uint32_t)__builtin_popcount(temporaries))
    {
        mask = temporaries;
        base = count + temporary_columns;
    }
    for (uint32_t k = 0; column == UINT32_MAX && mask != 0; k += 1)
    {
        const uint32_t bit = (uint32_t)__builtin_ctz(mask);
        const uint32_t offset = general ? (uint32_t)__builtin_popcount(prepared->call_save_columns & ((1u << bit) - 1)) : bit;
        column = k == index ? base + offset : UINT32_MAX;
        mask &= mask - 1;
    }
    return column < prepared->slot_columns ? prepared->sizes[column] : 0;
}

static uint32_t tier1_emit_cfg_object_row(const Tier1EmitResult* emitted, uint32_t object)
{
    const uint32_t temporaries = (uint32_t)__builtin_popcount(emitted->cfg.temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1));
    return tier1_emit_cfg_temporary_row(emitted, BUSTER_TIER1_ALLOCATE_GPR) + temporaries + object;
}

// Whether frame object `slot` is a variadic definition's overflow area (#789), whose address is in the caller's frame,
// past the incoming stack arguments (tier1_emit_cfg_incoming): the frame's base doesn't reach it, so its address has
// no frame row and no recipe.
static bool tier1_emit_cfg_overflow_area(const IrFunction* function, uint32_t slot)
{
    return slot < function->slot_count && (function->slots[slot].flags & BUSTER_IR_SLOT_OVERFLOW_AREA) != 0;
}

// The frame objects: the IR's slots, then, for a function with its own ABI records (#789), one per parameter (a by-value
// aggregate's copy, tier1_select_aggregate_parameter's) and one for the hidden pointer an aggregate result returns
// through, which the entry stores (tier1_emit_cfg_entry); then, for a function with long doubles (#141), one per F80
// node, its value's 16 bytes (tier1_emit_cfg_x87_object), and the x87's scratch object past them, which a conversion's
// control words and its integer or floating value pass through. A call's overflow staging object (#1450) is last,
// where one is needed. Their rows follow in that order.
static uint32_t tier1_emit_cfg_record_count(const IrFunction* function)
{
    return function->slot_count + (function->parameter_passed != 0 ? function->parameter_count + 1 : 0);
}

static uint32_t tier1_emit_cfg_object_count(const IrFunction* function, const Tier1EmitPrepared* prepared)
{
    return tier1_emit_cfg_record_count(function) + prepared->x87_count + (prepared->x87_count != 0 ? 1 : 0) + (prepared->staging_slots != 0 ? 1 : 0);
}

// A long double node's frame object (#141), or the x87 scratch object for UINT32_MAX.
static uint32_t tier1_emit_cfg_x87_object(const IrFunction* function, const Tier1EmitPrepared* prepared, uint32_t node)
{
    return tier1_emit_cfg_record_count(function) + (node == UINT32_MAX ? prepared->x87_count : prepared->x87_ranks[node]);
}

// Whether a by-value aggregate parameter's `record` has its copy in the frame (#789), which the entry stores its parts into
// (tier1_emit_cfg_entry): in registers, or in a register and a stack slot or in a slot alone (#141), but not on the stack
// whole, whose copy is its stack slots (tier1_emit_cfg_incoming_object).
static bool tier1_emit_cfg_record_copied(const IrPassed* record)
{
    return record != 0 && record->size != 0 && record->location.kind != BUSTER_ABI_LOCATION_IGNORE && !tier1_select_location_whole(record->location);
}

// The bytes a by-value aggregate parameter's copy at `location` takes (#789), which the entry stores its parts into whole:
// its size rounded up to 8, an eightbyte's store's, or past it a vector register's whole (#2798: a record's payload's
// register is wider than the record, and so may be a PAYLOAD slot's, which the entry copies whole).
static uint32_t tier1_emit_cfg_copy_bytes(AbiLocation location, uint32_t size)
{
    Tier1SelectPart parts[2];
    const uint32_t count = tier1_select_location_parts(location, size, parts);
    uint32_t bytes = (size + 7u) & ~7u;
    for (uint32_t k = 0; k < count; k += 1)
    {
        const uint32_t end = parts[k].offset + tier1_select_part_width(parts[k].bytes);
        bytes = end > bytes ? end : bytes;
    }
    return bytes;
}

// The bytes frame object `object` past the IR's slots takes (#789): a by-value aggregate parameter's copy, which the entry
// stores its parts into whole, 8 bytes each, so its size rounded up to them, or a vector register's whole bytes
// (tier1_emit_cfg_copy_bytes), and the hidden pointer's 8 bytes. An ignored aggregate's copy (#141) takes its size too,
// which the entry leaves as it is, as the callee may copy it whole. A parameter in registers of its own, or one on the
// stack whole (whose copy is its stack slots, tier1_emit_cfg_incoming), takes a row of one byte, which nothing reads, as
// a function without the hidden pointer does for it, and so does an ignored aggregate of no bytes.
static uint32_t tier1_emit_cfg_record_object_bytes(const IrFunction* function, const Tier1EmitPrepared* prepared, uint32_t object)
{
    // The optional final object stages all register-passed aggregate parts of the largest call (#1450).
    const bool staging = prepared->staging_slots != 0 && object + 1 == tier1_emit_cfg_object_count(function, prepared);
    // A long double's object and the x87 scratch object (#141): 16 bytes, aligned to 16.
    const bool x87 = object >= tier1_emit_cfg_record_count(function);
    const uint32_t parameter = object - function->slot_count;
    const IrPassed* record = parameter < function->parameter_count ? tier1_select_parameter_record(function, parameter) : 0;
    const bool copied = tier1_emit_cfg_record_copied(record) || (record != 0 && record->size != 0 && record->location.kind == BUSTER_ABI_LOCATION_IGNORE);
    const bool hidden = parameter == function->parameter_count && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
    return staging ? 8 * prepared->staging_slots : x87 ? 16 : copied ? tier1_emit_cfg_copy_bytes(record->location, record->size) : hidden ? 8 : 1;
}

// Whether frame object `object` has its address in the caller's frame, past the return address, `*past` bytes into the
// incoming argument area (tier1_emit_cfg_incoming): a variadic definition's overflow area (#789), past the named
// arguments' stack bytes, or a by-value aggregate parameter on the stack whole (#789), at its record's offset. The
// frame's base doesn't reach either, so neither has a frame row's address or a recipe.
static bool tier1_emit_cfg_incoming_object(const IrFunction* function, uint32_t object, uint64_t* past)
{
    const uint32_t parameter = object - function->slot_count;
    const IrPassed* record = object >= function->slot_count && parameter < function->parameter_count ? tier1_select_parameter_record(function, parameter) : 0;
    const bool stacked = record != 0 && record->size != 0 && tier1_select_location_whole(record->location);
    const bool overflow = tier1_emit_cfg_overflow_area(function, object);
    *past = overflow ? function->incoming.stack_bytes : stacked ? record->location.stack_offset : 0;
    return overflow || stacked;
}

// The frame's flags: the options' (tier1_emit_frame_flags, by whether the function `calls`), and
// BUSTER_TIER1_FRAME_VARIADIC for a variadic definition with a register save area (#789), which the frame reserves and its
// prologue fills from the argument registers (tier1_frame.c): the IR's save area is that one (tier1_emit_cfg_object).
static uint32_t tier1_emit_cfg_frame_flags(const IrFunction* function, const Tier1EmitOptions* options, bool calls)
{
    bool save_area = false;
    for (uint32_t s = 0; s < function->slot_count; s += 1)
    {
        save_area |= (function->slots[s].flags & BUSTER_IR_SLOT_REGISTER_SAVE_AREA) != 0;
    }
    // The dynamic stack (#141): a frame pointer, the frame's places from rbp (or a realigned frame's from r15) and each
    // call's outgoing area at its own rsp (Tier1Frame.dynamic_outgoing).
    // A frame address (#1847, a save of BUSTER_IR_STACK_SAVE_FRAME) keeps the frame pointer it reads, whatever the
    // options say of a leaf's, without the dynamic stack's places.
    bool dynamic = false;
    bool frame_address = false;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        const bool frame = function->op[node] == IR_STACK_SAVE && function->flags[node] == BUSTER_IR_STACK_SAVE_FRAME;
        dynamic |= function->op[node] == IR_ALLOCA || (function->op[node] == IR_STACK_SAVE && !frame) || function->op[node] == IR_STACK_RESTORE;
        frame_address |= frame;
    }
    // A function with its own ABI records (#789) stages an aggregate result's parts below rsp on its way out
    // (tier1_emit_cfg_aggregate_result): its frame stays out of the red zone. Its vector constants, which #141 built there,
    // are its literals' loads since #2744 (tier1_emit_cfg_vector_constant), which write nothing.
    return tier1_emit_frame_flags(options, calls) | (function->variadic && save_area ? BUSTER_TIER1_FRAME_VARIADIC : 0) |
           (function->parameter_passed != 0 ? BUSTER_TIER1_FRAME_NO_RED_ZONE : 0) | (dynamic ? BUSTER_TIER1_FRAME_DYNAMIC_STACK : 0) |
           (frame_address ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0);
}

// The alignment's log2 that rsp keeps in a dynamic frame (#141): the most any call's stack arguments need, 16 bytes or a
// 32- or 64-byte vector's or an over-aligned aggregate's (Tier1EmitPrepared.call_abi), so that every call after an
// allocation finds its area aligned. An allocation rounds its size up to it, and so does each call's outgoing area
// (tier1_emit_cfg_call_area). The prologue realigns rsp to the frame's alignment, which includes the calls'; a frame
// object's own alignment past that is its base register's (rbp or r15), not rsp's, and an IR allocation's address is
// 16-byte aligned. A frame realigned to a megabyte (#1450) therefore doesn't round each allocation and call to it.
static uint8_t tier1_emit_cfg_stack_alignment(const Tier1EmitResult* emitted)
{
    return emitted->prepared.call_abi.stack_alignment_log2 > BUSTER_ABI_STACK_ALIGNMENT_LOG2 ? emitted->prepared.call_abi.stack_alignment_log2 : BUSTER_ABI_STACK_ALIGNMENT_LOG2;
}

// A call's outgoing area in a dynamic frame (Tier1Frame.dynamic_outgoing, #141): its stack bytes rounded up to the
// alignment rsp keeps, which the call takes from rsp before its arguments' stores and gives back after it returns; 0 for
// a call without stack arguments or in a fixed frame, whose area is the frame's own.
static uint32_t tier1_emit_cfg_call_area(const Tier1EmitResult* emitted, uint32_t stack_bytes)
{
    const uint32_t alignment = UINT32_C(1) << tier1_emit_cfg_stack_alignment(emitted);
    return emitted->frame.dynamic_outgoing && stack_bytes != 0 ? (stack_bytes + alignment - 1) & ~(alignment - 1) : 0;
}

// rsp less or plus a call's outgoing area (#141): `sub rsp, area` or `add rsp, area`. Under stack-clash protection
// (#177, `probe`), the area's lowest byte is touched after the subtraction, as an allocation's is
// (tier1_emit_cfg_probed_allocation): rsp starts at a touched address (the probed frame's or an allocation's), and the
// call's stores may write the area's lowest bytes first. At a page or more, the frame prologue's loop touches every
// page and the final partial step. R11 is the selector's reserved scratch, so the loop preserves all arguments and an
// indirect callee still in their allocated registers. Dynamic frames keep the CFA in rbp throughout it.
static Tier1EmitError tier1_emit_cfg_adjust(uint32_t area, bool release, bool probe, Tier1EmitBuffer* buffer)
{
    const uint16_t form = area <= 127 ? (release ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_SUB_RM64_IMM8) : (release ? X86_FORM_ADD_RM64_IMM32 : X86_FORM_SUB_RM64_IMM32);
    const X86Instruction record = { .form = form, .rm = X86_REGISTER_RSP, .immediate = (int32_t)area };
    const X86Instruction touch = { .form = X86_FORM_OR_RM8_IMM8, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
    const bool probed = probe && !release && area != 0;
    const bool loop = probed && area >= BUSTER_TIER1_FRAME_PAGE_BYTES;
    const uint32_t records = area == 0 ? 0 : loop ? 6u + 2u * (area % BUSTER_TIER1_FRAME_PAGE_BYTES != 0) : 1u + probed;
    Tier1EmitError error = area > INT32_MAX ? TIER1_EMIT_LIMIT : TIER1_EMIT_OK;
    if (error == TIER1_EMIT_OK)
    {
        error = buffer->count <= buffer->capacity && records <= buffer->capacity - buffer->count &&
                (!loop || buffer->branch_count < buffer->branch_capacity) ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK && loop)
    {
        (void)tier1_frame_emit_probe_allocate(area, buffer);
    }
    else if (error == TIER1_EMIT_OK)
    {
        error = area == 0 || tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
        error = error != TIER1_EMIT_OK || !probed || tier1_emit_append(buffer, touch) ? error : TIER1_EMIT_MEMORY;
    }
    return error;
}

// The inverse of a conditional jump's form, or X86_FORM_NONE for a form the selector doesn't choose.
static uint16_t tier1_emit_cfg_inverse(uint16_t form)
{
    static const uint16_t pairs[][2] = {
        { X86_FORM_JE_REL8, X86_FORM_JNE_REL8 },
        { X86_FORM_JL_REL8, X86_FORM_JGE_REL8 },
        { X86_FORM_JLE_REL8, X86_FORM_JG_REL8 },
        { X86_FORM_JB_REL8, X86_FORM_JAE_REL8 },
        { X86_FORM_JBE_REL8, X86_FORM_JA_REL8 },
    };
    uint16_t inverse = X86_FORM_NONE;
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(pairs); p += 1)
    {
        inverse = pairs[p][0] == form ? pairs[p][1] : pairs[p][1] == form ? pairs[p][0] : inverse;
    }
    return inverse;
}

// The values a fragment may recompute rather than reload (#178), by node, into the recipes' columns (zero where none):
// - a constant;
// - a symbol's address, from rip or read from its GOT slot as its access says (a thread-local's none: the consumer
//   computes it where it is defined, from the thread pointer, #141);
// - a frame object's address, from the frame's base, which nothing moves between the prologue and the epilogues: rsp
//   where nothing else moves rsp either, and rbp (or a realigned frame's r15) where the dynamic stack does (#141);
// - a copy of one of them: a pointer's conversion, which the C driver's typed streams put around every address, their
//   pointers being 64-bit integers, has its operand's bits (TIER1_RULE_COPY);
// - a symbol's or a frame object's address plus a constant (a member's, or a constant index's: TIER1_RULE_ADDRESS without
//   an index), one lea at that displacement, a symbol's its reference's addend; a GOT slot's address takes no constant.
//   Selection's lea of a folded frame object (#3644), or from rip of a direct symbol (#2573), is one too.
// A frame object's displacement is the frame's layout's: until tier1_emit_cfg_frame fills it, the recipe's value is the
// object's slot, and its addend the constant added to it.
static void tier1_emit_cfg_recipes(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, uint8_t* operations, uint8_t* flags, uint64_t* values, int64_t* addends)
{
    const uint32_t count = plan->function->instruction_count;
    for (uint32_t node = 0; node < count; node += 1)
    {
        // The chain's root, through copies and constant displacements, each of them a value of its own (uncovered).
        uint32_t root = node;
        int64_t displacement = 0;
        bool walking = !plan->covered[node];
        for (uint32_t depth = 0; walking && depth < BUSTER_TIER1_EMIT_CFG_RECIPE_DEPTH; depth += 1)
        {
            const Tier1SelectPattern* pattern = &plan->patterns[root];
            const bool copy = pattern->rule == TIER1_RULE_COPY;
            const bool offset = pattern->rule == TIER1_RULE_ADDRESS && pattern->values[1] == BUSTER_IR_NONE;
            const uint32_t next = copy || offset ? pattern->values[0] : BUSTER_IR_NONE;
            walking = next < count && !plan->covered[next];
            displacement += walking && offset ? pattern->displacement : 0;
            root = walking ? next : root;
        }
        const Tier1SelectPattern* pattern = &plan->patterns[root];
        // No mask has a recipe (#179): a general register's copy can't recompute an opmask register's value, whose
        // recipe would need kxor, kxnor or kmov forms of its own. The masks' rules are none of those below, and neither
        // a mask nor a chain through one takes them.
        const bool mask = tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_MASK || tier1_emit_class(&emitted->prepared, root) == BUSTER_TIER1_ALLOCATE_MASK;
        const uint8_t rule = plan->covered[root] || mask ? TIER1_RULE_NONE : pattern->rule;
        // A folded symbol's lea (#2573): the symbol, at its own displacement past the chain's.
        const bool folded = rule == TIER1_RULE_ADDRESS && pattern->rip && pattern->stack_object != BUSTER_IR_NONE && pattern->values[1] == BUSTER_IR_NONE;
        const bool frame = rule == TIER1_RULE_ADDRESS && !pattern->rip && pattern->values[0] == BUSTER_IR_NONE && pattern->values[1] == BUSTER_IR_NONE &&
                           tier1_select_stack_object(plan->function, pattern->stack_object);
        const uint32_t symbol = folded ? pattern->stack_object : (uint32_t)pattern->immediate;
        const uint32_t object = frame ? pattern->stack_object : (uint32_t)pattern->immediate;
        const uint8_t access = (rule == TIER1_RULE_SYMBOL_ADDRESS || folded) && symbol < emitted->symbol_access_count ? emitted->symbol_access[symbol] : BUSTER_TIER1_ACCESS_DIRECT;
        displacement += folded || frame ? pattern->displacement : 0;
        const bool displaced = displacement != 0;
        const bool fits = displacement >= INT32_MIN && displacement <= INT32_MAX;
        uint64_t past = 0;
        if (rule == TIER1_RULE_CONSTANT && !displaced)
        {
            operations[node] = BUSTER_TIER1_REMAT_INTEGER;
            values[node] = pattern->immediate;
        }
        else if ((rule == TIER1_RULE_SYMBOL_ADDRESS || folded) && fits && (access == BUSTER_TIER1_ACCESS_DIRECT || (access == BUSTER_TIER1_ACCESS_GOT && !displaced && !folded)))
        {
            operations[node] = BUSTER_TIER1_REMAT_SYMBOL;
            values[node] = symbol;
            addends[node] = displacement;
            flags[node] = BUSTER_TIER1_REMAT_STABLE | (access == BUSTER_TIER1_ACCESS_GOT ? BUSTER_TIER1_REMAT_GOT : 0);
        }
        else if ((rule == TIER1_RULE_STACK_ADDRESS || frame) && fits && !tier1_emit_cfg_incoming_object(plan->function, object, &past))
        {
            operations[node] = BUSTER_TIER1_REMAT_STACK;
            values[node] = object;
            addends[node] = displacement;
            flags[node] = BUSTER_TIER1_REMAT_STABLE;
        }
    }
}

// The plan's allocation problem, in `output`, which the result keeps (see the header), with the maps between nodes and
// values and the prepared columns the shared helpers read: recipes and sizes by node, the calls' save rows, and the
// frame's columns (a node's slot, each caller-save row, then the temporary slot).
// The upper registers owned by a row's existing placement. A role may stay in an allocated operand's register, but
// every temporary fallback has a reserved home too. Floating roles follow the vector roles and share their span.
static uint32_t tier1_emit_cfg_row_reserved(const Tier1EmitPrepared* prepared, const Tier0Operation* row)
{
    const Tier0Reach reach = tier0_operation_reach(row);
    const bool upper_vectors = tier1_select_row_upper_vectors(row, tier1_emit_encoding(prepared), prepared->zmm);
    const bool upper_floats = !prepared->vex && !tier1_select_row_low_floating(row);
    uint32_t floating = tier0_class_floating(row->result_class);
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        floating += tier0_class_floating(row->operand_classes[j]);
    }
    const uint32_t vector_count = reach.vectors < 16 ? reach.vectors : 16;
    const uint32_t floating_end = reach.vectors + floating < 16 ? reach.vectors + floating : 16;
    const uint32_t vectors = upper_vectors ? (uint32_t)(((UINT64_C(1) << vector_count) - 1) << 16) : 0;
    const uint32_t floats = upper_floats ? (uint32_t)(((UINT64_C(1) << floating_end) - (UINT64_C(1) << vector_count)) << 16) : 0;
    return vectors | floats;
}

// Registered 64-byte aggregate parts wait in zmm16 and following registers before the call's parallel moves. Reserve
// the largest such span before allocating anything, including values used by a later argument of the same call.
static uint32_t tier1_emit_cfg_call_reserved(const IrFunction* function, const IrCall* call)
{
    uint32_t count = 0;
    for (uint32_t argument = 0; call->abi != 0 && argument < call->argument_count; argument += 1)
    {
        const IrPassed* record = tier1_select_argument_record(function, call, argument);
        Tier1SelectPart parts[2];
        const uint32_t part_count = tier1_select_aggregate_record(record) && !tier1_select_location_whole(record->location) ?
                                    tier1_select_location_parts(record->location, record->size, parts) : 0;
        for (uint32_t k = 0; k < part_count; k += 1)
        {
            count += parts[k].reg != BUSTER_ABI_REGISTER_NONE && tier1_select_part_in_zmm(parts[k].bytes);
        }
    }
    const uint32_t bounded = count < 16 ? count : 16;
    return (uint32_t)(((UINT64_C(1) << bounded) - 1) << BUSTER_TIER1_SELECT_PART_ZMM);
}

static Tier1EmitError tier1_emit_cfg_problem(const Tier1SelectPlan* plan, const IrCfg* verified_cfg, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    const Tier1LivenessError checked = tier1_liveness_validate_plan(plan, scratch, true);
    tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_LIVENESS, checked);
    Tier1EmitError error = checked == TIER1_LIVENESS_OK ? TIER1_EMIT_OK : checked == TIER1_LIVENESS_MEMORY ? TIER1_EMIT_MEMORY : checked == TIER1_LIVENESS_LIMIT ? TIER1_EMIT_LIMIT : checked == TIER1_LIVENESS_UNSUPPORTED ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
    error = error == TIER1_EMIT_OK && count == 0 ? TIER1_EMIT_INVALID : error;
    IrCfg cfg = { 0 };
    if (error == TIER1_EMIT_OK)
    {
        const IrError built = tier1_liveness_cfg(function, verified_cfg, output, scratch, &cfg);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_IR, built);
        error = built == IR_OK ? TIER1_EMIT_OK : built == IR_ERROR_MEMORY ? TIER1_EMIT_MEMORY : built == IR_ERROR_LIMIT ? TIER1_EMIT_LIMIT : TIER1_EMIT_INVALID;
    }
    const uint32_t added = error == TIER1_EMIT_OK && cfg.predecessor_offsets[1] != 0 ? 1 : 0;
    const uint32_t base = 2 * added;
    const uint32_t blocks = function->block_count + added;
    const uint32_t edges = cfg.edge_count + added;
    const uint64_t positions = (uint64_t)base + 2 * (uint64_t)count;
    error = error == TIER1_EMIT_OK && positions > BUSTER_TIER1_ALLOCATE_MAX_POSITIONS ? TIER1_EMIT_LIMIT : error;
    // Values, densely: every node with a result that no consumer covers.
    bool ok = error == TIER1_EMIT_OK;
    uint32_t* node_values = ok ? ir_allocate(output, count, 4, &ok) : 0;
    uint32_t* value_nodes = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    // Two per value: the operands whose registers its definition may take (#2685, Tier1CfgInput.ties), first as nodes;
    // and each node a return reads.
    uint32_t* ties = ok ? ir_allocate(output, 2 * (uint64_t)count + 2, 4, &ok) : 0;
    uint8_t* returned = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    if (ok)
    {
        memset(returned, 0, (uint64_t)count + 1);
    }
    uint32_t value_count = 0;
    uint32_t phi_count = 0;
    uint32_t operand_count = 0;
    uint32_t returns = 0;
    uint32_t symbols = 0;
    uint32_t fixed_saves = 0;
    uint32_t masks = 0;
    uint32_t floats = 0;
    Tier1EmitPrepared* prepared = &emitted->prepared;
    prepared->asm_result_heads = ok && function->asm_body_count != 0 ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    prepared->asm_result_next = ok && function->asm_body_count != 0 ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    if (ok && function->asm_body_count != 0)
    {
        memset(prepared->asm_result_heads, 0xff, ((uint64_t)count + 1) * 4);
        memset(prepared->asm_result_next, 0xff, ((uint64_t)count + 1) * 4);
    }
    // Each node's class: the opmask class's for a mask (#179), the vector class's for a floating value (#141), the
    // general class's for anything else. A long double (#141) is no value of the allocation's: its frame object holds it.
    uint8_t* node_classes = ok ? ir_allocate(output, (uint64_t)count + 1, 1, &ok) : 0;
    prepared->x87_ranks = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    prepared->x87_count = 0;
    prepared->vector_bytes = 0;
    prepared->vector_reserved = 0;
    prepared->function = function;
    // Whether the function has vectors (#141): one scan of it, by the requested path, so that a function without one pays no
    // node's vector checks.
    const bool vectors = ir_function_has_vectors(function);
    for (uint32_t node = 0; ok && node < count; node += 1)
    {
        if (function->op[node] == IR_ASM_RESULT && !plan->covered[node])
        {
            const uint32_t producer = function->a[node];
            prepared->asm_result_next[node] = prepared->asm_result_heads[producer];
            prepared->asm_result_heads[producer] = node;
        }
        const bool x87 = function->type[node] == IR_F80;
        prepared->x87_ranks[node] = x87 ? prepared->x87_count : UINT32_MAX;
        prepared->x87_count += x87;
        const bool value = plan->covered[node] == 0 && function->type[node] != IR_VOID && !x87;
        const bool mask = tier1_select_mask_width(function->type[node]) != 0;
        // A vector (#141) is the vector class's, as a floating value is, at its width; none in a function without vectors.
        const uint32_t vector_width = vectors ? tier1_select_vector_width(function->type[node]) : 0;
        const uint32_t vector_bytes = value ? vector_width : 0;
        const bool floating = tier1_select_float_width(function->type[node]) != 0 || vector_width != 0;
        prepared->vector_bytes = vector_bytes > prepared->vector_bytes ? vector_bytes : prepared->vector_bytes;
        node_classes[node] = mask ? BUSTER_TIER1_ALLOCATE_MASK : floating ? BUSTER_TIER1_ALLOCATE_VECTOR : BUSTER_TIER1_ALLOCATE_GPR;
        masks += mask && value;
        floats += floating && value;
        node_values[node] = value ? value_count : BUSTER_TIER1_CFG_NONE;
        value_nodes[value_count] = node;
        // The operands whose registers a two-address result may take (#2685, tier1_select_ties), as nodes, and the node a
        // return reads (tier1_select_returned's rule), from the pattern this pass reads anyway: the values' pass maps them.
        uint32_t first = BUSTER_IR_NONE;
        uint32_t second = BUSTER_IR_NONE;
        if (value)
        {
            tier1_select_ties(plan, node, &first, &second);
        }
        ties[2 * (uint64_t)value_count] = first;
        ties[2 * (uint64_t)value_count + 1] = second;
        const uint32_t read = plan->covered[node] == 0 && plan->patterns[node].rule == TIER1_RULE_RETURN ? plan->patterns[node].values[0] : BUSTER_IR_NONE;
        returned[read < count ? read : count] = read < count;
        value_count += value;
        phi_count += function->op[node] == IR_PHI && !x87;
        operand_count += function->op[node] == IR_PHI && !x87 ? function->b[node] : 0;
        returns += function->op[node] == IR_RET;
        // A symbol's address's reference, and a memory operand's that folds one (#2573).
        symbols += function->op[node] == IR_SYMBOL_ADDR || (plan->covered[node] == 0 && plan->patterns[node].rip && plan->patterns[node].stack_object != BUSTER_IR_NONE);
        // A division's rax and rdx, and a shift's rcx, may need call-save rows for the values it doesn't take.
        // An asm body's general registers too (#1839, tier1_select_node_fixed), callee-saved ones among them, which the
        // prologue saves (Tier1EmitPrepared.asm_callee).
        const uint32_t fixed = plan->covered[node] ? 0 : tier1_select_node_fixed(function, &plan->patterns[node]);
        const bool body = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM;
        fixed_saves |= fixed;
        emitted->fixed_count += fixed != 0;
        prepared->asm_count += body;
        prepared->asm_callee |= body ? fixed & BUSTER_ABI_CALLEE_SAVED : 0;
        prepared->asm_opaque |= body && function->asm_bodies[plan->patterns[node].immediate].opaque;
        // The asm operand contract names only the low vector registers. Keep its unmodelled upper clobbers outside
        // allocation until that separate contract describes all 32 registers.
        prepared->vector_reserved |= body ? 0xffff0000u : 0;
        if (!plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ROW)
        {
            const uint32_t row = (uint32_t)plan->patterns[node].immediate;
            prepared->vector_reserved |= row < function->row_count ? tier1_emit_cfg_row_reserved(prepared, &function->rows[row]) : 0xffff0000u;
        }
        if (tier1_select_function_call(function, node))
        {
            prepared->call_count += 1;
            // The argument registers of each class the calls take, and the outgoing area: the most stack slots a call's
            // arguments past their classes' registers take (#141).
            const IrCall* call = &function->calls[function->b[node]];
            prepared->vector_reserved |= tier1_emit_cfg_call_reserved(function, call);
            Tier1SelectPlaces places = { 0 };
            for (uint32_t argument = 0; call->abi == 0 && argument < call->argument_count; argument += 1)
            {
                tier1_select_next_place(&places, tier1_select_argument_type(function, call, argument));
            }
            // A call with ABI records (#789): its records' totals, and the alignment its outgoing area needs at rsp, past
            // 16 bytes for an argument on the stack aligned past them (a 32- or 64-byte vector, an over-aligned aggregate).
            // The frame takes the most of them (Tier1FrameRequest.calls): it realigns itself to it, and a dynamic frame
            // rounds its allocations and calls' areas to it (tier1_emit_cfg_stack_alignment; #1204's review, #1450).
            const AbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1].call : 0;
            const uint32_t general = abi != 0 ? abi->integer_registers : places.general;
            const uint32_t vectors = abi != 0 ? abi->vector_registers : places.vector;
            const uint32_t stacked = tier1_select_call_stack_bytes(function, call);
            const uint32_t staged = tier1_select_call_staging_slots(function, call);
            const uint8_t aligned = abi != 0 ? abi->stack_alignment_log2 : BUSTER_ABI_STACK_ALIGNMENT_LOG2;
            prepared->call_abi.integer_registers = prepared->call_abi.integer_registers > general ? prepared->call_abi.integer_registers : (uint8_t)general;
            prepared->call_abi.vector_registers = prepared->call_abi.vector_registers > vectors ? prepared->call_abi.vector_registers : (uint8_t)vectors;
            prepared->call_abi.stack_bytes = prepared->call_abi.stack_bytes > stacked ? prepared->call_abi.stack_bytes : stacked;
            prepared->call_abi.stack_alignment_log2 = prepared->call_abi.stack_alignment_log2 > aligned ? prepared->call_abi.stack_alignment_log2 : aligned;
            prepared->staging_slots = staged > prepared->staging_slots ? staged : prepared->staging_slots;
        }
    }
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    // Only calls that overflow the red zone add storage. The last frame object then holds every call's staged parts,
    // before and after register moves, with a lifetime covering the function so it aliases no spill or local object.
    prepared->staging_slots = prepared->staging_slots > BUSTER_TIER1_SELECT_STAGING_SLOTS ? prepared->staging_slots : 0;
    // A reference for each call, each symbol's address and each memory operand's folded symbol, and how the symbols are
    // reached.
    emitted->reference_capacity = prepared->call_count + symbols;
    emitted->symbol_access = options->symbol_access;
    emitted->symbol_access_count = options->symbol_access != 0 ? options->symbol_count : 0;
    prepared->call_abi.stack_alignment_log2 = prepared->call_abi.stack_alignment_log2 > BUSTER_ABI_STACK_ALIGNMENT_LOG2 ? prepared->call_abi.stack_alignment_log2 : BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    prepared->call_abi.integer_registers = prepared->call_abi.integer_registers < BUSTER_ABI_INTEGER_ARGUMENTS ? prepared->call_abi.integer_registers : BUSTER_ABI_INTEGER_ARGUMENTS;
    // The general registers' call-save columns: every caller-saved register where the function calls, and the fixed
    // registers' sequences'. Their rows, after allocation, are those of them a call or a sequence saves (tier1_emit_cfg_saves;
    // #177): until then, every column's.
    // An asm body's callee-saved registers (#1839) hold values only in a function that calls (tier1_emit_cfg_preference).
    const uint32_t held = options->register_mask | (prepared->call_count != 0 ? BUSTER_TIER1_EMIT_CFG_CALLEE : 0);
    prepared->call_save_columns = (prepared->call_count != 0 ? options->register_mask : 0) | (fixed_saves & held);
    prepared->call_save_mask = prepared->call_save_columns;
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_columns);
    // The slots' columns: a value's at its node, the general registers' caller-save rows', the opmask registers' (one
    // per register, whichever the calls save: tier1_emit_cfg_saves), the xmm registers' (#141), the three classes'
    // temporary slots', then each frame object's, live throughout until its lifetime markers say otherwise
    // (tier1_emit_cfg_lifetimes): the IR's slots, and past them the records' objects (tier1_emit_cfg_object_count), which
    // no marker names.
    const uint32_t objects = error == TIER1_EMIT_OK ? tier1_emit_cfg_object_count(function, prepared) : 0;
    prepared->classes = node_classes;
    prepared->mask_columns = masks != 0 ? 8 : 0;
    const bool upper_transport = prepared->vector_bytes != 0 && prepared->zmm && (!prepared->vex || prepared->vector_bytes == 64);
    prepared->vector_columns = floats != 0 ? (upper_transport && (prepared->vector_reserved & 0xffff0000u) != 0xffff0000u ? 32 : 16) : 0;
    const uint32_t mask_columns = prepared->mask_columns;
    const uint32_t fixed_columns = call_slots + mask_columns + prepared->vector_columns + BUSTER_TIER1_ALLOCATE_CLASSES;
    prepared->slot_columns = count + fixed_columns + objects;
    ok = error == TIER1_EMIT_OK;
    prepared->starts = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->ends = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->sizes = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    prepared->alignments = ok ? ir_allocate(output, prepared->slot_columns, 4, &ok) : 0;
    uint8_t* operations = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* widths = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* flags = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint8_t* marks = ok ? ir_allocate(output, count, 1, &ok) : 0;
    uint64_t* constants = ok ? ir_allocate(output, count, 8, &ok) : 0;
    int64_t* addends = ok ? ir_allocate(output, count, 8, &ok) : 0;
    uint16_t* costs = ok ? ir_allocate(output, count, 2, &ok) : 0;
    uint16_t* reload_costs = ok ? ir_allocate(output, count, 2, &ok) : 0;
    uint32_t* prospective = ok ? ir_allocate(scratch, (uint64_t)value_count + fixed_columns + objects, 4, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t column = 0; column < prepared->slot_columns; column += 1)
        {
            prepared->starts[column] = 0;
            prepared->ends[column] = (uint32_t)positions;
            prepared->sizes[column] = 8;
            prepared->alignments[column] = 8;
        }
        for (uint32_t o = 0; o < objects; o += 1)
        {
            // A variadic definition's anchors (#789) take a row of one byte, which nothing reads: the register save area is
            // the frame's own, and the overflow area is in the caller's frame. Past the IR's slots, the records' objects
            // (#789), aligned to 8 where they hold anything.
            const IrSlot* slot = o < function->slot_count ? &function->slots[o] : 0;
            const bool anchor = slot != 0 && (slot->flags & (BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) != 0;
            const uint32_t record_bytes = slot == 0 ? tier1_emit_cfg_record_object_bytes(function, prepared, o) : 0;
            prepared->sizes[count + fixed_columns + o] = slot == 0 ? record_bytes : !anchor && slot->size != 0 ? (uint32_t)slot->size : 1;
            prepared->alignments[count + fixed_columns + o] = slot == 0 ? (record_bytes == 16 && o >= tier1_emit_cfg_record_count(function) ? 16 : record_bytes > 1 ? 8 : 1) :
                                                                          !anchor && slot->alignment != 0 ? slot->alignment : 1;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            // A mask's slot is its bytes, which kmov of its width writes and reads, a floating value's its bytes, movss's or
            // movsd's (#141), and a vector's its bytes, vmovdqu32's (#141), aligned to 16 at most: its accesses are
            // unaligned ones, and an alignment past the incoming stack's would realign the frame.
            const uint32_t mask_width = tier1_select_mask_width(function->type[node]);
            const uint32_t float_width = tier1_select_float_width(function->type[node]);
            const uint32_t vector_width = vectors ? tier1_select_vector_width(function->type[node]) : 0;
            widths[node] = (uint8_t)tier1_select_storage_width(plan, node);
            prepared->sizes[node] = widths[node] != 0 ? widths[node] : mask_width != 0 ? mask_width : float_width != 0 ? float_width : vector_width != 0 ? vector_width : 8;
            prepared->alignments[node] = prepared->sizes[node] < 16 ? prepared->sizes[node] : 16;
        }
        // The vector class's call-save rows and its temporary slot hold a whole register of the widest vector (#141).
        const uint32_t vector_save_bytes = prepared->vector_bytes != 0 ? prepared->vector_bytes : 8;
        for (uint32_t column = count + call_slots + mask_columns; column < count + call_slots + mask_columns + prepared->vector_columns; column += 1)
        {
            prepared->sizes[column] = vector_save_bytes;
            prepared->alignments[column] = vector_save_bytes < 16 ? vector_save_bytes : 16;
        }
        prepared->sizes[count + call_slots + mask_columns + prepared->vector_columns + BUSTER_TIER1_ALLOCATE_VECTOR] = vector_save_bytes;
        prepared->alignments[count + call_slots + mask_columns + prepared->vector_columns + BUSTER_TIER1_ALLOCATE_VECTOR] = vector_save_bytes < 16 ? vector_save_bytes : 16;
        if (options->rematerialize)
        {
            tier1_emit_cfg_recipes(plan, emitted, operations, flags, constants, addends);
        }
        prepared->recipes = (Tier1RematerializationInput){
            .operations = operations,
            .widths = widths,
            .flags = flags,
            .values = constants,
            .addends = addends,
            .count = count,
        };
        prepared->recipe_values = constants;
        // The model's costs, where it has them, before the marks read them: the frame's base is rsp.
        const bool costed = tier1_rematerialization_model_costs(&prepared->recipes, X86_REGISTER_RSP, costs, reload_costs);
        prepared->recipes.costs = costs;
        prepared->recipes.reload_costs = reload_costs;
        const bool marked = costed && tier1_rematerialization_marks(&prepared->recipes, marks);
        error = marked ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    }
    // Frame-base reservations before allocation, from every value as a prospective slot, the caller-save rows and the
    // temporary slot: the actual set, a subset of it, can't add one.
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            prospective[v] = value_nodes[v];
        }
        for (uint32_t slot = 0; slot < fixed_columns + objects; slot += 1)
        {
            prospective[value_count + slot] = count + slot;
        }
        const Tier1FrameRequest request = {
            .slots = {
                .start = prepared->starts,
                .end = prepared->ends,
                .size = prepared->sizes,
                .alignment = prepared->alignments,
                .indices = prospective,
                .count = value_count + fixed_columns + objects,
                .column_count = prepared->slot_columns,
            },
            .calls = &prepared->call_abi,
            .call_count = prepared->call_count + prepared->asm_count != 0,
            .flags = tier1_emit_cfg_frame_flags(plan->function, options, prepared->call_count + prepared->asm_count != 0),
        };
        Tier1FrameRequirements requirements = { 0 };
        const Tier1FrameError framed = tier1_frame_requirements(&request, &requirements);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_FRAME, framed);
        error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : framed == TIER1_FRAME_OVERFLOW ? TIER1_EMIT_LIMIT : TIER1_EMIT_FRAME;
        prepared->reserved_registers = requirements.reserved_registers | (1u << BUSTER_TIER1_SELECT_SCRATCH);
    }
    const uint32_t registers = options->register_mask & ~prepared->reserved_registers;
    // The xmm registers the floating values take (#141): every one but the class's scratch, or the options' of them.
    const uint32_t vector_candidates = options->vector_register_mask != 0 ? options->vector_register_mask & BUSTER_TIER1_EMIT_VECTOR_REGISTERS : BUSTER_TIER1_EMIT_VECTOR_REGISTERS;
    const uint32_t vector_registers = vector_candidates & ~prepared->vector_reserved & (upper_transport ? UINT32_MAX : BUSTER_TIER1_EMIT_LOW_VECTOR_REGISTERS);
    // The callee-saved registers values live across a call may take, but those the frame reserves (tier1_emit_cfg_preference):
    // rbp where it is the frame pointer, r15 where it is a realigned dynamic frame's base.
    // And values live across an asm body (#2144), which may take them to keep out of the registers it touches.
    emitted->callee_registers = prepared->call_count != 0 || prepared->asm_count != 0 ? BUSTER_TIER1_EMIT_CFG_CALLEE & ~prepared->reserved_registers : 0;
    // A leaf's, where its caller-saved registers alone spill (#2819, tier1_emit_cfg_leaf).
    emitted->leaf_registers = prepared->call_count == 0 && prepared->asm_count == 0 ? BUSTER_TIER1_EMIT_CFG_CALLEE & ~prepared->reserved_registers : 0;
    ok = error == TIER1_EMIT_OK;
    uint32_t* block_starts = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint32_t* predecessor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* predecessors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint8_t* classes = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint8_t* slot_widths = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* allowed = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* defined = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* definitions = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint8_t* rematerializable = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* use_offsets = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    const uint64_t use_capacity = (uint64_t)plan->effective_use_count + value_count + 1;
    uint32_t* use_positions = ok ? ir_allocate(output, use_capacity, 4, &ok) : 0;
    uint8_t* use_required = ok ? ir_allocate(output, use_capacity, 1, &ok) : 0;
    // The register each use reads its value in, where a call's register argument names one (#2646).
    uint8_t* use_registers = ok ? ir_allocate(output, use_capacity, 1, &ok) : 0;
    uint32_t* use_cursors = ok ? ir_allocate(scratch, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* phi_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* phi_values = ok ? ir_allocate(output, (uint64_t)phi_count + 1, 4, &ok) : 0;
    uint32_t* phi_operand_offsets = ok ? ir_allocate(output, (uint64_t)phi_count + 1, 4, &ok) : 0;
    uint32_t* phi_operands = ok ? ir_allocate(output, (uint64_t)operand_count + 1, 4, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        // Blocks and edges, the added entry's first: each IR block one later, its successors in the IR's order (a
        // conditional branch's true successor first) and its predecessors in block order.
        block_starts[0] = 0;
        successor_offsets[0] = 0;
        predecessor_offsets[0] = 0;
        uint32_t edge = 0;
        if (added != 0)
        {
            successors[0] = 1;
            edge = 1;
        }
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            block_starts[b + added] = base + 2 * function->block_offsets[b];
            successor_offsets[b + added] = edge;
            for (uint32_t e = cfg.successor_offsets[b]; e < cfg.successor_offsets[b + 1]; e += 1)
            {
                successors[edge] = cfg.successors[e] + added;
                edge += 1;
            }
        }
        block_starts[blocks] = (uint32_t)positions;
        successor_offsets[blocks] = edge;
        edge = 0;
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            predecessor_offsets[b + added] = edge;
            if (added != 0 && b == 0)
            {
                predecessors[edge] = 0;
                edge += 1;
            }
            for (uint32_t e = cfg.predecessor_offsets[b]; e < cfg.predecessor_offsets[b + 1]; e += 1)
            {
                predecessors[edge] = cfg.predecessors[e] + added;
                edge += 1;
            }
        }
        predecessor_offsets[blocks] = edge;
        // Values: a parameter in its incoming register from position 0 (its definition's interval alone: elsewhere it may
        // be in any register), a phi at its block's start, any other at its node's definition phase.
        use_offsets[0] = 0;
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            const uint32_t node = value_nodes[v];
            const uint32_t op = function->op[node];
            classes[v] = node_classes[node];
            // A slot's bytes, the frame row's (tier1_emit_cfg_row_bytes): a phi web shares a slot only at one width.
            slot_widths[v] = (uint8_t)prepared->sizes[node];
            const bool vector_value = classes[v] == BUSTER_TIER1_ALLOCATE_VECTOR;
            const uint32_t value_vectors = tier1_select_vector_width(function->type[node]) != 0 ? vector_registers : vector_registers & BUSTER_TIER1_EMIT_LOW_VECTOR_REGISTERS;
            allowed[v] = classes[v] == BUSTER_TIER1_ALLOCATE_MASK ? BUSTER_TIER1_EMIT_MASK_REGISTERS : vector_value ? value_vectors : registers;
            // A parameter's incoming register, its place's (#141): a general one, or an xmm one for a floating value.
            // A by-value aggregate parameter (#789) is its copy's address, defined at its node as a frame object's is.
            const bool parameter = tier1_select_entry_parameter(function, node);
            defined[v] = parameter ? tier1_liveness_parameter_mask(function, function->a[node]) & (vector_value ? vector_registers : registers) : 0;
            const uint32_t producer = op == IR_ASM_RESULT ? function->a[node] : node;
            definitions[v] = parameter ? 0 : op == IR_PHI ? block_starts[cfg.instruction_blocks[node] + added] : base + 2 * producer + 1;
            // A mask and a floating value are never recomputed (their classes have no recipe, tier1_emit_cfg_recipes):
            // neither in a fragment's or an edge's place, nor left out as a dead definition (tier1_emit_cfg_dead_definition
            // reads this column).
            rematerializable[v] = classes[v] != BUSTER_TIER1_ALLOCATE_GPR ? 0 : marks[node];
            use_offsets[v + 1] = op != IR_PHI;
            // The ties as values of v's class; a returned value takes none (tier1_select_returned).
            for (uint32_t k = 0; k < 2; k += 1)
            {
                const uint32_t tied = ties[2 * (uint64_t)v + k];
                const uint32_t tied_value = tied < count && returned[node] == 0 ? node_values[tied] : BUSTER_TIER1_CFG_NONE;
                ties[2 * (uint64_t)v + k] = tied_value != BUSTER_TIER1_CFG_NONE && node_classes[tied] == classes[v] ? tied_value : BUSTER_TIER1_CFG_NONE;
            }
        }
        // Uses by value, counted then filled in node order: a node's effective uses, then its definition's own use.
        // A parameter's, at position 0, precedes every other use of it. So does an asm projection's, at its producer's
        // definition phase: its consumers follow the projection, leaving each value's uses in position order.
        // A long double argument (#141) is no value's use: its call copies it from its object.
        for (uint32_t node = 0; node < count; node += 1)
        {
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t v = node_values[plan->effective_use_values[use]];
                use_offsets[v + 1] += v != BUSTER_TIER1_CFG_NONE;
            }
        }
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            use_offsets[v + 1] += use_offsets[v];
            use_cursors[v] = use_offsets[v];
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            // A parameter past its class's registers, and a call's argument past them (#141), may be in a slot there: the
            // entry loads the one from its incoming slot wherever its first fragment is (tier1_emit_cfg_entry), and the
            // call stores the other into the outgoing area from wherever it is (tier1_emit_cfg_stack_argument).
            const uint32_t defined = node_values[node];
            // A row call's (#141) arguments are each in a register, as any operand's.
            const bool call = tier1_select_function_call(function, node);
            const IrCall* called = call ? &function->calls[function->b[node]] : 0;
            const uint32_t arguments = call ? called->argument_count : 0;
            if (defined != BUSTER_TIER1_CFG_NONE && tier1_select_entry_parameter(function, node))
            {
                use_positions[use_cursors[defined]] = 0;
                use_required[use_cursors[defined]] = tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_STACK;
                use_registers[use_cursors[defined]] = UINT8_MAX;
                use_cursors[defined] += 1;
            }
            Tier1SelectPlaces places = { 0 };
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t v = node_values[plan->effective_use_values[use]];
                const uint32_t argument = use - plan->effective_use_offsets[node];
                // A by-value aggregate argument's address (#789) isn't required either: its call copies its bytes from it
                // wherever it is (tier1_emit_cfg_aggregate_copy).
                const Tier1SelectPlace place = argument < arguments ? tier1_select_argument_place(function, called, argument, &places) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
                const uint8_t kind = place.kind;
                const bool stacked = kind == BUSTER_TIER1_PLACE_STACK || kind == BUSTER_TIER1_PLACE_AGGREGATE;
                // A register argument's register (tier1_emit_call's destinations), an indirect call's callee's, and a
                // destination that is a pointer value's rdi (#2820), the use after the callee's.
                const bool indirect = call && function->op[node] == IR_CALL_INDIRECT;
                const bool callee = indirect && argument == arguments;
                const bool destination = call && argument == arguments + (indirect ? 1u : 0u) && tier1_select_call_destination(function, node) != BUSTER_IR_NONE;
                const uint8_t reg = argument < arguments && kind == BUSTER_TIER1_PLACE_GENERAL ? tier1_select_parameter_registers[place.index] :
                                    argument < arguments && kind == BUSTER_TIER1_PLACE_VECTOR ? place.index :
                                    callee ? tier1_select_callee_register(function, called) : destination ? X86_REGISTER_RDI : UINT8_MAX;
                if (v != BUSTER_TIER1_CFG_NONE)
                {
                    use_positions[use_cursors[v]] = base + 2 * node;
                    use_required[use_cursors[v]] = !stacked;
                    use_registers[use_cursors[v]] = reg;
                    use_cursors[v] += 1;
                }
            }
            // A definition's own use requires its register, but a recipe's (#2618): where values read sooner hold every
            // register, the allocator gives the recipe none, the cheapest choice, which leaves the definition out
            // (tier1_emit_cfg_dead_definition), where it found none and failed the assignment, whose second try, with
            // every definition freed, spilled. An asm snapshot can start in a slot too: its fixed output register is
            // already available for the consumer to store, without any allocation register of its own.
            if (defined != BUSTER_TIER1_CFG_NONE && !tier1_select_entry_parameter(function, node) && function->op[node] != IR_PHI)
            {
                const bool snapshot = plan->patterns[node].rule == TIER1_RULE_ASM || function->op[node] == IR_ASM_RESULT;
                const uint32_t producer = function->op[node] == IR_ASM_RESULT ? function->a[node] : node;
                const uint32_t reg = function->op[node] == IR_ASM_RESULT ? function->b[node] :
                                     snapshot ? function->asm_bodies[function->calls[function->b[node]].body - 1].result - 1u : UINT32_MAX;
                use_positions[use_cursors[defined]] = base + 2 * producer + 1;
                // An asm result already exists in its fixed register; it may be copied straight to its spill slot.
                use_required[use_cursors[defined]] = !snapshot && rematerializable[defined] == 0;
                use_registers[use_cursors[defined]] = snapshot ? (uint8_t)(reg & 15) : UINT8_MAX;
                use_cursors[defined] += 1;
            }
        }
        // Phis, at their blocks' starts, each operand in its block's predecessors' order: the IR's pairs name each
        // predecessor once. The IR's entry has no phi, having no predecessor; with an added entry before it, its phis
        // would need an operand for the function's entry, which the IR has none of.
        uint32_t phi = 0;
        uint32_t operand = 0;
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
        {
            phi_offsets[b] = phi;
            const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
            const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
            bool leading = true;
            for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
            {
                const bool is_phi = function->op[node] == IR_PHI;
                error = !is_phi || (leading && b != added) ? error : added != 0 ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
                leading = leading && is_phi;
                // A long double's phi (#141) is no value's: its edges copy its operand's object into its own
                // (tier1_emit_cfg_x87_phis).
                if (error == TIER1_EMIT_OK && is_phi && function->type[node] != IR_F80)
                {
                    phi_values[phi] = node_values[node];
                    phi_operand_offsets[phi] = operand;
                    for (uint32_t e = predecessor_offsets[b]; error == TIER1_EMIT_OK && e < predecessor_offsets[b + 1]; e += 1)
                    {
                        const uint32_t source = predecessors[e] - added;
                        uint32_t incoming = BUSTER_IR_NONE;
                        // The last pair from `source`. Under MSan the scan stays scalar: clang's vector form (znver5's,
                        // 16 pairs a step) gathers the matching pairs' values into a vector whose other lanes are
                        // undefined, and MSan checks that whole vector where it takes the last matching lane, so it
                        // reports lanes the scan never reads (tier1_cfg_ir.c's scan has the same pragma). Release
                        // builds keep the vector form.
#if __has_feature(memory_sanitizer)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
                        for (uint32_t pair = 0; pair < function->b[node]; pair += 1)
                        {
                            const uint32_t* entry = function->extra + function->a[node] + (uint64_t)pair * 2;
                            incoming = entry[0] == source ? entry[1] : incoming;
                        }
                        // An undef operand is no value: the edge moves nothing for it (tier1_cfg.c), and the plan
                        // computes no undef for it (tier1_select_plan).
                        const bool undefined = incoming < count && function->op[incoming] == IR_UNDEF;
                        const uint32_t v = undefined ? BUSTER_TIER1_CFG_NONE : incoming < count ? node_values[incoming] : BUSTER_TIER1_CFG_NONE;
                        error = v != BUSTER_TIER1_CFG_NONE || undefined ? error : TIER1_EMIT_INVALID;
                        phi_operands[operand] = v;
                        operand += 1;
                    }
                    phi += 1;
                }
            }
        }
        phi_offsets[blocks] = phi;
        phi_operand_offsets[phi] = operand;
        // The edges along which a long double phi (#141) takes a value, which tier1_emit_cfg_x87_phis copies: placed as
        // edges with moves.
        uint8_t* edge_copies = prepared->x87_count != 0 ? ir_allocate(output, (uint64_t)edges + 1, 1, &ok) : 0;
        // The fixed registers' sequences (#2767): each node whose rule takes general registers it may write over values
        // (tier1_select_fixed_registers, which tier1_emit_cfg_fixed saves values around), at its definition phase, those of
        // them the values may take, the value it reads there and leaves (tier1_select_fixed_reader: a shift's count), and
        // the operand and own value its records would move out of them (tier1_select_fixed_operand, _result). A range live
        // across one avoids them, but its reader's, and so do those two.
        // Room for every node with fixed registers (emitted->fixed_count, its asm bodies' too), filled in one pass, which
        // a function without one, most, doesn't take.
        const uint32_t fixed_nodes = emitted->fixed_count;
        uint32_t* sequences = fixed_nodes != 0 ? ir_allocate(output, fixed_nodes, 4, &ok) : 0;
        uint32_t* sequence_registers = fixed_nodes != 0 ? ir_allocate(output, fixed_nodes, 4, &ok) : 0;
        uint32_t* sequence_readers = fixed_nodes != 0 ? ir_allocate(output, fixed_nodes, 4, &ok) : 0;
        uint32_t* sequence_operands = fixed_nodes != 0 ? ir_allocate(output, fixed_nodes, 4, &ok) : 0;
        uint32_t* sequence_results = fixed_nodes != 0 ? ir_allocate(output, fixed_nodes, 4, &ok) : 0;
        uint32_t sequence_count = 0;
        for (uint32_t node = 0; ok && sequence_results != 0 && node < count; node += 1)
        {
            const bool selected = !plan->covered[node] && function->op[node] != IR_PHI && !tier1_select_entry_parameter(function, node);
            const uint32_t written = selected ? tier1_select_fixed_registers(plan->patterns[node].rule) & registers : 0;
            if (written != 0)
            {
                const uint32_t reader = tier1_select_fixed_reader(&plan->patterns[node]);
                const uint32_t operand = tier1_select_fixed_operand(&plan->patterns[node]);
                sequences[sequence_count] = base + 2 * node + 1;
                sequence_registers[sequence_count] = written;
                sequence_readers[sequence_count] = reader < count ? node_values[reader] : BUSTER_TIER1_CFG_NONE;
                sequence_operands[sequence_count] = operand < count ? node_values[operand] : BUSTER_TIER1_CFG_NONE;
                sequence_results[sequence_count] = tier1_select_fixed_result(&plan->patterns[node]) ? node_values[node] : BUSTER_TIER1_CFG_NONE;
                sequence_count += 1;
            }
        }
        error = ok ? error : TIER1_EMIT_MEMORY;
        for (uint32_t b = added; error == TIER1_EMIT_OK && edge_copies != 0 && b < blocks; b += 1)
        {
            for (uint32_t e = successor_offsets[b]; e < successor_offsets[b + 1]; e += 1)
            {
                const uint32_t target = successors[e] - added;
                for (uint32_t node = function->block_offsets[target]; node < function->block_offsets[target + 1] && function->op[node] == IR_PHI; node += 1)
                {
                    for (uint32_t pair = 0; function->type[node] == IR_F80 && pair < function->b[node]; pair += 1)
                    {
                        const uint32_t* entry = function->extra + function->a[node] + (uint64_t)pair * 2;
                        edge_copies[e] |= entry[0] == b - added && entry[1] < count && function->op[entry[1]] != IR_UNDEF;
                    }
                }
            }
        }
        emitted->cfg_input = (Tier1CfgInput){
            .edge_copies = edge_copies,
            .block_starts = block_starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .classes = classes,
            .slot_widths = slot_widths,
            .allowed_registers = allowed,
            .definition_registers = defined,
            .definitions = definitions,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .use_registers = use_registers,
            .phi_offsets = phi_offsets,
            .phi_values = phi_values,
            .phi_operand_offsets = phi_operand_offsets,
            .phi_operands = phi_operands,
            .ties = ties,
            .sequences = sequences,
            .sequence_registers = sequence_registers,
            .sequence_readers = sequence_readers,
            .sequence_operands = sequence_operands,
            .sequence_results = sequence_results,
            .sequence_count = sequence_count,
            // The IR's tree, where the driver gives it (#2611): the machine CFG's blocks from `added` on are the IR's.
            .dominators = options->dominators,
            .dominator_shift = added,
            .block_count = blocks,
            .edge_count = edges,
            .value_count = value_count,
            .use_count = use_offsets[value_count],
            .phi_count = phi,
            .position_count = (uint32_t)positions,
            .registers = {
                registers,
                floats != 0 ? vector_registers : 0,
                masks != 0 ? BUSTER_TIER1_EMIT_MASK_REGISTERS : 0,
            },
            // xmm15, the vector class's scratch (#141), and k0, the opmask class's: it can't be a write mask.
            .scratch_registers = {
                BUSTER_TIER1_SELECT_SCRATCH,
                floats != 0 ? BUSTER_TIER1_SELECT_FLOAT_SCRATCH : 0,
                0,
            },
        };
        emitted->value_nodes = value_nodes;
        emitted->node_values = node_values;
        // The records' capacity counts the epilogues, one per return: tier1_emit_cfg_records reads it here.
        emitted->jumps = returns;
    }
    return error;
}

// Whether `node` is a machine row's that takes opmask registers of its own (#141, tier1_emit_cfg_row): a mask result or
// mask operands, mask scratch roles, or registers it names by number, which may need more free ones than the node has.
static bool tier1_emit_cfg_row_masks(const Tier1SelectPlan* plan, uint32_t node)
{
    const IrFunction* function = plan->function;
    const uint32_t index = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ROW ? (uint32_t)plan->patterns[node].immediate : UINT32_MAX;
    const Tier0Masks masks = index < function->row_count ? tier0_operation_masks(&function->rows[index]) : (Tier0Masks){ 0 };
    return masks.operands != 0 || masks.result || masks.scratch != 0 || masks.named != 0;
}

// The opmask registers an asm body's records write (#1839, IrAsmBody.masks), for node `node` of TIER1_RULE_ASM: 0 for
// another node, or a body that names none.
static uint32_t tier1_emit_cfg_asm_masks(const Tier1SelectPlan* plan, uint32_t node)
{
    const IrFunction* function = plan->function;
    const uint32_t index = (uint32_t)plan->patterns[node].immediate;
    return plan->patterns[node].rule == TIER1_RULE_ASM && index < function->asm_body_count ? function->asm_bodies[index].masks : 0;
}

// The registers some call saves, by the allocation, which the frame gives rows: each register fragment that covers a
// call's definition phase, where tier1_emit_cfg_records saves the values that live past the call, but the call's own
// result's. The general registers' (#177): a caller-saved one's, which tier1_emit_call stores and loads, and a fixed
// register's whose fragment covers the definition phase of a sequence that takes it (tier1_emit_cfg_fixed), each of the
// columns the frame's reservations counted; the rest of the columns take no row, where every caller-saved register's did,
// in every function that calls. The opmask registers whose masks some call saves (#179), and the xmm registers whose
// floating values some call saves (#141). The calls in position order, and each fixed register's sequences', then each
// fragment's first at or past its start, by halving. And the opmask registers whose masks a machine row of opmask
// registers (#141, tier1_emit_cfg_row) may borrow where too few are free: those whose fragments cover such a row's
// definition phase, the masks' alone (a row borrows no other class's), in a list of their own.
static Tier1EmitError tier1_emit_cfg_saves(const Tier1SelectPlan* plan, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t base = 2 * (emitted->cfg_input.block_count - function->block_count);
    const uint32_t count = function->instruction_count;
    const uint32_t columns = emitted->prepared.call_save_columns;
    const uint64_t mark = scratch->length;
    Tier1EmitSavePositions saves;
    bool ok = tier1_emit_save_positions(plan, columns, base, emitted->prepared.call_count, emitted->fixed_count, scratch, &saves);
    const uint32_t* calls = saves.positions;
    const uint32_t call_count = saves.call_count;
    uint32_t row_count = 0;
    for (uint32_t node = 0; ok && emitted->prepared.mask_columns != 0 && node < count; node += 1)
    {
        row_count += tier1_emit_cfg_row_masks(plan, node) ? 1 : 0;
    }
    uint32_t* rows = ok && row_count != 0 ? ir_allocate(scratch, (uint64_t)row_count + 1, 4, &ok) : 0;
    row_count = 0;
    for (uint32_t node = 0; ok && rows != 0 && node < count; node += 1)
    {
        rows[row_count] = base + 2 * node + 1;
        row_count += tier1_emit_cfg_row_masks(plan, node) ? 1 : 0;
    }
    // And the asm bodies' definition phases (#1839), whose touched xmm registers' floating values are saved in rows as a
    // call's are, and the ones whose records write an opmask register (IrAsmBody.masks), whose masks are too
    // (tier1_emit_cfg_asm).
    uint32_t body_count = 0;
    uint32_t masked_count = 0;
    for (uint32_t node = 0; ok && (emitted->prepared.vector_columns != 0 || emitted->prepared.mask_columns != 0) && emitted->prepared.asm_count != 0 && node < count; node += 1)
    {
        const bool body = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM;
        body_count += body ? 1 : 0;
        masked_count += body && tier1_emit_cfg_asm_masks(plan, node) != 0 ? 1 : 0;
    }
    uint32_t* bodies = ok && body_count != 0 ? ir_allocate(scratch, (uint64_t)body_count + 1, 4, &ok) : 0;
    uint32_t* masked = ok && masked_count != 0 ? ir_allocate(scratch, (uint64_t)masked_count + 1, 4, &ok) : 0;
    body_count = 0;
    masked_count = 0;
    for (uint32_t node = 0; ok && bodies != 0 && node < count; node += 1)
    {
        const bool body = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM;
        bodies[body_count] = base + 2 * node + 1;
        body_count += body ? 1 : 0;
        if (masked != 0)
        {
            masked[masked_count] = base + 2 * node + 1;
            masked_count += body && tier1_emit_cfg_asm_masks(plan, node) != 0 ? 1 : 0;
        }
    }
    uint32_t general_saved = 0;
    uint32_t saved = 0;
    uint32_t vectors_saved = 0;
    for (uint32_t i = 0; ok && (saves.positions != 0 || row_count != 0 || body_count != 0 || masked_count != 0) && i < cfg->intervals.count; i += 1)
    {
        const uint32_t class = cfg->intervals.classes[i];
        const uint32_t own = emitted->cfg_input.definitions[cfg->intervals.values[i]];
        const uint32_t owner = emitted->value_nodes[cfg->intervals.values[i]];
        for (uint32_t f = allocation->fragment_offsets[i]; f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            const uint32_t reg = allocation->registers[f];
            const bool in_register = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER;
            // A register found saved isn't searched again: its column is left out.
            general_saved |= class == BUSTER_TIER1_ALLOCATE_GPR && tier1_emit_fragment_saved(allocation, f, own, owner, columns & ~general_saved, emitted->callee_registers, &saves) ? 1u << reg : 0;
            for (uint32_t list = 0; class != BUSTER_TIER1_ALLOCATE_GPR && list < 4; list += 1)
            {
                const uint32_t* positions = list == 0 ? calls : list == 1 ? rows : list == 2 ? bodies : masked;
                const uint32_t position_count = list == 0 ? call_count : list == 1 ? row_count : list == 2 ? body_count : masked_count;
                const uint32_t low = tier1_emit_first_at(positions, position_count, allocation->starts[f]);
                const bool covers = low < position_count && positions[low] < allocation->ends[f] && in_register;
                saved |= list != 2 && class == BUSTER_TIER1_ALLOCATE_MASK && covers && reg < 8 ? 1u << reg : 0;
                vectors_saved |= (list == 0 || list == 2) && class == BUSTER_TIER1_ALLOCATE_VECTOR && covers && reg < 32 ? 1u << reg : 0;
            }
        }
    }
    emitted->prepared.call_save_mask = general_saved & columns;
    emitted->prepared.mask_save_mask = saved & BUSTER_TIER1_EMIT_MASK_REGISTERS;
    emitted->prepared.vector_save_mask = vectors_saved & BUSTER_TIER1_EMIT_VECTOR_REGISTERS;
    arena_pop_to(scratch, mark);
    return ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// The frame offset of frame object `object`: its row, the last ones (tier1_emit_cfg_frame), or for a variadic
// definition's register save area the frame's own (Tier1Frame.variadic_offset, which the prologue fills, #789). An
// overflow area has none (tier1_emit_cfg_incoming).
static bool tier1_emit_cfg_object(const IrFunction* function, const Tier1EmitResult* emitted, uint32_t object, int32_t* offset)
{
    const uint32_t flags = object < function->slot_count ? function->slots[object].flags : 0;
    const bool save_area = (flags & BUSTER_IR_SLOT_REGISTER_SAVE_AREA) != 0;
    const uint32_t row = tier1_emit_cfg_object_row(emitted, object);
    uint64_t past = 0;
    // The records' objects past the IR's slots (#789): a by-value aggregate parameter's copy and the hidden pointer's, but
    // an aggregate on the stack whole, whose copy is in the incoming area.
    const bool record = object >= function->slot_count && object < tier1_emit_cfg_object_count(function, &emitted->prepared) && !tier1_emit_cfg_incoming_object(function, object, &past);
    const bool found = (object < function->slot_count || record) && (flags & BUSTER_IR_SLOT_OVERFLOW_AREA) == 0 && (save_area ? emitted->frame.variadic : row < emitted->frame.count);
    *offset = !found ? 0 : save_area ? emitted->frame.variadic_offset : emitted->frame.offset[row];
    return found;
}

// An address `past` bytes into the incoming argument area, 8 bytes past the return address's slot: a stack parameter's
// (#141), or a variadic definition's overflow area, whose unnamed stack arguments start past its named ones
// (IrFunction.incoming.stack_bytes, #789). That is 16 past rbp in a frame with a frame pointer, a realigned one's among
// them, as the prologue sets rbp right after pushing it. In a frame without one (#177's protocol), the prologue moves
// rsp by its pushes of the callee-saved registers, then by its reservation, and nothing moves it again until the
// epilogue: the area is past both and the return address (the red zone too is below the entry's rsp). The protocol
// never realigns such a frame, which would leave rsp's distance from the entry unknown: the guard refuses one rather
// than miscompile it.
static bool tier1_emit_cfg_incoming(const Tier1EmitResult* emitted, uint64_t past, uint8_t* base, int32_t* offset)
{
    const Tier1Frame* frame = &emitted->frame;
    const uint64_t pushed = 8 * (uint64_t)__builtin_popcount(frame->saved_registers);
    const uint64_t above = frame->frame_pointer ? 16 : (uint64_t)frame->stack_adjust_bytes + pushed + 8;
    const uint64_t total = above + past;
    const bool found = total <= INT32_MAX && (frame->frame_pointer || !frame->realigned);
    *base = frame->frame_pointer ? X86_REGISTER_RBP : X86_REGISTER_RSP;
    *offset = found ? (int32_t)total : 0;
    return found;
}

typedef struct Tier1LifetimeMarker Tier1LifetimeMarker;
struct Tier1LifetimeMarker
{
    uint32_t block;
    uint32_t node;
};

typedef struct Tier1LifetimeRow Tier1LifetimeRow;
struct Tier1LifetimeRow
{
    __m512i seen;
    __m512i pending;
};

// A group's rows start empty. Only initialize the blocks its markers or live edges reach.
static Tier1LifetimeRow* tier1_emit_lifetime_row(Tier1LifetimeRow* rows, uint32_t* stamps, uint32_t group, uint32_t block)
{
    Tier1LifetimeRow* row = &rows[block];
    if (stamps[block] != group + 1)
    {
        row->seen = _mm512_setzero_si512();
        row->pending = _mm512_setzero_si512();
        stamps[block] = group + 1;
    }
    return row;
}

// One vector of independent objects. A block's last marker determines its exit state regardless of its entry state.
// Mark all such bits as seen and enqueue the starts. For unmarked bits, the first live incoming edge makes the exit
// live. Each (block, object) exit is propagated once, including around back edges and in disconnected components.
static void tier1_emit_lifetime_group(const IrFunction* function, const Tier1CfgInput* input, const Tier1LifetimeMarker* markers, uint32_t count, uint32_t group, Tier1LifetimeRow* rows, uint32_t* stamps, uint32_t* work, uint32_t* starts, uint32_t* ends)
{
    uint32_t queued = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        // Markers are in reverse instruction order within the group.
        const uint32_t block = markers[i].block;
        const uint32_t node = markers[i].node;
        const uint32_t bit_index = function->a[node] % BUSTER_DATAFLOW_VECTOR_BITS;
        const uint32_t word = bit_index / 64;
        const uint64_t bit = UINT64_C(1) << (bit_index % 64);
        Tier1LifetimeRow* row = tier1_emit_lifetime_row(rows, stamps, group, block);
        uint64_t* seen = (uint64_t*)&row->seen;
        if ((seen[word] & bit) == 0)
        {
            seen[word] |= bit;
            if (function->b[node] == 0)
            {
                if (_mm512_test_epi64_mask(row->pending, row->pending) == 0)
                {
                    work[queued++] = block;
                }
                ((uint64_t*)&row->pending)[word] |= bit;
            }
        }
    }

    const uint32_t* const offsets = input->successor_offsets;
    const uint32_t* const successors = input->successors;
    const uint32_t* const positions = input->block_starts;
    while (queued != 0)
    {
        const uint32_t block = work[--queued];
        const __m512i delta = rows[block].pending;
        rows[block].pending = _mm512_setzero_si512();
        // Every bit in delta is live at this exit and at each successor's entry, even one whose first marker ends it.
        // Fold those positions into the hull together. A terminal block contributes only its exit.
        uint32_t low = UINT32_MAX;
        uint32_t high = positions[block + 1];
        for (uint32_t edge = offsets[block]; edge < offsets[block + 1]; edge += 1)
        {
            const uint32_t next = successors[edge];
            const uint32_t entry = positions[next];
            low = entry < low ? entry : low;
            high = entry + 1 > high ? entry + 1 : high;
            Tier1LifetimeRow* row = tier1_emit_lifetime_row(rows, stamps, group, next);
            const __m512i fresh = _mm512_andnot_si512(row->seen, delta);
            if (_mm512_test_epi64_mask(fresh, fresh) != 0)
            {
                if (_mm512_test_epi64_mask(row->pending, row->pending) == 0)
                {
                    work[queued++] = next;
                }
                row->seen = _mm512_or_si512(row->seen, fresh);
                row->pending = _mm512_or_si512(row->pending, fresh);
            }
        }
        for (uint32_t word = 0; word < BUSTER_DATAFLOW_VECTOR_WORDS; word += 1)
        {
            for (uint64_t bits = (uint64_t)delta[word]; bits != 0; bits &= bits - 1)
            {
                const uint32_t slot = group * BUSTER_DATAFLOW_VECTOR_BITS + word * 64 + (uint32_t)__builtin_ctzll(bits);
                starts[slot] = low < starts[slot] ? low : starts[slot];
                ends[slot] = high > ends[slot] ? high : ends[slot];
            }
        }
    }
}

// Frame coloring uses the layout hull of each object's lifetime (#177). Markers give the within-block endpoints.
// Only a live exit can extend that hull across blocks. Solve groups of up to 512 slots in the same rows (#1384).
static Tier1EmitError tier1_emit_cfg_lifetimes(const IrFunction* function, const Tier1CfgInput* input, Arena* scratch, uint32_t* starts, uint32_t* ends)
{
    const uint64_t saved = scratch->length;
    const uint32_t objects = function->slot_count;
    const uint32_t blocks = input->block_count;
    const uint32_t ir_blocks = function->block_count;
    const uint32_t instructions = function->instruction_count;
    const uint32_t groups = (uint32_t)(((uint64_t)objects + BUSTER_DATAFLOW_VECTOR_BITS - 1) / BUSTER_DATAFLOW_VECTOR_BITS);
    bool ok = true;
    uint32_t* offsets = ir_allocate(scratch, groups, sizeof(uint32_t), &ok);
    uint32_t count = 0;
    for (uint32_t node = 0; ok && objects != 0 && node < instructions; node += 1)
    {
        if (function->op[node] == IR_LIFETIME)
        {
            offsets[function->a[node] / BUSTER_DATAFLOW_VECTOR_BITS] += 1;
            count += 1;
        }
    }
    if (ok && count != 0)
    {
        Tier1LifetimeMarker* markers = arena_push_array(scratch, count, sizeof(*markers), _Alignof(Tier1LifetimeMarker));
        Tier1LifetimeRow* rows = arena_push_array(scratch, blocks, sizeof(*rows), _Alignof(Tier1LifetimeRow));
        uint32_t* work = arena_push_array(scratch, blocks, sizeof(*work), _Alignof(uint32_t));
        uint32_t* stamps = ir_allocate(scratch, blocks, sizeof(*stamps), &ok);
        ok = ok && markers != 0 && rows != 0 && work != 0;
        if (ok)
        {
            memset(starts, 0xff, (uint64_t)objects * sizeof(*starts));
            memset(ends, 0, (uint64_t)objects * sizeof(*ends));
            for (uint32_t group = 1; group < groups; group += 1)
            {
                offsets[group] += offsets[group - 1];
            }
            const uint32_t added = blocks - ir_blocks;
            for (uint32_t b = 0; b < ir_blocks; b += 1)
            {
                const uint32_t first = function->block_offsets[b];
                const uint32_t end = function->block_offsets[b + 1];
                for (uint32_t node = first; node < end; node += 1)
                {
                    if (function->op[node] == IR_LIFETIME)
                    {
                        const uint32_t slot = function->a[node];
                        const uint32_t position = input->block_starts[b + added] + 2 * (node - first);
                        starts[slot] = function->b[node] == 0 && position < starts[slot] ? position : starts[slot];
                        ends[slot] = position + 1 > ends[slot] ? position + 1 : ends[slot];
                        markers[--offsets[slot / BUSTER_DATAFLOW_VECTOR_BITS]] = (Tier1LifetimeMarker){ .block = b + added, .node = node };
                    }
                }
            }
            for (uint32_t group = 0; group < groups; group += 1)
            {
                const uint32_t end = group + 1 < groups ? offsets[group + 1] : count;
                if (end != offsets[group])
                {
                    tier1_emit_lifetime_group(function, input, markers + offsets[group], end - offsets[group], group, rows, stamps, work, starts, ends);
                }
            }
        }
    }
    for (uint32_t slot = 0; ok && slot < objects; slot += 1)
    {
        // Unmarked objects and objects with only end markers retain the whole function.
        if (count == 0 || starts[slot] == UINT32_MAX)
        {
            starts[slot] = 0;
            ends[slot] = input->position_count;
        }
    }
    arena_pop_to(scratch, saved);
    return ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// The frame: each slot over its values' intervals' hull (a phi's widened to its predecessors' ends), the general
// registers' caller-save rows, the opmask registers' a call saves, the xmm registers' (#141), and each class's temporary
// slot where a move used it, in that row order, then the frame objects.
static Tier1EmitError tier1_emit_cfg_frame(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    Tier1EmitPrepared* prepared = &emitted->prepared;
    const uint32_t count = plan->function->instruction_count;
    const uint32_t spills = cfg->allocation.spill_count;
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_mask);
    const uint32_t call_columns = (uint32_t)__builtin_popcount(prepared->call_save_columns);
    const uint32_t mask_slots = (uint32_t)__builtin_popcount(prepared->mask_save_mask);
    const uint32_t vector_slots = (uint32_t)__builtin_popcount(prepared->vector_save_mask);
    const uint32_t temporary = (uint32_t)__builtin_popcount(cfg->temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1));
    const uint32_t temporary_columns = call_columns + prepared->mask_columns + prepared->vector_columns;
    const uint32_t fixed_columns = temporary_columns + BUSTER_TIER1_ALLOCATE_CLASSES;
    bool ok = true;
    const uint32_t objects = tier1_emit_cfg_object_count(plan->function, prepared);
    uint32_t* indices = ir_allocate(scratch, (uint64_t)spills + call_slots + mask_slots + vector_slots + temporary + objects + 1, 4, &ok);
    uint8_t* phis = ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok);
    Tier1EmitError error = ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK)
    {
        memset(phis, 0, (uint64_t)input->value_count + 1);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            phis[input->phi_values[k]] = 1;
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < spills; k += 1)
    {
        const uint32_t node = emitted->value_nodes[cfg->allocation.spill_values[k]];
        prepared->starts[node] = input->position_count;
        prepared->ends[node] = 0;
        indices[k] = node;
    }
    // A phi web's values share a slot (tier1_cfg_share_slots): its row's hull is all of theirs, kept in its first value's
    // column.
    for (uint32_t v = 0; error == TIER1_EMIT_OK && v < input->value_count; v += 1)
    {
        const uint32_t k = cfg->allocation.spill_ids[v];
        if (k < spills)
        {
            const uint32_t node = indices[k];
            uint32_t start = prepared->starts[node];
            uint32_t end = prepared->ends[node];
            for (uint32_t i = cfg->value_interval_offsets[v]; i < cfg->value_interval_offsets[v + 1]; i += 1)
            {
                start = cfg->intervals.starts[i] < start ? cfg->intervals.starts[i] : start;
                end = cfg->intervals.ends[i] > end ? cfg->intervals.ends[i] : end;
            }
            // A phi's slot is written at its predecessors' ends, where the phi isn't live yet.
            const uint32_t home = phis[v] != 0 ? tier1_cfg_block_at(input, cfg, input->definitions[v]) : 0;
            for (uint32_t e = phis[v] != 0 ? input->predecessor_offsets[home] : 0; phis[v] != 0 && e < input->predecessor_offsets[home + 1]; e += 1)
            {
                const uint32_t exit = input->block_starts[input->predecessors[e] + 1] - 1;
                start = exit < start ? exit : start;
                end = exit + 1 > end ? exit + 1 : end;
            }
            prepared->starts[node] = start;
            prepared->ends[node] = end;
        }
    }
    // The rows past the spills, each its column: the general registers' call saves (those a call or a fixed register's
    // sequence saves, each its register's column: #177), the opmask registers' (one column per register), the temporary
    // slots used, the frame objects.
    uint32_t row = spills;
    for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        if (((prepared->call_save_mask >> reg) & 1) != 0)
        {
            indices[row++] = count + (uint32_t)__builtin_popcount(prepared->call_save_columns & ((1u << reg) - 1));
        }
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < 8; k += 1)
    {
        if (((prepared->mask_save_mask >> k) & 1) != 0)
        {
            indices[row++] = count + call_columns + k;
        }
    }
    for (uint32_t x = 0; error == TIER1_EMIT_OK && x < 32; x += 1)
    {
        if (((prepared->vector_save_mask >> x) & 1) != 0)
        {
            indices[row++] = count + call_columns + prepared->mask_columns + x;
        }
    }
    for (uint32_t class = 0; error == TIER1_EMIT_OK && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        if (((cfg->temp_slots >> class) & 1) != 0)
        {
            indices[row++] = count + temporary_columns + class;
        }
    }
    for (uint32_t o = 0; error == TIER1_EMIT_OK && o < objects; o += 1)
    {
        indices[row++] = count + fixed_columns + o;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_lifetimes(plan->function, input, scratch, prepared->starts + count + fixed_columns, prepared->ends + count + fixed_columns);
    }
    if (error == TIER1_EMIT_OK)
    {
        const Tier1FrameRequest request = {
            .slots = {
                .start = prepared->starts,
                .end = prepared->ends,
                .size = prepared->sizes,
                .alignment = prepared->alignments,
                .indices = indices,
                .count = row,
                .column_count = prepared->slot_columns,
            },
            .calls = &prepared->call_abi,
            .call_count = prepared->call_count + prepared->asm_count != 0,
            // The callee-saved registers an asm body touches (#1839), which the prologue saves as clang's saves cpuid's rbx.
            .used_callee_saved = emitted->callee_used | prepared->asm_callee,
            .flags = tier1_emit_cfg_frame_flags(plan->function, options, prepared->call_count + prepared->asm_count != 0),
        };
        const Tier1FrameError framed = tier1_frame_plan(&request, output, scratch, &emitted->frame);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_FRAME, framed);
        error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : framed == TIER1_FRAME_OVERFLOW ? TIER1_EMIT_LIMIT : TIER1_EMIT_FRAME;
    }
    // A frame object's address's recipe, its displacement from the frame's base now that the layout has the object: its
    // offset, and the constant added to it (tier1_emit_cfg_recipes), as a 64-bit value tier1_rematerialize checks fits.
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count; node += 1)
    {
        int32_t offset = 0;
        if (prepared->recipes.operations[node] == BUSTER_TIER1_REMAT_STACK)
        {
            error = tier1_emit_cfg_object(plan->function, emitted, (uint32_t)prepared->recipe_values[node], &offset) ? error : TIER1_EMIT_FRAME;
            prepared->recipe_values[node] = (uint64_t)((int64_t)offset + prepared->recipes.addends[node]);
        }
    }
    return error;
}

// The frame offset of a move location's slot: a value's spill slot, or its class's temporary slot (the general class's,
// the vector class's or the opmask class's) after the call saves' rows.
static bool tier1_emit_cfg_slot(const Tier1EmitResult* emitted, uint32_t location, int32_t* offset)
{
    const uint32_t spills = emitted->cfg.allocation.spill_count;
    const uint32_t index = location - BUSTER_TIER1_CFG_SPILL;
    const bool temporary = index >= spills && index < spills + BUSTER_TIER1_ALLOCATE_CLASSES;
    // A temporary slot's row only for a temporary: another location's index past the spills names no class.
    const uint32_t row = index < spills ? index : temporary ? tier1_emit_cfg_temporary_row(emitted, index - spills) : UINT32_MAX;
    const bool found = location >= BUSTER_TIER1_CFG_SPILL && (index < spills || temporary) && row < emitted->frame.count;
    *offset = found ? emitted->frame.offset[row] : 0;
    return found;
}

// A call's argument past its class's registers (#141), node `value`'s, into its 8-byte slot `slot` bytes into the
// outgoing area from rsp: from where the allocation has it at the call's use phase `position`, which needn't be a
// register (its use isn't required): from its register (a floating value's xmm one by movss or movsd), from its slot
// through the scratch register, or recomputed into the scratch register. A general register's mov stores the slot's 8
// bytes, an I32's bits past its 32 the callee's to ignore, and a floating value's movss or movsd its 4 or 8, the rest
// of the slot unwritten and the callee's to ignore too, as the ABI says.
static bool tier1_emit_cfg_x87_offset(const IrFunction* function, const Tier1EmitResult* emitted, uint32_t node, int32_t* offset);
static X86Instruction tier1_emit_cfg_x87_memory(uint32_t form, uint8_t base, int32_t displacement);

static X86Instruction tier1_emit_cfg_x87_load(Tier1EmitResult* emitted, uint32_t value, uint8_t base, int32_t offset);
static bool tier1_emit_cfg_x87_append(Tier1EmitResult* emitted, Tier1EmitBuffer* buffer, X86Instruction record);

static Tier1EmitError tier1_emit_cfg_stack_argument(Tier1EmitResult* emitted, uint32_t value, uint32_t position, uint32_t slot, Tier1EmitBuffer* buffer)
{
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    const IrFunction* function = emitted->prepared.function;
    // A long double (#141): its object's 10 bytes into its 16, through the x87 stack, expanding an exact binary32 constant.
    const bool x87 = value < function->instruction_count && function->type[value] == IR_F80;
    const uint32_t v = emitted->node_values[value];
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    int32_t offset = 0;
    const bool found = x87 ? tier1_emit_cfg_x87_offset(function, emitted, value, &offset) : v != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, v, position, &location, &kind);
    Tier1EmitError error = found ? TIER1_EMIT_OK : x87 ? TIER1_EMIT_FRAME : TIER1_EMIT_ALLOCATION;
    uint8_t source = BUSTER_TIER1_SELECT_SCRATCH;
    uint16_t form = X86_FORM_MOV_RM64_R64;
    if (error == TIER1_EMIT_OK && x87)
    {
        error = tier1_emit_cfg_x87_append(emitted, buffer, tier1_emit_cfg_x87_load(emitted, value, base, offset)) ? error : TIER1_EMIT_MEMORY;
        form = X86_FORM_FSTP_M80;
    }
    else if (error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_emit_cfg_vector_location(location))
    {
        source = (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
        form = tier1_select_class_forms(emitted->prepared.sizes[value], tier1_emit_encoding(&emitted->prepared))->store;
    }
    else if (error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
    {
        source = (uint8_t)location;
        error = location < 16 ? error : TIER1_EMIT_ALLOCATION;
    }
    else if (error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_SPILL && tier1_emit_cfg_slot(emitted, location, &offset))
    {
        const uint32_t width = emitted->prepared.sizes[value];
        const X86Instruction load = { .displacement = offset, .reg = source, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32 };
        error = tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
        emitted->spill_loads += 1;
    }
    else if (error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
    {
        error = tier1_emit_copy(emitted, &emitted->prepared.recipes, value, source, base, buffer);
    }
    else
    {
        error = error == TIER1_EMIT_OK ? TIER1_EMIT_ALLOCATION : error;
    }
    // fstp's record has no register (its x87 stack's top).
    const X86Instruction store = { .displacement = (int32_t)slot, .reg = x87 ? 0 : source, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = form };
    return error == TIER1_EMIT_OK ? (tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY) : error;
}

// A by-value aggregate argument's bytes [from, from + size) from its address, node `value`'s at its call's use phase
// `position` (#789), to `to_base` + `to`: through the scratch register from the address's register, or, where the allocation has
// the address in a slot or recomputes it, from the scratch register, which takes it, through rax, whose live value the
// red zone's save slot holds meanwhile (BUSTER_TIER1_EMIT_STAGING_SAVE). The address's use isn't required: a call may pass more
// aggregates than the registers its first allocation gives its uses.
// A by-value aggregate's address, value `value` at `position`, in a register: its own, or the scratch register, reloaded
// from its spill slot or recomputed (`*registered` false then).
static Tier1EmitError tier1_emit_cfg_aggregate_address(Tier1EmitResult* emitted, uint32_t value, uint32_t position, Tier1EmitBuffer* buffer, uint8_t* source, bool* registered)
{
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    const uint32_t v = emitted->node_values[value];
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    int32_t offset = 0;
    const bool found = v != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, v, position, &location, &kind);
    *registered = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16;
    Tier1EmitError error = found ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    if (error == TIER1_EMIT_OK && !*registered && kind == BUSTER_TIER1_ALLOCATE_SPILL && tier1_emit_cfg_slot(emitted, location, &offset))
    {
        const X86Instruction load = { .displacement = offset, .reg = BUSTER_TIER1_SELECT_SCRATCH, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
        error = tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
        emitted->spill_loads += 1;
    }
    else if (error == TIER1_EMIT_OK && !*registered && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
    {
        error = tier1_emit_copy(emitted, &emitted->prepared.recipes, value, BUSTER_TIER1_SELECT_SCRATCH, base, buffer);
    }
    else if (error == TIER1_EMIT_OK && !*registered)
    {
        error = TIER1_EMIT_ALLOCATION;
    }
    *source = *registered ? (uint8_t)location : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
    return error;
}

static Tier1EmitError tier1_emit_cfg_aggregate_copy(Tier1EmitResult* emitted, const Tier1EmitTransitions* transitions, uint32_t owed, uint32_t value, uint32_t position, uint8_t to_base, int32_t to, uint32_t from, uint32_t size, Tier1EmitBuffer* buffer)
{
    uint8_t source = BUSTER_TIER1_SELECT_SCRATCH;
    bool registered = false;
    Tier1EmitError error = tier1_emit_cfg_aggregate_address(emitted, value, position, buffer, &source, &registered);
    const uint8_t data = registered ? (uint8_t)BUSTER_TIER1_SELECT_SCRATCH : (uint8_t)X86_REGISTER_RAX;
    // A borrowed rax needs preservation only while it actually holds a live value. Owners can outlast their intervals,
    // and an owed value is still in its call-save row: tier1_emit_owed already repaid any this call consumes. Saving an
    // otherwise unknown rax would read uninitialized storage in the code certificate.
    const uint32_t owner = transitions->owners[X86_REGISTER_RAX];
    const uint32_t v = owner != BUSTER_IR_NONE ? emitted->node_values[owner] : BUSTER_TIER1_CFG_NONE;
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    const bool preserve = !registered && (owed & (1u << X86_REGISTER_RAX)) == 0 && owner != BUSTER_IR_NONE && transitions->registers[owner] == X86_REGISTER_RAX &&
                          v != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, v, position, &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location == X86_REGISTER_RAX;
    const int32_t save_slot = tier1_emit_staging(BUSTER_TIER1_EMIT_STAGING_SAVE);
    const X86Instruction save = { .displacement = save_slot, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_RM64_R64 };
    const X86Instruction restore = { .displacement = save_slot, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
    error = error != TIER1_EMIT_OK || !preserve || tier1_emit_append(buffer, save) ? error : TIER1_EMIT_MEMORY;
    error = error != TIER1_EMIT_OK || tier1_emit_copy_bytes(buffer, data, to_base, to, source, (int32_t)from, size) ? error : TIER1_EMIT_MEMORY;
    error = error != TIER1_EMIT_OK || !preserve || tier1_emit_append(buffer, restore) ? error : TIER1_EMIT_MEMORY;
    return error;
}

// The records that load a call argument's part of `bytes` in a zmm register (tier1_select_part_in_zmm) from `source` +
// `from` into zmm register `zmm`, into `records`: a 64-byte one's whole (#141), or a record's payload of 40, 48 or 56
// bytes (#2798) in pieces that don't read past it (tier0_payload_load). How many (3 at most).
static uint32_t tier1_emit_cfg_zmm_records(uint8_t zmm, uint32_t bytes, uint8_t source, int32_t from, X86Instruction* records)
{
    records[0] = (X86Instruction){ .displacement = from, .reg = zmm, .rm = source, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_select_part_form(64, true) };
    return tier1_select_part_payload(bytes) ? tier0_payload_load(zmm, bytes, source, from, records) : 1;
}

// A call argument's part of `bytes` in a zmm register (#141, #2798) at `from` in the aggregate at value `value`'s address,
// loaded into zmm register `zmm` (16 and on, tier1_select_part_in_zmm) by tier1_emit_cfg_zmm_records, which
// tier1_emit_call moves into its argument register after the moves.
static Tier1EmitError tier1_emit_cfg_aggregate_zmm(Tier1EmitResult* emitted, uint32_t value, uint32_t position, uint8_t zmm, uint32_t from, uint32_t bytes, Tier1EmitBuffer* buffer)
{
    uint8_t source = BUSTER_TIER1_SELECT_SCRATCH;
    bool registered = false;
    Tier1EmitError error = tier1_emit_cfg_aggregate_address(emitted, value, position, buffer, &source, &registered);
    X86Instruction records[3];
    const uint32_t count = from <= INT32_MAX ? tier1_emit_cfg_zmm_records(zmm, bytes, source, (int32_t)from, records) : 0;
    error = error == TIER1_EMIT_OK && count == 0 ? TIER1_EMIT_MEMORY : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < count; k += 1)
    {
        error = tier1_emit_append(buffer, records[k]) ? error : TIER1_EMIT_MEMORY;
    }
    return error;
}

// A node whose rule takes x86's fixed registers (tier1_select_fixed: a division or a shift by a register, a division by a
// constant's reciprocal), with any other value live past it in those registers, by its interval at the node's definition
// phase, saved around it in its call-save row (tier1_emit_cfg_problem gives it one; tier1_emit_fixed). But the rule's
// reader (tier1_select_fixed_reader, #2767): a shift's count in rcx, which the shift reads there and leaves.
static Tier1EmitError tier1_emit_cfg_fixed(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, const Tier1EmitTransitions* transitions, uint32_t node, uint32_t use, const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    const uint32_t fixed = tier1_select_fixed_registers(holes->rule);
    const uint32_t reader = tier1_select_fixed_reader(&plan->patterns[node]);
    uint32_t saved = 0;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        const uint32_t owner = transitions->owners[reg];
        const bool live = ((fixed >> reg) & 1) != 0 && owner != BUSTER_IR_NONE && owner != node && owner != reader && transitions->registers[owner] == reg &&
                          tier1_cfg_interval_at(&emitted->cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        saved |= live && reg != holes->destination ? 1u << reg : 0;
    }
    return tier1_emit_fixed(emitted, node, saved, holes, buffer);
}

// The entry's moves: each parameter free at its definition (Tier1CfgInput.definition_registers 0 for it, every
// parameter where the allocation needed them all free) from its incoming register into the register its allocation has
// it in at the entry, its definition's, as one parallel move through the scratch register for the general registers and
// another through the vector class's for the xmm ones (#141). A parameter bound to its incoming register is there
// already. Then each parameter past its class's registers, always free, is loaded from its incoming stack slot into its
// register (#141): after the moves, which read the registers the loads may write.
static Tier1EmitError tier1_emit_cfg_entry(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, Arena* scratch, Tier1EmitBuffer* buffer)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1Allocation* allocation = &emitted->cfg.allocation;
    uint32_t sources[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t destinations[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint32_t vector_sources[BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t vector_destinations[BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t count = 0;
    uint32_t vectors = 0;
    Tier1EmitError error = TIER1_EMIT_OK;
    // A function with its own ABI records (#789): each by-value aggregate parameter with a copy into it, its parts stored
    // whole, 8 bytes each (the copy's size is rounded up to them, tier1_emit_cfg_record_object_bytes), a general
    // register's by mov, an xmm one's by movsd, a whole vector register's by its width's store (a record's payload's too,
    // #2798, which the copy is as wide as), and one in an incoming stack slot (#141, tier1_select_location_parts) through
    // the scratch register, but a PAYLOAD slot's payload (#2798), the slot whole through the vector scratch register, by
    // the payload's register's width (tier1_select_part_form); and the hidden pointer an aggregate result returns through,
    // rdi, into its object. Before the moves, which may write those registers; the prologue writes none of them.
    for (uint32_t p = 0; error == TIER1_EMIT_OK && function->parameter_passed != 0 && p <= function->parameter_count; p += 1)
    {
        const IrPassed* record = p < function->parameter_count ? tier1_select_parameter_record(function, p) : 0;
        const bool copied = tier1_emit_cfg_record_copied(record);
        const bool hidden = p == function->parameter_count && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
        Tier1SelectPart parts[2] = { { .reg = BUSTER_ABI_RDI, .offset = 0, .bytes = 8 } };
        const uint32_t part_count = copied ? tier1_select_location_parts(record->location, record->size, parts) : hidden ? 1 : 0;
        int32_t offset = 0;
        error = (!copied && !hidden) || (part_count != 0 && tier1_emit_cfg_object(function, emitted, function->slot_count + p, &offset)) ? error : TIER1_EMIT_FRAME;
        for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
        {
            const bool slot = parts[k].reg == BUSTER_ABI_REGISTER_NONE;
            const bool payload = slot && parts[k].bytes > 8;
            const bool vector = payload || (!slot && (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR);
            const uint8_t data = payload ? (uint8_t)BUSTER_TIER1_SELECT_FLOAT_SCRATCH : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
            uint8_t base = X86_REGISTER_RSP;
            int32_t incoming = 0;
            error = !slot || tier1_emit_cfg_incoming(emitted, record->location.stack_offset, &base, &incoming) ? error : TIER1_EMIT_FRAME;
            const X86Instruction load = { .displacement = incoming, .reg = data, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = payload ? tier1_select_part_form(parts[k].bytes, true) : X86_FORM_MOV_R64_RM64 };
            error = error != TIER1_EMIT_OK || !slot || tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
            const X86Instruction store = {
                .displacement = offset + parts[k].offset,
                .reg = slot ? data : (uint8_t)(parts[k].reg & 0x1f),
                .rm = (uint8_t)emitted->frame.base_register,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = vector ? tier1_select_part_form(parts[k].bytes, false) : X86_FORM_MOV_RM64_R64,
            };
            error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY;
        }
    }
    for (uint32_t node = 0; error == TIER1_EMIT_OK && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t v = emitted->node_values[node];
        const bool free = tier1_select_entry_parameter(function, node) && v != BUSTER_TIER1_CFG_NONE && (input->definition_registers == 0 || input->definition_registers[v] == 0);
        const Tier1SelectPlace place = tier1_select_entry_parameter(function, node) ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_STACK };
        const bool general = place.kind == BUSTER_TIER1_PLACE_GENERAL;
        const bool vector = place.kind == BUSTER_TIER1_PLACE_VECTOR;
        const uint32_t interval = free ? tier1_cfg_interval_at(&emitted->cfg, v, 0) : BUSTER_TIER1_CFG_NONE;
        const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? allocation->fragment_offsets[interval] : 0;
        const bool room = general ? count < BUSTER_ABI_INTEGER_ARGUMENTS : vectors < BUSTER_ABI_VECTOR_ARGUMENTS;
        error = !free || (!general && !vector) || (interval != BUSTER_TIER1_CFG_NONE && allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && room) ? error : TIER1_EMIT_ALLOCATION;
        if (free && general && error == TIER1_EMIT_OK && allocation->registers[fragment] != tier1_select_parameter_registers[place.index])
        {
            sources[count] = tier1_select_parameter_registers[place.index];
            destinations[count] = allocation->registers[fragment];
            count += 1;
        }
        else if (free && vector && error == TIER1_EMIT_OK && allocation->registers[fragment] != place.index)
        {
            vector_sources[vectors] = place.index;
            vector_destinations[vectors] = allocation->registers[fragment];
            vectors += 1;
        }
    }
    const Tier1ParallelMoves moves = {
        .sources = sources,
        .destinations = destinations,
        .count = count,
        .scratch = BUSTER_TIER1_SELECT_SCRATCH,
    };
    const Tier1ParallelMoves vector_moves = {
        .sources = vector_sources,
        .destinations = vector_destinations,
        .count = vectors,
        .scratch = BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
    };
    const uint64_t scratch_mark = scratch->length;
    Tier1ResolvedMoves resolved = { 0 };
    Tier1ResolvedMoves vector_resolved = { 0 };
    if (error == TIER1_EMIT_OK && count != 0)
    {
        const Tier1AllocationError moved = tier1_resolve_moves(&moves, scratch, &resolved);
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    if (error == TIER1_EMIT_OK && vectors != 0)
    {
        const Tier1AllocationError moved = tier1_resolve_moves(&vector_moves, scratch, &vector_resolved);
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    for (uint32_t move = 0; error == TIER1_EMIT_OK && count != 0 && move < resolved.count; move += 1)
    {
        error = tier1_emit_append(buffer, tier1_emit_move(resolved.sources[move], resolved.destinations[move], 8)) ? error : TIER1_EMIT_MEMORY;
    }
    for (uint32_t move = 0; error == TIER1_EMIT_OK && vectors != 0 && move < vector_resolved.count; move += 1)
    {
        error = tier1_emit_append(buffer, tier1_select_float_move((uint8_t)vector_resolved.destinations[move], (uint8_t)vector_resolved.sources[move])) ? error : TIER1_EMIT_MEMORY;
    }
    arena_pop_to(scratch, scratch_mark);
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_ENTRY, 0), BUSTER_IR_NONE);
    // Each parameter past its class's registers from its incoming slot, 8 bytes each, into where its first fragment is:
    // its register, or its slot through the scratch register (its definition isn't required to be in a register). A
    // narrow one is zero-extended into its canonical 32-bit storage and an I32 zeroed past its 32 bits, as every other's
    // is; a floating one is movss's or movsd's into its xmm register, or its bits' through the scratch register.
    for (uint32_t node = 0; error == TIER1_EMIT_OK && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t v = emitted->node_values[node];
        const Tier1SelectPlace place = function->op[node] == IR_PARAM ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
        if (function->op[node] == IR_PARAM && place.kind == BUSTER_TIER1_PLACE_STACK && v != BUSTER_TIER1_CFG_NONE)
        {
            uint32_t location = BUSTER_TIER1_CFG_NONE;
            uint8_t kind = 0;
            uint8_t base = X86_REGISTER_RSP;
            int32_t offset = 0;
            int32_t slot = 0;
            const bool found = tier1_cfg_location(&emitted->cfg, v, 0, &location, &kind) && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset);
            const uint32_t type = function->type[node];
            const uint32_t float_width = tier1_select_float_width(type);
            const bool in_vector = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && float_width != 0 && tier1_emit_cfg_vector_location(location);
            const bool in_register = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && float_width == 0 && location < 16;
            const bool in_slot = found && kind == BUSTER_TIER1_ALLOCATE_SPILL && tier1_emit_cfg_slot(emitted, location, &slot);
            const bool signed_parameter = type == IR_I32 && plan->patterns[node].sign_result;
            const uint16_t general_form = signed_parameter ? X86_FORM_MOVSXD_R64_RM32 : type == IR_I64 || type == IR_PTR || float_width == 8 ? X86_FORM_MOV_R64_RM64 : type == IR_I32 || float_width == 4 ? X86_FORM_MOV_R32_RM32 : type == IR_I16 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
            const uint8_t target = in_vector ? (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION) : in_register ? (uint8_t)location : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
            const X86Instruction load = { .form = in_vector ? tier1_select_float_forms(float_width)->load : general_form, .reg = target, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = offset };
            const uint32_t width = emitted->prepared.sizes[node];
            const X86Instruction store = { .displacement = slot, .reg = target, .rm = (uint8_t)emitted->frame.base_register, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            error = !in_vector && !in_register && !in_slot ? TIER1_EMIT_ALLOCATION : tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
            error = error == TIER1_EMIT_OK && in_slot ? (tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY) : error;
            emitted->spill_stores += error == TIER1_EMIT_OK && in_slot;
            tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_ENTRY, node), node);
        }
    }
    return error;
}

// Moves [first, end) of tier1_cfg's columns: registers and slots by tier1_cfg's location ids, and recomputations
// (tier1_emit_copy, a symbol's with its reference), into a slot through the scratch register.
static Tier1EmitError tier1_emit_cfg_x87_phis(const IrFunction* function, Tier1EmitResult* emitted, uint32_t edge, Tier1EmitBuffer* buffer);

// Edge `edge`'s moves, and its long double phis' copies (#141, tier1_emit_cfg_x87_phis) after them.
static Tier1EmitError tier1_emit_cfg_moves(Tier1EmitResult* emitted, uint32_t edge, Tier1EmitBuffer* buffer)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t first = cfg->edge_move_offsets[edge];
    const uint32_t end = cfg->edge_move_offsets[edge + 1];
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t m = first; error == TIER1_EMIT_OK && m < end; m += 1)
    {
        const uint32_t from = cfg->move_sources[m];
        const uint32_t to = cfg->move_destinations[m];
        const uint32_t node = emitted->value_nodes[cfg->move_values[m]];
        const uint32_t width = emitted->prepared.sizes[node];
        X86Instruction records[2] = { { 0 }, { 0 } };
        uint32_t record_count = 0;
        int32_t offset = 0;
        // A mask (#179) between opmask registers (k0 the class's scratch among them) and slots, by kmov of its width:
        // tier1_cfg moves one slot to another through k0, in two moves, and recomputes none.
        const bool mask = tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_MASK;
        const bool from_mask = from - BUSTER_TIER1_EMIT_MASK_LOCATION < 8;
        const bool to_mask = to - BUSTER_TIER1_EMIT_MASK_LOCATION < 8;
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        // A floating value (#141) between xmm registers (xmm15 the class's scratch among them) and slots, by movaps and
        // movss or movsd of its width, and a vector (#141) by vmovdqa64 and vmovdqu32 of its: tier1_cfg moves one slot to
        // another through xmm15, in two moves, and recomputes none.
        const bool vector = tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_VECTOR;
        const bool from_vector = from - BUSTER_TIER1_EMIT_VECTOR_LOCATION < 32;
        const bool to_vector = to - BUSTER_TIER1_EMIT_VECTOR_LOCATION < 32;
        const Tier1SelectFloatForms* floats = tier1_select_class_forms(width, tier1_emit_encoding(&emitted->prepared));
        if (vector && from_vector && to_vector)
        {
            records[record_count++] = tier1_select_class_move((uint8_t)(to & 31), (uint8_t)(from & 31), width, tier1_emit_encoding(&emitted->prepared));
        }
        else if (vector && from_vector && tier1_emit_cfg_slot(emitted, to, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(from & 31), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = floats->store };
            emitted->spill_stores += 1;
        }
        else if (vector && to_vector && tier1_emit_cfg_slot(emitted, from, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(to & 31), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = floats->load };
            emitted->spill_loads += 1;
        }
        else if (vector)
        {
            error = TIER1_EMIT_ALLOCATION;
        }
        else if (mask && from_mask && to_mask)
        {
            records[record_count++] = (X86Instruction){ .form = forms->move, .reg = (uint8_t)(to & 7), .rm = (uint8_t)(from & 7) };
        }
        else if (mask && from_mask && tier1_emit_cfg_slot(emitted, to, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(from & 7), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = forms->store };
            emitted->spill_stores += 1;
        }
        else if (mask && to_mask && tier1_emit_cfg_slot(emitted, from, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(to & 7), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = forms->load };
            emitted->spill_loads += 1;
        }
        else if (mask)
        {
            error = TIER1_EMIT_ALLOCATION;
        }
        else if (from == BUSTER_TIER1_CFG_REMATERIALIZE)
        {
            // The copy goes in now, then the store into a slot's destination after it.
            const uint8_t target = to < 16 ? (uint8_t)to : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
            const bool placed = to < 16 || tier1_emit_cfg_slot(emitted, to, &offset);
            error = placed ? tier1_emit_copy(emitted, &emitted->prepared.recipes, node, target, base, buffer) : TIER1_EMIT_ALLOCATION;
            emitted->edge_moves += error == TIER1_EMIT_OK;
            if (to >= 16)
            {
                records[record_count++] = (X86Instruction){ .displacement = offset, .reg = target, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            }
        }
        else if (from < 16 && to < 16)
        {
            records[record_count++] = tier1_emit_move(from, to, 8);
        }
        else if (from < 16 && tier1_emit_cfg_slot(emitted, to, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)from, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            emitted->spill_stores += 1;
        }
        else if (to < 16 && tier1_emit_cfg_slot(emitted, from, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)to, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32 };
            emitted->spill_loads += 1;
        }
        else
        {
            // tier1_cfg moves slot to slot through the scratch register, in two moves.
            error = TIER1_EMIT_ALLOCATION;
        }
        for (uint32_t r = 0; error == TIER1_EMIT_OK && r < record_count; r += 1)
        {
            error = tier1_emit_append(buffer, records[r]) ? error : TIER1_EMIT_MEMORY;
        }
        emitted->edge_moves += error == TIER1_EMIT_OK ? record_count : 0;
        tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_EDGE, edge), node);
    }
    if (error == TIER1_EMIT_OK && emitted->prepared.x87_count != 0)
    {
        error = tier1_emit_cfg_x87_phis(emitted->prepared.function, emitted, edge, buffer);
    }
    return error;
}

// Whether value v's definition at `definition` writes a register nothing reads: v is recomputed (#178), and the first
// fragment of the interval its definition starts serves no use of v but those of definitions left out (`dropped`, by
// node, decided for every later node first), and ends where a recomputation takes over (the next fragment's) or, as the
// interval's last, before its block's end, where an edge would read it; either way before the last position of the
// block the definition is in. An edge leaving a block whose end the fragment covers reads its register: for a phi's
// operand on the edge, or for v live into the successor, moved from the register or found there where the successor
// keeps v (or the phi) in the same one, with no move at all (#141: sqlite3VdbeExec's join of an error code). Its
// records can then be left out, the value recomputed where it is used (an edge's move or a fragment's transition takes
// the recipe rather than the register), as the definition's records only compute a value: a constant, an address or a
// copy of one. So is a definition the allocator gave no register (#2618), whose first fragment recomputes it: its own use
// isn't required, and values read sooner held every register (tier1_dropped_test.c's g). A use at position p > the
// definition is node (p - base) / 2's.
static bool tier1_emit_cfg_dead_definition(const Tier1EmitResult* emitted, uint32_t v, uint32_t definition, const uint8_t* dropped, uint32_t base)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t interval = input->rematerializable != 0 && input->rematerializable[v] != 0 ? tier1_cfg_interval_at(cfg, v, definition) : BUSTER_TIER1_CFG_NONE;
    const uint32_t first = interval != BUSTER_TIER1_CFG_NONE ? allocation->fragment_offsets[interval] : 0;
    const bool last = interval != BUSTER_TIER1_CFG_NONE && first + 1 == allocation->fragment_offsets[interval + 1];
    const uint32_t end = interval != BUSTER_TIER1_CFG_NONE ? cfg->intervals.ends[interval] : 0;
    const bool inside = last && end != input->block_starts[tier1_cfg_block_at(input, cfg, end - 1) + 1];
    const uint32_t block = interval != BUSTER_TIER1_CFG_NONE ? tier1_cfg_block_at(input, cfg, definition) : 0;
    const bool starts = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == definition;
    // Given no register at all (#2618: its definition's use isn't required), the first fragment recomputes it.
    const bool bare = starts && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
    // Or the fragment reaches its block's end, where no edge leaving the block reads its register (#2618): each successor
    // that v is live into enters with its recipe (a range that rests there), and no phi takes v along the edge.
    bool leaves = starts && last && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->ends[first] == end && end == input->block_starts[block + 1];
    for (uint32_t e = leaves ? input->successor_offsets[block] : 0; leaves && e < input->successor_offsets[block + 1]; e += 1)
    {
        const uint32_t successor = input->successors[e];
        uint32_t location = 0;
        uint8_t kind = 0;
        leaves = !tier1_cfg_entry_location(cfg, v, input->block_starts[successor], &location, &kind) || kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
        uint32_t j = 0;
        while (j < input->predecessor_offsets[successor + 1] - input->predecessor_offsets[successor] && input->predecessors[input->predecessor_offsets[successor] + j] != block)
        {
            j += 1;
        }
        for (uint32_t k = input->phi_offsets[successor]; leaves && k < input->phi_offsets[successor + 1]; k += 1)
        {
            leaves = input->phi_operands[input->phi_operand_offsets[k] + j] != v;
        }
    }
    bool dead = starts && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                ((allocation->ends[first] < input->block_starts[block + 1] && (inside || (!last && allocation->kinds[first + 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE))) || leaves);
    // Uses are sorted: the definition's own (at its position), then those the first fragment serves, before its end.
    for (uint32_t u = dead ? input->use_offsets[v] : 0; dead && u < input->use_offsets[v + 1]; u += 1)
    {
        const uint32_t position = input->use_positions[u];
        dead = position <= definition || position >= allocation->ends[first] || dropped[(position - base) / 2] != 0;
    }
    return dead || bare;
}

// A jump of `form` to `goal` (a block, or an edge's stub with BUSTER_TIER1_EMIT_CFG_STUB), resolved once every block's
// records are in: its branch slot and goal go in the pending columns.
static Tier1EmitError tier1_emit_cfg_jump(Tier1EmitBuffer* buffer, uint16_t form, uint32_t goal, uint32_t* pending_branches, uint32_t* pending_goals, uint32_t* pending, Tier1EmitResult* emitted)
{
    const bool room = buffer->branch_count < buffer->branch_capacity && tier1_emit_append(buffer, (X86Instruction){ .form = form });
    if (room)
    {
        buffer->branches[buffer->branch_count] = buffer->count - 1;
        buffer->targets[buffer->branch_count] = 0;
        pending_branches[*pending] = buffer->branch_count;
        pending_goals[*pending] = goal;
        *pending += 1;
        buffer->branch_count += 1;
        emitted->jumps += 1;
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_CONTROL, goal), BUSTER_IR_NONE);
    return room ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// Each jump to a block whose first record is an indirect jump of a register takes a copy of that jump instead (#2554):
// GNU C's computed goto reaches its labels through the one dispatch block (docs/ir.md's "Labels and br_indirect"), so
// each jump there ran a jump and the indirect jump, and the branch predictor saw one indirect jump for every site, where
// each copy is a branch of its own, as LLVM's tail duplication makes them. Nothing runs between the jump and the indirect
// jump, the block's first record, so the copy reads the same registers; the label addresses it goes to are the block's
// (an edge's stub among them). A conditional jump and a stub's goal stay. The copies leave the branch columns, which
// keep their order; the code certificate takes a copy for the block's first record (tier1_cert_emitted_walk).
static void tier1_emit_cfg_dispatch(Tier1EmitBuffer* buffer, const uint32_t* block_first, uint32_t blocks, const uint32_t* pending_branches, const uint32_t* pending_goals, uint32_t pending,
                                    Tier1EmitResult* emitted)
{
    uint32_t copies = 0;
    for (uint32_t k = 0; k < pending; k += 1)
    {
        const uint32_t goal = pending_goals[k];
        const uint32_t branch = pending_branches[k];
        const uint32_t record = buffer->branches[branch];
        const uint32_t first = (goal & BUSTER_TIER1_EMIT_CFG_STUB) == 0 && goal < blocks ? block_first[goal] : UINT32_MAX;
        const bool dispatch = first < buffer->count && buffer->records[record].form == X86_FORM_JMP_REL8 && buffer->records[first].form == X86_FORM_JMP_RM64 &&
                              buffer->records[first].mode == 0;
        if (dispatch)
        {
            buffer->records[record] = buffer->records[first];
            buffer->targets[branch] = UINT32_MAX;
            copies += 1;
        }
    }
    uint32_t kept = 0;
    for (uint32_t b = 0; copies != 0 && b < buffer->branch_count; b += 1)
    {
        if (buffer->targets[b] != UINT32_MAX)
        {
            buffer->branches[kept] = buffer->branches[b];
            buffer->targets[kept] = buffer->targets[b];
            kept += 1;
        }
    }
    buffer->branch_count = copies != 0 ? kept : buffer->branch_count;
    emitted->jumps -= copies;
    emitted->dispatch_copies += copies;
}

// A conditional branch's records after its comparison: the jumps to its two successors' blocks or stubs, and the stubs
// of its split edges, `e_true` and `e_true + 1` its edges and `b` its block.
static Tier1EmitError tier1_emit_cfg_branch(Tier1EmitResult* emitted, uint32_t b, uint32_t e_true, uint16_t condition, Tier1EmitBuffer* buffer, uint32_t* stub_first, uint32_t* pending_branches, uint32_t* pending_goals, uint32_t* pending)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t e_false = e_true + 1;
    const uint32_t t = input->successors[e_true];
    const uint32_t f = input->successors[e_false];
    const bool split_true = cfg->edge_placements[e_true] == TIER1_CFG_EDGE_SPLIT;
    const bool split_false = cfg->edge_placements[e_false] == TIER1_CFG_EDGE_SPLIT;
    const uint16_t inverse = tier1_emit_cfg_inverse(condition);
    // With two stubs, put the next block's stub last, so its moves fall into that block. A lone stub stays inline
    // after the conditional jump; without stubs, the next block is the fall-through successor.
    const bool reverse = (!split_true && !split_false && t == b + 1) || (split_true && (!split_false || f == b + 1));
    const uint32_t taken_edge = reverse ? e_false : e_true;
    const uint32_t inline_edge = reverse ? e_true : e_false;
    const uint32_t taken_block = reverse ? f : t;
    const uint32_t inline_block = reverse ? t : f;
    const bool taken_split = reverse ? split_false : split_true;
    const bool inline_split = reverse ? split_true : split_false;
    Tier1EmitError error = inverse != X86_FORM_NONE ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_jump(buffer, reverse ? inverse : condition, taken_split ? BUSTER_TIER1_EMIT_CFG_STUB | taken_edge : taken_block, pending_branches, pending_goals, pending, emitted);
    }
    if (error == TIER1_EMIT_OK && inline_split)
    {
        stub_first[inline_edge] = buffer->count;
        emitted->edge_stubs += 1;
        error = tier1_emit_cfg_moves(emitted, inline_edge, buffer);
    }
    if (error == TIER1_EMIT_OK && (inline_block != b + 1 || taken_split))
    {
        error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, inline_block, pending_branches, pending_goals, pending, emitted);
    }
    if (error == TIER1_EMIT_OK && taken_split)
    {
        stub_first[taken_edge] = buffer->count;
        emitted->edge_stubs += 1;
        error = tier1_emit_cfg_moves(emitted, taken_edge, buffer);
        if (error == TIER1_EMIT_OK && taken_block != b + 1)
        {
            error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, taken_block, pending_branches, pending_goals, pending, emitted);
        }
    }
    return error;
}

// An indirect jump's split edges, after its jump (#789): a stub for each, the edge's moves and a jump to its label's block,
// where the label's symbol goes (tier1_emit_cfg_labels). Its other edges' moves are at their labels' blocks' starts,
// which they alone reach, and none goes before the jump, which reads its target's register.
static Tier1EmitError tier1_emit_cfg_indirect(Tier1EmitResult* emitted, uint32_t b, Tier1EmitBuffer* buffer, uint32_t* stub_first, uint32_t* pending_branches, uint32_t* pending_goals, uint32_t* pending)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t e = input->successor_offsets[b]; error == TIER1_EMIT_OK && e < input->successor_offsets[b + 1]; e += 1)
    {
        if (cfg->edge_placements[e] == TIER1_CFG_EDGE_SPLIT)
        {
            stub_first[e] = buffer->count;
            emitted->edge_stubs += 1;
            error = tier1_emit_cfg_moves(emitted, e, buffer);
            error = error == TIER1_EMIT_OK ? tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, input->successors[e], pending_branches, pending_goals, pending, emitted) : error;
        }
    }
    return error;
}

// Each of the function's labels' first record (Tier1EmitResult.label_records, #789): its block's, or where the indirect
// jump that may go to it has a stub for that edge, the stub's, which the jump reaches through the label's address alone.
// A label no block holds keeps UINT32_MAX: no indirect branch reaches it, though other indirect branches with explicit
// target lists may remain. The driver still defines its symbol at the function's start, without adding a control-flow
// target. An indirect jump with one successor jumps there directly, and its label is its block's. A label is one
// address for every indirect jump that may go to it. The C lowering puts each on one jump's list (a jump table's, or
// the dispatch block's of GNU C's goto *), but
// for a dispatch block that lists none, which may go to every label, where goto * has no label of its own, and the IR
// doesn't require it. A label two jumps may go to is its block's where neither edge has a stub, and otherwise the
// function is refused, TIER1_EMIT_UNSUPPORTED: one jump would enter the other's stub (#1016's review).
static Tier1EmitError tier1_emit_cfg_labels(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, const uint32_t* block_first, const uint32_t* stub_first, Arena* output, Arena* scratch)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t added = input->block_count - function->block_count;
    bool ok = true;
    uint32_t* records = function->label_count != 0 ? ir_allocate(output, function->label_count, 4, &ok) : 0;
    // The edge from the jump's block to each of its successors, by successor: the CFG's successors are distinct.
    uint32_t* edges = function->label_count != 0 ? ir_allocate(scratch, input->block_count, 4, &ok) : 0;
    // Each label's indirect jumps that may go to it (the last one's block, and how many), and whether one's edge has a stub.
    uint32_t* owners = function->label_count != 0 ? ir_allocate(scratch, function->label_count, 4, &ok) : 0;
    uint32_t* owner_counts = function->label_count != 0 ? ir_allocate(scratch, function->label_count, 4, &ok) : 0;
    uint8_t* stubbed = function->label_count != 0 ? ir_allocate(scratch, function->label_count, 1, &ok) : 0;
    for (uint32_t k = 0; ok && k < function->label_count; k += 1)
    {
        records[k] = function->labels[k] == BUSTER_IR_NONE ? UINT32_MAX : block_first[function->labels[k] + added];
        owners[k] = BUSTER_IR_NONE;
        owner_counts[k] = 0;
        stubbed[k] = 0;
    }
    for (uint32_t b = 0; ok && function->label_count != 0 && b < function->block_count; b += 1)
    {
        const uint32_t last = function->block_offsets[b + 1] - 1;
        const uint32_t e_first = input->successor_offsets[b + added];
        const uint32_t e_end = input->successor_offsets[b + added + 1];
        if (function->op[last] == IR_BR_INDIRECT && e_end - e_first > 1)
        {
            for (uint32_t e = e_first; e < e_end; e += 1)
            {
                edges[input->successors[e]] = e;
            }
            const bool list = function->b[last] != BUSTER_IR_NONE;
            const uint32_t listed = list ? function->c[last] : function->label_count;
            for (uint32_t i = 0; i < listed; i += 1)
            {
                const uint32_t label = list ? function->extra[function->b[last] + i] : i;
                const uint32_t e = edges[function->labels[label] + added];
                const bool split = cfg->edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
                owner_counts[label] += owners[label] != b;
                owners[label] = b;
                stubbed[label] |= split;
                records[label] = split ? stub_first[e] : records[label];
            }
        }
    }
    bool owned = true;
    for (uint32_t k = 0; ok && k < function->label_count; k += 1)
    {
        owned &= owner_counts[k] < 2 || stubbed[k] == 0;
    }
    emitted->label_records = ok && owned ? records : 0;
    emitted->label_count = ok && owned ? function->label_count : 0;
    return !ok ? TIER1_EMIT_MEMORY : !owned ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_OK;
}

// Every block's records: see the header.
// A return of an aggregate result (#789) from the address in general register `source`, its ret operand's: in registers,
// each part's bytes staged in the red zone (the frame stays out of it, tier1_emit_cfg_frame_flags) and then its register
// loaded from there, 8 bytes, so that `source` may be one of them; through the hidden pointer, its bytes copied to the
// address the entry stored (tier1_emit_cfg_entry), which rax returns; and an ignored one (#141, an empty struct's) not at
// all. Before the epilogue, which moves rsp past the staging slots.
static Tier1EmitError tier1_emit_cfg_aggregate_result(const IrFunction* function, const Tier1EmitResult* emitted, uint8_t source, Tier1EmitBuffer* buffer)
{
    const AbiReturn result = function->result_abi;
    Tier1SelectPart parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t part_count = tier1_select_result_parts(result, function->result_size, parts);
    // In the x87's registers (#141): each part's 10 bytes loaded from its 16, the last first, so that st0 holds the first.
    const uint32_t x87_parts = tier1_select_x87_parts(result, function->result_size);
    const bool hidden = result.kind == BUSTER_ABI_RESULT_MEMORY;
    int32_t hidden_offset = 0;
    const bool found = hidden && tier1_emit_cfg_object(function, emitted, function->slot_count + function->parameter_count, &hidden_offset);
    const bool ignored = result.kind == BUSTER_ABI_RESULT_IGNORE;
    Tier1EmitError error = ignored || (source < 16 && source != BUSTER_TIER1_SELECT_SCRATCH && (part_count != 0 || x87_parts != 0 || found)) ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    // Every staged part's slots in the red zone (#1847): a split vector's parts take none, loaded straight from `source`.
    error = error == TIER1_EMIT_OK && !tier1_select_result_staged(parts, part_count) ? TIER1_EMIT_UNSUPPORTED : error;
    for (uint32_t k = x87_parts; error == TIER1_EMIT_OK && k-- > 0;)
    {
        const X86Instruction load = { .form = X86_FORM_FLD_M80, .rm = source, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(16 * k) };
        error = tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
    }
    // Each staged part from its slot on, after the staged parts before it (tier1_select_result_slot), a vector register's
    // (#141) in its bytes' slots (tier1_emit_staging_part). A split vector's parts (#1624: two or four of 16, 32 or 64
    // bytes, #1847) aren't staged: each is loaded straight from `source`, a general register no load overwrites; and
    // neither is a record's payload no register is as wide as (#2798), loaded from it in pieces that stay inside it
    // (tier0_payload_load).
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
    {
        const int32_t staged = tier1_emit_staging_part(tier1_select_result_slot(parts, k), parts[k].bytes);
        error = tier1_select_part_direct(parts[k]) || tier1_emit_copy_bytes(buffer, BUSTER_TIER1_SELECT_SCRATCH, X86_REGISTER_RSP, staged, source, parts[k].offset, parts[k].bytes) ? error : TIER1_EMIT_MEMORY;
    }
    // Then each part's register, a general register's loaded straight from `source` into `source` itself last
    // (tier1_select_result_order).
    for (uint32_t ordered = 0; error == TIER1_EMIT_OK && ordered < part_count; ordered += 1)
    {
        const uint32_t k = tier1_select_result_order(parts, part_count, source, ordered);
        const bool vector_part = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const bool direct = tier1_select_part_direct(parts[k]);
        X86Instruction loads[3] = {
            {
                .displacement = direct ? parts[k].offset : tier1_emit_staging_part(tier1_select_result_slot(parts, k), parts[k].bytes),
                .reg = (uint8_t)(parts[k].reg & 0x1f),
                .rm = direct ? source : X86_REGISTER_RSP,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = vector_part ? tier1_select_part_form(parts[k].bytes, true) : X86_FORM_MOV_R64_RM64,
            },
        };
        const uint32_t load_count = vector_part && tier1_select_part_payload(parts[k].bytes) ? tier0_payload_load(loads[0].reg, parts[k].bytes, source, parts[k].offset, loads) : 1;
        for (uint32_t j = 0; error == TIER1_EMIT_OK && j < load_count; j += 1)
        {
            error = tier1_emit_append(buffer, loads[j]) ? error : TIER1_EMIT_MEMORY;
        }
    }
    // The hidden pointer into rax, or into r10 where the address is in rax, which then takes it after the copy.
    const uint8_t holder = source == X86_REGISTER_RAX ? X86_REGISTER_R10 : X86_REGISTER_RAX;
    if (error == TIER1_EMIT_OK && hidden)
    {
        const X86Instruction load = { .displacement = hidden_offset, .reg = holder, .rm = (uint8_t)emitted->frame.base_register, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
        error = tier1_emit_append(buffer, load) && tier1_emit_copy_bytes(buffer, BUSTER_TIER1_SELECT_SCRATCH, holder, 0, source, 0, function->result_size) ? error : TIER1_EMIT_MEMORY;
        error = error != TIER1_EMIT_OK || holder == X86_REGISTER_RAX || tier1_emit_append(buffer, tier1_emit_move(holder, X86_REGISTER_RAX, 8)) ? error : TIER1_EMIT_MEMORY;
    }
    return error;
}

// ---------------------------------------------------------------------------------------------------------------------
// Long double (#141)

// A record of `form` on the memory at `displacement` from `base`.
static X86Instruction tier1_emit_cfg_x87_memory(uint32_t form, uint8_t base, int32_t displacement)
{
    return (X86Instruction){ .form = (uint16_t)form, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = displacement };
}

// x87 constants still wait at their original consumers. FLDZ and FLD1 push positive zero and one
// directly. The displacement of a memory constant names its pool index until the record is appended.
static X86Instruction tier1_emit_cfg_x87_load(Tier1EmitResult* emitted, uint32_t value, uint8_t base, int32_t offset)
{
    const IrFunction* function = emitted->prepared.function;
    X86Instruction record = tier1_emit_cfg_x87_memory(X86_FORM_FLD_M80, base, offset);
    if (function->op[value] == IR_CONST)
    {
        const uint32_t index = function->a[value];
        const bool valid = index < function->constant_count && emitted->x87_literal_of != NULL &&
                           emitted->symbol_access_count <= BUSTER_TIER1_EMIT_LITERAL;
        const IrConstant constant = valid ? function->constants[index] : (IrConstant){ 0 };
        uint64_t bits = 0;
        const uint16_t form = x87_literal_bits(constant.low, (uint16_t)constant.high, &bits);
        if (valid && (form == X86_FORM_FLDZ || form == X86_FORM_FLD1))
        {
            record = (X86Instruction){ .form = form };
        }
        else
        {
            uint32_t literal = valid ? emitted->x87_literal_of[index] : 0;
            if (valid && literal == 0 && emitted->literal_count < emitted->literal_capacity)
            {
                const IrSimdValue bytes = { .words = { bits, form == X86_FORM_FLD_M80 ? (uint16_t)constant.high : 0 } };
                const uint32_t k = emitted->literal_count++;
                emitted->literals[k] = bytes;
                emitted->literal_widths[k] = 16;
                literal = k + 1;
                emitted->x87_literal_of[index] = literal;
            }
            record = (X86Instruction){
                .form = (uint16_t)(form | BUSTER_X86_FORM_FIXUP),
                .mode = BUSTER_X86_MEMORY_RIP,
                .displacement = literal != 0 ? (int32_t)(literal - 1) : -1,
            };
        }
    }
    return record;
}

static bool tier1_emit_cfg_x87_append(Tier1EmitResult* emitted, Tier1EmitBuffer* buffer, X86Instruction record)
{
    if (record.mode == BUSTER_X86_MEMORY_RIP)
    {
        const uint32_t literal = (uint32_t)record.displacement;
        if (literal >= emitted->literal_count || emitted->reference_count >= emitted->reference_capacity)
        {
            return false;
        }
        emitted->references[emitted->reference_count++] = (LinkReference){
            .instruction = buffer->count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_PC32,
            .target = BUSTER_TIER1_EMIT_LITERAL + literal,
        };
        record.displacement = 0;
    }
    return tier1_emit_append(buffer, record);
}

// An x87 register's record of `form` (st(i)).
static X86Instruction tier1_emit_cfg_x87_register(uint32_t form, uint32_t i)
{
    return (X86Instruction){ .form = (uint16_t)form, .rm = (uint8_t)i, .mode = BUSTER_X86_REGISTER_DIRECT };
}

// A long double node's object's offset from the frame's base, or the x87 scratch object's for UINT32_MAX.
static bool tier1_emit_cfg_x87_offset(const IrFunction* function, const Tier1EmitResult* emitted, uint32_t node, int32_t* offset)
{
    const bool known = node == UINT32_MAX ? emitted->prepared.x87_count != 0 : node < function->instruction_count && emitted->prepared.x87_ranks[node] != UINT32_MAX;
    return known && tier1_emit_cfg_object(function, emitted, tier1_emit_cfg_x87_object(function, &emitted->prepared, node), offset);
}

// Whether a vector constant of no simpler kind (#141, BUSTER_TIER1_VECTOR_CONSTANT, whose constant's index is the
// pattern's `immediate`) is one whose quadwords are one value at EVEX's level (#2744's step 1; one whose doublewords are is
// BUSTER_TIER1_VECTOR_SPLAT's, so this is _mm512_set1_epi64(63)'s kind), which tier1_emit_cfg_vector_constant broadcasts
// from the scratch general register; every other is a literal the function's code loads (step 2).
static bool tier1_emit_cfg_vector_uniform(const IrFunction* function, const Tier1SelectHoles* holes)
{
    const uint32_t width = holes->width;
    const uint64_t index = holes->immediate;
    bool uniform = index < function->vector_constant_count && (width == 16 || width == 32 || width == 64) &&
                   tier1_select_holes_encoding(holes) == BUSTER_TIER1_ENCODING_EVEX;
    for (uint32_t w = 1; uniform && w < width / 8; w += 1)
    {
        uniform = function->vector_constants[index].words[w] == function->vector_constants[index].words[0];
    }
    return uniform;
}

// The literal of vector constant `index` at `width` bytes (16, 32 or 64; #2744's step 2, Tier1EmitResult.literals): the
// one the function's code already loads, or the next, its bytes the constant's first `width` and zero past them. UINT32_MAX
// where the literals' room (a VCONST node's each) is spent, or where the function's symbols reach the literals' targets
// (BUSTER_TIER1_EMIT_LITERAL), which a unit of two billion symbols would.
static uint32_t tier1_emit_cfg_literal(Tier1EmitResult* emitted, const IrFunction* function, uint32_t index, uint32_t width)
{
    const uint32_t class = width == 16 ? 0 : width == 32 ? 1 : 2;
    const bool valid = emitted->literal_of != 0 && index < function->vector_constant_count && (width == 16 || width == 32 || width == 64) &&
                       emitted->symbol_access_count <= BUSTER_TIER1_EMIT_LITERAL;
    uint32_t literal = valid ? emitted->literal_of[3 * (uint64_t)index + class] : 0;
    if (valid && literal == 0 && emitted->literal_count < emitted->literal_capacity)
    {
        const uint32_t k = emitted->literal_count;
        IrSimdValue bytes = { 0 };
        memcpy(bytes.words, function->vector_constants[index].words, width);
        emitted->literals[k] = bytes;
        emitted->literal_widths[k] = (uint8_t)width;
        emitted->literal_count = k + 1;
        emitted->literal_of[3 * (uint64_t)index + class] = k + 1;
        literal = k + 1;
    }
    return literal != 0 ? literal - 1 : UINT32_MAX;
}

// A vector constant of no simpler kind (#141, BUSTER_TIER1_VECTOR_CONSTANT): where its quadwords are one value at EVEX's
// level (tier1_emit_cfg_vector_uniform), the quadword into the scratch general register by the shortest mov that holds it
// (tier1_rematerialize's choice) and vpbroadcastq from it, two records and no memory; every other, its literal `literal`
// (tier1_emit_cfg_literal) loaded rip-relative by vmovdqu32 of its width (vmovdqu at a target without AVX512VL, #1281)
// into its register, the one record that names it, its reference in `reference` (its index among the records this call
// appends, `referenced`). Before #2744 that was its words as immediates into the red zone below rsp and a load no store
// forwarded to, 22 constants per 64-byte chunk of the compiler's own lexer by --tier1: a pool entry's load stalls on no
// store, and the uniform form takes 1,188 of the 3,157 c_tokenize built in 9,076 bytes. The emitted code's certificate
// replays this function for the node's records and its reference (tier1_certificate_emitted.c), and checks the literal's
// bytes against the constant's, so both change together.
static Tier1EmitError tier1_emit_cfg_vector_constant(const IrFunction* function, const Tier1SelectHoles* holes, uint32_t literal, Tier1EmitBuffer* buffer, LinkReference* reference,
                                                     bool* referenced)
{
    const uint32_t width = holes->width;
    const uint64_t index = holes->immediate;
    Tier1EmitError error = index < function->vector_constant_count && (width == 16 || width == 32 || width == 64) ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const bool uniform = error == TIER1_EMIT_OK && tier1_emit_cfg_vector_uniform(function, holes);
    *referenced = false;
    if (uniform)
    {
        const uint64_t value = function->vector_constants[index].words[0];
        const bool zero_extended = value <= UINT32_MAX;
        const bool sign_extended = (int64_t)value == (int64_t)(int32_t)value;
        const X86Instruction move = {
            .form = zero_extended ? X86_FORM_MOV_R32_IMM32 : sign_extended ? X86_FORM_MOV_RM64_IMM32 : X86_FORM_MOV_R64_IMM64,
            .rm = BUSTER_TIER1_SELECT_SCRATCH,
            .immediate = (int32_t)value,
            .displacement = zero_extended || sign_extended ? 0 : (int32_t)(value >> 32),
        };
        const uint32_t broadcast = width == 16 ? X86_FORM_VPBROADCASTQ_X_R64 : width == 32 ? X86_FORM_VPBROADCASTQ_Y_R64 : X86_FORM_VPBROADCASTQ_Z_R64;
        error = tier1_emit_append(buffer, move) && tier1_emit_append(buffer, tier1_select_register_record(broadcast, (uint8_t)(holes->destination & 31), BUSTER_TIER1_SELECT_SCRATCH)) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK)
    {
        error = literal != UINT32_MAX ? error : TIER1_EMIT_UNSUPPORTED;
        const X86Instruction load = {
            .form = (uint16_t)(tier1_select_class_forms(width, tier1_select_holes_encoding(holes))->load | BUSTER_X86_FORM_FIXUP),
            .reg = (uint8_t)(holes->destination & 31),
            .mode = BUSTER_X86_MEMORY_RIP,
        };
        *reference = (LinkReference){
            .instruction = buffer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_PC32,
            .target = BUSTER_TIER1_EMIT_LITERAL + literal,
            .addend = 0,
        };
        error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
        *referenced = error == TIER1_EMIT_OK;
    }
    return error;
}

// A long double node's records (TIER1_RULE_X87, #141), as Tier 0's (tier0_emit_x87): its long double operands loaded onto
// the x87 stack from their literals or objects, the operation, and a long double result stored into its object, leaving the x87
// stack empty. The scratch object (16 bytes) holds a conversion's control words (bytes 0 to 3: the caller's, then rounding
// toward zero, which C's conversions to integers take and fisttp, SSE3's, is past the baseline), 2^63 as a float (4) and
// an integer or a floating operand or result (8). The scratch register widens an integer operand, and moves the control
// word and an unsigned 64-bit result's top bit; the holes' registers are the node's values' (`left` an address or a
// conversion's operand, `destination` a comparison's or a conversion's result).
static Tier1EmitError tier1_emit_cfg_x87(const IrFunction* function, Tier1EmitResult* emitted, uint32_t node, const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    const uint8_t scratch = BUSTER_TIER1_SELECT_SCRATCH;
    const uint32_t kind = holes->scale;
    if (kind == BUSTER_TIER1_X87_CONSTANT)
    {
        // Consumers load the immutable bits. A definition has no x87 waiting point.
        return TIER1_EMIT_OK;
    }
    const uint32_t a = function->a[node];
    const uint32_t b = function->b[node];
    int32_t own = 0;
    int32_t left = 0;
    int32_t right = 0;
    int32_t temporary = 0;
    const bool produces = function->type[node] == IR_F80;
    const bool reads_a = kind == BUSTER_TIER1_X87_COPY || kind == BUSTER_TIER1_X87_BINARY || kind == BUSTER_TIER1_X87_NEGATE || kind == BUSTER_TIER1_X87_COMPARE || kind == BUSTER_TIER1_X87_TO_FLOAT ||
                         kind == BUSTER_TIER1_X87_TO_INT;
    const bool reads_b = kind == BUSTER_TIER1_X87_BINARY || kind == BUSTER_TIER1_X87_COMPARE || kind == BUSTER_TIER1_X87_STORE;
    const bool converts = kind == BUSTER_TIER1_X87_FROM_FLOAT || kind == BUSTER_TIER1_X87_TO_FLOAT || kind == BUSTER_TIER1_X87_FROM_INT || kind == BUSTER_TIER1_X87_TO_INT;
    bool valid = (!produces || tier1_emit_cfg_x87_offset(function, emitted, node, &own)) && (!reads_a || tier1_emit_cfg_x87_offset(function, emitted, a, &left)) &&
                 (!reads_b || tier1_emit_cfg_x87_offset(function, emitted, b, &right)) && (!converts || tier1_emit_cfg_x87_offset(function, emitted, UINT32_MAX, &temporary));
    X86Instruction records[32];
    uint32_t count = 0;
    const X86Instruction store = tier1_emit_cfg_x87_memory(X86_FORM_FSTP_M80, base, own);
    if (valid && kind == BUSTER_TIER1_X87_LOAD)
    {
        records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FLD_M80, holes->left, 0);
        records[count++] = store;
    }
    else if (valid && kind == BUSTER_TIER1_X87_STORE)
    {
        records[count++] = tier1_emit_cfg_x87_load(emitted, b, base, right);
        records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FSTP_M80, holes->left, 0);
    }
    else if (valid && (kind == BUSTER_TIER1_X87_COPY || kind == BUSTER_TIER1_X87_NEGATE))
    {
        records[count++] = tier1_emit_cfg_x87_load(emitted, a, base, left);
        if (kind == BUSTER_TIER1_X87_NEGATE)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_FCHS };
        }
        records[count++] = store;
    }
    else if (valid && kind == BUSTER_TIER1_X87_BINARY)
    {
        // a into st1 and b into st0, then faddp (fsubp, fmulp, fdivp) st(1), st(0), whose st(1) − st(0) is a − b.
        static const uint16_t operations[4] = { X86_FORM_FADDP_ST_ST0, X86_FORM_FSUBP_ST_ST0, X86_FORM_FMULP_ST_ST0, X86_FORM_FDIVP_ST_ST0 };
        records[count++] = tier1_emit_cfg_x87_load(emitted, a, base, left);
        records[count++] = tier1_emit_cfg_x87_load(emitted, b, base, right);
        records[count++] = tier1_emit_cfg_x87_register(operations[holes->operation & 3], 1);
        records[count++] = store;
    }
    else if (valid && kind == BUSTER_TIER1_X87_COMPARE)
    {
        // fucomip of the operands in the order that makes each condition one unordered operands fail, as ucomisd's: st0
        // a for ==, !=, > and >=, b for < and <= (b > a and b >= a); == and != with the parity flag, through the scratch
        // register.
        const uint32_t condition = holes->condition;
        const bool swapped = condition == IR_LT || condition == IR_LE;
        const uint8_t d = holes->destination;
        valid = d < 16 && condition <= IR_GE;
        records[count++] = tier1_emit_cfg_x87_load(emitted, swapped ? a : b, base, swapped ? left : right);
        records[count++] = tier1_emit_cfg_x87_load(emitted, swapped ? b : a, base, swapped ? right : left);
        records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FUCOMIP_ST0_ST, 1);
        records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FSTP_ST, 0);
        if (condition == IR_EQ || condition == IR_NE)
        {
            const bool equal = condition == IR_EQ;
            records[count++] = (X86Instruction){ .form = equal ? X86_FORM_SETE_RM8 : X86_FORM_SETNE_RM8, .rm = d, .mode = BUSTER_X86_REGISTER_DIRECT };
            records[count++] = (X86Instruction){ .form = equal ? X86_FORM_SETNP_RM8 : X86_FORM_SETP_RM8, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT };
            records[count++] = (X86Instruction){ .form = equal ? X86_FORM_AND_RM8_R8 : X86_FORM_OR_RM8_R8, .reg = scratch, .rm = d, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        else
        {
            const bool strict = condition == IR_GT || condition == IR_LT;
            records[count++] = (X86Instruction){ .form = strict ? X86_FORM_SETA_RM8 : X86_FORM_SETAE_RM8, .rm = d, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        records[count++] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM8, .reg = d, .rm = d, .mode = BUSTER_X86_REGISTER_DIRECT };
    }
    else if (valid && kind == BUSTER_TIER1_X87_FROM_FLOAT)
    {
        const bool single = holes->source_width == 4;
        const uint8_t x = (uint8_t)(holes->left & 15);
        records[count++] = (X86Instruction){ .form = single ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X, .reg = x, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
        records[count++] = tier1_emit_cfg_x87_memory(single ? X86_FORM_FLD_M32 : X86_FORM_FLD_M64, base, temporary + 8);
        records[count++] = store;
    }
    else if (valid && kind == BUSTER_TIER1_X87_TO_FLOAT)
    {
        const bool single = holes->width == 4;
        const uint8_t x = (uint8_t)(holes->destination & 15);
        records[count++] = tier1_emit_cfg_x87_load(emitted, a, base, left);
        records[count++] = tier1_emit_cfg_x87_memory(single ? X86_FORM_FSTP_M32 : X86_FORM_FSTP_M64, base, temporary + 8);
        records[count++] = (X86Instruction){ .form = single ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64, .reg = x, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
    }
    else if (valid && kind == BUSTER_TIER1_X87_FROM_INT)
    {
        // The integer widened in the scratch register: a narrow signed one sign-extended to 32 bits (its canonical storage
        // is zero-extended), an unsigned 32-bit one to 64; then fild of 32 or 64 bits, and for an unsigned 64-bit one, read
        // as signed, 2^64 added where its top bit is set (a float constant that bit makes, through the scratch object).
        const uint32_t bytes = holes->source_width;
        const bool is_signed = holes->operation != 0;
        const bool wide = bytes == 8 || (bytes == 4 && !is_signed);
        const uint8_t source = holes->left;
        valid = source < 16;
        records[count++] = bytes < 4 && is_signed ? (X86Instruction){ .form = bytes == 1 ? X86_FORM_MOVSX_R32_RM8 : X86_FORM_MOVSX_R32_RM16, .reg = scratch, .rm = source, .mode = BUSTER_X86_REGISTER_DIRECT }
                                                  : (X86Instruction){ .form = bytes == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, .reg = scratch, .rm = source, .mode = BUSTER_X86_REGISTER_DIRECT };
        records[count++] = (X86Instruction){ .form = wide ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32, .reg = scratch, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
        records[count++] = tier1_emit_cfg_x87_memory(wide ? X86_FORM_FILD_M64 : X86_FORM_FILD_M32, base, temporary + 8);
        if (bytes == 8 && !is_signed)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_SAR_RM64_IMM8, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 63 };
            records[count++] = (X86Instruction){ .form = X86_FORM_AND_RM32_IMM32, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 0x5f800000 };
            records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM32_R32, .reg = scratch, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FADD_M32, base, temporary + 8);
        }
        records[count++] = store;
    }
    else if (valid && kind == BUSTER_TIER1_X87_TO_INT)
    {
        // Truncating: the control word saved and loaded with rounding toward zero (RC 11), fistp of 64 bits, the control
        // word back. An unsigned 64-bit result of 2^63 or more: 2^63 taken away first and its top bit set after (fcmovbe
        // chooses what to take away, setbe the bit, both from one fucomi with 2^63), as Tier 0's. A narrower result in its
        // canonical zero-extended storage.
        const uint32_t bytes = holes->width;
        const bool is_signed = holes->operation != 0;
        const uint8_t d = holes->destination;
        valid = d < 16;
        records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FNSTCW_M16, base, temporary);
        records[count++] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM16, .reg = scratch, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary };
        records[count++] = (X86Instruction){ .form = X86_FORM_OR_RM32_IMM32, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 0x0c00 };
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM16_R16, .reg = scratch, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 2 };
        records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FLDCW_M16, base, temporary + 2);
        records[count++] = tier1_emit_cfg_x87_load(emitted, a, base, left);
        if (bytes == 8 && !is_signed)
        {
            X86Instruction limit = tier1_emit_cfg_x87_memory(X86_FORM_MOV_RM32_IMM32, base, temporary + 4);
            limit.immediate = 0x5f000000;
            records[count++] = limit;
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FLD_M32, base, temporary + 4);
            records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FUCOMI_ST0_ST, 1);
            records[count++] = (X86Instruction){ .form = X86_FORM_SETBE_RM8, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT };
            records[count++] = (X86Instruction){ .form = X86_FORM_FLDZ };
            records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FCMOVBE_ST0_ST, 1);
            records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FSTP_ST, 1);
            records[count++] = tier1_emit_cfg_x87_register(X86_FORM_FSUBP_ST_ST0, 1);
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FISTP_M64, base, temporary + 8);
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FLDCW_M16, base, temporary);
            records[count++] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM8, .reg = scratch, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT };
            records[count++] = (X86Instruction){ .form = X86_FORM_SHL_RM64_IMM8, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 63 };
            records[count++] = (X86Instruction){ .form = X86_FORM_XOR_R64_RM64, .reg = scratch, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
            records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = d, .rm = scratch, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        else
        {
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FISTP_M64, base, temporary + 8);
            records[count++] = tier1_emit_cfg_x87_memory(X86_FORM_FLDCW_M16, base, temporary);
            const uint16_t form = bytes == 8 ? X86_FORM_MOV_R64_RM64 : bytes == 4 ? X86_FORM_MOV_R32_RM32 : bytes == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
            records[count++] = (X86Instruction){ .form = form, .reg = d, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = temporary + 8 };
        }
    }
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    error = error != TIER1_EMIT_OK || count <= BUSTER_TIER1_EMIT_X87_RECORDS ? error : TIER1_EMIT_INVALID;
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < count; r += 1)
    {
        error = tier1_emit_cfg_x87_append(emitted, buffer, records[r]) ? error : TIER1_EMIT_MEMORY;
    }
    return error;
}

// A free register of `free` (a bit per register), the lowest, taken out of it; UINT8_MAX where there is none.
static uint8_t tier1_emit_cfg_take(uint32_t* free)
{
    const uint8_t reg = *free != 0 ? (uint8_t)__builtin_ctz(*free) : UINT8_MAX;
    *free &= *free - 1;
    return reg;
}

// A copy of a machine row's floating operand or result (#141): movaps between two registers below 16, a legacy form every
// target has (#1281), else vmovdqa64 of the xmm registers, one of them past 15 (the zmm registers past the vector roles').
static X86Instruction tier1_emit_cfg_row_float_move(uint8_t destination, uint8_t source)
{
    return destination < 16 && source < 16 ? tier1_select_float_move(destination, source) : tier1_select_register_record(X86_FORM_VMOVDQA64_X_XM128, destination, source);
}

// A machine row's record placed (#1283): as tier0_operation_place places it, but each VECTOR(j) role, in a field or as an
// /is4 operand, in vectors[j], tier1_emit_cfg_row's placement of it, rather than in a run from `placement`'s first; but
// VECTOR(0) in `early` before the record `split` (an index plus one, 0 for none) and in that record's fields but its
// write's (at `position`): the first operand read in its own register until the first write of the result.
static X86Instruction tier1_emit_cfg_row_place(X86Instruction original, uint32_t r, const Tier0Placement* placement, const uint8_t* vectors, uint32_t split, uint8_t early, uint32_t position)
{
    X86Instruction placed = tier0_operation_place(original, placement);
    Tier0Fields fields = tier0_operation_fields(&original);
    Tier0Fields placed_fields = tier0_operation_fields(&placed);
    for (uint32_t k = 0; k < fields.count && k < placed_fields.count; k += 1)
    {
        const uint8_t role = *fields.bytes[k];
        const uint32_t at = fields.bytes[k] == &original.reg ? BUSTER_X86_ROLE_REG : fields.bytes[k] == &original.vvvv ? BUSTER_X86_ROLE_VVVV : BUSTER_X86_ROLE_RM;
        const bool before = (role & 15) == 0 && (r + 1 < split || (r + 1 == split && (at != position || fields.bytes[k] == &original.index)));
        if (tier0_vector_register_class(fields.classes[k]) && role < BUSTER_TIER0_ROLE_FLOATING(0))
        {
            *placed_fields.bytes[k] = before ? early : vectors[role & 15];
        }
    }
    if (fields.is4)
    {
        const uint32_t j = (uint32_t)original.immediate >> 4 & 15;
        const uint32_t reg = j == 0 && r + 1 <= split ? early : vectors[j];
        placed.immediate = (int32_t)((reg << 4) | ((uint32_t)original.immediate & 15));
    }
    return placed;
}

// A machine row's records (#141, TIER1_RULE_ROW), placed as Tier 0 places a row (tier0_operation_place) in registers the
// allocation leaves the node, at its definition phase: every operand in its register (the plan's effective uses, the
// call's arguments), its definition's register the destination's. Its vector roles are placed one at a time (#1283,
// tier1_emit_cfg_row_place): a vector result's VECTOR(0) in the destination, and a vector operand's VECTOR(j) in its own
// register, where nothing else there is read after they are written (below, and the selection guide's "Machine rows");
// else VECTOR(j) is zmm16 + j, which no value takes and an EVEX form names (tier1_select_row_supported), or for a row
// with a VEX or legacy form's vector register, and at a target without AVX512VL for a row with a vector narrower than
// 64 bytes or at a target without AVX512F (#1281), a free xmm register below 16 (xmm15 among them, the class's
// scratch), each taken when its role is placed, a vector operand moved there first at its width and the result moved
// from VECTOR(0) into the destination last; its floating roles the zmm registers past zmm16's, or below 16 for a row
// that names one in a VEX or legacy form's field and at a target without AVX512VL (#1281): a written floating operand's
// a free xmm register no vector role takes (nor the destination a vector result is written in), and the result's the
// destination where no other operand is there, else such a free register; the vector roles and the floating ones below
// 16 take their registers from one pool. An integer, mask or floating operand its records don't write (tier1_select_row_written) stays in
// its register, and one they write is copied into a free register of its class first.
// Its result shares a register with its class's first operand, as Tier 0's placement has them (a row may read either as
// the other), that operand copied there first: an integer's or a mask's its destination where no other operand is there,
// else a free register; a floating one's past the vector roles, or below 16 as above. Its scratch roles take free
// registers: the caller-saved general registers (r11 among them) and k1 to k7 that hold no value live past the node and
// no operand. The opmask
// registers it names by number, its own scratch, take free ones too (tier1_select_row_rename), but a pair's
// (VP2INTERSECT's), which must be free where it is. Short of free opmask registers, it borrows those of masks live past
// it, saved and reloaded around it; short of others, the row is refused (TIER1_EMIT_UNSUPPORTED: its function is Tier
// 0's). A move into place never overwrites another's source: its destination is free or a vector role's.
// `sources` are the operands' registers at the node's use phase, as a kernel's holes are, before its definition phase's
// moves, which may take an operand's register for the definition or store an operand live past the node into its slot.
// An SSE carrier (#1280: a vector of 4 or 8 bytes, an f32 or f64 in the IR) has zeros past its bytes in its role's
// register, as Tier 0's operand has them (its reload's vmovd or vmovq), which rows rely on: each carrier operand moves
// into its role zero-extended (tier1_select_carrier_move), whatever made its value, a parameter or a call's result
// (whose upper lanes the convention leaves undefined) among them. A row's carrier result moves into its destination
// zero-extended too, at no cost (it replaces a whole register's copy; a zero-record shuffle's result is its wider
// operand), so that a carrier a row, a load or a constant makes has zeros past its bytes in its own register.
static Tier1EmitError tier1_emit_cfg_row(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, const Tier1EmitTransitions* transitions, const uint8_t* sources, uint32_t node, uint32_t use, Tier1EmitBuffer* buffer)
{
    const IrFunction* function = plan->function;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t index = (uint32_t)plan->patterns[node].immediate;
    const Tier0Operation* row = index < function->row_count ? &function->rows[index] : 0;
    const uint32_t first = plan->effective_use_offsets[node];
    Tier1EmitError error = row != 0 && plan->effective_use_offsets[node + 1] - first == row->operand_count ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    // The registers holding values live past the node, and the operands', which the row mustn't write.
    uint32_t busy_general = 0;
    uint32_t busy_masks = 0;
    uint32_t busy_vectors = 0;
    for (uint32_t location = 0; error == TIER1_EMIT_OK && location < BUSTER_TIER1_EMIT_MASK_LOCATION + 8; location += 1)
    {
        const bool general = location < BUSTER_TIER1_EMIT_GENERAL_REGISTERS;
        const bool mask = location >= BUSTER_TIER1_EMIT_MASK_LOCATION;
        const bool vector = location >= BUSTER_TIER1_EMIT_VECTOR_LOCATION && location < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 32;
        const uint32_t owner = general || mask || vector ? transitions->owners[location] : BUSTER_IR_NONE;
        const bool live = owner != BUSTER_IR_NONE && owner != node && transitions->registers[owner] == location && tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        busy_general |= live && general ? 1u << location : 0;
        busy_masks |= live && mask ? 1u << (location - BUSTER_TIER1_EMIT_MASK_LOCATION) : 0;
        busy_vectors |= live && vector ? 1u << (location - BUSTER_TIER1_EMIT_VECTOR_LOCATION) : 0;
    }
    // The masks live past the node, which the row may borrow the registers of where too few are free, and the vector
    // registers of values live past it, which its result can't be written in.
    const uint32_t live_masks = busy_masks;
    const uint32_t live_vectors = busy_vectors;
    for (uint32_t j = 0; error == TIER1_EMIT_OK && j < row->operand_count; j += 1)
    {
        const uint32_t value = plan->effective_use_values[first + j];
        const uint8_t location = sources[j];
        error = tier1_emit_cfg_holds(emitted, value, location) ? error : TIER1_EMIT_ALLOCATION;
        busy_general |= location < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << location : 0;
        busy_vectors |= location >= BUSTER_TIER1_EMIT_VECTOR_LOCATION && location < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 32 ? 1u << (location - BUSTER_TIER1_EMIT_VECTOR_LOCATION) : 0;
        busy_masks |= location >= BUSTER_TIER1_EMIT_MASK_LOCATION && location < BUSTER_TIER1_EMIT_MASK_LOCATION + 8 ? 1u << (location - BUSTER_TIER1_EMIT_MASK_LOCATION) : 0;
    }
    const uint32_t result_type = function->type[node];
    const uint8_t destination = result_type != IR_VOID ? transitions->registers[node] : UINT8_MAX;
    error = error != TIER1_EMIT_OK || result_type == IR_VOID || tier1_emit_cfg_holds(emitted, node, destination) ? error : TIER1_EMIT_ALLOCATION;
    const uint32_t caller_saved = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_R8) |
                                  (1u << X86_REGISTER_R9) | (1u << X86_REGISTER_R10) | (1u << X86_REGISTER_R11);
    uint32_t free_general = caller_saved & ~busy_general;
    uint32_t free_masks = BUSTER_TIER1_EMIT_MASK_REGISTERS & ~busy_masks;
    const uint32_t written = error == TIER1_EMIT_OK ? tier1_select_row_written(row) : 0;
    const Tier0Reach reach = error == TIER1_EMIT_OK ? tier0_operation_reach(row) : (Tier0Reach){ 0, 0, 0 };
    const Tier0Masks masks = error == TIER1_EMIT_OK ? tier0_operation_masks(row) : (Tier0Masks){ 0 };
    Tier0Placement placement;
    memset(&placement, 0, sizeof(placement));
    placement.vector = 16;
    // A target without AVX512VL (#1281) has no EVEX move of an xmm or a ymm register, and one without AVX512F no register
    // past xmm15 at all; one with AVX512F but not VL (Knights Landing's) has zmm16 and on and EVEX's 512-bit moves.
    const bool vex = emitted->prepared.vex;
    // Its moves' encoding: a legacy SSE target's 16-byte vectors move by movaps (#141's SSE targets).
    const uint32_t encoding = tier1_emit_encoding(&emitted->prepared);
    // A row's vector roles are below 16 where it has a VEX or legacy form's vector register, and at a target without
    // AVX512VL where it has a vector narrower than 64 bytes (whose moves into zmm16 and on would be VL's) or the target has
    // no zmm16 (#1281's review: a 64-byte EVEX row at an AVX512F target keeps zmm16 and on). A row that names a floating
    // role in a VEX or legacy form's field (F16C's conversions), and any row at a target without AVX512VL (whose floating
    // copies past 15 would be VL's), has its floating roles below 16 too. Either kind takes free xmm registers below 16
    // one at a time, from one pool, where it doesn't stay in place (#1283): not a contiguous run, which register pressure
    // could leave none of (#1281's review).
    const bool low_vectors = error == TIER1_EMIT_OK && reach.vectors != 0 && !tier1_select_row_upper_vectors(row, encoding, emitted->prepared.zmm);
    const bool low_floats = error == TIER1_EMIT_OK && (vex || tier1_select_row_low_floating(row));
    error = error == TIER1_EMIT_OK && (busy_vectors & tier1_emit_cfg_row_reserved(&emitted->prepared, row)) != 0 ? TIER1_EMIT_ALLOCATION : error;
    uint32_t free_low = low_vectors || low_floats ? 0xffffu & ~busy_vectors : 0;
    // Each VECTOR(j) role's register (#1283), placed one at a time: where no role below takes another, zmm16 + j, which
    // no value is in, or for a role below 16 a free register taken when it is placed (UINT8_MAX until then).
    const Tier1SelectRowVectors roles = error == TIER1_EMIT_OK ? tier1_select_row_vectors(row) : (Tier1SelectRowVectors){ 0 };
    uint8_t vectors[16];
    for (uint32_t j = 0; j < 16; j += 1)
    {
        vectors[j] = low_vectors ? UINT8_MAX : (uint8_t)(placement.vector + j);
    }
    uint8_t next_floating = (uint8_t)(16 + reach.vectors);
    X86Instruction records[BUSTER_TIER1_EMIT_ROW_RECORDS];
    uint32_t count = 0;
    // The destination is the result's alone: no operand moves into it, and no scratch role takes it.
    const uint32_t result_class = row != 0 ? row->result_class : BUSTER_TYPED_CLASS_VOID;
    free_general &= error == TIER1_EMIT_OK && tier0_class_integer(result_class) ? ~(1u << destination) : UINT32_MAX;
    free_masks &= error == TIER1_EMIT_OK && tier0_class_mask(result_class) ? ~(1u << (destination - BUSTER_TIER1_EMIT_MASK_LOCATION)) : UINT32_MAX;
    // The opmask registers the row names by number, k1 to k7, the row's own scratch (a row with a mask operand or result
    // names none: tier0_operation_valid), each renamed to a free one, where it has a pair's (VP2INTERSECT's k and k + 1),
    // which must stay in place, nothing live in it, and k0 kept: in a decoration it is no mask at all.
    uint8_t renamed[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    const bool paired = error == TIER1_EMIT_OK && tier1_select_row_pairs(row);
    // Where too few opmask registers are free for what the row takes of them (its renamed registers, its mask scratch, a
    // written mask operand's copy and a mask result away from its destination), it borrows registers of masks live past
    // it, the lowest first, each saved in its row before the row (tier1_emit_cfg_saves gave them rows) and reloaded
    // after it.
    uint32_t mask_need = paired ? 0 : (uint32_t)__builtin_popcount(masks.named);
    mask_need += masks.scratch;
    mask_need += error == TIER1_EMIT_OK && tier0_class_mask(result_class) && ((busy_masks >> (destination - BUSTER_TIER1_EMIT_MASK_LOCATION)) & 1) != 0 ? 1 : 0;
    for (uint32_t j = 0; error == TIER1_EMIT_OK && j < row->operand_count; j += 1)
    {
        mask_need += tier0_class_mask(row->operand_classes[j]) && ((written >> j) & 1) != 0 ? 1 : 0;
    }
    uint32_t borrowed = 0;
    uint32_t lendable = live_masks & emitted->prepared.mask_save_mask & ~masks.named;
    for (uint32_t j = 0; j < row->operand_count && error == TIER1_EMIT_OK; j += 1)
    {
        lendable &= sources[j] >= BUSTER_TIER1_EMIT_MASK_LOCATION && sources[j] < BUSTER_TIER1_EMIT_MASK_LOCATION + 8 ? ~(1u << (sources[j] - BUSTER_TIER1_EMIT_MASK_LOCATION)) : UINT32_MAX;
    }
    while (error == TIER1_EMIT_OK && (uint32_t)__builtin_popcount(free_masks) < mask_need && lendable != 0)
    {
        const uint32_t k = (uint32_t)__builtin_ctz(lendable);
        lendable &= lendable - 1;
        const uint32_t owner = transitions->owners[BUSTER_TIER1_EMIT_MASK_LOCATION + k];
        const uint32_t save_row = tier1_emit_cfg_mask_save_row(emitted, k);
        error = owner != BUSTER_IR_NONE && save_row < emitted->frame.count ? error : TIER1_EMIT_FRAME;
        if (error == TIER1_EMIT_OK)
        {
            records[count++] = (X86Instruction){ .displacement = emitted->frame.offset[save_row], .reg = (uint8_t)k, .rm = (uint8_t)emitted->frame.base_register, .mode = BUSTER_X86_MEMORY_BASE,
                                                 .form = tier1_select_mask_forms(emitted->prepared.sizes[owner])->store };
            borrowed |= 1u << k;
            free_masks |= 1u << k;
        }
    }
    error = error == TIER1_EMIT_OK && (!paired || (masks.named & busy_masks) == 0) ? error : error == TIER1_EMIT_OK ? TIER1_EMIT_UNSUPPORTED : error;
    free_masks &= paired ? ~masks.named : UINT32_MAX;
    for (uint32_t k = 1; error == TIER1_EMIT_OK && !paired && k < 8; k += 1)
    {
        renamed[k] = ((masks.named >> k) & 1) != 0 ? tier1_emit_cfg_take(&free_masks) : (uint8_t)k;
        error = renamed[k] != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
    }
    // The result's role, which Tier 0 places in its class's first operand's register (tier0_operation_placement and
    // tier0_operation_mask_place: INTEGER_RESULT at the first integer stack operand's, where that is an integer, since a
    // vector's place on that stack is no general register of its own (#1284); FLOATING_RESULT at the first floating one's,
    // MASK_RESULT at the first mask one's), so that a row may read it as that operand, or that operand as its result once
    // written: here both are one register, that operand copied into it first. An integer's or a mask's is its destination
    // where no other operand is there (the allocation gave the destination to the node: it holds nothing live past it),
    // else a free register moved from last; a floating one's past the floating operands', moved into the destination last;
    // a vector's VECTOR(0).
    uint32_t aliased = UINT32_MAX;
    bool searching = true;
    for (uint32_t j = 0; error == TIER1_EMIT_OK && searching && j < row->operand_count; j += 1)
    {
        const uint32_t class = row->operand_classes[j];
        const bool integer = tier0_class_integer(result_class) && tier0_stack(class) == 1;
        const bool floating = tier0_class_floating(result_class) && tier0_class_floating(class);
        const bool mask = tier0_class_mask(result_class) && tier0_class_mask(class);
        searching = !integer && !floating && !mask;
        aliased = (integer && tier0_class_integer(class)) || floating || mask ? j : aliased;
    }
    // A destination another operand is in can't be the result's: the row may write the result before it reads that one.
    bool shared = false;
    for (uint32_t j = 0; error == TIER1_EMIT_OK && result_class != BUSTER_TYPED_CLASS_VOID && j < row->operand_count; j += 1)
    {
        shared |= j != aliased && sources[j] == destination;
    }
    bool result_moves = false;
    uint8_t result_register = UINT8_MAX;
    // Whether a vector result is written in the destination itself (#1283), and its first write, where the first operand
    // is read in its own register (`early`) up to it.
    bool in_place = false;
    uint32_t split = 0;
    uint8_t early = 0;
    if (error == TIER1_EMIT_OK && tier0_class_integer(result_class))
    {
        const bool kept = !shared && (((busy_general >> destination) & 1) == 0 || (aliased != UINT32_MAX && sources[aliased] == destination));
        result_register = kept ? destination : tier1_emit_cfg_take(&free_general);
        result_moves = !kept;
        placement.integer_result = result_register;
    }
    else if (error == TIER1_EMIT_OK && tier0_class_mask(result_class))
    {
        const uint8_t k = (uint8_t)(destination - BUSTER_TIER1_EMIT_MASK_LOCATION);
        const bool kept = !shared && ((busy_masks >> k) & 1) == 0 ? ((masks.named >> k) & 1) == 0 : !shared && aliased != UINT32_MAX && sources[aliased] == destination;
        result_register = kept ? k : tier1_emit_cfg_take(&free_masks);
        result_moves = !kept;
        placement.mask_result = result_register;
    }
    else if (error == TIER1_EMIT_OK && tier0_class_floating(result_class) && low_floats)
    {
        // Below 16 (#1281), the destination itself where no other operand is there, which no vector role then takes,
        // else a free xmm register, moved into the destination last.
        const uint8_t x = (uint8_t)(destination - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
        const bool kept = !shared && (((busy_vectors >> x) & 1) == 0 || (aliased != UINT32_MAX && sources[aliased] == destination));
        free_low &= ~(1u << x);
        result_register = kept ? x : tier1_emit_cfg_take(&free_low);
        result_moves = !kept;
        placement.floating_result = result_register;
    }
    else if (error == TIER1_EMIT_OK && tier0_class_floating(result_class))
    {
        result_register = next_floating++;
        result_moves = true;
        placement.floating_result = result_register;
    }
    else if (error == TIER1_EMIT_OK && tier0_class_vector(result_class))
    {
        // A vector's result is VECTOR(0), written in the destination itself (#1283) where nothing else there is read
        // after it is written, and every record that writes VECTOR(0) is a VEX or EVEX form's, which zeroes the register
        // past its field: one no wider than the result leaves the bits past it zero, as the move at its width left them;
        // a wider one's are cleared by a move of the destination into itself at the result's width last. The first
        // operand, VECTOR(0)'s value before then, is copied into the destination first; or, where the first write is a
        // pure one (tier1_select_row_vectors) and nothing reads the operand past its width up to it, it is read in its
        // own register up to that write (`split`), which writes the destination.
        // Nothing else is there: no value live past the node, no floating operand, and no other operand where the first
        // is copied in (its copy, before the others' moves, would overwrite it before it is read or moved); else no
        // vector operand that stays in its own register there whose role the records name after the first write of
        // VECTOR(0) (an operand dying at the node, whose register the allocation gave the result, is read first), or in
        // that record where its destination can't be one of its sources (a gather's or a complex multiplication's); no
        // other role below 16 then takes it. Else VECTOR(0) is a register of its own (zmm16, a free one below 16, or the
        // first operand's where nothing writes it), moved into the destination at the result's width last.
        const uint8_t d = (uint8_t)(destination - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
        const bool first_vector = row->operand_count != 0 && tier0_class_vector(row->operand_classes[0]);
        const bool clean = (roles.written & 1) != 0 && (roles.legacy & 1) == 0;
        const bool apart = first_vector && sources[0] != destination;
        const bool splits = apart && (roles.pure & 1) != 0 && roles.widest_early[0] <= row->operand_bytes[0];
        const bool copied_in = apart && !splits;
        const uint32_t write = roles.first_write[0];
        bool blocked = ((live_vectors >> d) & 1) != 0;
        for (uint32_t j = first_vector ? 1 : 0; clean && j < row->operand_count; j += 1)
        {
            const uint32_t class = row->operand_classes[j];
            const bool own = tier0_class_vector(class) && ((roles.written >> j) & 1) == 0 && roles.widest_early[j] <= row->operand_bytes[j];
            const uint32_t named = roles.last_named[j];
            const bool late = named > write || (named == write && ((roles.exclusive >> (write - 1)) & 1) != 0);
            blocked |= sources[j] == destination && (copied_in || tier0_class_floating(class) || (own && late));
        }
        in_place = !blocked && clean;
        // Below 16 (#1281), the destination taken by the result written in place; else VECTOR(0) takes a free register
        // only where it is placed and doesn't stay in the first operand's (#1293's review: a row that never writes it
        // reads it there), in the operand loop or after it.
        free_low &= in_place ? ~(1u << d) : UINT32_MAX;
        vectors[0] = in_place ? d : vectors[0];
        split = in_place && splits ? write : 0;
        early = split != 0 ? (uint8_t)(sources[0] - BUSTER_TIER1_EMIT_VECTOR_LOCATION) : 0;
        result_register = vectors[0];
        result_moves = !in_place || roles.widest_written[0] > row->result_bytes;
    }
    // A vector result's VECTOR(0) below 16 may be placed later (low_vectors).
    error = error == TIER1_EMIT_OK && (result_class == BUSTER_TYPED_CLASS_VOID || result_register != UINT8_MAX || (low_vectors && tier0_class_vector(result_class))) ? error : error == TIER1_EMIT_OK ? TIER1_EMIT_UNSUPPORTED : error;
    // The moves into place and out of it (#1283, Tier1EmitResult.row_moves): the operands' and the result's.
    uint32_t moves = 0;
    for (uint32_t j = 0; error == TIER1_EMIT_OK && j < row->operand_count; j += 1)
    {
        const uint32_t class = row->operand_classes[j];
        const uint8_t location = sources[j];
        // The operand the result shares a register with is copied into it, wherever it isn't already.
        const bool alias = j == aliased;
        const bool writes = ((written >> j) & 1) != 0 || alias;
        if (tier0_class_vector(class))
        {
            // VECTOR(j) (#1283): the operand's own register where nothing reads it past its width up to the role's first
            // write (whose bits the move at its width would have zeroed: the result's move reads VECTOR(0) at the
            // result's) and no record writes it, or they write it by VEX and EVEX forms alone (which zero it past their
            // fields) and it dies at the node, no other operand in its register, not the destination the result is
            // written in; the destination where the result is written in place, the operand copied in first (into
            // itself, where it is there but read past its width), or read in its own register up to the result's first
            // write (`split`); else the role's register, the operand copied in.
            const uint8_t own = (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
            const bool result_role = j == 0 && tier0_class_vector(result_class);
            const bool placed = result_role && in_place;
            const uint32_t read = result_role && !placed && row->result_bytes > roles.widest_early[0] ? row->result_bytes : roles.widest_early[j];
            const bool narrow = read <= row->operand_bytes[j];
            bool alone = ((live_vectors >> own) & 1) == 0 && ((roles.legacy >> j) & 1) == 0 && !result_role && !(in_place && own == vectors[0]);
            for (uint32_t k = 0; k < row->operand_count; k += 1)
            {
                alone &= k == j || sources[k] != location;
            }
            const bool stays = narrow && (((roles.written >> j) & 1) == 0 || alone);
            // A role below 16 that doesn't stay in place takes a free register now (#1281's review).
            vectors[j] = !placed && !stays && vectors[j] == UINT8_MAX ? tier1_emit_cfg_take(&free_low) : vectors[j];
            error = vectors[j] != UINT8_MAX || placed || stays ? error : TIER1_EMIT_UNSUPPORTED;
            const uint8_t reg = placed ? vectors[0] : stays ? own : vectors[j];
            const bool moved = (reg != own || !narrow) && !(placed && split != 0);
            vectors[j] = reg;
            result_register = result_role ? reg : result_register;
            // An SSE carrier (#1280) copied zero-extended: its value's register may hold anything past its bytes (a
            // parameter's or a call result's, whose upper lanes the convention leaves undefined), which every vector
            // field reads past, so it never stays in its own register. Both by VEX forms at a target without AVX512VL.
            records[count] = row->operand_bytes[j] < 16 ? tier1_select_carrier_move(reg, own, row->operand_bytes[j], vex) :
                                                          tier1_select_register_record(tier1_select_vector_forms(row->operand_bytes[j], encoding)->move, reg, own);
            count += moved;
            moves += moved;
        }
        else if (tier0_class_floating(class))
        {
            const uint8_t own = (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
            const uint8_t reg = alias ? result_register : writes ? (low_floats ? tier1_emit_cfg_take(&free_low) : next_floating++) : own;
            error = reg != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
            records[count] = tier1_emit_cfg_row_float_move(reg, own);
            count += reg != own;
            moves += reg != own;
            placement.operands[j] = reg;
        }
        else if (tier0_class_mask(class))
        {
            const uint8_t own = (uint8_t)(location - BUSTER_TIER1_EMIT_MASK_LOCATION);
            const uint8_t reg = alias ? result_register : writes ? tier1_emit_cfg_take(&free_masks) : own;
            error = reg != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
            records[count] = tier1_select_register_record(tier1_select_mask_forms(row->operand_bytes[j])->move, reg, own);
            count += reg != own;
            moves += reg != own;
            placement.masks[j] = reg;
        }
        else
        {
            const uint8_t reg = alias ? result_register : writes ? tier1_emit_cfg_take(&free_general) : location;
            error = reg != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
            records[count] = tier1_select_move(8, reg, location);
            count += reg != location;
            moves += reg != location;
            placement.operands[j] = reg;
        }
    }
    error = error == TIER1_EMIT_OK && next_floating <= 32 ? error : error == TIER1_EMIT_OK ? TIER1_EMIT_UNSUPPORTED : error;
    // The vector roles below 16 no operand placed (a row's own vector scratch, and a vector result's VECTOR(0) where its
    // first operand is no vector), each a free register; the result's register, VECTOR(0)'s.
    for (uint32_t j = 0; error == TIER1_EMIT_OK && j < reach.vectors; j += 1)
    {
        vectors[j] = vectors[j] == UINT8_MAX ? tier1_emit_cfg_take(&free_low) : vectors[j];
        error = vectors[j] != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
    }
    result_register = error == TIER1_EMIT_OK && tier0_class_vector(result_class) ? vectors[0] : result_register;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < reach.scratch; k += 1)
    {
        placement.scratch[k] = tier1_emit_cfg_take(&free_general);
        error = placement.scratch[k] != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < masks.scratch; k += 1)
    {
        placement.mask_scratch[k] = tier1_emit_cfg_take(&free_masks);
        error = placement.mask_scratch[k] != UINT8_MAX ? error : TIER1_EMIT_UNSUPPORTED;
    }
    // The row's own records, after the borrowed masks' saves and the operands' moves into place.
    const uint32_t row_first = count;
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < row->record_count; r += 1)
    {
        const X86Instruction placed = tier1_emit_cfg_row_place(row->records[r], r, &placement, vectors, split, early, roles.write_position[0]);
        records[count++] = masks.named != 0 ? tier1_select_row_rename(placed, row->records[r], renamed) : placed;
    }
    // The placement, for the code's certificate (#1284): each role's register as the records name it.
    if (error == TIER1_EMIT_OK && emitted->row_placements != 0)
    {
        Tier1RowPlacement* claimed = &emitted->row_placements[node];
        memset(claimed, 0, sizeof(*claimed));
        memcpy(claimed->vectors, vectors, sizeof(claimed->vectors));
        memcpy(claimed->operands, placement.operands, sizeof(claimed->operands));
        memcpy(claimed->masks, placement.masks, sizeof(claimed->masks));
        memcpy(claimed->scratch, placement.scratch, sizeof(claimed->scratch));
        memcpy(claimed->mask_scratch, placement.mask_scratch, sizeof(claimed->mask_scratch));
        memcpy(claimed->renamed, renamed, sizeof(claimed->renamed));
        claimed->integer_result = placement.integer_result;
        claimed->floating_result = placement.floating_result;
        claimed->mask_result = placement.mask_result;
        claimed->early = early;
        claimed->split = (uint8_t)split;
        claimed->position = split != 0 ? (uint8_t)roles.write_position[0] : 0;
    }
    if (error == TIER1_EMIT_OK && result_moves)
    {
        moves += 1;
        const uint8_t vector_destination = (uint8_t)(destination - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
        records[count++] = tier0_class_vector(result_class) && row->result_bytes < 16 ? tier1_select_carrier_move(vector_destination, result_register, row->result_bytes, vex) :
                           tier0_class_vector(result_class)   ? tier1_select_register_record(tier1_select_vector_forms(row->result_bytes, encoding)->move, vector_destination, result_register) :
                           tier0_class_floating(result_class) ? tier1_emit_cfg_row_float_move(vector_destination, result_register) :
                           tier0_class_mask(result_class)     ? tier1_select_register_record(tier1_select_mask_forms(row->result_bytes)->move, (uint8_t)(destination - BUSTER_TIER1_EMIT_MASK_LOCATION), result_register) :
                                                                tier1_select_move(tier0_class_bytes(result_class) == 8 ? 8 : 4, destination, result_register);
    }
    // The borrowed registers' masks back.
    for (uint32_t left = error == TIER1_EMIT_OK ? borrowed : 0; left != 0; left &= left - 1)
    {
        const uint32_t k = (uint32_t)__builtin_ctz(left);
        const uint32_t owner = transitions->owners[BUSTER_TIER1_EMIT_MASK_LOCATION + k];
        const uint32_t save_row = tier1_emit_cfg_mask_save_row(emitted, k);
        records[count++] = (X86Instruction){ .displacement = emitted->frame.offset[save_row], .reg = (uint8_t)k, .rm = (uint8_t)emitted->frame.base_register, .mode = BUSTER_X86_MEMORY_BASE,
                                             .form = tier1_select_mask_forms(emitted->prepared.sizes[owner])->load };
    }
    // The borrowed masks' saves and the operands' moves are moves of values at the node's definition phase; the row's own
    // records, its result's move and the borrowed masks' reloads are its node's (#1284).
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < count; r += 1)
    {
        if (r == row_first)
        {
            tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, use + 1), BUSTER_IR_NONE);
        }
        error = tier1_emit_append(buffer, records[r]) ? error : TIER1_EMIT_MEMORY;
    }
    tier1_emit_mark(buffer, tier1_origin(error == TIER1_EMIT_OK && count == row_first ? TIER1_ORIGIN_MOVE : TIER1_ORIGIN_NODE, error == TIER1_EMIT_OK && count == row_first ? use + 1 : node), BUSTER_IR_NONE);
    emitted->machine_rows += error == TIER1_EMIT_OK ? 1 : 0;
    emitted->row_moves += error == TIER1_EMIT_OK ? moves : 0;
    // The borrowed masks' saves and reloads count as a call's do.
    emitted->call_stores += error == TIER1_EMIT_OK ? (uint32_t)__builtin_popcount(borrowed) : 0;
    emitted->call_loads += error == TIER1_EMIT_OK ? (uint32_t)__builtin_popcount(borrowed) : 0;
    return error;
}

// An edge's long double phis' copies (#141): each phi of the successor whose operand from the predecessor is a value, its
// operand's object into its own, as one parallel copy through the x87 stack: every operand loaded, then every phi's
// object stored in reverse, so that a phi's object read as another's operand is read before it is written. The x87 stack
// holds 8: an edge of more such phis refuses the function (TIER1_EMIT_UNSUPPORTED).
static Tier1EmitError tier1_emit_cfg_x87_phis(const IrFunction* function, Tier1EmitResult* emitted, uint32_t edge, Tier1EmitBuffer* buffer)
{
    const Tier1CfgInput* input = &emitted->cfg_input;
    const uint32_t added = input->block_count - function->block_count;
    // The edge's predecessor: the block whose successors' range holds it.
    uint32_t low = 0;
    uint32_t high = input->block_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = input->successor_offsets[middle] <= edge ? middle : low;
        high = input->successor_offsets[middle] <= edge ? high : middle;
    }
    const uint32_t successor = input->successors[edge];
    const bool ir_edge = low >= added && successor >= added;
    const uint32_t source = low - added;
    const uint32_t target = successor - added;
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    int32_t destinations[8];
    uint32_t copies = 0;
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t node = ir_edge ? function->block_offsets[target] : 0; error == TIER1_EMIT_OK && ir_edge && node < function->block_offsets[target + 1] && function->op[node] == IR_PHI; node += 1)
    {
        uint32_t incoming = BUSTER_IR_NONE;
        // Scalar under MSan, as tier1_emit_cfg_problem's scan of a phi's pairs: MSan reports the undefined lanes of
        // clang's vector form (main's native MSan job on #1204's merge, through test-tier1-wide's join of 200
        // predecessors). Whether the phi is a long double's is the trip count's, not the loop condition's: clang
        // unswitches a loop on an invariant condition, and the copy it vectorizes doesn't keep the pragma.
        const uint32_t pairs = function->type[node] == IR_F80 ? function->b[node] : 0;
#if __has_feature(memory_sanitizer)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
        for (uint32_t pair = 0; pair < pairs; pair += 1)
        {
            const uint32_t* entry = function->extra + function->a[node] + (uint64_t)pair * 2;
            incoming = entry[0] == source ? entry[1] : incoming;
        }
        const bool copied = incoming < function->instruction_count && function->op[incoming] != IR_UNDEF;
        int32_t from = 0;
        error = !copied || copies < 8 ? error : TIER1_EMIT_UNSUPPORTED;
        error = error != TIER1_EMIT_OK || !copied || (tier1_emit_cfg_x87_offset(function, emitted, incoming, &from) && tier1_emit_cfg_x87_offset(function, emitted, node, &destinations[copies])) ? error : TIER1_EMIT_FRAME;
        if (error == TIER1_EMIT_OK && copied)
        {
            error = tier1_emit_cfg_x87_append(emitted, buffer, tier1_emit_cfg_x87_load(emitted, incoming, base, from)) ? error : TIER1_EMIT_MEMORY;
            copies += 1;
        }
    }
    for (uint32_t k = copies; error == TIER1_EMIT_OK && k-- > 0;)
    {
        error = tier1_emit_append(buffer, tier1_emit_cfg_x87_memory(X86_FORM_FSTP_M80, base, destinations[k])) ? error : TIER1_EMIT_MEMORY;
    }
    return error;
}

// The records a function's ABI records take past every node's share (#789): per call with records, two per 8 bytes of
// its stack area and four per argument for its stack aggregates' copies, five per part for its parts' copies and loads,
// and its result's stores and hidden pointer; per return of an aggregate result, its copies and loads; and the entry's
// stores.
// A probed dynamic allocation's records past its rule's (#177): an immediate's move into the destination, and the probes'
// ten for its subtraction's one.
#define BUSTER_TIER1_EMIT_PROBE_RECORDS 10u
#define BUSTER_TIER1_EMIT_PROBE_BRANCHES 3u

// A dynamic allocation under stack-clash protection (#177, the frame's probe_stack, -fstack-clash-protection): the
// selection's records for the rule (tier1_select_expand's), but its subtraction from rsp, which the probed allocation of
// the same bytes replaces, as Tier 0's (tier0_emit_probed_allocation): with the size in a register (the destination, an
// immediate moved there first), while more than a page is left, rsp moved a page and its byte touched, the page taken
// from the size; then, unless nothing is left, rsp moved by the rest and its byte touched. Each page is touched before
// rsp passes it, and so is the allocation's lowest byte, from which the next one's probes start; no byte at or above the
// old rsp, the frame's own, is touched. The size's register is the rule's own, which its last record sets to the
// address. The loop's branches target its own records, in the buffer's columns as the frame's probes' do
// (tier1_frame_emit.c). A function with an allocation keeps no code certificate (TIER1_CERT_EMITTED_DYNAMIC).
static Tier1EmitError tier1_emit_cfg_probed_allocation(const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    const Tier1SelectRecords selected = tier1_select_expand(holes);
    const uint8_t destination = holes->destination;
    Tier1EmitError error = !selected.overflowed && !selected.requested && buffer->branch_count + BUSTER_TIER1_EMIT_PROBE_BRANCHES <= buffer->branch_capacity ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < selected.count; r += 1)
    {
        const X86Instruction* record = &selected.records[r];
        const bool dynamic = record->form == X86_FORM_SUB_R64_RM64 && record->reg == X86_REGISTER_RSP && record->mode == BUSTER_X86_REGISTER_DIRECT;
        const bool constant = (record->form == X86_FORM_SUB_RM64_IMM8 || record->form == X86_FORM_SUB_RM64_IMM32) && record->rm == X86_REGISTER_RSP &&
                              record->mode == BUSTER_X86_REGISTER_DIRECT;
        if (!dynamic && !constant)
        {
            error = tier1_emit_append(buffer, *record) ? error : TIER1_EMIT_MEMORY;
        }
        else
        {
            const uint8_t size = dynamic ? record->rm : destination;
            if (constant)
            {
                // The rounded constant into the destination, which the rule's last record overwrites with the address.
                const X86Instruction move = { .form = X86_FORM_MOV_RM64_IMM32, .rm = destination, .immediate = (int32_t)x86_immediate_value(record->form, record->immediate, 0) };
                error = tier1_emit_append(buffer, move) ? error : TIER1_EMIT_MEMORY;
            }
            const X86Instruction touch = { .form = X86_FORM_OR_RM8_IMM8, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
            const X86Instruction compare = { .form = X86_FORM_CMP_RM64_IMM32, .rm = size, .immediate = (int32_t)BUSTER_TIER1_FRAME_PAGE_BYTES };
            const X86Instruction page = { .form = X86_FORM_SUB_RM64_IMM32, .rm = X86_REGISTER_RSP, .immediate = (int32_t)BUSTER_TIER1_FRAME_PAGE_BYTES };
            const X86Instruction taken = { .form = X86_FORM_SUB_RM64_IMM32, .rm = size, .immediate = (int32_t)BUSTER_TIER1_FRAME_PAGE_BYTES };
            const X86Instruction empty = { .form = X86_FORM_TEST_RM64_R64, .reg = size, .rm = size };
            const X86Instruction rest = { .form = X86_FORM_SUB_RM64_R64, .reg = size, .rm = X86_REGISTER_RSP };
            const uint32_t loop = buffer->count;
            error = error == TIER1_EMIT_OK && tier1_emit_append(buffer, compare) && buffer->count < buffer->capacity ? error : TIER1_EMIT_MEMORY;
            const uint32_t remainder = buffer->branch_count;
            if (error == TIER1_EMIT_OK)
            {
                tier1_frame_emit_branch(buffer, X86_FORM_JBE_REL8, 0);
            }
            error = error == TIER1_EMIT_OK && tier1_emit_append(buffer, page) && tier1_emit_append(buffer, touch) && tier1_emit_append(buffer, taken) &&
                            buffer->count < buffer->capacity ? error : TIER1_EMIT_MEMORY;
            if (error == TIER1_EMIT_OK)
            {
                tier1_frame_emit_branch(buffer, X86_FORM_JMP_REL8, loop);
                buffer->targets[remainder] = buffer->count;
            }
            error = error == TIER1_EMIT_OK && tier1_emit_append(buffer, empty) && buffer->count < buffer->capacity ? error : TIER1_EMIT_MEMORY;
            const uint32_t done = buffer->branch_count;
            if (error == TIER1_EMIT_OK)
            {
                tier1_frame_emit_branch(buffer, X86_FORM_JE_REL8, 0);
            }
            error = error == TIER1_EMIT_OK && tier1_emit_append(buffer, rest) && tier1_emit_append(buffer, touch) ? error : TIER1_EMIT_MEMORY;
            if (error == TIER1_EMIT_OK)
            {
                buffer->targets[done] = buffer->count;
            }
        }
    }
    return error;
}

// A node's holes from `registers`, by node, as the emitter's view has them (#939: the certificate of the code builds them
// from the allocation's): an allocation rounds to the alignment rsp keeps (#141), its calls' past 16 bytes.
static Tier1SelectHoles tier1_emit_cfg_holes(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const uint8_t* registers, uint32_t node)
{
    Tier1SelectHoles holes = tier1_select_holes(plan, registers, node);
    holes.scale = holes.rule == TIER1_RULE_ALLOCA || holes.rule == TIER1_RULE_ALLOCA_IMMEDIATE ? tier1_emit_cfg_stack_alignment(emitted) : holes.scale;
    holes.vex = emitted->prepared.vex;
    holes.unsigned_dword_memory_forms = emitted->prepared.unsigned_dword_memory_forms;
    holes.unsigned_i64_float_forms = emitted->prepared.unsigned_i64_float_forms;
    holes.avx2 = emitted->prepared.avx2;
    holes.sse = emitted->prepared.sse;
    // A floating select's blend (#2842) takes k1 at a target with AVX512VL in a function with no mask values, where no
    // opmask register holds a value between its nodes.
    holes.opmask = !emitted->prepared.vex && emitted->prepared.mask_columns == 0 ? 1 : 0;
    return holes;
}

// A node's own records by its rule, its holes `holes` (#939: the certificate of the code regenerates them so, from the
// allocation's registers, and compares them with the emitted ones): its expansion, with a frame object's address and
// offsets patched in, and for a symbol's address, or a memory operand that folds one (#2573), the reference of its one
// record that names it (`reference`, where `referenced`, its instruction the caller's). A return's ret is the last
// record, and its epilogue the caller's to put before it.
static Tier1EmitError tier1_emit_cfg_kernel(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const Tier1SelectHoles* holes, uint32_t node, Tier1SelectRecords* records, LinkReference* reference, bool* referenced)
{
    const IrFunction* function = plan->function;
    // Resolve a folded frame address before expansion, so comparisons and two-operand arithmetic see the actual
    // base when checking destination aliases. A comparison's memory access precedes its SETcc or branches.
    const Tier1SelectPattern* pattern = &plan->patterns[node];
    // A folded symbol (#2573) is rip-relative: no base or index register for an alias check to see.
    const bool symbol = pattern->rip && pattern->stack_object != BUSTER_IR_NONE;
    const bool object = pattern->stack_object != BUSTER_IR_NONE && !symbol;
    int32_t object_offset = 0;
    int32_t displacement = 0;
    const bool placed = !object || (tier1_emit_cfg_object(function, emitted, pattern->stack_object, &object_offset) &&
                                   !__builtin_add_overflow(object_offset, pattern->displacement, &displacement));
    Tier1SelectHoles object_holes;
    if (object && placed)
    {
        object_holes = *holes;
        object_holes.memory.rm = (uint8_t)emitted->frame.base_register;
        object_holes.memory.displacement = displacement;
        holes = &object_holes;
    }
    Tier1SelectRecords selected_records = tier1_select_expand(holes);
    Tier1EmitError error = !placed ? TIER1_EMIT_FRAME : selected_records.overflowed ? TIER1_EMIT_INVALID : TIER1_EMIT_OK;
    *referenced = false;
    // A memory operand's folded symbol (#2573): the one rip-relative record its expansion makes, marked for its PC32,
    // whose addend is the symbol's offset; link_add_code_fixups takes off the distance to the instruction's end, an
    // immediate's bytes after the field among it (cmp byte ptr [rip + x], 3's). Its displacement is the reference's, as
    // a recomputed symbol's lea's is (tier1_rematerialize). Only a symbol this function's column reaches directly
    // folds, as selection's did (tier1_select_direct_symbol).
    if (symbol && error == TIER1_EMIT_OK)
    {
        const uint32_t target = pattern->stack_object;
        const uint8_t access = target < emitted->symbol_access_count ? emitted->symbol_access[target] : BUSTER_TIER1_ACCESS_DIRECT;
        uint32_t at = UINT32_MAX;
        uint32_t found = 0;
        for (uint32_t r = 0; r < selected_records.count; r += 1)
        {
            const bool rip = (selected_records.records[r].mode & (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_RIP)) == (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_RIP);
            at = rip ? r : at;
            found += rip;
        }
        error = found == 1 && access == BUSTER_TIER1_ACCESS_DIRECT ? error : TIER1_EMIT_INVALID;
        if (error == TIER1_EMIT_OK)
        {
            selected_records.records[at].form |= BUSTER_X86_FORM_FIXUP;
            selected_records.records[at].displacement = 0;
            *reference = (LinkReference){
                .instruction = at,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_PC32,
                .target = target,
                .addend = pattern->displacement,
            };
            *referenced = true;
        }
    }
    // A frame object's address, from the frame's base at the object's offset; an overflow area's from the incoming frame
    // (#789).
    if (plan->patterns[node].rule == TIER1_RULE_STACK_ADDRESS)
    {
        const uint32_t object = (uint32_t)plan->patterns[node].immediate;
        uint8_t base = (uint8_t)emitted->frame.base_register;
        uint64_t past = 0;
        const bool found = tier1_emit_cfg_incoming_object(function, object, &past) ? tier1_emit_cfg_incoming(emitted, past, &base, &object_offset) : tier1_emit_cfg_object(function, emitted, object, &object_offset);
        error = selected_records.count == 1 && found ? error : TIER1_EMIT_FRAME;
        selected_records.records[0].rm = base;
        selected_records.records[0].displacement = object_offset;
    }
    // A symbol's address, its reference the record's: a lea of it, or a load of its GOT slot. A thread-local's (#141), as
    // Tier 0's: the thread pointer, fs:[0], plus its offset, a TPOFF32 immediate for the image's own (local-exec); or for
    // one that may be outside the image (initial-exec), its offset from its GOT slot, a GOTTPOFF on the mov the linkers
    // rewrite, plus the thread pointer. Neither has a recipe, so no copy recomputes one (tier1_emit_cfg_recipes); no flag
    // lives past the node for its add to clobber (BUSTER_TIER1_EMIT_BOUNDARY_FLAGS).
    if (plan->patterns[node].rule == TIER1_RULE_SYMBOL_ADDRESS && error == TIER1_EMIT_OK)
    {
        const uint32_t symbol = (uint32_t)plan->patterns[node].immediate;
        const uint8_t access = symbol < emitted->symbol_access_count ? emitted->symbol_access[symbol] : BUSTER_TIER1_ACCESS_DIRECT;
        const bool local_exec = access == BUSTER_TIER1_ACCESS_LOCAL_EXEC;
        const bool initial_exec = access == BUSTER_TIER1_ACCESS_INITIAL_EXEC;
        const uint8_t destination = selected_records.records[0].reg;
        error = selected_records.count == 1 && !selected_records.requested ? error : TIER1_EMIT_INVALID;
        error = error != TIER1_EMIT_OK || access == BUSTER_TIER1_ACCESS_DIRECT || access == BUSTER_TIER1_ACCESS_GOT || local_exec || initial_exec ? error : TIER1_EMIT_UNSUPPORTED;
        const X86Instruction thread_pointer = {
            .form = (local_exec ? X86_FORM_MOV_R64_RM64 : X86_FORM_ADD_R64_RM64) + BUSTER_X86_FORM_FS,
            .reg = destination,
            .mode = BUSTER_X86_MEMORY_ABSOLUTE,
        };
        const X86Instruction offset = { .form = X86_FORM_ADD_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .rm = destination, .mode = BUSTER_X86_REGISTER_DIRECT };
        const X86Instruction slot = { .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP, .reg = destination, .mode = BUSTER_X86_MEMORY_RIP };
        selected_records.records[0] = local_exec ? thread_pointer : access == BUSTER_TIER1_ACCESS_GOT || initial_exec ? slot : selected_records.records[0];
        selected_records.records[1] = local_exec ? offset : thread_pointer;
        selected_records.count = local_exec || initial_exec ? 2 : 1;
        // Its reference's record, by its index among the node's records: the caller adds the first's position.
        *reference = (LinkReference){
            .instruction = local_exec ? 1u : 0u,
            .field = local_exec ? X86_FIXUP_IMMEDIATE : X86_FIXUP_DISPLACEMENT,
            .kind = local_exec ? BUSTER_LINK_FIXUP_TPOFF32 : initial_exec ? BUSTER_LINK_FIXUP_GOTTPOFF : access == BUSTER_TIER1_ACCESS_GOT ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
            .target = symbol,
            .addend = 0,
        };
        *referenced = error == TIER1_EMIT_OK;
    }
    *records = selected_records;
    return error;
}

// An asm body's register operands into their registers (#2144, tier1_emit_cfg_asm): from `sources` (a general register's
// number, or BUSTER_TIER1_EMIT_VECTOR_LOCATION + n for xmm n), one parallel move per class, as tier1_emit_call moves a
// call's arguments.
static Tier1EmitError tier1_emit_cfg_asm_operands(const IrAsmBody* body, const uint32_t* sources, Tier1EmitBuffer* buffer, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t general_sources[BUSTER_TIER1_SELECT_MAX_ARGUMENTS];
    uint32_t general_destinations[BUSTER_TIER1_SELECT_MAX_ARGUMENTS];
    uint32_t vector_sources[BUSTER_TIER1_SELECT_MAX_ARGUMENTS];
    uint32_t vector_destinations[BUSTER_TIER1_SELECT_MAX_ARGUMENTS];
    uint32_t general = 0;
    uint32_t vectors = 0;
    Tier1EmitError error = body->passed <= BUSTER_TIER1_SELECT_MAX_ARGUMENTS ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < body->passed; k += 1)
    {
        const uint8_t reg = body->locations[k].registers[0];
        const bool vector = (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const bool placed = vector ? sources[k] >= BUSTER_TIER1_EMIT_VECTOR_LOCATION && sources[k] < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 16 : sources[k] < BUSTER_TIER1_EMIT_GENERAL_REGISTERS;
        error = placed ? error : TIER1_EMIT_ALLOCATION;
        if (vector)
        {
            vector_sources[vectors] = sources[k] - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
            vector_destinations[vectors++] = reg & 0x1fu;
        }
        else
        {
            general_sources[general] = sources[k];
            general_destinations[general++] = reg;
        }
    }
    const Tier1ParallelMoves moves[2] = {
        { .sources = general_sources, .destinations = general_destinations, .count = general, .scratch = BUSTER_TIER1_SELECT_SCRATCH },
        { .sources = vector_sources, .destinations = vector_destinations, .count = vectors, .scratch = BUSTER_TIER1_SELECT_FLOAT_SCRATCH },
    };
    for (uint32_t c = 0; error == TIER1_EMIT_OK && c < 2; c += 1)
    {
        Tier1ResolvedMoves resolved = { 0 };
        const Tier1AllocationError moved = moves[c].count != 0 ? tier1_resolve_moves(&moves[c], scratch, &resolved) : TIER1_ALLOCATION_OK;
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
        for (uint32_t m = 0; error == TIER1_EMIT_OK && m < resolved.count; m += 1)
        {
            const X86Instruction record = c == 0 ? tier1_emit_move(resolved.sources[m], resolved.destinations[m], 8) :
                                                   tier1_select_float_move((uint8_t)resolved.destinations[m], (uint8_t)resolved.sources[m]);
            error = tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

// All of one body's results are born together (#2144). Spill destinations are written first, while their fixed
// source registers still hold the template's results. The register destinations then take one parallel move per
// class, so an allocated destination may be another result's source without destroying it.
static Tier1EmitError tier1_emit_cfg_asm_results(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, Tier1EmitTransitions* transitions, uint32_t node, uint32_t position,
                                               Tier1EmitBuffer* buffer, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const IrFunction* function = plan->function;
    const IrAsmBody* body = &function->asm_bodies[function->calls[function->b[node]].body - 1];
    uint32_t sources[2][16];
    uint32_t destinations[2][16];
    uint32_t counts[2] = { 0 };
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t result = body->result != 0 ? node : BUSTER_IR_NONE; error == TIER1_EMIT_OK && result != BUSTER_IR_NONE;
         result = result == node ? emitted->prepared.asm_result_heads[node] : emitted->prepared.asm_result_next[result])
    {
        const uint32_t reg = result == node ? body->result - 1u : function->b[result];
        const uint32_t class = reg >= 16;
        const uint32_t value = emitted->node_values[result];
        uint32_t location = BUSTER_TIER1_CFG_NONE;
        uint8_t kind = 0;
        const bool found = value != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, value, position, &location, &kind);
        const bool held = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location / 32 == class && location % 32 < 16;
        int32_t offset = 0;
        const bool spilled = found && kind == BUSTER_TIER1_ALLOCATE_SPILL && tier1_emit_cfg_slot(emitted, location, &offset);
        error = held || spilled ? error : TIER1_EMIT_ALLOCATION;
        if (error == TIER1_EMIT_OK && spilled)
        {
            const uint32_t width = emitted->prepared.sizes[result];
            const X86Instruction record = {
                .form = class != 0 ? tier1_select_float_forms(width)->store : width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32,
                .reg = (uint8_t)(reg & 15),
                .rm = (uint8_t)emitted->frame.base_register,
                .mode = BUSTER_X86_MEMORY_BASE,
                .displacement = offset,
            };
            error = tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
            emitted->spill_stores += error == TIER1_EMIT_OK;
            if (transitions->slot_stamps != 0)
            {
                transitions->slot_stamps[result] = transitions->stamp;
            }
        }
        if (error == TIER1_EMIT_OK && held)
        {
            error = counts[class] < 16 ? error : TIER1_EMIT_INVALID;
            if (error == TIER1_EMIT_OK)
            {
                sources[class][counts[class]] = reg & 15;
                destinations[class][counts[class]++] = location & 31;
                transitions->owners[location] = result;
            }
        }
        transitions->registers[result] = held ? (uint8_t)location : UINT8_MAX;
    }
    for (uint32_t class = 0; error == TIER1_EMIT_OK && class < 2; class += 1)
    {
        const Tier1ParallelMoves moves = {
            .sources = sources[class],
            .destinations = destinations[class],
            .count = counts[class],
            .scratch = class == 0 ? BUSTER_TIER1_SELECT_SCRATCH : BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
        };
        Tier1ResolvedMoves resolved = { 0 };
        const Tier1AllocationError moved = counts[class] != 0 ? tier1_resolve_moves(&moves, scratch, &resolved) : TIER1_ALLOCATION_OK;
        error = moved == TIER1_ALLOCATION_OK ? error : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
        for (uint32_t m = 0; error == TIER1_EMIT_OK && m < resolved.count; m += 1)
        {
            const X86Instruction record = class == 0 ? tier1_emit_move(resolved.sources[m], resolved.destinations[m], 8) :
                                                     tier1_select_float_move((uint8_t)resolved.destinations[m], (uint8_t)resolved.sources[m]);
            error = tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
        }
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, position), BUSTER_IR_NONE);
    arena_pop_to(scratch, mark);
    return error;
}

// An asm body's records (#1839, TIER1_RULE_ASM) in its node's place, at its definition phase: the values that live past
// the node in the general registers it touches (its fixed registers, tier1_select_node_fixed), a callee-saved one's
// among them, stored into their call-save rows and loaded after, as a fixed rule's sequence has them (tier1_emit_fixed),
// and the floating values in the xmm registers it touches and the masks in the opmask registers its records write
// (IrAsmBody.masks) into theirs, as a call's are; between them its records, each
// that addresses the operand block (BUSTER_TIER0_ASM_BLOCK) addressing the block's frame object from the frame's base
// register, and its own branches, each to its target's record in the function's code (the result captures' first record for one
// to its end), sized with the function's. The consumer gave those registers rows (tier1_emit_cfg_saves); one missing is
// TIER1_EMIT_ALLOCATION. Its register operands (#2144, IrAsmBody.locations), its effective uses, each in the register of its
// class `sources` names at its use phase, are moved into their own registers after the saves, as a call's arguments are
// (tier1_emit_call): one parallel move through the scratch register for the general ones and another through the
// floating one's for the xmm ones, full-width, as an I32 operand's upper half is the template's to ignore. Every result
// is captured at the body boundary, before the saved values are restored (tier1_emit_cfg_asm_results). Its records
// are one ORIGINAL of the node, the saves', the operands' and the restores' moves. Into `range`, the body's first record
// and the one past its last: the template's own, which the target's forms don't bound (as the assembler takes any
// instruction whatever -march says, so does clang's object).
static Tier1EmitError tier1_emit_cfg_asm(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, Tier1EmitTransitions* transitions, const uint32_t* sources, uint32_t node, uint32_t use,
                                         Tier1EmitBuffer* buffer, uint32_t* range, Arena* scratch)
{
    const IrFunction* function = plan->function;
    const IrCall* call = &function->calls[function->b[node]];
    const uint32_t index = (uint32_t)plan->patterns[node].immediate;
    const IrAsmBody* body = index < function->asm_body_count ? &function->asm_bodies[index] : 0;
    const uint32_t block = body != 0 ? ir_asm_body_block(function, call, body) : BUSTER_IR_NONE;
    int32_t offset = 0;
    const bool placed = block == BUSTER_IR_NONE || (block < function->instruction_count && function->op[block] == IR_STACK_ADDR && tier1_emit_cfg_object(function, emitted, function->a[block], &offset));
    Tier1EmitError error = body == 0 ? TIER1_EMIT_INVALID : placed ? TIER1_EMIT_OK : TIER1_EMIT_FRAME;
    uint32_t saved = 0;
    uint32_t vectors_saved = 0;
    for (uint32_t location = 0; location < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 15; location += 1)
    {
        const bool general = location < BUSTER_TIER1_EMIT_GENERAL_REGISTERS;
        const bool vector = location >= BUSTER_TIER1_EMIT_VECTOR_LOCATION;
        const uint32_t reg = vector ? location - BUSTER_TIER1_EMIT_VECTOR_LOCATION : location;
        const bool touched = body != 0 && (general || vector) && ((body->touched >> (vector ? 16 + reg : reg)) & 1) != 0;
        const uint32_t owner = touched ? transitions->owners[location] : BUSTER_IR_NONE;
        const bool live = owner != BUSTER_IR_NONE && owner != node && transitions->registers[owner] == location && tier1_cfg_interval_at(&emitted->cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        saved |= live && general ? 1u << reg : 0;
        vectors_saved |= live && vector ? 1u << reg : 0;
    }
    // And the masks that live past it in the opmask registers its records write (IrAsmBody.masks), as a call saves them.
    uint32_t masks_saved = 0;
    for (uint32_t k = 1; body != 0 && k < 8; k += 1)
    {
        const uint32_t location = BUSTER_TIER1_EMIT_MASK_LOCATION + k;
        const uint32_t owner = ((body->masks >> k) & 1) != 0 ? transitions->owners[location] : BUSTER_IR_NONE;
        const bool live = owner != BUSTER_IR_NONE && owner != node && transitions->registers[owner] == location && tier1_cfg_interval_at(&emitted->cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        masks_saved |= live ? 1u << k : 0;
    }
    error = error == TIER1_EMIT_OK && ((saved & ~emitted->prepared.call_save_mask) != 0 || (vectors_saved & ~emitted->prepared.vector_save_mask) != 0 ||
                                       (masks_saved & ~emitted->prepared.mask_save_mask) != 0) ?
            TIER1_EMIT_ALLOCATION :
            error;
    const Tier1SelectFloatForms* forms = emitted->prepared.vector_bytes != 0 ? tier1_select_class_forms(emitted->prepared.vector_bytes, tier1_emit_encoding(&emitted->prepared)) : 0;
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            error = ((saved >> reg) & 1) == 0 || tier1_emit_append(buffer, tier1_emit_call_save(emitted, reg, pass == 1)) ? error : TIER1_EMIT_MEMORY;
            emitted->call_stores += pass == 0 ? (saved >> reg) & 1 : 0;
            emitted->call_loads += pass == 1 ? (saved >> reg) & 1 : 0;
        }
        for (uint32_t x = 0; error == TIER1_EMIT_OK && x < 15; x += 1)
        {
            const uint32_t row = tier1_emit_cfg_vector_save_row(emitted, x);
            const bool listed = ((vectors_saved >> x) & 1) != 0;
            error = !listed || row < emitted->frame.count ? error : TIER1_EMIT_FRAME;
            const X86Instruction record = {
                .displacement = listed ? emitted->frame.offset[row] : 0,
                .reg = (uint8_t)x,
                .rm = (uint8_t)emitted->frame.base_register,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = forms != 0 ? (pass == 0 ? forms->store : forms->load) : pass == 0 ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSD_X_XM64,
            };
            error = error != TIER1_EMIT_OK || !listed || tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
            emitted->call_stores += pass == 0 && listed;
            emitted->call_loads += pass == 1 && listed;
        }
        for (uint32_t k = 1; error == TIER1_EMIT_OK && k < 8; k += 1)
        {
            const bool listed = ((masks_saved >> k) & 1) != 0;
            const uint32_t row = listed ? tier1_emit_cfg_mask_save_row(emitted, k) : 0;
            error = !listed || row < emitted->frame.count ? error : TIER1_EMIT_FRAME;
            if (error == TIER1_EMIT_OK && listed)
            {
                // Each mask by kmov of its width, as a call's (tier1_select_mask_forms).
                const uint32_t owner = transitions->owners[BUSTER_TIER1_EMIT_MASK_LOCATION + k];
                const Tier1SelectMaskForms* mask_forms = tier1_select_mask_forms(emitted->prepared.sizes[owner]);
                const X86Instruction record = {
                    .displacement = emitted->frame.offset[row],
                    .reg = (uint8_t)k,
                    .rm = (uint8_t)emitted->frame.base_register,
                    .mode = BUSTER_X86_MEMORY_BASE,
                    .form = pass == 0 ? mask_forms->store : mask_forms->load,
                };
                error = tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
                emitted->call_stores += pass == 0;
                emitted->call_loads += pass == 1;
            }
        }
        error = error == TIER1_EMIT_OK && pass == 0 && body->passed != 0 ? tier1_emit_cfg_asm_operands(body, sources, buffer, scratch) : error;
        tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, use + 1), BUSTER_IR_NONE);
        // The body's records, between the saves and the restores.
        const uint32_t first = buffer->count;
        range[0] = pass == 0 ? first : range[0];
        if (error == TIER1_EMIT_OK && pass == 0)
        {
            error = body->label_count <= emitted->asm_label_capacity - emitted->asm_label_count ? error : TIER1_EMIT_MEMORY;
            for (uint32_t k = 0; error == TIER1_EMIT_OK && k < body->label_count; k += 1)
            {
                const uint32_t at = emitted->asm_label_count++;
                emitted->asm_label_records[at] = first + body->label_records[k];
                emitted->asm_label_names[at] = body->label_names[k];
            }
        }
        uint32_t branch = 0;
        for (uint32_t r = 0; error == TIER1_EMIT_OK && pass == 0 && r < body->count; r += 1)
        {
            X86Instruction record = body->records[r];
            const bool addressed = (record.mode & BUSTER_X86_MODE_MEMORY) != 0 && (record.mode & BUSTER_X86_MODE_BASE) != 0 && record.rm == BUSTER_TIER0_ASM_BLOCK;
            error = !addressed || block != BUSTER_IR_NONE ? error : TIER1_EMIT_INVALID;
            record.rm = addressed ? (uint8_t)emitted->frame.base_register : record.rm;
            record.displacement += addressed ? offset : 0;
            const bool branches = branch < body->branch_count && body->branches[branch] == r;
            error = error != TIER1_EMIT_OK || !branches || buffer->branch_count < buffer->branch_capacity ? error : TIER1_EMIT_MEMORY;
            if (error == TIER1_EMIT_OK && branches)
            {
                buffer->branches[buffer->branch_count] = first + r;
                buffer->targets[buffer->branch_count] = first + body->targets[branch];
                buffer->branch_count += 1;
                branch += 1;
            }
            error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
        }
        range[1] = pass == 0 ? buffer->count : range[1];
        tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
        error = error == TIER1_EMIT_OK && pass == 0 && body->result != 0 ? tier1_emit_cfg_asm_results(plan, emitted, transitions, node, use + 1, buffer, scratch) : error;
    }
    return error;
}

// Space beyond the per-node and per-block bounds. Read 64 operation bytes at once: only calls need a
// descriptor. Count returns, vector literals and dynamic stack allocations directly from their masks.
typedef struct Tier1EmitCfgCapacity Tier1EmitCfgCapacity;
struct Tier1EmitCfgCapacity
{
    uint64_t records;
    uint64_t branches;
    uint64_t asm_labels;
    uint32_t references;
    uint32_t literals;
};

static Tier1EmitCfgCapacity tier1_emit_cfg_capacity(const Tier1SelectPlan* plan, bool probe)
{
    const IrFunction* function = plan->function;
    Tier1EmitCfgCapacity capacity = { .records = function->parameter_passed != 0 ? 2u * (uint64_t)function->parameter_count + 2 : 0 };
    const bool extra = function->call_count != 0 || function->result_size != 0 || function->vector_constant_count != 0 || probe;
    for (uint32_t first = 0; extra && first < function->instruction_count;)
    {
        const uint32_t left = function->instruction_count - first;
        const uint32_t length = left < 64 ? left : 64;
        const __mmask64 lanes = length == 64 ? ~(__mmask64)0 : (((__mmask64)1 << length) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi8(lanes, function->op + first);
        const __mmask64 calls = _mm512_mask_cmpeq_epi8_mask(lanes, ops, _mm512_set1_epi8(IR_CALL)) | _mm512_mask_cmpeq_epi8_mask(lanes, ops, _mm512_set1_epi8(IR_CALL_INDIRECT));
        if (function->result_size != 0)
        {
            const uint32_t returns = (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, ops, _mm512_set1_epi8(IR_RET)));
            capacity.records += (uint64_t)returns * (function->result_size / 4u + 32u);
        }
        if (function->vector_constant_count != 0)
        {
            capacity.literals += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, ops, _mm512_set1_epi8(IR_VCONST)));
        }
        if (probe)
        {
            const uint32_t allocations = (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, ops, _mm512_set1_epi8(IR_ALLOCA)));
            capacity.records += (uint64_t)allocations * BUSTER_TIER1_EMIT_PROBE_RECORDS + 7u * (uint32_t)__builtin_popcountll(calls);
            capacity.branches += (uint64_t)allocations * BUSTER_TIER1_EMIT_PROBE_BRANCHES;
        }
        for (uint64_t bits = calls; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            const uint32_t node = first + lane;
            const IrCall* call = &function->calls[function->b[node]];
            if (call->abi != 0)
            {
                capacity.records += function->abi_calls[call->abi - 1].call.stack_bytes / 4u + 4u * (uint64_t)call->argument_count + 5u * (BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS) + 16u;
            }
            capacity.records += call->row != 0 ? BUSTER_TIER1_EMIT_ROW_RECORDS : 0;
            if (call->body != 0 && call->body <= function->asm_body_count)
            {
                // The body's records, the moves of its operands, its register saves and restores, and its own branches.
                const IrAsmBody* body = &function->asm_bodies[call->body - 1];
                capacity.records += body->count + 2u * (uint64_t)body->passed + 2u * (uint32_t)__builtin_popcount(body->result_registers) + 2u + 2u * (BUSTER_TIER1_EMIT_GENERAL_REGISTERS + 15u + 7u);
                capacity.branches += function->op[node] == IR_CALL ? body->branch_count : 0;
                capacity.asm_labels += body->label_count;
            }
            if (tier1_select_function_call(function, node))
            {
                capacity.branches += probe;
                // An uncovered callee's recomputed arguments can carry a symbol reference (#141).
                capacity.references += !plan->covered[node] ? tier1_select_call_stack_bytes(function, call) / BUSTER_ABI_SLOT_BYTES + call->argument_count : 0;
            }
        }
        first += length;
    }
    // A vector constant can emit two broadcast or literal-load records and one literal reference (#2744).
    capacity.records += 2u * (uint64_t)capacity.literals;
    capacity.references += capacity.literals;
    return capacity;
}

// The long double phis' copies' records (#141, tier1_emit_cfg_x87_phis), past every node's share: on each edge, an fld and
// an fstp per long double phi of its target, at most 8 before the edge is refused. A join's phis copy on each of its
// incoming edges, so their records grow with the edges, not with the phis (#1204's review). Scalar: it runs only in
// functions with a long double, over their edges.
static uint64_t tier1_emit_cfg_x87_copy_records(const IrFunction* function, const Tier1CfgInput* input, uint32_t x87_count)
{
    const uint32_t added = input->block_count - function->block_count;
    uint64_t records = 0;
    for (uint32_t edge = 0; x87_count != 0 && edge < input->edge_count; edge += 1)
    {
        const uint32_t successor = input->successors[edge];
        const bool ir_block = successor >= added;
        uint32_t phis = 0;
        for (uint32_t node = ir_block ? function->block_offsets[successor - added] : 0; ir_block && phis < 8 && node < function->block_offsets[successor - added + 1] && function->op[node] == IR_PHI; node += 1)
        {
            phis += function->type[node] == IR_F80;
        }
        records += 2u * phis;
    }
    return records;
}

// The long double nodes' records past every node's share (#141, tier1_emit_cfg_x87): up to BUSTER_TIER1_EMIT_X87_RECORDS
// each, where a node's share is BUSTER_TIER1_SELECT_RECORDS_PER_NODE, so that a block of conversions doesn't live on
// other nodes' slack (#1204's review). Scalar: it runs only in functions with a long double.
static uint64_t tier1_emit_cfg_x87_node_records(const Tier1SelectPlan* plan, uint32_t x87_count, uint32_t* constants, uint64_t* references)
{
    uint64_t records = 0;
    for (uint32_t node = 0; x87_count != 0 && node < plan->function->instruction_count; node += 1)
    {
        const IrFunction* function = plan->function;
        if (!plan->covered[node] && (plan->patterns[node].rule == TIER1_RULE_X87 || plan->patterns[node].rule == TIER1_RULE_RETURN))
        {
            *constants += function->op[node] == IR_CONST && function->type[node] == IR_F80;
            const uint32_t operands[2] = { function->a[node], function->b[node] };
            const uint32_t uses = ir_value_mask(function, node);
            for (uint32_t i = 0; i < 2; i += 1)
            {
                const uint32_t value = operands[i];
                *references += (uses >> i & 1) != 0 && value < function->instruction_count &&
                               function->type[value] == IR_F80 && function->op[value] == IR_CONST;
            }
        }
        records += !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_X87 ? BUSTER_TIER1_EMIT_X87_RECORDS - BUSTER_TIER1_SELECT_RECORDS_PER_NODE : 0;
    }
    return records;
}

// A phi reads its incoming object in the emitted edge copy. Within a predecessor with one successor,
// the unconditional terminator owns that copy and the following jump, if any.
static uint32_t tier1_emit_cfg_x87_consumer(const Tier1EmitResult* emitted, const Tier1SelectPlan* plan, uint32_t node, IrUse use)
{
    const IrFunction* function = plan->function;
    const uint32_t user = use.instruction;
    uint32_t consumer = plan->patterns[user].rule == TIER1_RULE_X87 && function->op[user] != IR_PHI ? user : BUSTER_IR_NONE;
    if (function->op[user] == IR_PHI && use.operand >= 3)
    {
        const uint32_t source = function->extra[function->a[user] + (uint64_t)(use.operand - 3) * 2];
        const uint32_t terminator = function->block_offsets[source + 1] - 1;
        const uint32_t block = source + emitted->cfg_input.block_count - function->block_count;
        const uint32_t edge = emitted->cfg_input.successor_offsets[block];
        if (node >= function->block_offsets[source] && node < terminator && function->op[terminator] == IR_BR &&
            emitted->cfg_input.successor_offsets[block + 1] == edge + 1 && emitted->cfg.edge_placements[edge] == TIER1_CFG_EDGE_PREDECESSOR)
        {
            consumer = terminator;
        }
    }
    return consumer;
}

// Optional transport of an F80 result to its sole consumer (#1387). Count every IR use, including
// repeated operands, calls and phi edges. Only the selected producer's own private store and the selected consumer's
// matching load are annotated. The shared record proof preserves both waiting points and every metadata index,
// and declines barriers, unresolved operand order and excess pressure.
static void tier1_emit_cfg_x87_transfers(const Tier1SelectPlan* plan, Tier1EmitResult* emitted, uint32_t records, const uint32_t* block_first, uint32_t blocks, Arena* scratch)
{
    const IrFunction* function = plan->function;
    const uint32_t nodes = function->instruction_count;
    const uint64_t mark = scratch->length;
    bool ok = emitted->prepared.x87_count != 0;
    const uint64_t rows = (uint64_t)records + 1;
    IrUse* consumers = ok ? arena_push_array(scratch, nodes, sizeof(IrUse), _Alignof(IrUse)) : NULL;
    uint32_t* tokens = consumers != NULL ? arena_push_array(scratch, rows, sizeof(uint32_t) + 2, _Alignof(uint32_t)) : NULL;
    if (tokens != NULL)
    {
        uint8_t* annotations = (uint8_t*)(tokens + rows);
        uint8_t* boundaries = annotations + rows;
        bool has_store = false;
        memset(tokens, 0xff, rows * sizeof(uint32_t));
        memset(annotations, 0, rows * 2);
        memset(consumers, 0xff, (uint64_t)nodes * sizeof(IrUse));
        // A store needs only its sole consumer. Preserve all operand positions, including side tables
        // and phi edges. A second use invalidates the operand while leaving the first user marked.
        uint64_t total = 0;
        for (uint32_t i = 0; i < nodes; i += 1)
        {
            const uint32_t count = ir_operand_count(function, i);
            total += count;
            for (uint32_t k = 0; k < count; k += 1)
            {
                const IrOperand operand = ir_operand(function, i, k, 0);
                if (function->type[operand.value] == IR_F80)
                {
                    IrUse* use = &consumers[operand.value];
                    use->operand = use->instruction == BUSTER_IR_NONE ? operand.position : BUSTER_IR_NONE;
                    use->instruction = i;
                }
            }
        }
        boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
        boundaries[records] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            boundaries[block_first[b]] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
        }
        for (uint32_t node = 0; node < nodes; node += 1)
        {
            const uint32_t first = emitted->code.node_first[node];
            const uint32_t end = node + 1 < nodes ? emitted->code.node_first[node + 1] : records;
            const uint32_t op = function->op[node];
            boundaries[first] |= BUSTER_X87_TRANSFER_BOUNDARY;
            boundaries[end] |= BUSTER_X87_TRANSFER_BOUNDARY;
            const bool barrier = (function->flags[node] & BUSTER_IR_OBSERVABLE) != 0 || op == IR_PHI ||
                                 tier1_select_function_call(function, node) || plan->patterns[node].rule == TIER1_RULE_ROW || plan->patterns[node].rule == TIER1_RULE_ASM;
            if (barrier)
            {
                boundaries[first] |= BUSTER_X87_TRANSFER_CUT;
                boundaries[end] |= BUSTER_X87_TRANSFER_CUT;
            }
        }
        for (uint32_t node = 0; node < nodes; node += 1)
        {
            const bool single = consumers[node].operand != BUSTER_IR_NONE;
            const bool producer = function->type[node] == IR_F80 && single && !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_X87;
            const uint32_t user = producer ? tier1_emit_cfg_x87_consumer(emitted, plan, node, consumers[node]) : BUSTER_IR_NONE;
            const bool candidate = user < nodes && !plan->covered[user];
            int32_t offset = 0;
            if (candidate && tier1_emit_cfg_x87_offset(function, emitted, node, &offset))
            {
                const uint32_t first = emitted->code.node_first[node];
                const uint32_t end = node + 1 < nodes ? emitted->code.node_first[node + 1] : records;
                const uint32_t use_first = emitted->code.node_first[user];
                uint32_t use_end = user + 1 < nodes ? emitted->code.node_first[user + 1] : records;
                if (function->op[user] == IR_BR && use_end > use_first && emitted->code.records[use_end - 1].form == X86_FORM_JMP_REL8)
                {
                    // The parallel copy leaves the stack empty before control leaves the predecessor.
                    use_end -= 1;
                    boundaries[use_end] |= BUSTER_X87_TRANSFER_BOUNDARY;
                }
                uint32_t store = BUSTER_X87_TRANSFER_NONE;
                uint32_t load = BUSTER_X87_TRANSFER_NONE;
                uint32_t stores = 0;
                uint32_t loads = 0;
                for (uint32_t r = first; r < end; r += 1)
                {
                    const X86Instruction* record = &emitted->code.records[r];
                    const bool found = record->form == X86_FORM_FSTP_M80 && record->mode == BUSTER_X86_MEMORY_BASE &&
                                       record->rm == emitted->frame.base_register && record->displacement == offset;
                    store = found ? r : store;
                    stores += found;
                }
                for (uint32_t r = use_first; r < use_end; r += 1)
                {
                    const X86Instruction* record = &emitted->code.records[r];
                    const bool found = record->form == X86_FORM_FLD_M80 && record->mode == BUSTER_X86_MEMORY_BASE &&
                                       record->rm == emitted->frame.base_register && record->displacement == offset;
                    load = found ? r : load;
                    loads += found;
                }
                if (stores == 1 && loads == 1 && store < load)
                {
                    tokens[store] = store;
                    tokens[load] = store;
                    annotations[store] = BUSTER_X87_TRANSFER_STORE;
                    annotations[load] = BUSTER_X87_TRANSFER_RELOAD;
                    has_store = true;
                }
            }
        }
        if (has_store && total <= UINT32_MAX)
        {
            (void)x87_transfer_records(emitted->code.records, records, tokens, annotations, boundaries);
        }
    }
    arena_pop_to(scratch, mark);
}

static Tier1EmitError tier1_emit_cfg_records(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, bool certify, bool sources, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = input->block_count;
    const uint32_t added = blocks - function->block_count;
    const uint32_t base = 2 * added;
    const uint32_t returns = emitted->jumps;
    emitted->jumps = 0;
    emitted->dispatch_copies = 0;
    const Tier1EmitCfgCapacity extra = tier1_emit_cfg_capacity(plan, emitted->frame.probe_stack);
    uint32_t x87_constants = 0;
    uint64_t x87_references = 0;
    const uint64_t x87_copies = tier1_emit_cfg_x87_copy_records(function, input, emitted->prepared.x87_count);
    const uint64_t x87_nodes = tier1_emit_cfg_x87_node_records(plan, emitted->prepared.x87_count, &x87_constants, &x87_references);
    const uint64_t capacity = (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE + (uint64_t)allocation->fragment_count * 2 + (uint64_t)cfg->move_count * 2 + BUSTER_TIER1_FRAME_EMIT_RECORDS * ((uint64_t)returns + 1) + (uint64_t)emitted->prepared.call_count * BUSTER_TIER1_EMIT_CALL_RECORDS + (uint64_t)blocks * 2 + input->edge_count + extra.records +
                              x87_copies + x87_nodes + (uint64_t)emitted->fixed_count * BUSTER_TIER1_EMIT_FIXED_RECORDS + 2 * BUSTER_TIER1_SELECT_MAX_ARGUMENTS;
    const uint64_t branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES * ((uint64_t)returns + 1) + (uint64_t)blocks * 2 + input->edge_count + extra.branches;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = capacity <= UINT32_MAX && branch_capacity <= UINT32_MAX && extra.asm_labels <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1SelectCode* code = &emitted->code;
    Tier1EmitBuffer buffer = { .capacity = (uint32_t)capacity, .branch_capacity = (uint32_t)branch_capacity };
    Tier1SelectRequests requests = { 0 };
    // The emitter's view by node, as the straight-line consumer keeps it, and the allocation with its spill ids by node,
    // which the shared helpers read.
    Tier1EmitTransitions transitions = { 0 };
    Tier1Allocation view = *allocation;
    // A reference for each recomputed symbol's address too (tier1_emit_copy): a fragment's from recomputation into a
    // register, and an edge's move that recomputes one.
    const uint8_t* recipes = emitted->prepared.recipes.operations;
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
    {
        const bool symbol = recipes[emitted->value_nodes[cfg->intervals.values[i]]] == BUSTER_TIER1_REMAT_SYMBOL;
        for (uint32_t f = allocation->fragment_offsets[i] + 1; symbol && f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            emitted->reference_capacity += allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER;
        }
    }
    for (uint32_t m = 0; error == TIER1_EMIT_OK && m < cfg->move_count; m += 1)
    {
        emitted->reference_capacity += cfg->move_sources[m] == BUSTER_TIER1_CFG_REMATERIALIZE && recipes[emitted->value_nodes[cfg->move_values[m]]] == BUSTER_TIER1_REMAT_SYMBOL;
    }
    const uint32_t vector_constants = extra.literals;
    // Each x87 phi copy can read one constant. Each call already has room for its stack-slot operands.
    const uint64_t literal_room = (uint64_t)vector_constants + x87_constants;
    const uint64_t reference_room = (uint64_t)emitted->reference_capacity + extra.references + x87_references +
                                    (x87_constants != 0 ? x87_copies / 2 : 0);
    error = error != TIER1_EMIT_OK || (literal_room <= BUSTER_TIER1_EMIT_LITERAL && reference_room <= UINT32_MAX) ? error : TIER1_EMIT_LIMIT;
    emitted->reference_capacity = (uint32_t)reference_room;
    bool ok = error == TIER1_EMIT_OK;
    code->records = ok ? ir_allocate(output, capacity, sizeof(X86Instruction), &ok) : 0;
    code->cost_ids = ok ? ir_allocate(output, capacity, 1, &ok) : 0;
    code->node_first = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    code->branches = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    code->targets = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    emitted->references = ok && emitted->reference_capacity != 0 ? ir_allocate(output, emitted->reference_capacity, sizeof(LinkReference), &ok) : 0;
    emitted->asm_label_capacity = (uint32_t)extra.asm_labels;
    emitted->asm_label_count = 0;
    emitted->asm_label_records = ok && extra.asm_labels != 0 ? ir_allocate(output, extra.asm_labels, 4, &ok) : 0;
    emitted->asm_label_names = ok && extra.asm_labels != 0 ? ir_allocate(output, extra.asm_labels, 4, &ok) : 0;
    // Each vector constant or F80 constant needs at most one literal per definition. The vector index also distinguishes widths.
    const uint64_t literal_index = vector_constants != 0 ? 3 * (uint64_t)function->vector_constant_count : 0;
    emitted->literal_capacity = (uint32_t)literal_room;
    emitted->literal_count = 0;
    emitted->literals = ok && literal_room != 0 ? ir_allocate(output, literal_room, sizeof(IrSimdValue), &ok) : 0;
    emitted->literal_widths = ok && literal_room != 0 ? ir_allocate(output, literal_room, 1, &ok) : 0;
    emitted->literal_of = ok && literal_index != 0 ? ir_allocate(output, literal_index, 4, &ok) : 0;
    emitted->x87_literal_of = ok && x87_constants != 0 ? ir_allocate(output, function->constant_count, 4, &ok) : 0;
    if (ok && x87_constants != 0)
    {
        memset(emitted->x87_literal_of, 0, (uint64_t)function->constant_count * 4);
    }
    emitted->exits = ok ? ir_allocate(output, (uint64_t)returns + 1, 4, &ok) : 0;
    emitted->exit_count = 0;
    // The frame's events (#1056): the prologue's, and each return's epilogue's.
    emitted->frame_events = ok ? ir_allocate(output, BUSTER_TIER1_FRAME_EMIT_EVENTS * ((uint64_t)returns + 1), sizeof(EmissionFrameEvent), &ok) : 0;
    emitted->frame_event_count = 0;
    // The certificate of the code (#939) reads each record's origin, and each block's and stub's first record.
    emitted->origins = ok && (certify || sources) ? ir_allocate(output, capacity + 1, 4, &ok) : 0;
    emitted->origin_values = ok && certify ? ir_allocate(output, capacity + 1, 4, &ok) : 0;
    emitted->block_records = ok && certify ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    emitted->stub_records = ok && certify ? ir_allocate(output, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    const bool call_rows = emitted->prepared.call_save_mask != 0;
    const uint64_t row_count = (uint64_t)blocks * BUSTER_TIER1_EMIT_GENERAL_REGISTERS;
    emitted->entry_rows = ok && certify && call_rows ? ir_allocate(output, row_count, 4, &ok) : 0;
    uint32_t* exit_rows = ok && call_rows ? ir_allocate(scratch, row_count, 4, &ok) : 0;
    emitted->dropped = ok && certify ? ir_allocate(output, (uint64_t)count + 1, 1, &ok) : 0;
    emitted->row_placements = ok && certify && function->row_count != 0 ? ir_allocate(output, (uint64_t)count + 1, sizeof(Tier1RowPlacement), &ok) : 0;
    transitions.offsets = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 2, 4, &ok) : 0;
    transitions.fragments = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.values = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.registers = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    transitions.slot_stamps = ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* spill_ids = ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* cursors = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    uint32_t* entry_offsets = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* entry_edges = ok ? ir_allocate(scratch, blocks, 4, &ok) : 0;
    uint32_t* block_first = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* stub_first = ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    uint32_t* pending_branches = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    uint32_t* pending_goals = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    uint8_t* dropped = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    // The asm bodies' records' ranges (#1839), in code order, which the target's forms don't bound. Retain them so
    // post scheduling preserves the template's exact records and the instructions its labels name (#2351).
    uint32_t* asm_ranges = ok && emitted->prepared.asm_count != 0 ? ir_allocate(output, 2 * (uint64_t)emitted->prepared.asm_count, 4, &ok) : 0;
    uint32_t asm_range_count = 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    uint32_t* entries = 0;
    uint32_t pending = 0;
    if (error == TIER1_EMIT_OK)
    {
        buffer.records = code->records;
        buffer.branches = code->branches;
        buffer.targets = code->targets;
        buffer.origins = emitted->origins;
        buffer.origin_values = emitted->origin_values;
        buffer.origin_base = base;
        if (certify || sources)
        {
            memset(emitted->origins, 0, (capacity + 1) * 4);
        }
        if (certify)
        {
            memset(stub_first, 0xff, ((uint64_t)input->edge_count + 1) * 4);
        }
        view.spill_ids = spill_ids;
        memset(transitions.offsets, 0, ((uint64_t)input->position_count + 2) * 4);
        memset(transitions.registers, UINT8_MAX, (uint64_t)count + 1);
        memset(transitions.slot_stamps, 0, ((uint64_t)count + 1) * 4);
        memset(spill_ids, 0xff, ((uint64_t)count + 1) * 4);
        memset(entry_offsets, 0, ((uint64_t)blocks + 1) * 4);
        tier1_cfg_single_predecessor_edges(input, entry_edges);
        for (uint32_t v = 0; v < input->value_count; v += 1)
        {
            spill_ids[emitted->value_nodes[v]] = allocation->spill_ids[v];
        }
    }
    // Fragment transitions by position, and each block's entry intervals (those that cover its start), each counted then
    // filled. A definition's first fragment is its register (its use there is required); a reload at a definition
    // phase, or a move from register to register within an interval, is the straight-line consumer's refusal too.
    for (uint32_t pass = 0; error == TIER1_EMIT_OK && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            for (uint32_t q = 0; q < input->position_count; q += 1)
            {
                transitions.offsets[q + 1] += transitions.offsets[q];
                cursors[q] = transitions.offsets[q];
            }
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                entry_offsets[b + 1] += entry_offsets[b];
                block_first[b] = entry_offsets[b];
            }
            entries = ir_allocate(scratch, (uint64_t)entry_offsets[blocks] + 1, 4, &ok);
            emitted->entry_slots = ok && certify ? ir_allocate(output, (uint64_t)entry_offsets[blocks] + 1, 4, &ok) : 0;
            emitted->entry_slot_offsets = ok && certify ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
            error = ok ? error : TIER1_EMIT_MEMORY;
        }
        for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
        {
            const uint32_t v = cfg->intervals.values[i];
            const uint32_t node = emitted->value_nodes[v];
            const uint32_t first = allocation->fragment_offsets[i];
            const uint32_t start = cfg->intervals.starts[i];
            // A definition writes a register, but a parameter past its class's registers', which the entry loads from its
            // incoming slot into its first fragment's place, a register or its own slot (tier1_emit_cfg_entry, #141).
            const bool stacked = function->op[node] == IR_PARAM && tier1_select_parameter_place(function, function->a[node]).kind == BUSTER_TIER1_PLACE_STACK;
            const bool defines = start == input->definitions[v] && function->op[node] != IR_PHI && !stacked;
            // A recipe's definition may have none (#2618), and is then left out (tier1_emit_cfg_dead_definition).
            const bool recomputed = allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE && input->rematerializable != 0 && input->rematerializable[v] != 0;
            const bool snapshot = plan->patterns[node].rule == TIER1_RULE_ASM || function->op[node] == IR_ASM_RESULT;
            error = !defines || allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER || recomputed ||
                    (snapshot && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_SPILL) ? error : TIER1_EMIT_ALLOCATION;
            for (uint32_t f = first + 1; error == TIER1_EMIT_OK && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const uint8_t kind = allocation->kinds[f];
                const uint8_t previous = allocation->kinds[f - 1];
                const uint32_t q = allocation->starts[f];
                error = kind != BUSTER_TIER1_ALLOCATE_REGISTER || (previous != BUSTER_TIER1_ALLOCATE_REGISTER && ((q - base) & 1) == 0) ? error : TIER1_EMIT_UNSUPPORTED;
                error = kind != BUSTER_TIER1_ALLOCATE_SPILL || previous != BUSTER_TIER1_ALLOCATE_REMATERIALIZE ? error : TIER1_EMIT_ALLOCATION;
                if (pass == 0)
                {
                    transitions.offsets[q + 1] += 1;
                }
                else
                {
                    transitions.fragments[cursors[q]] = f;
                    cursors[q] += 1;
                }
                transitions.values[f] = node;
            }
            // The blocks whose starts the interval covers.
            uint32_t b = tier1_cfg_block_at(input, cfg, start);
            b += input->block_starts[b] < start ? 1 : 0;
            for (; error == TIER1_EMIT_OK && b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
            {
                if (pass == 0)
                {
                    entry_offsets[b + 1] += 1;
                }
                else
                {
                    entries[block_first[b]] = i;
                    block_first[b] += 1;
                }
            }
        }
    }
    // The definitions left out (tier1_emit_cfg_dead_definition), the last first: one that only definitions left out read
    // is left out too.
    for (uint32_t k = count; error == TIER1_EMIT_OK && k > 0; k -= 1)
    {
        const uint32_t node = k - 1;
        const uint32_t v = emitted->node_values[node];
        const bool selected = !plan->covered[node] && function->op[node] != IR_PHI && !tier1_select_entry_parameter(function, node);
        dropped[node] = selected && v != BUSTER_TIER1_CFG_NONE && tier1_emit_cfg_dead_definition(emitted, v, base + 2 * node + 1, dropped, base);
    }
    buffer.events = emitted->frame_events;
    buffer.event_capacity = emitted->frame_events != 0 ? (uint32_t)(BUSTER_TIER1_FRAME_EMIT_EVENTS * ((uint64_t)returns + 1)) : 0;
    if (error == TIER1_EMIT_OK && certify)
    {
        memcpy(emitted->dropped, dropped, count);
    }
    if (error == TIER1_EMIT_OK && tier1_frame_emit(&emitted->frame, false, &buffer) != TIER1_FRAME_EMIT_OK)
    {
        error = TIER1_EMIT_FRAME;
    }
    emitted->body_record = buffer.count;
    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_FRAME, 0), BUSTER_IR_NONE);
    // Parameter representations, before anything reads or moves their incoming registers. Narrow values become
    // canonical zero-extended I32; an I32 with shared signed uses explicitly establishes its full signed I64 bits.
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t type = function->type[node];
        const uint32_t narrow = type == IR_I1 || type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
        const bool signed_parameter = type == IR_I32 && plan->patterns[node].sign_result;
        const Tier1SelectPlace place = function->op[node] == IR_PARAM && (narrow != 0 || signed_parameter) ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_STACK };
        if (place.kind == BUSTER_TIER1_PLACE_GENERAL && emitted->node_values[node] != BUSTER_TIER1_CFG_NONE)
        {
            const uint8_t reg = (uint8_t)tier1_select_parameter_registers[place.index];
            const X86Instruction extend = { .form = signed_parameter ? X86_FORM_MOVSXD_R64_RM32 : narrow == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = reg, .rm = reg };
            error = tier1_emit_append(&buffer, extend) ? error : TIER1_EMIT_MEMORY;
            tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_ENTRY, node), node);
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_entry(plan, emitted, scratch, &buffer);
        tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_ENTRY, 0), BUSTER_IR_NONE);
    }
    uint32_t slot_count = 0;
    if (error == TIER1_EMIT_OK && emitted->entry_slot_offsets != 0)
    {
        emitted->entry_slot_offsets[0] = 0;
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        block_first[b] = b == 0 ? 0 : buffer.count;
        const uint32_t entry = input->block_starts[b];
        const uint32_t degree = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
        const uint32_t only = degree == 1 ? input->predecessors[input->predecessor_offsets[b]] : UINT32_MAX;
        // A block whose only predecessor is the one before it continues that block's run of slot stamps (tier1_emit_phase).
        const bool run = b != 0 && only == b - 1;
        transitions.stamp = run ? transitions.stamp : b + 1;
        // And the slots that hold their values at the block's start whatever the edge into it (tier1_cfg_current_slots,
        // #2553), whose stores are left out too.
        const uint32_t current_first = cfg->current_slot_offsets != 0 ? cfg->current_slot_offsets[b] : 0;
        const uint32_t current_end = cfg->current_slot_offsets != 0 ? cfg->current_slot_offsets[b + 1] : 0;
        for (uint32_t k = current_first; k < current_end; k += 1)
        {
            transitions.slot_stamps[emitted->value_nodes[cfg->current_slots[k]]] = transitions.stamp;
        }
        // The single incoming edge's moves, where tier1_cfg placed them at this block's start.
        if (only != UINT32_MAX)
        {
            const uint32_t e = entry_edges[b];
            if (cfg->edge_placements[e] == TIER1_CFG_EDGE_SUCCESSOR)
            {
                error = tier1_emit_cfg_moves(emitted, e, &buffer);
            }
        }
        // The entry state: every interval that covers the block's start, where control brings it.
        for (uint32_t location = 0; location < BUSTER_TIER1_EMIT_LOCATIONS; location += 1)
        {
            transitions.owners[location] = BUSTER_IR_NONE;
        }
        for (uint32_t x = entry_offsets[b]; error == TIER1_EMIT_OK && x < entry_offsets[b + 1]; x += 1)
        {
            const uint32_t v = cfg->intervals.values[entries[x]];
            const uint32_t node = emitted->value_nodes[v];
            uint32_t location = 0;
            uint8_t kind = 0;
            error = tier1_cfg_entry_location(cfg, v, entry, &location, &kind) ? error : TIER1_EMIT_ALLOCATION;
            const bool held = error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_emit_cfg_register_location(location);
            transitions.registers[node] = held ? (uint8_t)location : UINT8_MAX;
            if (held)
            {
                transitions.owners[location] = node;
            }
            // A slot the run carries in (#2870), or one current at the block's start (#2553), holds the value too: its
            // store may be left out in this block. Each stamp of the block's run is the run's or this block's.
            if (emitted->entry_slots != 0 && error == TIER1_EMIT_OK && kind != BUSTER_TIER1_ALLOCATE_SPILL && transitions.slot_stamps[node] == transitions.stamp)
            {
                emitted->entry_slots[slot_count] = v;
                slot_count += 1;
            }
        }
        if (emitted->entry_slot_offsets != 0)
        {
            emitted->entry_slot_offsets[b + 1] = slot_count;
        }
        const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
        const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
        // No load is owed at entry. Carry private call-save rows only from the sole predecessor already emitted.
        // Joins and backward layout edges start unknown, so a loop cannot carry an earlier incarnation of a name.
        // A carried-in name cannot be defined here: this block would then dominate its own sole predecessor.
        // Edge moves use allocation slots, not call-save rows. With certify, publish these untrusted extra homes;
        // each is proved from the predecessor's actual records, not from this carry heuristic (#1270).
        Tier1EmitSaves saves;
        tier1_emit_saves_reset(&saves);
        if (exit_rows != 0 && only < b)
        {
            memcpy(saves.rows, exit_rows + (uint64_t)only * BUSTER_TIER1_EMIT_GENERAL_REGISTERS, sizeof(saves.rows));
        }
        if (emitted->entry_rows != 0)
        {
            memcpy(emitted->entry_rows + (uint64_t)b * BUSTER_TIER1_EMIT_GENERAL_REGISTERS, saves.rows, sizeof(saves.rows));
        }
        for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
        {
            const uint32_t op = function->op[node];
            const uint32_t use = base + 2 * node;
            const uint32_t v = emitted->node_values[node];
            const bool selected = !plan->covered[node] && op != IR_PHI && op != IR_ASM_RESULT && !tier1_select_entry_parameter(function, node);
            // A row call (#141) is no call of a function's: its records are tier1_emit_cfg_row's.
            const bool calls = tier1_select_function_call(function, node);
            code->node_first[node] = buffer.count;
            // The interval a definition starts, looked up once for its owed loads and its register below.
            const uint32_t interval = v != BUSTER_TIER1_CFG_NONE && selected ? tier1_cfg_interval_at(cfg, v, use + 1) : BUSTER_TIER1_CFG_NONE;
            // The owed loads this node needs, first. Its definition's register is its interval's first, a call's result's
            // excepted: the call drops the owed values it doesn't save. A return's other owed values are dead.
            const bool leaves = op == IR_RET || op == IR_UNREACHABLE;
            if (saves.owed != 0)
            {
                const bool general = !calls && interval != BUSTER_TIER1_CFG_NONE && tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_GPR;
                const uint8_t defined = general ? allocation->registers[allocation->fragment_offsets[interval]] : UINT8_MAX;
                const uint32_t repaid = tier1_emit_owed(plan, &emitted->prepared, allocation, &transitions, node, use, defined, selected, node + 1 == end && !leaves, saves.owed);
                error = repaid != 0 ? tier1_emit_repay(emitted, &saves, repaid, use, &buffer) : error;
            }
            error = error == TIER1_EMIT_OK ? tier1_emit_phase(&emitted->prepared, &view, &emitted->frame, &transitions, use, &buffer, emitted) : error;
            // A definition writes its interval's first register; a parameter and a phi are where the entry state put
            // them.
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected)
            {
                error = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == use + 1 ? error : TIER1_EMIT_ALLOCATION;
                // Its class's register, by location: a mask's is the opmask class's (#179). A definition left out has none.
                const uint32_t base_location = 32 * tier1_emit_class(&emitted->prepared, node);
                const bool held = error == TIER1_EMIT_OK && allocation->kinds[allocation->fragment_offsets[interval]] == BUSTER_TIER1_ALLOCATE_REGISTER;
                transitions.registers[node] = held ? (uint8_t)(base_location + allocation->registers[allocation->fragment_offsets[interval]]) : UINT8_MAX;
            }
            Tier1SelectHoles holes = { 0 };
            // A call's outgoing area in a dynamic frame (tier1_emit_cfg_call_area), released after it returns.
            uint32_t call_area = 0;
            // The arguments' registers, and an indirect call's callee's and a pointer destination's (#2820) after them.
            uint32_t argument_registers[BUSTER_TIER1_SELECT_MAX_ARGUMENTS + 2] = { 0 };
            uint8_t row_sources[BUSTER_TIER0_OPERATION_OPERANDS] = { 0 };
            if (error == TIER1_EMIT_OK && selected)
            {
                const Tier1SelectPattern* pattern = &plan->patterns[node];
                // A long double (#141) is in its frame object, no register; a definition left out writes none.
                bool registered = dropped[node] != 0 || pattern->rule == TIER1_RULE_ASM || function->type[node] == IR_VOID || function->type[node] == IR_F80 || tier1_emit_cfg_holds(emitted, node, transitions.registers[node]);
                for (uint32_t p = 0; p < BUSTER_TIER1_SELECT_VALUES; p += 1)
                {
                    const uint32_t value = pattern->values[p];
                    registered &= value == BUSTER_IR_NONE || tier1_emit_cfg_holds(emitted, value, transitions.registers[value]);
                }
                // A call's arguments past their classes' registers go to the outgoing area now, from where they are at its
                // use phase, before its definition phase's transitions may take their registers (#141); the others are in
                // registers of their classes, a floating value's an xmm one.
                const IrCall* called = calls ? &function->calls[function->b[node]] : 0;
                const uint32_t arguments = calls ? called->argument_count : 0;
                Tier1SelectPlaces places = { 0 };
                uint32_t staged = 0;
                uint32_t staged_zmm = 0;
                // A dynamic frame's call takes its outgoing area from rsp first (#141): its stores below are rsp's.
                call_area = calls ? tier1_emit_cfg_call_area(emitted, tier1_select_call_stack_bytes(function, called)) : 0;
                error = error == TIER1_EMIT_OK ? tier1_emit_cfg_adjust(call_area, false, emitted->frame.probe_stack, &buffer) : error;
                tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
                for (uint32_t u = plan->effective_use_offsets[node]; calls && u < plan->effective_use_offsets[node + 1]; u += 1)
                {
                    const uint32_t value = plan->effective_use_values[u];
                    const uint32_t argument = u - plan->effective_use_offsets[node];
                    const Tier1SelectPlace place = argument < arguments ? tier1_select_argument_place(function, called, argument, &places) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
                    const bool stacked = place.kind == BUSTER_TIER1_PLACE_STACK;
                    const bool aggregate = place.kind == BUSTER_TIER1_PLACE_AGGREGATE;
                    argument_registers[argument] = transitions.registers[value];
                    registered &= stacked || aggregate || tier1_emit_cfg_holds(emitted, value, transitions.registers[value]);
                    if (stacked && registered && error == TIER1_EMIT_OK)
                    {
                        error = tier1_emit_cfg_stack_argument(emitted, value, use, BUSTER_ABI_SLOT_BYTES * place.index, &buffer);
                        tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_MOVE, use), value);
                    }
                    // A by-value aggregate (#789): its bytes from its address now, before the moves, on the stack whole into
                    // its slots, else each part in a register into the next staging slot, which tier1_emit_call
                    // loads its register from after the moves, a 64-byte one into the next of zmm16 and on (#141), and a part
                    // in a stack slot (#141) into that slot.
                    const IrPassed* record = aggregate ? tier1_select_argument_record(function, called, argument) : 0;
                    Tier1SelectPart parts[2];
                    const bool whole = record != 0 && tier1_select_location_whole(record->location);
                    const uint32_t part_count = record != 0 && !whole ? tier1_select_location_parts(record->location, record->size, parts) : 0;
                    if (whole && registered && error == TIER1_EMIT_OK)
                    {
                        error = tier1_emit_cfg_aggregate_copy(emitted, &transitions, saves.owed, value, use, X86_REGISTER_RSP, (int32_t)record->location.stack_offset, 0, record->size, &buffer);
                    }
                    for (uint32_t k = 0; registered && error == TIER1_EMIT_OK && k < part_count; k += 1)
                    {
                        const bool slot = parts[k].reg == BUSTER_ABI_REGISTER_NONE;
                        // A whole vector register's part (#141) takes its bytes' slots, from the lowest; a 64-byte one the
                        // next of zmm16 and on, as tier1_emit_call numbers them.
                        const bool zmm = !slot && tier1_select_part_in_zmm(parts[k].bytes);
                        const uint32_t slots = slot ? 0 : tier1_select_argument_part_slots(parts[k].bytes);
                        const Tier1EmitInto to = slot ? (Tier1EmitInto){ .displacement = (int32_t)record->location.stack_offset, .base = X86_REGISTER_RSP, .valid = true } :
                                                  zmm ? (Tier1EmitInto){ .valid = true } : tier1_emit_argument_staging(emitted, staged, parts[k].bytes);
                        if (zmm)
                        {
                            error = staged_zmm < BUSTER_ABI_VECTOR_ARGUMENTS ? tier1_emit_cfg_aggregate_zmm(emitted, value, use, (uint8_t)(BUSTER_TIER1_SELECT_PART_ZMM + staged_zmm), parts[k].offset, parts[k].bytes, &buffer)
                                                                             : TIER1_EMIT_ALLOCATION;
                        }
                        else
                        {
                            error = to.valid ? tier1_emit_cfg_aggregate_copy(emitted, &transitions, saves.owed, value, use, to.base, to.displacement, parts[k].offset, parts[k].bytes, &buffer) : TIER1_EMIT_ALLOCATION;
                        }
                        staged += slots;
                        staged_zmm += zmm ? 1 : 0;
                    }
                    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
                }
                error = registered ? error : TIER1_EMIT_ALLOCATION;
                if (error == TIER1_EMIT_OK && !calls)
                {
                    holes = tier1_emit_cfg_holes(plan, emitted, transitions.registers, node);
                }
                // A machine row's operands' registers (#141), now, as the holes are taken (tier1_emit_cfg_row).
                for (uint32_t u = plan->effective_use_offsets[node]; pattern->rule == TIER1_RULE_ROW && u < plan->effective_use_offsets[node + 1] && u - plan->effective_use_offsets[node] < BUSTER_TIER0_OPERATION_OPERANDS; u += 1)
                {
                    row_sources[u - plan->effective_use_offsets[node]] = transitions.registers[plan->effective_use_values[u]];
                }
                // An asm body's register operands' (#2144), each in a register of its class (tier1_emit_cfg_asm moves it
                // into its own).
                for (uint32_t u = plan->effective_use_offsets[node]; pattern->rule == TIER1_RULE_ASM && u < plan->effective_use_offsets[node + 1] && u - plan->effective_use_offsets[node] < BUSTER_TIER1_SELECT_MAX_ARGUMENTS; u += 1)
                {
                    const uint32_t value = plan->effective_use_values[u];
                    argument_registers[u - plan->effective_use_offsets[node]] = transitions.registers[value];
                    error = tier1_emit_cfg_holds(emitted, value, transitions.registers[value]) ? error : TIER1_EMIT_ALLOCATION;
                }
            }
            if (error == TIER1_EMIT_OK)
            {
                error = tier1_emit_phase(&emitted->prepared, &view, &emitted->frame, &transitions, use + 1, &buffer, emitted);
            }
            if (error == TIER1_EMIT_OK && calls)
            {
                // The registers of values that live past the call: their intervals cover its definition phase.
                uint32_t saved = 0;
                for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
                {
                    const uint32_t owner = transitions.owners[reg];
                    const bool live = owner != BUSTER_IR_NONE && owner != node && transitions.registers[owner] == reg && tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                    // A callee-saved register's value survives the call.
                    saved |= live && ((emitted->callee_registers >> reg) & 1) == 0 ? 1u << reg : 0;
                }
                // And the masks that live past it (#179), which no opmask register keeps across a call: each saved in its
                // register's row before the call and reloaded after, by kmov of its width.
                uint32_t masks_saved = 0;
                for (uint32_t k = 1; k < 8; k += 1)
                {
                    const uint32_t location = BUSTER_TIER1_EMIT_MASK_LOCATION + k;
                    const uint32_t owner = transitions.owners[location];
                    const bool live = owner != BUSTER_IR_NONE && owner != node && transitions.registers[owner] == location && tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                    masks_saved |= live ? 1u << k : 0;
                }
                error = (masks_saved & ~emitted->prepared.mask_save_mask) == 0 ? error : TIER1_EMIT_ALLOCATION;
                // And the floating values that live past it (#141), which no xmm register keeps across a call: each saved
                // in its register's row before the call and reloaded after, its 8 bytes by movsd, or in a function with
                // vectors (#141) the widest one's bytes by vmovdqu32. The call's own result can't share a register with one.
                uint32_t vectors_saved = 0;
                for (uint32_t x = 0; x < 32; x += 1)
                {
                    const uint32_t location = BUSTER_TIER1_EMIT_VECTOR_LOCATION + x;
                    const uint32_t owner = transitions.owners[location];
                    const bool live = owner != BUSTER_IR_NONE && owner != node && transitions.registers[owner] == location && tier1_cfg_interval_at(cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
                    vectors_saved |= live ? 1u << x : 0;
                }
                error = (vectors_saved & ~emitted->prepared.vector_save_mask) == 0 ? error : TIER1_EMIT_ALLOCATION;
                error = tier1_select_float_width(function->type[node]) == 0 || ((vectors_saved >> (transitions.registers[node] & 15)) & 1) == 0 ? error : TIER1_EMIT_ALLOCATION;
                for (uint32_t pass = 0; pass < 2; pass += 1)
                {
                    for (uint32_t x = 0; error == TIER1_EMIT_OK && x < 32; x += 1)
                    {
                        const uint32_t row = tier1_emit_cfg_vector_save_row(emitted, x);
                        const bool listed = ((vectors_saved >> x) & 1) != 0;
                        error = !listed || row < emitted->frame.count ? error : TIER1_EMIT_FRAME;
                        if (error == TIER1_EMIT_OK && listed)
                        {
                            const X86Instruction record = {
                                .displacement = emitted->frame.offset[row],
                                .reg = (uint8_t)x,
                                .rm = (uint8_t)emitted->frame.base_register,
                                .mode = BUSTER_X86_MEMORY_BASE,
                                .form = emitted->prepared.vector_bytes != 0 ? (pass == 0 ? tier1_select_class_forms(emitted->prepared.vector_bytes, tier1_emit_encoding(&emitted->prepared))->store : tier1_select_class_forms(emitted->prepared.vector_bytes, tier1_emit_encoding(&emitted->prepared))->load) :
                                        pass == 0 ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSD_X_XM64,
                            };
                            error = tier1_emit_append(&buffer, record) ? error : TIER1_EMIT_MEMORY;
                            emitted->call_stores += pass == 0;
                            emitted->call_loads += pass == 1;
                            tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_MOVE, use + 1), BUSTER_IR_NONE);
                        }
                    }
                    for (uint32_t k = 1; error == TIER1_EMIT_OK && k < 8; k += 1)
                    {
                        const uint32_t row = tier1_emit_cfg_mask_save_row(emitted, k);
                        const uint32_t owner = transitions.owners[BUSTER_TIER1_EMIT_MASK_LOCATION + k];
                        const bool listed = ((masks_saved >> k) & 1) != 0;
                        error = !listed || row < emitted->frame.count ? error : TIER1_EMIT_FRAME;
                        if (error == TIER1_EMIT_OK && listed)
                        {
                            const Tier1SelectMaskForms* forms = tier1_select_mask_forms(emitted->prepared.sizes[owner]);
                            const X86Instruction record = {
                                .displacement = emitted->frame.offset[row],
                                .reg = (uint8_t)k,
                                .rm = (uint8_t)emitted->frame.base_register,
                                .mode = BUSTER_X86_MEMORY_BASE,
                                .form = pass == 0 ? forms->store : forms->load,
                            };
                            error = tier1_emit_append(&buffer, record) ? error : TIER1_EMIT_MEMORY;
                            emitted->call_stores += pass == 0;
                            emitted->call_loads += pass == 1;
                            tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_MOVE, use + 1), BUSTER_IR_NONE);
                        }
                    }
                    if (error == TIER1_EMIT_OK && pass == 0)
                    {
                        // An aggregate result's object (#789): the frame object the call's into operand addresses, or
                        // none where it is a pointer value (#2820), which the call's moves put in rdi.
                        const uint32_t into_node = function->c[node];
                        Tier1EmitInto into = { .base = (uint8_t)emitted->frame.base_register };
                        into.valid = into_node < count && function->op[into_node] == IR_STACK_ADDR && tier1_emit_cfg_object(function, emitted, function->a[into_node], &into.displacement);
                        // A long double result (#141) goes from st0 into the call's own object.
                        into.valid = function->type[node] == IR_F80 ? tier1_emit_cfg_x87_offset(function, emitted, node, &into.displacement) : into.valid;
                        error = function->type[node] != IR_F80 || into.valid ?
                                tier1_emit_call(plan, node, argument_registers, saved, tier1_emit_kept(&saves, &transitions, saved), transitions.registers[node], &into, &buffer, scratch, emitted) :
                                TIER1_EMIT_FRAME;
                        tier1_emit_saved(&saves, &transitions, saved);
                    }
                }
                error = error == TIER1_EMIT_OK ? tier1_emit_cfg_adjust(call_area, true, false, &buffer) : error;
                tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
            }
            else if (error == TIER1_EMIT_OK && selected && (op == IR_BR || op == IR_CBR || (op == IR_BR_INDIRECT && input->successor_offsets[b + 1] - input->successor_offsets[b] == 1)))
            {
                // The terminator, by its block's edges (one for a conditional branch whose successors are one block, and
                // for an indirect jump that can go to one label alone, which jumps there).
                const uint32_t e_first = input->successor_offsets[b];
                if (input->successor_offsets[b + 1] - e_first == 1)
                {
                    const uint32_t s = input->successors[e_first];
                    if (cfg->edge_placements[e_first] == TIER1_CFG_EDGE_PREDECESSOR)
                    {
                        error = tier1_emit_cfg_moves(emitted, e_first, &buffer);
                    }
                    if (error == TIER1_EMIT_OK && s != b + 1)
                    {
                        error = tier1_emit_cfg_jump(&buffer, X86_FORM_JMP_REL8, s, pending_branches, pending_goals, &pending, emitted);
                    }
                }
                else
                {
                    // The comparison's records, with any frame object's address resolved as in every other kernel.
                    // Its last two records are the conditional jump and the jump; its one symbolic reference is a
                    // comparison's folded symbol's (#2573), among the records before them.
                    Tier1SelectRecords selected_records = { 0 };
                    LinkReference reference = { 0 };
                    bool referenced = false;
                    error = tier1_emit_cfg_kernel(plan, emitted, &holes, node, &selected_records, &reference, &referenced);
                    const uint32_t compares = selected_records.count >= 2 ? selected_records.count - 2u : 0;
                    error = error != TIER1_EMIT_OK || ((!referenced || reference.instruction < compares) && !selected_records.overflowed && selected_records.count >= 2 &&
                                                     (!selected_records.requested || selected_records.request < compares)) ? error : TIER1_EMIT_INVALID;
                    const uint32_t compare_first = buffer.count;
                    if (error == TIER1_EMIT_OK && referenced)
                    {
                        error = emitted->reference_count < emitted->reference_capacity ? error : TIER1_EMIT_INVALID;
                        reference.instruction += compare_first;
                        if (error == TIER1_EMIT_OK)
                        {
                            emitted->references[emitted->reference_count++] = reference;
                        }
                    }
                    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < compares; r += 1)
                    {
                        error = tier1_emit_append(&buffer, selected_records.records[r]) ? error : TIER1_EMIT_MEMORY;
                    }
                    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_select_requests_add(&requests, &selected_records, compare_first, buffer.records);
                        error = tier1_emit_cfg_branch(emitted, b, e_first, selected_records.records[compares].form, &buffer, stub_first, pending_branches, pending_goals, &pending);
                    }
                }
            }
            else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_X87)
            {
                error = tier1_emit_cfg_x87(function, emitted, node, &holes, &buffer);
            }
            else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_ROW)
            {
                error = tier1_emit_cfg_row(plan, emitted, &transitions, row_sources, node, use, &buffer);
            }
            else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_ASM)
            {
                // An asm body (#1839): the saves of its fixed registers' rows hold their owners now, which nothing else
                // stored since; those rows are taken as a fixed rule's are.
                error = asm_range_count < emitted->prepared.asm_count ? tier1_emit_cfg_asm(plan, emitted, &transitions, argument_registers, node, use, &buffer, asm_ranges + 2 * asm_range_count, scratch) : TIER1_EMIT_INVALID;
                asm_range_count += 1;
                tier1_emit_fixed_rows(&saves, tier1_select_node_fixed(function, &plan->patterns[node]));
            }
            else if (error == TIER1_EMIT_OK && selected && plan->patterns[node].rule == TIER1_RULE_VECTOR && plan->patterns[node].scale == BUSTER_TIER1_VECTOR_CONSTANT)
            {
                // A literal's load names it (#2744): its reference, by its index among the code's records.
                const uint32_t literal = tier1_emit_cfg_vector_uniform(function, &holes) ? UINT32_MAX : tier1_emit_cfg_literal(emitted, function, (uint32_t)holes.immediate, holes.width);
                LinkReference literal_reference = { 0 };
                bool referenced = false;
                error = tier1_emit_cfg_vector_constant(function, &holes, literal, &buffer, &literal_reference, &referenced);
                error = error != TIER1_EMIT_OK || !referenced || emitted->reference_count < emitted->reference_capacity ? error : TIER1_EMIT_INVALID;
                if (error == TIER1_EMIT_OK && referenced)
                {
                    emitted->references[emitted->reference_count++] = literal_reference;
                }
                // Its records are its node's, which the code's certificate regenerates (#1284).
                tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
            }
            else if (error == TIER1_EMIT_OK && selected && emitted->frame.probe_stack &&
                     (plan->patterns[node].rule == TIER1_RULE_ALLOCA || plan->patterns[node].rule == TIER1_RULE_ALLOCA_IMMEDIATE))
            {
                // A dynamic allocation under stack-clash protection (#177): its probes' loop.
                error = tier1_emit_cfg_probed_allocation(&holes, &buffer);
                tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
            }
            else if (error == TIER1_EMIT_OK && selected && tier1_select_fixed_registers(plan->patterns[node].rule) != 0)
            {
                error = tier1_emit_cfg_fixed(plan, emitted, &transitions, node, use, &holes, &buffer);
                tier1_emit_fixed_rows(&saves, tier1_select_fixed_registers(plan->patterns[node].rule));
            }
            else if (error == TIER1_EMIT_OK && selected && dropped[node] != 0)
            {
                // A recomputed value that nothing reads before its recomputation takes over: no records here.
                emitted->dropped_definitions += 1;
            }
            else if (error == TIER1_EMIT_OK && selected)
            {
                Tier1SelectRecords selected_records = { 0 };
                LinkReference kernel_reference = { 0 };
                bool referenced = false;
                error = tier1_emit_cfg_kernel(plan, emitted, &holes, node, &selected_records, &kernel_reference, &referenced);
                // A symbol's address's reference is one of its records': the kernel's record, by its index there, and so
                // is a memory operand's folded symbol's (#2573).
                if (error == TIER1_EMIT_OK && referenced)
                {
                    error = emitted->reference_count < emitted->reference_capacity ? error : TIER1_EMIT_INVALID;
                    kernel_reference.instruction += buffer.count;
                    if (error == TIER1_EMIT_OK)
                    {
                        emitted->references[emitted->reference_count++] = kernel_reference;
                    }
                }
                const bool returns_here = op == IR_RET;
                error = !returns_here || (selected_records.count != 0 && selected_records.records[selected_records.count - 1].form == X86_FORM_RET) ? error : TIER1_EMIT_INVALID;
                // A return's epilogue goes before its ret, the last record, after any request's record.
                const uint32_t record_first = buffer.count;
                for (uint32_t r = 0; error == TIER1_EMIT_OK && r < selected_records.count; r += 1)
                {
                    // An aggregate result (#789): its parts into their registers, or its bytes through the hidden pointer,
                    // from its address, the ret operand's.
                    if (returns_here && r + 1 == selected_records.count && function->result_size != 0)
                    {
                        const uint32_t address = plan->patterns[node].values[0];
                        error = tier1_emit_cfg_aggregate_result(function, emitted, address < count ? transitions.registers[address] : UINT8_MAX, &buffer);
                        tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
                    }
                    // A long double result (#141): its object's value into st0, from the frame's base before the epilogue.
                    if (error == TIER1_EMIT_OK && returns_here && r + 1 == selected_records.count && function->result_type == IR_F80)
                    {
                        int32_t result_offset = 0;
                        error = tier1_emit_cfg_x87_offset(function, emitted, function->a[node], &result_offset) ? error : TIER1_EMIT_FRAME;
                        error = error != TIER1_EMIT_OK || tier1_emit_cfg_x87_append(emitted, &buffer, tier1_emit_cfg_x87_load(emitted, function->a[node], (uint8_t)emitted->frame.base_register, result_offset)) ? error : TIER1_EMIT_MEMORY;
                    }
                    // A dirty function's upper state cleared before its epilogue, one of its records (#2830), but where its
                    // result is live in a ymm or zmm register.
                    if (error == TIER1_EMIT_OK && returns_here && r + 1 == selected_records.count && emitted->zero_upper && !tier1_select_result_wide(function))
                    {
                        error = tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_VZEROUPPER }) ? error : TIER1_EMIT_MEMORY;
                    }
                    if (error == TIER1_EMIT_OK && returns_here && r + 1 == selected_records.count && tier1_frame_emit(&emitted->frame, true, &buffer) != TIER1_FRAME_EMIT_OK)
                    {
                        error = TIER1_EMIT_FRAME;
                    }
                    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_FRAME, 1), BUSTER_IR_NONE);
                    if (error == TIER1_EMIT_OK && !tier1_emit_append(&buffer, selected_records.records[r]))
                    {
                        error = TIER1_EMIT_MEMORY;
                    }
                    tier1_emit_mark(&buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
                }
                if (error == TIER1_EMIT_OK)
                {
                    tier1_select_requests_add(&requests, &selected_records, record_first, buffer.records);
                }
                if (error == TIER1_EMIT_OK && returns_here && emitted->exit_count < returns)
                {
                    emitted->exits[emitted->exit_count] = buffer.count - 1;
                    emitted->exit_count += 1;
                }
                else if (error == TIER1_EMIT_OK && returns_here)
                {
                    error = TIER1_EMIT_INVALID;
                }
            }
            // An indirect jump to more than one label: its split edges' stubs, after the jump the generic path wrote.
            if (error == TIER1_EMIT_OK && selected && op == IR_BR_INDIRECT && input->successor_offsets[b + 1] - input->successor_offsets[b] > 1)
            {
                error = tier1_emit_cfg_indirect(emitted, b, &buffer, stub_first, pending_branches, pending_goals, &pending);
            }
            // A definition left out writes no register, so nothing saves one for it around a call or a fixed sequence.
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected && dropped[node] == 0 && tier1_emit_cfg_holds(emitted, node, transitions.registers[node]))
            {
                transitions.owners[transitions.registers[node]] = node;
            }
            saves.owed = leaves ? 0 : saves.owed;
            if (error != TIER1_EMIT_OK && error_node != 0)
            {
                *error_node = node;
            }
        }
        // The block's last node took every owed value back, or left the function; a node that failed keeps its own error,
        // a refusal among them (a machine row's, #141), which left the block's later nodes unemitted.
        error = error != TIER1_EMIT_OK || saves.owed == 0 ? error : TIER1_EMIT_INVALID;
        // The added entry's single edge: its moves, then it falls through into the IR's entry.
        if (error == TIER1_EMIT_OK && b < added && cfg->edge_placements[0] == TIER1_CFG_EDGE_PREDECESSOR)
        {
            error = tier1_emit_cfg_moves(emitted, 0, &buffer);
        }
        if (error == TIER1_EMIT_OK && exit_rows != 0)
        {
            memcpy(exit_rows + (uint64_t)b * BUSTER_TIER1_EMIT_GENERAL_REGISTERS, saves.rows, sizeof(saves.rows));
        }
    }
    // The pending jumps' targets, now that every block and stub has its first record, and the labels'.
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < pending; k += 1)
    {
        const uint32_t goal = pending_goals[k];
        buffer.targets[pending_branches[k]] = (goal & BUSTER_TIER1_EMIT_CFG_STUB) != 0 ? stub_first[goal & ~BUSTER_TIER1_EMIT_CFG_STUB] : block_first[goal];
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_cfg_dispatch(&buffer, block_first, blocks, pending_branches, pending_goals, pending, emitted);
    }
    if (error == TIER1_EMIT_OK && certify)
    {
        memcpy(emitted->block_records, block_first, (uint64_t)blocks * 4);
        emitted->block_records[blocks] = buffer.count;
        memcpy(emitted->stub_records, stub_first, (uint64_t)input->edge_count * 4);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_labels(plan, emitted, block_first, stub_first, output, scratch);
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_select_requests_flush(&requests, buffer.records);
        tier1_emit_cfg_x87_transfers(plan, emitted, buffer.count, block_first, blocks, scratch);
    }
    if (error == TIER1_EMIT_OK && x86_validate(buffer.records, buffer.count) != buffer.count)
    {
        error = TIER1_EMIT_INVALID;
    }
    uint32_t asm_range = 0;
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < buffer.count; r += 1)
    {
        while (asm_range < asm_range_count && r >= asm_ranges[2 * asm_range + 1])
        {
            asm_range += 1;
        }
        const bool own = asm_range < asm_range_count && r >= asm_ranges[2 * asm_range];
        if (!own && !x86_target_form_allowed(allowed_forms, buffer.records[r].form))
        {
            error = TIER1_EMIT_TARGET;
        }
        else
        {
            code->cost_ids[r] = x86_zen5_id(&buffer.records[r]);
            code->unknown_costs += code->cost_ids[r] == 0;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        code->count = buffer.count;
        code->branch_count = buffer.branch_count;
        emitted->frame_event_count = buffer.event_count;
        emitted->asm_ranges = asm_ranges;
        emitted->asm_range_count = asm_range_count;
        code->node_first[count] = buffer.count;
        code->error_node = BUSTER_IR_NONE;
        code->arena_bytes = output->length - output_mark;
        emitted->transition_scratch_bytes = scratch->length - scratch_mark;
    }
    return error;
}

// Whether the speculative callee-saved preference's allocation failure must fail the function. The plain allocation
// still stands, so only a corrupt input is the allocator's own failure; a constraint or exhausted arena only drops the
// preference. A test in the Tier 1 outcome regression checks every Tier1CfgError value.
static bool tier1_emit_cfg_preference_fatal(Tier1CfgError error)
{
    return error != TIER1_CFG_OK && error != TIER1_CFG_CONSTRAINT && error != TIER1_CFG_CAPACITY;
}

// The calls before each position (#2538): `before[q]`, for q from 0 to `positions`, counts the calls of `calls` (in
// position order, each below `positions`) at positions below q. A 0/1 column of the calls' positions, one past each,
// and its prefix sum, 16 positions a step: within a register by four shifted adds, then the carry, the last lane's
// sum, broadcast into the next step.
static void tier1_emit_cfg_calls_before_vector(const uint32_t* calls, uint32_t call_count, uint32_t positions, uint32_t* before)
{
    memset(before, 0, ((uint64_t)positions + 1) * sizeof(uint32_t));
    for (uint32_t k = 0; k < call_count; k += 1)
    {
        before[calls[k] + 1] = 1;
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i last = _mm512_set1_epi32(15);
    __m512i carry = zero;
    for (uint32_t q = 0; q <= positions; q += 16)
    {
        const uint32_t left = positions + 1 - q;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        __m512i sum = _mm512_maskz_loadu_epi32(lanes, before + q);
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 15));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 14));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 12));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 8));
        sum = _mm512_add_epi32(sum, carry);
        _mm512_mask_storeu_epi32(before + q, lanes, sum);
        carry = _mm512_permutexvar_epi32(last, sum);
    }
}

// The recomputable values read on every iteration of the loops they are live across (#2574), `hot[v]` 1 for each of
// `candidates` (general values with a recipe, in no phi web, `hot[v]` 2 on entry, any other value 0). The loops are a
// value's outermost ones: a loop whose header the value is live into (an interval of it covers the header's start,
// which isn't its definition), but not its parent's. Each must be reducible, and every path from its header back to it
// must pass a block that reads the value. Lua's dispatch table is read at the end of every handler, a switch's table
// where it dispatches; a frame object's address read in a few of a loop's blocks is not, nor one read on every turn of
// an inner loop but live across its parent: the value's registers are chosen for its whole range. Such a value is worth
// a register over its loops, as LLVM's spill weights find from block frequencies: read once per iteration, it weighs as
// much as the loop's hottest value. The test walks each loop's body from its header, the value's readers stopping it,
// for a back edge; at most BUSTER_TIER1_EMIT_CFG_ITERATION_VISITS edges per block in all, past which no further value is
// found hot. False where an arena is short; a CFG the loop analysis refuses finds none. Scalar: it runs for the few values
// with recipes that cross calls (luaV_execute's are its frame objects' addresses and its dispatch table's), each walk
// bounded by its loop's blocks, and the loop analysis it runs once is dominators.c's, vector where that chooses.
// Whether the CFG has an edge to its own block or to an earlier one in layout: without one its layout is a topological
// order, and it has no loop to analyze.
static bool tier1_emit_cfg_cyclic(const Tier1CfgInput* input)
{
    bool cyclic = false;
    for (uint32_t b = 0; !cyclic && b < input->block_count; b += 1)
    {
        for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
        {
            cyclic |= input->successors[e] <= b;
        }
    }
    return cyclic;
}

static bool tier1_emit_cfg_hot_recipes(const Tier1CfgInput* input, const Tier1CfgAllocation* intervals, uint32_t candidates, Arena* output, Arena* scratch, uint8_t* hot)
{
    const uint32_t blocks = input->block_count;
    bool ok = true;
    Dominators loops = { 0 };
    const DominatorCfg graph = {
        .block_count = blocks,
        .edge_count = input->edge_count,
        .entry = 0,
        .successor_offsets = input->successor_offsets,
        .successors = input->successors,
        .predecessor_offsets = input->predecessor_offsets,
        .predecessors = input->predecessors,
    };
    DominatorOptions options = dominators_options();
    options.outputs = BUSTER_DOMINATOR_LOOPS;
    // A CFG without a backward edge in layout has no loop: most functions without one skip the analysis.
    const DominatorStatus analyzed = candidates != 0 && tier1_emit_cfg_cyclic(input) ? dominators_analyze(scratch, output, &graph, options, &loops) : DOMINATOR_INVALID;
    ok = analyzed != DOMINATOR_NO_MEMORY;
    const bool looped = analyzed == DOMINATOR_OK && loops.loop_count != 0;
    // Per block: the stamp of the value whose readers it holds, and of the walk that reached it.
    uint32_t* reads = looped ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* seen = looped ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* stack = looped ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    // Per loop, whether the value being tested is live into its header.
    uint8_t* live = looped ? ir_allocate(scratch, (uint64_t)loops.loop_count + 1, 1, &ok) : 0;
    if (reads != 0 && seen != 0)
    {
        memset(reads, 0, ((uint64_t)blocks + 1) * 4);
        memset(seen, 0, ((uint64_t)blocks + 1) * 4);
    }
    uint64_t budget = (uint64_t)BUSTER_TIER1_EMIT_CFG_ITERATION_VISITS * blocks;
    uint32_t walk = 0;
    for (uint32_t v = 0; v < input->value_count; v += 1)
    {
        const bool candidate = hot[v] == 2;
        hot[v] = 0;
        for (uint32_t u = input->use_offsets[v]; candidate && stack != 0 && u < input->use_offsets[v + 1]; u += 1)
        {
            reads[tier1_cfg_block_at(input, intervals, input->use_positions[u])] = v + 1;
        }
        // Live into each loop's header: an interval of the value covers its start, which isn't the value's definition.
        for (uint32_t l = 0; candidate && live != 0 && l < loops.loop_count; l += 1)
        {
            const uint32_t start = input->block_starts[loops.loop_header[l]];
            uint32_t low = intervals->value_interval_offsets[v];
            uint32_t high = intervals->value_interval_offsets[v + 1];
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = intervals->intervals.ends[middle] <= start ? middle + 1 : low;
                high = intervals->intervals.ends[middle] <= start ? high : middle;
            }
            live[l] = low < intervals->value_interval_offsets[v + 1] && intervals->intervals.starts[low] <= start && input->definitions[v] != start;
        }
        // Hot where it has an outermost loop and every one is read on every iteration.
        bool every = candidate && live != 0;
        bool any = false;
        for (uint32_t l = 0; every && l < loops.loop_count; l += 1)
        {
            const uint32_t parent = loops.loop_parent[l];
            const bool outermost = live[l] != 0 && (parent == BUSTER_DOMINATOR_NONE || live[parent] == 0);
            any |= outermost;
            if (outermost)
            {
                const uint32_t header = loops.loop_header[l];
                // A back edge reached without passing a reader is an iteration that doesn't read the value.
                const uint64_t* body = loops.loop_bodies + (uint64_t)l * loops.row_words;
                bool open = reads[header] != v + 1 && loops.loop_irreducible[l] == 0;
                const bool read = reads[header] == v + 1;
                uint32_t depth = 0;
                walk += 1;
                seen[header] = walk;
                stack[depth++] = header;
                while (open && depth != 0 && budget != 0)
                {
                    const uint32_t b = stack[--depth];
                    for (uint32_t e = input->successor_offsets[b]; open && budget != 0 && e < input->successor_offsets[b + 1]; e += 1)
                    {
                        const uint32_t next = input->successors[e];
                        budget -= 1;
                        open = next != header;
                        if (open && seen[next] != walk && reads[next] != v + 1 && ((body[next >> 6] >> (next & 63)) & 1) != 0)
                        {
                            seen[next] = walk;
                            stack[depth++] = next;
                        }
                    }
                }
                every = read || (open && budget != 0 && loops.loop_irreducible[l] == 0);
            }
        }
        hot[v] = every && any ? 1 : 0;
    }
    return ok;
}

// Values live across a call into callee-saved registers, which the prologue saves once, rather than in caller-saved ones
// that every call saves and restores around itself: from the plain problem's intervals (`intervals`), each value whose
// interval covers a call's definition phase, but the call's own result, is allowed the callee-saved registers alone (and a
// parameter's incoming one, its definition's), and every other value the caller-saved ones. A value whose interval covers
// an asm body's definition phase (#2144, TIER1_RULE_ASM) is kept out of the general registers the body touches, which it
// would otherwise save and restore around itself (tier1_emit_cfg_asm), and may take the callee-saved ones too where it
// crosses no call: a phi's web as one, as for calls. The preferred problem's sets, on the output arena, which
// tier1_cfg_preferred joins to each definition's registers while it is bound, and how many general values it moves
// (`crossing`, none: nothing to prefer). False where an arena is short.
//
// An interval crosses a call where the first call at or past its start is before its end and isn't its value's own
// definition. With `counted` (#2538's step 4) that is two loads of the calls before each position
// (tier1_emit_cfg_calls_before_vector): a call in [start, end), unless the first is
// the value's own, which only a call's result's interval can start at. Without it, the first call at or past the start
// by halving the calls, the form tier1_emit_cfg_two_calls keeps, which --verify compares.
static bool tier1_emit_cfg_preference(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const Tier1CfgInput* input, const Tier1CfgAllocation* intervals, bool counted, Arena* output, Arena* scratch, uint32_t** preferred, uint32_t* crossing,
                                      Tier1CfgPreference* clobbering)
{
    const IrFunction* function = plan->function;
    const uint32_t callee = emitted->callee_registers;
    const uint32_t added = input->block_count - function->block_count;
    const uint32_t base = 2 * added;
    bool ok = true;
    uint32_t* calls = ok ? ir_allocate(scratch, (uint64_t)emitted->prepared.call_count + 1, 4, &ok) : 0;
    uint32_t call_count = 0;
    for (uint32_t node = 0; ok && node < function->instruction_count; node += 1)
    {
        if (tier1_select_function_call(function, node))
        {
            calls[call_count++] = base + 2 * node + 1;
        }
    }
    uint32_t* before = ok && counted ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    if (before != 0)
    {
        tier1_emit_cfg_calls_before_vector(calls, call_count, input->position_count, before);
    }
    // The asm bodies' definition phases, in position order, and the general registers each touches.
    uint32_t* bodies = ok && emitted->prepared.asm_count != 0 ? ir_allocate(scratch, (uint64_t)emitted->prepared.asm_count + 1, 4, &ok) : 0;
    uint32_t* touched = ok && emitted->prepared.asm_count != 0 ? ir_allocate(scratch, (uint64_t)emitted->prepared.asm_count + 1, 4, &ok) : 0;
    uint32_t body_count = 0;
    for (uint32_t node = 0; ok && bodies != 0 && node < function->instruction_count && body_count < emitted->prepared.asm_count; node += 1)
    {
        if (!plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM)
        {
            bodies[body_count] = base + 2 * node + 1;
            touched[body_count++] = tier1_select_node_fixed(function, &plan->patterns[node]);
        }
    }
    uint32_t* allowed = ok ? ir_allocate(output, (uint64_t)input->value_count + 1, 4, &ok) : 0;
    // Each phi web's root (a phi and its operands, joined), and whether a member of the root's web crosses a call: the
    // whole web then takes callee-saved registers, so that a loop's back edge can keep its phi's register. And the general
    // registers of the asm bodies a member crosses (`avoided`), which the whole web keeps out of.
    uint32_t* parents = ok ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 4, &ok) : 0;
    uint8_t* crossed = ok ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    uint32_t* avoided = ok ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 4, &ok) : 0;
    for (uint32_t v = 0; ok && v < input->value_count; v += 1)
    {
        parents[v] = v;
        crossed[v] = 0;
        avoided[v] = 0;
    }
    for (uint32_t k = 0; ok && k < input->phi_count; k += 1)
    {
        uint32_t a = input->phi_values[k];
        while (parents[a] != a)
        {
            parents[a] = parents[parents[a]];
            a = parents[a];
        }
        for (uint32_t o = input->phi_operand_offsets[k]; o < input->phi_operand_offsets[k + 1]; o += 1)
        {
            uint32_t b = input->phi_operands[o];
            while (b != BUSTER_TIER1_CFG_NONE && parents[b] != b)
            {
                parents[b] = parents[parents[b]];
                b = parents[b];
            }
            if (b != BUSTER_TIER1_CFG_NONE && b != a)
            {
                parents[b] = a;
            }
        }
    }
    uint32_t count = 0;
    const uint32_t caller = input->registers[0];
    // The recomputable values that cross a call, outside phi webs (2), of which tier1_emit_cfg_hot_recipes keeps those
    // read on every iteration of a loop (1).
    uint8_t* hot = ok ? ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok) : 0;
    uint32_t candidates = 0;
    uint32_t hot_count = 0;
    for (uint32_t pass = 0; ok && pass < 2; pass += 1)
    {
        if (pass == 1 && candidates != 0)
        {
            ok = tier1_emit_cfg_hot_recipes(input, intervals, candidates, output, scratch, hot);
        }
        for (uint32_t v = 0; ok && v < input->value_count; v += 1)
        {
            const uint32_t node = emitted->value_nodes[v];
            const uint32_t own = input->definitions[v];
            // Where the value is a call's result: then its definition's interval starts at that call, which it doesn't cross.
            const bool called = before != 0 && tier1_select_function_call(function, node);
            // A mask keeps its class's registers (#179): no opmask register survives a call.
            const bool general = input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR;
            bool crosses = false;
            uint32_t avoid = 0;
            for (uint32_t i = intervals->value_interval_offsets[v]; i < intervals->value_interval_offsets[v + 1]; i += 1)
            {
                const uint32_t start = intervals->intervals.starts[i];
                const uint32_t end = intervals->intervals.ends[i];
                if (before != 0)
                {
                    // A call in [start, end), and the first of them not the value's own: none before its own in the interval.
                    const bool inside = called && start <= own && own < end;
                    crosses |= before[end] != before[start] && !(inside && before[own] == before[start]);
                }
                else
                {
                    // The first call at or past the interval's start, by halving: the calls are in position order.
                    uint32_t low = 0;
                    uint32_t high = call_count;
                    while (low < high)
                    {
                        const uint32_t middle = low + (high - low) / 2;
                        low = calls[middle] < start ? middle + 1 : low;
                        high = calls[middle] < start ? high : middle;
                    }
                    crosses |= low < call_count && calls[low] < end && calls[low] != own;
                }
                // The bodies crossed by this interval, excluding the body that defines this result snapshot.
                uint32_t first = 0;
                uint32_t past = body_count;
                while (first < past)
                {
                    const uint32_t middle = first + (past - first) / 2;
                    first = bodies[middle] < start ? middle + 1 : first;
                    past = bodies[middle] < start ? past : middle;
                }
                for (uint32_t b = first; general && b < body_count && bodies[b] < end; b += 1)
                {
                    avoid |= bodies[b] != own ? touched[b] : 0;
                }
            }
            // A parameter keeps its incoming register over its definition's interval, as in the plain allocation
            // (tier1_cfg_preferred adds it): free there too, in a callee-saved register from the entry on, it would hold one
            // for the whole entry block, which spilled more than its saves around the calls there cost (on speedtest1, a slower
            // run). A parameter the plain allocation already freed stays free, the entry's moves bringing it.
            uint32_t root = v;
            while (parents[root] != root)
            {
                root = parents[root];
            }
            const bool recipe = general && crosses && root == v && input->rematerializable != 0 && input->rematerializable[v] != 0;
            hot[v] = pass == 0 ? (recipe ? 2 : 0) : hot[v];
            candidates += pass == 0 && recipe;
            crossed[root] |= pass == 0 && crosses;
            crosses = crossed[root] != 0;
            avoided[root] |= pass == 0 ? avoid : 0;
            avoid = avoided[root];
            // A recomputable value read on every iteration of a loop (#2574) may hold a caller-saved register past calls too,
            // which evict it: recomputed after one, where it is next needed, rather than evicted first from the callee-saved
            // registers the loop's other values crowd.
            const uint32_t across = hot[v] == 1 ? callee | (caller & BUSTER_TIER1_EMIT_CFG_RESIDENT) : callee;
            allowed[v] = general ? (crosses ? across : avoid != 0 ? caller | callee : caller) & ~avoid : input->allowed_registers[v];
            count += pass == 1 && general && (crosses || avoid != 0);
            hot_count += pass == 1 && hot[v] == 1 && (caller & BUSTER_TIER1_EMIT_CFG_RESIDENT) != 0;
        }
    }
    // The calls' definition phases clobber the caller-saved registers in the preferred problem, where a hot value leaves
    // one of them: its recomputation, where it is next needed, stands for a save and a reload around the call.
    uint32_t* clobbers = ok && hot_count != 0 ? ir_allocate(output, (uint64_t)call_count + 1, 4, &ok) : 0;
    if (clobbers != 0 && call_count != 0)
    {
        memcpy(clobbers, calls, (uint64_t)call_count * 4);
    }
    *clobbering = (Tier1CfgPreference){
        .clobbers = clobbers,
        .clobber_count = clobbers != 0 ? call_count : 0,
        .clobber_registers = {
            clobbers != 0 ? caller : 0,
            0,
            0,
        },
    };
    *preferred = allowed;
    *crossing = count;
    return ok;
}

// The callee-saved registers the general class's fragments took (a mask's k3 is no rbx), which the frame saves.
static uint32_t tier1_emit_cfg_callee_used(const Tier1CfgAllocation* cfg, uint32_t callee)
{
    uint32_t used = 0;
    const Tier1Allocation* allocation = &cfg->allocation;
    for (uint32_t i = 0; i < cfg->intervals.count; i += 1)
    {
        for (uint32_t f = allocation->fragment_offsets[i]; cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR && f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            const uint8_t reg = allocation->registers[f];
            used |= allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && reg < 16 && ((callee >> reg) & 1) != 0 ? 1u << reg : 0;
        }
    }
    return used;
}

// An allocation's outcome as the function's: a constraint the allocator met is TIER1_EMIT_ALLOCATION; and without fixed
// ranges there are no splits, each class's temporary slot a frame row of its own, so an allocation with either is too.
static Tier1EmitError tier1_emit_cfg_allocated(Tier1CfgError allocated, const Tier1CfgAllocation* cfg)
{
    const Tier1EmitError error = allocated == TIER1_CFG_OK ? TIER1_EMIT_OK : allocated == TIER1_CFG_CAPACITY ? TIER1_EMIT_MEMORY : allocated == TIER1_CFG_CONSTRAINT ? TIER1_EMIT_ALLOCATION : TIER1_EMIT_INVALID;
    return error != TIER1_EMIT_OK || (cfg->split_count == 0 && (cfg->temp_slots & ~((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1)) == 0) ? error : TIER1_EMIT_ALLOCATION;
}

// A leaf's callee-saved registers (#2819). A function with no call and no asm body prefers no register to another: every
// value takes the caller-saved ones, which cost it nothing, and where more are live than they hold, some spill, inside a
// loop on every iteration, while the callee-saved ones stay free. Where the allocation that stands (`cfg`, of `input`)
// spills and the function has a loop, its general values may take the callee-saved registers the frame doesn't reserve
// too, each range avoiding them (Tier1CfgInput.avoided_general): it takes one only where no caller-saved register is free.
// That allocation replaces the plain one, as the callee-saved preference's does (tier1_cfg_taken), where it spills fewer
// values by at least as many as the callee-saved registers it takes: a spilled value costs a store and a reload each
// time its slot is written and read, a saved register a push and a pop each call. Without a loop the two cost about the
// same, and the plain allocation keeps its red zone and its early returns free of saves. Both orders end with it, so that
// --verify compares them as they are. `error` is the order's outcome; the result is the function's.
static Tier1EmitError tier1_emit_cfg_leaf(const Tier1EmitOptions* options, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg, Tier1EmitError error)
{
    const uint32_t leaf = emitted->leaf_registers;
    if (error == TIER1_EMIT_OK && leaf != 0 && cfg->allocation.spill_count != 0 && tier1_emit_cfg_cyclic(input))
    {
        const uint64_t output_mark = output->length;
        const uint32_t caller = input->registers[BUSTER_TIER1_ALLOCATE_GPR];
        bool ok = true;
        uint32_t* allowed = ir_allocate(output, (uint64_t)input->value_count + 1, 4, &ok);
        for (uint32_t v = 0; ok && v < input->value_count; v += 1)
        {
            // Every general value may take every caller-saved register (tier1_emit_cfg_problem), and now these too.
            allowed[v] = input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR ? caller | leaf : input->allowed_registers[v];
        }
        Tier1CfgInput problem = *input;
        problem.allowed_registers = allowed;
        problem.registers[BUSTER_TIER1_ALLOCATE_GPR] = caller | leaf;
        problem.avoided_general = leaf;
        Tier1CfgAllocation second = { 0 };
        const Tier1CfgError preferred = ok ? tier1_cfg_allocate(&problem, output, scratch, &second) : TIER1_CFG_CAPACITY;
        // As the callee-saved preference's: a constraint or an exhausted arena keeps the plain allocation.
        if (tier1_emit_cfg_preference_fatal(preferred))
        {
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, preferred);
            error = TIER1_EMIT_INVALID;
        }
        const uint32_t saved = tier1_cfg_taken(preferred, &second) ? (uint32_t)__builtin_popcount(tier1_emit_cfg_callee_used(&second, leaf)) : UINT32_MAX;
        if (error == TIER1_EMIT_OK && saved != UINT32_MAX && second.allocation.spill_count + saved <= cfg->allocation.spill_count && saved != 0)
        {
            *cfg = second;
            *input = problem;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
    }
    return error;
}

// The order the CFG consumer allocated in before #2538, kept as the one-call order's reference (--verify) and for a
// problem with fixed ranges: the plain problem allocated whole, and again with every parameter free where a parameter
// bound to its incoming register over its definition's interval leaves a use there no register; then the preference from
// its intervals, and the preferred problem allocated whole again, whose allocation replaces the plain one where it is taken
// (allocated, without a split). `input` and `cfg` are the problem and the allocation as the order leaves them, and
// `crossing` the values it moved where the preference is taken.
static Tier1EmitError tier1_emit_cfg_two_calls(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg, uint32_t* crossing)
{
    *crossing = 0;
    Tier1CfgError allocated = tier1_cfg_allocate(input, output, scratch, cfg);
    if (allocated == TIER1_CFG_CONSTRAINT)
    {
        input->definition_registers = 0;
        allocated = tier1_cfg_allocate(input, output, scratch, cfg);
    }
    tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, allocated);
    Tier1EmitError error = tier1_emit_cfg_allocated(allocated, cfg);
    uint32_t* allowed = 0;
    uint32_t moved = 0;
    Tier1CfgPreference clobbering = { 0 };
    if (error == TIER1_EMIT_OK && emitted->callee_registers != 0)
    {
        error = tier1_emit_cfg_preference(plan, emitted, input, cfg, false, output, scratch, &allowed, &moved, &clobbering) ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    }
    Tier1CfgInput problem = *input;
    const Tier1CfgPreference preference = {
        .sets = allowed,
        .registers = {
            input->registers[0] | emitted->callee_registers,
            input->registers[1],
            input->registers[2],
        },
        .clobbers = clobbering.clobbers,
        .clobber_count = clobbering.clobber_count,
        .clobber_registers = {
            clobbering.clobber_registers[0],
            clobbering.clobber_registers[1],
            clobbering.clobber_registers[2],
        },
    };
    error = error == TIER1_EMIT_OK && moved != 0 && !tier1_cfg_preferred(input, &preference, output, &problem) ? TIER1_EMIT_MEMORY : error;
    if (error == TIER1_EMIT_OK && moved != 0)
    {
        Tier1CfgAllocation second;
        const Tier1CfgError preferred = tier1_cfg_allocate(&problem, output, scratch, &second);
        // The preferred problem only tries to keep values in callee-saved registers. A constraint or exhausted arena
        // discards the preference and keeps the plain allocation; a corrupt input is the allocator's own failure to report,
        // not a capability or capacity result.
        if (tier1_emit_cfg_preference_fatal(preferred))
        {
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, preferred);
            error = TIER1_EMIT_INVALID;
        }
        if (tier1_cfg_taken(preferred, &second))
        {
            *cfg = second;
            *input = problem;
            *crossing = moved;
        }
    }
    return tier1_emit_cfg_leaf(options, emitted, output, scratch, input, cfg, error);
}

// The CFG consumer's allocation (#2538): the intervals prepared once for both problems, which have the same ones without
// fixed ranges (tier1_cfg_order_prepare); the preference from those intervals; then the preferred problem assigned with
// the plain one's verdict inside its call, the plain problem assigned whole only where the preferred allocation isn't
// taken, and lookup columns, slot sharing and edge moves made once, for the allocation that stands
// (tier1_cfg_order_run). The same problem and allocation as tier1_emit_cfg_two_calls's, which --verify checks
// (tier1_emit_cfg_order_check).
static Tier1EmitError tier1_emit_cfg_one_call(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg, uint32_t* crossing)
{
    Tier1CfgOrder order;
    *crossing = 0;
    Tier1CfgError allocated = tier1_cfg_order_prepare(input, output, scratch, &order);
    tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, allocated);
    Tier1EmitError error = tier1_emit_cfg_allocated(allocated, &order.prepared.candidate);
    uint32_t* sets = 0;
    uint32_t moved = 0;
    Tier1CfgPreference clobbering = { 0 };
    if (error == TIER1_EMIT_OK && emitted->callee_registers != 0)
    {
        error = tier1_emit_cfg_preference(plan, emitted, input, &order.prepared.candidate, true, output, scratch, &sets, &moved, &clobbering) ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK)
    {
        const Tier1CfgPreference preference = {
            .sets = sets,
            .registers = {
                input->registers[0] | emitted->callee_registers,
                input->registers[1],
                input->registers[2],
            },
            .clobbers = clobbering.clobbers,
            .clobber_count = clobbering.clobber_count,
            .clobber_registers = {
                clobbering.clobber_registers[0],
                clobbering.clobber_registers[1],
                clobbering.clobber_registers[2],
            },
            .verdict = true,
        };
        const uint32_t* preferred = 0;
        Tier1CfgError second = TIER1_CFG_OK;
        bool taken = false;
        allocated = tier1_cfg_order_run(input, moved != 0 ? &preference : 0, output, scratch, &order, cfg, &preferred, &second, &taken);
        // The preferred problem only tries to keep values in callee-saved registers. A constraint or exhausted arena
        // discards the preference and keeps the plain allocation; a corrupt input is the allocator's own failure to report.
        if (allocated == TIER1_CFG_OK && tier1_emit_cfg_preference_fatal(second))
        {
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, second);
            error = TIER1_EMIT_INVALID;
        }
        else
        {
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, allocated);
            error = tier1_emit_cfg_allocated(allocated, cfg);
        }
        if (taken)
        {
            input->allowed_registers = preferred;
            input->registers[0] = preference.registers[0];
            *crossing = moved;
        }
    }
    else if (allocated == TIER1_CFG_OK)
    {
        tier1_cfg_release(&order.prepared, scratch);
    }
    return tier1_emit_cfg_leaf(options, emitted, output, scratch, input, cfg, error);
}

// --verify's check of the one-call order (#2538): today's order, tier1_emit_cfg_two_calls, run after it on the problem as
// tier1_emit_cfg_problem built it (`problem`), and the two allocations (tier1_cfg_differs), the problems as each left
// them and the values each moved compared. Both arenas go back to where they were. A difference is
// TIER1_EMIT_CERTIFICATE, with the first group of fields that differs plus BUSTER_TIER1_EMIT_CFG_ORDER as a
// TIER1_EMIT_CAUSE_CFG_ALLOCATION diagnostic's error. The reference running out of memory compares nothing: that order
// keeps two allocations' columns.
static Tier1EmitError tier1_emit_cfg_order_check(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const Tier1EmitResult* emitted, const Tier1CfgInput* problem, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitOptions quiet = *options;
    quiet.diagnostic = NULL;
    Tier1CfgInput input = *problem;
    Tier1CfgAllocation cfg = { 0 };
    uint32_t crossing = 0;
    const Tier1EmitError reference = tier1_emit_cfg_two_calls(plan, &quiet, emitted, output, scratch, &input, &cfg, &crossing);
    const Tier1CfgInput* kept = &emitted->cfg_input;
    uint32_t differs = reference == TIER1_EMIT_MEMORY ? 0 : reference != TIER1_EMIT_OK ? 15 : tier1_cfg_differs(&input, &cfg, &emitted->cfg);
    bool same = differs != 0 || reference != TIER1_EMIT_OK || (crossing == emitted->crossing_values && memcmp(input.registers, kept->registers, sizeof(input.registers)) == 0);
    same = same && (differs != 0 || reference != TIER1_EMIT_OK || ((input.definition_registers == 0) == (kept->definition_registers == 0)));
    for (uint32_t v = 0; same && differs == 0 && reference == TIER1_EMIT_OK && v < input.value_count; v += 1)
    {
        same = input.allowed_registers[v] == kept->allowed_registers[v] && (input.definition_registers == 0 || input.definition_registers[v] == kept->definition_registers[v]);
    }
    differs = same ? differs : 14;
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    if (differs != 0)
    {
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CFG_ALLOCATION, BUSTER_TIER1_EMIT_CFG_ORDER | differs);
    }
    return differs != 0 ? TIER1_EMIT_CERTIFICATE : TIER1_EMIT_OK;
}

// Refine each vector value's mask by its producer and every selected use before allocation. The arrays belong to this
// just-built problem and no allocation has borrowed them yet. One selected-kernel probe per node qualifies all its
// operands together; rows' longer operand lists use the same placement contract as emission. Phis need only their
// full-width transport, since their copies are independent of the producers' and consumers' selected forms.
static void tier1_emit_cfg_vector_constraints(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    const uint32_t encoding = tier1_emit_encoding(&emitted->prepared);
    const uint32_t low = BUSTER_TIER1_EMIT_LOW_VECTOR_REGISTERS;
    uint32_t* const allowed = (uint32_t*)emitted->cfg_input.allowed_registers;
    uint32_t* const defined = (uint32_t*)emitted->cfg_input.definition_registers;
    const uint32_t* const node_values = emitted->node_values;
    const uint8_t* const classes = emitted->cfg_input.classes;
    for (uint32_t node = 0; node < count; node += 1)
    {
        if (!plan->covered[node])
        {
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            const uint32_t roles = tier1_select_upper_vector_roles(plan, node, allowed_forms, encoding, emitted->prepared.avx2);
            const uint32_t value = node_values[node];
            if (value != BUSTER_TIER1_CFG_NONE && classes[value] == BUSTER_TIER1_ALLOCATE_VECTOR)
            {
                allowed[value] &= (roles & 1) != 0 ? UINT32_MAX : low;
            }
            const uint32_t row_index = pattern->rule == TIER1_RULE_ROW ? (uint32_t)pattern->immediate : UINT32_MAX;
            const bool row_upper = row_index < function->row_count && tier1_select_row_upper_vectors(&function->rows[row_index], encoding, emitted->prepared.zmm);
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t source = plan->effective_use_values[use];
                const uint32_t used = node_values[source];
                if (used != BUSTER_TIER1_CFG_NONE && classes[used] == BUSTER_TIER1_ALLOCATE_VECTOR)
                {
                    bool upper = pattern->rule == TIER1_RULE_ROW ? row_upper && tier1_select_upper_vector_type(function->type[source], allowed_forms, encoding) : function->op[node] == IR_PHI;
                    if (pattern->rule != TIER1_RULE_ROW && function->op[node] != IR_PHI)
                    {
                        bool named = false;
                        upper = true;
                        for (uint32_t role = 0; role < BUSTER_TIER1_SELECT_VALUES; role += 1)
                        {
                            const bool same = pattern->values[role] == source;
                            named |= same;
                            upper &= !same || ((roles >> (role + 1)) & 1) != 0;
                        }
                        upper &= named;
                    }
                    allowed[used] &= upper ? UINT32_MAX : low;
                }
            }
        }
    }
    for (uint32_t value = 0; defined != 0 && value < emitted->cfg_input.value_count; value += 1)
    {
        defined[value] &= allowed[value];
    }
}

// The CFG consumer's whole function: problem, allocation, frame and records, out of line, so that tier1_emit_function's
// straight-line path compiles as it did without it.
static __attribute__((noinline)) Tier1EmitError tier1_emit_cfg_function(const Tier1SelectPlan* plan, const IrCfg* verified_cfg, const Tier1EmitOptions* options, const uint8_t* allowed_forms, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
{
    emitted->general = true;
    // A target without AVX512VL (#1281) has no EVEX form of a 16- or 32-byte vector: its moves, spills and kernels take
    // VEX ones, and machine rows their roles below xmm16.
    emitted->prepared.vex = !x86_target_form_allowed(allowed_forms, X86_FORM_VMOVDQA64_X_XM128);
    emitted->prepared.unsigned_dword_memory_forms = tier1_emit_unsigned_dword_memory_forms(allowed_forms);
    emitted->prepared.unsigned_i64_float_forms = tier1_emit_unsigned_i64_float_forms(allowed_forms);
    emitted->prepared.avx2 = x86_target_form_allowed(allowed_forms, X86_FORM_VPCMPEQD_Y_Y_YM256);
    // A target without AVX (#141's SSE targets) allows no VEX move of an xmm register.
    emitted->prepared.sse = !x86_target_form_allowed(allowed_forms, X86_FORM_VMOVDQU_X_XM128);
    emitted->prepared.zmm = x86_target_form_allowed(allowed_forms, X86_FORM_VMOVDQA64_Z_ZM512);
    Tier1EmitError error = tier1_emit_cfg_problem(plan, verified_cfg, options, output, scratch, emitted);
    if (error == TIER1_EMIT_OK && (emitted->cfg_input.registers[BUSTER_TIER1_ALLOCATE_VECTOR] & 0xffff0000u) != 0)
    {
        tier1_emit_cfg_vector_constraints(plan, allowed_forms, emitted);
    }
    const Tier1CfgInput problem = emitted->cfg_input;
    if (error == TIER1_EMIT_OK)
    {
        // Around a fixed range a value's intervals depend on its registers, so the two problems share none: today's order.
        // The CFG consumer gives none.
        error = problem.fixed.count != 0 ? tier1_emit_cfg_two_calls(plan, options, emitted, output, scratch, &emitted->cfg_input, &emitted->cfg, &emitted->crossing_values) :
                                           tier1_emit_cfg_one_call(plan, options, emitted, output, scratch, &emitted->cfg_input, &emitted->cfg, &emitted->crossing_values);
    }
    if (error == TIER1_EMIT_OK && options->certify && problem.fixed.count == 0)
    {
        error = tier1_emit_cfg_order_check(plan, options, emitted, &problem, output, scratch);
    }
    // A leaf whose values took its callee-saved registers (tier1_emit_cfg_leaf) saves them as a function that calls does.
    emitted->callee_registers |= error == TIER1_EMIT_OK && emitted->cfg_input.avoided_general != 0 ? emitted->leaf_registers : 0;
    emitted->callee_used = error == TIER1_EMIT_OK ? tier1_emit_cfg_callee_used(&emitted->cfg, emitted->callee_registers) : 0;
    if (error == TIER1_EMIT_OK && options->certify)
    {
        // The final allocation's certificate (#939): the problem as the callee-saved step left it, and its result.
        Tier1CertReport report = { 0 };
        Tier1CertCfgStats stats = { 0 };
        const Tier1CertError certified = tier1_certificate_cfg_check(&emitted->cfg_input, &emitted->cfg, output, scratch, &report, &stats);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CERTIFICATE, certified);
        error = certified == TIER1_CERT_OK ? TIER1_EMIT_OK : certified == TIER1_CERT_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_CERTIFICATE;
        emitted->certified = error == TIER1_EMIT_OK;
        emitted->certificate_facts = stats.facts;
        emitted->certificate_edits = stats.transitions + stats.split_moves + stats.edge_moves;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_saves(plan, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        emitted->allocation = emitted->cfg.allocation;
        error = tier1_emit_cfg_frame(plan, options, output, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_records(plan, allowed_forms, options->certify || options->sources, options->sources, output, scratch, emitted, error_node);
    }
    return error;
}

