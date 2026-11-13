// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A checked, complete-declaration prefix of a failed parse, for diagnostic ordering only. Scalar recovery replaces
// the entire malformed top-level declaration with ERROR leaves and removes its bindings. Earlier declarations keep
// their trees, but parse_translation_unit deliberately omits postorder/sizes after any error. This adapter validates
// the surviving forest and builds those two columns without modifying or reparsing the original input.
//
// A READY prefix is not acceptance of the translation unit: the caller must retain every original parse diagnostic
// and its failing status. In particular, an earlier semantic error inside the malformed declaration is unavailable;
// recovery erased the whole declaration. A complete declaration whose only errors are unknown type specifiers or
// missing parameter types can retain its tree and use int for those types, as clang's recovery does. The prefix never guesses
// missing punctuation or revives erased nodes.
// Elaborate it with check_input_prefix, not check_input: tentative objects may acquire a complete type later in the
// original unit, so end-of-translation-unit obligations must not run on this view. Operation-site checks still run.
// Include after c_parser.c. The view borrows no owning Parser arenas: release the caller's output arena, never call
// parse_release on the view. Its source indices refer to the original preprocessor output and its token indices stay
// unchanged. Output and scratch are distinct arenas. Output storage must remain alive through checking; all scratch
// storage is restored on every outcome.

#ifndef BUSTER_CHECK_PREFIX_IMPLEMENTATION
#define BUSTER_CHECK_PREFIX_IMPLEMENTATION 1

typedef enum CheckPrefixStatus
{
    CHECK_PREFIX_READY,
    CHECK_PREFIX_EMPTY,
    CHECK_PREFIX_UNAVAILABLE,
    CHECK_PREFIX_INVALID,
    CHECK_PREFIX_RESOURCE,
} CheckPrefixStatus;

typedef struct CheckPrefix CheckPrefix;
struct CheckPrefix
{
    Parser parser;
    CheckPrefixStatus status;
    // The first original parse error's diagnostic index, or BUSTER_PARSE_NONE. Warnings do not cut the prefix.
    uint32_t diagnostic;
    // The retained half-open token range is [0, end). On failure it is empty.
    uint32_t end;
};

typedef struct CheckPrefixFrame CheckPrefixFrame;
struct CheckPrefixFrame
{
    uint32_t token;
    uint32_t child;
    uint32_t first;
    bool emitted;
};

static void* check_prefix_column(Arena* arena, uint32_t count, uint32_t size)
{
    const uint64_t slots = (uint64_t)count + 2 * BUSTER_PARSE_TOKEN_PADDING;
    uint8_t* bytes = arena_push_array(arena, slots, size, 64);
    if (bytes != 0)
    {
        memset(bytes, 0, slots * size);
    }
    return bytes != 0 ? bytes + BUSTER_PARSE_TOKEN_PADDING * size : 0;
}

// Bounded explicit DFS proves both the forest and its contiguous source intervals. Visiting children in token order
// must reproduce 0..count-1 in order when each parent's own token is inserted among them. That rules out interleaved
// subtrees, cycles, missing nodes and multiple visits before any untrusted postorder is given to the checker.
static CheckPrefixStatus check_prefix_order(Parser* view, Arena* scratch)
{
    const uint32_t count = view->tokens.count;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count);
    CheckPrefixFrame* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckPrefixFrame, slots);
    CheckPrefixStatus status = first != 0 && next != 0 && seen != 0 && stack != 0 ? CHECK_PREFIX_READY : CHECK_PREFIX_RESOURCE;
    if (status == CHECK_PREFIX_READY)
    {
        memset(first, 0xff, slots * sizeof(uint32_t));
        memset(seen, 0, count);
        for (uint32_t token = count; token-- > 0;)
        {
            const uint32_t parent = view->tree.parents[token];
            const uint32_t slot = parent == BUSTER_PARSE_ROOT ? count : parent;
            next[token] = first[slot];
            first[slot] = token;
        }
        uint32_t position = 0;
        uint32_t source = 0;
        for (uint32_t root = first[count]; status == CHECK_PREFIX_READY && root != BUSTER_PARSE_NONE; root = next[root])
        {
            uint32_t depth = 1;
            seen[root] = 1;
            stack[0] = (CheckPrefixFrame){ .token = root, .child = first[root], .first = position };
            while (status == CHECK_PREFIX_READY && depth != 0)
            {
                CheckPrefixFrame* frame = stack + depth - 1;
                const uint32_t token = frame->token;
                const uint32_t child = frame->child;
                if (child == BUSTER_PARSE_NONE || child > token)
                {
                    if (!frame->emitted)
                    {
                        status = source == token ? CHECK_PREFIX_READY : CHECK_PREFIX_INVALID;
                        source += 1;
                        frame->emitted = true;
                    }
                }
                if (status == CHECK_PREFIX_READY && child != BUSTER_PARSE_NONE)
                {
                    if (seen[child] || depth >= count)
                    {
                        status = CHECK_PREFIX_INVALID;
                    }
                    else
                    {
                        frame->child = next[child];
                        seen[child] = 1;
                        stack[depth] = (CheckPrefixFrame){ .token = child, .child = first[child], .first = position };
                        depth += 1;
                    }
                }
                else if (status == CHECK_PREFIX_READY)
                {
                    if (position >= count)
                    {
                        status = CHECK_PREFIX_INVALID;
                    }
                    else
                    {
                        view->postorder[position] = token;
                        view->sizes[position] = position - frame->first + 1;
                        position += 1;
                        depth -= 1;
                    }
                }
            }
        }
        if (status == CHECK_PREFIX_READY && (position != count || source != count))
        {
            status = CHECK_PREFIX_INVALID;
        }
    }
    return status;
}

