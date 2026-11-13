// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// --tier1's inlining (#1282, #2541): a body's calls of the unit's candidates expanded into its lowered IR before SSA
// promotion, and the by-value aggregates' copies the calls leave split. Include in c_check_tier1.c, after
// ir_optimize_inline.c, ir_optimize_sroa.c and check_tier1_passes.
//
// The candidates are the unit's defined functions of internal linkage, but naked ones and those that call a function that
// returns twice: an always_inline one (whatever its size or control flow, as clang expands it), and with no inline
// attribute, the small class (#2541 step 1: a summary of at most BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE instructions, each
// call expanded at an inliner cost of at most BUSTER_CHECK_TIER1_INLINE_SMALL_COST, the prototype's c64). The emission
// builds the table once a unit (check_tier1_inline_table: by symbol, a candidate's body index plus one) and lowers every
// part before any part's code (CheckEmitRun.tier1_inline).
//
// Between the halves, the unit stage (check_tier1_inline_stage) runs once, on the first lane to need it: the candidates'
// call graph from their typed streams' direct calls (Tier0Function.call_targets), whose cycles' members (Tarjan's
// components, a self call too) are no candidates. Each other candidate has a summary, built once for the unit by the first
// lane to need it (check_tier1_inline_summary): its typed stream lowered at its own target, its calls of candidates
// expanded from their summaries (built first), and Tier 1's passes from promotion to the verified optimizer
// (check_tier1_passes), whose function and analysis it keeps. A lane claims a summary by its state word (a
// compare-and-swap) and a lane that needs one another lane is building waits for it (lane_wait); summaries depend only on
// their callees', which form no cycle, so no wait waits on itself. A summary lives in its builder's lane state's arenas of
// the emission run (CheckTier1Inline.arenas), until the run ends. It is what each caller copies (check_tier1_inline: the
// caller's direct callees' summaries and the caller, its calls of them decided by each callee's class and target, through
// ir_optimize_inline with the caller alone rewritten), and what the candidate's own code goes on from, at selection
// (check_tier1_inline_own): its lowering, passes and optimization are moved, not repeated.
//
// Determinism: a summary is a function of its body and its callees' summaries, and the decisions are functions of those,
// so the output doesn't depend on which lane builds what, or on the lanes' count (#153). Any failure here keeps the
// lowered body as it was: inlining is an optimization, never a refusal.

#define BUSTER_CHECK_TIER1_INLINE_CALLEES 64u
// A body's summary's state (lane_wait words: bit 0 clear): none yet, being built by a lane, ready, or refused.
#define BUSTER_CHECK_TIER1_INLINE_NONE 0u
#define BUSTER_CHECK_TIER1_INLINE_BUILDING 2u
#define BUSTER_CHECK_TIER1_INLINE_READY 4u
#define BUSTER_CHECK_TIER1_INLINE_REFUSED 6u
// A body's class as a callee (CheckTier1Inline.classes).
#define BUSTER_CHECK_TIER1_INLINE_CLASS_NONE 0u
#define BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS 1u
#define BUSTER_CHECK_TIER1_INLINE_CLASS_SMALL 2u
// The small class's bounds (#2541 step 1, the prototype's c64): a summary of at most this many instructions, each call of
// it expanded at an inliner cost (ir_optimize_inline.c's units) of at most this.
#define BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE 256u
#define BUSTER_CHECK_TIER1_INLINE_SMALL_COST 64u

// A candidate's summary (the file's comment): what Tier 1's passes made of its lowering with its calls expanded, the
// function its callers copy and its own code goes on from, with its verified analysis, and the calls expanded in it.
typedef struct CheckTier1Summary CheckTier1Summary;
struct CheckTier1Summary
{
    CheckTier1Passes passes;
    uint32_t inlined;
};

