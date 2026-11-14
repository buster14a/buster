// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// --tier1's inlining (#1282, #2541): a body's calls of the unit's candidates expanded into its lowered IR before SSA
// promotion, and the by-value aggregates' copies the calls leave split. Include in c_check_tier1.c, after
// ir_optimize_inline.c, ir_optimize_sroa.c and check_tier1_passes.
//
// The candidates are the unit's defined functions, but naked ones, those that call a function that returns twice, and
// external ones another unit's definition may replace (check_tier1_inline_marks), which keep their calls: an
// always_inline one (whatever its size or control flow, as clang expands it); with no inline attribute, the small class (a
// summary of at most BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE instructions, each call expanded at an inliner cost of at most
// BUSTER_CHECK_TIER1_INLINE_SMALL_COST: #2541 step 2's sweep); and of those, an internal one with one direct call in the
// unit that nothing else keeps (check_tier1_inline_single), within the single-site class's larger bounds. An external
// candidate (#2541 step 2) keeps its own code whatever its callers expand. The emission builds the table once a unit
// (check_tier1_inline_table: by symbol, a candidate's body index plus one) and lowers every part before any part's code
// (CheckEmitRun.tier1_inline).
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
// Dead functions (#2541 step 2): an internal candidate that nothing roots (CheckTier1Inline.rooted) and no emitted code
// refers to once its callers expanded its calls gets no code (check_tier1_inline_dead, after the lanes). So that it costs
// no selection, allocation or emission either, one the unit stage holds (CheckTier1Inline.held: not rooted, in no cycle,
// its address taken by no code) whose summary its callers should expand makes no code on its lane
// (check_tier1_inline_held); the emission's last pass makes the code of each held one some emitted code still refers to.
//
// Determinism: a summary is a function of its body and its callees' summaries, and the decisions are functions of those,
// so the output doesn't depend on which lane builds what, or on the lanes' count (#153), nor on what is held. Any failure
// here keeps the lowered body as it was: inlining is an optimization, never a refusal.

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
#define BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE 1024u
#define BUSTER_CHECK_TIER1_INLINE_SMALL_COST 256u
// The single-site class's bounds (#2541 step 2): a summary of at most this many instructions, its call expanded at an
// inliner cost of at most this.
#define BUSTER_CHECK_TIER1_INLINE_SINGLE_SIZE 2048u
#define BUSTER_CHECK_TIER1_INLINE_SINGLE_COST 8192u

// A candidate's summary (the file's comment): what Tier 1's passes made of its lowering with its calls expanded, the
// function its callers copy and its own code goes on from, with its verified analysis, and the calls expanded in it.
// The analysis's arena is `frozen`, a copy of its builder's analyses arena as it was once the summary was built:
// ir_verified_current reads the arena's length, and the builder's own goes on growing as it builds more, on its lane,
// while another lane's code reads the summary. Nothing writes the copy once the summary is published.
typedef struct CheckTier1Summary CheckTier1Summary;
struct CheckTier1Summary
{
    CheckTier1Passes passes;
    Arena frozen;
    uint32_t inlined;
};

// The emission's generations of its inlining tables.
static uint64_t check_tier1_inline_generations = 0;