// Whether a parse diagnostic is a parameter's missing or unknown type, which the parser read in place as clang's does
// (its recovered parameter is `int`).
static bool check_prefix_parameter_error(const ParseDiagnostic* diagnostic)
{
    return diagnostic->parameter_type && (diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_REQUIRED || diagnostic->message == PARSE_MESSAGE_TYPE_SPECIFIER_MISSING ||
                                          diagnostic->message == PARSE_MESSAGE_UNKNOWN_TYPE_NAME);
}

// An unknown type already parsed as a specifier has a complete declaration tree. As for an unknown parameter
// type, clang retains that declaration with a recovered int type; it need not cut off later diagnostics (#2509).
static bool check_prefix_type_error(const Parser* parser, const ParseDiagnostic* diagnostic)
{
    const uint32_t token = diagnostic->token;
    const bool result = check_prefix_parameter_error(diagnostic) ||
                        (diagnostic->message == PARSE_MESSAGE_UNKNOWN_TYPE_NAME && token < parser->tokens.count &&
                         parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_SPECIFIER);
    return result;
}

static CheckPrefixStatus check_prefix_make(CheckPrefix* prefix, const Parser* parser, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *prefix = (CheckPrefix){ .status = CHECK_PREFIX_UNAVAILABLE, .diagnostic = BUSTER_PARSE_NONE };
    const uint32_t count = parser->tokens.count;
    uint32_t limit = count;
    uint32_t first_error = count;
    bool parameter_types = false;
    uint32_t end = 0;
    if (output == scratch)
    {
        prefix->status = CHECK_PREFIX_INVALID;
    }
    else if (parser->errors != 0 && parser->fatal == PARSE_MESSAGE_NONE)
    {
        prefix->status = CHECK_PREFIX_INVALID;
        if (count <= BUSTER_PARSE_MAX_TOKENS && parser->diagnostics != 0 && parser->diagnostic_count <= parser->diagnostic_capacity &&
            parser->tokens.kinds != 0 && parser->tokens.names != 0 && parser->tokens.sources != 0 && parser->tokens.flags != 0 &&
            parser->tree.roles != 0 && parser->tree.parents != 0 && parser->tree.depths != 0 &&
            (parser->tokens.pragma_count == 0 || (parser->tokens.pragma_sources != 0 && parser->tokens.pragma_positions != 0)))
        {
            for (uint32_t i = 0; i < parser->diagnostic_count; i += 1)
            {
                const ParseDiagnostic* diagnostic = parser->diagnostics + i;
                const uint32_t at = diagnostic->token < count ? diagnostic->token : count;
                const bool parameter = check_prefix_parameter_error(diagnostic);
                if (diagnostic->error && (prefix->diagnostic == BUSTER_PARSE_NONE || at < first_error))
                {
                    prefix->diagnostic = i;
                    first_error = at;
                    parameter_types = parameter;
                }
                parameter_types |= diagnostic->error && parameter;
                if (diagnostic->error && !check_prefix_type_error(parser, diagnostic) && at < limit)
                {
                    limit = at;
                }
            }
            if (prefix->diagnostic != BUSTER_PARSE_NONE)
            {
                prefix->status = CHECK_PREFIX_EMPTY;
                for (uint32_t token = 0; token < limit; token += 1)
                {
                    const uint8_t role = parser->tree.roles[token];
                    if (role == PARSE_ROLE_ERROR)
                    {
                        limit = token;
                    }
                    else if (parser->tree.parents[token] == BUSTER_PARSE_ROOT && parser->tree.depths[token] == 0 &&
                             (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_BODY))
                    {
                        end = token + 1;
                    }
                }
                if (end != 0)
                {
                    prefix->status = CHECK_PREFIX_READY;
                }
            }
        }
    }
    for (uint32_t token = 0; prefix->status == CHECK_PREFIX_READY && token < count; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (token < end)
        {
            const uint8_t role = parser->tree.roles[token];
            const uint8_t kind = parser->tokens.kinds[token];
            const bool root = parent == BUSTER_PARSE_ROOT;
            if (kind == PARSE_KIND_END || kind >= PARSE_KIND_COUNT || role <= PARSE_ROLE_ERROR || role >= PARSE_ROLE_COUNT ||
                (!root && parent >= end) || parent == token ||
                (root && (parser->tree.depths[token] != 0 || (role != PARSE_ROLE_TERMINATOR && role != PARSE_ROLE_CLOSE_BODY))))
            {
                prefix->status = CHECK_PREFIX_INVALID;
            }
            else if ((!root && parser->tree.depths[parent] > parser->tree.depths[token]) ||
                     (parse_role_rules[role] == PARSE_RULE_OPENER && (root || parent < token || parser->tree.roles[parent] != parse_closer_roles[role])))
            {
                prefix->status = CHECK_PREFIX_INVALID;
            }
        }
        else if (parent < end)
        {
            // No discarded token may still be part of a retained declaration.
            prefix->status = CHECK_PREFIX_INVALID;
        }
    }
    if (prefix->status == CHECK_PREFIX_READY)
    {
        Parser* view = &prefix->parser;
        view->options = parser->options;
        view->options.tree_only = false;
        view->language = parser->language;
        view->preprocessor = parser->preprocessor;
        view->prefix_view = true;
        view->recovered_parameter_types = parameter_types && end > first_error;
        memcpy(view->predeclared, parser->predeclared, sizeof(view->predeclared));
        view->tokens.count = end;
        view->tokens.capacity = end;
        view->tokens.kinds = check_prefix_column(output, end, sizeof(uint8_t));
        view->tokens.names = check_prefix_column(output, end, sizeof(uint32_t));
        view->tokens.sources = check_prefix_column(output, end, sizeof(uint32_t));
        view->tokens.flags = check_prefix_column(output, end, sizeof(uint8_t));
        view->tree.roles = check_prefix_column(output, end, sizeof(uint8_t));
        view->tree.parents = check_prefix_column(output, end, sizeof(uint32_t));
        view->tree.depths = check_prefix_column(output, end, sizeof(uint32_t));
        view->postorder = check_prefix_column(output, end, sizeof(uint32_t));
        view->sizes = check_prefix_column(output, end, sizeof(uint32_t));
        if (view->tokens.kinds == 0 || view->tokens.names == 0 || view->tokens.sources == 0 || view->tokens.flags == 0 || view->tree.roles == 0 ||
            view->tree.parents == 0 || view->tree.depths == 0 || view->postorder == 0 || view->sizes == 0)
        {
            prefix->status = CHECK_PREFIX_RESOURCE;
        }
        else
        {
            memcpy(view->tokens.kinds, parser->tokens.kinds, end);
            memcpy(view->tokens.names, parser->tokens.names, (uint64_t)end * sizeof(uint32_t));
            memcpy(view->tokens.sources, parser->tokens.sources, (uint64_t)end * sizeof(uint32_t));
            memcpy(view->tokens.flags, parser->tokens.flags, end);
            memcpy(view->tree.roles, parser->tree.roles, end);
            memcpy(view->tree.parents, parser->tree.parents, (uint64_t)end * sizeof(uint32_t));
            memcpy(view->tree.depths, parser->tree.depths, (uint64_t)end * sizeof(uint32_t));
            // An unknown type has already been diagnosed by the original parser. Its exact specifier
            // token becomes int only in this private checking view, as clang's recovery does; no other
            // unknown identifier, source location or original diagnostic is changed.
            uint8_t* recovered_types = NULL;
            for (uint32_t d = 0; prefix->status == CHECK_PREFIX_READY && d < parser->diagnostic_count; d += 1)
            {
                const ParseDiagnostic* diagnostic = parser->diagnostics + d;
                const uint32_t token = diagnostic->token;
                if (diagnostic->error && diagnostic->message == PARSE_MESSAGE_UNKNOWN_TYPE_NAME && token < end &&
                    view->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && view->tree.roles[token] == PARSE_ROLE_SPECIFIER)
                {
                    view->tokens.kinds[token] = PARSE_KIND_TYPE;
                    view->tokens.names[token] = C_NAME_INT;
                    if (!diagnostic->parameter_type)
                    {
                        recovered_types = recovered_types != NULL ? recovered_types : check_prefix_column(output, end, sizeof(uint8_t));
                        prefix->status = recovered_types != NULL ? CHECK_PREFIX_READY : CHECK_PREFIX_RESOURCE;
                        if (recovered_types != NULL)
                        {
                            recovered_types[token] = 1;
                        }
                    }
                }
            }
            view->recovered_types = recovered_types;
            // Pragma positions are monotone. A pragma exactly at end belongs to the excluded declaration.
            while (view->tokens.pragma_count < parser->tokens.pragma_count && parser->tokens.pragma_positions[view->tokens.pragma_count] < end)
            {
                view->tokens.pragma_count += 1;
            }
            view->tokens.pragma_positions = parser->tokens.pragma_positions;
            view->tokens.pragma_sources = parser->tokens.pragma_sources;
            prefix->status = prefix->status == CHECK_PREFIX_READY ? check_prefix_order(view, scratch) : prefix->status;
        }
    }
    arena_pop_to(scratch, scratch_mark);
    if (prefix->status == CHECK_PREFIX_READY)
    {
        prefix->end = end;
    }
    else
    {
        arena_pop_to(output, output_mark);
        prefix->parser = (Parser){ 0 };
    }
    return prefix->status;
}

