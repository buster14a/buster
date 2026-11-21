// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The lifetimes of a unit's block-scope objects (#177), for Tier 1's frame, which shares a slot between objects that are
// never live at once (tier1_emit_cfg_lifetimes). C gives an automatic object its block's lifetime: from the block's entry
// to its exit, however either happens (C17 6.2.4p5), whatever its declaration's place in the block. So each block that
// declares locals (CheckProgram.lifetime_first, by its closing brace) gets a CHECK_CONTROL_LIFETIME start where its
// statements begin, and an end where they end and before each break, continue or goto that leaves it
// (check_control_lifetimes_left); a return, a computed goto and the function's own end need none, since nothing follows
// them in the block, and an end missing on a path only makes its objects live longer. The lowering turns each into its
// locals' events (Tier0Function.lifetime_entries), and ir_lower into IR_LIFETIME markers.
//
// A block that a jump can enter other than through its opening brace has no lifetimes: its objects live throughout the
// function, as clang leaves an object a goto bypasses without lifetime markers (VarBypassDetector). Those are the blocks
// between a goto's label (or an asm goto's) and the innermost block that holds the jump too, between a case or default
// label and its switch (a switch's own block among them, whose opening brace never runs), and every block around a label
// whose address is taken. So is a statement expression's block (its region's), whose value the region computes after
// its statements. No emitted body reaches the asm goto part yet: the lowering refuses asm goto by name
// (CheckProgramAsm.reason, c_check_program_asm.c), and the part stands ready for when it takes one.
//
// Included by c_check_program.c. Only a program asked for them (CheckProgramOptions.lifetimes, the C driver's --tier1)
// computes them, so a compile without --tier1 pays nothing for them. Every pass is linear in the tokens and the jumps
// (#1355): local ownership and control capacity by memoized parent paths, the blocks a jump enters by their jumps'
// sources folded up the blocks with locals and two sweeps of the tokens, and the writer's walks over the blocks with
// lifetimes alone (check_control_lifetimes_left), along the chain check_program_lifetime_entered leaves in its first
// column (docs/commands/tier1-frame.md's "Lifetime planning on deep trees").

// Whether `token` is a local declaration of an object: the lowering's own test (check_lower_local_declaration), whose
// locals the program's bodies allocate.
static bool check_program_lifetime_local(const CheckInput* input, uint32_t token)
{
    return input->declarations[token] == token && check_sparse_get(input->declaration_bodies, token) < input->token_count &&
           input->declaration_kinds[token] == C_BINDING_OBJECT && !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC);
}

