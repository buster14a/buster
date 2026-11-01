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
// (a loop's header), an empty block goes before it and base is 2. tier1_cfg_allocate, or its scalar twin, then
// allocates the selected uses over the CFG.
//
// The records, block by block in layout order: the entry's prologue, then at each block's start its single incoming
// edge's moves where tier1_cfg placed them there; each node as the straight-line consumer emits it (its fragment
// transitions at both phases, its selected records, a call's saves and argument moves, a return's epilogue); then the
// block's terminator. A jump's edge moves go before it. A conditional branch's go at its successors' starts or, for a
// split critical edge, in a stub after the branch: the edge's moves, then a jump to the successor. A jump to the
// record that follows it is left out, and a branch is inverted where its true successor follows it or where its true
// edge alone has a stub, which then follows it. An indirect jump's (#789) go at its labels' blocks' starts or in stubs
// after it, and each label's symbol where that jump enters it, its stub or its block (tier1_emit_cfg_labels); one that
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
        const bool floating = tier1_select_float_width(type) != 0 || tier1_select_float_rule(rule);
        // Arguments past their classes' registers, and parameters past them, which the CFG consumer's entry loads, are its
        // own (#141).
        const bool stacked = (op == IR_CALL && tier1_select_call_stack_bytes(function, &function->calls[function->b[node]]) != 0) ||
                             (op == IR_PARAM && tier1_select_parameter_place(function, function->a[node]).kind == BUSTER_TIER1_PLACE_STACK);
        // The dynamic stack's operations need the CFG consumer's frame pointer and call areas (#141).
        const bool dynamic = op == IR_ALLOCA || op == IR_STACK_SAVE || op == IR_STACK_RESTORE;
        chain = op != IR_PHI && op != IR_STACK_ADDR && op != IR_LIFETIME && op != IR_SYMBOL_ADDR && op != IR_CALL_INDIRECT && (op != IR_PARAM || (prefix && !narrow)) && rule != TIER1_RULE_DIVIDE && !dynamic &&
                (op != IR_CALL || function->calls[function->b[node]].abi == 0) &&
                rule != TIER1_RULE_SHIFT && !mask && !floating && !stacked;
        prefix &= op == IR_PARAM;
    }
    return chain;
}

// A register the CFG consumer's values may be in: the selector's caller-saved ones, and the callee-saved ones that values
// live across a call take (tier1_emit_cfg_callee); never rsp, rbp or the scratch register.
static bool tier1_emit_cfg_register(uint8_t reg)
{
    const uint32_t callee = (1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_R12) | (1u << X86_REGISTER_R13) | (1u << X86_REGISTER_R14) | (1u << X86_REGISTER_R15);
    return tier1_select_register_allowed(reg) || (reg < 16 && ((callee >> reg) & 1) != 0);
}

// Whether a location is an xmm register a floating value may be in (#141): xmm0 to xmm14.
static bool tier1_emit_cfg_vector_location(uint32_t location)
{
    const uint32_t x = location - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
    return x < 16 && ((BUSTER_TIER1_EMIT_VECTOR_REGISTERS >> x) & 1) != 0;
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
    return location < 16 || (location >= BUSTER_TIER1_EMIT_VECTOR_LOCATION && location < BUSTER_TIER1_EMIT_VECTOR_LOCATION + 16) || (location >= BUSTER_TIER1_EMIT_MASK_LOCATION && location < BUSTER_TIER1_EMIT_MASK_LOCATION + 8);
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
// through, which the entry stores (tier1_emit_cfg_entry). Their rows follow in that order.
static uint32_t tier1_emit_cfg_object_count(const IrFunction* function)
{
    return function->slot_count + (function->parameter_passed != 0 ? function->parameter_count + 1 : 0);
}

// The bytes frame object `object` past the IR's slots takes (#789): a by-value aggregate parameter's copy, which the entry
// stores its registers into whole, 8 bytes each, so its size rounded up to them, and the hidden pointer's 8 bytes. A
// parameter in registers of its own, or one on the stack whole (whose copy is its stack slots, tier1_emit_cfg_incoming),
// takes a row of one byte, which nothing reads, as a function without the hidden pointer does for it.
static uint32_t tier1_emit_cfg_record_object_bytes(const IrFunction* function, uint32_t object)
{
    const uint32_t parameter = object - function->slot_count;
    const IrPassed* record = parameter < function->parameter_count ? tier1_select_parameter_record(function, parameter) : 0;
    const bool copied = record != 0 && record->size != 0 && record->location.kind == BUSTER_ABI_LOCATION_REGISTERS;
    const bool hidden = parameter == function->parameter_count && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
    return copied ? (record->size + 7u) & ~7u : hidden ? 8 : 1;
}

// Whether frame object `object` has its address in the caller's frame, past the return address, `*past` bytes into the
// incoming argument area (tier1_emit_cfg_incoming): a variadic definition's overflow area (#789), past the named
// arguments' stack bytes, or a by-value aggregate parameter on the stack whole (#789), at its record's offset. The
// frame's base doesn't reach either, so neither has a frame row's address or a recipe.
static bool tier1_emit_cfg_incoming_object(const IrFunction* function, uint32_t object, uint64_t* past)
{
    const uint32_t parameter = object - function->slot_count;
    const IrPassed* record = object >= function->slot_count && parameter < function->parameter_count ? tier1_select_parameter_record(function, parameter) : 0;
    const bool stacked = record != 0 && record->size != 0 && record->location.kind == BUSTER_ABI_LOCATION_STACK;
    const bool overflow = tier1_emit_cfg_overflow_area(function, object);
    *past = overflow ? function->incoming.stack_bytes : stacked ? record->location.stack_offset : 0;
    return overflow || stacked;
}

// The frame's flags: the options', and BUSTER_TIER1_FRAME_VARIADIC for a variadic definition with a register save area
// (#789), which the frame reserves and its prologue fills from the argument registers (tier1_frame.c): the IR's save
// area is that one (tier1_emit_cfg_object).
static uint32_t tier1_emit_cfg_frame_flags(const IrFunction* function, const Tier1EmitOptions* options)
{
    bool save_area = false;
    for (uint32_t s = 0; s < function->slot_count; s += 1)
    {
        save_area |= (function->slots[s].flags & BUSTER_IR_SLOT_REGISTER_SAVE_AREA) != 0;
    }
    // The dynamic stack (#141): a frame pointer, the frame's places from rbp (or a realigned frame's from r15) and each
    // call's outgoing area at its own rsp (Tier1Frame.dynamic_outgoing).
    bool dynamic = false;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        dynamic |= function->op[node] == IR_ALLOCA || function->op[node] == IR_STACK_SAVE || function->op[node] == IR_STACK_RESTORE;
    }
    // A function with its own ABI records (#789) stages an aggregate result's parts below rsp on its way out
    // (tier1_emit_cfg_aggregate_result): its frame stays out of the red zone.
    return options->frame_flags | (function->variadic && save_area ? BUSTER_TIER1_FRAME_VARIADIC : 0) | (function->parameter_passed != 0 ? BUSTER_TIER1_FRAME_NO_RED_ZONE : 0) |
           (dynamic ? BUSTER_TIER1_FRAME_DYNAMIC_STACK : 0);
}

// The alignment's log2 that rsp keeps in a dynamic frame (#141): 16 bytes', or a realigned frame's. An allocation rounds
// its size up to it, and so does each call's outgoing area (tier1_emit_cfg_call_area).
static uint8_t tier1_emit_cfg_stack_alignment(const Tier1Frame* frame)
{
    const uint32_t alignment = frame->alignment > 16 ? frame->alignment : 16;
    return (uint8_t)__builtin_ctz(alignment);
}

// A call's outgoing area in a dynamic frame (Tier1Frame.dynamic_outgoing, #141): its stack bytes rounded up to the
// alignment rsp keeps, which the call takes from rsp before its arguments' stores and gives back after it returns; 0 for
// a call without stack arguments or in a fixed frame, whose area is the frame's own.
static uint32_t tier1_emit_cfg_call_area(const Tier1EmitResult* emitted, uint32_t stack_bytes)
{
    const uint32_t alignment = UINT32_C(1) << tier1_emit_cfg_stack_alignment(&emitted->frame);
    return emitted->frame.dynamic_outgoing && stack_bytes != 0 ? (stack_bytes + alignment - 1) & ~(alignment - 1) : 0;
}

// rsp less or plus a call's outgoing area (#141): `sub rsp, area` or `add rsp, area`.
static Tier1EmitError tier1_emit_cfg_adjust(uint32_t area, bool release, Tier1EmitBuffer* buffer)
{
    const uint16_t form = area <= 127 ? (release ? X86_FORM_ADD_RM64_IMM8 : X86_FORM_SUB_RM64_IMM8) : (release ? X86_FORM_ADD_RM64_IMM32 : X86_FORM_SUB_RM64_IMM32);
    const X86Instruction record = { .form = form, .rm = X86_REGISTER_RSP, .immediate = (int32_t)area };
    return area == 0 || tier1_emit_append(buffer, record) ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
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
// - a symbol's address, from rip or read from its GOT slot as its access says (a thread-local's the consumer refuses);
// - a frame object's address, from the frame's base, which nothing moves between the prologue and the epilogues
//   (selection refuses the dynamic stack);
// - a copy of one of them: a pointer's conversion, which the C driver's typed streams put around every address, their
//   pointers being 64-bit integers, has its operand's bits (TIER1_RULE_COPY);
// - a symbol's or a frame object's address plus a constant (a member's, or a constant index's: TIER1_RULE_ADDRESS without
//   an index), one lea at that displacement, a symbol's its reference's addend; a GOT slot's address takes no constant.
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
        const uint32_t symbol = (uint32_t)pattern->immediate;
        const uint8_t access = rule == TIER1_RULE_SYMBOL_ADDRESS && symbol < emitted->symbol_access_count ? emitted->symbol_access[symbol] : BUSTER_TIER1_ACCESS_DIRECT;
        const bool displaced = displacement != 0;
        const bool fits = displacement >= INT32_MIN && displacement <= INT32_MAX;
        uint64_t past = 0;
        if (rule == TIER1_RULE_CONSTANT && !displaced)
        {
            operations[node] = BUSTER_TIER1_REMAT_INTEGER;
            values[node] = pattern->immediate;
        }
        else if (rule == TIER1_RULE_SYMBOL_ADDRESS && fits && (access == BUSTER_TIER1_ACCESS_DIRECT || (access == BUSTER_TIER1_ACCESS_GOT && !displaced)))
        {
            operations[node] = BUSTER_TIER1_REMAT_SYMBOL;
            values[node] = symbol;
            addends[node] = displacement;
            flags[node] = BUSTER_TIER1_REMAT_STABLE | (access == BUSTER_TIER1_ACCESS_GOT ? BUSTER_TIER1_REMAT_GOT : 0);
        }
        else if (rule == TIER1_RULE_STACK_ADDRESS && fits && !tier1_emit_cfg_incoming_object(plan->function, (uint32_t)pattern->immediate, &past))
        {
            operations[node] = BUSTER_TIER1_REMAT_STACK;
            values[node] = pattern->immediate;
            addends[node] = displacement;
            flags[node] = BUSTER_TIER1_REMAT_STABLE;
        }
    }
}

