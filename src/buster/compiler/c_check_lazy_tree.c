// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1364's common-case lazy demand planner. The successful syntax tree certifies direct file-scope function
// declarators and expression uses of their names. A name which might instead declare an ordinary shadow, a
// function typedef, or a declarator outside that certificate sends the unit to the exact binding adapter.
// No syntax is reparsed on the admitted path. The old adapter is also the independent selection oracle.
//
// Only names of definitions matter to demand. Their first visible declaration points suffice once the pass has
// proved that none of those names has a scoped ordinary declaration. Names in member/tag/label namespaces and
// attributes do not add edges. Source-order uses before a complete file declarator remain unresolved, as in the
// binding adapter. The semantic reader still visits its selected bodies in original source order.
//
// Scratch holds one name-to-body table and compact body/reference columns, never a token-wide owner or binding
// column. Numeric name masks have scalar/vector twins. Unsupported inputs and resource failures restore both
// arenas; success retains only the existing ordered body records and final byte mask.

#ifndef BUSTER_CHECK_LAZY_TREE_INCLUDED
#define BUSTER_CHECK_LAZY_TREE_INCLUDED 1

#define BUSTER_CHECK_LAZY_TREE_OPAQUE UINT32_C(0x80000000)
#define BUSTER_CHECK_LAZY_TREE_KEEP UINT32_C(0x40000000)
#define BUSTER_CHECK_LAZY_TREE_BODY UINT32_C(0x3fffffff)
#define BUSTER_CHECK_LAZY_TREE_PARENTS 256u

typedef enum CheckLazyTreeUse
{
    CHECK_LAZY_TREE_UNKNOWN,
    CHECK_LAZY_TREE_IGNORED,
    CHECK_LAZY_TREE_USE,
} CheckLazyTreeUse;

static uint64_t check_lazy_tree_mask_scalar(const Parser* parser, const uint32_t* targets, uint32_t names, uint32_t first, uint32_t count)
{
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const ids = parser->tokens.names;
    uint64_t mask = 0;
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        const uint32_t token = first + lane;
        const uint32_t id = ids[token];
        const bool named = id < names && targets[id] != 0;
        const bool special = kinds[token] == PARSE_KIND_ASM || gnu_syntax_attribute_role(roles[token]);
        mask |= named || special ? UINT64_C(1) << lane : 0;
    }
    return mask;
}

static __attribute__((noinline)) uint64_t check_lazy_tree_mask(const Parser* parser, const uint32_t* targets, uint32_t names, uint32_t first, uint32_t count, bool scalar)
{
    uint64_t mask = 0;
#if BUSTER_HOST_VECTOR
    if (!scalar && names <= INT32_MAX)
    {
        const uint64_t live = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        const __m512i kinds = _mm512_maskz_loadu_epi8(live, parser->tokens.kinds + first);
        const __m512i roles = _mm512_maskz_loadu_epi8(live, parser->tree.roles + first);
        mask = _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8(PARSE_KIND_ASM));
        const __mmask64 attributes = _mm512_mask_cmpge_epu8_mask(live, roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_ATTRIBUTE)) &
                                    _mm512_cmple_epu8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT));
        mask |= attributes;
        const uint32_t* const ids = parser->tokens.names;
        for (uint32_t lane = 0; lane < count; lane += 16)
        {
            const uint32_t remaining = count - lane;
            const __mmask16 active = remaining < 16 ? (__mmask16)((1u << remaining) - 1) : (__mmask16)UINT16_MAX;
            const __m512i keys = _mm512_maskz_loadu_epi32(active, ids + first + lane);
            const __mmask16 bounded = _mm512_mask_cmplt_epu32_mask(active, keys, _mm512_set1_epi32((int32_t)names));
            const __m512i values = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), bounded, keys, targets, 4);
            const __mmask16 named = _mm512_mask_cmpneq_epi32_mask(bounded, values, _mm512_setzero_si512());
            mask |= (uint64_t)named << lane;
        }
    }
    else
#else
    (void)scalar;
#endif
    {
        mask = check_lazy_tree_mask_scalar(parser, targets, names, first, count);
    }
    return mask;
}

