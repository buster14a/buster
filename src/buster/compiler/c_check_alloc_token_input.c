// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __builtin_infer_alloc_token's token (#148): clang 23's allocation type inference (clang/lib/AST/InferAlloc.cpp's
// inferPossibleType and getAllocTokenMetadata), the type's name as clang prints a canonical type without tag keywords,
// and LLVM's stable SipHash of that name (llvm/lib/Support/AllocToken.cpp's getAllocToken) in the default mode,
// TypeHashPointerSplit, over 64-bit tokens. Include after c_check_packed_input.c.

// Half of the default token space, UINT64_MAX tokens (the AllocToken pass's MaxTokens of 0, an IntPtrTy's mask): a type
// without a pointer takes a token below it, one with a pointer above.
#define BUSTER_CHECK_ALLOC_TOKEN_HALF (UINT64_MAX / 2)

// The most records a pointer search visits, and the most nodes of a type's structure the unnamed-tag search keeps.
#define BUSTER_CHECK_ALLOC_TOKEN_STACK 64u

static void check_input_siphash_round(uint64_t* v)
{
    v[0] += v[1];
    v[1] = (v[1] << 13) | (v[1] >> 51);
    v[1] ^= v[0];
    v[0] = (v[0] << 32) | (v[0] >> 32);
    v[2] += v[3];
    v[3] = (v[3] << 16) | (v[3] >> 48);
    v[3] ^= v[2];
    v[0] += v[3];
    v[3] = (v[3] << 21) | (v[3] >> 43);
    v[3] ^= v[0];
    v[2] += v[1];
    v[1] = (v[1] << 17) | (v[1] >> 47);
    v[1] ^= v[2];
    v[2] = (v[2] << 32) | (v[2] >> 32);
}

// LLVM's getStableSipHash: SipHash-2-4 of `length` bytes under its fixed key (llvm/lib/Support/SipHash.cpp), the 64-bit
// result read little-endian. A type name is a few bytes, hashed once per call: scalar.
static uint64_t check_input_siphash(const uint8_t* bytes, uint64_t length)
{
    static const uint8_t key[16] = { 0xb5, 0xd4, 0xc9, 0xeb, 0x79, 0x10, 0x4a, 0x79, 0x6f, 0xec, 0x8b, 0x1b, 0x42, 0x87, 0x81, 0xd4 };
    uint64_t k0 = 0;
    uint64_t k1 = 0;
    memcpy(&k0, key, 8);
    memcpy(&k1, key + 8, 8);
    uint64_t v[4] = { k0 ^ UINT64_C(0x736f6d6570736575), k1 ^ UINT64_C(0x646f72616e646f6d), k0 ^ UINT64_C(0x6c7967656e657261),
                      k1 ^ UINT64_C(0x7465646279746573) };
    const uint64_t end = length - length % 8;
    for (uint64_t i = 0; i <= end; i += 8)
    {
        uint64_t m = length << 56;
        if (i < end)
        {
            memcpy(&m, bytes + i, 8);
        }
        for (uint64_t j = 0; i == end && j < length % 8; j += 1)
        {
            m |= (uint64_t)bytes[end + j] << (8 * j);
        }
        v[3] ^= m;
        check_input_siphash_round(v);
        check_input_siphash_round(v);
        v[0] ^= m;
    }
    v[2] ^= 0xff;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        check_input_siphash_round(v);
    }
    return v[0] ^ v[1] ^ v[2] ^ v[3];
}

// Whether a token is a node clang's arithmetic inference looks through (inferPossibleTypeFromArithSizeofExpr, past
// IgnoreParenImpCasts): a grouping's `)`, or a binary +, -, *, /, << or >>.
static bool check_input_alloc_through(const Parser* parser, uint32_t token)
{
    const uint8_t role = parser->tree.roles[token];
    const uint8_t kind = parser->tokens.kinds[token];
    const bool binary = (role == PARSE_ROLE_MULTIPLICATIVE && (kind == PARSE_KIND_STAR || kind == PARSE_KIND_SLASH)) || role == PARSE_ROLE_ADDITIVE ||
                        role == PARSE_ROLE_SHIFT;
    return role == PARSE_ROLE_CLOSE_GROUP || binary;
}

