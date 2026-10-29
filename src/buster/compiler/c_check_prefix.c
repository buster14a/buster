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
// recovery erased the whole declaration. The prefix never guesses missing punctuation or revives erased nodes.
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

static CheckPrefixStatus check_prefix_make(CheckPrefix* prefix, const Parser* parser, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *prefix = (CheckPrefix){ .status = CHECK_PREFIX_UNAVAILABLE, .diagnostic = BUSTER_PARSE_NONE };
    const uint32_t count = parser->tokens.count;
    uint32_t limit = count;
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
                if (diagnostic->error && (prefix->diagnostic == BUSTER_PARSE_NONE || diagnostic->token < limit))
                {
                    prefix->diagnostic = i;
                    limit = diagnostic->token < count ? diagnostic->token : count;
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
            // Pragma positions are monotone. A pragma exactly at end belongs to the excluded declaration.
            while (view->tokens.pragma_count < parser->tokens.pragma_count && parser->tokens.pragma_positions[view->tokens.pragma_count] < end)
            {
                view->tokens.pragma_count += 1;
            }
            view->tokens.pragma_positions = parser->tokens.pragma_positions;
            view->tokens.pragma_sources = parser->tokens.pragma_sources;
            prefix->status = check_prefix_order(view, scratch);
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

#endif