// The prefix view of `parser` where its first error is a recoverable missing or unknown type, which the view reads as
// `int` with every declaration before the first structural error: the driver takes it before
// a repair view (check_repair_make), which would end at that parameter's declaration, so that each parameter's typo is
// searched (#1447, check_input_parameter_typo) and the later declarations are checked. Whether it made one; `output` is
// restored where it didn't.
static bool check_prefix_type_view(CheckPrefix* prefix, const Parser* parser, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    bool first = parser->errors != 0 && parser->diagnostics != 0 && parser->diagnostic_count <= parser->diagnostic_capacity;
    uint32_t at = UINT32_MAX;
    for (uint32_t i = 0; first && i < parser->diagnostic_count; i += 1)
    {
        const ParseDiagnostic* diagnostic = parser->diagnostics + i;
        const bool recoverable = check_prefix_type_error(parser, diagnostic);
        const bool earlier = diagnostic->error && diagnostic->token < at;
        first = earlier ? recoverable : first;
        at = earlier ? diagnostic->token : at;
    }
    const bool result = first && at != UINT32_MAX && check_prefix_make(prefix, parser, output, scratch) == CHECK_PREFIX_READY;
    if (!result)
    {
        arena_pop_to(output, output_mark);
        *prefix = (CheckPrefix){ 0 };
    }
    return result;
}

