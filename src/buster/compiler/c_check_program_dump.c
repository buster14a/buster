// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __builtin_dump_struct's calls (#148), planned before the function lanes run, as clang's BuiltinDumpStructGenerator
// (clang/lib/Sema/SemaChecking.cpp) builds them: printer(rest..., format, operands...) once a line. The record's type
// name ("%s"), then " {\n", then a line a field: its indent, its type's name and its name ("%s%s %s "), a bit-field's
// width (": %zu "), then "=" and, for a field of a struct or union type, " {\n" and its fields one level deeper and
// "%s}\n" with its indent; for any other, " " and the value's format (PrintfSpecifier::fixType's, a string's "\"%.32s\"",
// another pointer's "%p") and "\n", or "*%p" and the field's address where no format fits. Unnamed bit-fields are
// skipped and anonymous members' fields flattened (clang's IndirectFieldDecl), and the record's line ends "}\n". Each
// call's strings are a pool of its own (CheckLowerInitializers.items), each string once.

#define BUSTER_CHECK_DUMP_LINES 4096u
#define BUSTER_CHECK_DUMP_DEPTH 64u
#define BUSTER_CHECK_DUMP_IMAGE (1u << 20)
// The nodes of the printer's and the rest of the arguments' trees, which each call evaluates again.
#define BUSTER_CHECK_DUMP_SUBTREE 4096u

typedef struct CheckLowerDumpBuilder CheckLowerDumpBuilder;
struct CheckLowerDumpBuilder
{
    const CheckProgram* program;
    Arena* scratch;
    TypeNames names;
    // Each record's body, its `{` and `}` (BUSTER_CHECK_NONE where it has none: check_lower_dump_body), which give an
    // unnamed record's scope (check_lower_dump_parent).
    uint32_t* opens;
    uint32_t* closes;
    uint32_t options;
    bool c99;
    uint8_t* image;
    uint32_t image_size;
    uint32_t* strings;
    uint32_t string_count;
    CheckLowerDumpLine* lines;
    uint32_t line_count;
    CheckLowerDumpOperand* operands;
    uint32_t operand_count;
    const char* refusal;
};

// The string's offset in the image, its bytes added once, NUL-terminated.
static uint32_t check_lower_dump_string(CheckLowerDumpBuilder* builder, const char* bytes, uint64_t length)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t s = 0; result == UINT32_MAX && s < builder->string_count; s += 1)
    {
        const uint32_t offset = builder->strings[s];
        const bool same = builder->image_size - offset > length && builder->image[offset + length] == 0 && memcmp(builder->image + offset, bytes, length) == 0;
        result = same ? offset : result;
    }
    if (result == UINT32_MAX && builder->refusal == NULL)
    {
        if (length >= BUSTER_CHECK_DUMP_IMAGE - builder->image_size || builder->string_count >= BUSTER_CHECK_DUMP_LINES * 6)
        {
            builder->refusal = "__builtin_dump_struct of a record whose lines take more than 1 MiB or 4096 lines";
        }
        else
        {
            result = builder->image_size;
            memcpy(builder->image + result, bytes, length);
            builder->image[result + length] = 0;
            builder->image_size += (uint32_t)length + 1;
            builder->strings[builder->string_count++] = result;
        }
    }
    return result == UINT32_MAX ? 0 : result;
}

static uint32_t check_lower_dump_text(CheckLowerDumpBuilder* builder, const char* text)
{
    return check_lower_dump_string(builder, text, strlen(text));
}

// A call's line: its format and operands.
static void check_lower_dump_line(CheckLowerDumpBuilder* builder, const char* format, const CheckLowerDumpOperand* operands, uint32_t count)
{
    const uint32_t offset = check_lower_dump_text(builder, format);
    if (builder->refusal == NULL && builder->line_count >= BUSTER_CHECK_DUMP_LINES)
    {
        builder->refusal = "__builtin_dump_struct of a record whose lines take more than 1 MiB or 4096 lines";
    }
    if (builder->refusal == NULL)
    {
        builder->lines[builder->line_count++] = (CheckLowerDumpLine){ .format = offset, .first = builder->operand_count, .count = count };
        for (uint32_t o = 0; o < count; o += 1)
        {
            builder->operands[builder->operand_count++] = operands[o];
        }
    }
}

static CheckLowerDumpOperand check_lower_dump_operand_string(uint32_t offset)
{
    return (CheckLowerDumpOperand){ .kind = CHECK_LOWER_DUMP_STRING, .value = offset };
}

// The indent of `depth` levels, two spaces each (clang's PrintingPolicy.Indentation).
static uint32_t check_lower_dump_indent(CheckLowerDumpBuilder* builder, uint32_t depth)
{
    char spaces[2 * BUSTER_CHECK_DUMP_DEPTH + 2];
    const uint32_t length = 2 * depth;
    memset(spaces, ' ', length);
    return check_lower_dump_string(builder, spaces, length);
}

// The body of record `tag`'s definition, its `{` and its `}`, from its completion (CheckGraph.tag_completions, the
// token past its `}`, which the deferred reader's merge maps as source order makes it), or false.
static bool check_lower_dump_body(const CheckProgram* program, uint32_t tag, uint32_t* open, uint32_t* close)
{
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    const uint32_t completion = graph->tag_completions != NULL && tag < graph->tag_completion_count ? graph->tag_completions[tag] : BUSTER_CHECK_NONE;
    const bool braced = completion != BUSTER_CHECK_NONE && completion > 0 && completion <= parser->tokens.count &&
                        parser->tokens.kinds[completion - 1] == PARSE_KIND_R_BRACE;
    uint32_t depth = 0;
    uint32_t i = braced ? completion - 1 : 0;
    bool scanning = braced;
    while (scanning && i > 0)
    {
        const uint8_t kind = parser->tokens.kinds[i];
        depth = kind == PARSE_KIND_R_BRACE ? depth + 1 : kind == PARSE_KIND_L_BRACE ? depth - 1 : depth;
        scanning = depth != 0;
        i -= scanning ? 1 : 0;
    }
    const bool found = braced && !scanning && parser->tokens.kinds[i] == PARSE_KIND_L_BRACE;
    *open = found ? i : BUSTER_CHECK_NONE;
    *close = found ? completion - 1 : BUSTER_CHECK_NONE;
    return found;
}

