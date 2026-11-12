// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's cpu_specific and cpu_dispatch function multiversioning (#1447), as clang 23's Sema checks it on x86-64 Linux
// (handleCPUSpecificAttr in SemaDeclAttr.cpp, CheckMultiVersionFunction and its helpers in SemaDecl.cpp, at
// llvmorg-23.1.4, read for their rules and written again in C). Include from c_check_attribute.c, after the
// multiversioning attribute checks (#839).
//
// - Each CPU name an attribute takes is a processor of c_check_cpu_dispatch.inc (derived LLVM data): an unknown one is
//   clang's "invalid option 'X' for cpu_specific" (or cpu_dispatch) at it, and two names of one mangling character are
//   -Wfunction-multiversion's "CPU list contains duplicate entries; attribute ignored" at the attribute. Either drops the
//   attribute, as clang's handler returns before adding it.
// - A declaration with one is a version of its function (cpu_specific) or its dispatcher (cpu_dispatch). A declaration of
//   the same kind and CPU list (names compared in order) redeclares that version; another one, of the other kind or of
//   another list, is a version of its own, which shares the function's name but not its entity: c_check_input.c's
//   binding keeps its object apart (check_input_cpu_dispatch_match). Each keeps clang's rules against the declaration
//   before it (check_input_cpu_dispatch_declaration).
//
// These declarations are rare (a unit without the attributes never calls this file's functions), each check reads one
// declaration's few attributes and the chain of its name's few earlier declarations, and the CPU table is searched by
// name for each argument: scalar, as a vector pass over tens of tokens would cost more than it saves.
#ifndef BUSTER_C_CHECK_CPU_DISPATCH_INCLUDED
#define BUSTER_C_CHECK_CPU_DISPATCH_INCLUDED

// A processor clang's cpu_specific and cpu_dispatch take (c_check_cpu_dispatch.inc): its name, its mangling character
// (0 for none, which clang spells as an empty suffix), the mask of libgcc's CPU model a resolver tests it with (word 0 at
// __cpu_model + 12, words 1 to 3 __cpu_features2[0] to [2]) and the features its version's body is compiled with,
// comma-separated, as a target attribute's.
typedef struct CheckCpuDispatchRow CheckCpuDispatchRow;
struct CheckCpuDispatchRow
{
    const char* name;
    char mangling;
    uint32_t mask[4];
    const char* features;
};

static const CheckCpuDispatchRow check_cpu_dispatch_rows[] =
{
#define BUSTER_CHECK_CPU_DISPATCH(NAME, MANGLING, WORD0, WORD1, WORD2, WORD3, FEATURES) { NAME, MANGLING, { WORD0, WORD1, WORD2, WORD3 }, FEATURES },
#include "c_check_cpu_dispatch.inc"
#undef BUSTER_CHECK_CPU_DISPATCH
};

