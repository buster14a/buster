// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A discarded call's result (#1447, clang's Sema::DiagnoseUnusedExprResult and DiagnoseNoDiscard): a call whose callee is
// declared warn_unused_result or nodiscard is "ignoring return value of function declared with 'warn_unused_result'
// attribute" (-Wunused-result), one whose return type's struct, union, enum or typedef is is "ignoring return value of type
// 'S' declared with 'nodiscard' attribute" (-Wunused-value), and otherwise one declared pure or const, or one of clang's
// builtins its table marks so, "ignoring return value of function declared with pure attribute" (-Wunused-value), unless the
// call is written in a macro's body. Each is clang's at once, not at the body's end, in unreachable code too. Included by
// c_check_common_warnings.c after its emission's state.
//
// The marks are found once a unit (check_common_ignored_prepare), by the tokens that have attributes: a function's by its
// first declaration (CheckInput.objects), each redeclaration in source order, so that a call reads those up to the
// declaration its lookup finds, as clang's redeclarations inherit their attributes; a typedef's by its name; a tag's by its
// index. A call then reads one bit of its callee's first declaration, and a builtin's name one byte, found by its first
// call, so that a unit whose calls are of unmarked functions pays a test a discarded call.

#define BUSTER_CHECK_IGNORED_CONST 0x01u
#define BUSTER_CHECK_IGNORED_PURE 0x02u
#define BUSTER_CHECK_IGNORED_RESULT 0x04u
// A builtin name's byte (CheckCommonIgnored.builtins): known, its CONST and PURE bits, and whether it is a plain library
// function's (attribute f), which is a builtin only where the unit keeps it one.
#define BUSTER_CHECK_IGNORED_KNOWN 0x80u
#define BUSTER_CHECK_IGNORED_LIBRARY 0x40u
// The builtins' operations a call is (check_common_ignored_of), a bit each from CHECK_OP_BUILTIN_UNARY.
#define BUSTER_CHECK_IGNORED_BUILTIN_BIT(op) (UINT32_C(1) << ((op) - CHECK_OP_BUILTIN_UNARY))
#define BUSTER_CHECK_IGNORED_BUILTIN_OPS                                                                                                     \
    (BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_UNARY) | BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_OVERFLOW) |                  \
     BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_MISC) | BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_FLOAT) |                      \
     BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_QUERY) | BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_ATOMIC) |                    \
     BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_VECTOR) | BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_X86) |                      \
     BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_BUILTIN_BIT_CAST) | BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_REAL) |                           \
     BUSTER_CHECK_IGNORED_BUILTIN_BIT(CHECK_OP_IMAGINARY))
_Static_assert(CHECK_OP_COUNT - CHECK_OP_BUILTIN_UNARY <= 32, "the builtins' operations are bits of a 32-bit mask");

// Whether `op` is a call's: a function's or a builtin's.
static inline bool check_common_ignored_op(uint8_t op)
{
    const uint32_t at = (uint32_t)op - CHECK_OP_BUILTIN_UNARY;
    return op == CHECK_OP_CALL || (op >= CHECK_OP_BUILTIN_UNARY && at < 32 && (BUSTER_CHECK_IGNORED_BUILTIN_OPS >> at & 1) != 0);
}

typedef struct CheckCommonIgnoredMark CheckCommonIgnoredMark;
struct CheckCommonIgnoredMark
{
    // A function's first declaration, or a typedef's name; a tag's index for the tags' marks.
    uint32_t key;
    // The declaration whose attributes these are, and their name for a tag's or a typedef's.
    uint32_t declaration;
    uint8_t kinds;
    // The first warn_unused_result or nodiscard of them.
    const CheckInputAttribute* result;
};

// (Its typedef is c_check_common_warnings.c's, before CheckCommonEmission.)
struct CheckCommonIgnored
{
    // The functions' and typedefs' marks in the order of their keys, then of their declarations, and a bit a token of the
    // keys that have any.
    const CheckCommonIgnoredMark* marks;
    uint32_t mark_count;
    uint64_t* keys;
    // The tags' (warn_unused_result alone), few.
    const CheckCommonIgnoredMark* tags;
    uint32_t tag_count;
    // Whether a typedef has warn_unused_result, which a call's return type's specifiers are then read for.
    bool typedefs;
    // A byte an identifier: a builtin's name's (BUSTER_CHECK_IGNORED_*), 0 until a call asks.
    uint8_t* builtins;
    uint32_t builtin_count;
};