// A failed parse read as clang's recovery reads it (#147): where a declaration's `;` is missing before a declaration
// specifier, clang's ParseDeclGroup goes on as if the `;` were there (ParseDiagnostic.resumed), so its later
// declarations are checked as any others, the one before the specifier and those after it. This view puts a synthetic
// `;` before each such specifier, the parse's every one before its first other error, and parses it again; a missing
// `;` in a body, which the parser's recovery skips with the rest of its top-level declaration, shows up in the next
// round's parse. A view without errors is checked whole, as a prefix is (no translation-unit end); one with errors
// takes the prefix and repair views of its own first error in the original's place. The view's token indices are its
// own: the original's past a synthetic `;` move up by one. The synthetic `;`s have no spelling of their own: each takes
// the following token's source, as clang's diagnostics of the recovery point there.
//
// The parse's diagnostics as clang's parser gives them are the missing `;`s' errors, each round's, and the last view's,
// at the original's tokens (`diagnostics`): those of a body after its first missing `;`, which the original's recovery
// skipped, among them. READY means at least one `;` was put in. Its columns and diagnostics live in `output`; scratch is
// restored on every outcome.
typedef struct CheckResumed CheckResumed;
struct CheckResumed
{
    Parser parser;
    CheckPrefixStatus status;
    // The synthetic `;`s put in, in all.
    uint32_t inserted;
    // Tokens removed from malformed K&R parameter declaration tails.
    uint32_t discarded;
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_count;
};

// A change to the original's tokens that a view starts from (#1447, check_driver_typo_edits): the name at `token` read
// as another token (`kind` and `name`: a keyword, or a typedef's name) where `end` is 0, as clang's parser reads the
// statement again with its typo's correction; or the tokens [token, end) left out, as clang's skips an invalid
// expression statement's rest to its `;`. Edits are in token order and don't overlap.
typedef struct CheckPrefixEdit CheckPrefixEdit;
struct CheckPrefixEdit
{
    uint32_t token;
    uint32_t end;
    uint32_t name;
    uint8_t kind;
};

// A tentative typo's tail (#1447, check_driver_typo_tail): `end` answers, for an error of a view whose tokens'
// originals `origin` has (NULL for the original's own), the token that ends the tail the view leaves out, or 0 where the
// error is no such tail; the tail starts at `*begin`, which it sets (the error's token, or before it), and `*name` is the
// name it is of. The view takes each one as it takes a missing `;`: its diagnostic is pending at the tail's start with
// ParseDiagnostic.tail and the name as its argument, so that one pass finds a body's every one.
typedef struct CheckPrefixTails CheckPrefixTails;
struct CheckPrefixTails
{
    uint32_t (*end)(const Parser* view, const uint32_t* origin, const ParseDiagnostic* diagnostic, uint32_t* begin, uint32_t* name, const void* context);
    const void* context;
};

// The rounds of insertions a view takes, each a parse of the whole unit: one for the file scope's missing `;`s, which the
// original parse reports all of, and one more for each body's in turn, so a body with k of them takes k rounds. They go
// on until a round finds none, within a budget of this many times the unit's tokens parsed in all: past it, the last
// view stands, with its errors (its own prefix and repair views), as a unit's resources would end it.
#define BUSTER_CHECK_RESUME_WORK 256

// Whether a pragma just before `token` comes between a missing `;` and the specifier after it as clang's parser's
// annotation token (`#pragma weak`, `GCC visibility`, `pack`...), so that its recovery skips the declaration instead: any
// but those its preprocessor takes alone (`message`, the diagnostic ones, `GCC warning` and the like).
static bool check_prefix_pragma_between(const Parser* view, uint32_t token)
{
    const Preprocessor* pp = view->preprocessor;
    uint32_t low = 0;
    uint32_t high = view->tokens.pragma_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = view->tokens.pragma_positions[middle] < token;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    bool between = false;
    for (uint32_t k = low; pp != 0 && !between && k < view->tokens.pragma_count && view->tokens.pragma_positions[k] == token; k += 1)
    {
        const uint32_t hash = view->tokens.pragma_sources[k];
        uint32_t end = hash;
        const bool pragma = parse_pragma_at(&pp->output, hash, &end);
        const bool vendor = pragma && (pp_token_is_name(pp, &pp->output, hash + 2, end, "GCC") || pp_token_is_name(pp, &pp->output, hash + 2, end, "clang"));
        const uint32_t name = vendor ? hash + 3 : hash + 2;
        const bool preprocessor = pragma && (pp_token_is_name(pp, &pp->output, name, end, "message") || pp_token_is_name(pp, &pp->output, name, end, "diagnostic") ||
                                             pp_token_is_name(pp, &pp->output, name, end, "warning") || pp_token_is_name(pp, &pp->output, name, end, "error") ||
                                             pp_token_is_name(pp, &pp->output, name, end, "poison") || pp_token_is_name(pp, &pp->output, name, end, "system_header") ||
                                             pp_token_is_name(pp, &pp->output, name, end, "dependency") || pp_token_is_name(pp, &pp->output, name, end, "push_macro") ||
                                             pp_token_is_name(pp, &pp->output, name, end, "pop_macro"));
        between = !preprocessor;
    }
    return between;
}