// The plan's allocation problem, in `output`, which the result keeps (see the header), with the maps between nodes and
// values and the prepared columns the shared helpers read: recipes and sizes by node, the calls' save rows, and the
// frame's columns (a node's slot, each caller-save row, then the temporary slot).
static Tier1EmitError tier1_emit_cfg_problem(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    const bool vector = BUSTER_VECTOR_PATH(options->vector);
    const Tier1LivenessError checked = tier1_liveness_validate_plan(plan, scratch, vector, true);
    Tier1EmitError error = checked == TIER1_LIVENESS_OK ? TIER1_EMIT_OK : checked == TIER1_LIVENESS_MEMORY ? TIER1_EMIT_MEMORY : checked == TIER1_LIVENESS_LIMIT ? TIER1_EMIT_LIMIT : checked == TIER1_LIVENESS_UNSUPPORTED ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
    error = error == TIER1_EMIT_OK && count == 0 ? TIER1_EMIT_INVALID : error;
    IrCfg cfg = { 0 };
    if (error == TIER1_EMIT_OK)
    {
        const IrError built = ir_build_cfg(function, output, scratch, &cfg);
        error = built == IR_OK ? TIER1_EMIT_OK : built == IR_ERROR_MEMORY ? TIER1_EMIT_MEMORY : TIER1_EMIT_INVALID;
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
    uint32_t value_count = 0;
    uint32_t phi_count = 0;
    uint32_t operand_count = 0;
    uint32_t returns = 0;
    uint32_t symbols = 0;
    uint32_t fixed_saves = 0;
    uint32_t masks = 0;
    uint32_t floats = 0;
    Tier1EmitPrepared* prepared = &emitted->prepared;
    // Each node's class: the opmask class's for a mask (#179), the vector class's for a floating value (#141), the
    // general class's for anything else.
    uint8_t* node_classes = ok ? ir_allocate(output, (uint64_t)count + 1, 1, &ok) : 0;
    for (uint32_t node = 0; ok && node < count; node += 1)
    {
        const bool value = plan->covered[node] == 0 && function->type[node] != IR_VOID;
        const bool mask = tier1_select_mask_width(function->type[node]) != 0;
        const bool floating = tier1_select_float_width(function->type[node]) != 0;
        node_classes[node] = mask ? BUSTER_TIER1_ALLOCATE_MASK : floating ? BUSTER_TIER1_ALLOCATE_VECTOR : BUSTER_TIER1_ALLOCATE_GPR;
        masks += mask && value;
        floats += floating && value;
        node_values[node] = value ? value_count : BUSTER_TIER1_CFG_NONE;
        value_nodes[value_count] = node;
        value_count += value;
        phi_count += function->op[node] == IR_PHI;
        operand_count += function->op[node] == IR_PHI ? function->b[node] : 0;
        returns += function->op[node] == IR_RET;
        symbols += function->op[node] == IR_SYMBOL_ADDR;
        // A division's rax and rdx, and a shift's rcx, may need call-save rows for the values it doesn't take.
        const uint8_t rule = plan->covered[node] ? TIER1_RULE_NONE : plan->patterns[node].rule;
        fixed_saves |= tier1_select_fixed_registers(rule);
        emitted->fixed_count += tier1_select_fixed_registers(rule) != 0;
        if (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT)
        {
            prepared->call_count += 1;
            // The argument registers of each class the calls take, and the outgoing area: the most stack slots a call's
            // arguments past their classes' registers take (#141).
            const IrCall* call = &function->calls[function->b[node]];
            Tier1SelectPlaces places = { 0 };
            for (uint32_t argument = 0; call->abi == 0 && argument < call->argument_count; argument += 1)
            {
                tier1_select_next_place(&places, tier1_select_argument_type(function, call, argument));
            }
            // A call with ABI records (#789): its records' totals.
            const AbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1].call : 0;
            const uint32_t general = abi != 0 ? abi->integer_registers : places.general;
            const uint32_t vectors = abi != 0 ? abi->vector_registers : places.vector;
            const uint32_t stacked = tier1_select_call_stack_bytes(function, call);
            prepared->call_abi.integer_registers = prepared->call_abi.integer_registers > general ? prepared->call_abi.integer_registers : (uint8_t)general;
            prepared->call_abi.vector_registers = prepared->call_abi.vector_registers > vectors ? prepared->call_abi.vector_registers : (uint8_t)vectors;
            prepared->call_abi.stack_bytes = prepared->call_abi.stack_bytes > stacked ? prepared->call_abi.stack_bytes : stacked;
        }
    }
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    // A reference for each call and each symbol's address, and how the symbols are reached.
    emitted->reference_capacity = prepared->call_count + symbols;
    emitted->symbol_access = options->symbol_access;
    emitted->symbol_access_count = options->symbol_access != 0 ? options->symbol_count : 0;
    prepared->call_abi.stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    prepared->call_abi.integer_registers = prepared->call_abi.integer_registers < BUSTER_ABI_INTEGER_ARGUMENTS ? prepared->call_abi.integer_registers : BUSTER_ABI_INTEGER_ARGUMENTS;
    prepared->call_save_mask = (prepared->call_count != 0 ? options->register_mask : 0) | (fixed_saves & options->register_mask);
    const uint32_t call_slots = (uint32_t)__builtin_popcount(prepared->call_save_mask);
    // The slots' columns: a value's at its node, the general registers' caller-save rows', the opmask registers' (one
    // per register, whichever the calls save: tier1_emit_cfg_mask_saves), the xmm registers' (#141), the three classes'
    // temporary slots', then each frame object's, live throughout until its lifetime markers say otherwise
    // (tier1_emit_cfg_lifetimes): the IR's slots, and past them the records' objects (tier1_emit_cfg_object_count), which
    // no marker names.
    const uint32_t objects = tier1_emit_cfg_object_count(function);
    prepared->classes = node_classes;
    prepared->mask_columns = masks != 0 ? 8 : 0;
    prepared->vector_columns = floats != 0 ? 16 : 0;
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
            const uint32_t record_bytes = slot == 0 ? tier1_emit_cfg_record_object_bytes(function, o) : 0;
            prepared->sizes[count + fixed_columns + o] = slot == 0 ? record_bytes : !anchor && slot->size != 0 ? (uint32_t)slot->size : 1;
            prepared->alignments[count + fixed_columns + o] = slot == 0 ? (record_bytes > 1 ? 8 : 1) : !anchor && slot->alignment != 0 ? slot->alignment : 1;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            // A mask's slot is its bytes, which kmov of its width writes and reads, and a floating value's its bytes,
            // movss's or movsd's (#141).
            const uint32_t mask_width = tier1_select_mask_width(function->type[node]);
            const uint32_t float_width = tier1_select_float_width(function->type[node]);
            widths[node] = (uint8_t)tier1_select_width(function->type[node]);
            prepared->sizes[node] = widths[node] != 0 ? widths[node] : mask_width != 0 ? mask_width : float_width != 0 ? float_width : 8;
            prepared->alignments[node] = prepared->sizes[node];
        }
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
        const bool costed = tier1_rematerialization_model_costs(&prepared->recipes, X86_REGISTER_RSP, costs, reload_costs, vector);
        prepared->recipes.costs = costs;
        prepared->recipes.reload_costs = reload_costs;
        const bool marked = costed && (vector ? tier1_rematerialization_marks(&prepared->recipes, marks) : tier1_rematerialization_marks_scalar(&prepared->recipes, marks));
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
            .call_count = prepared->call_count != 0,
            .flags = tier1_emit_cfg_frame_flags(plan->function, options),
        };
        Tier1FrameRequirements requirements = { 0 };
        error = tier1_frame_requirements(&request, &requirements) == TIER1_FRAME_OK ? TIER1_EMIT_OK : TIER1_EMIT_FRAME;
        prepared->reserved_registers = requirements.reserved_registers | (1u << BUSTER_TIER1_SELECT_SCRATCH);
    }
    const uint32_t registers = options->register_mask & ~prepared->reserved_registers;
    // The xmm registers the floating values take (#141): every one but the class's scratch, or the options' of them.
    const uint32_t vector_registers = options->vector_register_mask != 0 ? options->vector_register_mask & BUSTER_TIER1_EMIT_VECTOR_REGISTERS : BUSTER_TIER1_EMIT_VECTOR_REGISTERS;
    // The callee-saved registers values live across a call may take, but those the frame reserves (tier1_emit_cfg_callee).
    const uint32_t callee = (1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_R12) | (1u << X86_REGISTER_R13) | (1u << X86_REGISTER_R14) | (1u << X86_REGISTER_R15);
    emitted->callee_registers = prepared->call_count != 0 ? callee & ~prepared->reserved_registers : 0;
    ok = error == TIER1_EMIT_OK;
    uint32_t* block_starts = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* successors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint32_t* predecessor_offsets = ok ? ir_allocate(output, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* predecessors = ok ? ir_allocate(output, (uint64_t)edges + 1, 4, &ok) : 0;
    uint8_t* classes = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* allowed = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* defined = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint32_t* definitions = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    uint8_t* rematerializable = ok ? ir_allocate(output, (uint64_t)value_count + 1, 1, &ok) : 0;
    uint32_t* use_offsets = ok ? ir_allocate(output, (uint64_t)value_count + 1, 4, &ok) : 0;
    const uint64_t use_capacity = (uint64_t)plan->effective_use_count + value_count + 1;
    uint32_t* use_positions = ok ? ir_allocate(output, use_capacity, 4, &ok) : 0;
    uint8_t* use_required = ok ? ir_allocate(output, use_capacity, 1, &ok) : 0;
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
            const bool vector_value = classes[v] == BUSTER_TIER1_ALLOCATE_VECTOR;
            allowed[v] = classes[v] == BUSTER_TIER1_ALLOCATE_MASK ? BUSTER_TIER1_EMIT_MASK_REGISTERS : vector_value ? vector_registers : registers;
            // A parameter's incoming register, its place's (#141): a general one, or an xmm one for a floating value.
            // A by-value aggregate parameter (#789) is its copy's address, defined at its node as a frame object's is.
            const bool parameter = tier1_select_entry_parameter(function, node);
            defined[v] = parameter ? tier1_liveness_parameter_mask(function, function->a[node]) & (vector_value ? vector_registers : registers) : 0;
            definitions[v] = parameter ? 0 : op == IR_PHI ? block_starts[cfg.instruction_blocks[node] + added] : base + 2 * node + 1;
            // A mask and a floating value are never recomputed (their classes have no recipe, tier1_emit_cfg_recipes):
            // neither in a fragment's or an edge's place, nor left out as a dead definition (tier1_emit_cfg_dead_definition
            // reads this column).
            rematerializable[v] = classes[v] != BUSTER_TIER1_ALLOCATE_GPR ? 0 : marks[node];
            use_offsets[v + 1] = op != IR_PHI;
        }
        // Uses by value, counted then filled in node order, which is position order: a node's effective uses, then its
        // definition's own use. A parameter's, at position 0, precedes every other use of it.
        for (uint32_t node = 0; node < count; node += 1)
        {
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                use_offsets[node_values[plan->effective_use_values[use]] + 1] += 1;
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
            const bool call = function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT;
            const IrCall* called = call ? &function->calls[function->b[node]] : 0;
            const uint32_t arguments = call ? called->argument_count : 0;
            if (defined != BUSTER_TIER1_CFG_NONE && tier1_select_entry_parameter(function, node))
            {
                use_positions[use_cursors[defined]] = 0;
                use_required[use_cursors[defined]] = tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_STACK;
                use_cursors[defined] += 1;
            }
            Tier1SelectPlaces places = { 0 };
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t v = node_values[plan->effective_use_values[use]];
                const uint32_t argument = use - plan->effective_use_offsets[node];
                // A by-value aggregate argument's address (#789) isn't required either: its call copies its bytes from it
                // wherever it is (tier1_emit_cfg_aggregate_copy).
                const uint8_t kind = argument < arguments ? tier1_select_argument_place(function, called, argument, &places).kind : BUSTER_TIER1_PLACE_GENERAL;
                const bool stacked = kind == BUSTER_TIER1_PLACE_STACK || kind == BUSTER_TIER1_PLACE_AGGREGATE;
                use_positions[use_cursors[v]] = base + 2 * node;
                use_required[use_cursors[v]] = !stacked;
                use_cursors[v] += 1;
            }
            if (defined != BUSTER_TIER1_CFG_NONE && !tier1_select_entry_parameter(function, node) && function->op[node] != IR_PHI)
            {
                use_positions[use_cursors[defined]] = base + 2 * node + 1;
                use_required[use_cursors[defined]] = 1;
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
                if (error == TIER1_EMIT_OK && is_phi)
                {
                    phi_values[phi] = node_values[node];
                    phi_operand_offsets[phi] = operand;
                    for (uint32_t e = predecessor_offsets[b]; error == TIER1_EMIT_OK && e < predecessor_offsets[b + 1]; e += 1)
                    {
                        const uint32_t source = predecessors[e] - added;
                        uint32_t incoming = BUSTER_IR_NONE;
                        for (uint32_t pair = 0; pair < function->b[node]; pair += 1)
                        {
                            const uint32_t* entry = function->extra + function->a[node] + (uint64_t)pair * 2;
                            incoming = entry[0] == source ? entry[1] : incoming;
                        }
                        const uint32_t v = incoming < count ? node_values[incoming] : BUSTER_TIER1_CFG_NONE;
                        error = v != BUSTER_TIER1_CFG_NONE ? error : TIER1_EMIT_INVALID;
                        phi_operands[operand] = v;
                        operand += 1;
                    }
                    phi += 1;
                }
            }
        }
        phi_offsets[blocks] = phi;
        phi_operand_offsets[phi] = operand;
        emitted->cfg_input = (Tier1CfgInput){
            .block_starts = block_starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .classes = classes,
            .allowed_registers = allowed,
            .definition_registers = defined,
            .definitions = definitions,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .phi_offsets = phi_offsets,
            .phi_values = phi_values,
            .phi_operand_offsets = phi_operand_offsets,
            .phi_operands = phi_operands,
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

// The opmask registers whose masks some call saves (#179), and the xmm registers whose floating values some call saves
// (#141): each register fragment of a mask or a floating value that covers a call's definition phase, where
// tier1_emit_cfg_records saves the values that live past the call. The calls in position order, then each fragment's
// first call at or past its start, by halving.
static Tier1EmitError tier1_emit_cfg_mask_saves(const Tier1SelectPlan* plan, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t base = 2 * (emitted->cfg_input.block_count - function->block_count);
    const uint64_t mark = scratch->length;
    bool ok = true;
    const bool classes = emitted->prepared.mask_columns != 0 || emitted->prepared.vector_columns != 0;
    uint32_t* calls = classes && emitted->prepared.call_count != 0 ? ir_allocate(scratch, (uint64_t)emitted->prepared.call_count + 1, 4, &ok) : 0;
    uint32_t call_count = 0;
    for (uint32_t node = 0; ok && calls != 0 && node < function->instruction_count; node += 1)
    {
        if (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT)
        {
            calls[call_count++] = base + 2 * node + 1;
        }
    }
    uint32_t saved = 0;
    uint32_t vectors_saved = 0;
    for (uint32_t i = 0; ok && call_count != 0 && i < cfg->intervals.count; i += 1)
    {
        const uint32_t class = cfg->intervals.classes[i];
        for (uint32_t f = allocation->fragment_offsets[i]; class != BUSTER_TIER1_ALLOCATE_GPR && f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            uint32_t low = 0;
            uint32_t high = call_count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = calls[middle] < allocation->starts[f] ? middle + 1 : low;
                high = calls[middle] < allocation->starts[f] ? high : middle;
            }
            const bool covers = low < call_count && calls[low] < allocation->ends[f] && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER;
            saved |= class == BUSTER_TIER1_ALLOCATE_MASK && covers && allocation->registers[f] < 8 ? 1u << allocation->registers[f] : 0;
            vectors_saved |= class == BUSTER_TIER1_ALLOCATE_VECTOR && covers && allocation->registers[f] < 16 ? 1u << allocation->registers[f] : 0;
        }
    }
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
    const bool record = object >= function->slot_count && object < tier1_emit_cfg_object_count(function) && !tier1_emit_cfg_incoming_object(function, object, &past);
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

// Each frame object's lifetime over tier1_cfg's positions (#177), into `starts` and `ends` by slot: its whole function
// for an object without lifetime markers (IR_LIFETIME), and for one with them the hull of the positions where some path
// from a start reaches without an end. That forward problem is backward liveness on the reversed CFG, which dataflow.c
// solves (its vector path or its scalar twin): a block's last marker of the object a use where it starts and a
// definition where it ends, so that the fixpoint's in row is the object's state after the block and its out row the one
// before it. The hull takes each block's start where the object is live before it, its end where the object is live
// after it, and each marker's own position: an access through the object's address, at a node between a start and an
// end on some path, has its positions inside. Scratch keeps the dataflow's rows; the caller restores it.
static Tier1EmitError tier1_emit_cfg_lifetimes(const IrFunction* function, const Tier1CfgInput* input, bool vector, Arena* scratch, uint32_t* starts, uint32_t* ends)
{
    (void)vector;

    const uint32_t objects = function->slot_count;
    const uint32_t blocks = input->block_count;
    const uint32_t added = blocks - function->block_count;
    bool ok = true;
    uint32_t* ids = objects != 0 ? ir_allocate(scratch, objects, 4, &ok) : 0;
    Tier1EmitError error = ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    uint32_t marked = 0;
    for (uint32_t o = 0; error == TIER1_EMIT_OK && o < objects; o += 1)
    {
        ids[o] = BUSTER_IR_NONE;
        starts[o] = 0;
        ends[o] = input->position_count;
    }
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < function->instruction_count; node += 1)
    {
        if (function->op[node] == IR_LIFETIME && ids[function->a[node]] == BUSTER_IR_NONE)
        {
            ids[function->a[node]] = marked++;
        }
    }
    Dataflow lifetimes = { 0 };
    if (error == TIER1_EMIT_OK && marked != 0)
    {
        ok = dataflow_create(scratch, blocks, marked, input->edge_count, &lifetimes);
        if (ok)
        {
            memcpy(lifetimes.successor_offsets, input->predecessor_offsets, ((uint64_t)blocks + 1) * sizeof(uint32_t));
            if (input->edge_count != 0)
            {
                memcpy(lifetimes.successors, input->predecessors, (uint64_t)input->edge_count * sizeof(uint32_t));
            }
            ok = dataflow_postorder(&lifetimes, scratch);
        }
        error = ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK && marked != 0)
    {
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            uint64_t* use = lifetimes.use + (uint64_t)(b + added) * lifetimes.row_words;
            uint64_t* def = lifetimes.def + (uint64_t)(b + added) * lifetimes.row_words;
            for (uint32_t node = function->block_offsets[b]; node < function->block_offsets[b + 1]; node += 1)
            {
                if (function->op[node] == IR_LIFETIME)
                {
                    const uint32_t id = ids[function->a[node]];
                    const uint64_t bit = UINT64_C(1) << (id & 63);
                    const bool start = function->b[node] == 0;
                    use[id >> 6] = start ? use[id >> 6] | bit : use[id >> 6] & ~bit;
                    def[id >> 6] = start ? def[id >> 6] & ~bit : def[id >> 6] | bit;
                }
            }
        }
        if (BUSTER_VECTOR_PATH(vector))
        {
            (void)dataflow_liveness_vector(&lifetimes);
        }
        else
        {
            (void)dataflow_liveness_scalar(&lifetimes);
        }
        uint32_t* low = ir_allocate(scratch, marked, 4, &ok);
        uint32_t* high = ir_allocate(scratch, marked, 4, &ok);
        error = ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
        for (uint32_t id = 0; error == TIER1_EMIT_OK && id < marked; id += 1)
        {
            low[id] = UINT32_MAX;
            high[id] = 0;
        }
        for (uint32_t cb = 0; error == TIER1_EMIT_OK && cb < blocks; cb += 1)
        {
            const uint64_t* before = lifetimes.out + (uint64_t)cb * lifetimes.row_words;
            const uint64_t* after = lifetimes.in + (uint64_t)cb * lifetimes.row_words;
            for (uint64_t word = 0; word < lifetimes.row_words; word += 1)
            {
                for (uint64_t bits = before[word]; bits != 0; bits &= bits - 1)
                {
                    const uint32_t id = (uint32_t)(word * 64 + (uint64_t)__builtin_ctzll(bits));
                    low[id] = input->block_starts[cb] < low[id] ? input->block_starts[cb] : low[id];
                    high[id] = input->block_starts[cb] + 1 > high[id] ? input->block_starts[cb] + 1 : high[id];
                }
                for (uint64_t bits = after[word]; bits != 0; bits &= bits - 1)
                {
                    const uint32_t id = (uint32_t)(word * 64 + (uint64_t)__builtin_ctzll(bits));
                    high[id] = input->block_starts[cb + 1] > high[id] ? input->block_starts[cb + 1] : high[id];
                }
            }
        }
        for (uint32_t b = 0; error == TIER1_EMIT_OK && b < function->block_count; b += 1)
        {
            for (uint32_t node = function->block_offsets[b]; node < function->block_offsets[b + 1]; node += 1)
            {
                if (function->op[node] == IR_LIFETIME)
                {
                    const uint32_t id = ids[function->a[node]];
                    const uint32_t position = input->block_starts[b + added] + 2 * (node - function->block_offsets[b]);
                    low[id] = function->b[node] == 0 && position < low[id] ? position : low[id];
                    high[id] = position + 1 > high[id] ? position + 1 : high[id];
                }
            }
        }
        for (uint32_t o = 0; error == TIER1_EMIT_OK && o < objects; o += 1)
        {
            const uint32_t id = ids[o];
            if (id != BUSTER_IR_NONE && low[id] < high[id])
            {
                starts[o] = low[id];
                ends[o] = high[id];
            }
        }
    }
    return error;
}

