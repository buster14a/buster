// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// --tier1's inlining (#1282): a body's calls of the unit's always_inline functions of internal linkage, the x86
// intrinsics' wrappers above all, expanded into its lowered IR before SSA promotion, and the by-value vectors' copies the
// calls leave made values. Include in c_check_tier1.c, after ir_optimize_inline.c and ir_optimize_sroa.c.
//
// The emission builds the callees' table once a unit (check_tier1_inline_table: by symbol, a body index plus one) and
// lowers every part before any part's code (CheckEmitRun.tier1_inline), so that a body's lane reads any callee's typed
// stream. Each lane prepares a callee once an emission, in its own cache arena (CheckTier1Lane.inline_cache): its typed
// stream lowered at its own target, promoted to SSA and optimized (SCCP, dead code and CFG cleanup), which leaves a
// wrapper one block that ends in its return, and a helper with control flow its blocks, which the general splice places
// at the call (#2541, IrInlineOptions.control_flow). A callee that doesn't prepare, or whose target has a feature its
// caller's lacks, stays a call. The unit of the caller and the callees its calls reach
// (transitively, up to BUSTER_CHECK_TIER1_INLINE_CALLEES of them) goes through ir_optimize_inline with no cost threshold,
// as clang inlines an always_inline function whatever its size; a call it keeps, of a callee whose body it doesn't take,
// stays a call to that function's own code. SROA, with the options #1282 gave it, then splits the aggregates' slots
// (the typed stream's objects, which a vector passed or returned by value is) into fields that promotion makes values.
// Any failure here keeps the lowered body as it was: inlining is an optimization, never a refusal.

#define BUSTER_CHECK_TIER1_INLINE_CALLEES 64u
#define BUSTER_CHECK_TIER1_INLINE_UNPREPARED 0u
#define BUSTER_CHECK_TIER1_INLINE_READY 1u
#define BUSTER_CHECK_TIER1_INLINE_REFUSED 2u

// The emission's generations of its inlining tables, which a lane's cache is valid for.
static uint64_t check_tier1_inline_generations = 0;

// The table of `program`'s inline callees (CheckTier1Inline), in `arena`: none (false, *table untouched) in a unit without
// a defined always_inline function of internal linkage, the usual one.
static bool check_tier1_inline_table(const CheckProgram* program, const CheckProgramLowered* lowered, uint32_t symbol_count, Arena* arena, CheckTier1Inline* table)
{
    const CheckInput* input = program->input;
    uint32_t candidates = 0;
    for (uint32_t b = 0; program->object_inline != 0 && b < program->body_count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const uint8_t bits = body.object <= input->token_count ? program->object_inline[body.object] : 0;
        candidates += bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && check_input_linkage(input->declaration_bits, body.declaration) == 1;
    }
    uint32_t* bodies = candidates != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbol_count) : 0;
    uint8_t* ready = bodies != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)program->body_count + 1) : 0;
    if (bodies != 0 && ready != 0)
    {
        memset(bodies, 0, (uint64_t)symbol_count * sizeof(uint32_t));
        memset(ready, 0, (uint64_t)program->body_count + 1);
        for (uint32_t b = 0; b < program->body_count; b += 1)
        {
            const CheckProgramBody body = program->bodies[b];
            const uint8_t bits = body.object <= input->token_count ? program->object_inline[body.object] : 0;
            const SymbolId symbol = body.object <= input->token_count ? program->object_symbols[body.object] : 0;
            if (bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && check_input_linkage(input->declaration_bits, body.declaration) == 1 && symbol != 0 && symbol < symbol_count)
            {
                bodies[symbol] = b + 1;
            }
        }
        *table = (CheckTier1Inline){
            .program = program,
            .lowered = lowered,
            .bodies = bodies,
            .ready = ready,
            .symbol_count = symbol_count,
            .generation = __atomic_add_fetch(&check_tier1_inline_generations, 1, __ATOMIC_RELAXED),
        };
    }
    return bodies != 0 && ready != 0;
}

static void check_tier1_inline_renew(CheckTier1Inline* table)
{
    table->generation = __atomic_add_fetch(&check_tier1_inline_generations, 1, __ATOMIC_RELAXED);
}