// The objects' marks the table reads (#2541 step 2), a byte each in `marks` (cleared, of the unit's token count plus one),
// from their declarations' attributes: BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE where another unit's definition may
// replace theirs at link time (weak or selectany, or a late weak one, `#pragma weak`'s: CheckProgram.weak_late_keys),
// as clang's IR makes such a definition weak or linkonce, whose calls LLVM's inliner keeps (isInterposable); and
// BUSTER_CHECK_TIER1_INLINE_ROOTED where an attribute keeps a definition's code whatever its callers expand (used, a
// constructor's or destructor's, an interrupt handler's, a multiversioned one's, an alias's, ifunc's or weakref's, a
// section's, retain, patchable_function_entry, naked; and the interposable ones). The attributed declarations are found
// 16 tokens at a time, as the emission's scan of them is (check_input_attributed_next), with its scalar twin.
#define BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE 1u
#define BUSTER_CHECK_TIER1_INLINE_ROOTED 2u
static void check_tier1_inline_marks(const CheckProgram* program, bool scalar, uint8_t* marks)
{
    const CheckInput* input = program->input;
    const uint32_t tokens = input->token_count;
    const CheckSparseWide attributes = input->attributes;
    for (uint32_t first = check_input_attributed_next(attributes, 0, tokens, scalar); first < tokens; first = check_input_attributed_next(attributes, first + 16, tokens, scalar))
    {
        for (uint32_t selected = check_input_attributed_group(attributes, first, tokens, scalar); selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            const CheckInputAttributes* records = input->declarations[token] == token ? check_input_attributes_at(input, token) : 0;
            const uint32_t object = input->objects[token];
            for (uint32_t i = 0; records != 0 && object <= tokens && i < records->count; i += 1)
            {
                const CheckInputAttribute* attribute = &records->records[i];
                const CheckInputAttributeKind kind = attribute->kind;
                const bool interposable = kind == CHECK_ATTRIBUTE_WEAK || kind == CHECK_ATTRIBUTE_SELECTANY;
                const bool rooted = interposable || kind == CHECK_ATTRIBUTE_USED || kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
                                    kind == CHECK_ATTRIBUTE_INTERRUPT || kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES ||
                                    kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || kind == CHECK_ATTRIBUTE_CPU_DISPATCH || kind == CHECK_ATTRIBUTE_ALIAS ||
                                    kind == CHECK_ATTRIBUTE_IFUNC || kind == CHECK_ATTRIBUTE_WEAKREF || kind == CHECK_ATTRIBUTE_SECTION || kind == CHECK_ATTRIBUTE_RETAIN ||
                                    kind == CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY || kind == CHECK_ATTRIBUTE_NAKED;
                marks[object] |= attribute->ignored ? 0 : (interposable ? BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE : 0) | (rooted ? BUSTER_CHECK_TIER1_INLINE_ROOTED : 0);
            }
        }
    }
    for (uint32_t k = 0; k < program->weak_late_count; k += 1)
    {
        const uint64_t object = program->weak_late_keys[k] >> 32;
        marks[object <= tokens ? object : 0] |= BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE | BUSTER_CHECK_TIER1_INLINE_ROOTED;
    }
}