// What a discarded call's result warns of (check_common_ignored_of): its callee's pure or const, the warn_unused_result or
// nodiscard of its callee or of its return type, and where clang reports it, the callee's first token.
typedef struct CheckCommonIgnoredResult CheckCommonIgnoredResult;
struct CheckCommonIgnoredResult
{
    uint32_t token;
    uint8_t kinds;
    const CheckInputAttribute* result;
    const CheckInputAttribute* type_result;
    uint32_t type_name;
};

// The ignored-result kinds of the attributes `attributes` hold, and their first warn_unused_result.
static uint8_t check_common_ignored_kinds(const CheckInputAttributes* attributes, const CheckInputAttribute** result)
{
    uint8_t kinds = 0;
    *result = NULL;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        // (check_input_attribute_definition marks every nodiscard spelling ignored for multiversioning's conflicts, which
        // C23's standard one is no GNU one's to have; its subject check still applies.)
        const bool standard = attribute->kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT && attribute->standard;
        const bool applied = (!attribute->ignored || standard) && !attribute->subject_ignored;
        const uint8_t kind = !applied ? 0u :
                             attribute->kind == CHECK_ATTRIBUTE_CONST ? BUSTER_CHECK_IGNORED_CONST :
                             attribute->kind == CHECK_ATTRIBUTE_PURE ? BUSTER_CHECK_IGNORED_PURE :
                             attribute->kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT ? BUSTER_CHECK_IGNORED_RESULT : 0u;
        if (kind == BUSTER_CHECK_IGNORED_RESULT && *result == NULL)
        {
            *result = attribute;
        }
        kinds |= kind;
    }
    return kinds;
}

// The unit's marks (CheckCommonIgnored), from the tokens that have attributes, in source order; false where scratch can't
// hold them.
static bool check_common_ignored_prepare(CheckCommonIgnored* ignored, const Parser* parser, const Preprocessor* pp, const CheckInput* input,
                                         const TypeTable* types, Arena* scratch)
{
    const uint32_t count = parser->tokens.count < input->token_count ? parser->tokens.count : input->token_count;
    const bool scalar = parser->options.scalar;
    *ignored = (CheckCommonIgnored){ 0 };
    bool valid = true;
    uint32_t marks = 0;
    uint32_t tags = 0;
    // The first pass counts, the second records.
    CheckCommonIgnoredMark* records = NULL;
    CheckCommonIgnoredMark* tag_records = NULL;
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            records = marks != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonIgnoredMark, marks) : NULL;
            tag_records = tags != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonIgnoredMark, tags) : NULL;
            valid = (marks == 0 || records != NULL) && (tags == 0 || tag_records != NULL);
        }
        uint32_t mark = 0;
        uint32_t tag = 0;
        for (uint32_t first = check_input_attributed_next(input->attributes, 0, count, scalar); valid && first < count;
             first = check_input_attributed_next(input->attributes, first + 16, count, scalar))
        {
            for (uint32_t selected = check_input_attributed_group(input->attributes, first, count, scalar); selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const CheckInputAttribute* result = NULL;
                const uint8_t kinds = check_common_ignored_kinds(check_input_attributes_at(input, d), &result);
                const uint8_t kind = parser->tokens.kinds[d];
                // A declarator's name's binding (C_BINDING_TYPEDEF is 0, every other token's too); a tag's keyword has none.
                const uint8_t binding = kinds != 0 && kind == PARSE_KIND_IDENTIFIER && input->declaration_kinds != NULL ? input->declaration_kinds[d] : C_BINDING_KINDS;
                // A tag's attributes are at its keyword, or at its name.
                const uint32_t tag_name = kinds != 0 && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM) ? check_input_skip_attributes(parser, d + 1, count) :
                                          binding == C_BINDING_TAG_KIND ? d : count;
                const uint32_t tag_index = tag_name < count && parser->tokens.kinds[tag_name] == PARSE_KIND_IDENTIFIER && input->declared_types != NULL ?
                                           check_input_tag_of(types, input->declared_types[tag_name]) : 0;
                if (binding == C_BINDING_FUNCTION || (binding == C_BINDING_TYPEDEF && result != NULL))
                {
                    const uint32_t object = binding == C_BINDING_FUNCTION && input->objects != NULL ? input->objects[d] : 0;
                    if (pass == 1)
                    {
                        records[mark] = (CheckCommonIgnoredMark)
                        {
                            .key = object != 0 && object - 1 < count ? object - 1 : d,
                            .declaration = d,
                            .kinds = binding == C_BINDING_FUNCTION ? kinds : BUSTER_CHECK_IGNORED_RESULT,
                            .result = result,
                        };
                    }
                    mark += 1;
                    ignored->typedefs |= binding == C_BINDING_TYPEDEF;
                }
                else if (tag_index != 0 && result != NULL)
                {
                    if (pass == 1)
                    {
                        tag_records[tag] = (CheckCommonIgnoredMark){ .key = tag_index, .declaration = tag_name, .kinds = BUSTER_CHECK_IGNORED_RESULT, .result = result };
                    }
                    tag += 1;
                }
            }
        }
        marks = pass == 0 ? mark : marks;
        tags = pass == 0 ? tag : tags;
    }
    // The marks by key, each key's in source order (a stable sort of their keys, the mark's index below).
    uint64_t* keys = valid && marks != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, marks) : NULL;
    uint64_t* temporary = keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, marks) : NULL;
    CheckCommonIgnoredMark* sorted = temporary != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonIgnoredMark, marks) : NULL;
    uint64_t* bits = sorted != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count / 64 + 1) : NULL;
    valid &= marks == 0 || bits != NULL;
    if (valid && marks != 0)
    {
        memset(bits, 0, ((uint64_t)count / 64 + 1) * sizeof(uint64_t));
        for (uint32_t i = 0; i < marks; i += 1)
        {
            keys[i] = (uint64_t)records[i].key << 32 | i;
            bits[records[i].key / 64] |= UINT64_C(1) << (records[i].key % 64);
        }
        const uint32_t varying = count != 0 ? UINT32_MAX >> __builtin_clz(count) : 0;
        const uint64_t* ordered = c_bindings_sort(keys, temporary, marks, !BUSTER_SCALAR_PATH(scalar), varying);
        for (uint32_t i = 0; i < marks; i += 1)
        {
            sorted[i] = records[(uint32_t)ordered[i]];
        }
        ignored->marks = sorted;
        ignored->mark_count = marks;
        ignored->keys = bits;
    }
    ignored->tags = tag_records;
    ignored->tag_count = valid ? tags : 0;
    const uint32_t names = pp != NULL && pp->table != NULL ? pp->table->count : 0;
    ignored->builtins = valid && names != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, names) : NULL;
    valid &= names == 0 || ignored->builtins != NULL;
    if (ignored->builtins != NULL)
    {
        memset(ignored->builtins, 0, names);
        ignored->builtin_count = names;
    }
    return valid;
}