// Body `b`'s target (check_program_emit_tier1's).
static const X86Target* check_tier1_inline_target(const CheckProgram* program, uint32_t b)
{
    const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
    return body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? &program->target : &program->targets[body_target - 1];
}

// The lane's cache for `inlining`'s generation: emptied, with a state and a function per body, at its first use.
static bool check_tier1_inline_cache(CheckTier1Lane* lane, const CheckTier1Inline* inlining)
{
    bool valid = lane->inline_generation == inlining->generation && lane->inline_states != 0;
    if (!valid)
    {
        if (lane->inline_cache.base == 0)
        {
            lane->inline_cache = arena_reserve(UINT64_C(1) << 34);
        }
        arena_pop_to(&lane->inline_cache, 0);
        const uint32_t count = inlining->program->body_count;
        lane->inline_functions = lane->inline_cache.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&lane->inline_cache, IrFunction, (uint64_t)count + 1) : 0;
        lane->inline_states = lane->inline_cache.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&lane->inline_cache, uint8_t, (uint64_t)count + 1) : 0;
        valid = lane->inline_functions != 0 && lane->inline_states != 0;
        if (valid)
        {
            memset(lane->inline_states, BUSTER_CHECK_TIER1_INLINE_UNPREPARED, (uint64_t)count + 1);
            lane->inline_generation = inlining->generation;
        }
    }
    return valid;
}

// Callee body `b` prepared for inlining in the lane's cache (the file's comment), or false where it doesn't prepare. Its
// lowering and promotion go in steps[1] and steps[2], free before the caller's own promotion.
static bool check_tier1_inline_prepare(CheckTier1Lane* lane, const CheckTier1Inline* inlining, uint32_t b, bool scalar)
{
    // A callee that calls a function that returns twice (#1847) stays a call, as LLVM's isInlineViable keeps it: its caller
    // would hold the call without Tier0Function.calls_returns_twice.
    uint8_t state = inlining->ready[b] != 0 && !inlining->lowered->functions[b].calls_returns_twice ? lane->inline_states[b] : BUSTER_CHECK_TIER1_INLINE_REFUSED;
    if (state == BUSTER_CHECK_TIER1_INLINE_UNPREPARED)
    {
        const CheckProgram* program = inlining->program;
        const Tier0Function* function = &inlining->lowered->functions[b];
        const uint8_t result_class = check_program_result_class(function, program, b, false);
        const IrType result_type = result_class == UINT8_MAX ? IR_VOID : (IrType)ir_typed_types[result_class];
        const uint64_t features = check_tier1_inline_target(program, b)->features;
        Arena* scratch = &lane->scratch;
        const uint64_t scratch_mark = scratch->length;
        const uint64_t cache_mark = lane->inline_cache.length;
        IrFunction lowered = { 0 };
        IrCheck check = scalar ? ir_lower_typed_target_scalar(function, result_type, inlining->symbol_count, features, &lane->steps[1], scratch, &lowered) :
                                 ir_lower_typed_target(function, result_type, inlining->symbol_count, features, &lane->steps[1], scratch, &lowered);
        IrFunction promoted = { 0 };
        if (check.error == IR_OK)
        {
            check = scalar ? ssa_ir_promote_scalar(&lowered, &lane->steps[2], scratch, &promoted) : ssa_ir_promote(&lowered, &lane->steps[2], scratch, &promoted);
        }
        IrOptimizeResult optimized = { 0 };
        if (check.error == IR_OK)
        {
            IrOptimizeOptions options = ir_optimize_options();
            options.scalar = scalar;
            check = ir_optimize(&promoted, &lane->inline_cache, scratch, options, &optimized);
        }
        state = check.error == IR_OK ? BUSTER_CHECK_TIER1_INLINE_READY : BUSTER_CHECK_TIER1_INLINE_REFUSED;
        lane->inline_functions[b] = check.error == IR_OK ? optimized.function : (IrFunction){ 0 };
        if (check.error != IR_OK)
        {
            arena_pop_to(&lane->inline_cache, cache_mark);
        }
        lane->inline_states[b] = state;
        arena_pop_to(&lane->steps[1], 0);
        arena_pop_to(&lane->steps[2], 0);
        arena_pop_to(scratch, scratch_mark);
    }
    return state == BUSTER_CHECK_TIER1_INLINE_READY;
}