// The table of `program`'s inline callees (CheckTier1Inline), in `arena`: none (false, *table untouched) in a unit without
// a candidate. Tier 0's are the always_inline functions of internal linkage (`bodies`); with `tier1`, Tier 1's are those, the
// small class's and the external definitions' (`candidates`, `classes`), with the unit stage's state. An external
// definition (#2541 step 2) is a candidate as an internal one is, but where another unit's may replace it
// (check_tier1_inline_marks); it keeps its own code whatever its callers expand. `scalar` takes the attributes'
// scan's scalar twin.
static bool check_tier1_inline_table(const CheckProgram* program, const CheckProgramLowered* lowered, uint32_t symbol_count, bool tier1, bool scalar, Arena* arena,
                                     CheckTier1Inline* table)
{
    const CheckInput* input = program->input;
    const uint32_t count = program->body_count;
    uint32_t always = 0;
    uint32_t small = 0;
    uint32_t external = 0;
    for (uint32_t b = 0; b < count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const uint8_t bits = program->object_inline != 0 && body.object <= input->token_count ? program->object_inline[body.object] : 0;
        const uint32_t linkage = check_input_linkage(input->declaration_bits, body.declaration);
        always += bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && linkage == 1;
        small += tier1 && bits == 0 && linkage == 1;
        external += tier1 && (bits == 0 || bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS) && linkage == 2;
    }
    small += external;
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
    // Per body, whether an internal candidate keeps its code whatever its callers expand (CheckTier1Inline.rooted), and
    // whether the emission gave it none (CheckTier1Inline.dead).
    uint8_t* rooted = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* dead = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* sites = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* held = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* released = stage ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1) : 0;
    // The objects' marks (check_tier1_inline_marks), while the table is built (popped after it).
    const uint64_t mark = arena->length;
    uint8_t* marks = stage && rooted != 0 && dead != 0 && sites != 0 && held != 0 && released != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)input->token_count + 1) : 0;
    if (marks != 0)
    {
        memset(marks, 0, (uint64_t)input->token_count + 1);
        check_tier1_inline_marks(program, scalar, marks);
        // What else keeps an object's code: static data's references to it, an alias's of it.
        for (uint32_t k = 0; k < program->initializer_reference_count; k += 1)
        {
            const uint32_t object = program->initializer_references[k];
            marks[object <= input->token_count ? object : 0] |= BUSTER_CHECK_TIER1_INLINE_ROOTED;
        }
        // A section's placement (an attribute's or a pragma's).
        const CheckInputSections* sections = input->sections;
        for (uint32_t k = 0; sections != NULL && k < sections->placement_count; k += 1)
        {
            const uint32_t token = sections->placements[k].token;
            const uint32_t object = token < input->token_count ? input->objects[token] : 0;
            marks[object <= input->token_count ? object : 0] |= BUSTER_CHECK_TIER1_INLINE_ROOTED;
        }
        for (uint32_t k = 0; k < program->alias_count; k += 1)
        {
            const CheckProgramAlias* alias = &program->aliases[k];
            marks[alias->target_object <= input->token_count ? alias->target_object : 0] |= BUSTER_CHECK_TIER1_INLINE_ROOTED;
            marks[alias->aliasee <= input->token_count ? alias->aliasee : 0] |= BUSTER_CHECK_TIER1_INLINE_ROOTED;
            marks[alias->base <= input->token_count ? alias->base : 0] |= BUSTER_CHECK_TIER1_INLINE_ROOTED;
        }
    }
    const bool valid = bodies != 0 && ready != 0 && (!stage || (columns && marks != 0));
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
            memset(rooted, 0, (uint64_t)count + 1);
            memset(dead, 0, (uint64_t)count + 1);
            memset(sites, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(held, 0, (uint64_t)count + 1);
            memset(released, 0, (uint64_t)count + 1);
            *word = 0;
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            const CheckProgramBody body = program->bodies[b];
            const uint8_t bits = program->object_inline != 0 && body.object <= input->token_count ? program->object_inline[body.object] : 0;
            const SymbolId symbol = body.object <= input->token_count ? program->object_symbols[body.object] : 0;
            const uint32_t linkage = check_input_linkage(input->declaration_bits, body.declaration);
            const bool internal = linkage == 1;
            // An external definition no other unit's may replace: a candidate whose own code stays.
            const uint8_t marked = marks != 0 ? marks[body.object <= input->token_count ? body.object : 0] : BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE;
            const bool exported = linkage == 2 && (marked & BUSTER_CHECK_TIER1_INLINE_INTERPOSABLE) == 0;
            const bool named = symbol != 0 && symbol < symbol_count;
            if (bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS && internal && named)
            {
                bodies[symbol] = b + 1;
            }
            // A naked function's code is its templates' and it has no body to copy.
            const bool naked = lowered->asms.naked != 0 && lowered->asms.naked[b].planned;
            const uint8_t class = !(internal || exported) || !named || naked ? BUSTER_CHECK_TIER1_INLINE_CLASS_NONE :
                                  bits == BUSTER_CHECK_PROGRAM_INLINE_ALWAYS ? BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS :
                                  bits == 0 ? BUSTER_CHECK_TIER1_INLINE_CLASS_SMALL : BUSTER_CHECK_TIER1_INLINE_CLASS_NONE;
            if (stage && class != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE)
            {
                candidates[symbol] = b + 1;
                classes[b] = class;
                // An external definition keeps its code, and so does one of several versions (a target attribute's, #180).
                const bool versioned = program->body_targets != 0 && program->body_targets[b] != 0;
                rooted[b] = !internal || versioned || (marked & BUSTER_CHECK_TIER1_INLINE_ROOTED) != 0;
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
            .rooted = rooted,
            .dead = dead,
            .sites = sites,
            .held = held,
            .released = released,
            .scalar = scalar,
            .body_count = count,
        };
    }
    arena_pop_to(arena, mark);
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
        memset(table->sites, 0, ((uint64_t)table->body_count + 1) * sizeof(uint32_t));
        memset(table->held, 0, (uint64_t)table->body_count + 1);
        memset(table->released, 0, (uint64_t)table->body_count + 1);
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

