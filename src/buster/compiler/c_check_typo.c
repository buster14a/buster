// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typo correction (#1447), clang 23's CorrectTypo for an undeclared name in C. An undeclared identifier in a value context
// (check_input_typo_use) gets "did you mean 'counter'?" and goes on as the candidate; a call to an undeclared function
// that clang declares implicitly (check_input_implicit_function) gets the suggestion as notes, the call staying the
// implicit declaration's. The candidates are the visible ordinary names of the kinds the context takes: objects, functions
// and enumerators for a value, functions for an implicit call (clang's DeclFilterCCC<FunctionDecl>); never a typedef's,
// a keyword, a member, a tag or a label.
//
// The choice is clang's TypoCorrectionConsumer's: a typo of fewer than 3 bytes is never corrected; a candidate whose length
// differs by `delta` is skipped where n / delta < 3 (n the typo's length, integer division); its Levenshtein distance
// (insertions, deletions and replacements of bytes, a transposition being two) counts within (n + 2) / 3; the least
// distance wins where one candidate alone has it (a tie gives none, whichever would type-check), and where n / distance
// is at least 3. Each attempt, failed or not, counts toward clang's 50 per unit (-fspell-checking-limit's default), after
// which no name is corrected. (-c's deferred bodies: a body whose misses no identifier of the unit is near stays on its
// lane and doesn't count them; one whose misses could be corrected is read in source order, which counts.)
//
// Data-parallel: the visible candidates are read off the bindings' active declarations once per scope state (a
// fingerprint of the active stack, CheckTypo.active and top), so a body's typos share one set. Their lengths are filtered
// 16 at a time, and the vector path's distances are Myers' and Hyyrö's bit-parallel algorithm with the typo as its
// pattern, one candidate in each of 16 32-bit lanes (typos up to 32 bytes), its bytes transposed per batch and its
// pattern masks gathered. The scalar path, its twin, takes the textbook dynamic programme; both give the exact distance.
// A typo past 32 bytes takes the dynamic programme on both. Only a miss runs any of it: error-free units pay a load.

enum
{
    // clang's -fspell-checking-limit default: the attempts a unit makes before none is corrected.
    CHECK_TYPO_LIMIT = 50,
    // The longest typo the bit-parallel distance holds, in its 32-bit lanes.
    CHECK_TYPO_LANE_BITS = 32,
    // The longest typo corrected at all; longer ones (no identifier a person types) are attempted and left.
    CHECK_TYPO_LONGEST = 255,
};

// A visible ordinary name a typo may be corrected to: its name, the token that declares it and its binding's kind.
typedef struct CheckTypoCandidate CheckTypoCandidate;
struct CheckTypoCandidate
{
    uint32_t name;
    uint32_t declaration;
    uint32_t length;
    uint8_t kind;
};

// A unit's typo state, made at its first miss: the visible candidates for the scope state they were read at, the
// attempts made (toward CHECK_TYPO_LIMIT) and each attempt's token and its correction (its declaration plus one, or 0),
// so that a token the walk visits again takes its first answer without counting again.
typedef struct CheckTypo CheckTypo;
struct CheckTypo
{
    CheckTypoCandidate* candidates;
    uint32_t candidate_count;
    uint32_t candidate_capacity;
    uint32_t active;
    uint32_t top;
    bool read;
    uint32_t attempts;
    uint32_t* tokens;
    uint32_t* corrections;
    uint32_t record_count;
    uint32_t record_capacity;
};

// The Levenshtein distance of `a` and `b`, by rows of the textbook dynamic programme: the scalar path's, and the twin
// and oracle of check_typo_distances.
static uint32_t check_typo_distance(const uint8_t* a, uint32_t a_length, const uint8_t* b, uint32_t b_length)
{
    uint32_t row[CHECK_TYPO_LONGEST + 1];
    for (uint32_t i = 0; i <= a_length; i += 1)
    {
        row[i] = i;
    }
    for (uint32_t j = 1; j <= b_length; j += 1)
    {
        uint32_t diagonal = row[0];
        row[0] = j;
        for (uint32_t i = 1; i <= a_length; i += 1)
        {
            const uint32_t above = row[i];
            uint32_t best = diagonal + (a[i - 1] != b[j - 1] ? 1u : 0u);
            best = above + 1 < best ? above + 1 : best;
            best = row[i - 1] + 1 < best ? row[i - 1] + 1 : best;
            row[i] = best;
            diagonal = above;
        }
    }
    return row[a_length];
}