// The inliner's flags of prepared callee `b`: internal, and variadic where its IR is.
static uint8_t check_tier1_inline_flags(const CheckTier1Lane* lane, uint32_t b)
{
    return (uint8_t)(BUSTER_IR_INLINE_INTERNAL | (lane->inline_functions[b].variadic ? BUSTER_IR_INLINE_VARIADIC : 0));
}

// The inline callee bodies of `function`'s direct calls (not row calls) whose symbols `inlining` has, but `caller`
// itself, that prepare and whose targets' features `target` has: appended to `bodies` (`*count` of them so far, up to
// BUSTER_CHECK_TIER1_INLINE_CALLEES) where not there yet. A callee that a call's signature doesn't match (the inliner's
// check, ir_inline_signature, which would refuse the whole unit) goes in `refused` instead (`*refused_count`), and its
// calls stay calls.
static void check_tier1_inline_reach(CheckTier1Lane* lane, const CheckTier1Inline* inlining, const IrFunction* function, uint32_t caller, const X86Target* target, bool scalar,
                                     uint32_t* bodies, uint32_t* count, uint32_t* refused, uint32_t* refused_count)
{
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        const bool direct = function->op[node] == IR_CALL && function->calls[function->b[node]].row == 0 && function->calls[function->b[node]].body == 0 && function->a[node] < inlining->symbol_count;
        const uint32_t b = direct ? inlining->bodies[function->a[node]] : 0;
        bool known = b == 0 || b - 1 == caller;
        bool taken = false;
        for (uint32_t k = 0; !known && k < *count; k += 1)
        {
            known = bodies[k] == b - 1;
            taken = known;
        }
        for (uint32_t k = 0; !known && k < *refused_count; k += 1)
        {
            known = refused[k] == b - 1;
        }
        if (!known && *count < BUSTER_CHECK_TIER1_INLINE_CALLEES && *refused_count < BUSTER_CHECK_TIER1_INLINE_CALLEES)
        {
            const X86Target* own = check_tier1_inline_target(inlining->program, b - 1);
            const bool features = (own->features & ~target->features) == 0 && (own->extensions & ~target->extensions) == 0;
            if (features && check_tier1_inline_prepare(lane, inlining, b - 1, scalar))
            {
                bodies[*count] = b - 1;
                *count += 1;
                taken = true;
            }
        }
        // A full list leaves the callee in, and the inliner then refuses the unit: the body keeps every call.
        if (taken && *refused_count < BUSTER_CHECK_TIER1_INLINE_CALLEES && !ir_inline_signature(function, node, &lane->inline_functions[b - 1], check_tier1_inline_flags(lane, b - 1)))
        {
            refused[*refused_count] = b - 1;
            *refused_count += 1;
        }
    }
}