// Where a constant evaluation reads the call, its own per-token type-name queries (CheckConstantInput.queries, from
// `first` to `end`), which hold a sizeof's type name that the walk hasn't published yet (an enumerator's, read in source
// order); none for the walk's calls.
typedef struct CheckInputAllocQueries CheckInputAllocQueries;
struct CheckInputAllocQueries
{
    const TypeId* queries;
    uint32_t first;
    uint32_t end;
};

// The type of sizeof `token`'s operand, as the reader has published it, never evaluated here (the evidence runs inside
// the constant evaluator's calls): a type name's query (the reader's column, or the evaluation's own), the operand's
// type that check_input_sizeof_prepare publishes, or, before that pass, a lone variable's declared type. 0 for none.
static TypeId check_input_alloc_sizeof(CheckInputReader* reader, uint32_t token, const CheckInputAllocQueries* queries)
{
    const Parser* parser = reader->parser;
    const uint32_t close = reader->ends[token];
    const bool named = close < parser->tokens.count && close > token + 2 && parser->tree.roles[close] == PARSE_ROLE_CLOSE_SIZEOF &&
                       parser->tree.parents[close] == token;
    TypeId result = reader->query_types[token] != 0 ? reader->query_types[token] : reader->sizeof_types[token];
    if (result == 0 && named)
    {
        result = reader->query_types[close];
    }
    const bool local = queries->queries != NULL && token >= queries->first && close < queries->end;
    if (result == 0 && local)
    {
        result = queries->queries[token - queries->first] != 0 ? queries->queries[token - queries->first] : queries->queries[close - queries->first];
    }
    uint32_t begin = token + 1;
    uint32_t last = close + 1;
    while (result == 0 && !named && last > begin + 1 && parser->tokens.kinds[begin] == PARSE_KIND_L_PAREN && parser->tree.parents[begin] == last - 1)
    {
        begin += 1;
        last -= 1;
    }
    if (result == 0 && !named && last == begin + 1 && parser->tokens.kinds[begin] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = reader->output->declarations[begin];
        result = declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_OBJECT ? reader->declared_types[declaration] : 0;
    }
    return result;
}

// An expression's inferred type, from `first` to `end` (inferPossibleTypeFromArithSizeofExpr): the first sizeof (not
// _Alignof) in source order that nothing but groupings and those binary operators hold, which is the leftmost one that
// clang's walk, its left operand first, reaches; its operand's type, or 0 for none. `sizeof` is the token the type
// comes from.
static TypeId check_input_alloc_arithmetic(CheckInputReader* reader, uint32_t first, uint32_t end, const CheckInputAllocQueries* queries,
                                           uint32_t* sizeof_token)
{
    const Parser* parser = reader->parser;
    TypeId result = 0;
    for (uint32_t token = first; result == 0 && token < end; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_SIZEOF && parser->tokens.names[token] == C_NAME_SIZEOF && parser->tree.roles[token] == PARSE_ROLE_PREFIX)
        {
            bool held = true;
            uint32_t parent = parser->tree.parents[token];
            while (held && parent >= first && parent < end)
            {
                held = check_input_alloc_through(parser, parent);
                parent = parser->tree.parents[parent];
            }
            if (held)
            {
                result = check_input_alloc_sizeof(reader, token, queries);
                *sizeof_token = token;
            }
        }
    }
    return result;
}