// A cpu_specific or cpu_dispatch declaration's error (CHECK_INPUT_CPU_DISPATCH's detail), each clang's message.
typedef enum CheckCpuDispatchError
{
    // "invalid option 'X' for cpu_specific" (or cpu_dispatch), at the CPU's token, which `attribute` names; `value` 1 for
    // cpu_dispatch.
    CHECK_CPU_DISPATCH_INVALID_OPTION,
    // "'main' cannot be a multiversioned function".
    CHECK_CPU_DISPATCH_MAIN,
    // "multiversioned function must have a prototype", with clang's caused-here note where an earlier declaration lacks it.
    CHECK_CPU_DISPATCH_PROTOTYPE,
    // "function declaration is missing 'cpu_specific' or 'cpu_dispatch' attribute in a multiversioned function".
    CHECK_CPU_DISPATCH_REQUIRED,
    // "multiversioning attributes cannot be combined", with a note at the previous declaration.
    CHECK_CPU_DISPATCH_MIXED,
    // "'cpu_dispatch' function redeclared with different CPUs", with a note at the previous declaration.
    CHECK_CPU_DISPATCH_MISMATCH,
    // "multiple 'cpu_specific' functions cannot specify the same CPU: 'X'", the CPU's token in `attribute`, with a note
    // at the previous declaration.
    CHECK_CPU_DISPATCH_SAME_CPU,
    // "multiversioned function declaration has a different return type", "... inline specification", "... linkage".
    CHECK_CPU_DISPATCH_RETURN_TYPE,
    CHECK_CPU_DISPATCH_INLINE,
    CHECK_CPU_DISPATCH_LINKAGE,
    // Multiversioning by target strings' (#1838, c_check_target_versions.c): "function multiversioning doesn't support
    // feature 'X'" (or "architecture"), `attribute` the target string's first token and `value` the item's bytes in its
    // decoded text (start, and length above bit 32), with the caused-here note where an earlier declaration has it;
    CHECK_CPU_DISPATCH_TARGET_FEATURE,
    CHECK_CPU_DISPATCH_TARGET_ARCHITECTURE,
    // "multiversioned function redeclarations require identical target attributes", with a note at the previous
    // declaration;
    CHECK_CPU_DISPATCH_TARGET_DUPLICATE,
    // "function declaration cannot become a multiversioned function after first usage", with a note at the previous
    // declaration where `note_token` has one;
    CHECK_CPU_DISPATCH_AFTER_USE,
    // "function declaration is missing 'target' attribute in a multiversioned function", with the caused-here note where
    // `note_token` has one;
    CHECK_CPU_DISPATCH_TARGET_REQUIRED,
    // "'target_clones' attribute does not match previous declaration", with a note at the previous declaration;
    CHECK_CPU_DISPATCH_CLONES_MISMATCH,
    // "no matching function for call to 'f'", at a call's callee where no default version is visible.
    CHECK_CPU_DISPATCH_NO_MATCH,
} CheckCpuDispatchError;

// A declaration's multiversioning by these attributes: none, a version (cpu_specific) or the dispatcher (cpu_dispatch).
typedef enum CheckCpuDispatchKind
{
    CHECK_CPU_DISPATCH_NONE,
    CHECK_CPU_DISPATCH_SPECIFIC,
    CHECK_CPU_DISPATCH_DISPATCH,
} CheckCpuDispatchKind;

// The row of the processor `name` names, or -1.
static int32_t check_cpu_dispatch_row(String8 name)
{
    int32_t result = -1;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(check_cpu_dispatch_rows) && result < 0; r += 1)
    {
        const char* row = check_cpu_dispatch_rows[r].name;
        result = strlen(row) == name.length && memcmp(row, name.pointer, name.length) == 0 ? (int32_t)r : -1;
    }
    return result;
}

// The declaration's first cpu_specific or cpu_dispatch attribute that isn't ignored, or NULL.
static const CheckInputAttribute* check_cpu_dispatch_attribute(const CheckInputAttributes* attributes)
{
    const CheckInputAttribute* result = NULL;
    for (uint32_t i = 0; attributes != NULL && result == NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        result = !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH) ? attribute : NULL;
    }
    return result;
}

static CheckCpuDispatchKind check_cpu_dispatch_kind(const CheckInputAttributes* attributes)
{
    const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(attributes);
    return attribute == NULL ? CHECK_CPU_DISPATCH_NONE : attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ? CHECK_CPU_DISPATCH_SPECIFIC : CHECK_CPU_DISPATCH_DISPATCH;
}

// The CPUs' tokens of `attribute`, an identifier list's arguments, in order: `count` of them, the first at
// argument_first and each after the comma after the one before.
static uint32_t check_cpu_dispatch_next(const Parser* parser, const CheckInputAttribute* attribute, uint32_t token)
{
    uint32_t next = token + 1;
    while (next < attribute->argument_end && parser->tokens.kinds[next] != PARSE_KIND_IDENTIFIER)
    {
        next += 1;
    }
    return next;
}

static uint32_t check_cpu_dispatch_first(const Parser* parser, const CheckInputAttribute* attribute)
{
    return attribute->argument_first < attribute->argument_end && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER ?
               attribute->argument_first : check_cpu_dispatch_next(parser, attribute, attribute->argument_first);
}