#if BUSTER_HOST_VECTOR
// The distances of the typo (`peq` its pattern's masks: bit i of peq[c] where its byte i is c; `length` up to 32 bytes) to
// up to 16 candidates (`bytes`, `lengths`; a lane past `lanes` has length 0), into `distances`: Myers' and Hyyrö's
// bit-parallel algorithm, one candidate in each 32-bit lane. The vertical deltas of the pattern's column (pv, mv) step
// through the candidates' bytes, transposed so that row j holds each lane's byte j; a lane past its candidate's end keeps
// its state. The score starts at the typo's length, row 0's horizontal delta is +1 (a global distance), and each step adds
// the bottom row's horizontal delta.
static void check_typo_distances(const uint32_t* peq, uint32_t length, const uint8_t* const* bytes, const uint32_t* lengths, uint32_t lanes,
                                 uint32_t* distances)
{
    enum { ROWS = CHECK_TYPO_LANE_BITS + CHECK_TYPO_LANE_BITS / 3 + 1 };
    uint8_t rows[ROWS][16];
    uint32_t widths[16];
    uint32_t longest = 0;
    memset(rows, 0, sizeof(rows));
    for (uint32_t lane = 0; lane < 16; lane += 1)
    {
        widths[lane] = lane < lanes ? lengths[lane] : 0;
        longest = widths[lane] > longest ? widths[lane] : longest;
        for (uint32_t j = 0; j < widths[lane]; j += 1)
        {
            rows[j][lane] = bytes[lane][j];
        }
    }
    const __m512i mask = _mm512_set1_epi32(length == 32 ? -1 : (int)((1u << length) - 1));
    const __m512i top = _mm512_set1_epi32((int)(1u << (length - 1)));
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i ends = _mm512_loadu_si512(widths);
    __m512i pv = mask;
    __m512i mv = _mm512_setzero_si512();
    __m512i score = _mm512_set1_epi32((int)length);
    for (uint32_t j = 0; j < longest; j += 1)
    {
        const __mmask16 active = _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32((int)j));
        const __m512i bytes_j = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)rows[j]));
        const __m512i eq = _mm512_i32gather_epi32(bytes_j, peq, 4);
        const __m512i xv = _mm512_or_si512(eq, mv);
        const __m512i xh = _mm512_and_si512(_mm512_or_si512(_mm512_xor_si512(_mm512_add_epi32(_mm512_and_si512(eq, pv), pv), pv), eq), mask);
        __m512i ph = _mm512_and_si512(_mm512_or_si512(mv, _mm512_andnot_si512(_mm512_or_si512(xh, pv), mask)), mask);
        __m512i mh = _mm512_and_si512(pv, xh);
        score = _mm512_mask_add_epi32(score, _mm512_test_epi32_mask(ph, top) & active, score, one);
        score = _mm512_mask_sub_epi32(score, _mm512_test_epi32_mask(mh, top) & active, score, one);
        ph = _mm512_and_si512(_mm512_or_si512(_mm512_slli_epi32(ph, 1), one), mask);
        mh = _mm512_and_si512(_mm512_slli_epi32(mh, 1), mask);
        pv = _mm512_mask_mov_epi32(pv, active, _mm512_and_si512(_mm512_or_si512(mh, _mm512_andnot_si512(_mm512_or_si512(xv, ph), mask)), mask));
        mv = _mm512_mask_mov_epi32(mv, active, _mm512_and_si512(ph, xv));
    }
    _mm512_storeu_si512(distances, score);
}
#endif

// The unit's typo state, made at its first miss. NULL where room ran out.
static CheckTypo* check_typo_state(CheckInputReader* reader)
{
    if (reader->typo == NULL)
    {
        reader->typo = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckTypo, 1);
        reader->output->resource |= reader->typo == NULL;
        if (reader->typo != NULL)
        {
            *reader->typo = (CheckTypo){ 0 };
        }
    }
    return reader->typo;
}