// Each block's local declarations, linked in token order from `first` (by the block's closing brace, BUSTER_CHECK_NONE
// for a block without any) through `next` (by declaration); both have a token's room each. Returns the most
// CHECK_CONTROL_LIFETIME entries the writer can make of them: a start and an end per such block, and an end per block
// such a jump leaves, each jump (a token of role RETURN) counted for every such block around it. `work` has count + 1
// entries, distinct from the immutable parser/input and both outputs. Each parent path stops at a cached answer, then
// retraces its previously unknown nodes to publish that answer. Each node is filled once, so the total is linear for
// a valid tree. This needs no postorder: tree-only driver parses do not retain one. No extra allocation is needed.
static uint64_t check_program_lifetime_blocks(const Parser* parser, const CheckInput* input, uint32_t* first, uint32_t* next, uint32_t* work)
{
    const uint32_t count = input->token_count;
    const uint32_t* const parents = parser->tree.parents;
    const uint8_t* const roles = parser->tree.roles;
    uint64_t room = 0;
    memset(first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    memset(work, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    // The closest block or function-body boundary above each token. A declaration in the function's own block keeps
    // its whole-function lifetime, so a body resets the inherited block to NONE.
    for (uint32_t token = 0; token < count; token += 1)
    {
        uint32_t ancestor = token;
        uint32_t block = count;
        uint32_t steps = 0;
        while (ancestor < count && work[ancestor] == BUSTER_CHECK_NONE && steps < count)
        {
            const uint32_t parent = parents[ancestor];
            const uint8_t role = parent < count ? roles[parent] : PARSE_ROLE_CLOSE_BODY;
            if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY)
            {
                block = role == PARSE_ROLE_CLOSE_BLOCK ? parent : count;
                ancestor = count;
            }
            else
            {
                ancestor = parent;
            }
            steps += 1;
        }
        block = ancestor < count && work[ancestor] != BUSTER_CHECK_NONE ? work[ancestor] : block;
        ancestor = token;
        steps = 0;
        while (ancestor < count && work[ancestor] == BUSTER_CHECK_NONE && steps < count)
        {
            work[ancestor] = block;
            const uint32_t parent = parents[ancestor];
            const uint8_t role = parent < count ? roles[parent] : PARSE_ROLE_CLOSE_BODY;
            ancestor = role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY ? count : parent;
            steps += 1;
        }
    }
    for (uint32_t token = count; token-- > 0;)
    {
        const uint32_t block = check_program_lifetime_local(input, token) && work[token] < count ? work[token] : BUSTER_CHECK_NONE;
        if (block != BUSTER_CHECK_NONE)
        {
            room += first[block] == BUSTER_CHECK_NONE ? 2 : 0;
            next[token] = first[block];
            first[block] = token;
        }
    }
    // Reuse the same column for the number of blocks with locals above a token. Count jumps after all declarations
    // have been linked, since a declaration later in the block has the same block lifetime as an earlier one.
    if (room != 0)
    {
        memset(work, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            uint32_t ancestor = token;
            uint32_t depth = 0;
            uint32_t steps = 0;
            while (ancestor < count && work[ancestor] == BUSTER_CHECK_NONE && steps < count)
            {
                const uint32_t parent = parents[ancestor];
                const bool inside = parent < count && roles[parent] != PARSE_ROLE_CLOSE_BODY;
                depth += inside && roles[parent] == PARSE_ROLE_CLOSE_BLOCK && first[parent] != BUSTER_CHECK_NONE;
                ancestor = inside ? parent : count;
                steps += 1;
            }
            depth += ancestor < count && work[ancestor] != BUSTER_CHECK_NONE ? work[ancestor] : 0;
            ancestor = token;
            steps = 0;
            while (ancestor < count && work[ancestor] == BUSTER_CHECK_NONE && steps < count)
            {
                work[ancestor] = depth;
                const uint32_t parent = parents[ancestor];
                const bool inside = parent < count && roles[parent] != PARSE_ROLE_CLOSE_BODY;
                depth -= inside && roles[parent] == PARSE_ROLE_CLOSE_BLOCK && first[parent] != BUSTER_CHECK_NONE;
                ancestor = inside ? parent : count;
                steps += 1;
            }
            room += roles[token] == PARSE_ROLE_RETURN ? work[token] : 0;
        }
    }
    return room;
}

// Each token's innermost block with locals above it, in `nearest` (a token's room and one more): its parent, where that
// is a block whose `first` isn't BUSTER_CHECK_NONE, else its parent's own answer; `count` past the root, and with `bodies`
// past a function's body too. Each path climbs to the first token whose answer is known or is its parent's own, then
// retraces the tokens it climbed, each filled once, so the work is linear in the tokens (#1355), as
// check_program_lifetime_blocks' passes are. Returns the steps of the climbs and the retraces, which test-checker bounds
// (CheckProgramOptions.lifetime_work). The paths stay scalar, as check_program_lifetime_blocks' do: each step's load is
// the address of the next (docs/commands/tier1-frame.md says why, and what is still to measure on the 9700X).
static uint64_t check_program_lifetime_nearest(const Parser* parser, const uint32_t* first, uint32_t* nearest, bool bodies)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t* const parents = parser->tree.parents;
    const uint8_t* const roles = parser->tree.roles;
    uint64_t work = 0;
    memset(nearest, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    for (uint32_t token = 0; token < count; token += 1)
    {
        uint32_t ancestor = token;
        uint32_t answer = BUSTER_CHECK_NONE;
        uint32_t steps = 0;
        while (ancestor < count && nearest[ancestor] == BUSTER_CHECK_NONE && answer == BUSTER_CHECK_NONE && steps < count)
        {
            const uint32_t parent = parents[ancestor];
            const bool outside = parent >= count || (bodies && roles[parent] == PARSE_ROLE_CLOSE_BODY);
            const bool held = !outside && roles[parent] == PARSE_ROLE_CLOSE_BLOCK && first[parent] != BUSTER_CHECK_NONE;
            answer = outside ? count : held ? parent : BUSTER_CHECK_NONE;
            ancestor = parent;
            steps += 1;
        }
        answer = answer == BUSTER_CHECK_NONE && ancestor < count ? nearest[ancestor] : answer;
        work += steps;
        ancestor = token;
        steps = 0;
        while (ancestor < count && nearest[ancestor] == BUSTER_CHECK_NONE && steps < count)
        {
            nearest[ancestor] = answer;
            const uint32_t parent = parents[ancestor];
            const bool outside = parent >= count || (bodies && roles[parent] == PARSE_ROLE_CLOSE_BODY);
            const bool held = !outside && roles[parent] == PARSE_ROLE_CLOSE_BLOCK && first[parent] != BUSTER_CHECK_NONE;
            ancestor = outside || held ? count : parent;
            steps += 1;
        }
        work += steps;
    }
    return work;
}

// A jump from `source` to `target` folded into the innermost block with locals above the target (`nearest`, the sink
// `count` for none): a forward one's source (before its target) as the least in `sources`, a backward one's, with
// `backward`, as the greatest. A source at or past `count` is backward: it stamps no ancestor, as an addressed label's
// jumps (BUSTER_CHECK_NONE) and a case without its switch do.
static void check_program_lifetime_fold(const uint32_t* nearest, uint32_t* sources, uint32_t count, uint32_t source, uint32_t target, bool backward)
{
    if (target < count && (source > target) == backward)
    {
        const uint32_t block = nearest[target] < count ? nearest[target] : count;
        const uint32_t held = sources[block];
        sources[block] = backward ? (source > held ? source : held) : (source < held ? source : held);
    }
}

// The lanes of the 64 tokens from `base` that are the unit's: every one but in the last chunk of `count`.
static __mmask64 check_program_lifetime_lanes(uint32_t count, uint32_t base)
{
    return count - base < 64 ? _bzhi_u64(UINT64_MAX, count - base) : UINT64_MAX;
}

// Every jump into a token of the unit folded (check_program_lifetime_fold) with `backward`: a goto's (not a computed
// one's) into its label, each label whose address is taken (`addressed`, null for none) from nowhere, a switch into each
// of its cases, and an asm goto into each of its labels (unreached while the lowering refuses asm goto). The gotos and the
// labels come from masks of 64 tokens' kinds, roles and marks at a time, the last chunk's lanes past the unit's masked
// off (#1355), and only their set lanes are visited.
static void check_program_lifetime_jumps(const CheckProgram* program, const Parser* parser, const uint8_t* addressed, const uint32_t* nearest, uint32_t* sources,
                                         bool backward)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    const uint32_t chunks = count / 64 + (count % 64 != 0);
    for (uint32_t chunk = 0; chunk < chunks; chunk += 1)
    {
        const uint32_t base = chunk * 64;
        const __mmask64 lanes = check_program_lifetime_lanes(count, base);
        const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, parser->tokens.kinds + base);
        const __m512i roles = _mm512_maskz_loadu_epi8(lanes, parser->tree.roles + base);
        const uint64_t gotos = _mm512_mask_cmpeq_epi8_mask(lanes, kinds, _mm512_set1_epi8(PARSE_KIND_GOTO)) & _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_RETURN));
        const uint64_t marked = addressed != NULL ? _mm512_mask_test_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, addressed + base), _mm512_set1_epi8(-1)) : 0;
        uint64_t selected = gotos | marked;
        while (selected != 0)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(selected);
            const uint32_t token = base + lane;
            if (((gotos >> lane) & 1) != 0 && !check_input_indirect_goto(parser, token))
            {
                check_program_lifetime_fold(nearest, sources, count, token, check_sparse_get(input->label_targets, token), backward);
            }
            if (((marked >> lane) & 1) != 0)
            {
                check_program_lifetime_fold(nearest, sources, count, BUSTER_CHECK_NONE, token, backward);
            }
            selected &= selected - 1;
        }
    }
    for (uint32_t c = 0; c < program->case_count; c += 1)
    {
        check_program_lifetime_fold(nearest, sources, count, program->cases[c].owner, program->cases[c].token, backward);
    }
    for (uint32_t a = 0; a < input->asm_count; a += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + a;
        for (uint32_t l = 0; assembly->statement < count && l < assembly->label_count; l += 1)
        {
            check_program_lifetime_fold(nearest, sources, count, assembly->statement, assembly->labels[l].target, backward);
        }
    }
}