// Whether the view's parse puts a `;` before diagnostic `d`'s token: a resumed error before `limit`, its first other, with no
// pragma between (check_prefix_pragma_between).
static bool check_prefix_resumes(const Parser* view, const ParseDiagnostic* diagnostic, uint32_t limit)
{
    const uint32_t begin = diagnostic->resume_begin != 0 ? diagnostic->resume_begin : diagnostic->token;
    bool valid = diagnostic->error && (diagnostic->resumed || diagnostic->resume_colon) && diagnostic->token < limit && diagnostic->token != 0 &&
                 begin <= diagnostic->token && (view->tokens.pragma_count == 0 || !check_prefix_pragma_between(view, begin));
    if (valid && diagnostic->resume_end != 0)
    {
        valid = diagnostic->resume_end >= diagnostic->token && diagnostic->resume_end <= view->tokens.count;
        valid &= !diagnostic->resume_delete || diagnostic->resume_end > begin;
        // Do not move an annotation out of a discarded K&R tail. Those pragmas need their own parser recovery.
        uint32_t low = 0;
        uint32_t high = view->tokens.pragma_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = view->tokens.pragma_positions[middle] < begin;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        valid &= low == view->tokens.pragma_count || view->tokens.pragma_positions[low] >= diagnostic->resume_end;
    }
    valid &= !diagnostic->resume_delete || diagnostic->resume_end != 0;
    valid &= diagnostic->resume_begin == 0 || (diagnostic->resume_delete && diagnostic->resume_end != 0);
    return valid;
}

// `diagnostic` at the original's tokens, from a view's whose tokens' originals `origin` has (a synthetic `;`'s, the
// token after it).
static ParseDiagnostic check_prefix_original_diagnostic(const ParseDiagnostic* diagnostic, const uint32_t* origin, uint32_t count, uint32_t original_count)
{
    ParseDiagnostic result = *diagnostic;
    result.token = diagnostic->token < count ? origin[diagnostic->token] : original_count;
    result.argument = diagnostic->argument < count ? origin[diagnostic->argument] : diagnostic->argument;
    result.note = diagnostic->note < count ? origin[diagnostic->note] : diagnostic->note;
    result.resumed = false;
    result.resume_delete = false;
    result.resume_end = 0;
    result.resume_begin = 0;
    result.resume_colon = false;
    return result;
}

// Sorts `items` by their tokens, stably (a bottom-up merge through `temporary`, of as many): the missing `;`s' errors come
// round by round, each round's in order.
static void check_prefix_sort_diagnostics(ParseDiagnostic* items, ParseDiagnostic* temporary, uint32_t count)
{
    ParseDiagnostic* from = items;
    ParseDiagnostic* to = temporary;
    for (uint32_t width = 1; width < count; width *= 2)
    {
        for (uint32_t left = 0; left < count; left += 2 * width)
        {
            const uint32_t middle = left + width < count ? left + width : count;
            const uint32_t right = middle + width < count ? middle + width : count;
            uint32_t a = left;
            uint32_t b = middle;
            for (uint32_t k = left; k < right; k += 1)
            {
                const bool take_a = a < middle && (b >= right || from[a].token <= from[b].token);
                to[k] = take_a ? from[a] : from[b];
                a += take_a;
                b += !take_a;
            }
        }
        ParseDiagnostic* swap = from;
        from = to;
        to = swap;
    }
    if (from != items && count != 0)
    {
        memcpy(items, from, (uint64_t)count * sizeof(ParseDiagnostic));
    }
}

// The original's tokens with `edits` applied (CheckPrefixEdit), parsed again into `view`, whose columns have room for
// `capacity` tokens; `origin` gets each view token's original. Whether the parse ran (its errors are the view's).
static bool check_prefix_edit(Parser* view, uint32_t* origin, const Parser* original, const CheckPrefixEdit* edits, uint32_t edit_count, uint32_t capacity,
                              uint8_t* meanings, Arena* scratch)
{
    const uint32_t count = original->tokens.count;
    bool valid = edit_count != 0 && count <= capacity;
    for (uint32_t e = 0; valid && e < edit_count; e += 1)
    {
        const CheckPrefixEdit* edit = edits + e;
        valid = edit->token < count && (edit->end == 0 || (edit->end > edit->token && edit->end <= count)) &&
                (e == 0 || edits[e - 1].token + 1 <= edit->token) && (e == 0 || edits[e - 1].end <= edit->token);
    }
    uint32_t written = 0;
    uint32_t from = 0;
    uint32_t pragma = 0;
    for (uint32_t e = 0; valid && e <= edit_count; e += 1)
    {
        const CheckPrefixEdit* edit = e < edit_count ? edits + e : NULL;
        const uint32_t to = edit != NULL ? edit->token : count;
        const uint32_t length = to - from;
        memcpy(view->tokens.kinds + written, original->tokens.kinds + from, length);
        memcpy(view->tokens.names + written, original->tokens.names + from, (uint64_t)length * sizeof(uint32_t));
        memcpy(view->tokens.sources + written, original->tokens.sources + from, (uint64_t)length * sizeof(uint32_t));
        memcpy(view->tokens.flags + written, original->tokens.flags + from, length);
        for (uint32_t k = 0; k < length; k += 1)
        {
            origin[written + k] = from + k;
        }
        // A pragma before a kept token stays before it; one within a left-out range goes before the token after it.
        const uint32_t skipped = edit != NULL ? (edit->end != 0 ? edit->end : to + 1) : to;
        for (; pragma < view->tokens.pragma_count && original->tokens.pragma_positions[pragma] < skipped + (edit == NULL); pragma += 1)
        {
            const uint32_t at = original->tokens.pragma_positions[pragma];
            view->tokens.pragma_positions[pragma] = at < to ? written + (at - from) : written + length;
        }
        written += length;
        if (edit != NULL && edit->end == 0)
        {
            view->tokens.kinds[written] = edit->kind;
            view->tokens.names[written] = edit->name;
            view->tokens.sources[written] = original->tokens.sources[to];
            view->tokens.flags[written] = original->tokens.flags[to];
            origin[written] = to;
            written += 1;
        }
        from = edit != NULL ? (edit->end != 0 ? edit->end : to + 1) : count;
    }
    if (valid)
    {
        view->tokens.count = written;
        view->tokens.capacity = written;
        view->diagnostic_count = 0;
        memset(view->tokens.kinds + written, 0, BUSTER_PARSE_TOKEN_PADDING);
        memset(view->tokens.names + written, 0, (uint64_t)BUSTER_PARSE_TOKEN_PADDING * sizeof(uint32_t));
        memset(view->tokens.sources + written, 0, (uint64_t)BUSTER_PARSE_TOKEN_PADDING * sizeof(uint32_t));
        memset(view->tokens.flags + written, 0, BUSTER_PARSE_TOKEN_PADDING);
        memset(meanings, PARSE_MEANING_ORDINARY, original->meanings.capacity);
        for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            meanings[view->predeclared[i]] = PARSE_MEANING_TYPEDEF;
        }
        const ParseScalarResult parsed = parse_scalar(view->tokens.kinds, view->tokens.names, view->tokens.flags, written, view->language, &view->meanings,
                                                      &view->tree, scratch, view->diagnostics, view->diagnostic_capacity, &view->diagnostic_count);
        view->errors = parsed.errors;
        view->fatal = PARSE_MESSAGE_NONE;
        valid = !parsed.out_of_memory;
    }
    return valid;
}