// The next declarator of a member named `name` in a record's body, from `*cursor` (past which it moves) to its `}`,
// `close`: an identifier at the body's own depth that declares itself, of the kind byte a member's declarator leaves 0
// (TYPEDEF's; a parameter of a member's declarator is an OBJECT), or 0 where the body has none.
static uint32_t check_lower_dump_member(const CheckProgram* program, uint32_t* cursor, uint32_t close, uint32_t name)
{
    const Parser* parser = program->parser;
    const CheckInput* input = program->input;
    const uint32_t end = close < input->token_count && close < parser->tokens.count ? close : 0;
    const bool known = input->declarations != NULL && input->declaration_kinds != NULL;
    uint32_t result = 0;
    uint32_t depth = 0;
    for (uint32_t t = *cursor; known && name != 0 && result == 0 && t < end; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        depth = kind == PARSE_KIND_L_BRACE ? depth + 1 : kind == PARSE_KIND_R_BRACE && depth != 0 ? depth - 1 : depth;
        result = depth == 0 && kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == name && input->declarations[t] == t &&
                 input->declaration_kinds[t] == C_BINDING_TYPEDEF ? t : 0;
    }
    *cursor = result != 0 ? result + 1 : *cursor;
    return result;
}

// The typedef whose name declaration token `token`'s type specifier spells (check_input_typedef_spelling), or 0.
static uint32_t check_lower_dump_spelling(const CheckProgram* program, uint32_t token)
{
    const CheckInput* input = program->input;
    const bool known = token != 0 && token < input->token_count && input->declarations != NULL && input->declaration_kinds != NULL &&
                       input->declared_types != NULL;
    return known ? check_input_typedef_spelling(input, program->parser, token) : 0;
}

// The name of `type`, a member's declared one as its declaration at `token` spells it (its typedef specifier's name kept,
// as clang's sugared type prints), or with `token` 0 its canonical type, as clang's TypePrinter prints it.
static uint32_t check_lower_dump_type(CheckLowerDumpBuilder* builder, TypeId type, uint32_t token)
{
    const CheckProgram* program = builder->program;
    const uint32_t spelling = check_lower_dump_spelling(program, token);
    TypeNames names = builder->names;
    TypeId alias_type = 0;
    String8 alias_name = { 0 };
    if (spelling != 0 && program->parser->tokens.names[spelling] < program->identifiers->count)
    {
        const uint32_t identifier = program->parser->tokens.names[spelling];
        alias_type = type_unqualified(type_strip_aligned(program->types, program->input->declared_types[spelling]));
        alias_name = (String8){ .pointer = (char*)intern_bytes(program->identifiers, identifier), .length = intern_length(program->identifiers, identifier) };
        names.alias_types = &alias_type;
        names.alias_names = &alias_name;
        names.alias_count = 1;
    }
    const uint64_t mark = builder->scratch->length;
    const String8 printed = type_print(program->types, builder->scratch, type, (String8){ 0 }, &names, builder->options);
    if (printed.length == 0 && builder->refusal == NULL)
    {
        builder->refusal = "__builtin_dump_struct of a type whose name this compiler doesn't print";
    }
    const uint32_t result = check_lower_dump_string(builder, printed.pointer, printed.length);
    arena_pop_to(builder->scratch, mark);
    return result;
}

// The scope of unnamed record `tag` (clang's DeclContext of it): the innermost struct or union whose body holds its body,
// whatever declarator of its members the record is part of (an array's, a pointer's, a function's), or BUSTER_CHECK_NONE
// for one at file or function scope; and with `anonymous`, whether it is that record's anonymous member's type. A named
// tag's scope is never a record's: C's tags aren't members of their records (clang puts them in the scope around).
static uint32_t check_lower_dump_parent(const CheckLowerDumpBuilder* builder, uint32_t tag, bool* anonymous)
{
    const TypeTable* types = builder->program->types;
    const uint32_t open = builder->opens[tag];
    const uint32_t close = builder->closes[tag];
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; types->tags.name[tag] == 0 && open != BUSTER_CHECK_NONE && t < types->tags.count; t += 1)
    {
        const uint8_t kind = type_is_valid(types, types->tags.type[t]) ? type_kind(types, types->tags.type[t]) : BUSTER_TYPE_KIND_NONE;
        const bool holds = t != tag && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && builder->opens[t] != BUSTER_CHECK_NONE &&
                           builder->opens[t] < open && close < builder->closes[t];
        result = holds && (result == BUSTER_CHECK_NONE || builder->opens[result] < builder->opens[t]) ? t : result;
    }
    *anonymous = false;
    for (uint32_t m = 0; result != BUSTER_CHECK_NONE && m < types->tags.member_count[result]; m += 1)
    {
        const uint32_t member = types->tags.member_begin[result] + m;
        const TypeId bare = type_unqualified(type_strip_aligned(types, types->members.type[member]));
        const uint8_t kind = type_kind(types, bare);
        *anonymous |= types->members.name[member] == 0 && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 &&
                      (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && types->types.index[type_index(bare)] == tag;
    }
    return result;
}