// Leaves out of `first` the blocks a jump enters other than through their opening brace, and the statement expressions'
// blocks. A jump enters each block with locals around its target that doesn't hold its source too: the blocks between the
// target and the innermost that holds both, below the target's function's body (a source past the unit's tokens holds
// none). A block's tokens are a run from its opening brace to its closing one, so a source outside that run enters it.
// Rather than a walk from each target (quadratic in an else-if chain's depth, #1355), each target's block with locals
// takes its jumps' sources, and each block passes its own to the block with locals around it, from the inside out: a
// sweep from the last token back takes each block at its opening brace, after the blocks inside it, and leaves out one
// that a source before that brace enters (forward jumps, the least source); a sweep from the first token takes each at its
// closing brace and leaves out one that a source after it enters (backward jumps, the greatest). `nearest` and `sources`
// have a token's room and one more each; afterwards `nearest` holds each token's innermost block with lifetimes above it,
// past function bodies, which the writer's walks follow (check_control_lifetimes_left). The sweeps visit the braces alone,
// by masks of 64 tokens' roles at a time; each block's fold reads the blocks' inside it, so the braces go in order.
// Returns the steps of the parent paths (check_program_lifetime_nearest).
static uint64_t check_program_lifetime_entered(const CheckProgram* program, const Parser* parser, const uint8_t* addressed, uint32_t* nearest, uint32_t* sources,
                                               uint32_t* first)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    const uint32_t* const parents = parser->tree.parents;
    const uint8_t* const roles = parser->tree.roles;
    uint64_t work = check_program_lifetime_nearest(parser, first, nearest, true);
    memset(sources, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    check_program_lifetime_jumps(program, parser, addressed, nearest, sources, false);
    // From the last chunk of 64 tokens back, its opening braces from the last: a block inside another opens after it, so
    // is met first.
    const uint32_t chunks = count / 64 + (count % 64 != 0);
    for (uint32_t chunk = chunks; chunk-- > 0;)
    {
        const uint32_t base = chunk * 64;
        const __mmask64 lanes = check_program_lifetime_lanes(count, base);
        uint64_t openings = _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, roles + base), _mm512_set1_epi8(PARSE_ROLE_OPEN_BLOCK));
        while (openings != 0)
        {
            const uint32_t lane = 63 - (uint32_t)__builtin_clzll(openings);
            const uint32_t opening = base + lane;
            const uint32_t block = parents[opening];
            if (block < count && roles[block] == PARSE_ROLE_CLOSE_BLOCK)
            {
                const uint32_t outer = nearest[block] < count ? nearest[block] : count;
                sources[outer] = sources[block] < sources[outer] ? sources[block] : sources[outer];
                first[block] = sources[block] < opening ? BUSTER_CHECK_NONE : first[block];
            }
            openings ^= UINT64_C(1) << lane;
        }
    }
    memset(sources, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    check_program_lifetime_jumps(program, parser, addressed, nearest, sources, true);
    // From the first chunk, its closing braces from the first: a block inside another closes before it.
    for (uint32_t chunk = 0; chunk < chunks; chunk += 1)
    {
        const uint32_t base = chunk * 64;
        const __mmask64 lanes = check_program_lifetime_lanes(count, base);
        uint64_t closings = _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, roles + base), _mm512_set1_epi8(PARSE_ROLE_CLOSE_BLOCK));
        while (closings != 0)
        {
            const uint32_t block = base + (uint32_t)_tzcnt_u64(closings);
            const uint32_t outer = nearest[block] < count ? nearest[block] : count;
            sources[outer] = sources[block] > sources[outer] ? sources[block] : sources[outer];
            first[block] = sources[block] > block ? BUSTER_CHECK_NONE : first[block];
            closings &= closings - 1;
        }
    }
    for (uint32_t r = 0; r < input->region_count; r += 1)
    {
        if (input->regions[r].block < count)
        {
            first[input->regions[r].block] = BUSTER_CHECK_NONE;
        }
    }
    work += check_program_lifetime_nearest(parser, first, nearest, false);
    return work;
}