// The frame: each spilled value's slot over its intervals' hull (a phi's widened to its predecessors' ends), the general
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
    const uint32_t mask_slots = (uint32_t)__builtin_popcount(prepared->mask_save_mask);
    const uint32_t vector_slots = (uint32_t)__builtin_popcount(prepared->vector_save_mask);
    const uint32_t temporary = (uint32_t)__builtin_popcount(cfg->temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1));
    const uint32_t temporary_columns = call_slots + prepared->mask_columns + prepared->vector_columns;
    const uint32_t fixed_columns = temporary_columns + BUSTER_TIER1_ALLOCATE_CLASSES;
    bool ok = true;
    const uint32_t objects = tier1_emit_cfg_object_count(plan->function);
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
        const uint32_t v = cfg->allocation.spill_values[k];
        const uint32_t node = emitted->value_nodes[v];
        uint32_t start = input->position_count;
        uint32_t end = 0;
        for (uint32_t i = cfg->value_interval_offsets[v]; i < cfg->value_interval_offsets[v + 1]; i += 1)
        {
            start = cfg->intervals.starts[i] < start ? cfg->intervals.starts[i] : start;
            end = cfg->intervals.ends[i] > end ? cfg->intervals.ends[i] : end;
        }
        // A phi's slot is written at its predecessors' ends, where the phi isn't live yet.
        const uint32_t home = phis[v] != 0 ? tier1_cfg_block_of(input, input->definitions[v]) : 0;
        for (uint32_t e = phis[v] != 0 ? input->predecessor_offsets[home] : 0; phis[v] != 0 && e < input->predecessor_offsets[home + 1]; e += 1)
        {
            const uint32_t exit = input->block_starts[input->predecessors[e] + 1] - 1;
            start = exit < start ? exit : start;
            end = exit + 1 > end ? exit + 1 : end;
        }
        prepared->starts[node] = start;
        prepared->ends[node] = end;
        indices[k] = node;
    }
    // The rows past the spills, each its column: the general registers' call saves, the opmask registers' (one column
    // per register), the temporary slots used, the frame objects.
    uint32_t row = spills;
    for (uint32_t slot = 0; error == TIER1_EMIT_OK && slot < call_slots; slot += 1)
    {
        indices[row++] = count + slot;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < 8; k += 1)
    {
        if (((prepared->mask_save_mask >> k) & 1) != 0)
        {
            indices[row++] = count + call_slots + k;
        }
    }
    for (uint32_t x = 0; error == TIER1_EMIT_OK && x < 16; x += 1)
    {
        if (((prepared->vector_save_mask >> x) & 1) != 0)
        {
            indices[row++] = count + call_slots + prepared->mask_columns + x;
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
        error = tier1_emit_cfg_lifetimes(plan->function, input, options->vector, scratch, prepared->starts + count + fixed_columns, prepared->ends + count + fixed_columns);
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
            .call_count = prepared->call_count != 0,
            .used_callee_saved = emitted->callee_used,
            .flags = tier1_emit_cfg_frame_flags(plan->function, options),
        };
        const Tier1FrameError framed = BUSTER_VECTOR_PATH(options->vector) ? tier1_frame_plan(&request, output, scratch, &emitted->frame) : tier1_frame_plan_scalar(&request, output, scratch, &emitted->frame);
        error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_FRAME;
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
    const uint32_t row = index < spills ? index : tier1_emit_cfg_temporary_row(emitted, index - spills);
    const bool found = location >= BUSTER_TIER1_CFG_SPILL && (index < spills || temporary) && row < emitted->frame.count;
    *offset = found ? emitted->frame.offset[row] : 0;
    return found;
}

