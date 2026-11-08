// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 0's inlining in the emission (#1398): each body Tier 0 compiles gets its calls of the unit's always_inline
// functions of internal linkage spliced in (tier0_inline.c), the callees #1282's Tier 1 inliner takes
// (check_tier1_inline_table, whose table the emission builds once a unit for either tier). docs/tier0-inlining.md has the
// design. Include in c_check_program.c, after c_check_program_emit.c.
//
// The emission lowers every part before any part's code where a unit has a callee (CheckEmitRun.inline_table), so that
// a body's lane reads any callee's typed stream. Each lane expands a callee once an emission, in its own cache
// (CheckTier0InlineLane): the callee's stream with its own sites already spliced, so that one splice at a caller takes
// everything a chain of wrappers reaches (_mm256_mask_add_epi32 calls _mm256_add_epi32). The expansion walks the callees
// with an explicit stack: a callee whose sites' callees are all settled is spliced and ready; a site whose callee is on
// the stack (a cycle) or past BUSTER_CHECK_TIER0_INLINE_DEPTH stays a call; a callee that tier0_inline_callee_valid
// refuses, or whose target has a feature its caller's lacks, keeps its calls. An expansion depends only on the callee and
// the bodies it reaches, so every lane's is the same bytes. Any failure keeps the body's own stream: inlining is an
// optimization, never a refusal.

// Defined in c_check_tier1_inline.c: body `b`'s target.
static const X86Target* check_tier1_inline_target(const CheckProgram* program, uint32_t b);

#define BUSTER_CHECK_TIER0_INLINE_DEPTH 16u
#define BUSTER_CHECK_TIER0_INLINE_UNPREPARED 0u
#define BUSTER_CHECK_TIER0_INLINE_PROGRESS 1u
#define BUSTER_CHECK_TIER0_INLINE_READY 2u
#define BUSTER_CHECK_TIER0_INLINE_REFUSED 3u

// The vector transport of a function of `target` (the lowering's Tier0Function.vector_bytes).
static uint8_t check_tier0_inline_transport(const X86Target* target)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    return (target->features & evex) == evex ? 0 : (target->features & BUSTER_X86_FEATURE(AVX)) != 0 ? 32 : 16;
}

// The lane's cache for `table`'s generation: emptied, with a state and a stream per body, at its first use.
static bool check_tier0_inline_cache(CheckTier0InlineLane* lane, const CheckTier1Inline* table)
{
    bool valid = lane->generation == table->generation && lane->states != 0;
    if (!valid)
    {
        if (lane->cache.base == 0)
        {
            lane->cache = arena_reserve(UINT64_C(1) << 34);
        }
        arena_pop_to(&lane->cache, 0);
        const uint32_t count = table->program->body_count;
        lane->functions = lane->cache.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, Tier0Function, (uint64_t)count + 1) : 0;
        lane->states = lane->cache.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint8_t, (uint64_t)count + 1) : 0;
        valid = lane->functions != 0 && lane->states != 0;
        if (valid)
        {
            memset(lane->states, BUSTER_CHECK_TIER0_INLINE_UNPREPARED, (uint64_t)count + 1);
            lane->generation = table->generation;
        }
    }
    return valid;
}

// The callee body of row `c` of body `b`'s stream `function`, plus one: a direct call of an inline callee other than `b`,
// lowered, not naked, whose target's features `b`'s target has; else 0.
static uint32_t check_tier0_inline_row(const CheckEmitRun* run, const CheckTier1Inline* table, const Tier0Function* function, uint32_t b, uint32_t c)
{
    const SymbolId symbol = function->call_targets[c];
    const uint32_t callee = symbol != 0 && symbol < table->symbol_count ? table->bodies[symbol] : 0;
    const bool lowered = callee != 0 && callee - 1 != b && table->ready[callee - 1] != 0 &&
                         (run->lowered->asms.naked == 0 || !run->lowered->asms.naked[callee - 1].planned);
    bool fits = false;
    if (lowered)
    {
        const X86Target* own = check_tier1_inline_target(table->program, callee - 1);
        const X86Target* target = check_tier1_inline_target(table->program, b);
        fits = (own->features & ~target->features) == 0 && (own->extensions & ~target->extensions) == 0;
    }
    return lowered && fits ? callee : 0;
}