// An argument's inferred type (inferPossibleType's for one argument): its arithmetic's, or, for a variable's name alone
// (past groupings), its initializer's arithmetic's (inferPossibleTypeFromVarInitSizeofExpr).
static TypeId check_input_alloc_argument(CheckInputReader* reader, uint32_t first, uint32_t end, const CheckInputAllocQueries* queries,
                                         uint32_t* sizeof_token)
{
    const Parser* parser = reader->parser;
    TypeId result = check_input_alloc_arithmetic(reader, first, end, queries, sizeof_token);
    uint32_t begin = first;
    uint32_t last = end;
    while (result == 0 && last > begin + 1 && parser->tokens.kinds[begin] == PARSE_KIND_L_PAREN && parser->tree.parents[begin] == last - 1 &&
           parser->tree.roles[last - 1] == PARSE_ROLE_CLOSE_GROUP)
    {
        begin += 1;
        last -= 1;
    }
    if (result == 0 && last == begin + 1 && parser->tokens.kinds[begin] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = reader->output->declarations[begin];
        const bool variable = declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_OBJECT;
        const uint32_t initializer = variable ? check_sparse_get(reader->output->initializer_begins, declaration) : BUSTER_CHECK_NONE;
        const uint32_t initializer_end = variable ? check_sparse_get(reader->output->initializer_ends, declaration) : BUSTER_CHECK_NONE;
        if (initializer < initializer_end && initializer_end <= parser->tokens.count)
        {
            result = check_input_alloc_arithmetic(reader, initializer, initializer_end, queries, sizeof_token);
        }
    }
    return result;
}

// Whether a typedef chain from the declaration `declaration` names uintptr_t, which clang takes for a pointer
// (typeContainsPointer's special case): the declaration's own typedef-name specifier, then that typedef's, a few deep.
static bool check_input_alloc_uintptr(const CheckInputReader* reader, uint32_t declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t name = intern_find(reader->pp->table, (const uint8_t*)"uintptr_t", 9);
    bool result = false;
    uint32_t current = declaration;
    for (uint32_t depth = 0; !result && name != 0 && depth < 16 && current != 0 && current < parser->tokens.count; depth += 1)
    {
        const bool typedef_name = reader->declaration_kinds[current] == C_BINDING_TYPEDEF;
        result = typedef_name && parser->tokens.names[current] == name;
        current = check_input_typedef_spelling(reader->output, parser, current);
    }
    return result;
}

// Whether a sizeof's operand names uintptr_t through typedefs: a type name's leading typedef name, or a variable's
// declaration's, as clang's typedef chain walk reads the inferred type's sugar.
static bool check_input_alloc_spelled_uintptr(const CheckInputReader* reader, uint32_t sizeof_token)
{
    const Parser* parser = reader->parser;
    const uint32_t end = reader->ends[sizeof_token] + 1;
    bool result = false;
    for (uint32_t token = sizeof_token + 1; !result && token < end && token < parser->tokens.count; token += 1)
    {
        const uint32_t declaration = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[token] : 0;
        const bool named = declaration != 0 && declaration < parser->tokens.count;
        const uint8_t binding = named ? reader->declaration_kinds[declaration] : 0;
        result = (binding == C_BINDING_TYPEDEF && check_input_alloc_uintptr(reader, declaration)) ||
                 (binding == C_BINDING_OBJECT && check_input_alloc_uintptr(reader, check_input_typedef_spelling(reader->output, parser, declaration)));
        token = named || parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? end : token;
    }
    return result;
}

// Whether a record's definition declares a member of uintptr_t's typedef chain (typeContainsPointer reads each field's
// sugar, which the type table doesn't keep): a typedef name at its body's own level, outside parentheses, brackets and
// nested braces, its body found from the tag's completion (its `}`) back to its `{`.
static bool check_input_alloc_uintptr_member(const CheckInputReader* reader, uint32_t tag)
{
    const Parser* parser = reader->parser;
    const uint32_t completion = reader->tag_completions[tag];
    const uint32_t close = completion != BUSTER_CHECK_NONE && completion > 0 && completion <= parser->tokens.count ? completion - 1 : 0;
    uint32_t open = close;
    uint32_t depth = 0;
    bool searching = close != 0 && parser->tokens.kinds[close] == PARSE_KIND_R_BRACE;
    while (searching && open > 0)
    {
        open -= 1;
        const uint8_t kind = parser->tokens.kinds[open];
        depth = kind == PARSE_KIND_R_BRACE ? depth + 1 : kind == PARSE_KIND_L_BRACE && depth != 0 ? depth - 1 : depth;
        searching = !(kind == PARSE_KIND_L_BRACE && depth == 0 && parser->tree.parents[open] == close);
    }
    bool result = false;
    depth = 0;
    for (uint32_t token = open + 1; !searching && !result && token < close; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        const uint32_t declaration = depth == 0 && kind == PARSE_KIND_IDENTIFIER ? reader->output->declarations[token] : 0;
        result = declaration != 0 && declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF &&
                 check_input_alloc_uintptr(reader, declaration);
        depth = opener ? depth + 1 : closer && depth != 0 ? depth - 1 : depth;
    }
    return result;
}