// A call's argument past its class's registers (#141), node `value`'s, into its 8-byte slot `slot` bytes into the
// outgoing area from rsp: from where the allocation has it at the call's use phase `position`, which needn't be a
// register (its use isn't required): from its register (a floating value's xmm one by movss or movsd), from its slot
// through the scratch register, or recomputed into the scratch register. The slot's 8 bytes are stored whole: an I32's
// bits past its 32, and an F32's, are the callee's to ignore, as the ABI says.
static Tier1EmitError tier1_emit_cfg_stack_argument(Tier1EmitResult* emitted, uint32_t value, uint32_t position, uint32_t slot, Tier1EmitBuffer* buffer)
{
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    const uint32_t v = emitted->node_values[value];
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    int32_t offset = 0;
    const bool found = v != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, v, position, &location, &kind);
    Tier1EmitError error = found ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    uint8_t source = BUSTER_TIER1_SELECT_SCRATCH;
    uint16_t form = X86_FORM_MOV_RM64_R64;
    if (error == TIER1_EMIT_OK && kind == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_emit_cfg_vector_location(location))
    {
        source = (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION);
        form = tier1_select_float_forms(emitted->prepared.sizes[value])->store;
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
        error = TIER1_EMIT_ALLOCATION;
    }
    const X86Instruction store = { .displacement = (int32_t)slot, .reg = source, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = form };
    return error == TIER1_EMIT_OK ? (tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY) : error;
}