// Whether declaration token `token` is a typedef's name: the typedef keyword among its declaration's specifiers, before
// it at its own brace depth (a record's body skipped), where a member's declaration meets its record's `{` first.
static bool check_lower_dump_typedef(const Parser* parser, uint32_t token)
{
    uint32_t depth = 0;
    bool found = false;
    bool open = true;
    for (uint32_t t = token; open && !found && t-- > 0;)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        found = depth == 0 && kind == PARSE_KIND_TYPEDEF;
        open = !(depth == 0 && (kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE));
        depth = kind == PARSE_KIND_R_BRACE ? depth + 1 : kind == PARSE_KIND_L_BRACE && depth != 0 ? depth - 1 : depth;
    }
    return found;
}

// The most typedefs a parameter's spelling keeps (check_lower_dump_parameter).
#define BUSTER_CHECK_DUMP_PARAMETER_ALIASES 16u

// A function scope's parameter `parameter` (its type in the function's type), as its declaration from `begin` to `end`
// (a segment of the definition's parameter list) spells it, which clang prints (ParmVarDecl::getType's): its declarator's
// declared type, qualifiers kept, every typedef its tokens name printed for the type it names; an array's or a function's
// adjusted type, which keeps none, as the function's type has it.
static String8 check_lower_dump_parameter(CheckLowerDumpBuilder* builder, uint32_t begin, uint32_t end, TypeId parameter)
{
    const CheckProgram* program = builder->program;
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const TypeTable* types = program->types;
    const TypeId adjusted = type_unqualified(type_strip_aligned(types, parameter));
    const bool known = input->declarations != NULL && input->declaration_kinds != NULL && input->declared_types != NULL;
    TypeId alias_types[BUSTER_CHECK_DUMP_PARAMETER_ALIASES];
    String8 alias_names[BUSTER_CHECK_DUMP_PARAMETER_ALIASES];
    uint32_t aliases = 0;
    TypeId printed = parameter;
    bool named = false;
    for (uint32_t t = begin; known && t < end && t < input->token_count; t += 1)
    {
        const uint32_t resolved = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? input->declarations[t] : BUSTER_CHECK_NONE;
        const uint32_t identifier = parser->tokens.names[t];
        if (resolved == t && !named)
        {
            // The parameter's own declarator, the first identifier of its declaration of its type that declares itself.
            const TypeId declared = input->declared_types[t];
            const TypeId bare = type_is_valid(types, declared) ? type_unqualified(type_strip_aligned(types, declared)) : 0;
            const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
            named = bare == adjusted || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION;
            printed = named && bare == adjusted ? declared : parameter;
        }
        else if (resolved != BUSTER_CHECK_NONE && resolved != t && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF &&
                 aliases < BUSTER_CHECK_DUMP_PARAMETER_ALIASES && identifier < program->identifiers->count && check_lower_dump_typedef(parser, resolved))
        {
            alias_types[aliases] = type_unqualified(type_strip_aligned(types, input->declared_types[resolved]));
            alias_names[aliases++] = (String8){ .pointer = (char*)intern_bytes(program->identifiers, identifier), .length = intern_length(program->identifiers, identifier) };
        }
    }
    TypeNames names = builder->names;
    names.alias_types = alias_types;
    names.alias_names = alias_names;
    names.alias_count = aliases;
    return type_print(types, builder->scratch, printed, (String8){ 0 }, &names, builder->options);
}