// The candidates whose address a body's code takes (a BUSTER_TYPED_GLOBAL_ADDRESS entry of its typed stream, whose value
// is the symbol), marked in `addressed` (cleared, a byte a body): 64 kinds a vector by a compare, then each match's
// value; the scalar path, its twin, reads every entry.
static void check_tier1_inline_addressed(const CheckTier1Inline* inlining, bool scalar, uint8_t* addressed)
{
    (void)scalar;
    const Tier0Function* functions = inlining->lowered->functions;
    for (uint32_t b = 0; b < inlining->body_count; b += 1)
    {
        const Tier0Function* function = &functions[b];
        const uint32_t count = inlining->ready[b] != 0 ? function->count : 0;
        if (BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint64_t symbol = function->kinds[i] == BUSTER_TYPED_GLOBAL_ADDRESS ? function->values[i] : 0;
                const uint32_t candidate = symbol != 0 && symbol < inlining->symbol_count ? inlining->candidates[symbol] : 0;
                addressed[candidate != 0 ? candidate - 1 : inlining->body_count] = 1;
            }
        }
        else
        {
            for (uint32_t first = 0; first < count; first += 64)
            {
                const uint32_t n = count - first < 64 ? count - first : 64;
                const __mmask64 mask = n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(mask, function->kinds + first);
                for (__mmask64 hits = _mm512_mask_cmpeq_epi8_mask(mask, kinds, _mm512_set1_epi8(BUSTER_TYPED_GLOBAL_ADDRESS)); hits != 0; hits &= hits - 1)
                {
                    const uint64_t symbol = function->values[first + (uint32_t)__builtin_ctzll(hits)];
                    const uint32_t candidate = symbol != 0 && symbol < inlining->symbol_count ? inlining->candidates[symbol] : 0;
                    addressed[candidate != 0 ? candidate - 1 : inlining->body_count] = 1;
                }
            }
        }
    }
}