// The marks of `key` by its declarations up to `declaration`, which inherits theirs: their kinds, and their first
// warn_unused_result into `result`.
static uint8_t check_common_ignored_marks(const CheckCommonIgnored* ignored, uint32_t key, uint32_t declaration, const CheckInputAttribute** result)
{
    uint8_t kinds = 0;
    *result = NULL;
    if (ignored->keys != NULL && (ignored->keys[key / 64] & (UINT64_C(1) << (key % 64))) != 0)
    {
        uint32_t low = 0;
        uint32_t high = ignored->mark_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            low = ignored->marks[middle].key < key ? middle + 1 : low;
            high = ignored->marks[middle].key < key ? high : middle;
        }
        for (uint32_t i = low; i < ignored->mark_count && ignored->marks[i].key == key && ignored->marks[i].declaration <= declaration; i += 1)
        {
            kinds |= ignored->marks[i].kinds;
            *result = *result == NULL ? ignored->marks[i].result : *result;
        }
    }
    return kinds;
}

// A builtin's kinds by its name's identifier (BUSTER_CHECK_IGNORED_*), from clang's builtin table's attributes: c const,
// U pure, g const where floating-point exceptions are ignored (the default), e const without errno too (never, as clang's
// default -fmath-errno has it), f a plain library function's row. Found once an identifier.
static uint8_t check_common_ignored_builtin(CheckCommonIgnored* ignored, const Preprocessor* pp, uint32_t name)
{
    uint8_t result = 0;
    if (ignored->builtins != NULL && name != 0 && name < ignored->builtin_count)
    {
        result = ignored->builtins[name];
        if (result == 0)
        {
            // The catalog's negative filter answers most names (a unit's own functions') without its search.
            const InternSpan span = pp->table->spans[name];
            const BuiltinIntrinsicName spelled = { (const char*)pp->table->records + span.offset, span.length };
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_filter_candidates(&spelled, 1, pp->options.scalar) != 0 ?
                                               builtin_intrinsic_lookup(spelled.name, spelled.length) : NULL;
            result = BUSTER_CHECK_IGNORED_KNOWN;
            bool argument = false;
            for (uint32_t i = 0; info != NULL && i < info->attributes_length; i += 1)
            {
                // An attribute's argument (a format's `p:0:`) is between colons.
                const char letter = info->attributes[i];
                argument = letter == ':' ? !argument : argument;
                result |= argument || letter == ':' ? 0u :
                          letter == 'c' || letter == 'g' ? BUSTER_CHECK_IGNORED_CONST :
                          letter == 'U' ? BUSTER_CHECK_IGNORED_PURE :
                          letter == 'f' ? BUSTER_CHECK_IGNORED_LIBRARY : 0u;
            }
            ignored->builtins[name] = result;
        }
    }
    return result & (uint8_t)~BUSTER_CHECK_IGNORED_KNOWN;
}