// typeContainsPointer: a pointer, or an array, atomic, struct or union that holds one (a uintptr_t member too), each
// record visited once; and whether a record on the way is incomplete, which with no pointer gives no token.
static bool check_input_alloc_pointer(CheckInputReader* reader, TypeId type, bool* incomplete)
{
    const TypeTable* types = reader->types;
    TypeId stack[BUSTER_CHECK_ALLOC_TOKEN_STACK];
    uint32_t visited[BUSTER_CHECK_ALLOC_TOKEN_STACK];
    uint32_t visited_count = 0;
    uint32_t depth = 0;
    bool result = false;
    stack[depth++] = type;
    while (!result && depth != 0)
    {
        const TypeId current = type_unqualified(type_strip_aligned(types, stack[--depth]));
        const uint8_t kind = type_is_valid(types, current) ? type_kind(types, current) : BUSTER_TYPE_KIND_VOID;
        if (kind == BUSTER_TYPE_KIND_POINTER)
        {
            result = true;
        }
        else if ((type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ATOMIC) && depth < BUSTER_CHECK_ALLOC_TOKEN_STACK)
        {
            stack[depth++] = type_element(types, current);
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = types->types.index[type_index(current)];
            bool seen = false;
            for (uint32_t i = 0; i < visited_count; i += 1)
            {
                seen |= visited[i] == tag;
            }
            if (!type_is_complete(types, current))
            {
                *incomplete = true;
            }
            else if (!seen && visited_count < BUSTER_CHECK_ALLOC_TOKEN_STACK)
            {
                visited[visited_count++] = tag;
                result = check_input_alloc_uintptr_member(reader, tag);
                const uint32_t begin = types->tags.member_begin[tag];
                const uint32_t count = types->tags.member_count[tag];
                // In reverse, so that the first member is searched first.
                for (uint32_t member = begin + count; member > begin && depth < BUSTER_CHECK_ALLOC_TOKEN_STACK; member -= 1)
                {
                    stack[depth++] = types->members.type[member - 1];
                }
            }
        }
    }
    return result;
}