// The scope a function gives the record whose body opens at `open` (NamedDecl::printNestedNameSpecifier's): the function
// whose body holds it, as its name and, with a prototype, its parameters' types (and "..."), then "::"; nothing outside
// a function. Writes it to `text`, of `room` bytes, and returns its length (`room` or more where it doesn't fit).
static uint64_t check_lower_dump_function_scope(CheckLowerDumpBuilder* builder, uint32_t open, char* text, uint64_t room)
{
    const CheckProgram* program = builder->program;
    const Parser* parser = program->parser;
    const TypeTable* types = program->types;
    uint32_t body = BUSTER_CHECK_NONE;
    for (uint32_t b = 0; open != BUSTER_CHECK_NONE && b < program->body_count; b += 1)
    {
        const uint32_t opening = program->bodies[b].opening;
        const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : 0;
        body = opening < open && open < closing && closing <= parser->tokens.count ? b : body;
    }
    uint64_t length = 0;
    const uint32_t declaration = body != BUSTER_CHECK_NONE ? program->bodies[body].declaration : BUSTER_CHECK_NONE;
    const TypeId function = declaration < program->input->token_count ? type_strip_aligned(types, program->input->declared_types[declaration]) : 0;
    const uint32_t identifier = declaration < parser->tokens.count ? parser->tokens.names[declaration] : 0;
    if (function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION && identifier != 0 && identifier < program->identifiers->count)
    {
        // A K&R definition's parameters' declarations end in a `;` before its body: it has no written prototype, whose
        // types clang prints.
        const uint32_t opening = program->bodies[body].opening;
        const bool written = opening == 0 || parser->tokens.kinds[opening - 1] != PARSE_KIND_SEMI;
        const uint8_t flags = types->types.flags[type_index(function)];
        uint32_t count = 0;
        const TypeId* parameters = written && (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? type_parameters(types, function, &count) : NULL;
        count = parameters != NULL ? count : 0;
        length += (uint64_t)snprintf(text, room, "%s(", (const char*)intern_bytes(program->identifiers, identifier));
        // The parameters' declarations: the list after the name, cut at its own commas (a parameter's segment is
        // empty where the list isn't found, whose parameter prints as the function's type has it).
        const uint32_t list = declaration + 1 < parser->tokens.count && parser->tokens.kinds[declaration + 1] == PARSE_KIND_L_PAREN ? declaration + 1 : 0;
        uint32_t cursor = list + 1;
        for (uint32_t p = 0; p < count && length < room; p += 1)
        {
            uint32_t end = cursor;
            uint32_t depth = 0;
            bool open_list = list != 0;
            while (open_list && end < parser->tokens.count)
            {
                const uint8_t kind = parser->tokens.kinds[end];
                const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
                const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
                open_list = !(depth == 0 && (closer || kind == PARSE_KIND_COMMA));
                depth = opener ? depth + 1 : closer && depth != 0 ? depth - 1 : depth;
                end += open_list ? 1 : 0;
            }
            const uint64_t mark = builder->scratch->length;
            const String8 printed = check_lower_dump_parameter(builder, list != 0 ? cursor : 0, list != 0 ? end : 0, parameters[p]);
            length += (uint64_t)snprintf(text + length, room - length, "%s%.*s", p != 0 ? ", " : "", (int)printed.length, printed.pointer);
            arena_pop_to(builder->scratch, mark);
            cursor = end + 1;
        }
        const bool variadic = parameters != NULL && (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        length += length < room ? (uint64_t)snprintf(text + length, room - length, "%s)::", variadic ? count != 0 ? ", ..." : "..." : "") : 0;
    }
    return length;
}

// The canonical name of a record's type (clang's getCanonicalTagType's, AnonymousTagMode::Plain): a named one's, or its
// typedef's, or an unnamed one's scope (NamedDecl::printNestedNameSpecifier's: the records whose bodies hold it
// (check_lower_dump_parent), each a
// name, a typedef's, or "(unnamed struct)", "(anonymous struct)" for an anonymous member's, each then "::", after the
// function whose body holds them), then "(unnamed)".
static uint32_t check_lower_dump_canonical(CheckLowerDumpBuilder* builder, TypeId record)
{
    const TypeTable* types = builder->program->types;
    const uint32_t tag = types->types.index[type_index(record)];
    uint32_t result = 0;
    bool anonymous = false;
    const uint32_t scope = types->tags.name[tag] == 0 && builder->names.anonymous[tag].length == 0 ? check_lower_dump_parent(builder, tag, &anonymous) : BUSTER_CHECK_NONE;
    if (scope == BUSTER_CHECK_NONE)
    {
        result = check_lower_dump_type(builder, record, 0);
    }
    else
    {
        // The scopes outward, each with whether it is its own scope's anonymous member's type.
        uint32_t chain[BUSTER_CHECK_DUMP_DEPTH];
        bool anonymous_members[BUSTER_CHECK_DUMP_DEPTH];
        uint32_t depth = 0;
        for (uint32_t parent = scope; parent != BUSTER_CHECK_NONE && depth < BUSTER_CHECK_DUMP_DEPTH;)
        {
            chain[depth] = parent;
            parent = check_lower_dump_parent(builder, parent, &anonymous_members[depth]);
            depth += 1;
        }
        char text[4096];
        uint64_t length = (uint64_t)snprintf(text, sizeof(text), "%s ", type_kind(types, record) == BUSTER_TYPE_KIND_UNION ? "union" : "struct");
        length += check_lower_dump_function_scope(builder, depth != 0 ? builder->opens[chain[depth - 1]] : BUSTER_CHECK_NONE, text + length,
                                                  sizeof(text) - length);
        for (uint32_t c = depth; c-- > 0 && length < sizeof(text);)
        {
            const uint32_t context = chain[c];
            const uint32_t name = types->tags.name[context];
            const char* kind = type_kind(types, types->tags.type[context]) == BUSTER_TYPE_KIND_UNION ? "union" : "struct";
            const String8 own = name != 0 ? (String8){ .pointer = (char*)intern_bytes(builder->program->identifiers, name),
                                                       .length = intern_length(builder->program->identifiers, name) } :
                                            builder->names.anonymous[context];
            length += own.length != 0 ? (uint64_t)snprintf(text + length, sizeof(text) - length, "%.*s::", (int)own.length, own.pointer) :
                                        (uint64_t)snprintf(text + length, sizeof(text) - length, "(%s %s)::",
                                                           anonymous_members[c] ? "anonymous" : "unnamed", kind);
        }
        length += length < sizeof(text) ? (uint64_t)snprintf(text + length, sizeof(text) - length, "(unnamed)") : 0;
        if (length >= sizeof(text) && builder->refusal == NULL)
        {
            builder->refusal = "__builtin_dump_struct of a type whose name this compiler doesn't print";
        }
        result = check_lower_dump_string(builder, text, length < sizeof(text) ? length : 0);
    }
    return result;
}

// The length modifier a size_t, ssize_t, ptrdiff_t, intmax_t or uintmax_t typedef of file scope gives the member's type
// spelled at `token` (clang's namedTypeToLengthModifier, through the typedefs that spell the whole type `bare`), or 0.
static char check_lower_dump_named_length(const CheckLowerDumpBuilder* builder, uint32_t token, TypeId bare)
{
    const CheckProgram* program = builder->program;
    const CheckInput* input = program->input;
    char result = 0;
    uint32_t spelling = check_lower_dump_spelling(program, token);
    for (uint32_t guard = 0; result == 0 && spelling != 0 && guard < 64; guard += 1)
    {
        const bool whole = type_unqualified(type_strip_aligned(program->types, input->declared_types[spelling])) == bare;
        const bool file_scope = check_sparse_get(input->declaration_bodies, spelling) >= input->token_count;
        const uint32_t identifier = program->parser->tokens.names[spelling];
        const char* name = identifier < program->identifiers->count ? (const char*)intern_bytes(program->identifiers, identifier) : "";
        if (whole && file_scope)
        {
            // Interned spellings end in a NUL.
            result = strcmp(name, "size_t") == 0 || strcmp(name, "ssize_t") == 0 ? 'z' : strcmp(name, "ptrdiff_t") == 0 ? 't' :
                     strcmp(name, "intmax_t") == 0 || strcmp(name, "uintmax_t") == 0 ? 'j' : 0;
        }
        spelling = whole ? check_lower_dump_spelling(program, spelling) : 0;
    }
    return result;
}

// The format of a field of `type` spelled at `token` (clang's appendFormatSpecifier), written to `format`; false where
// none fits, whose field prints as "*%p" of its address. bool, char, signed char and unsigned char print as numbers; a
// pointer to a character type as a quoted string of at most 32 characters, any other as %p; an enum as its underlying
// type; a vector of a builtin type with its elements' count (%v4d); __int128, _Float16, __bf16, __fp16 and __float128,
// a _BitInt, an array, a complex or an _Atomic type have none.
static bool check_lower_dump_specifier(const CheckLowerDumpBuilder* builder, TypeId type, uint32_t token, char* format)
{
    const TypeTable* types = builder->program->types;
    TypeId bare = type_unqualified(type_strip_aligned(types, type));
    uint8_t kind = type_kind(types, bare);
    bool result = true;
    format[0] = 0;
    if (bare == BUSTER_TYPE_BOOL || bare == BUSTER_TYPE_CHAR || bare == BUSTER_TYPE_SCHAR || bare == BUSTER_TYPE_UCHAR)
    {
        const bool is_unsigned = bare == BUSTER_TYPE_UCHAR || (bare == BUSTER_TYPE_CHAR && types->char_unsigned);
        strcpy(format, bare == BUSTER_TYPE_BOOL ? "%d" : is_unsigned ? "%hhu" : "%hhd");
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId pointee = type_unqualified(type_strip_aligned(types, type_element(types, bare)));
        const bool string = pointee == BUSTER_TYPE_CHAR || pointee == BUSTER_TYPE_SCHAR || pointee == BUSTER_TYPE_UCHAR;
        strcpy(format, string ? "\"%.32s\"" : "%p");
    }
    else
    {
        const bool enumeration = kind == BUSTER_TYPE_KIND_ENUM;
        const bool vector = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        const uint64_t elements = vector ? types->types.extent[type_index(bare)] : 0;
        bare = enumeration || vector ? type_unqualified(type_strip_aligned(types, types->types.base[type_index(bare)])) : bare;
        kind = type_kind(types, bare);
        const bool integer = kind == BUSTER_TYPE_KIND_INTEGER && bare != BUSTER_TYPE_BOOL && bare != BUSTER_TYPE_INT128 && bare != BUSTER_TYPE_UINT128;
        const bool floating = bare == BUSTER_TYPE_FLOAT || bare == BUSTER_TYPE_DOUBLE || bare == BUSTER_TYPE_LONG_DOUBLE;
        result = integer || floating;
        if (result)
        {
            const char* length = bare == BUSTER_TYPE_CHAR || bare == BUSTER_TYPE_SCHAR || bare == BUSTER_TYPE_UCHAR ? "hh" :
                                 bare == BUSTER_TYPE_SHORT || bare == BUSTER_TYPE_USHORT ? "h" :
                                 bare == BUSTER_TYPE_LONG || bare == BUSTER_TYPE_ULONG ? "l" :
                                 bare == BUSTER_TYPE_LLONG || bare == BUSTER_TYPE_ULLONG ? "ll" :
                                 bare == BUSTER_TYPE_LONG_DOUBLE ? "L" :
                                 vector && (bare == BUSTER_TYPE_INT || bare == BUSTER_TYPE_UINT || bare == BUSTER_TYPE_FLOAT) ? "hl" :
                                 vector && bare == BUSTER_TYPE_DOUBLE ? "l" : "";
            // A typedef's own length (C99's z, t and j), through the member's spelling; an enum's and a vector's element
            // type have none of it here.
            const char named = builder->c99 && !enumeration && !vector ? check_lower_dump_named_length(builder, token, bare) : 0;
            uint32_t width = 0;
            bool is_signed = false;
            (void)check_action_integer_type(types, bare, &width, &is_signed);
            // A character type (a vector's element; its typedef, which clang's element type may keep, isn't known here)
            // prints as a character, any other integer as a number.
            const bool character = bare == BUSTER_TYPE_CHAR || bare == BUSTER_TYPE_SCHAR || bare == BUSTER_TYPE_UCHAR;
            const char conversion = character ? 'c' : floating ? 'f' : is_signed ? 'd' : 'u';
            char count[24] = { 0 };
            if (vector)
            {
                snprintf(count, sizeof(count), "v%llu", (unsigned long long)elements);
            }
            const char named_text[2] = { named, 0 };
            snprintf(format, 40, "%%%s%s%c", count, named != 0 ? named_text : conversion == 'c' ? "" : length, conversion);
        }
    }
    return result;
}

// The promoted type of a field's value of `type` (of `width` bits for a bit-field), as a variadic argument's: an integer's
// promotion (a bit-field's, clang's isPromotableBitField), a float's double.
static TypeId check_lower_dump_promoted(const TypeTable* types, TypeId type, uint32_t width)
{
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    TypeId result = bare;
    if (width != BUSTER_TYPE_NOT_BIT_FIELD)
    {
        result = type_promote_bit_field(types, bare, width);
    }
    else if (bare == BUSTER_TYPE_FLOAT)
    {
        result = BUSTER_TYPE_DOUBLE;
    }
    else if (type_kind(types, bare) == BUSTER_TYPE_KIND_INTEGER || type_kind(types, bare) == BUSTER_TYPE_KIND_ENUM)
    {
        result = type_promote(types, bare);
    }
    return result;
}

// The lines of a dump of a record of `record` (dumpUnnamedRecord and dumpRecordValue's), by an explicit stack of the
// records being dumped: a nested record's fields one level deeper, an anonymous member's at its record's level.
static void check_lower_dump_record(CheckLowerDumpBuilder* builder, TypeId record)
{
    typedef struct
    {
        uint32_t tag;
        uint32_t index;
        uint64_t offset;
        uint32_t depth;
        // The indent its closing line prints (BUSTER_CHECK_NONE for the dumped record's), and whether it is an
        // anonymous member's, which has no lines of its own.
        uint32_t indent;
        bool anonymous;
        // Its body's `}` and the place its next member's declarator is looked for from (check_lower_dump_member).
        uint32_t close;
        uint32_t cursor;
    } Frame;
    const CheckProgram* program = builder->program;
    const TypeTable* types = program->types;
    Frame stack[BUSTER_CHECK_DUMP_DEPTH];
    uint32_t depth = 0;
    const CheckLowerDumpOperand header = check_lower_dump_operand_string(check_lower_dump_canonical(builder, record));
    check_lower_dump_line(builder, "%s", &header, 1);
    check_lower_dump_line(builder, " {\n", NULL, 0);
    const uint32_t first_tag = types->types.index[type_index(record)];
    stack[depth++] = (Frame){ .tag = first_tag, .indent = BUSTER_CHECK_NONE, .close = builder->closes[first_tag], .cursor = builder->opens[first_tag] + 1 };
    while (depth != 0 && builder->refusal == NULL)
    {
        Frame* top = stack + depth - 1;
        const uint32_t tag = top->tag;
        if (top->index >= types->tags.member_count[tag])
        {
            const Frame done = *top;
            depth -= 1;
            if (!done.anonymous && done.indent != BUSTER_CHECK_NONE)
            {
                const CheckLowerDumpOperand indent = check_lower_dump_operand_string(done.indent);
                check_lower_dump_line(builder, "%s}\n", &indent, 1);
            }
            else if (!done.anonymous)
            {
                check_lower_dump_line(builder, "}\n", NULL, 0);
            }
        }
        else
        {
            const uint32_t member = types->tags.member_begin[tag] + top->index;
            top->index += 1;
            const uint32_t name = types->members.name[member];
            const bool bits = (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const TypeId type = types->members.type[member];
            const TypeId bare = type_unqualified(type_strip_aligned(types, type));
            const uint8_t kind = type_kind(types, bare);
            const bool record_kind = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
            const uint64_t offset = top->offset + types->members.offset[member];
            // The member's declarator, in its record's body (none for a record without one: its types print canonical).
            const uint32_t token = top->close != BUSTER_CHECK_NONE ? check_lower_dump_member(program, &top->cursor, top->close, name) : 0;
            const uint32_t inner = record_kind ? types->types.index[type_index(bare)] : 0;
            if (name == 0 && !bits && record_kind && depth < BUSTER_CHECK_DUMP_DEPTH)
            {
                // An anonymous struct or union: its fields, at this record's level.
                stack[depth++] = (Frame){ .tag = inner, .offset = offset, .depth = top->depth, .indent = top->indent, .anonymous = true,
                                          .close = builder->closes[inner], .cursor = builder->opens[inner] + 1 };
            }
            else if (name == 0 && !bits && record_kind)
            {
                builder->refusal = "__builtin_dump_struct of records nested more than 64 deep";
            }
            else if (name != 0)
            {
                const uint32_t field_depth = top->depth + 1;
                CheckLowerDumpOperand operands[5];
                uint32_t count = 0;
                char format[64] = "%s%s %s ";
                const uint32_t indent = check_lower_dump_indent(builder, field_depth);
                operands[count++] = check_lower_dump_operand_string(indent);
                operands[count++] = check_lower_dump_operand_string(check_lower_dump_type(builder, type, token));
                operands[count++] = check_lower_dump_operand_string(check_lower_dump_string(builder, (const char*)intern_bytes(program->identifiers, name),
                                                                                            intern_length(program->identifiers, name)));
                if (bits)
                {
                    strcat(format, ": %zu ");
                    operands[count++] = (CheckLowerDumpOperand){ .kind = CHECK_LOWER_DUMP_SIZE, .value = types->members.bit_width[member] };
                }
                strcat(format, "=");
                if (record_kind && depth < BUSTER_CHECK_DUMP_DEPTH && field_depth < BUSTER_CHECK_DUMP_DEPTH)
                {
                    check_lower_dump_line(builder, format, operands, count);
                    check_lower_dump_line(builder, " {\n", NULL, 0);
                    stack[depth++] = (Frame){ .tag = inner, .offset = offset, .depth = field_depth, .indent = indent, .close = builder->closes[inner],
                                              .cursor = builder->opens[inner] + 1 };
                }
                else if (record_kind)
                {
                    builder->refusal = "__builtin_dump_struct of records nested more than 64 deep";
                }
                else
                {
                    char specifier[40];
                    const uint32_t width = bits ? types->members.bit_width[member] : BUSTER_TYPE_NOT_BIT_FIELD;
                    if (check_lower_dump_specifier(builder, type, token, specifier))
                    {
                        strcat(format, " ");
                        strcat(format, specifier);
                        operands[count++] = (CheckLowerDumpOperand){ .kind = CHECK_LOWER_DUMP_VALUE, .value = offset, .type = bare, .width = width,
                                                                     .promoted = check_lower_dump_promoted(types, bare, width) };
                    }
                    else
                    {
                        strcat(format, " *%p");
                        operands[count++] = (CheckLowerDumpOperand){ .kind = CHECK_LOWER_DUMP_ADDRESS, .value = offset };
                    }
                    strcat(format, "\n");
                    check_lower_dump_line(builder, format, operands, count);
                }
            }
        }
    }
}

// The nodes of `root`'s tree, or UINT32_MAX for one of more than BUSTER_CHECK_DUMP_SUBTREE nodes or with a statement
// expression, which this lowering doesn't evaluate again.
static uint32_t check_lower_dump_nodes(const CheckGraph* graph, uint32_t root)
{
    uint32_t stack[BUSTER_CHECK_DUMP_SUBTREE];
    uint32_t depth = 0;
    uint32_t count = 0;
    bool valid = root < graph->count;
    if (valid)
    {
        stack[depth++] = root;
    }
    while (valid && depth != 0)
    {
        const uint32_t node = stack[--depth];
        count += 1;
        valid = count <= BUSTER_CHECK_DUMP_SUBTREE && graph->ops[node] != CHECK_OP_STATEMENT_VALUE;
        const uint32_t children[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t c = 0; valid && c < 3; c += 1)
        {
            valid = children[c] >= graph->count || depth < BUSTER_CHECK_DUMP_SUBTREE;
            if (valid && children[c] < graph->count)
            {
                stack[depth++] = children[c];
            }
        }
        const uint32_t first = graph->argument_offsets != NULL ? graph->argument_offsets[node] : 0;
        const uint32_t arguments = graph->argument_counts != NULL ? graph->argument_counts[node] : 0;
        for (uint32_t a = 0; valid && a < arguments; a += 1)
        {
            const uint32_t child = graph->argument_nodes[first + a];
            valid = child >= graph->count || depth < BUSTER_CHECK_DUMP_SUBTREE;
            if (valid && child < graph->count)
            {
                stack[depth++] = child;
            }
        }
    }
    return valid ? count : UINT32_MAX;
}

// A dump's plan for the call at `node` (its lines and image, or its refusal), and the room its calls take.
static bool check_lower_dump_plan(CheckLowerInitializers* pools, CheckLowerDumpBuilder* builder, CheckLowerDump* dump, uint32_t node, Arena* output)
{
    const CheckProgram* program = builder->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    *dump = (CheckLowerDump){ .node = node, .pool = BUSTER_CHECK_NONE };
    const TypeId pointer = count >= 2 ? type_strip_aligned(types, stream->argument_conversions[first].type != 0 ? stream->argument_conversions[first].type :
                                                                                                                check_stream_type(stream, graph->argument_nodes[first])) : 0;
    const uint8_t pointer_kind = pointer != 0 ? type_kind(types, pointer) : BUSTER_TYPE_KIND_NONE;
    const TypeId record = pointer_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(pointer_kind) ?
                          type_unqualified(type_strip_aligned(types, types->types.base[type_index(pointer)])) : 0;
    const uint8_t record_kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_NONE;
    uint64_t reevaluated = 0;
    for (uint32_t a = 1; a < count && reevaluated != UINT32_MAX; a += 1)
    {
        const uint32_t nodes = check_lower_dump_nodes(graph, graph->argument_nodes[first + a]);
        reevaluated = nodes == UINT32_MAX ? UINT32_MAX : reevaluated + nodes;
    }
    bool valid = true;
    builder->image_size = 0;
    builder->string_count = 0;
    builder->line_count = 0;
    builder->operand_count = 0;
    builder->refusal = NULL;
    if (record_kind != BUSTER_TYPE_KIND_STRUCT && record_kind != BUSTER_TYPE_KIND_UNION)
    {
        dump->refusal = "__builtin_dump_struct of no record";
    }
    else if (reevaluated == UINT32_MAX)
    {
        dump->refusal = "__builtin_dump_struct whose printer or other arguments have a statement expression or more than 4096 nodes, which its calls evaluate again";
    }
    else
    {
        check_lower_dump_record(builder, record);
        dump->refusal = builder->refusal;
    }
    if (dump->refusal == NULL)
    {
        uint8_t* image = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, builder->image_size);
        CheckLowerDumpLine* lines = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerDumpLine, builder->line_count);
        CheckLowerDumpOperand* operands = builder->operand_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerDumpOperand, builder->operand_count) : NULL;
        valid = image != NULL && lines != NULL && (builder->operand_count == 0 || operands != NULL);
        if (valid)
        {
            memcpy(image, builder->image, builder->image_size);
            memcpy(lines, builder->lines, builder->line_count * sizeof(*lines));
            for (uint32_t o = 0; o < builder->operand_count; o += 1)
            {
                operands[o] = builder->operands[o];
            }
            dump->lines = lines;
            dump->line_count = builder->line_count;
            dump->operands = operands;
            dump->pool = pools->count;
            pools->items[pools->count++] = (CheckLowerInitPool){ .bytes = image, .size = builder->image_size, .part = BUSTER_CHECK_NONE };
            // Each call evaluates the printer and the rest again (32 entries, 3 calls, 4 arguments and 6 locals a node at
            // most), and its own entries: the format's and each operand's address, load and conversion.
            const uint64_t lines_count = builder->line_count;
            pools->extra_entries += lines_count * (32 * reevaluated + 64) + (uint64_t)builder->operand_count * 48;
            pools->extra_calls += lines_count * (1 + 3 * reevaluated);
            pools->extra_arguments += lines_count * (count + 4 * reevaluated) + builder->operand_count;
            pools->extra_locals += 2 + lines_count * 6 * reevaluated + builder->operand_count;
        }
    }
    return valid;
}