// The opener of the group that `close` closes: the nearest token before it at its depth.
static uint32_t check_common_ignored_opener(const Parser* parser, uint32_t close)
{
    const uint32_t depth = parser->tree.depths[close];
    uint32_t open = close;
    while (open > 0 && parser->tree.depths[open - 1] > depth)
    {
        open -= 1;
    }
    return open > 0 ? open - 1 : BUSTER_CHECK_NONE;
}

// The typedef a function declaration's return type is spelled with: the specifiers its declarator hangs from, up its
// chunks (a pointer's `*`, groups, other declarators' commas), as clang's call return type keeps that sugar; or none.
static uint32_t check_common_ignored_typedef(const Parser* parser, const CheckInput* input, uint32_t declaration)
{
    const uint32_t count = parser->tokens.count < input->token_count ? parser->tokens.count : input->token_count;
    uint32_t up = declaration < count ? parser->tree.parents[declaration] : count;
    for (uint32_t steps = 0; up < count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[up];
        const bool chunk = role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_GROUP ||
                           role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_COMMA;
        up = chunk ? parser->tree.parents[up] : up;
        steps = chunk ? steps : count;
    }
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; up < count && steps < count && result == BUSTER_CHECK_NONE && parser->tree.roles[up] == PARSE_ROLE_SPECIFIER; steps += 1)
    {
        const uint32_t named = parser->tokens.kinds[up] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[up] : BUSTER_CHECK_NONE;
        result = named < count && input->declaration_kinds != NULL && input->declaration_kinds[named] == C_BINDING_TYPEDEF ? named : BUSTER_CHECK_NONE;
        up = parser->tree.parents[up];
    }
    return result;
}