// The visible ordinary names, read off the bindings' active declarations where the scope state changed since they were
// last read: each active declaration that is still its name's head. Whether there was room.
static bool check_typo_candidates(CheckInputReader* reader, CheckTypo* typo)
{
    const CBindings* bindings = &reader->bindings;
    const uint32_t active = bindings->active_count;
    const uint32_t top = active != 0 ? bindings->active[active - 1] : 0;
    bool valid = true;
    if (!typo->read || typo->active != active || typo->top != top)
    {
        if (typo->candidate_capacity < active)
        {
            const uint32_t capacity = active < 64 ? 64 : active;
            typo->candidates = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckTypoCandidate, capacity);
            typo->candidate_capacity = typo->candidates != NULL ? capacity : 0;
        }
        valid = typo->candidates != NULL || active == 0;
        typo->candidate_count = 0;
        for (uint32_t i = 0; valid && i < active; i += 1)
        {
            const uint32_t event = bindings->active[i];
            const uint32_t key = bindings->key[event];
            // (An ordinary name's key is its name: the ordinary namespace is the first.)
            const bool ordinary = key < bindings->name_capacity && bindings->heads[key] == event + 1;
            if (ordinary)
            {
                typo->candidates[typo->candidate_count++] = (CheckTypoCandidate){
                    .name = key,
                    .declaration = bindings->declaration[event],
                    .length = intern_length(reader->pp->table, key),
                    .kind = bindings->kind[event],
                };
            }
        }
        typo->read = valid;
        typo->active = active;
        typo->top = top;
    }
    return valid;
}