// A by-value aggregate argument's bytes [from, from + size) from its address, node `value`'s at its call's use phase
// `position` (#789), to rsp + `to`: through the scratch register from the address's register, or, where the allocation has
// the address in a slot or recomputes it, from the scratch register, which takes it, through rax, which the red zone's
// save slot holds meanwhile (BUSTER_TIER1_EMIT_STAGING_SAVE). The address's use isn't required: a call may pass more
// aggregates than the registers its first allocation gives its uses.
static Tier1EmitError tier1_emit_cfg_aggregate_copy(Tier1EmitResult* emitted, uint32_t value, uint32_t position, int32_t to, uint32_t from, uint32_t size, Tier1EmitBuffer* buffer)
{
    const uint8_t base = (uint8_t)emitted->frame.base_register;
    const uint32_t v = emitted->node_values[value];
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    int32_t offset = 0;
    const bool found = v != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, v, position, &location, &kind);
    const bool registered = found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16;
    Tier1EmitError error = found ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    if (error == TIER1_EMIT_OK && !registered && kind == BUSTER_TIER1_ALLOCATE_SPILL && tier1_emit_cfg_slot(emitted, location, &offset))
    {
        const X86Instruction load = { .displacement = offset, .reg = BUSTER_TIER1_SELECT_SCRATCH, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
        error = tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
        emitted->spill_loads += 1;
    }
    else if (error == TIER1_EMIT_OK && !registered && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
    {
        error = tier1_emit_copy(emitted, &emitted->prepared.recipes, value, BUSTER_TIER1_SELECT_SCRATCH, base, buffer);
    }
    else if (error == TIER1_EMIT_OK && !registered)
    {
        error = TIER1_EMIT_ALLOCATION;
    }
    const uint8_t source = registered ? (uint8_t)location : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
    const uint8_t data = registered ? (uint8_t)BUSTER_TIER1_SELECT_SCRATCH : (uint8_t)X86_REGISTER_RAX;
    const int32_t save_slot = tier1_emit_staging(BUSTER_TIER1_EMIT_STAGING_SAVE);
    const X86Instruction save = { .displacement = save_slot, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_RM64_R64 };
    const X86Instruction restore = { .displacement = save_slot, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
    error = error != TIER1_EMIT_OK || registered || tier1_emit_append(buffer, save) ? error : TIER1_EMIT_MEMORY;
    error = error != TIER1_EMIT_OK || tier1_emit_copy_bytes(buffer, data, X86_REGISTER_RSP, to, source, (int32_t)from, size) ? error : TIER1_EMIT_MEMORY;
    error = error != TIER1_EMIT_OK || registered || tier1_emit_append(buffer, restore) ? error : TIER1_EMIT_MEMORY;
    return error;
}

// A node whose rule takes x86's fixed registers (tier1_select_fixed: a division or a shift by a register, a division by a
// constant's reciprocal), with any other value live past it in those registers, by its interval at the node's definition
// phase, saved around it in its call-save row (tier1_emit_cfg_problem gives it one; tier1_emit_fixed).
static Tier1EmitError tier1_emit_cfg_fixed(Tier1EmitResult* emitted, const Tier1EmitTransitions* transitions, uint32_t node, uint32_t use, const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    const uint32_t fixed = tier1_select_fixed_registers(holes->rule);
    uint32_t saved = 0;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        const uint32_t owner = transitions->owners[reg];
        const bool live = ((fixed >> reg) & 1) != 0 && owner != BUSTER_IR_NONE && owner != node && transitions->registers[owner] == reg &&
                          tier1_cfg_interval_at(&emitted->cfg, emitted->node_values[owner], use + 1) != BUSTER_TIER1_CFG_NONE;
        saved |= live && reg != holes->destination ? 1u << reg : 0;
    }
    return tier1_emit_fixed(emitted, saved, holes, buffer);
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
    // A function with its own ABI records (#789): each by-value aggregate parameter in registers into its copy, its parts
    // stored whole, 8 bytes each (the copy's size is rounded up to them, tier1_emit_cfg_record_object_bytes), a general
    // register's by mov and an xmm one's by movsd; and the hidden pointer an aggregate result returns through, rdi, into
    // its object. Before the moves, which may write those registers; the prologue writes none of them.
    for (uint32_t p = 0; error == TIER1_EMIT_OK && function->parameter_passed != 0 && p <= function->parameter_count; p += 1)
    {
        const IrPassed* record = p < function->parameter_count ? tier1_select_parameter_record(function, p) : 0;
        const bool copied = record != 0 && record->size != 0 && record->location.kind == BUSTER_ABI_LOCATION_REGISTERS;
        const bool hidden = p == function->parameter_count && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
        Tier1SelectPart parts[2] = { { .reg = BUSTER_ABI_RDI, .offset = 0, .bytes = 8 } };
        const uint32_t part_count = copied ? tier1_select_parts(record->location.registers, record->size, BUSTER_TIER1_SELECT_ARGUMENT_GENERAL, BUSTER_ABI_VECTOR_ARGUMENTS, parts) : hidden ? 1 : 0;
        int32_t offset = 0;
        error = (!copied && !hidden) || (part_count != 0 && tier1_emit_cfg_object(function, emitted, function->slot_count + p, &offset)) ? error : TIER1_EMIT_FRAME;
        for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
        {
            const bool vector = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
            const X86Instruction store = {
                .displacement = offset + parts[k].offset,
                .reg = (uint8_t)(parts[k].reg & 0x1f),
                .rm = (uint8_t)emitted->frame.base_register,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = vector ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOV_RM64_R64,
            };
            error = tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY;
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
            const uint16_t general_form = type == IR_I64 || type == IR_PTR || float_width == 8 ? X86_FORM_MOV_R64_RM64 : type == IR_I32 || float_width == 4 ? X86_FORM_MOV_R32_RM32 : type == IR_I16 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
            const uint8_t target = in_vector ? (uint8_t)(location - BUSTER_TIER1_EMIT_VECTOR_LOCATION) : in_register ? (uint8_t)location : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
            const X86Instruction load = { .form = in_vector ? tier1_select_float_forms(float_width)->load : general_form, .reg = target, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = offset };
            const uint32_t width = emitted->prepared.sizes[node];
            const X86Instruction store = { .displacement = slot, .reg = target, .rm = (uint8_t)emitted->frame.base_register, .mode = BUSTER_X86_MEMORY_BASE, .form = width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32 };
            error = !in_vector && !in_register && !in_slot ? TIER1_EMIT_ALLOCATION : tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
            error = error == TIER1_EMIT_OK && in_slot ? (tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY) : error;
            emitted->spill_stores += error == TIER1_EMIT_OK && in_slot;
        }
    }
    return error;
}

// Moves [first, end) of tier1_cfg's columns: registers and slots by tier1_cfg's location ids, and recomputations
// (tier1_emit_copy, a symbol's with its reference), into a slot through the scratch register.
static Tier1EmitError tier1_emit_cfg_moves(Tier1EmitResult* emitted, uint32_t first, uint32_t end, Tier1EmitBuffer* buffer)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
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
        // movss or movsd of its width: tier1_cfg moves one slot to another through xmm15, in two moves, and recomputes
        // none.
        const bool vector = tier1_emit_class(&emitted->prepared, node) == BUSTER_TIER1_ALLOCATE_VECTOR;
        const bool from_vector = from - BUSTER_TIER1_EMIT_VECTOR_LOCATION < 16;
        const bool to_vector = to - BUSTER_TIER1_EMIT_VECTOR_LOCATION < 16;
        const Tier1SelectFloatForms* floats = tier1_select_float_forms(width);
        if (vector && from_vector && to_vector)
        {
            records[record_count++] = tier1_select_float_move((uint8_t)(to & 15), (uint8_t)(from & 15));
        }
        else if (vector && from_vector && tier1_emit_cfg_slot(emitted, to, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(from & 15), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = floats->store };
            emitted->spill_stores += 1;
        }
        else if (vector && to_vector && tier1_emit_cfg_slot(emitted, from, &offset))
        {
            records[record_count++] = (X86Instruction){ .displacement = offset, .reg = (uint8_t)(to & 15), .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = floats->load };
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
// copy of one. A use at position p > the definition is node (p - base) / 2's.
static bool tier1_emit_cfg_dead_definition(const Tier1EmitResult* emitted, uint32_t v, uint32_t definition, const uint8_t* dropped, uint32_t base)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1Allocation* allocation = &cfg->allocation;
    const uint32_t interval = input->rematerializable != 0 && input->rematerializable[v] != 0 ? tier1_cfg_interval_at(cfg, v, definition) : BUSTER_TIER1_CFG_NONE;
    const uint32_t first = interval != BUSTER_TIER1_CFG_NONE ? allocation->fragment_offsets[interval] : 0;
    const bool last = interval != BUSTER_TIER1_CFG_NONE && first + 1 == allocation->fragment_offsets[interval + 1];
    const uint32_t end = interval != BUSTER_TIER1_CFG_NONE ? cfg->intervals.ends[interval] : 0;
    const bool inside = last && end != input->block_starts[tier1_cfg_block_of(input, end - 1) + 1];
    const uint32_t block = interval != BUSTER_TIER1_CFG_NONE ? tier1_cfg_block_of(input, definition) : 0;
    bool dead = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == definition && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                allocation->ends[first] < input->block_starts[block + 1] && (inside || (!last && allocation->kinds[first + 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE));
    // Uses are sorted: the definition's own (at its position), then those the first fragment serves, before its end.
    for (uint32_t u = dead ? input->use_offsets[v] : 0; dead && u < input->use_offsets[v + 1]; u += 1)
    {
        const uint32_t position = input->use_positions[u];
        dead = position <= definition || position >= allocation->ends[first] || dropped[(position - base) / 2] != 0;
    }
    return dead;
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
    return room ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
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
    Tier1EmitError error = inverse != X86_FORM_NONE ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (error == TIER1_EMIT_OK && t == b + 1 && !split_true && !split_false)
    {
        // The true successor follows: jump on the inverse to the false one, and fall through.
        error = tier1_emit_cfg_jump(buffer, inverse, f, pending_branches, pending_goals, pending, emitted);
    }
    else if (error == TIER1_EMIT_OK && split_true && !split_false)
    {
        // Only the true edge has a stub: jump on the inverse to the false successor and fall into the stub, so that the
        // true path (a loop's back edge, often) takes one jump rather than two.
        error = tier1_emit_cfg_jump(buffer, inverse, f, pending_branches, pending_goals, pending, emitted);
        stub_first[e_true] = buffer->count;
        emitted->edge_stubs += 1;
        error = error == TIER1_EMIT_OK ? tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_true], cfg->edge_move_offsets[e_true + 1], buffer) : error;
        if (error == TIER1_EMIT_OK && t != b + 1)
        {
            error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, t, pending_branches, pending_goals, pending, emitted);
        }
    }
    else if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_jump(buffer, condition, split_true ? BUSTER_TIER1_EMIT_CFG_STUB | e_true : t, pending_branches, pending_goals, pending, emitted);
        // The false edge: its stub falls through from the branch, or a jump, unless its block follows.
        if (error == TIER1_EMIT_OK && split_false)
        {
            stub_first[e_false] = buffer->count;
            emitted->edge_stubs += 1;
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_false], cfg->edge_move_offsets[e_false + 1], buffer);
        }
        if (error == TIER1_EMIT_OK && (f != b + 1 || split_true))
        {
            error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, f, pending_branches, pending_goals, pending, emitted);
        }
        if (error == TIER1_EMIT_OK && split_true)
        {
            stub_first[e_true] = buffer->count;
            emitted->edge_stubs += 1;
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_true], cfg->edge_move_offsets[e_true + 1], buffer);
            if (error == TIER1_EMIT_OK && t != b + 1)
            {
                error = tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, t, pending_branches, pending_goals, pending, emitted);
            }
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
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e], cfg->edge_move_offsets[e + 1], buffer);
            error = error == TIER1_EMIT_OK ? tier1_emit_cfg_jump(buffer, X86_FORM_JMP_REL8, input->successors[e], pending_branches, pending_goals, pending, emitted) : error;
        }
    }
    return error;
}