// Body `caller`'s lowered IR `lowered` with its calls of inline callees expanded (`*inlined` of them) and its aggregates'
// slots split (the file's comment), into *result in steps[4] and steps[5]; or false, and `lowered` stays the body's.
static bool check_tier1_inline(CheckTier1Lane* lane, uint32_t caller, const IrFunction* lowered, const X86Target* target, bool scalar, IrFunction* result, uint32_t* inlined)
{
    const CheckTier1Inline* inlining = lane->inlining;
    const CheckProgram* program = inlining->program;
    *inlined = 0;
    bool changed = false;
    uint32_t bodies[BUSTER_CHECK_TIER1_INLINE_CALLEES];
    uint32_t refused[BUSTER_CHECK_TIER1_INLINE_CALLEES];
    uint32_t count = 0;
    uint32_t refused_count = 0;
    if (check_tier1_inline_cache(lane, inlining))
    {
        check_tier1_inline_reach(lane, inlining, lowered, caller, target, scalar, bodies, &count, refused, &refused_count);
        // The callees' own calls, breadth first: every callee is a prepared body of the cache, which no later preparation
        // moves (the cache only grows).
        for (uint32_t k = 0; k < count; k += 1)
        {
            check_tier1_inline_reach(lane, inlining, &lane->inline_functions[bodies[k]], caller, target, scalar, bodies, &count, refused, &refused_count);
        }
        // The refused callees leave the unit: their calls are of unknown symbols there.
        uint32_t kept = 0;
        for (uint32_t k = 0; k < count; k += 1)
        {
            bool out = false;
            for (uint32_t r = 0; r < refused_count; r += 1)
            {
                out = out || refused[r] == bodies[k];
            }
            bodies[kept] = bodies[k];
            kept += out ? 0 : 1;
        }
        count = kept;
    }
    const SymbolId own = program->bodies[caller].object <= program->input->token_count ? program->object_symbols[program->bodies[caller].object] : 0;
    IrFunction current = *lowered;
    Arena* scratch = &lane->scratch;
    if (count != 0 && own != 0 && own < inlining->symbol_count)
    {
        // The unit: the caller and its callees, by symbol (insertion; a unit has a few).
        IrFunction functions[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint32_t symbols[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint8_t flags[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint32_t members = 0;
        uint32_t position = 0;
        for (uint32_t k = 0; k <= count; k += 1)
        {
            const uint32_t symbol = k < count ? program->object_symbols[program->bodies[bodies[k]].object] : own;
            uint32_t at = members;
            while (at != 0 && symbols[at - 1] > symbol)
            {
                functions[at] = functions[at - 1];
                symbols[at] = symbols[at - 1];
                flags[at] = flags[at - 1];
                at -= 1;
            }
            functions[at] = k < count ? lane->inline_functions[bodies[k]] : *lowered;
            symbols[at] = symbol;
            flags[at] = k < count ? check_tier1_inline_flags(lane, bodies[k]) : 0;
            members += 1;
        }
        for (uint32_t k = 0; k < members; k += 1)
        {
            position = symbols[k] == own ? k : position;
        }
        const IrInlineUnit unit = {
            .functions = functions,
            .symbols = symbols,
            .flags = flags,
            .count = members,
            .symbol_count = inlining->symbol_count,
        };
        // An always_inline callee's size is no reason to keep its call; the growth bounds keep a pathological unit's.
        IrInlineOptions options = ir_inline_options();
        options.maximum_cost = UINT32_MAX - 1;
        options.maximum_function_growth = BUSTER_IR_INLINE_MAX_ITEMS / 4;
        options.scalar = scalar;
        // Whatever its control flow (#2541): its blocks spliced at the call, as clang expands an always_inline function.
        options.control_flow = true;
        const uint64_t evex = BUSTER_X86_FEATURE(AVX512F);
        const uint64_t narrow = evex | BUSTER_X86_FEATURE(AVX512VL);
        options.vector_copies = (target->features & evex) != evex ? 0 : (target->features & narrow) == narrow ? 7 : 4;
        IrInlineResult expanded = { 0 };
        uint32_t failed = BUSTER_IR_NONE;
        const IrCheck check = ir_optimize_inline(&unit, &lane->steps[4], scratch, options, &expanded, &failed);
        for (uint32_t call = check.error == IR_OK ? expanded.call_offsets[position] : 0; check.error == IR_OK && call < expanded.call_offsets[position + 1]; call += 1)
        {
            *inlined += expanded.decisions[call] == IR_INLINE_SELECTED;
        }
        if (check.error == IR_OK && *inlined != 0)
        {
            current = expanded.functions[position];
            changed = true;
        }
    }
    // The aggregates' slots of a body with vectors, or of one whose callees' copies are now its own.
    if (changed || ir_function_has_vectors_path(&current, !scalar))
    {
        IrSroaOptions options = ir_sroa_options();
        options.scalar = scalar;
        options.simd = true;
        options.flagged = true;
        IrSroaResult split = { 0 };
        const IrCheck check = ir_optimize_sroa(&current, &lane->steps[5], scratch, options, &split);
        if (check.error == IR_OK && split.split_slots != 0)
        {
            current = split.function;
            changed = true;
        }
    }
    *result = current;
    return changed;
}