// clang's handleCPUSpecificAttr, after the attribute's subject and count were checked: each CPU a processor's name, and
// no two of one mangling character. An unknown name is its error at the name, a repeated character its warning at the
// attribute; either drops the attribute. Whether no error was reported.
static bool check_input_cpu_dispatch_options(CheckInputReader* reader, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    // The manglings seen, one bit per character ('A' to 'z') and bit 63 for none.
    uint64_t seen = 0;
    for (uint32_t token = check_cpu_dispatch_first(parser, attribute); !attribute->ignored && token < attribute->argument_end;
         token = check_cpu_dispatch_next(parser, attribute, token))
    {
        const int32_t row = check_cpu_dispatch_row(check_input_attribute_name(reader, parser->tokens.names[token]));
        if (row < 0)
        {
            const uint32_t before = reader->output->report_count;
            check_input_report(reader, token, CHECK_INPUT_CPU_DISPATCH);
            if (reader->output->report_count != before)
            {
                reader->output->reports[before].detail = CHECK_CPU_DISPATCH_INVALID_OPTION;
                reader->output->reports[before].attribute = token;
                reader->output->reports[before].value = attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH;
                reader->output->reports[before].completion = reader->attribute_completion;
            }
            attribute->ignored = true;
            valid = false;
        }
        else
        {
            const char mangling = check_cpu_dispatch_rows[row].mangling;
            const uint64_t bit = mangling != 0 ? UINT64_C(1) << ((uint32_t)(mangling - 'A') & 63) : UINT64_C(1) << 63;
            if ((seen & bit) != 0)
            {
                check_input_warning(reader, CHECK_WARNING_ID_FUNCTION_MULTIVERSION, CHECK_WARNING_EVENT_CPU_LIST_DUPLICATE, attribute->name_token,
                                    CHECK_WARNING_COMPLETE_AT_TOKEN, reader->attribute_completion != 0 ? reader->attribute_completion : attribute->name_token + 1);
                attribute->ignored = true;
            }
            seen |= bit;
        }
    }
    return valid;
}

// Whether the CPU lists of `a` and `b` are the same names in the same order (clang's std::equal of their names).
static bool check_cpu_dispatch_same_list(CheckInputReader* reader, const CheckInputAttribute* a, const CheckInputAttribute* b)
{
    const Parser* parser = reader->parser;
    uint32_t x = check_cpu_dispatch_first(parser, a);
    uint32_t y = check_cpu_dispatch_first(parser, b);
    bool same = true;
    while (same && x < a->argument_end && y < b->argument_end)
    {
        same = parser->tokens.names[x] == parser->tokens.names[y];
        x = check_cpu_dispatch_next(parser, a, x);
        y = check_cpu_dispatch_next(parser, b, y);
    }
    return same && x >= a->argument_end && y >= b->argument_end;
}

// Whether declarations `a` and `b` are of one version (or both the dispatcher): the same kind and CPU list.
static bool check_cpu_dispatch_same_version(CheckInputReader* reader, uint32_t a, uint32_t b)
{
    const CheckInputAttribute* x = check_cpu_dispatch_attribute(check_input_attributes_at(reader->output, a));
    const CheckInputAttribute* y = check_cpu_dispatch_attribute(check_input_attributes_at(reader->output, b));
    return x != NULL && y != NULL && x->kind == y->kind && check_cpu_dispatch_same_list(reader, x, y);
}

// Whether `declaration` is one clang made invalid (CheckInputReader.cpu_invalid).
static bool check_cpu_dispatch_invalid(const CheckInputReader* reader, uint32_t declaration)
{
    bool invalid = false;
    for (uint32_t i = 0; !invalid && i < reader->cpu_invalid_count; i += 1)
    {
        invalid = reader->cpu_invalid[i] == declaration;
    }
    return invalid;
}