// The correction of the undeclared name at `token` (clang's CorrectTypo, above): the declaration token of the visible name
// it takes, of a function where `functions` (an implicit call's), else of an object, a function or an enumerator; or
// BUSTER_CHECK_NONE. Each call is an attempt toward CHECK_TYPO_LIMIT, but a token's second, which takes its first's answer.
static uint32_t check_typo_correct(CheckInputReader* reader, uint32_t token, bool functions)
{
    CheckTypo* typo = check_typo_state(reader);
    uint32_t result = BUSTER_CHECK_NONE;
    bool known = false;
    // A token the walk visits again (a type query's replay) is at or before the last attempt's, in source order.
    for (uint32_t r = typo != NULL ? typo->record_count : 0; !known && r-- > 0 && typo->tokens[r] >= token;)
    {
        known = typo->tokens[r] == token;
        result = known && typo->corrections[r] != 0 ? typo->corrections[r] - 1 : result;
    }
    if (typo != NULL && !known && typo->attempts < CHECK_TYPO_LIMIT)
    {
        typo->attempts += 1;
        const uint32_t name = reader->parser->tokens.names[token];
        const uint32_t length = intern_length(reader->pp->table, name);
        const uint8_t* bytes = (const uint8_t*)intern_bytes(reader->pp->table, name);
        const uint32_t bound = (length + 2) / 3;
        const bool attempted = length >= 3 && length <= CHECK_TYPO_LONGEST && check_typo_candidates(reader, typo);
        const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar) || length > CHECK_TYPO_LANE_BITS;
        uint32_t best = UINT32_MAX;
        uint32_t best_count = 0;
        uint32_t best_declaration = BUSTER_CHECK_NONE;
        // The candidates the kinds and the lengths leave, then their distances: 16 at a time on the vector path.
        uint32_t peq[256];
        if (attempted && !scalar)
        {
            memset(peq, 0, sizeof(peq));
            for (uint32_t i = 0; i < length; i += 1)
            {
                peq[bytes[i]] |= 1u << i;
            }
        }
        uint32_t batch[16];
        uint32_t batched = 0;
        for (uint32_t c = 0; attempted && c <= typo->candidate_count; c += 1)
        {
            // (Past the last, the batch left is flushed.)
            const CheckTypoCandidate candidate = c < typo->candidate_count ? typo->candidates[c] : (CheckTypoCandidate){ 0 };
            const bool kind = c < typo->candidate_count &&
                              (candidate.kind == C_BINDING_FUNCTION || (!functions && (candidate.kind == C_BINDING_OBJECT || candidate.kind == C_BINDING_ENUMERATOR)));
            const uint32_t delta = candidate.length > length ? candidate.length - length : length - candidate.length;
            const bool near = kind && delta <= bound && (delta == 0 || length / delta >= 3) && candidate.length <= CHECK_TYPO_LONGEST;
            if (near)
            {
                batch[batched++] = c;
            }
            const bool flush = batched == 16 || (c == typo->candidate_count && batched != 0);
            uint32_t distances[16];
            if (flush && scalar)
            {
                for (uint32_t b = 0; b < batched; b += 1)
                {
                    const CheckTypoCandidate* item = typo->candidates + batch[b];
                    distances[b] = check_typo_distance(bytes, length, (const uint8_t*)intern_bytes(reader->pp->table, item->name), item->length);
                }
            }
#if BUSTER_HOST_VECTOR
            else if (flush)
            {
                const uint8_t* lanes[16];
                uint32_t lengths[16];
                for (uint32_t b = 0; b < batched; b += 1)
                {
                    lanes[b] = (const uint8_t*)intern_bytes(reader->pp->table, typo->candidates[batch[b]].name);
                    lengths[b] = typo->candidates[batch[b]].length;
                }
                check_typo_distances(peq, length, lanes, lengths, batched, distances);
            }
#endif
            for (uint32_t b = 0; flush && b < batched; b += 1)
            {
                const uint32_t distance = distances[b];
                const bool counted = distance <= bound;
                best_count = counted && distance == best ? best_count + 1 : counted && distance < best ? 1 : best_count;
                best_declaration = counted && distance < best ? typo->candidates[batch[b]].declaration : best_declaration;
                best = counted && distance < best ? distance : best;
            }
            batched = flush ? 0 : batched;
        }
        result = best_count == 1 && best != 0 && length / best >= 3 ? best_declaration : BUSTER_CHECK_NONE;
    }
    // Recorded for the walk's visits again; the attempts past the limit too, which can't count.
    if (typo != NULL && !known)
    {
        if (typo->record_count == typo->record_capacity)
        {
            const uint32_t capacity = typo->record_capacity == 0 ? 16 : typo->record_capacity * 2;
            uint32_t* tokens = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
            uint32_t* corrections = tokens != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity) : NULL;
            reader->output->resource |= corrections == NULL;
            if (corrections != NULL && typo->record_count != 0)
            {
                memcpy(tokens, typo->tokens, (uint64_t)typo->record_count * sizeof(uint32_t));
                memcpy(corrections, typo->corrections, (uint64_t)typo->record_count * sizeof(uint32_t));
            }
            typo->tokens = corrections != NULL ? tokens : typo->tokens;
            typo->corrections = corrections != NULL ? corrections : typo->corrections;
            typo->record_capacity = corrections != NULL ? capacity : typo->record_capacity;
        }
        if (typo->record_count < typo->record_capacity)
        {
            typo->tokens[typo->record_count] = token;
            typo->corrections[typo->record_count] = result != BUSTER_CHECK_NONE ? result + 1 : 0;
            typo->record_count += 1;
        }
    }
    return result;
}

// Whether a tree role is an expression's node, which a full expression's root climbs through.
static bool check_typo_expression_role(uint8_t role)
{
    return role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_STRING_NEXT || role == PARSE_ROLE_PREFIX ||
           role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_MEMBER_OPERATOR || role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE ||
           role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND ||
           role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR ||
           role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_OPEN_CALL ||
           role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF || role == PARSE_ROLE_OPEN_SUBSCRIPT || role == PARSE_ROLE_OPEN_TERNARY ||
           role == PARSE_ROLE_OPEN_COMPOUND;
}