// Each of the function's labels' first record (Tier1EmitResult.label_records, #789): its block's, or where the indirect
// jump that may go to it has a stub for that edge, the stub's, which the jump reaches through the label's address alone.
// A label no block holds, in a function without br_indirect, gets the function's first record. An indirect jump with one
// successor jumps there directly, and its label is its block's. A label is one address for every indirect jump that may
// go to it. The C lowering puts each on one jump's list (a jump table's, or the dispatch block's of GNU C's goto *), but
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
        records[k] = function->labels[k] == BUSTER_IR_NONE ? 0 : block_first[function->labels[k] + added];
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
// address the entry stored (tier1_emit_cfg_entry), which rax returns. Before the epilogue, which moves rsp past the
// staging slots.
static Tier1EmitError tier1_emit_cfg_aggregate_result(const IrFunction* function, const Tier1EmitResult* emitted, uint8_t source, Tier1EmitBuffer* buffer)
{
    const AbiReturn result = function->result_abi;
    Tier1SelectPart parts[2];
    const uint32_t part_count = result.kind == BUSTER_ABI_RESULT_REGISTERS ? tier1_select_parts(result.registers, function->result_size, BUSTER_TIER1_SELECT_RESULT_GENERAL, 2, parts) : 0;
    const bool hidden = result.kind == BUSTER_ABI_RESULT_MEMORY;
    int32_t hidden_offset = 0;
    const bool found = hidden && tier1_emit_cfg_object(function, emitted, function->slot_count + function->parameter_count, &hidden_offset);
    Tier1EmitError error = source < 16 && source != BUSTER_TIER1_SELECT_SCRATCH && (part_count != 0 || found) ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
    {
        error = tier1_emit_copy_bytes(buffer, BUSTER_TIER1_SELECT_SCRATCH, X86_REGISTER_RSP, tier1_emit_staging(k), source, parts[k].offset, parts[k].bytes) ? error : TIER1_EMIT_MEMORY;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
    {
        const bool vector_part = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const X86Instruction load = {
            .displacement = tier1_emit_staging(k),
            .reg = (uint8_t)(parts[k].reg & 0x1f),
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = vector_part ? X86_FORM_MOVSD_X_XM64 : X86_FORM_MOV_R64_RM64,
        };
        error = tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
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

// The records a function's ABI records take past every node's share (#789): per call with records, two per 8 bytes of
// its stack area and four per argument for its stack aggregates' copies, five per part for its parts' copies and loads,
// and its result's stores and hidden pointer; per return of an aggregate result, its copies and loads; and the entry's
// stores.
static uint64_t tier1_emit_cfg_record_capacity(const IrFunction* function)
{
    uint64_t records = function->parameter_passed != 0 ? 2u * (uint64_t)function->parameter_count + 2 : 0;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        const uint32_t op = function->op[node];
        const IrCall* call = op == IR_CALL || op == IR_CALL_INDIRECT ? &function->calls[function->b[node]] : 0;
        const bool recorded = call != 0 && call->abi != 0;
        records += recorded ? function->abi_calls[call->abi - 1].call.stack_bytes / 4u + 4u * (uint64_t)call->argument_count + 5u * (BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS) + 16u : 0;
        records += op == IR_RET && function->result_size != 0 ? function->result_size / 4u + 32u : 0;
    }
    return records;
}

static Tier1EmitError tier1_emit_cfg_records(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, bool vector, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
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
    const uint64_t capacity = (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE + (uint64_t)allocation->fragment_count * 2 + (uint64_t)cfg->move_count * 2 + BUSTER_TIER1_FRAME_EMIT_RECORDS * ((uint64_t)returns + 1) + (uint64_t)emitted->prepared.call_count * BUSTER_TIER1_EMIT_CALL_RECORDS + (uint64_t)blocks * 2 + input->edge_count + tier1_emit_cfg_record_capacity(function) +
                              (uint64_t)emitted->fixed_count * BUSTER_TIER1_EMIT_FIXED_RECORDS + 2 * BUSTER_TIER1_SELECT_MAX_ARGUMENTS;
    const uint64_t branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES * ((uint64_t)returns + 1) + (uint64_t)blocks * 2 + input->edge_count;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = capacity <= UINT32_MAX && branch_capacity <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1SelectCode* code = &emitted->code;
    Tier1EmitBuffer buffer = { .capacity = (uint32_t)capacity, .branch_capacity = (uint32_t)branch_capacity };
    Tier1SelectRequests requests = { .vector = vector };
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
    // A call's argument past its class's registers that the call recomputes, a symbol's address with a reference (#141).
    for (uint32_t node = 0; node < count; node += 1)
    {
        const bool call = !plan->covered[node] && (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT);
        emitted->reference_capacity += call ? tier1_select_call_stack_bytes(function, &function->calls[function->b[node]]) / BUSTER_ABI_SLOT_BYTES + function->calls[function->b[node]].argument_count : 0;
    }
    bool ok = error == TIER1_EMIT_OK;
    code->records = ok ? ir_allocate(output, capacity, sizeof(X86Instruction), &ok) : 0;
    code->cost_ids = ok ? ir_allocate(output, capacity, 1, &ok) : 0;
    code->node_first = ok ? ir_allocate(output, (uint64_t)count + 1, 4, &ok) : 0;
    code->branches = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    code->targets = ok ? ir_allocate(output, branch_capacity, 4, &ok) : 0;
    emitted->references = ok && emitted->reference_capacity != 0 ? ir_allocate(output, emitted->reference_capacity, sizeof(LinkReference), &ok) : 0;
    emitted->exits = ok ? ir_allocate(output, (uint64_t)returns + 1, 4, &ok) : 0;
    emitted->exit_count = 0;
    transitions.offsets = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 2, 4, &ok) : 0;
    transitions.fragments = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.values = ok ? ir_allocate(scratch, (uint64_t)allocation->fragment_count + 1, 4, &ok) : 0;
    transitions.registers = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    uint32_t* spill_ids = ok ? ir_allocate(scratch, (uint64_t)count + 1, 4, &ok) : 0;
    uint32_t* cursors = ok ? ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok) : 0;
    uint32_t* entry_offsets = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* block_first = ok ? ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok) : 0;
    uint32_t* stub_first = ok ? ir_allocate(scratch, (uint64_t)input->edge_count + 1, 4, &ok) : 0;
    uint32_t* pending_branches = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    uint32_t* pending_goals = ok ? ir_allocate(scratch, branch_capacity + 1, 4, &ok) : 0;
    uint8_t* dropped = ok ? ir_allocate(scratch, (uint64_t)count + 1, 1, &ok) : 0;
    error = ok || error != TIER1_EMIT_OK ? error : TIER1_EMIT_MEMORY;
    uint32_t* entries = 0;
    uint32_t pending = 0;
    if (error == TIER1_EMIT_OK)
    {
        buffer.records = code->records;
        buffer.branches = code->branches;
        buffer.targets = code->targets;
        view.spill_ids = spill_ids;
        memset(transitions.offsets, 0, ((uint64_t)input->position_count + 2) * 4);
        memset(transitions.registers, UINT8_MAX, (uint64_t)count + 1);
        memset(spill_ids, 0xff, ((uint64_t)count + 1) * 4);
        memset(entry_offsets, 0, ((uint64_t)blocks + 1) * 4);
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
            error = !defines || allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER ? error : TIER1_EMIT_ALLOCATION;
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
            uint32_t b = tier1_cfg_block_of(input, start);
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
    if (error == TIER1_EMIT_OK && tier1_frame_emit(&emitted->frame, false, &buffer) != TIER1_FRAME_EMIT_OK)
    {
        error = TIER1_EMIT_FRAME;
    }
    // A narrow parameter into its canonical 32-bit storage, in its register before anything reads it: its caller
    // extends it by its own type (sign or zero), which the canonical form doesn't keep.
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t type = function->type[node];
        const uint32_t narrow = type == IR_I1 || type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
        const Tier1SelectPlace place = function->op[node] == IR_PARAM && narrow != 0 ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_STACK };
        if (place.kind == BUSTER_TIER1_PLACE_GENERAL && emitted->node_values[node] != BUSTER_TIER1_CFG_NONE)
        {
            const uint8_t reg = (uint8_t)tier1_select_parameter_registers[place.index];
            const X86Instruction extend = { .form = narrow == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = reg, .rm = reg };
            error = tier1_emit_append(&buffer, extend) ? error : TIER1_EMIT_MEMORY;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_entry(plan, emitted, scratch, &buffer);
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < blocks; b += 1)
    {
        block_first[b] = b == 0 ? 0 : buffer.count;
        const uint32_t entry = input->block_starts[b];
        // The single incoming edge's moves, where tier1_cfg placed them at this block's start.
        if (input->predecessor_offsets[b + 1] - input->predecessor_offsets[b] == 1)
        {
            const uint32_t p = input->predecessors[input->predecessor_offsets[b]];
            uint32_t e = input->successor_offsets[p];
            while (input->successors[e] != b)
            {
                e += 1;
            }
            if (cfg->edge_placements[e] == TIER1_CFG_EDGE_SUCCESSOR)
            {
                error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e], cfg->edge_move_offsets[e + 1], &buffer);
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
        }
        const uint32_t first = b < added ? 0 : function->block_offsets[b - added];
        const uint32_t end = b < added ? 0 : function->block_offsets[b - added + 1];
        for (uint32_t node = first; error == TIER1_EMIT_OK && node < end; node += 1)
        {
            const uint32_t op = function->op[node];
            const uint32_t use = base + 2 * node;
            const uint32_t v = emitted->node_values[node];
            const bool selected = !plan->covered[node] && op != IR_PHI && !tier1_select_entry_parameter(function, node);
            code->node_first[node] = buffer.count;
            error = tier1_emit_phase(&emitted->prepared, &view, &emitted->frame, &transitions, use, &buffer, emitted);
            // A definition writes its interval's first register; a parameter and a phi are where the entry state put
            // them.
            if (error == TIER1_EMIT_OK && v != BUSTER_TIER1_CFG_NONE && selected)
            {
                const uint32_t interval = tier1_cfg_interval_at(cfg, v, use + 1);
                error = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == use + 1 ? error : TIER1_EMIT_ALLOCATION;
                // Its class's register, by location: a mask's is the opmask class's (#179).
                const uint32_t base_location = 32 * tier1_emit_class(&emitted->prepared, node);
                transitions.registers[node] = error == TIER1_EMIT_OK ? (uint8_t)(base_location + allocation->registers[allocation->fragment_offsets[interval]]) : UINT8_MAX;
            }
            const bool calls = op == IR_CALL || op == IR_CALL_INDIRECT;
            Tier1SelectHoles holes = { 0 };
            // A call's outgoing area in a dynamic frame (tier1_emit_cfg_call_area), released after it returns.
            uint32_t call_area = 0;
            // The arguments' registers, and an indirect call's callee's after them.
            uint32_t argument_registers[BUSTER_TIER1_SELECT_MAX_ARGUMENTS + 1] = { 0 };
            if (error == TIER1_EMIT_OK && selected)
            {
                const Tier1SelectPattern* pattern = &plan->patterns[node];
                bool registered = function->type[node] == IR_VOID || tier1_emit_cfg_holds(emitted, node, transitions.registers[node]);
                for (uint32_t p = 0; p < 3; p += 1)
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
                // A dynamic frame's call takes its outgoing area from rsp first (#141): its stores below are rsp's.
                call_area = calls ? tier1_emit_cfg_call_area(emitted, tier1_select_call_stack_bytes(function, called)) : 0;
                error = error == TIER1_EMIT_OK ? tier1_emit_cfg_adjust(call_area, false, &buffer) : error;
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
                    }
                    // A by-value aggregate (#789): its bytes from its address now, before the moves, on the stack whole into
                    // its slots, else each part into the red zone's next staging slot, which tier1_emit_call loads its
                    // register from after the moves.
                    const IrPassed* record = aggregate ? tier1_select_argument_record(function, called, argument) : 0;
                    Tier1SelectPart parts[2];
                    const bool whole = record != 0 && record->location.kind == BUSTER_ABI_LOCATION_STACK;
                    const uint32_t part_count = record != 0 && !whole ? tier1_select_parts(record->location.registers, record->size, BUSTER_TIER1_SELECT_ARGUMENT_GENERAL, BUSTER_ABI_VECTOR_ARGUMENTS, parts) : 0;
                    if (whole && registered && error == TIER1_EMIT_OK)
                    {
                        error = tier1_emit_cfg_aggregate_copy(emitted, value, use, (int32_t)record->location.stack_offset, 0, record->size, &buffer);
                    }
                    for (uint32_t k = 0; registered && error == TIER1_EMIT_OK && k < part_count; k += 1)
                    {
                        error = staged < BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS ? tier1_emit_cfg_aggregate_copy(emitted, value, use, tier1_emit_staging(staged), parts[k].offset, parts[k].bytes, &buffer) : TIER1_EMIT_ALLOCATION;
                        staged += 1;
                    }
                }
                error = registered ? error : TIER1_EMIT_ALLOCATION;
                if (error == TIER1_EMIT_OK && !calls)
                {
                    holes = tier1_select_holes(plan, transitions.registers, node);
                    // An allocation rounds to the alignment rsp keeps (#141), a realigned frame's past 16 bytes.
                    holes.scale = holes.rule == TIER1_RULE_ALLOCA || holes.rule == TIER1_RULE_ALLOCA_IMMEDIATE ? tier1_emit_cfg_stack_alignment(&emitted->frame) : holes.scale;
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
                // in its register's row before the call and reloaded after, its 8 bytes by movsd. The call's own result
                // can't share a register with one.
                uint32_t vectors_saved = 0;
                for (uint32_t x = 0; x < 15; x += 1)
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
                    for (uint32_t x = 0; error == TIER1_EMIT_OK && x < 15; x += 1)
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
                                .form = pass == 0 ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSD_X_XM64,
                            };
                            error = tier1_emit_append(&buffer, record) ? error : TIER1_EMIT_MEMORY;
                            emitted->call_stores += pass == 0;
                            emitted->call_loads += pass == 1;
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
                        }
                    }
                    if (error == TIER1_EMIT_OK && pass == 0)
                    {
                        // An aggregate result's object (#789): the frame object the call's into operand addresses.
                        const uint32_t into_node = function->c[node];
                        Tier1EmitInto into = { .base = (uint8_t)emitted->frame.base_register };
                        into.valid = into_node < count && function->op[into_node] == IR_STACK_ADDR && tier1_emit_cfg_object(function, emitted, function->a[into_node], &into.displacement);
                        error = tier1_emit_call(plan, node, argument_registers, saved, transitions.registers[node], &into, &buffer, scratch, emitted);
                    }
                }
                error = error == TIER1_EMIT_OK ? tier1_emit_cfg_adjust(call_area, true, &buffer) : error;
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
                        error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[e_first], cfg->edge_move_offsets[e_first + 1], &buffer);
                    }
                    if (error == TIER1_EMIT_OK && s != b + 1)
                    {
                        error = tier1_emit_cfg_jump(&buffer, X86_FORM_JMP_REL8, s, pending_branches, pending_goals, &pending, emitted);
                    }
                }
                else
                {
                    // The comparison's records, the selector's (its last two are the conditional jump and the jump),
                    // with its immediate's request.
                    const Tier1SelectRecords selected_records = tier1_select_expand(&holes);
                    const uint32_t compares = selected_records.count >= 2 ? selected_records.count - 2u : 0;
                    error = !selected_records.overflowed && selected_records.count >= 2 && (!selected_records.requested || selected_records.request < compares) ? error : TIER1_EMIT_INVALID;
                    const uint32_t compare_first = buffer.count;
                    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < compares; r += 1)
                    {
                        error = tier1_emit_append(&buffer, selected_records.records[r]) ? error : TIER1_EMIT_MEMORY;
                    }
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_select_requests_add(&requests, &selected_records, compare_first, buffer.records);
                        error = tier1_emit_cfg_branch(emitted, b, e_first, selected_records.records[compares].form, &buffer, stub_first, pending_branches, pending_goals, &pending);
                    }
                }
            }
            else if (error == TIER1_EMIT_OK && selected && tier1_select_fixed_registers(plan->patterns[node].rule) != 0)
            {
                error = tier1_emit_cfg_fixed(emitted, &transitions, node, use, &holes, &buffer);
            }
            else if (error == TIER1_EMIT_OK && selected && dropped[node] != 0)
            {
                // A recomputed value that nothing reads before its recomputation takes over: no records here.
                emitted->dropped_definitions += 1;
            }
            else if (error == TIER1_EMIT_OK && selected)
            {
                Tier1SelectRecords selected_records = tier1_select_expand(&holes);
                error = selected_records.overflowed ? TIER1_EMIT_INVALID : error;
                // A frame object's address, from the frame's base at the object's offset; an overflow area's from the
                // incoming frame (#789).
                int32_t object_offset = 0;
                if (plan->patterns[node].rule == TIER1_RULE_STACK_ADDRESS)
                {
                    const uint32_t object = (uint32_t)plan->patterns[node].immediate;
                    uint8_t base = (uint8_t)emitted->frame.base_register;
                    uint64_t past = 0;
                    const bool found = tier1_emit_cfg_incoming_object(function, object, &past) ? tier1_emit_cfg_incoming(emitted, past, &base, &object_offset)
                                                                                               : tier1_emit_cfg_object(function, emitted, object, &object_offset);
                    error = selected_records.count == 1 && found ? error : TIER1_EMIT_FRAME;
                    selected_records.records[0].rm = base;
                    selected_records.records[0].displacement = object_offset;
                }
                // A load, a store or an address through a frame object: the frame's base register at the object's
                // offset plus the walked displacement, as the object's lea's. The memory record is the last one.
                if (plan->patterns[node].stack_object != BUSTER_IR_NONE && error == TIER1_EMIT_OK)
                {
                    int32_t total = 0;
                    error = selected_records.count != 0 && tier1_emit_cfg_object(function, emitted, plan->patterns[node].stack_object, &object_offset) &&
                                    !__builtin_add_overflow(object_offset, plan->patterns[node].displacement, &total) ? TIER1_EMIT_OK : TIER1_EMIT_FRAME;
                    if (error == TIER1_EMIT_OK)
                    {
                        selected_records.records[selected_records.count - 1].rm = (uint8_t)emitted->frame.base_register;
                        selected_records.records[selected_records.count - 1].displacement = total;
                    }
                }
                // A symbol's address, its reference the record's: a lea of it, or a load of its GOT slot; a thread-local's
                // is the driver's Tier 0's.
                if (plan->patterns[node].rule == TIER1_RULE_SYMBOL_ADDRESS && error == TIER1_EMIT_OK)
                {
                    const uint32_t symbol = (uint32_t)plan->patterns[node].immediate;
                    const uint8_t access = symbol < emitted->symbol_access_count ? emitted->symbol_access[symbol] : BUSTER_TIER1_ACCESS_DIRECT;
                    error = selected_records.count == 1 && emitted->reference_count < emitted->reference_capacity ? error : TIER1_EMIT_INVALID;
                    error = error != TIER1_EMIT_OK || access == BUSTER_TIER1_ACCESS_DIRECT || access == BUSTER_TIER1_ACCESS_GOT ? error : TIER1_EMIT_UNSUPPORTED;
                    selected_records.records[0].form = access == BUSTER_TIER1_ACCESS_GOT ? X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP : selected_records.records[0].form;
                    if (error == TIER1_EMIT_OK)
                    {
                        emitted->references[emitted->reference_count++] = (LinkReference){
                            .instruction = buffer.count,
                            .field = X86_FIXUP_DISPLACEMENT,
                            .kind = access == BUSTER_TIER1_ACCESS_GOT ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
                            .target = symbol,
                            .addend = 0,
                        };
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
                    }
                    if (error == TIER1_EMIT_OK && returns_here && r + 1 == selected_records.count && tier1_frame_emit(&emitted->frame, true, &buffer) != TIER1_FRAME_EMIT_OK)
                    {
                        error = TIER1_EMIT_FRAME;
                    }
                    else if (error == TIER1_EMIT_OK && !tier1_emit_append(&buffer, selected_records.records[r]))
                    {
                        error = TIER1_EMIT_MEMORY;
                    }
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
            if (error != TIER1_EMIT_OK && error_node != 0)
            {
                *error_node = node;
            }
        }
        // The added entry's single edge: its moves, then it falls through into the IR's entry.
        if (error == TIER1_EMIT_OK && b < added && cfg->edge_placements[0] == TIER1_CFG_EDGE_PREDECESSOR)
        {
            error = tier1_emit_cfg_moves(emitted, cfg->edge_move_offsets[0], cfg->edge_move_offsets[1], &buffer);
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
        error = tier1_emit_cfg_labels(plan, emitted, block_first, stub_first, output, scratch);
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_select_requests_flush(&requests, buffer.records);
    }
    if (error == TIER1_EMIT_OK && x86_validate(buffer.records, buffer.count) != buffer.count)
    {
        error = TIER1_EMIT_INVALID;
    }
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < buffer.count; r += 1)
    {
        if (!x86_target_form_allowed(allowed_forms, buffer.records[r].form))
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
        code->node_first[count] = buffer.count;
        code->error_node = BUSTER_IR_NONE;
        code->arena_bytes = output->length - output_mark;
        emitted->transition_scratch_bytes = scratch->length - scratch_mark;
    }
    return error;
}