// Records `declaration` as invalid.
static void check_cpu_dispatch_invalidate(CheckInputReader* reader, uint32_t declaration)
{
    if (reader->cpu_invalid_count == reader->cpu_invalid_capacity)
    {
        const uint32_t capacity = reader->cpu_invalid_capacity != 0 ? reader->cpu_invalid_capacity * 2 : 8;
        uint32_t* grown = capacity > reader->cpu_invalid_capacity ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity) : NULL;
        reader->output->resource |= grown == NULL;
        if (grown != NULL && reader->cpu_invalid_count != 0)
        {
            memcpy(grown, reader->cpu_invalid, (uint64_t)reader->cpu_invalid_count * sizeof(uint32_t));
        }
        reader->cpu_invalid = grown != NULL ? grown : reader->cpu_invalid;
        reader->cpu_invalid_capacity = grown != NULL ? capacity : reader->cpu_invalid_capacity;
    }
    if (reader->cpu_invalid_count < reader->cpu_invalid_capacity)
    {
        reader->cpu_invalid[reader->cpu_invalid_count++] = declaration;
    }
}

// The most recent declaration of a function along the name's chain from binding `handle` that clang didn't make invalid,
// or BUSTER_CHECK_NONE where the most recent valid one isn't a function's.
static uint32_t check_cpu_dispatch_visible(const CheckInputReader* reader, uint32_t handle)
{
    uint32_t result = BUSTER_CHECK_NONE;
    bool found = false;
    for (uint32_t h = handle; !found && h != 0; h = reader->bindings.previous[h - 1])
    {
        const uint32_t declaration = reader->bindings.declaration[h - 1];
        found = declaration >= reader->parser->tokens.count || !check_cpu_dispatch_invalid(reader, declaration);
        result = found && declaration < reader->parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION ? declaration : BUSTER_CHECK_NONE;
    }
    return result;
}

// Appends a declaration's error of `detail` at `token`, with `note` at `note_token`, to the deferred multiversioning
// reports (#839's, published after the unit's bodies' checks, in discovery order).
static void check_input_cpu_dispatch_report(CheckInputReader* reader, uint32_t token, CheckCpuDispatchError detail, uint32_t note_token, CheckInputNoteKind note,
                                            uint32_t cpu)
{
    const uint32_t before = reader->output->multiversion_report_count;
    check_input_report_multiversion(reader, token, CHECK_INPUT_CPU_DISPATCH, 0, cpu, note_token, note, 0);
    if (reader->output->multiversion_report_count != before)
    {
        CheckInputReport* report = reader->output->multiversion_reports + before;
        report->detail = detail;
        report->completion = reader->attribute_completion;
    }
}

// The previous declarations of a name that clang's lookup finds when a later declaration of it is checked: the most
// recent declaration of each entity, most recent first, from the binding `handle` back (c_bindings' chain); `count`
// visited, each a function's.
static uint32_t check_cpu_dispatch_previous(CheckInputReader* reader, uint32_t handle, uint32_t* visited, uint32_t capacity)
{
    uint32_t count = 0;
    for (uint32_t h = handle; h != 0 && count < capacity; h = reader->bindings.previous[h - 1])
    {
        const uint32_t declaration = reader->bindings.declaration[h - 1];
        bool seen = declaration >= reader->parser->tokens.count || reader->declaration_kinds[declaration] != C_BINDING_FUNCTION ||
                    check_cpu_dispatch_invalid(reader, declaration);
        for (uint32_t v = 0; !seen && v < count; v += 1)
        {
            seen = reader->objects[visited[v]] == reader->objects[declaration];
        }
        if (!seen)
        {
            visited[count++] = declaration;
        }
    }
    return count;
}