// An unnamed tag's name as clang prints it without its keyword (#148): its first typedef's name; else its record
// contexts (check_input_packed_names: a named one's name, an unnamed one's "(unnamed struct)" or an anonymous member's
// "(anonymous struct)") each with `::`, then `(unnamed struct at FILE:LINE:COLUMN)`, `anonymous` for an anonymous member.
// Its place is the packed warnings' (check_input_packed_name). A deferred body's lane can't read the preprocessor's
// places, so it refuses the body (check_input_lane_refuse), which source order then reads.
static String8 check_input_alloc_tag_name(CheckInputReader* reader, uint32_t tag, CheckInputPackedNames* names)
{
    const TypeTable* types = reader->types;
    const Parser* parser = reader->parser;
    check_input_lane_refuse(reader, reader->lane, "a body infers an allocation token of an unnamed type");
    String8 result = { 0 };
    if (!reader->lane && check_input_packed_names(reader, names))
    {
        result = check_input_packed_identifier(reader, names->typedefs[tag]);
        const uint32_t token = names->tokens[tag];
        if (result.length == 0 && token < parser->tokens.count)
        {
            uint32_t* contexts = names->stack;
            uint32_t depth = 0;
            uint64_t capacity = 128;
            for (uint32_t parent = names->parents[tag]; parent != BUSTER_CHECK_NONE && depth < types->tags.count; parent = names->parents[parent])
            {
                contexts[depth++] = parent;
                const uint32_t name = types->tags.name[parent] != 0 ? types->tags.name[parent] : names->typedefs[parent];
                capacity += check_input_packed_identifier(reader, name).length + 32;
            }
            const uint32_t source = parser->tokens.sources[token];
            pp_history_ensure(reader->pp);
            const uint32_t location = pp_expansion_location(reader->pp, source, pp_history_at(reader->pp, source));
            const char* file = "";
            uint32_t file_length = 0;
            const uint32_t line = pp_presumed(reader->pp, location, &file, &file_length);
            uint32_t column = 0;
            const uint32_t range = pp_find_range(reader->pp, location);
            if (range != BUSTER_PP_NONE)
            {
                pp_line_of(reader->pp, range, location, &column);
            }
            capacity += file_length;
            char* text = arena_push(reader->arena, capacity, 1);
            reader->output->resource |= text == NULL;
            if (text != NULL)
            {
                uint64_t length = 0;
                for (uint32_t i = depth; i > 0; i -= 1)
                {
                    const uint32_t context = contexts[i - 1];
                    const uint32_t context_name = types->tags.name[context] != 0 ? types->tags.name[context] : names->typedefs[context];
                    const String8 name = check_input_packed_identifier(reader, context_name);
                    const bool anonymous = i < depth && check_input_packed_anonymous(types, context, contexts[i]);
                    const char* context_kind = type_kind(types, types->tags.type[context]) == BUSTER_TYPE_KIND_UNION ? "union" : "struct";
                    length += (uint64_t)(name.length != 0 ? snprintf(text + length, capacity - length, "%.*s::", (int)name.length, name.pointer) :
                        snprintf(text + length, capacity - length, "(%s %s)::", anonymous ? "anonymous" : "unnamed", context_kind));
                }
                const bool anonymous = depth != 0 && check_input_packed_anonymous(types, tag, contexts[0]);
                const char* kind = type_kind(types, types->tags.type[tag]) == BUSTER_TYPE_KIND_UNION ? "union" : "struct";
                length += (uint64_t)snprintf(text + length, capacity - length, "(%s %s at %.*s:%u:%u)", anonymous ? "anonymous" : "unnamed", kind,
                                             (int)file_length, file, line, column);
                result = (String8){ .pointer = text, .length = length };
            }
        }
    }
    return result;
}