// The emission's generations of its inlining tables.
static uint64_t check_tier1_inline_generations = 0;
// The table of `program`'s inline callees (CheckTier1Inline), in `arena`: none (false, *table untouched) in a unit without
// a candidate. Tier 0's are the always_inline functions of internal linkage (`bodies`); with `tier1`, Tier 1's are those and
// the small class's (`candidates`, `classes`), with the unit stage's state.
static bool check_tier1_inline_table(const CheckProgram* program, const CheckProgramLowered* lowered, uint32_t symbol_count, bool tier1, Arena* arena, CheckTier1Inline* table)
{
    const CheckInput* input = program->input;
    const uint32_t count = program->body_count;
    uint32_t always = 0;
    uint32_t small = 0;
    for (uint32_t b = 0; b < count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const uint8_t bits = program->object_inline != 0 && body.object <= input->token_count ? program->object_inline[body.object] : 0;
        const bool internal = check_input_linkage(input->declaration_bits, body.declaration) == 1;
        always += bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && internal;
        small += tier1 && bits == 0 && internal;
    }
    uint32_t* bodies = always + small != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbol_count) : 0;
    uint8_t* ready = bodies != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    const bool stage = tier1 && bodies != 0;
    uint32_t* candidates = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbol_count) : 0;
    uint8_t* classes = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* cyclic = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* states = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1) : 0;
    CheckTier1Summary* summaries = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckTier1Summary, (uint64_t)count + 1) : 0;
    Arena* arenas = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, Arena, 2 * BUSTER_CHECK_EMIT_MAX_LANES) : 0;
    uint32_t* word = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 1) : 0;
    const bool columns = candidates != 0 && classes != 0 && cyclic != 0 && states != 0 && summaries != 0 && arenas != 0 && word != 0;
    const bool valid = bodies != 0 && ready != 0 && (!stage || columns);
    if (valid)
    {
        memset(bodies, 0, (uint64_t)symbol_count * sizeof(uint32_t));
        memset(ready, 0, (uint64_t)count + 1);
        if (stage)
        {
            memset(candidates, 0, (uint64_t)symbol_count * sizeof(uint32_t));
            memset(classes, 0, (uint64_t)count + 1);
            memset(cyclic, 0, (uint64_t)count + 1);
            memset(states, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(arenas, 0, 2 * BUSTER_CHECK_EMIT_MAX_LANES * sizeof(Arena));
            *word = 0;
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            const CheckProgramBody body = program->bodies[b];
            const uint8_t bits = program->object_inline != 0 && body.object <= input->token_count ? program->object_inline[body.object] : 0;
            const SymbolId symbol = body.object <= input->token_count ? program->object_symbols[body.object] : 0;
            const bool internal = check_input_linkage(input->declaration_bits, body.declaration) == 1;
            const bool named = symbol != 0 && symbol < symbol_count;
            if (bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && internal && named)
            {
                bodies[symbol] = b + 1;
            }
            // A naked function's code is its templates' and it has no body to copy.
            const bool naked = lowered->asms.naked != 0 && lowered->asms.naked[b].planned;
            const uint8_t class = !internal || !named || naked ? BUSTER_CHECK_TIER1_INLINE_CLASS_NONE :
                                  bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS ? BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS :
                                  bits == 0 ? BUSTER_CHECK_TIER1_INLINE_CLASS_SMALL : BUSTER_CHECK_TIER1_INLINE_CLASS_NONE;
            if (stage && class != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE)
            {
                candidates[symbol] = b + 1;
                classes[b] = class;
            }
        }
        *table = (CheckTier1Inline){
            .program = program,
            .lowered = lowered,
            .bodies = bodies,
            .ready = ready,
            .symbol_count = symbol_count,
            .generation = __atomic_add_fetch(&check_tier1_inline_generations, 1, __ATOMIC_RELAXED),
            .candidates = candidates,
            .classes = classes,
            .cyclic = cyclic,
            .states = states,
            .summaries = summaries,
            .arenas = arenas,
            .stage = word,
            .body_count = count,
        };
    }
    return valid;
}