// What the call `node`'s discarded result warns of (CheckCommonIgnoredResult), or false: a call of a function or a
// builtin whose result isn't void.
static bool check_common_ignored_of(CheckCommonEmission* emission, uint32_t node, CheckCommonIgnoredResult* out)
{
    CheckCommonIgnored* ignored = emission->ignored;
    const CheckGraph* graph = &emission->input->graph;
    const CheckInput* input = emission->input;
    const Parser* parser = emission->parser;
    const uint32_t count = parser->tokens.count < input->token_count ? parser->tokens.count : input->token_count;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool call = op == CHECK_OP_CALL;
    const bool builtin = !call && op < CHECK_OP_COUNT && check_common_ignored_op(op);
    const TypeTable* types = emission->types;
    *out = (CheckCommonIgnoredResult){ .token = BUSTER_CHECK_NONE, .type_name = BUSTER_CHECK_NONE };
    // A function's name, its declaration as the call's lookup finds it, and its first declaration; an indirect call has
    // no name, and warns of its return type's alone.
    const uint32_t callee = call ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const uint32_t name = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : BUSTER_CHECK_NONE;
    const uint32_t declaration = name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ?
                                 input->declarations[name] : BUSTER_CHECK_NONE;
    const bool function = declaration < count && input->declaration_kinds != NULL && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
    const uint32_t object = function && input->objects != NULL ? input->objects[declaration] : 0;
    const uint32_t key = object != 0 && object - 1 < count ? object - 1 : declaration;
    // A call of an unmarked function whose name is no builtin's, in a unit of no marked return type, is most calls: one bit
    // and one byte decide it, before its type is read.
    const bool marked = function && ignored != NULL && ignored->keys != NULL && (ignored->keys[key / 64] & (UINT64_C(1) << (key % 64))) != 0;
    const uint8_t table = function && ignored != NULL ? check_common_ignored_builtin(ignored, emission->pp, parser->tokens.names[name]) : 0u;
    const bool candidate = ignored != NULL && (builtin || marked || (table & (BUSTER_CHECK_IGNORED_CONST | BUSTER_CHECK_IGNORED_PURE)) != 0 ||
                                               (call && (ignored->tag_count != 0 || ignored->typedefs)));
    const TypeId type = candidate ? check_stream_type(emission->stream, node) : 0;
    const bool valued = type != 0 && type_kind(types, type_unqualified(type_strip_aligned(types, type))) != BUSTER_TYPE_KIND_VOID;
    if (valued && call)
    {
        if (function)
        {
            out->kinds = check_common_ignored_marks(ignored, key, declaration, &out->result) & (BUSTER_CHECK_IGNORED_CONST | BUSTER_CHECK_IGNORED_PURE);
            // A library function's row of the builtin table is its declaration's where the unit keeps it a builtin and
            // declares it with external linkage (a static one is the unit's own).
            const InternSpan span = emission->pp->table->spans[parser->tokens.names[name]];
            const bool library = (table & BUSTER_CHECK_IGNORED_LIBRARY) == 0 ||
                                 (check_input_linkage(input->declaration_bits, declaration) == 2 &&
                                  pp_library_builtin(&emission->pp->options, emission->pp->table->records + span.offset, span.length));
            out->kinds |= library ? table & (BUSTER_CHECK_IGNORED_CONST | BUSTER_CHECK_IGNORED_PURE) : 0u;
        }
        // The report is at the callee's first token, its parentheses' too.
        uint32_t at = name;
        while (at < count && at > 0 && parser->tokens.kinds[at - 1] == PARSE_KIND_L_PAREN && parser->tree.roles[at - 1] == PARSE_ROLE_OPEN_GROUP)
        {
            at -= 1;
        }
        out->token = at;
        // The return type's: its struct's, union's or enum's, then its typedef's and theirs.
        const uint32_t tag = ignored->tag_count != 0 ? check_input_tag_of(types, type) : 0;
        for (uint32_t i = 0; tag != 0 && out->type_result == NULL && i < ignored->tag_count; i += 1)
        {
            out->type_result = ignored->tags[i].key == tag ? ignored->tags[i].result : NULL;
            out->type_name = ignored->tags[i].key == tag ? ignored->tags[i].declaration : BUSTER_CHECK_NONE;
        }
        uint32_t spelled = ignored->typedefs && out->type_result == NULL && function ? check_common_ignored_typedef(parser, input, declaration) : BUSTER_CHECK_NONE;
        for (uint32_t steps = 0; spelled < count && out->type_result == NULL && steps < 64; steps += 1)
        {
            const CheckInputAttribute* result = NULL;
            check_common_ignored_marks(ignored, spelled, spelled, &result);
            out->type_result = result;
            out->type_name = result != NULL ? spelled : BUSTER_CHECK_NONE;
            spelled = result == NULL ? check_common_ignored_typedef(parser, input, spelled) : BUSTER_CHECK_NONE;
        }
    }
    else if (valued && builtin)
    {
        // A builtin's name: its call's `(`'s token before it.
        const uint32_t close = graph->tokens[node];
        const uint32_t open = close < count && (parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[close] == PARSE_ROLE_CLOSE_KEYWORD) ?
                              check_common_ignored_opener(parser, close) : BUSTER_CHECK_NONE;
        const uint32_t name = open != BUSTER_CHECK_NONE && open > 0 && parser->tokens.kinds[open - 1] == PARSE_KIND_IDENTIFIER ? open - 1 : BUSTER_CHECK_NONE;
        const uint8_t table = name < count ? check_common_ignored_builtin(ignored, emission->pp, parser->tokens.names[name]) : 0u;
        const InternSpan span = name < count ? emission->pp->table->spans[parser->tokens.names[name]] : (InternSpan){ 0 };
        const bool library = (table & BUSTER_CHECK_IGNORED_LIBRARY) == 0 ||
                             pp_library_builtin(&emission->pp->options, emission->pp->table->records + span.offset, span.length);
        out->kinds = library ? table & (BUSTER_CHECK_IGNORED_CONST | BUSTER_CHECK_IGNORED_PURE) : 0u;
        out->token = name;
    }
    return out->token < count && (out->kinds != 0 || out->result != NULL || out->type_result != NULL);
}

