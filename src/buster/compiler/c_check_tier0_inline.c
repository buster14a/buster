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
// everything a chain of wrappers reaches (_mm256_mask_add_epi32 calls _mm256_add_epi32).
//
// Which callees expand, and into what, is a property of the unit's graph of callees alone, never of the order a lane
// meets them in: the lanes take bodies in an order their timing decides, and every lane's object must be one lane's
// (#1398's review: a depth bound counted from whichever root a lane met first cached a cut expansion that later bodies
// took). The graph's nodes are the callees tier0_inline_callee_valid accepts, and its edges their rows' calls of other
// such callees whose targets theirs include (check_tier0_inline_row). A lane finds the graph's strongly connected
// components, as far as a body's calls reach, by an iterative Tarjan search (check_tier0_inline_prepare): a callee in a
// cycle is never inlined, and its own calls of the cycle stay calls, whichever member a lane met first. Every other
// callee is settled after the callees it calls (the search's postorder): its height is one more than the highest of
// those that expand, and one past BUSTER_CHECK_TIER0_INLINE_DEPTH isn't inlined either (its own calls are); else it is
// spliced with those callees and ready. A callee the splice refuses is ready as its own stream, its calls kept. Any
// failure keeps the body's own stream: inlining is an optimization, never a refusal.

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
        const bool reserved = lane->cache.base != 0;
        lane->functions = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, Tier0Function, (uint64_t)count + 1) : 0;
        lane->states = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint8_t, (uint64_t)count + 1) : 0;
        lane->indices = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        lane->links = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        lane->heights = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        lane->components = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        lane->path = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        lane->cursors = reserved ? BUSTER_ARENA_PUSH_ARRAY(&lane->cache, uint32_t, (uint64_t)count + 1) : 0;
        valid = lane->functions != 0 && lane->states != 0 && lane->indices != 0 && lane->links != 0 && lane->heights != 0 && lane->components != 0 &&
                lane->path != 0 && lane->cursors != 0;
        if (valid)
        {
            memset(lane->states, BUSTER_CHECK_TIER0_INLINE_UNPREPARED, (uint64_t)count + 1);
            lane->visits = 0;
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

// Callee `v`, met by the search: refused at once where tier0_inline_callee_valid refuses it (it is no node of the graph),
// else visited: its index and low link, on the component stack and the search's stack, in progress.
static void check_tier0_inline_visit(const CheckEmitRun* run, CheckTier0InlineLane* lane, uint32_t v, uint32_t* components, uint32_t* depth)
{
    uint32_t body = 0;
    if (tier0_inline_callee_valid(&run->lowered->functions[v], &body))
    {
        lane->visits += 1;
        lane->indices[v] = lane->visits;
        lane->links[v] = lane->visits;
        lane->states[v] = BUSTER_CHECK_TIER0_INLINE_PROGRESS;
        lane->components[*components] = v;
        *components += 1;
        lane->path[*depth] = v;
        lane->cursors[*depth] = 0;
        *depth += 1;
    }
    else
    {
        lane->states[v] = BUSTER_CHECK_TIER0_INLINE_REFUSED;
    }
}

// Callee `v`, alone in its component and every callee it calls settled: its height (one more than the highest of the
// callees it calls that expand) and, within BUSTER_CHECK_TIER0_INLINE_DEPTH, its expansion, ready; past it, refused.
static void check_tier0_inline_settle(const CheckEmitRun* run, CheckTier0InlineLane* lane, const CheckTier1Inline* table, uint32_t v)
{
    const Tier0Function* function = &run->lowered->functions[v];
    uint32_t height = 1;
    for (uint32_t c = 0; c < function->call_count; c += 1)
    {
        const uint32_t w = check_tier0_inline_row(run, table, function, v, c);
        const bool ready = w != 0 && lane->states[w - 1] == BUSTER_CHECK_TIER0_INLINE_READY;
        height = ready && lane->heights[w - 1] + 1 > height ? lane->heights[w - 1] + 1 : height;
    }
    lane->heights[v] = height;
    if (height <= BUSTER_CHECK_TIER0_INLINE_DEPTH)
    {
        Tier0Inlined expanded;
        const bool spliced = check_tier0_inline_splice(run, lane, table, v, function, &lane->cache, &expanded);
        lane->functions[v] = spliced ? expanded.function : *function;
        lane->states[v] = BUSTER_CHECK_TIER0_INLINE_READY;
    }
    else
    {
        lane->states[v] = BUSTER_CHECK_TIER0_INLINE_REFUSED;
    }
}

// Settles every callee that body `b`'s rows reach in the lane's cache (the file's comment), by Tarjan's search from each
// one unmet, with explicit stacks: the search's (a callee and its next row each) and the components' (the callees in
// progress, whose components aren't complete). A component of more than one callee is refused whole; one of one is
// settled (check_tier0_inline_settle), after every callee it calls, which the search completed first. Body `b` is a node
// only as another's callee: its own code splices it.
static void check_tier0_inline_prepare(const CheckEmitRun* run, CheckTier0InlineLane* lane, const CheckTier1Inline* table, uint32_t b)
{
    const Tier0Function* root = &run->lowered->functions[b];
    uint32_t components = 0;
    for (uint32_t r = 0; r < root->call_count; r += 1)
    {
        const uint32_t start = check_tier0_inline_row(run, table, root, b, r);
        uint32_t depth = 0;
        if (start != 0 && lane->states[start - 1] == BUSTER_CHECK_TIER0_INLINE_UNPREPARED)
        {
            check_tier0_inline_visit(run, lane, start - 1, &components, &depth);
        }
        while (depth != 0)
        {
            const uint32_t v = lane->path[depth - 1];
            const Tier0Function* function = &run->lowered->functions[v];
            if (lane->cursors[depth - 1] < function->call_count)
            {
                const uint32_t w = check_tier0_inline_row(run, table, function, v, lane->cursors[depth - 1]);
                lane->cursors[depth - 1] += 1;
                const uint8_t state = w != 0 ? lane->states[w - 1] : BUSTER_CHECK_TIER0_INLINE_REFUSED;
                if (state == BUSTER_CHECK_TIER0_INLINE_UNPREPARED)
                {
                    check_tier0_inline_visit(run, lane, w - 1, &components, &depth);
                }
                else if (state == BUSTER_CHECK_TIER0_INLINE_PROGRESS)
                {
                    // On the component stack: a callee of v's component or one the path still holds.
                    lane->links[v] = lane->indices[w - 1] < lane->links[v] ? lane->indices[w - 1] : lane->links[v];
                }
            }
            else
            {
                depth -= 1;
                if (depth != 0)
                {
                    const uint32_t u = lane->path[depth - 1];
                    lane->links[u] = lane->links[v] < lane->links[u] ? lane->links[v] : lane->links[u];
                }
                if (lane->links[v] == lane->indices[v])
                {
                    // v's component, complete: the component stack from v up.
                    uint32_t first = components;
                    while (first != 0 && lane->components[first - 1] != v)
                    {
                        first -= 1;
                    }
                    first -= 1;
                    const bool alone = components - first == 1;
                    for (uint32_t k = first; k < components; k += 1)
                    {
                        lane->states[lane->components[k]] = BUSTER_CHECK_TIER0_INLINE_REFUSED;
                    }
                    components = first;
                    if (alone)
                    {
                        check_tier0_inline_settle(run, lane, table, v);
                    }
                }
            }
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