// The unit stage (the file's comment), once, by the first lane to need it while the others wait: the candidates' call
// graph's components (Tarjan's, iterative), each member of a component of several bodies or of a body that calls itself
// marked cyclic; each candidate's direct calls (CheckTier1Inline.sites); and with holding (#2541 step 2), the candidates
// whose code is held back (CheckTier1Inline.held): internal ones kept by nothing but their calls (not rooted), whose
// address no code takes (check_tier1_inline_addressed) and that are in no cycle, whose expansions should leave them
// dead. Its temporaries are in `scratch`. False where they don't fit, which leaves every candidate cyclic: none is
// expanded, held back, and every body keeps its calls.
static bool check_tier1_inline_stage(Arena* scratch, const CheckTier1Inline* inlining)
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
        // Each candidate's direct calls in the unit (CheckTier1Inline.sites), by every lowered body's.
        for (uint32_t b = 0; b < count; b += 1)
        {
            const Tier0Function* function = &functions[b];
            for (uint32_t c = 0; inlining->ready[b] != 0 && c < function->call_count; c += 1)
            {
                const SymbolId target = function->call_targets[c];
                const uint32_t candidate = target != 0 && target < inlining->symbol_count ? inlining->candidates[target] : 0;
                if (candidate != 0 && inlining->sites[candidate - 1] != UINT32_MAX)
                {
                    inlining->sites[candidate - 1] += 1;
                }
            }
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
        // The held candidates, by the bodies' addresses taken (a byte past the bodies takes the other symbols').
        uint8_t* addressed = valid && inlining->holding ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
        if (addressed != 0)
        {
            memset(addressed, 0, (uint64_t)count + 1);
            check_tier1_inline_addressed(inlining, inlining->scalar, addressed);
            for (uint32_t b = 0; b < count; b += 1)
            {
                const bool held = inlining->classes[b] != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE && inlining->ready[b] != 0 && inlining->rooted[b] == 0 &&
                                  addressed[b] == 0 && inlining->cyclic[b] == 0;
                inlining->held[b] = held ? BUSTER_CHECK_TIER1_INLINE_HELD : 0;
            }
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

// Whether candidate body `b` is of the single-site class (#2541 step 2): a small one, internal and kept by nothing but its
// calls (CheckTier1Inline.rooted), with one direct call in the unit, whose expansion leaves it dead.
static bool check_tier1_inline_single(const CheckTier1Inline* inlining, uint32_t b)
{
    return inlining->classes[b] == BUSTER_CHECK_TIER1_INLINE_CLASS_SMALL && inlining->rooted[b] == 0 && inlining->sites[b] == 1;
}

// Whether candidate body `b`'s ready summary may be copied into a caller at `target`: its target's features are the
// caller's, and a small one is within its class's size.
static bool check_tier1_inline_offered(const CheckTier1Inline* inlining, uint32_t b, const X86Target* target)
{
    const X86Target* own = check_tier1_inline_target(inlining->program, b);
    const bool features = (own->features & ~target->features) == 0 && (own->extensions & ~target->extensions) == 0;
    const bool always = inlining->classes[b] == BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS;
    const bool ready = __atomic_load_n(&inlining->states[b], __ATOMIC_ACQUIRE) == BUSTER_CHECK_TIER1_INLINE_READY;
    const uint32_t size = check_tier1_inline_single(inlining, b) ? BUSTER_CHECK_TIER1_INLINE_SINGLE_SIZE :
                                                                   BUSTER_CHECK_TIER1_INLINE_SMALL_SIZE;
    return features && ready && (always || check_tier1_inline_function(inlining, b)->instruction_count <= size);
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
// The unit is the caller and its direct callees' summaries, each once (every one: #2541 step 2 lifted step 1's bound of 64,
// which a dispatch loop's callees pass), by symbol, each with its class's cost limit: an always_inline one none, as clang
// inlines it whatever its size, a single-site one BUSTER_CHECK_TIER1_INLINE_SINGLE_COST, a small one
// BUSTER_CHECK_TIER1_INLINE_SMALL_COST; the caller alone is rewritten (IrInlineUnit.only). A callee whose signature the
// call doesn't match (ir_inline_signature, which would refuse the whole unit) stays a call. The unit's columns are in
// steps[4], before the expansion.
static bool check_tier1_inline_expand(CheckTier1Lane* lane, const CheckTier1Inline* inlining, uint32_t caller, const IrFunction* lowered, const X86Target* target, bool scalar,
                                      IrFunction* result, uint32_t* inlined, bool* verified)
{
    const CheckProgram* program = inlining->program;
    *inlined = 0;
    *verified = true;
    bool changed = false;
    Arena* scratch = &lane->scratch;
    const uint64_t mark = scratch->length;
    // The callees, each once (a byte a body marks the ones taken, cleared after), as keys of their symbol in the high half
    // and their body in the low, and the caller's own last.
    const uint64_t room = (uint64_t)lowered->call_count + 1;
    uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, room);
    uint64_t* sorting = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, room);
    uint8_t* taken = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)inlining->body_count + 1);
    const bool fits = keys != 0 && sorting != 0 && taken != 0;
    if (taken != 0)
    {
        memset(taken, 0, (uint64_t)inlining->body_count + 1);
    }
    // A callee is taken (1) where it is offered and every call of it matches its signature, which the inliner checks at
    // each call and refuses the whole unit for (ir_inline_signature): a call that doesn't match leaves it out (2), and its
    // calls stay calls.
    uint32_t taken_count = 0;
    for (uint32_t node = 0; fits && node < lowered->instruction_count; node += 1)
    {
        const bool direct = lowered->op[node] == IR_CALL && lowered->calls[lowered->b[node]].row == 0 && lowered->calls[lowered->b[node]].body == 0 && lowered->a[node] < inlining->symbol_count;
        const uint32_t candidate = direct ? inlining->candidates[lowered->a[node]] : 0;
        const uint32_t b = candidate - 1;
        if (candidate != 0 && b != caller && taken[b] != 2)
        {
            const bool first = taken[b] == 0;
            const bool offered = !first || (inlining->cyclic[b] == 0 && check_tier1_inline_offered(inlining, b, target));
            const bool matches = offered && ir_inline_signature(lowered, node, check_tier1_inline_function(inlining, b), check_tier1_inline_flags(inlining, b));
            keys[taken_count] = (uint64_t)program->object_symbols[program->bodies[b].object] << 32 | b;
            taken_count += first && matches ? 1 : 0;
            taken[b] = matches ? 1 : 2;
        }
    }
    // The callees taken, each once.
    uint32_t count = 0;
    for (uint32_t k = 0; k < taken_count; k += 1)
    {
        keys[count] = keys[k];
        count += taken[(uint32_t)keys[k]] == 1 ? 1 : 0;
    }
    const SymbolId own = program->bodies[caller].object <= program->input->token_count ? program->object_symbols[program->bodies[caller].object] : 0;
    IrFunction current = *lowered;
    bool aggregates = false;
    if (count != 0 && own != 0 && own < inlining->symbol_count)
    {
        // The unit: the caller and its callees, by symbol (a radix sort of the keys, as the bindings' are).
        keys[count] = (uint64_t)own << 32 | UINT32_MAX;
        const uint32_t members = count + 1;
        const uint64_t* sorted = c_bindings_sort(keys, sorting, members, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX);
        // The unit's columns precede the inliner's output (steps[4]), whose unused tail its inputs mustn't share, as they
        // mustn't its scratch.
        Arena* columns = &lane->steps[4];
        IrFunction* functions = BUSTER_ARENA_PUSH_ARRAY(columns, IrFunction, members);
        uint32_t* symbols = BUSTER_ARENA_PUSH_ARRAY(columns, uint32_t, members);
        uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(columns, uint8_t, members);
        uint32_t* limits = BUSTER_ARENA_PUSH_ARRAY(columns, uint32_t, members);
        const bool unit_fits = functions != 0 && symbols != 0 && flags != 0 && limits != 0;
        uint32_t position = 0;
        for (uint32_t k = 0; unit_fits && k < members; k += 1)
        {
            const uint32_t b = (uint32_t)sorted[k];
            const bool self = b == UINT32_MAX;
            functions[k] = self ? *lowered : *check_tier1_inline_function(inlining, b);
            symbols[k] = (uint32_t)(sorted[k] >> 32);
            flags[k] = self ? 0 : check_tier1_inline_flags(inlining, b);
            limits[k] = self ? 0 :
                        inlining->classes[b] == BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS ? UINT32_MAX - 1 :
                        check_tier1_inline_single(inlining, b) ? BUSTER_CHECK_TIER1_INLINE_SINGLE_COST :
                                                                 BUSTER_CHECK_TIER1_INLINE_SMALL_COST;
            position = self ? k : position;
        }
        const IrInlineUnit unit = unit_fits ? (IrInlineUnit){
            .functions = functions,
            .symbols = symbols,
            .flags = flags,
            .count = members,
            .symbol_count = inlining->symbol_count,
            .limits = limits,
            .only = position + 1,
        } : (IrInlineUnit){ 0 };
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
        // (A unit that didn't fit is no unit, which the inliner refuses.)
        const IrCheck check = unit_fits ? ir_optimize_inline(&unit, &lane->steps[4], scratch, options, &expanded, &failed) : ir_check(IR_ERROR_MEMORY, BUSTER_IR_NONE, 0);
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
    // The inliner restores the scratch it takes: the callees' keys go.
    arena_pop_to(scratch, mark);
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
    if (built)
    {
        inlining->summaries[b].frozen = *analyses;
        inlining->summaries[b].passes.analysis.arena = &inlining->summaries[b].frozen;
    }
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
    if (inlining->stage != 0 && check_tier1_inline_stage(&lane->scratch, inlining))
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
                           inlining->ready[b] != 0 && check_tier1_inline_stage(&lane->scratch, inlining);
    const bool ready = candidate && check_tier1_inline_summary(lane, inlining, b, scalar, certify);
    *passes = ready ? inlining->summaries[b].passes : (CheckTier1Passes){ 0 };
    *inlined = ready ? inlining->summaries[b].inlined : 0;
    return ready;
}