// The table for the bodies' second lowering, on one lane (the emission's run alone): its unit stage and summaries undone,
// their arenas emptied.
static void check_tier1_inline_renew(CheckTier1Inline* table)
{
    table->generation = __atomic_add_fetch(&check_tier1_inline_generations, 1, __ATOMIC_RELAXED);
    if (table->stage != 0)
    {
        *table->stage = 0;
        memset(table->states, 0, ((uint64_t)table->body_count + 1) * sizeof(uint32_t));
        memset(table->cyclic, 0, (uint64_t)table->body_count + 1);
        for (uint32_t k = 0; k < 2 * BUSTER_CHECK_EMIT_MAX_LANES; k += 1)
        {
            arena_pop_to(&table->arenas[k], 0);
        }
    }
}

// The summaries' arenas, once the emission's lanes are done with them.
static void check_tier1_inline_release(CheckTier1Inline* table)
{
    for (uint32_t k = 0; table->arenas != 0 && k < 2 * BUSTER_CHECK_EMIT_MAX_LANES; k += 1)
    {
        arena_release(&table->arenas[k]);
    }
}

// Body `b`'s target (check_program_emit_tier1's).
static const X86Target* check_tier1_inline_target(const CheckProgram* program, uint32_t b)
{
    const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
    return body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? &program->target : &program->targets[body_target - 1];
}

// The candidate body that direct call `c` of `function` calls, plus one, or 0: a body whose stream was lowered, but
// `self`.
static uint32_t check_tier1_inline_callee(const CheckTier1Inline* inlining, const Tier0Function* function, uint32_t c, uint32_t self)
{
    const SymbolId target = function->call_targets[c];
    const uint32_t candidate = target != 0 && target < inlining->symbol_count ? inlining->candidates[target] : 0;
    return candidate != 0 && candidate - 1 != self && inlining->ready[candidate - 1] != 0 ? candidate : 0;
}