// The CFG consumer's whole function: problem, allocation, frame and records, out of line, so that tier1_emit_function's
// straight-line path compiles as it did without it.
// Values live across a call into callee-saved registers, which the prologue saves once, rather than in caller-saved ones
// that every call saves and restores around itself: from the first allocation's intervals, each value whose interval
// covers a call's definition phase, but the call's own result, is allocated again with the callee-saved registers alone
// (and a parameter's incoming one, its definition's), and every other value with the caller-saved ones. If that
// allocation fails, the first stands. Then the callee-saved registers the allocation took, which the frame saves.
static Tier1EmitError tier1_emit_cfg_callee(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, Arena* output, Arena* scratch, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    Tier1CfgInput* input = &emitted->cfg_input;
    const uint32_t callee = emitted->callee_registers;
    const uint32_t added = input->block_count - function->block_count;
    const uint32_t base = 2 * added;
    bool ok = callee != 0;
    uint32_t* calls = ok ? ir_allocate(scratch, (uint64_t)emitted->prepared.call_count + 1, 4, &ok) : 0;
    uint32_t call_count = 0;
    for (uint32_t node = 0; ok && node < function->instruction_count; node += 1)
    {
        if (function->op[node] == IR_CALL || function->op[node] == IR_CALL_INDIRECT)
        {
            calls[call_count++] = base + 2 * node + 1;
        }
    }
    uint32_t* allowed = ok ? ir_allocate(output, (uint64_t)input->value_count + 1, 4, &ok) : 0;
    uint32_t crossing = 0;
    const uint32_t caller = input->registers[0];
    for (uint32_t v = 0; ok && v < input->value_count; v += 1)
    {
        const uint32_t node = emitted->value_nodes[v];
        // A mask keeps its class's registers (#179): no opmask register survives a call.
        const bool general = input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR;
        bool crosses = false;
        for (uint32_t i = emitted->cfg.value_interval_offsets[v]; i < emitted->cfg.value_interval_offsets[v + 1]; i += 1)
        {
            const uint32_t start = emitted->cfg.intervals.starts[i];
            const uint32_t end = emitted->cfg.intervals.ends[i];
            // The first call at or past the interval's start, by halving: the calls are in position order.
            uint32_t low = 0;
            uint32_t high = call_count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = calls[middle] < start ? middle + 1 : low;
                high = calls[middle] < start ? high : middle;
            }
            crosses |= low < call_count && calls[low] < end && calls[low] != base + 2 * node + 1;
        }
        // A parameter keeps its incoming register over its definition's interval, as in the first allocation: free there
        // too, in a callee-saved register from the entry on, it would hold one for the whole entry block, which spilled
        // more than its saves around the calls there cost (on speedtest1, a slower run). A parameter the first
        // allocation already freed (tier1_emit_cfg_function) stays free, the entry's moves bringing it.
        const uint32_t defined = input->definition_registers != 0 ? input->definition_registers[v] : 0;
        allowed[v] = general ? (crosses ? callee : caller) | defined : input->allowed_registers[v];
        crossing += general && crosses;
    }
    Tier1EmitError error = ok || callee == 0 ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
    if (error == TIER1_EMIT_OK && crossing != 0)
    {
        const Tier1CfgInput first_input = *input;
        input->allowed_registers = allowed;
        input->registers[0] = caller | callee;
        Tier1CfgAllocation second;
        const Tier1CfgError allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_cfg_allocate(input, output, scratch, &second) : tier1_cfg_allocate_scalar(input, output, scratch, &second);
        const bool taken = allocated == TIER1_CFG_OK && second.split_count == 0 && (second.temp_slots & ~((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1)) == 0;
        if (taken)
        {
            emitted->cfg = second;
            emitted->crossing_values = crossing;
        }
        else
        {
            *input = first_input;
        }
    }
    // The callee-saved registers the general class's fragments took (a mask's k3 is no rbx).
    emitted->callee_used = 0;
    const Tier1Allocation* allocation = &emitted->cfg.allocation;
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < emitted->cfg.intervals.count; i += 1)
    {
        for (uint32_t f = allocation->fragment_offsets[i]; emitted->cfg.intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR && f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            const uint8_t reg = allocation->registers[f];
            emitted->callee_used |= allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && reg < 16 && ((callee >> reg) & 1) != 0 ? 1u << reg : 0;
        }
    }
    return error;
}