// The plans of the unit's __builtin_dump_struct calls, and the room __builtin_os_log_format's data takes, evaluated once
// an item (check_lower_os_log). False where the arenas are full.
static bool check_lower_dump_prepare(CheckLowerInitializers* pools, const CheckProgram* program, Arena* output, Arena* scratch)
{
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = program->input != NULL ? &program->input->graph : NULL;
    uint32_t dumps = 0;
    bool valid = true;
    for (uint32_t i = 0; stream != NULL && graph != NULL && i < stream->misc_count; i += 1)
    {
        const CheckBuiltinMiscEffect* effect = stream->misc + i;
        const bool call = effect->node < graph->count && graph->ops[effect->node] == CHECK_OP_BUILTIN_MISC;
        dumps += call && effect->runtime_builtin == CBUILTIN_DUMP_STRUCT;
        if (call && effect->runtime_builtin == CBUILTIN_OS_LOG_FORMAT)
        {
            // Each item's data is evaluated as the item is stored.
            const CheckBuiltinCall* witness = check_graph_builtin_call(graph, effect->node);
            const uint32_t first = graph->argument_offsets[effect->node];
            const uint32_t count = graph->argument_counts[effect->node];
            for (uint32_t k = 0; witness != NULL && witness->os_log_known && k < witness->os_log_count; k += 1)
            {
                const CheckOSLogItem* item = witness->os_log_items + k;
                const uint32_t nodes = item->source == BUSTER_CHECK_OS_LOG_ARGUMENT && 2 + item->argument < count ?
                                       check_lower_dump_nodes(graph, graph->argument_nodes[first + 2 + item->argument]) : 0;
                const uint64_t bounded = nodes == UINT32_MAX ? BUSTER_CHECK_DUMP_SUBTREE : nodes;
                pools->extra_entries += 32 * bounded + 32;
                pools->extra_calls += 3 * bounded;
                pools->extra_arguments += 4 * bounded;
                pools->extra_locals += 6 * bounded + 2;
            }
        }
    }
    if (dumps != 0)
    {
        const uint64_t mark = scratch->length;
        const TypeTable* types = program->types;
        const InternTable* identifiers = program->identifiers;
        const uint32_t name_count = identifiers->count;
        CheckLowerDumpBuilder builder = {
            .program = program,
            .scratch = scratch,
            .options = program->parser->language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0,
            .c99 = program->parser->language.standard >= 1999,
        };
        const uint8_t** bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, name_count);
        uint32_t* lengths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, name_count);
        String8* anonymous = BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)types->tags.count + 1);
        builder.opens = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)types->tags.count + 1);
        builder.closes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)types->tags.count + 1);
        builder.image = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, BUSTER_CHECK_DUMP_IMAGE);
        builder.strings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_CHECK_DUMP_LINES * 6);
        builder.lines = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerDumpLine, BUSTER_CHECK_DUMP_LINES);
        builder.operands = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerDumpOperand, BUSTER_CHECK_DUMP_LINES * 5);
        CheckLowerInitPool* items = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerInitPool, (uint64_t)pools->count + dumps);
        pools->dumps = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerDump, dumps);
        valid = bytes != NULL && lengths != NULL && anonymous != NULL && builder.opens != NULL && builder.closes != NULL && builder.image != NULL && builder.strings != NULL && builder.lines != NULL &&
                builder.operands != NULL && items != NULL && pools->dumps != NULL;
        if (valid)
        {
            memset(bytes, 0, (uint64_t)name_count * sizeof(*bytes));
            memset(lengths, 0, (uint64_t)name_count * sizeof(*lengths));
            memset(anonymous, 0, ((uint64_t)types->tags.count + 1) * sizeof(*anonymous));
            for (uint32_t tag = 0; tag <= types->tags.count; tag += 1)
            {
                if (tag == types->tags.count || !check_lower_dump_body(program, tag, builder.opens + tag, builder.closes + tag))
                {
                    builder.opens[tag] = BUSTER_CHECK_NONE;
                    builder.closes[tag] = BUSTER_CHECK_NONE;
                }
            }
            for (uint32_t tag = 0; tag < types->tags.count; tag += 1)
            {
                const uint32_t name = types->tags.name[tag];
                if (name != 0 && name < name_count)
                {
                    bytes[name] = intern_bytes(identifiers, name);
                    lengths[name] = intern_length(identifiers, name);
                }
            }
            // An unnamed tag prints as the typedef that names it (the first that declares its type, clang's typedef name
            // for linkage), or as "(unnamed)", clang's AnonymousTagMode::Plain.
            // A member's declarator declares itself too, of a kind byte left 0, TYPEDEF's: a typedef's name has the typedef
            // keyword among its declaration's specifiers (check_lower_dump_typedef).
            const CheckInput* input = program->input;
            const uint32_t tokens = input->token_count < program->parser->tokens.count ? input->token_count : program->parser->tokens.count;
            for (uint32_t t = 0; input->declaration_kinds != NULL && input->declared_types != NULL && t < tokens; t += 1)
            {
                if (input->declaration_kinds[t] == C_BINDING_TYPEDEF && input->declarations[t] == t)
                {
                    const TypeId declared = type_strip_aligned(types, input->declared_types[t]);
                    const uint8_t kind = type_is_valid(types, declared) ? type_kind(types, declared) : BUSTER_TYPE_KIND_NONE;
                    const uint32_t tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM ?
                                         types->types.index[type_index(declared)] : UINT32_MAX;
                    const uint32_t identifier = program->parser->tokens.names[t];
                    if (tag < types->tags.count && types->tags.name[tag] == 0 && anonymous[tag].length == 0 && identifier < name_count &&
                        check_lower_dump_typedef(program->parser, t))
                    {
                        anonymous[tag] = (String8){ .pointer = (char*)intern_bytes(identifiers, identifier), .length = intern_length(identifiers, identifier) };
                    }
                }
            }
            builder.names = (TypeNames){ .bytes = bytes, .lengths = lengths, .count = name_count, .anonymous = anonymous, .anonymous_count = types->tags.count };
            if (pools->count != 0)
            {
                memcpy(items, pools->items, (uint64_t)pools->count * sizeof(*items));
            }
            pools->items = items;
        }
        for (uint32_t i = 0; valid && i < stream->misc_count; i += 1)
        {
            const CheckBuiltinMiscEffect* effect = stream->misc + i;
            if (effect->node < graph->count && graph->ops[effect->node] == CHECK_OP_BUILTIN_MISC && effect->runtime_builtin == CBUILTIN_DUMP_STRUCT)
            {
                valid = check_lower_dump_plan(pools, &builder, pools->dumps + pools->dump_count, effect->node, output);
                pools->dump_count += valid;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// The plan of the __builtin_dump_struct call at `node`, or NULL: a unit has few of them.
static const CheckLowerDump* check_lower_dump_find(const CheckLowerInitializers* pools, uint32_t node)
{
    const CheckLowerDump* result = NULL;
    for (uint32_t d = 0; result == NULL && d < pools->dump_count; d += 1)
    {
        result = pools->dumps[d].node == node ? pools->dumps + d : NULL;
    }
    return result;
}