// The first token of the full expression that holds `token`: clang corrects a typo once its full expression is complete
// (its TypoExpr), so its error comes after that expression's own diagnostics and before those of what takes it (an
// initialization's, a return's conversion), which are located at its start and published past it.
static uint32_t check_typo_expression_first(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t top = token;
    uint32_t parent = parser->tree.parents[top];
    for (uint32_t steps = 0; parent < count && check_typo_expression_role(parser->tree.roles[parent]) && steps < count; steps += 1)
    {
        top = parent;
        parent = parser->tree.parents[top];
    }
    const uint32_t size = reader->subtree_sizes[reader->postorder_positions[top]];
    return size != 0 && size <= reader->ends[top] + 1 ? reader->ends[top] + 1 - size : token;
}

// Whether a lane's miss at `token` could be corrected (#1447): whether any identifier of the unit but the typo itself passes
// the length filter and is within the distance bound and a third of the typo's length. A superset of the visible
// candidates, which a lane can't list (its stream has the top level's names only as the proxies of the names its body
// uses): where one is, the lane refuses, and source order corrects the typo and counts the attempt; where none is, no
// correction exists and the body stays on its lane. Scalar alone: a lane's miss is an error's.
static bool check_typo_lane_near(const CheckInputReader* reader, uint32_t token)
{
    const InternTable* table = reader->pp->table;
    const uint32_t name = reader->parser->tokens.names[token];
    const uint32_t length = intern_length(table, name);
    const uint32_t bound = (length + 2) / 3;
    const uint8_t* bytes = intern_bytes(table, name);
    bool near = false;
    for (uint32_t id = 1; length >= 3 && length <= CHECK_TYPO_LONGEST && !near && id < table->count; id += 1)
    {
        const uint32_t other = intern_length(table, id);
        const uint32_t delta = other > length ? other - length : length - other;
        const bool filtered = id != name && delta <= bound && (delta == 0 || length / delta >= 3) && other <= CHECK_TYPO_LONGEST;
        const uint32_t distance = filtered ? check_typo_distance(bytes, length, intern_bytes(table, id), other) : 0;
        near = filtered && distance != 0 && distance <= bound && length / distance >= 3;
    }
    return near;
}

// A value context's use of a name no declaration binds (#1447), the walk in source order's (check_input_resolve_name) at
// its use event (`event`, plus one): where a visible name corrects it, the event becomes the candidate's use, which both
// resolvers then answer with the candidate's declaration (its type, and the diagnostics that follow from it, clang's),
// and clang's error, "did you mean 'counter'?" with its note at the candidate, is reported and recovered: the use is no
// missing name, which check_input_diagnostic_recover would cut. A lane's miss that a correction could take goes to source
// order, where the attempts are counted in the unit's order (check_typo_lane_near). Rare: its caller tests the event's
// head first.
static __attribute__((noinline, cold)) void check_input_typo_use(CheckInputReader* reader, uint32_t token, uint32_t event)
{
    check_input_lane_refuse(reader, reader->lane && check_typo_lane_near(reader, token), "a missing name's typo correction counts in source order");
    const uint32_t declaration = reader->lane ? BUSTER_CHECK_NONE : check_typo_correct(reader, token, false);
    if (declaration != BUSTER_CHECK_NONE)
    {
        CBindings* bindings = &reader->bindings;
        const uint32_t key = reader->parser->tokens.names[declaration];
        const uint32_t previous = bindings->heads[key];
        bindings->key[event - 1] = key;
        bindings->previous[event - 1] = previous;
        if (bindings->first != 0)
        {
            bindings->first[event - 1] = previous != 0 ? bindings->first[previous - 1] : event;
        }
        CheckInput* output = reader->output;
        const uint32_t before = output->report_count;
        check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
        if (output->report_count > before)
        {
            output->reports[before].subject = declaration + 1;
            output->reports[before].note_token = declaration;
            output->reports[before].note_kind = CHECK_INPUT_NOTE_FUNCTION_DECLARATION;
            // Ordered and published at its full expression's start, each of its typos with its own (`repeated`).
            output->reports[before].sort = check_typo_expression_first(reader, token);
            output->reports[before].completion = output->reports[before].sort + 1;
            output->reports[before].repeated = true;
            check_input_recover_reports(reader, before);
        }
    }
}