// The unit stage (the file's comment), once, by the first lane to need it while the others wait: the candidates' call
// graph's components (Tarjan's, iterative), each member of a component of several bodies or of a body that calls itself
// marked cyclic. Its temporaries are the lane's scratch's. False where they don't fit, which leaves every candidate
// cyclic: none is expanded, and every body keeps its calls.
static bool check_tier1_inline_stage(CheckTier1Lane* lane, const CheckTier1Inline* inlining)
{
    uint32_t state = __atomic_load_n(inlining->stage, __ATOMIC_ACQUIRE);
    uint32_t expected = BUSTER_CHECK_TIER1_INLINE_NONE;
    const bool claimed = state == BUSTER_CHECK_TIER1_INLINE_NONE &&
                         __atomic_compare_exchange_n(inlining->stage, &expected, BUSTER_CHECK_TIER1_INLINE_BUILDING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    // A lane that lost the claim waits on the word as the exchange saw it.
    state = state == BUSTER_CHECK_TIER1_INLINE_NONE && !claimed ? expected : state;
    if (claimed)
    {
        const uint32_t count = inlining->body_count;
        const Tier0Function* functions = inlining->lowered->functions;
        Arena* scratch = &lane->scratch;
        const uint64_t mark = scratch->length;
        uint32_t* indices = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint32_t* low = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint32_t* dfs = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint32_t* active = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint8_t* stacked = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
        const bool valid = indices != 0 && low != 0 && next != 0 && dfs != 0 && active != 0 && stacked != 0;
        if (valid)
        {
            memset(indices, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(stacked, 0, (uint64_t)count + 1);
        }
        uint32_t clock = 0;
        uint32_t active_count = 0;
        for (uint32_t root = 0; valid && root < count; root += 1)
        {
            if (inlining->classes[root] != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE && inlining->ready[root] != 0 && indices[root] == BUSTER_IR_NONE)
            {
                uint32_t depth = 1;
                dfs[0] = root;
                indices[root] = clock;
                low[root] = clock++;
                next[root] = 0;
                active[active_count++] = root;
                stacked[root] = 1;
                while (depth != 0)
                {
                    const uint32_t body = dfs[depth - 1];
                    const Tier0Function* function = &functions[body];
                    if (next[body] < function->call_count)
                    {
                        const uint32_t c = next[body]++;
                        const SymbolId target = function->call_targets[c];
                        const uint32_t candidate = target != 0 && target < inlining->symbol_count ? inlining->candidates[target] : 0;
                        const uint32_t callee = candidate != 0 && inlining->ready[candidate - 1] != 0 ? candidate - 1 : BUSTER_IR_NONE;
                        if (callee == body)
                        {
                            inlining->cyclic[body] = 1;
                        }
                        else if (callee != BUSTER_IR_NONE && indices[callee] == BUSTER_IR_NONE)
                        {
                            dfs[depth++] = callee;
                            indices[callee] = clock;
                            low[callee] = clock++;
                            next[callee] = 0;
                            active[active_count++] = callee;
                            stacked[callee] = 1;
                        }
                        else if (callee != BUSTER_IR_NONE && stacked[callee] && indices[callee] < low[body])
                        {
                            low[body] = indices[callee];
                        }
                    }
                    else
                    {
                        if (low[body] == indices[body])
                        {
                            // The component's members, the last pushed first: several make a cycle.
                            const uint32_t top = active_count;
                            uint32_t member;
                            do
                            {
                                member = active[--active_count];
                                stacked[member] = 0;
                            } while (member != body);
                            for (uint32_t k = active_count; top - active_count > 1 && k < top; k += 1)
                            {
                                inlining->cyclic[active[k]] = 1;
                            }
                        }
                        depth -= 1;
                        if (depth != 0 && low[body] < low[dfs[depth - 1]])
                        {
                            low[dfs[depth - 1]] = low[body];
                        }
                    }
                }
            }
        }
        if (!valid)
        {
            memset(inlining->cyclic, 1, (uint64_t)count + 1);
        }
        arena_pop_to(scratch, mark);
        lane_publish(inlining->stage, BUSTER_CHECK_TIER1_INLINE_READY);
        state = BUSTER_CHECK_TIER1_INLINE_READY;
    }
    while (state != BUSTER_CHECK_TIER1_INLINE_READY)
    {
        state = lane_wait(inlining->stage, state);
    }
    return true;
}

// Candidate body `b`'s summary's function, once its state is ready.
static const IrFunction* check_tier1_inline_function(const CheckTier1Inline* inlining, uint32_t b)
{
    return &inlining->summaries[b].passes.optimized.function;
}

// The inliner's flags of candidate body `b`'s summary: internal, and variadic where its IR is.
static uint8_t check_tier1_inline_flags(const CheckTier1Inline* inlining, uint32_t b)
{
    return (uint8_t)(BUSTER_IR_INLINE_INTERNAL | (check_tier1_inline_function(inlining, b)->variadic ? BUSTER_IR_INLINE_VARIADIC : 0));
}

// The lane state's arena of the run, of the two this lane builds summaries in (CheckTier1Lane.summaries: `which` 0 for
// their functions, 1 for their analyses), reserved on its first use; null without one.
static Arena* check_tier1_inline_arena(CheckTier1Lane* lane, uint32_t which)
{
    Arena* arena = lane->summaries != 0 ? &lane->summaries[which] : 0;
    if (arena != 0 && arena->base == 0)
    {
        *arena = arena_reserve(UINT64_C(1) << 34);
    }
    return arena != 0 && arena->base != 0 ? arena : 0;
}

// Whether candidate body `b`'s ready summary may be copied into a caller at `target`: its target's features are the
// caller's, and a small one is within its class's size.
static bool check_tier1_inline_offered(const CheckTier1Inline* inlining, uint32_t b, const X86Target* target)
{
    const X86Target* own = check_tier1_inline_target(inlining->program, b);
    const bool features = (own->features & ~target->features) == 0 && (own->extensions & ~target->extensions) == 0;
    const bool always = inlining->classes[b] == BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS;
    const bool ready = __atomic_load_n(&inlining->states[b], __ATOMIC_ACQUIRE) == BUSTER_CHECK_TIER1_INLINE_READY;
    return features && ready && (always || check_tier1_inline_function(inlining, b)->instruction_count <= BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE);
}

// Whether the call at `node` of `function` passes an aggregate by value or gives one back: its expansion copies the
// aggregate into a slot of its own (ir_optimize_inline.c's head and continuation), which SROA then splits.
static bool check_tier1_inline_aggregates(const IrFunction* function, uint32_t node)
{
    const IrCall call = function->calls[function->b[node]];
    bool aggregates = function->c[node] != BUSTER_IR_NONE;
    for (uint32_t p = 0; call.abi != 0 && p < call.argument_count; p += 1)
    {
        aggregates = aggregates || function->passed[function->abi_calls[call.abi - 1].passed + p].size != 0;
    }
    return aggregates;
}

// Body `caller`'s lowered IR `lowered` with its calls of candidates whose summaries are ready expanded (`*inlined` of
// them) and its aggregates' slots split, into *result in steps[4] and steps[5]; or false, and `lowered` stays the body's.
// *verified is whether *result is verified: the lowering, or what SROA verified; an expansion SROA didn't take isn't.
// The unit is the caller and its direct callees' summaries (up to BUSTER_CHECK_TIER1_INLINE_CALLEES of them), each with
// its class's cost limit: an always_inline one none, as clang inlines it whatever its size, a small one
// BUSTER_CHECK_TIER1_INLINE_SMALL_COST; the caller alone is rewritten (IrInlineUnit.only). A callee whose signature the
// call doesn't match (ir_inline_signature, which would refuse the whole unit) stays a call.
static bool check_tier1_inline_expand(CheckTier1Lane* lane, const CheckTier1Inline* inlining, uint32_t caller, const IrFunction* lowered, const X86Target* target, bool scalar,
                                      IrFunction* result, uint32_t* inlined, bool* verified)
{
    const CheckProgram* program = inlining->program;
    *inlined = 0;
    *verified = true;
    bool changed = false;
    uint32_t bodies[BUSTER_CHECK_TIER1_INLINE_CALLEES];
    uint32_t count = 0;
    for (uint32_t node = 0; node < lowered->instruction_count && count < BUSTER_CHECK_TIER1_INLINE_CALLEES; node += 1)
    {
        const bool direct = lowered->op[node] == IR_CALL && lowered->calls[lowered->b[node]].row == 0 && lowered->calls[lowered->b[node]].body == 0 && lowered->a[node] < inlining->symbol_count;
        const uint32_t candidate = direct ? inlining->candidates[lowered->a[node]] : 0;
        bool known = candidate == 0 || candidate - 1 == caller;
        for (uint32_t k = 0; !known && k < count; k += 1)
        {
            known = bodies[k] == candidate - 1;
        }
        const uint32_t b = candidate - 1;
        if (!known && inlining->cyclic[b] == 0 && check_tier1_inline_offered(inlining, b, target) &&
            ir_inline_signature(lowered, node, check_tier1_inline_function(inlining, b), check_tier1_inline_flags(inlining, b)))
        {
            bodies[count] = b;
            count += 1;
        }
    }
    const SymbolId own = program->bodies[caller].object <= program->input->token_count ? program->object_symbols[program->bodies[caller].object] : 0;
    IrFunction current = *lowered;
    Arena* scratch = &lane->scratch;
    bool aggregates = false;
    if (count != 0 && own != 0 && own < inlining->symbol_count)
    {
        // The unit: the caller and its callees, by symbol (insertion; a unit has a few).
        IrFunction functions[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint32_t symbols[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint8_t flags[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
        uint32_t limits[BUSTER_CHECK_TIER1_INLINE_CALLEES + 1];
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
                limits[at] = limits[at - 1];
                at -= 1;
            }
            functions[at] = k < count ? *check_tier1_inline_function(inlining, bodies[k]) : *lowered;
            symbols[at] = symbol;
            flags[at] = k < count ? check_tier1_inline_flags(inlining, bodies[k]) : 0;
            limits[at] = k == count ? 0 : inlining->classes[bodies[k]] == BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS ? UINT32_MAX - 1 : BUSTER_CHECK_TIER1_INLINE_SMALL_COST;
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
            .limits = limits,
            .only = position + 1,
        };
        // The growth bounds keep a pathological unit's; an always_inline callee's size is no reason to keep its call.
        IrInlineOptions options = ir_inline_options();
        options.maximum_function_growth = BUSTER_IR_INLINE_MAX_ITEMS / 4;
        options.scalar = scalar;
        // Whatever its control flow (#2541): its blocks spliced at the call, as clang expands an always_inline function.
        options.control_flow = true;
        // The lowering and the summaries are verified, and the expansion is by SROA below where it runs, else by
        // promotion (check_tier1_passes).
        options.trusted = true;
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
            *verified = false;
            for (uint32_t call = expanded.call_offsets[position]; call < expanded.call_offsets[position + 1]; call += 1)
            {
                aggregates = aggregates || (expanded.decisions[call] == IR_INLINE_SELECTED && check_tier1_inline_aggregates(lowered, expanded.call_nodes[call]));
            }
        }
    }
    // The aggregates' slots of a body with vectors, as without inlining, or of an expansion that copied an aggregate argument
    // or result into a slot of its own, which SROA splits. Its other aggregates are as they would be without inlining:
    // splitting them is no expansion's work (on sqlite3.c, 0.05% of the code for a quarter of the expansion's time). Where
    // SROA fails the body keeps its calls. An expansion SROA doesn't take is verified by promotion.
    if (aggregates || ir_function_has_vectors_path(&current, !scalar))
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
        else if (check.error != IR_OK && *inlined != 0)
        {
            // The expansion didn't verify, or SROA couldn't take it: the body keeps its calls.
            current = *lowered;
            changed = false;
            *inlined = 0;
        }
        // SROA verifies its input as it prepares and its output as it ends.
        *verified = check.error == IR_OK || !changed;
    }
    *result = current;
    return changed;
}

// Candidate body `b`'s summary, built by this lane: its stream lowered at its own target in steps[1], its calls expanded
// from its callees' summaries (check_tier1_inline_expand) in steps[4] and [5], and Tier 1's passes (check_tier1_passes,
// with promotion in steps[2]), whose function and analysis go to this lane's two arenas of the run
// (check_tier1_inline_arena); the steps are free after. A body that calls a function that returns twice (#1847) is
// refused, as LLVM's isInlineViable keeps it: its callers would hold the call without
// Tier0Function.calls_returns_twice, and its own code is Tier 0's.
static void check_tier1_inline_build(CheckTier1Lane* lane, const CheckTier1Inline* inlining, uint32_t b, bool scalar, bool certify)
{
    const CheckProgram* program = inlining->program;
    const Tier0Function* function = &inlining->lowered->functions[b];
    const uint8_t result_class = check_program_result_class(function, program, b, false);
    const IrType result_type = result_class == UINT8_MAX ? IR_VOID : (IrType)ir_typed_types[result_class];
    const X86Target* target = check_tier1_inline_target(program, b);
    Arena* scratch = &lane->scratch;
    Arena* output = check_tier1_inline_arena(lane, 0);
    Arena* analyses = check_tier1_inline_arena(lane, 1);
    const uint64_t scratch_mark = scratch->length;
    IrFunction lowered = { 0 };
    IrCheck check = ir_check(output != 0 && analyses != 0 && !function->calls_returns_twice ? IR_OK : IR_ERROR_UNSUPPORTED, BUSTER_IR_NONE, 0);
    if (check.error == IR_OK)
    {
        check = scalar ? ir_lower_typed_target_scalar(function, result_type, inlining->symbol_count, target->features, &lane->steps[1], scratch, &lowered) :
                         ir_lower_typed_target(function, result_type, inlining->symbol_count, target->features, &lane->steps[1], scratch, &lowered);
    }
    IrFunction expanded = lowered;
    uint32_t inlined = 0;
    bool verified = true;
    const bool changed = check.error == IR_OK && check_tier1_inline_expand(lane, inlining, b, &lowered, target, scalar, &expanded, &inlined, &verified);
    CheckTier1Summary summary = { 0 };
    bool built = false;
    if (check.error == IR_OK)
    {
        const uint64_t output_mark = output->length;
        const uint64_t analyses_mark = analyses->length;
        // The lowering is verified as it is made, and an expansion SROA took by SROA.
        const CheckTier1PassesInput input = {
            .function = &expanded,
            .verified = verified,
            .unexpanded = changed && inlined != 0 ? &lowered : NULL,
        };
        IrVerifiedRevision revision = { 0 };
        const CheckTier1Outcome outcome = check_tier1_passes(lane, &input, target, scalar, certify, NULL, &lane->steps[2], output, analyses, &revision, &summary.passes);
        built = outcome.stage == CHECK_TIER1_COMPILED;
        summary.inlined = summary.passes.unexpanded ? 0 : inlined;
        arena_pop_to(output, built ? output->length : output_mark);
        arena_pop_to(analyses, built ? analyses->length : analyses_mark);
    }
    inlining->summaries[b] = built ? summary : (CheckTier1Summary){ 0 };
    arena_pop_to(&lane->steps[1], 0);
    arena_pop_to(&lane->steps[2], 0);
    arena_pop_to(&lane->steps[4], 0);
    arena_pop_to(&lane->steps[5], 0);
    arena_pop_to(&lane->steps[6], 0);
    arena_pop_to(scratch, scratch_mark);
    lane_publish(&inlining->states[b], built ? BUSTER_CHECK_TIER1_INLINE_READY : BUSTER_CHECK_TIER1_INLINE_REFUSED);
}

// Candidate body `b`'s summary, ready or refused once this returns: built by this lane where no lane has claimed it,
// after each of its acyclic candidate callees' (an explicit stack: a body is claimed, then its callees are, then it is
// built), or waited for where another lane is building it. Returns whether it is ready.
static bool check_tier1_inline_summary(CheckTier1Lane* lane, const CheckTier1Inline* inlining, uint32_t b, bool scalar, bool certify)
{
    const uint32_t count = inlining->body_count;
    bool ready = inlining->cyclic[b] == 0 && inlining->classes[b] != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE && inlining->ready[b] != 0;
    uint32_t state = ready ? __atomic_load_n(&inlining->states[b], __ATOMIC_ACQUIRE) : BUSTER_CHECK_TIER1_INLINE_REFUSED;
    if (ready && state != BUSTER_CHECK_TIER1_INLINE_READY && state != BUSTER_CHECK_TIER1_INLINE_REFUSED)
    {
        Arena* scratch = &lane->scratch;
        const uint64_t mark = scratch->length;
        uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        uint8_t* claimed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
        uint32_t depth = stack != 0 && claimed != 0 ? 1 : 0;
        if (depth != 0)
        {
            stack[0] = b;
            claimed[0] = 0;
        }
        while (depth != 0)
        {
            const uint32_t top = stack[depth - 1];
            if (claimed[depth - 1] == 0)
            {
                uint32_t current = __atomic_load_n(&inlining->states[top], __ATOMIC_ACQUIRE);
                uint32_t expected = BUSTER_CHECK_TIER1_INLINE_NONE;
                if (current == BUSTER_CHECK_TIER1_INLINE_READY || current == BUSTER_CHECK_TIER1_INLINE_REFUSED)
                {
                    depth -= 1;
                }
                else if (current == BUSTER_CHECK_TIER1_INLINE_NONE &&
                         __atomic_compare_exchange_n(&inlining->states[top], &expected, BUSTER_CHECK_TIER1_INLINE_BUILDING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                {
                    claimed[depth - 1] = 1;
                }
                else if (current == BUSTER_CHECK_TIER1_INLINE_BUILDING)
                {
                    lane_wait(&inlining->states[top], BUSTER_CHECK_TIER1_INLINE_BUILDING);
                }
            }
            else
            {
                // A claimed body's first callee whose summary isn't final yet goes on the stack; with none, the body's
                // summary is built.
                const Tier0Function* function = &inlining->lowered->functions[top];
                uint32_t pending = BUSTER_IR_NONE;
                for (uint32_t c = 0; pending == BUSTER_IR_NONE && c < function->call_count; c += 1)
                {
                    const uint32_t candidate = check_tier1_inline_callee(inlining, function, c, top);
                    const uint32_t callee = candidate != 0 && inlining->cyclic[candidate - 1] == 0 ? candidate - 1 : BUSTER_IR_NONE;
                    const uint32_t callee_state = callee != BUSTER_IR_NONE ? __atomic_load_n(&inlining->states[callee], __ATOMIC_ACQUIRE) : BUSTER_CHECK_TIER1_INLINE_READY;
                    pending = callee_state == BUSTER_CHECK_TIER1_INLINE_READY || callee_state == BUSTER_CHECK_TIER1_INLINE_REFUSED ? BUSTER_IR_NONE : callee;
                }
                if (pending != BUSTER_IR_NONE && depth < count)
                {
                    stack[depth] = pending;
                    claimed[depth] = 0;
                    depth += 1;
                }
                else
                {
                    check_tier1_inline_build(lane, inlining, top, scalar, certify);
                    depth -= 1;
                }
            }
        }
        arena_pop_to(scratch, mark);
        state = stack != 0 && claimed != 0 ? __atomic_load_n(&inlining->states[b], __ATOMIC_ACQUIRE) : BUSTER_CHECK_TIER1_INLINE_REFUSED;
    }
    return ready && state == BUSTER_CHECK_TIER1_INLINE_READY;
}

// Whether `function`'s lowered IR `lowered` (body `caller`'s) calls candidates: their summaries made ready first (each
// built by its first lane), then its calls expanded from them (check_tier1_inline_expand), into *result in
// steps[4] and steps[5], verified where *verified; or false, and `lowered` stays the body's. The unit stage runs first,
// once.
static bool check_tier1_inline(CheckTier1Lane* lane, uint32_t caller, const IrFunction* lowered, const X86Target* target, bool scalar, bool certify, IrFunction* result,
                               uint32_t* inlined, bool* verified)
{
    const CheckTier1Inline* inlining = lane->inlining;
    const Tier0Function* function = &inlining->lowered->functions[caller];
    *inlined = 0;
    *verified = true;
    *result = *lowered;
    bool changed = false;
    if (inlining->stage != 0 && check_tier1_inline_stage(lane, inlining))
    {
        for (uint32_t c = 0; c < function->call_count; c += 1)
        {
            const uint32_t candidate = check_tier1_inline_callee(inlining, function, c, caller);
            if (candidate != 0)
            {
                check_tier1_inline_summary(lane, inlining, candidate - 1, scalar, certify);
            }
        }
        changed = check_tier1_inline_expand(lane, inlining, caller, lowered, target, scalar, result, inlined, verified);
    }
    return changed;
}

// Whether body `b`'s own code goes on from its summary (the file's comment): a candidate whose summary is ready, its
// passes' result in *passes, which nothing writes, and the calls its summary expanded in *inlined.
static bool check_tier1_inline_own(CheckTier1Lane* lane, uint32_t b, bool scalar, bool certify, CheckTier1Passes* passes, uint32_t* inlined)
{
    const CheckTier1Inline* inlining = lane->inlining;
    const bool candidate = inlining != NULL && inlining->stage != 0 && b < inlining->body_count && inlining->classes[b] != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE &&
                           inlining->ready[b] != 0 && check_tier1_inline_stage(lane, inlining);
    const bool ready = candidate && check_tier1_inline_summary(lane, inlining, b, scalar, certify);
    *passes = ready ? inlining->summaries[b].passes : (CheckTier1Passes){ 0 };
    *inlined = ready ? inlining->summaries[b].inlined : 0;
    return ready;
}