// A successful parser gives every identifier used as a specifier its typedef meaning. Tag names have their own
// roles. Only the small outer header is walked here; nested groups are skipped exactly as check_lazy_header does.
static uint32_t check_lazy_tree_header(const Parser* parser, const Preprocessor* pp, uint32_t first, uint32_t end)
{
    const uint8_t* const roles = parser->tree.roles;
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint32_t* const names = parser->tokens.names;
    const uint32_t* const parents = parser->tree.parents;
    uint32_t flags = 0;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint8_t role = roles[token];
        const uint32_t name = names[token];
        if (role == PARSE_ROLE_SPECIFIER)
        {
            flags |= name == C_NAME_STATIC ? BUSTER_CHECK_LAZY_STATIC : 0;
            flags |= name == C_NAME_EXTERN ? BUSTER_CHECK_LAZY_EXTERN : 0;
            flags |= name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2 ? BUSTER_CHECK_LAZY_INLINE : 0;
            flags |= kinds[token] == PARSE_KIND_IDENTIFIER ? BUSTER_CHECK_LAZY_TYPEDEF : 0;
        }
        if (gnu_syntax_attribute_role(role))
        {
            const uint32_t close = parents[token];
            for (uint32_t attribute = token + 1; attribute < close && attribute < end; attribute += 1)
            {
                const uint32_t id = names[attribute];
                const bool retained = check_lazy_name(pp->table, id, "used") || check_lazy_name(pp->table, id, "retain") ||
                                      check_lazy_name(pp->table, id, "constructor") || check_lazy_name(pp->table, id, "destructor") ||
                                      check_lazy_name(pp->table, id, "gnu_inline");
                flags |= retained ? BUSTER_CHECK_LAZY_RETAIN : 0;
            }
        }
        const uint32_t close = parents[token];
        if (parse_closer_roles[role] != PARSE_ROLE_NONE && close > token && close < end)
        {
            token = close;
        }
    }
    return flags;
}

static uint32_t check_lazy_tree_first(const Parser* parser, uint32_t token)
{
    const uint32_t* const parents = parser->tree.parents;
    uint32_t first = token;
    while (first != 0 && parents[first - 1] != BUSTER_PARSE_ROOT)
    {
        first -= 1;
    }
    return first;
}

// Exactly one unparenthesized direct function declarator. Parenthesized/derived return declarators and K&R
// declaration lists remain the exact adapter's, rather than inferring a name from a nearby parenthesis.
static bool check_lazy_tree_declarator(const Parser* parser, uint32_t first, uint32_t end, uint32_t* declaration)
{
    const uint8_t* const roles = parser->tree.roles;
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint32_t* const parents = parser->tree.parents;
    uint32_t name = BUSTER_PARSE_NONE;
    bool valid = first < end;
    for (uint32_t token = first; valid && token < end; token += 1)
    {
        const uint8_t role = roles[token];
        if (role == PARSE_ROLE_OPERAND)
        {
            valid = name == BUSTER_PARSE_NONE && kinds[token] == PARSE_KIND_IDENTIFIER && token + 1 < end &&
                    roles[token + 1] == PARSE_ROLE_OPEN_FUNCTION && parser->tree.depths[token] == 0;
            name = valid ? token : name;
        }
        else if (role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_TERMINATOR ||
                 (role == PARSE_ROLE_SPECIFIER && (name != BUSTER_PARSE_NONE || kinds[token] == PARSE_KIND_TYPEDEF)))
        {
            valid = false;
        }
        const uint32_t close = parents[token];
        if (parse_closer_roles[role] != PARSE_ROLE_NONE)
        {
            valid &= close > token && close < end;
            token = valid ? close : token;
        }
    }
    *declaration = valid ? name : BUSTER_PARSE_NONE;
    return valid && name != BUSTER_PARSE_NONE;
}