// The rows a type's name prints its tags by (TypeNames): every tag's identifier, by name, and the unnamed records the
// type's structure reaches (through pointers, arrays, functions' results and parameters, atomic, complex and vector
// types), by tag (check_input_alloc_tag_name), for the printer, which reads no record's members. False for one it can't
// name (an unnamed enumeration, or a body a lane refused).
static bool check_input_alloc_names(CheckInputReader* reader, TypeId type, TypeNames* names)
{
    const TypeTable* types = reader->types;
    const uint32_t name_count = reader->pp->table->count;
    const uint32_t tag_count = types->tags.count;
    const uint8_t** bytes = arena_push_array(reader->scratch, name_count, sizeof(uint8_t*), 8);
    uint32_t* lengths = arena_push_array(reader->scratch, name_count, sizeof(uint32_t), 4);
    String8* anonymous = arena_push_array(reader->scratch, tag_count + 1, sizeof(String8), 8);
    bool valid = bytes != NULL && lengths != NULL && anonymous != NULL;
    reader->output->resource |= !valid;
    if (valid)
    {
        for (uint32_t tag = 0; tag < tag_count; tag += 1)
        {
            const uint32_t name = types->tags.name[tag];
            anonymous[tag] = (String8){ 0 };
            if (name != 0 && name < name_count)
            {
                bytes[name] = intern_bytes(reader->pp->table, name);
                lengths[name] = intern_length(reader->pp->table, name);
            }
        }
        CheckInputPackedNames tags = { 0 };
        TypeId stack[BUSTER_CHECK_ALLOC_TOKEN_STACK];
        uint32_t depth = 0;
        stack[depth++] = type;
        while (valid && depth != 0)
        {
            const TypeId current = type_unqualified(type_strip_aligned(types, stack[--depth]));
            const uint8_t kind = type_is_valid(types, current) ? type_kind(types, current) : BUSTER_TYPE_KIND_VOID;
            const uint32_t tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM ?
                                 types->types.index[type_index(current)] : BUSTER_CHECK_NONE;
            if (tag < tag_count && types->tags.name[tag] == 0 && anonymous[tag].length == 0)
            {
                valid = kind != BUSTER_TYPE_KIND_ENUM;
                anonymous[tag] = valid ? check_input_alloc_tag_name(reader, tag, &tags) : (String8){ 0 };
                valid &= anonymous[tag].length != 0;
            }
            else if (kind == BUSTER_TYPE_KIND_FUNCTION)
            {
                uint32_t count = 0;
                const TypeId* parameters = type_parameters(types, current, &count);
                valid = depth + count < BUSTER_CHECK_ALLOC_TOKEN_STACK;
                for (uint32_t i = 0; valid && i < count; i += 1)
                {
                    stack[depth++] = parameters[i];
                }
                if (valid)
                {
                    stack[depth++] = type_return(types, current);
                }
            }
            else if (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_COMPLEX ||
                     kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
            {
                valid = depth < BUSTER_CHECK_ALLOC_TOKEN_STACK;
                if (valid)
                {
                    stack[depth++] = type_element(types, current);
                }
            }
        }
        *names = (TypeNames){
            .bytes = bytes,
            .lengths = lengths,
            .count = name_count,
            .anonymous = anonymous,
            .anonymous_count = tag_count,
        };
    }
    return valid;
}

// The call's token (CheckBuiltinCall.alloc_token, #148), its arguments from `first` to the closing parenthesis
// `closing`: the first argument's inferred type (inferPossibleType), its canonical name as clang prints it without tag
// keywords (C23's `bool`), hashed, and in the upper half of the tokens for a type holding a pointer (uintptr_t's
// typedef chain counts, at the sizeof's operand). No inferred type, or an incomplete record without a pointer, gives no
// token: clang's code takes its fallback, 0, and its evaluator no constant.
static __attribute__((noinline)) void check_input_alloc_token_evidence(CheckInputReader* reader, uint32_t first, uint32_t closing, const TypeId* queries,
                                                                       uint32_t query_first, uint32_t query_end, CheckBuiltinCall* evidence)
{
    const CheckInputAllocQueries local = { .queries = queries, .first = query_first, .end = query_end };
    const Parser* parser = reader->parser;
    TypeId type = 0;
    uint32_t sizeof_token = BUSTER_CHECK_NONE;
    for (uint32_t begin = first; type == 0 && begin < closing;)
    {
        const uint32_t end = check_constant_punctuation(parser, begin, closing, PARSE_KIND_COMMA);
        type = check_input_alloc_argument(reader, begin, end, &local, &sizeof_token);
        begin = end + 1;
    }
    bool incomplete = false;
    const bool pointer = type != 0 && (check_input_alloc_pointer(reader, type, &incomplete) || check_input_alloc_spelled_uintptr(reader, sizeof_token));
    const uint64_t mark = reader->scratch->length;
    TypeNames names = { 0 };
    if (type_is_valid(reader->types, type) && (pointer || !incomplete) && check_input_alloc_names(reader, type, &names))
    {
        const uint32_t options = BUSTER_TYPE_PRINT_NO_TAG_KEYWORD | (parser->language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0);
        const String8 printed = type_print(reader->types, reader->scratch, type, (String8){ 0 }, &names, options);
        if (printed.length != 0)
        {
            evidence->alloc_token = check_input_siphash((const uint8_t*)printed.pointer, printed.length) % BUSTER_CHECK_ALLOC_TOKEN_HALF +
                                    (pointer ? BUSTER_CHECK_ALLOC_TOKEN_HALF : 0);
            evidence->alloc_token_known = true;
        }
    }
    arena_pop_to(reader->scratch, mark);
}