// The candidates `references` (of `count`) refer to that nothing reached yet, marked in `reached` and pushed on `stack`
// (of `depth`, which it returns): their targets, symbols, or with `names` asm names' indices (asm_symbols, of
// `asm_names`).
static uint32_t check_tier1_inline_reach(const CheckTier1Inline* inlining, const LinkReference* references, uint32_t count, bool names, const SymbolId* asm_symbols,
                                         uint32_t asm_names, uint8_t* reached, uint32_t* stack, uint32_t depth)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t raw = references[i].target;
        const SymbolId target = names ? (raw < asm_names ? asm_symbols[raw] : 0) : raw;
        const uint32_t candidate = target != 0 && target < inlining->symbol_count ? inlining->candidates[target] : 0;
        if (candidate != 0 && reached[candidate - 1] == 0)
        {
            reached[candidate - 1] = 1;
            stack[depth++] = candidate - 1;
        }
    }
    return depth;
}

// The dead functions (#2541 step 2): the internal candidates that no code the emission keeps refers to, once every body's
// code is made (`codes`, CheckEmitRun.codes), marked in CheckTier1Inline.dead; returns their count. A candidate may lose
// its code where it isn't rooted (CheckTier1Inline.rooted) and has no labels (the lowering numbers them across the unit's
// functions, Tier0Function.label_count, and the merge defines them in turn: a held body's zeroed code would say it has
// none); it keeps it where a kept body's code refers to it (a call no caller expanded, a Tier 0 caller's, an address). The
// kept bodies are the others, with the asm helpers (`helpers`), and their references are followed from each body they
// reach (an explicit stack). An asm template's references (a helper's or a naked body's, `naked`) are its names' indices,
// through `asm_symbols` (of `asm_names`). Not under -g (`sources`), whose entries have no form for a function without code
// yet (#2541 step 3), nor with -fpatchable-function-entry; the emission asks in -c's object alone. Temporaries in
// `scratch`; none dead where they don't fit.
static uint32_t check_tier1_inline_dead(CheckTier1Inline* inlining, const EmittedCode* codes, const CheckProgramHelpers* helpers, const CheckProgramNaked* naked,
                                        const SymbolId* asm_symbols, uint32_t asm_names, bool sources, Arena* scratch)
{
    const uint32_t count = inlining->body_count;
    const CheckProgram* program = inlining->program;
    const uint64_t mark = scratch->length;
    uint8_t* reached = inlining->stage != 0 && !sources && program->patchable_count == 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* stack = reached != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t dead = 0;
    if (stack != 0)
    {
        // Every body a root but the candidates that may lose their code.
        uint32_t depth = 0;
        for (uint32_t b = 0; b < count; b += 1)
        {
            const bool assembly = naked != 0 && naked[b].planned;
            // Labels are numbered across the unit's functions by the lowering (Tier0Function.label_count: a jump table's
            // entries, an address taken), which the merge defines in turn: a function with any keeps its code, as a held
            // one's zeroed code would say it has none.
            const bool may = inlining->classes[b] != BUSTER_CHECK_TIER1_INLINE_CLASS_NONE && inlining->rooted[b] == 0 && inlining->lowered->functions[b].label_count == 0 &&
                             codes[b].label_count == 0 && codes[b].asm_label_count == 0 && !assembly;
            reached[b] = may ? 0 : 1;
            if (!may)
            {
                stack[depth++] = b;
            }
        }
        // The helpers' references, then each reached body's.
        for (uint32_t h = 0; h < helpers->count; h += 1)
        {
            const Tier0Code* code = &helpers->items[h].code;
            depth = check_tier1_inline_reach(inlining, code->references, code->reference_count, helpers->items[h].assembly != 0, asm_symbols, asm_names, reached, stack, depth);
        }
        while (depth != 0)
        {
            const uint32_t b = stack[--depth];
            const bool assembly = naked != 0 && naked[b].planned;
            depth = check_tier1_inline_reach(inlining, codes[b].references, codes[b].reference_count, assembly, asm_symbols, asm_names, reached, stack, depth);
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            inlining->dead[b] = reached[b] == 0;
            dead += reached[b] == 0;
        }
    }
    arena_pop_to(scratch, mark);
    return dead;
}