// The completion point of a direct function declarator, including its trailing attributes. Only a file-scope
// function declaration is admitted: a block declaration could hide an object and needs real scope receipts.
static bool check_lazy_tree_function(const Parser* parser, uint32_t token, uint32_t* first, uint32_t* point)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const parents = parser->tree.parents;
    bool valid = token + 1 < count && parser->tree.depths[token] == 0 && roles[token] == PARSE_ROLE_OPERAND && roles[token + 1] == PARSE_ROLE_OPEN_FUNCTION;
    uint32_t end = valid ? parents[token + 1] : count;
    valid &= end > token + 1 && end < count;
    end += valid ? 1 : 0;
    while (valid && end < count && (parser->tokens.kinds[end] == PARSE_KIND_ATTRIBUTE || gnu_syntax_attribute_role(roles[end])))
    {
        const bool keyword = parser->tokens.kinds[end] == PARSE_KIND_ATTRIBUTE;
        end += keyword ? 1 : 0;
        valid = end < count && gnu_syntax_attribute_role(roles[end]) && parents[end] > end && parents[end] < count;
        end = valid ? parents[end] + 1 : end;
    }
    valid = valid && end < count && (roles[end] == PARSE_ROLE_TERMINATOR || roles[end] == PARSE_ROLE_COMMA || roles[end] == PARSE_ROLE_OPEN_BODY);
    uint32_t declared = BUSTER_PARSE_NONE;
    const uint32_t begin = valid ? check_lazy_tree_first(parser, token) : token;
    valid = valid && check_lazy_tree_declarator(parser, begin, end, &declared) && declared == token;
    *first = begin;
    *point = end;
    return valid;
}

// Certify expression context from the final tree, without guessing whether an operand is a declaration name.
// Initializer RHSs and array bounds are expressions even below a declaration's specifiers. A declaration reached
// before any such context is unsupported, except a record member or a local label, whose namespace is separate.
// The bounded walk makes even an adversarially deep expression an explicit fallback rather than quadratic work.
static CheckLazyTreeUse check_lazy_tree_use(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* const roles = parser->tree.roles;
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint32_t* const parents = parser->tree.parents;
    CheckLazyTreeUse result = CHECK_LAZY_TREE_UNKNOWN;
    const uint8_t own = roles[token];
    bool done = own == PARSE_ROLE_MEMBER || own == PARSE_ROLE_TAG || own == PARSE_ROLE_LEAF_RIGHT ||
                (token + 1 < count && roles[token + 1] == PARSE_ROLE_LABEL) ||
                (token != 0 && (kinds[token - 1] == PARSE_KIND_GOTO || (kinds[token - 1] == PARSE_KIND_AMPAMP && roles[token - 1] == PARSE_ROLE_PREFIX)));
    result = done ? CHECK_LAZY_TREE_IGNORED : result;
    bool expression = false;
    bool declaration = false;
    bool label = false;
    uint32_t child = token;
    uint32_t at = parents[token];
    for (uint32_t steps = 0; !done && own == PARSE_ROLE_OPERAND && at < count && steps < BUSTER_CHECK_LAZY_TREE_PARENTS; steps += 1)
    {
        const uint8_t role = roles[at];
        const uint32_t parent = parents[at];
        if (role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_SPECIFIER || (role == PARSE_ROLE_CLOSE_FUNCTION && !expression) || gnu_syntax_attribute_role(role) ||
            (role >= PARSE_ROLE_CLOSE_ATTRIBUTE && role <= PARSE_ROLE_CLOSE_ATTRIBUTE_LIST))
        {
            done = true;
        }
        else if (role == PARSE_ROLE_CLOSE_ENUM && token < at)
        {
            result = expression ? CHECK_LAZY_TREE_USE : CHECK_LAZY_TREE_UNKNOWN;
            done = true;
        }
        else if (role == PARSE_ROLE_CLOSE_STRUCT && token < at)
        {
            result = expression ? CHECK_LAZY_TREE_USE : declaration ? CHECK_LAZY_TREE_IGNORED : CHECK_LAZY_TREE_UNKNOWN;
            done = true;
        }
        else if (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK ||
                 role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
        {
            const bool member = declaration && parent < count && roles[parent] == PARSE_ROLE_CLOSE_STRUCT;
            const bool file_name = !expression && !declaration && parser->tree.depths[token] == 0;
            result = label || member ? CHECK_LAZY_TREE_IGNORED : expression || (!declaration && !file_name) ? CHECK_LAZY_TREE_USE : CHECK_LAZY_TREE_UNKNOWN;
            done = true;
        }
        else
        {
            expression |= !declaration && ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_LOGICAL_OR) || role == PARSE_ROLE_RETURN ||
                          role == PARSE_ROLE_POSTFIX || role == PARSE_ROLE_MEMBER_OPERATOR || role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_RANGE ||
                          (role == PARSE_ROLE_PREFIX && kinds[at] != PARSE_KIND_STAR) ||
                          ((role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_BITFIELD || role == PARSE_ROLE_CLOSE_CAST) && token > at) ||
                          (role == PARSE_ROLE_CLOSE_SUBSCRIPT && parser->tree.depths[child] > parser->tree.depths[at]) || role == PARSE_ROLE_CLOSE_SIZEOF);
            declaration |= !expression && (role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_LEAF_RIGHT ||
                                           role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_FUNCTION);
            label |= role == PARSE_ROLE_SPECIFIER && kinds[at] == PARSE_KIND_LABEL;
            child = at;
            at = parent;
        }
    }
    return result;
}