// Body `b`'s stream (`function`) spliced with the ready callees of its rows (the lane's cache), in `arena`, into
// *result: false where it has none, or the splice refuses it.
static bool check_tier0_inline_splice(const CheckEmitRun* run, CheckTier0InlineLane* lane, const CheckTier1Inline* table, uint32_t b, const Tier0Function* function,
                                      Arena* arena, Tier0Inlined* result)
{
    const uint64_t mark = arena->length;
    const uint32_t rows = function->call_count;
    uint32_t* row_callees = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)rows + 1);
    const Tier0Function** callees = BUSTER_ARENA_PUSH_ARRAY(arena, const Tier0Function*, (uint64_t)rows + 1);
    uint32_t* bodies = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)rows + 1);
    bool valid = row_callees != 0 && callees != 0 && bodies != 0;
    uint32_t count = 0;
    for (uint32_t c = 0; valid && c < rows; c += 1)
    {
        const uint32_t callee = check_tier0_inline_row(run, table, function, b, c);
        const bool ready = callee != 0 && lane->states[callee - 1] == BUSTER_CHECK_TIER0_INLINE_READY;
        uint32_t index = 0;
        while (ready && index < count && bodies[index] != callee - 1)
        {
            index += 1;
        }
        if (ready && index == count)
        {
            bodies[count] = callee - 1;
            callees[count] = &lane->functions[callee - 1];
            count += 1;
        }
        row_callees[c] = ready ? index + 1 : 0;
    }
    if (valid && count != 0)
    {
        const Tier0InlineInput input = {
            .caller = function,
            .row_callees = row_callees,
            .callees = callees,
            .callee_count = count,
            .vector_bytes = check_tier0_inline_transport(check_tier1_inline_target(table->program, b)),
        };
        valid = run->scalar ? tier0_inline_scalar(&input, arena, result) : tier0_inline(&input, arena, result);
    }
    else
    {
        valid = false;
    }
    if (!valid)
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

// Settles the callees that body `b`'s rows reach in the lane's cache (the file's comment): each ready, with its expanded
// stream, or refused. An explicit stack of bodies, each with the next row it looks at.
static void check_tier0_inline_prepare(const CheckEmitRun* run, CheckTier0InlineLane* lane, const CheckTier1Inline* table, uint32_t b)
{
    uint32_t stack[BUSTER_CHECK_TIER0_INLINE_DEPTH + 1];
    uint32_t cursors[BUSTER_CHECK_TIER0_INLINE_DEPTH + 1];
    uint32_t depth = 1;
    stack[0] = b;
    cursors[0] = 0;
    while (depth != 0)
    {
        const uint32_t top = stack[depth - 1];
        const Tier0Function* function = &run->lowered->functions[top];
        // The next row whose callee is unsettled, which goes on the stack where there is room.
        uint32_t next = 0;
        while (next == 0 && cursors[depth - 1] < function->call_count)
        {
            const uint32_t callee = check_tier0_inline_row(run, table, function, top, cursors[depth - 1]);
            next = callee != 0 && lane->states[callee - 1] == BUSTER_CHECK_TIER0_INLINE_UNPREPARED && depth <= BUSTER_CHECK_TIER0_INLINE_DEPTH ? callee : 0;
            cursors[depth - 1] += 1;
        }
        if (next != 0)
        {
            uint32_t body = 0;
            const bool valid = tier0_inline_callee_valid(&run->lowered->functions[next - 1], &body);
            lane->states[next - 1] = valid ? BUSTER_CHECK_TIER0_INLINE_PROGRESS : BUSTER_CHECK_TIER0_INLINE_REFUSED;
            if (valid)
            {
                stack[depth] = next - 1;
                cursors[depth] = 0;
                depth += 1;
            }
        }
        else
        {
            // Every callee of the top's rows is settled (or stays a call): the top's expansion, but for `b` itself, which
            // its own code splices.
            if (depth > 1)
            {
                Tier0Inlined expanded;
                const bool spliced = check_tier0_inline_splice(run, lane, table, top, function, &lane->cache, &expanded);
                lane->functions[top] = spliced ? expanded.function : *function;
                lane->states[top] = BUSTER_CHECK_TIER0_INLINE_READY;
            }
            depth -= 1;
        }
    }
}

static bool check_tier0_inline_body(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b, Arena* arena, Tier0Inlined* result)
{
    const CheckTier1Inline* table = run->tier0_inline;
    CheckTier0InlineLane* cache = lane->tier0_inline;
    const Tier0Function* function = &run->lowered->functions[b];
    bool valid = table != NULL && cache != NULL && function->call_count != 0 && check_tier0_inline_cache(cache, table);
    // Only a body with a row of a callee prepares anything.
    bool any = false;
    for (uint32_t c = 0; valid && !any && c < function->call_count; c += 1)
    {
        any = check_tier0_inline_row(run, table, function, b, c) != 0;
    }
    if (valid && any)
    {
        check_tier0_inline_prepare(run, cache, table, b);
        valid = check_tier0_inline_splice(run, cache, table, b, function, arena, result);
    }
    return valid && any;
}