// Whether candidate body `b`'s own code waits for the emission's last pass (#2541 step 2), its summary ready: with holding,
// one the unit stage held (CheckTier1Inline.held) and the last pass hasn't let go on (CheckTier1Inline.released), whose
// summary its callers should expand at each call: offered at its own target (check_tier1_inline_offered), and but for an
// always_inline one, at an inliner cost (ir_inline_shape's, before the copies of a call's aggregates) within its class's
// limit. A summary past it is a call that stays, and its code is made now rather than alone in the last pass.
static bool check_tier1_inline_held(CheckTier1Lane* lane, uint32_t b, const X86Target* target)
{
    const CheckTier1Inline* inlining = lane->inlining;
    bool held = inlining->holding && b < inlining->body_count && inlining->held[b] == BUSTER_CHECK_TIER1_INLINE_HELD && inlining->released[b] == 0 &&
                check_tier1_inline_offered(inlining, b, target);
    if (held && inlining->classes[b] != BUSTER_CHECK_TIER1_INLINE_CLASS_ALWAYS)
    {
        const uint64_t mark = lane->scratch.length;
        IrInlineBody shape = { 0 };
        const uint32_t limit = check_tier1_inline_single(inlining, b) ? BUSTER_CHECK_TIER1_INLINE_SINGLE_COST :
                                                                         BUSTER_CHECK_TIER1_INLINE_SMALL_COST;
        held = ir_inline_shape(check_tier1_inline_function(inlining, b), true, &lane->scratch, &shape) && shape.supported && shape.cost <= limit;
        arena_pop_to(&lane->scratch, mark);
    }
    // It waits (BUSTER_CHECK_TIER1_INLINE_WAITING), which the last pass reads once the lanes are done: this lane alone
    // writes body `b`'s byte.
    if (held)
    {
        inlining->held[b] = BUSTER_CHECK_TIER1_INLINE_WAITING;
    }
    return held;
}