static void check_lazy_tree_special_names(const Preprocessor* pp, uint32_t* targets, uint32_t count)
{
    static const char* const opaque[] = { "alias", "__alias__", "ifunc", "__ifunc__", "cleanup", "__cleanup__" };
    static const char* const retained[] = { "used", "__used__", "retain", "__retain__" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(opaque); i += 1)
    {
        const IdentId id = intern_find(pp->table, (const uint8_t*)opaque[i], (uint32_t)strlen(opaque[i]));
        if (id != 0 && id < count)
        {
            targets[id] |= BUSTER_CHECK_LAZY_TREE_OPAQUE;
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(retained); i += 1)
    {
        const IdentId id = intern_find(pp->table, (const uint8_t*)retained[i], (uint32_t)strlen(retained[i]));
        if (id != 0 && id < count)
        {
            targets[id] |= BUSTER_CHECK_LAZY_TREE_KEEP;
        }
    }
}

static bool check_lazy_tree_eligible(uint32_t flags)
{
    return (flags & BUSTER_CHECK_LAZY_SYSTEM) != 0 && (flags & (BUSTER_CHECK_LAZY_STATIC | BUSTER_CHECK_LAZY_INLINE)) != 0 &&
           (flags & (BUSTER_CHECK_LAZY_RETAIN | BUSTER_CHECK_LAZY_OPAQUE)) == 0;
}

static CheckLazyStatus check_lazy_tree_plan(CheckLazy* result, const Parser* parser, const Preprocessor* pp, Arena* output, Arena* scratch, bool scalar)
{
    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    CheckLazy candidate = { .status = CHECK_LAZY_INVALID };
    bool valid = output != scratch && parser != NULL && pp != NULL && parser->fatal == PARSE_MESSAGE_NONE && parser->errors == 0 &&
                 parser->tree.roles != NULL && parser->tree.parents != NULL && parser->tree.depths != NULL && parser->tokens.sources != NULL && pp->table != NULL;
    const uint32_t count = valid ? parser->tokens.count : 0;
    const uint32_t names = valid ? parser->meanings.capacity : 0;
    valid &= names != 0;
    uint32_t body_count = 0;
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        body_count += parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY;
    }
    valid &= body_count < BUSTER_CHECK_LAZY_TREE_BODY;
    candidate.status = valid ? parser->diagnostic_count != 0 ? CHECK_LAZY_UNSUPPORTED : CHECK_LAZY_RESOURCE : CHECK_LAZY_INVALID;
    valid = valid && parser->diagnostic_count == 0;
    candidate.bodies = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckLazyBody, body_count) : NULL;
    candidate.deferred_tokens = valid && (body_count == 0 || candidate.bodies != NULL) ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, (uint64_t)count + 1) : NULL;
    valid &= candidate.deferred_tokens != NULL;
    const uint64_t retained = output->length;
    CBodyRange* ranges = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CBodyRange, body_count) : NULL;
    uint32_t* targets = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, names) : NULL;
    uint32_t* visible = targets != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body_count + 1) : NULL;
    valid &= (body_count == 0 || ranges != NULL) && visible != NULL;
    uint32_t found = 0;
    valid = valid && c_environment_bodies(&parser->tree, count, ranges, body_count, &found) && found == body_count;
    if (valid)
    {
        memset(candidate.deferred_tokens, 0, (uint64_t)count + 1);
        memset(targets, 0, (uint64_t)names * sizeof(uint32_t));
        for (uint32_t body = 0; valid && body < body_count; body += 1)
        {
            const uint32_t begin = ranges[body].begin;
            const uint32_t first = check_lazy_tree_first(parser, begin);
            uint32_t declaration = BUSTER_PARSE_NONE;
            valid = check_lazy_tree_declarator(parser, first, begin, &declaration);
            candidate.status = valid ? CHECK_LAZY_RESOURCE : CHECK_LAZY_UNSUPPORTED;
            if (valid)
            {
                const uint32_t name = parser->tokens.names[declaration];
                valid = name != 0 && name < names && targets[name] == 0;
                candidate.status = valid ? CHECK_LAZY_RESOURCE : CHECK_LAZY_UNSUPPORTED;
                if (valid)
                {
                    uint32_t flags = check_lazy_tree_header(parser, pp, first, begin);
                    const bool system = check_lazy_system(pp, parser->tokens.sources[declaration]) && check_lazy_system(pp, parser->tokens.sources[begin]);
                    flags |= system ? BUSTER_CHECK_LAZY_SYSTEM : 0;
                    flags |= (flags & BUSTER_CHECK_LAZY_STATIC) == 0 && ((flags & BUSTER_CHECK_LAZY_INLINE) == 0 || (flags & BUSTER_CHECK_LAZY_EXTERN) != 0) ?
                             BUSTER_CHECK_LAZY_RETAIN : 0;
                    candidate.bodies[body] = (CheckLazyBody)
                    {
                        .begin = begin,
                        .end = ranges[body].end,
                        .declaration = declaration,
                        .flags = flags,
                        .selected = true,
                    };
                    targets[name] = body + 1;
                    visible[body] = begin;
                }
            }
        }
    }
    bool opaque = pp != NULL && pp->options.no_history;
    uint32_t occurrences = 0;
    if (valid)
    {
        check_lazy_tree_special_names(pp, targets, names);
        uint32_t body = 0;
        for (uint64_t block = 0; block < count; block += 64)
        {
            const uint32_t live = count - block < 64 ? (uint32_t)(count - block) : 64;
            uint64_t mask = check_lazy_tree_mask(parser, targets, names, (uint32_t)block, live, BUSTER_SCALAR_PATH(scalar));
            while (mask != 0)
            {
                const uint32_t token = (uint32_t)block + (uint32_t)__builtin_ctzll(mask);
                mask &= mask - 1;
                const uint32_t name = parser->tokens.names[token];
                const uint32_t entry = name < names ? targets[name] : 0;
                opaque |= parser->tokens.kinds[token] == PARSE_KIND_ASM || (entry & BUSTER_CHECK_LAZY_TREE_OPAQUE) != 0;
                occurrences += (entry & BUSTER_CHECK_LAZY_TREE_BODY) != 0;
                while (body < body_count && token >= ranges[body].end)
                {
                    body += 1;
                }
                if (body < body_count && token >= ranges[body].begin && (entry & BUSTER_CHECK_LAZY_TREE_KEEP) != 0)
                {
                    candidate.bodies[body].flags |= BUSTER_CHECK_LAZY_RETAIN;
                }
            }
        }
        bool eligible = false;
        for (uint32_t body = 0; body < body_count; body += 1)
        {
            candidate.bodies[body].flags |= opaque ? BUSTER_CHECK_LAZY_OPAQUE : 0;
            eligible |= check_lazy_tree_eligible(candidate.bodies[body].flags);
        }
        candidate.tree_all_selected = !eligible;
    }
    uint32_t references = 0;
    if (valid && !candidate.tree_all_selected)
    {
        uint32_t* sources = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)occurrences + 1);
        uint32_t* destinations = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)occurrences + 1);
        uint32_t* kinds = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)occurrences + 1);
        uint32_t* flags = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body_count + 1);
        valid = sources != NULL && destinations != NULL && kinds != NULL && flags != NULL;
        uint32_t body = 0;
        uint32_t attribute_end = 0;
        for (uint64_t block = 0; valid && block < count; block += 64)
        {
            const uint32_t live = count - block < 64 ? (uint32_t)(count - block) : 64;
            uint64_t mask = check_lazy_tree_mask(parser, targets, names, (uint32_t)block, live, BUSTER_SCALAR_PATH(scalar));
            while (valid && mask != 0)
            {
                const uint32_t token = (uint32_t)block + (uint32_t)__builtin_ctzll(mask);
                mask &= mask - 1;
                while (body < body_count && token >= ranges[body].end)
                {
                    body += 1;
                }
                const uint32_t owner = body < body_count && token >= ranges[body].begin ? body + 1 : 0;
                const uint32_t name = parser->tokens.names[token];
                const uint32_t target = name < names ? targets[name] & BUSTER_CHECK_LAZY_TREE_BODY : 0;
                if (gnu_syntax_attribute_role(parser->tree.roles[token]))
                {
                    const uint32_t close = parser->tree.parents[token];
                    attribute_end = close > attribute_end ? close : attribute_end;
                }
                if (target != 0 && token < attribute_end)
                {
                    // Attribute spelling is normally outside ordinary lookup, but nested grammar can still declare
                    // a name. Do not hide such a shadow from the certificate by skipping an entire attribute group.
                    valid = false;
                    candidate.status = CHECK_LAZY_UNSUPPORTED;
                }
                else if (target != 0)
                {
                    uint32_t first = 0;
                    uint32_t point = 0;
                    if (owner == 0 && check_lazy_tree_function(parser, token, &first, &point))
                    {
                        const uint32_t declared_flags = check_lazy_tree_header(parser, pp, first, point);
                        CheckLazyBody* row = candidate.bodies + target - 1;
                        row->flags |= declared_flags & BUSTER_CHECK_LAZY_RETAIN;
                        if ((row->flags & BUSTER_CHECK_LAZY_STATIC) == 0 && ((declared_flags & BUSTER_CHECK_LAZY_INLINE) == 0 || (declared_flags & BUSTER_CHECK_LAZY_EXTERN) != 0))
                        {
                            row->flags |= BUSTER_CHECK_LAZY_RETAIN;
                        }
                        visible[target - 1] = point < visible[target - 1] ? point : visible[target - 1];
                    }
                    else
                    {
                        const CheckLazyTreeUse use = check_lazy_tree_use(parser, token);
                        valid = use != CHECK_LAZY_TREE_UNKNOWN;
                        candidate.status = valid ? CHECK_LAZY_RESOURCE : CHECK_LAZY_UNSUPPORTED;
                        if (valid && use == CHECK_LAZY_TREE_USE && visible[target - 1] <= token)
                        {
                            if (owner == 0)
                            {
                                candidate.bodies[target - 1].flags |= BUSTER_CHECK_LAZY_RETAIN;
                            }
                            else
                            {
                                sources[references] = owner - 1;
                                destinations[references] = target - 1;
                                kinds[references++] = C_LOWERING_REFERENCE_EVALUATED;
                            }
                        }
                    }
                }
            }
        }
        for (uint32_t body = 0; valid && body < body_count; body += 1)
        {
            flags[body] = BUSTER_C_LOWERING_FUNCTION | (check_lazy_tree_eligible(candidate.bodies[body].flags) ? 0 : BUSTER_C_LOWERING_USED);
        }
        CLowering plan = { 0 };
        const CLoweringInput graph =
        {
            .definition_count = body_count,
            .reference_count = references,
            .definition_flags = flags,
            .reference_source = sources,
            .reference_target = destinations,
            .reference_kind = kinds,
        };
        valid = valid && c_lowering_prepare(scratch, graph, &plan) == C_LOWERING_OK;
        valid = valid && (BUSTER_SCALAR_PATH(scalar) ? c_lowering_scalar(&plan, C_LOWERING_REACHABLE) : c_lowering_vector(&plan, C_LOWERING_REACHABLE)) == C_LOWERING_OK;
        if (valid)
        {
            for (uint32_t body = 0; body < body_count; body += 1)
            {
                CheckLazyBody* row = candidate.bodies + body;
                row->selected = (plan.reached[body / 64] & (UINT64_C(1) << (body % 64))) != 0;
                if (!row->selected)
                {
                    memset(candidate.deferred_tokens + row->begin, 1, row->end - row->begin);
                }
            }
            candidate.selected_count = plan.selected_count;
            candidate.reachability = plan.statistics;
        }
    }
    if (valid)
    {
        candidate.token_count = count;
        candidate.body_count = body_count;
        candidate.selected_count = candidate.tree_all_selected ? body_count : candidate.selected_count;
        candidate.deferred_count = body_count - candidate.selected_count;
        candidate.reference_count = references;
        candidate.tree_tokens = count;
        candidate.retained_bytes = retained - mark;
        candidate.status = check_lazy_mask_valid(parser, candidate.deferred_tokens) ? CHECK_LAZY_OK : CHECK_LAZY_INVALID;
        valid = candidate.status == CHECK_LAZY_OK;
    }
    arena_pop_to(output, valid ? retained : mark);
    arena_pop_to(scratch, scratch_mark);
    *result = valid ? candidate : (CheckLazy){ .status = candidate.status };
    return result->status;
}

#endif