static CheckPrefixStatus check_prefix_resume(CheckResumed* resumed, const Parser* original, const CheckPrefixEdit* edits, uint32_t edit_count,
                                             const CheckPrefixTails* tails, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *resumed = (CheckResumed){ .status = CHECK_PREFIX_UNAVAILABLE };
    const Parser* current = original;
    const uint32_t original_count = original->tokens.count;
    bool more = output != scratch && (original->errors != 0 || edit_count != 0) && original->fatal == PARSE_MESSAGE_NONE && original->diagnostics != 0 &&
                original->diagnostic_count <= original->diagnostic_capacity && original_count <= BUSTER_PARSE_MAX_TOKENS / 2 &&
                original->tokens.kinds != 0 && original->tokens.names != 0 && original->tokens.sources != 0 && original->tokens.flags != 0 &&
                original->meanings.capacity != 0 && (original->tokens.pragma_count == 0 || original->tokens.pragma_positions != 0);
    for (uint32_t i = 0; more && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        more = original->predeclared[i] < original->meanings.capacity;
    }
    // Each `;` goes before an original token, a declaration specifier, which no later round's error is at again: a view has
    // fewer than twice the original's tokens. Two views' columns, the rounds taking them in turn, each token's original
    // (scratch) and the missing `;`s' errors found so far, at the original's tokens (scratch): memory of the unit's size,
    // whatever the rounds.
    const uint32_t capacity = more ? 2 * original_count + 1 : 0;
    // (A third, the edited original's, where `edits` asks for one.)
    Parser* views = more ? BUSTER_ARENA_PUSH_ARRAY(output, Parser, 3) : 0;
    uint8_t* meanings = more ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, original->meanings.capacity) : 0;
    uint32_t* origins[3] = { more ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity) : 0, more ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity) : 0,
                             more && edit_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity) : 0 };
    ParseDiagnostic* pending = more ? BUSTER_ARENA_PUSH_ARRAY(scratch, ParseDiagnostic, (uint64_t)original_count + 1) : 0;
    // A round's diagnostics, with the tails taken as resumed ones (CheckPrefixTails).
    const uint32_t round_capacity = original->diagnostic_capacity > capacity + 16 ? original->diagnostic_capacity : capacity + 16;
    ParseDiagnostic* round_diagnostics = more && tails != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, ParseDiagnostic, round_capacity) : 0;
    more = more && (tails == NULL || round_diagnostics != 0);
    more = more && views != 0 && meanings != 0 && origins[0] != 0 && origins[1] != 0 && (edit_count == 0 || origins[2] != 0) && pending != 0;
    for (uint32_t v = 0; more && v < (edit_count != 0 ? 3u : 2u); v += 1)
    {
        Parser* view = views + v;
        *view = (Parser){ .options = original->options, .language = original->language, .preprocessor = original->preprocessor };
        view->options.tree_only = false;
        memcpy(view->predeclared, original->predeclared, sizeof(view->predeclared));
        view->tokens.kinds = check_prefix_column(output, capacity, 1);
        view->tokens.names = check_prefix_column(output, capacity, sizeof(uint32_t));
        view->tokens.sources = check_prefix_column(output, capacity, sizeof(uint32_t));
        view->tokens.flags = check_prefix_column(output, capacity, 1);
        view->tree.roles = check_prefix_column(output, capacity, 1);
        view->tree.parents = check_prefix_column(output, capacity, sizeof(uint32_t));
        view->tree.depths = check_prefix_column(output, capacity, sizeof(uint32_t));
        view->diagnostic_capacity = capacity + 16;
        view->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, ParseDiagnostic, view->diagnostic_capacity);
        view->tokens.pragma_count = original->tokens.pragma_count;
        view->tokens.pragma_sources = original->tokens.pragma_sources;
        view->tokens.pragma_positions = view->tokens.pragma_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, view->tokens.pragma_count) : 0;
        view->meanings = original->meanings;
        view->meanings.meanings = meanings;
        more = view->tokens.kinds != 0 && view->tokens.names != 0 && view->tokens.sources != 0 && view->tokens.flags != 0 && view->tree.roles != 0 &&
               view->tree.parents != 0 && view->tree.depths != 0 && view->diagnostics != 0 && (view->tokens.pragma_count == 0 || view->tokens.pragma_positions != 0);
    }
    resumed->status = more || capacity == 0 ? resumed->status : CHECK_PREFIX_RESOURCE;
    const uint64_t budget = (uint64_t)BUSTER_CHECK_RESUME_WORK * original_count + 1024;
    uint64_t work = 0;
    const uint32_t* origin = 0;
    uint32_t pending_count = 0;
    if (more && edit_count != 0)
    {
        // The edited original, parsed again: the rounds go on from it, at its tokens' originals.
        const bool parsed = check_prefix_edit(views + 2, origins[2], original, edits, edit_count, capacity, meanings, scratch);
        resumed->status = parsed ? CHECK_PREFIX_READY : CHECK_PREFIX_RESOURCE;
        current = parsed ? views + 2 : current;
        origin = parsed ? origins[2] : origin;
        work += parsed ? current->tokens.count : 0;
        more = parsed && current->errors != 0;
    }
    for (uint32_t round = 0; more; round += 1)
    {
        const uint32_t count = current->tokens.count;
        const ParseDiagnostic* diagnostics = current->diagnostics;
        if (tails != NULL)
        {
            for (uint32_t d = 0; d < current->diagnostic_count; d += 1)
            {
                ParseDiagnostic diagnostic = current->diagnostics[d];
                uint32_t begin = diagnostic.token;
                uint32_t name = BUSTER_PARSE_NONE;
                const uint32_t end = diagnostic.error && !diagnostic.resumed ? tails->end(current, origin, &diagnostic, &begin, &name, tails->context) : 0;
                diagnostic.token = end != 0 ? begin : diagnostic.token;
                diagnostic.argument = end != 0 ? name : diagnostic.argument;
                diagnostic.resumed = diagnostic.resumed || end != 0;
                diagnostic.resume_delete = diagnostic.resume_delete || end != 0;
                diagnostic.resume_end = end != 0 ? end : diagnostic.resume_end;
                diagnostic.tail = end != 0;
                round_diagnostics[d] = diagnostic;
            }
            diagnostics = round_diagnostics;
        }
        // The resumed errors before the parse's first other one, whose tokens get a `;` before them. The diagnostics are
        // in token order, but for the notes of errors that don't stop the parse, which are taken as other errors.
        uint32_t limit = count;
        for (uint32_t d = 0; d < current->diagnostic_count; d += 1)
        {
            const ParseDiagnostic* diagnostic = diagnostics + d;
            // Unknown types were read as specifiers before recovery erased the declaration's tree. They do not stop
            // a later declarator's proven tail repair (#2640); the reparse retains their errors and the prefix view
            // marks their recovered types invalid. Missing parameter types likewise do not stop the rounds.
            const bool type = check_prefix_parameter_error(diagnostic) || diagnostic->message == PARSE_MESSAGE_UNKNOWN_TYPE_NAME;
            const bool other = diagnostic->error && !check_prefix_resumes(current, diagnostic, count) && !type;
            limit = other && diagnostic->token < limit ? diagnostic->token : limit;
            // The original parser abandoned this K&R header. Later diagnostics may have treated its remaining
            // parameter declarations as file scope, so re-establish the header before considering those repairs.
            if (!other && diagnostic->error && diagnostic->resume_end != 0 && diagnostic->token < limit)
            {
                limit = diagnostic->token + 1;
            }
        }
        uint32_t insert = 0;
        uint32_t discard = 0;
        uint32_t repairs = 0;
        uint32_t last = 0;
        bool ordered = true;
        for (uint32_t d = 0; d < current->diagnostic_count; d += 1)
        {
            const ParseDiagnostic* diagnostic = diagnostics + d;
            const bool taken = check_prefix_resumes(current, diagnostic, limit);
            const uint32_t begin = diagnostic->resume_begin != 0 ? diagnostic->resume_begin : diagnostic->token;
            ordered &= !taken || begin > last;
            last = taken ? (diagnostic->resume_end != 0 ? diagnostic->resume_end - 1 : diagnostic->token) : last;
            insert += taken && !diagnostic->resume_delete;
            repairs += taken;
            discard += taken && diagnostic->resume_end != 0 ? diagnostic->resume_end - begin : 0;
        }
        const uint32_t total = count + insert - discard;
        work += total;
        more = ordered && repairs != 0 && total <= capacity && pending_count + repairs <= original_count && work <= budget;
        Parser* view = views + (round & 1);
        uint32_t* next_origin = origins[round & 1];
        if (more)
        {
            view->tokens.count = total;
            view->tokens.capacity = total;
            view->diagnostic_count = 0;
            // Each kept token moves up by the `;`s before it; a pragma just before a resumed token stays before it, after
            // its `;`.
            uint32_t written = 0;
            uint32_t from = 0;
            uint32_t pragma = 0;
            for (uint32_t d = 0; d <= current->diagnostic_count; d += 1)
            {
                const ParseDiagnostic* diagnostic = d < current->diagnostic_count ? diagnostics + d : 0;
                const bool taken = diagnostic == 0 || check_prefix_resumes(current, diagnostic, limit);
                const uint32_t to = diagnostic != 0 ? (diagnostic->resume_begin != 0 ? diagnostic->resume_begin : diagnostic->token) : count;
                if (taken)
                {
                    const uint32_t length = to - from;
                    memcpy(view->tokens.kinds + written, current->tokens.kinds + from, length);
                    memcpy(view->tokens.names + written, current->tokens.names + from, (uint64_t)length * sizeof(uint32_t));
                    memcpy(view->tokens.sources + written, current->tokens.sources + from, (uint64_t)length * sizeof(uint32_t));
                    memcpy(view->tokens.flags + written, current->tokens.flags + from, length);
                    for (uint32_t k = 0; k < length; k += 1)
                    {
                        next_origin[written + k] = origin != 0 ? origin[from + k] : from + k;
                    }
                    for (; pragma < view->tokens.pragma_count && current->tokens.pragma_positions[pragma] < to + (diagnostic == 0); pragma += 1)
                    {
                        view->tokens.pragma_positions[pragma] = written + (current->tokens.pragma_positions[pragma] - from);
                    }
                    written += length;
                    if (diagnostic != 0)
                    {
                        if (!diagnostic->resume_delete)
                        {
                            view->tokens.kinds[written] = diagnostic->resume_colon ? PARSE_KIND_COLON : PARSE_KIND_SEMI;
                            view->tokens.names[written] = 0;
                            view->tokens.sources[written] = current->tokens.sources[to];
                            view->tokens.flags[written] = 0;
                            next_origin[written] = origin != 0 ? origin[to] : to;
                            written += 1;
                        }
                        // (The first round's tokens are the original's.)
                        pending[pending_count] = origin != 0 ? check_prefix_original_diagnostic(diagnostic, origin, count, original_count) : *diagnostic;
                        pending[pending_count].resumed = false;
                        pending[pending_count].resume_delete = false;
                        pending[pending_count].resume_end = 0;
                        pending[pending_count].resume_begin = 0;
                        pending[pending_count].resume_colon = false;
                        pending_count += 1;
                    }
                    from = diagnostic != NULL && diagnostic->resume_end != 0 ? diagnostic->resume_end : to;
                }
            }
            // A discarded tail can make this alternating view shorter than its previous round. Restore its padded
            // end tokens instead of letting the scalar parser see stale kinds from that older, longer view.
            memset(view->tokens.kinds + total, 0, BUSTER_PARSE_TOKEN_PADDING);
            memset(view->tokens.names + total, 0, (uint64_t)BUSTER_PARSE_TOKEN_PADDING * sizeof(uint32_t));
            memset(view->tokens.sources + total, 0, (uint64_t)BUSTER_PARSE_TOKEN_PADDING * sizeof(uint32_t));
            memset(view->tokens.flags + total, 0, BUSTER_PARSE_TOKEN_PADDING);
            // (Every pragma position is at most the count: the last segment took them all.)
            origin = next_origin;
            memset(meanings, PARSE_MEANING_ORDINARY, original->meanings.capacity);
            for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
            {
                meanings[view->predeclared[i]] = PARSE_MEANING_TYPEDEF;
            }
            const ParseScalarResult parsed = parse_scalar(view->tokens.kinds, view->tokens.names, view->tokens.flags, total, view->language, &view->meanings,
                                                          &view->tree, scratch, view->diagnostics, view->diagnostic_capacity, &view->diagnostic_count);
            view->errors = parsed.errors;
            view->fatal = PARSE_MESSAGE_NONE;
            resumed->inserted += insert;
            resumed->discarded += discard;
            resumed->status = parsed.out_of_memory ? CHECK_PREFIX_RESOURCE : CHECK_PREFIX_READY;
            more = !parsed.out_of_memory && parsed.errors != 0;
            current = view;
        }
    }
    if (resumed->status == CHECK_PREFIX_READY && current != original)
    {
        // The parse's diagnostics: the missing `;`s' and the last view's other ones, by their tokens.
        const uint32_t count = current->tokens.count;
        const uint32_t total = pending_count + current->diagnostic_count;
        resumed->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, ParseDiagnostic, (uint64_t)total + 1);
        ParseDiagnostic* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseDiagnostic, (uint64_t)total + 1);
        resumed->status = resumed->diagnostics != 0 && temporary != 0 ? CHECK_PREFIX_READY : CHECK_PREFIX_RESOURCE;
        for (uint32_t d = 0; resumed->status == CHECK_PREFIX_READY && d < total; d += 1)
        {
            resumed->diagnostics[d] = d < pending_count ? pending[d] :
                                      check_prefix_original_diagnostic(current->diagnostics + d - pending_count, origin, count, original_count);
        }
        if (resumed->status == CHECK_PREFIX_READY)
        {
            check_prefix_sort_diagnostics(resumed->diagnostics, temporary, total);
        }
        resumed->diagnostic_count = total;
    }
    if (resumed->status == CHECK_PREFIX_READY && current != original && current->errors == 0)
    {
        // A whole view brings its postorder, as a prefix does (check_prefix_order).
        Parser* view = (Parser*)current;
        view->postorder = check_prefix_column(output, view->tokens.count, sizeof(uint32_t));
        view->sizes = check_prefix_column(output, view->tokens.count, sizeof(uint32_t));
        resumed->status = view->postorder != 0 && view->sizes != 0 ? check_prefix_order(view, scratch) : CHECK_PREFIX_RESOURCE;
    }
    arena_pop_to(scratch, scratch_mark);
    if (resumed->status == CHECK_PREFIX_READY && current != original)
    {
        resumed->parser = *current;
    }
    else
    {
        resumed->status = resumed->status == CHECK_PREFIX_READY ? CHECK_PREFIX_UNAVAILABLE : resumed->status;
        resumed->parser = (Parser){ 0 };
        resumed->diagnostics = 0;
        resumed->diagnostic_count = 0;
        arena_pop_to(output, output_mark);
    }
    return resumed->status;
}

#endif