// Whether `token` was written in a macro's body, or in a system header's macro, where clang drops a pure or const call's
// warning (SourceManager's isMacroBodyExpansion, isInSystemMacro): a macro's token that no argument substituted.
static bool check_common_ignored_macro(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    const uint32_t source = token < parser->tokens.count ? parser->tokens.sources[token] : BUSTER_PP_NONE;
    bool result = false;
    if (source < pp->output.count && pp->output.flags != NULL && (pp->output.flags[source] & BUSTER_PP_FROM_MACRO) != 0)
    {
        const uint32_t origin = pp_history_ensure(pp) ? pp_history_at(pp, source) : 0;
        const bool argument = origin != 0 && (origin & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_ARGUMENT;
        const uint32_t spelling = pp->output.locations != NULL ? pp->output.locations[source] : 0;
        result = !argument || (spelling != 0 && pp_system_level_at(pp, spelling) != BUSTER_PP_USER_FILE);
    }
    return result;
}

// The warning of a discarded call's result (CheckCommonIgnoredResult), at once, as clang's DiagnoseUnusedExprResult
// gives it: the callee's warn_unused_result, then its return type's, then (outside a macro's body) its pure, then its
// const.
static void check_common_ignored_report(CheckCommonEmission* emission, const CheckCommonIgnoredResult* found)
{
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    const CheckInputAttribute* attribute = found->result != NULL ? found->result : found->type_result;
    // The attribute as clang names it: a scoped one with its scope (`gnu::warn_unused_result`).
    const bool scoped = attribute != NULL && attribute->standard && attribute->first != attribute->name_token && attribute->first < parser->tokens.count;
    const String8 name = attribute != NULL ? check_input_attribute_spelling(pp->table, attribute->name) : (String8){ 0 };
    const String8 scope = scoped ? check_input_attribute_spelling(pp->table, parser->tokens.names[attribute->first]) : (String8){ 0 };
    const char* named = check_warning_text(emission->scratch, "%.*s%s%.*s", (int)scope.length, scope.pointer, scoped ? "::" : "", (int)name.length, name.pointer);
    const String8 spelling = { (char*)named, named != NULL ? strlen(named) : 0 };
    // Only the standard attribute's message is clang's (handleWarnUnusedResult reads none of a GNU or a scoped one).
    const bool message = attribute != NULL && attribute->standard && !scoped && attribute->text.length != 0;
    const char* text = NULL;
    uint32_t id = CHECK_WARNING_ID_UNUSED_VALUE;
    if (found->result != NULL)
    {
        id = CHECK_WARNING_ID_UNUSED_RESULT;
        text = check_warning_text(emission->scratch, "ignoring return value of function declared with '%.*s' attribute%s%.*s", (int)spelling.length, spelling.pointer,
                                  message ? ": " : "", message ? (int)attribute->text.length : 0, message ? attribute->text.pointer : "");
    }
    else if (found->type_result != NULL && found->type_name < parser->tokens.count)
    {
        const uint32_t name = parser->tokens.names[found->type_name];
        text = check_warning_text(emission->scratch, "ignoring return value of type '%.*s' declared with '%.*s' attribute%s%.*s", (int)intern_length(pp->table, name),
                                  (const char*)intern_bytes(pp->table, name), (int)spelling.length, spelling.pointer, message ? ": " : "",
                                  message ? (int)attribute->text.length : 0, message ? attribute->text.pointer : "");
    }
    else if (found->kinds != 0 && !check_common_ignored_macro(parser, pp, found->token))
    {
        text = check_warning_text(emission->scratch, "ignoring return value of function declared with %s attribute",
                                  (found->kinds & BUSTER_CHECK_IGNORED_PURE) != 0 ? "pure" : "const");
    }
    if (text != NULL)
    {
        check_common_report(parser, pp, emission->input, found->token, id, false, text, NULL, 0, emission->scratch);
    }
}

// The discarded call `node`'s warning, if its result warns of one (check_common_ignored_of): whether it does.
static bool check_common_ignored_call(CheckCommonEmission* emission, uint32_t node)
{
    CheckCommonIgnoredResult found;
    const bool result = emission->ignored != NULL && check_common_ignored_of(emission, node, &found);
    if (result)
    {
        check_common_ignored_report(emission, &found);
    }
    return result;
}