// clang's CheckMultiVersionFunction for a function's declaration at `name`, of `type` and `attributes`, where either it or
// `previous` (the name's visible declaration of a function before it, or BUSTER_CHECK_NONE; `handle` its binding) is
// multiversioned by cpu_specific or cpu_dispatch: main's, the prototype's, the attribute a redeclaration of a version
// misses, a mix with target multiversioning, a dispatcher's other CPUs, a version's CPU another version has, and the
// return type, inline specification and linkage every version shares with the declaration before it. A declaration of
// cpu_dispatch with a body (its `{` at `brace`) has -Wfunction-multiversion's warning at its first statement. A declaration with an error is
// invalid (CheckInputReader.cpu_invalid), which the later declarations' rules skip and binding keeps apart, as clang's
// lookup does; its body is checked all the same.
static void check_input_cpu_dispatch_declaration(CheckInputReader* reader, const CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t handle,
                                                 bool is_inline, bool is_static, bool main, uint32_t brace)
{
    const Parser* parser = reader->parser;
    const uint32_t previous = check_cpu_dispatch_visible(reader, handle);
    const TypeTable* types = reader->types;
    const CheckCpuDispatchKind kind = check_cpu_dispatch_kind(attributes);
    const bool function_previous = previous < parser->tokens.count && reader->declaration_kinds[previous] == C_BINDING_FUNCTION;
    const CheckInputAttributes* previous_attributes = function_previous ? check_input_attributes_at(reader->output, previous) : NULL;
    const CheckCpuDispatchKind previous_kind = check_cpu_dispatch_kind(previous_attributes);
    // A target attribute's multiversioning kind (#839's): target_clones, or any target attribute, which makes a declaration
    // after a version's a version of target's.
    bool target = false;
    for (uint32_t i = 0; kind == CHECK_CPU_DISPATCH_NONE && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        target |= !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_TARGET || attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES);
    }
    const bool previous_target = previous_kind == CHECK_CPU_DISPATCH_NONE && check_input_attribute_multiversion_present(previous_attributes);
    const uint32_t reports = reader->output->multiversion_report_count;
    bool valid = true;
    if (main && kind != CHECK_CPU_DISPATCH_NONE)
    {
        check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_MAIN, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
        valid = false;
    }
    else if (!function_previous && kind != CHECK_CPU_DISPATCH_NONE)
    {
        // The function's first declaration (CheckMultiVersionFirstFunction).
        if ((types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_PROTOTYPE, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
            valid = false;
        }
    }
    else if (function_previous && previous_kind != CHECK_CPU_DISPATCH_NONE && kind == CHECK_CPU_DISPATCH_NONE && !target)
    {
        check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_REQUIRED, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
        valid = false;
    }
    else if (function_previous && ((previous_kind != CHECK_CPU_DISPATCH_NONE && target) || (previous_target && kind != CHECK_CPU_DISPATCH_NONE)))
    {
        check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_MIXED, previous, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
        valid = false;
    }
    else if (function_previous && kind != CHECK_CPU_DISPATCH_NONE)
    {
        // CheckMultiVersionAdditionalDecl: the earlier versions, most recent first, then the rules against the
        // declaration before it.
        uint32_t visited[64];
        const uint32_t count = check_cpu_dispatch_previous(reader, handle, visited, BUSTER_ARRAY_LENGTH(visited));
        const CheckInputAttribute* own = check_cpu_dispatch_attribute(attributes);
        bool redeclaration = false;
        for (uint32_t v = 0; valid && !redeclaration && v < count; v += 1)
        {
            const uint32_t current = visited[v];
            const CheckInputAttribute* other = check_cpu_dispatch_attribute(check_input_attributes_at(reader->output, current));
            if (other != NULL && other->kind == own->kind)
            {
                redeclaration = check_cpu_dispatch_same_list(reader, own, other);
                if (!redeclaration && kind == CHECK_CPU_DISPATCH_DISPATCH)
                {
                    check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_MISMATCH, current, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
                    valid = false;
                }
                for (uint32_t x = check_cpu_dispatch_first(parser, other); valid && !redeclaration && x < other->argument_end; x = check_cpu_dispatch_next(parser, other, x))
                {
                    for (uint32_t y = check_cpu_dispatch_first(parser, own); valid && y < own->argument_end; y = check_cpu_dispatch_next(parser, own, y))
                    {
                        if (parser->tokens.names[x] == parser->tokens.names[y])
                        {
                            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_SAME_CPU, current, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, y);
                            valid = false;
                        }
                    }
                }
            }
        }
        // CheckMultiVersionAdditionalRules against the declaration before it (areMultiversionVariantFunctionsCompatible).
        const bool causes = previous_kind == CHECK_CPU_DISPATCH_NONE;
        const TypeId previous_type = reader->declared_types[previous];
        const uint8_t previous_flags = reader->output->function_flags != NULL ? reader->output->function_flags[previous] : 0;
        const bool previous_static = check_input_linkage(reader->declaration_bits, previous) == 1;
        if (valid && !redeclaration && causes && check_input_attribute_multiversion_disallowed(previous_attributes, kind == CHECK_CPU_DISPATCH_SPECIFIC ? CHECK_MULTIVERSION_CPU_SPECIFIC : CHECK_MULTIVERSION_CPU_DISPATCH,
                                                                                               NULL, BUSTER_CHECK_NONE, (previous_flags & CHECK_INPUT_FUNCTION_INLINE) != 0) != BUSTER_CHECK_NONE)
        {
            const uint32_t bad = check_input_attribute_multiversion_disallowed(previous_attributes, kind == CHECK_CPU_DISPATCH_SPECIFIC ? CHECK_MULTIVERSION_CPU_SPECIFIC : CHECK_MULTIVERSION_CPU_DISPATCH,
                                                                               NULL, BUSTER_CHECK_NONE, (previous_flags & CHECK_INPUT_FUNCTION_INLINE) != 0);
            check_input_report_multiversion(reader, previous, CHECK_INPUT_MULTIVERSION_ATTRIBUTE,
                                            kind == CHECK_CPU_DISPATCH_SPECIFIC ? CHECK_MULTIVERSION_CPU_SPECIFIC : CHECK_MULTIVERSION_CPU_DISPATCH,
                                            previous_attributes->records[bad].name_token, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, 0);
            valid = false;
        }
        else if (valid && !redeclaration && causes && (types->types.flags[type_index(previous_type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
        {
            check_input_cpu_dispatch_report(reader, previous, CHECK_CPU_DISPATCH_PROTOTYPE, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (valid && !redeclaration && (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_PROTOTYPE, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (valid && !redeclaration)
        {
            const TypeAnswer same = type_same_canonical(types, type_return(types, previous_type), type_return(types, type));
            reader->output->resource |= same == TYPE_ANSWER_FULL;
            const CheckCpuDispatchError different = same != TYPE_ANSWER_YES                                         ? CHECK_CPU_DISPATCH_RETURN_TYPE :
                                                    ((previous_flags & CHECK_INPUT_FUNCTION_INLINE) != 0) != is_inline ? CHECK_CPU_DISPATCH_INLINE :
                                                    previous_static != is_static                                     ? CHECK_CPU_DISPATCH_LINKAGE :
                                                                                                                       CHECK_CPU_DISPATCH_INVALID_OPTION;
            if (different != CHECK_CPU_DISPATCH_INVALID_OPTION)
            {
                check_input_cpu_dispatch_report(reader, name, different, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
                valid = false;
            }
        }
    }
    if (!valid || reader->output->multiversion_report_count != reports)
    {
        check_cpu_dispatch_invalidate(reader, name);
    }
    // The body's warning, at its first statement, once the body is checked (clang's ActOnFinishFunctionBody): a valid
    // dispatcher's, of a body with a statement.
    const uint32_t close = brace != BUSTER_CHECK_NONE ? parser->tree.parents[brace] : BUSTER_CHECK_NONE;
    if (valid && reader->output->multiversion_report_count == reports && kind == CHECK_CPU_DISPATCH_DISPATCH && brace != BUSTER_CHECK_NONE && brace + 1 < close)
    {
        check_input_warning(reader, CHECK_WARNING_ID_FUNCTION_MULTIVERSION, CHECK_WARNING_EVENT_CPU_DISPATCH_BODY, brace + 1, CHECK_WARNING_COMPLETE_AT_TOKEN, close + 1);
    }
}

// For check_input_bind: the earlier declaration a cpu_specific or cpu_dispatch declaration `token` redeclares (one of its
// version: the same kind and CPU list), found along its name's chain from binding `handle` (the most recent first), or
// BUSTER_CHECK_NONE. *separate is whether the declaration is a version apart from the visible declaration before it: it
// has the attribute and the earlier one does too, of another version, or it has none and the earlier one has one (an
// error already). A declaration of the attribute after a plain one of its function redeclares that one, as clang merges
// them, and is no separate version.
static uint32_t check_input_cpu_dispatch_match(CheckInputReader* reader, uint32_t token, uint32_t handle, bool* separate)
{
    const bool invalid = check_cpu_dispatch_invalid(reader, token);
    const uint32_t previous = check_cpu_dispatch_visible(reader, handle);
    const bool function_previous = previous < reader->parser->tokens.count;
    const CheckCpuDispatchKind kind = check_cpu_dispatch_kind(check_input_attributes_at(reader->output, token));
    const CheckCpuDispatchKind previous_kind = function_previous ? check_cpu_dispatch_kind(check_input_attributes_at(reader->output, previous)) : CHECK_CPU_DISPATCH_NONE;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t h = handle; !invalid && kind != CHECK_CPU_DISPATCH_NONE && previous_kind != CHECK_CPU_DISPATCH_NONE && result == BUSTER_CHECK_NONE && h != 0;
         h = reader->bindings.previous[h - 1])
    {
        const uint32_t declaration = reader->bindings.declaration[h - 1];
        result = declaration < reader->parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                 !check_cpu_dispatch_invalid(reader, declaration) && check_cpu_dispatch_same_version(reader, token, declaration) ? declaration : BUSTER_CHECK_NONE;
    }
    *separate = invalid || previous_kind != CHECK_CPU_DISPATCH_NONE;
    return result;
}

static void check_input_attribute_late(CheckInputReader* reader, uint32_t token, uint32_t previous, uint32_t point, bool visible, bool redefinition);
static void check_input_invalid_declare(CheckInputReader* reader, uint32_t token);

// The redeclaration `token` of the version declared at `version` (#1447), which binding keeps apart from other versions:
// its object; clang's redefinition error where both define it, at `token` with its note at the version's definition;
// otherwise the definition it inherits and the composite type, or clang's conflicting-types error where the types aren't
// compatible. A redefinition is clang's invalid declaration (#2362): its error is recovered, and it stays bound, the
// declarations after it while it is visible invalid with it and quiet (check_input_bind); one after another version
// redeclares it as any.
static void check_input_cpu_dispatch_redeclare(CheckInputReader* reader, uint32_t token, uint32_t version, uint32_t point)
{
    reader->objects[token] = reader->objects[version];
    const uint32_t definition = reader->definition_origins[version];
    const bool redefinition = reader->definition_origins[token] != 0 && definition != 0;
    // clang merges the redeclaration's attributes into its version's (mergeDeclAttributes): those after the version's
    // definition are dropped with their warning, a redefinition's before its error (#1920).
    check_input_attribute_late(reader, token, version, point, true, redefinition);
    if (redefinition)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_subject(reader, token, CHECK_INPUT_REDECLARATION, version);
        if (reader->output->report_count == before + 1)
        {
            reader->output->reports[before].note_token = definition - 1;
            reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
            // (Not a lane's: an invalid declaration's uses are quiet in source order.)
            if (!reader->lane && check_input_recover_reports(reader, before))
            {
                check_input_invalid_declare(reader, token);
            }
        }
    }
    else
    {
        reader->definition_origins[token] = reader->definition_origins[token] != 0 ? reader->definition_origins[token] : definition;
        const TypeAnswer compatible = type_compatible(reader->types, reader->declared_types[version], reader->declared_types[token]);
        reader->output->resource |= compatible == TYPE_ANSWER_FULL;
        if (compatible != TYPE_ANSWER_YES)
        {
            check_input_report_subject(reader, token, CHECK_INPUT_CONFLICTING_TYPES, version);
        }
        else
        {
            const TypeId composite = check_input_type_result(reader, type_composite(reader->types, reader->declared_types[version], reader->declared_types[token]));
            reader->declared_types[token] = composite != 0 ? composite : reader->declared_types[token];
        }
        if (reader->output->function_flags != NULL)
        {
            reader->output->function_flags[token] |= reader->output->function_flags[version] & (CHECK_INPUT_FUNCTION_EXTERNAL_DEFINITION | CHECK_INPUT_FUNCTION_NORETURN);
        }
    }
}

#endif