static __attribute__((noinline)) Tier1EmitError tier1_emit_cfg_function(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const uint8_t* allowed_forms, Arena* output, Arena* scratch, Tier1EmitResult* emitted, uint32_t* error_node)
{
    emitted->general = true;
    Tier1EmitError error = tier1_emit_cfg_problem(plan, options, output, scratch, emitted);
    if (error == TIER1_EMIT_OK)
    {
        Tier1CfgError allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_cfg_allocate(&emitted->cfg_input, output, scratch, &emitted->cfg) : tier1_cfg_allocate_scalar(&emitted->cfg_input, output, scratch, &emitted->cfg);
        // A parameter bound to its incoming register over its definition's interval may leave a use there no register:
        // then every parameter is free, the entry's moves bringing each where its allocation has it.
        if (allocated == TIER1_CFG_CONSTRAINT)
        {
            emitted->cfg_input.definition_registers = 0;
            allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_cfg_allocate(&emitted->cfg_input, output, scratch, &emitted->cfg) : tier1_cfg_allocate_scalar(&emitted->cfg_input, output, scratch, &emitted->cfg);
        }
        error = allocated == TIER1_CFG_OK ? TIER1_EMIT_OK : allocated == TIER1_CFG_CAPACITY ? TIER1_EMIT_MEMORY : allocated == TIER1_CFG_CONSTRAINT ? TIER1_EMIT_ALLOCATION : TIER1_EMIT_INVALID;
        // Without fixed ranges there are no splits; each class's temporary slot is a frame row of its own.
        error = error != TIER1_EMIT_OK || (emitted->cfg.split_count == 0 && (emitted->cfg.temp_slots & ~((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1)) == 0) ? error : TIER1_EMIT_ALLOCATION;
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_callee(plan, options, output, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_mask_saves(plan, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        emitted->allocation = emitted->cfg.allocation;
        error = tier1_emit_cfg_frame(plan, options, output, scratch, emitted);
    }
    if (error == TIER1_EMIT_OK)
    {
        error = tier1_emit_cfg_records(plan, allowed_forms, BUSTER_VECTOR_PATH(options->vector), output, scratch, emitted, error_node);
    }
    return error;
}
