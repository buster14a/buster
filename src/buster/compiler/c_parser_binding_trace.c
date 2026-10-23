// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional source-event recording for #128, included after ParseScalar. The ordinary parser pays only the null
// trace tests. A traced parse shares its exact declaration and scope transitions with the syntax parser. Every
// ordinary undo entry has one active binding; tags and labels reserve a harmless name-zero undo entry so that
// prototype, statement and block marks restore all namespaces together. Builtin typedefs precede that undo log.
// The source adapter owns the trace's storage and supplies a previously checked tree for forward labels.

#include "buster/compiler/c_bindings.c"

struct ParseBindingTrace
{
    CBindings* bindings;
    const ParseTree* source_tree;
    uint32_t* token_events;
    uint32_t* declarations;
    uint8_t* namespaces;
    uint8_t* kinds;
    uint32_t* kept_events;
    uint32_t* frame_scopes;
    uint32_t* statement_scopes;
    uint32_t* matches;
    uint32_t* label_first;
    uint32_t* label_next;
    uint32_t point;
    uint32_t builtin_count;
};

static uint32_t parse_binding_trace_scope(const ParseScalar* parser)
{
    const ParseBindingTrace* trace = parser->binding_trace;
    const uint32_t frame = trace->frame_scopes[parser->frame_count - 1];
    const uint32_t statement = parser->statement_count != 0 ? trace->statement_scopes[parser->statement_count - 1] : 0;
    return frame > statement ? frame : statement;
}

static void parse_binding_trace_record(ParseScalar* parser, uint32_t token, CBindingNamespace space, uint32_t event)
{
    ParseBindingTrace* trace = parser->binding_trace;
    CBindings* bindings = trace->bindings;
    if (event != 0 && token < parser->count)
    {
        const uint32_t resolved = bindings->operation[event - 1] == C_BINDING_DECLARE ? event :
                                  c_bindings_lookup(bindings, space, parser->names[token]);
        trace->token_events[token] = event;
        trace->declarations[token] = resolved != 0 ? bindings->declaration[resolved - 1] : BUSTER_PARSE_NONE;
        trace->namespaces[token] = (uint8_t)space;
        trace->kinds[token] = resolved != 0 ? bindings->kind[resolved - 1] :
                              space == C_BINDING_LABEL ? C_BINDING_LABEL_KIND : space == C_BINDING_TAG ? C_BINDING_TAG_KIND : C_BINDING_OBJECT;
    }
}

static void parse_binding_trace_declare(ParseScalar* parser, uint32_t token, CBindingNamespace space, CBindingKind kind, bool auxiliary)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0)
    {
        const uint32_t event = c_bindings_declare(trace->bindings, space, parser->names[token], kind, token, trace->point, parse_binding_trace_scope(parser));
        parse_binding_trace_record(parser, token, space, event);
        if (auxiliary)
        {
            parser->log_names[parser->log_count] = 0;
            parser->log_meanings[parser->log_count] = parser->meanings[0];
            parser->log_count += 1;
        }
    }
}

static void parse_binding_trace_use(ParseScalar* parser, uint32_t token, CBindingNamespace space)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0)
    {
        const uint32_t event = c_bindings_use(trace->bindings, space, parser->names[token], token, trace->point);
        parse_binding_trace_record(parser, token, space, event);
    }
}

static void parse_binding_trace_rebind(ParseScalar* parser, uint32_t kept)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0 && trace->bindings->valid)
    {
        CBindings* bindings = trace->bindings;
        const uint32_t old = trace->kept_events[kept];
        const uint32_t key = bindings->key[old];
        c_bindings_declare(bindings, (CBindingNamespace)(key / bindings->name_capacity), key % bindings->name_capacity,
                           (CBindingKind)bindings->kind[old], bindings->declaration[old], trace->point, parse_binding_trace_scope(parser));
    }
}

static void parse_binding_trace_tag(ParseScalar* parser, uint32_t token, bool definition, bool standalone)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0)
    {
        const uint32_t old = c_bindings_lookup(trace->bindings, C_BINDING_TAG, parser->names[token]);
        // Keep lexical declaration identity, including same-scope redeclarations. A checker may join their
        // types/entities through previous/depth; an elaborated specifier with a visible tag is just a use.
        if (old == 0 || definition || standalone)
        {
            parse_binding_trace_declare(parser, token, C_BINDING_TAG, C_BINDING_TAG_KIND, true);
        }
        else
        {
            parse_binding_trace_use(parser, token, C_BINDING_TAG);
        }
    }
}

static void parse_binding_trace_labels(ParseScalar* parser, uint32_t body)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0)
    {
        for (uint32_t token = trace->label_first[body]; token != BUSTER_PARSE_NONE; token = trace->label_next[token])
        {
            parse_binding_trace_declare(parser, token, C_BINDING_LABEL, C_BINDING_LABEL_KIND, true);
        }
    }
}

static void parse_binding_trace_identifier(ParseScalar* parser, uint32_t token, uint8_t mode, uint32_t flags, uint32_t argument, uint8_t role)
{
    ParseBindingTrace* trace = parser->binding_trace;
    if (trace != 0 && token < parser->count && parser->kinds[token] == PARSE_KIND_IDENTIFIER &&
        trace->token_events[token] == 0 && trace->namespaces[token] == UINT8_MAX)
    {
        const bool attribute = (flags & BUSTER_PARSE_FRAME_ATTRIBUTE) != 0;
        const bool member = role == PARSE_ROLE_MEMBER || mode == PARSE_MODE_OFFSETOF_MEMBER || mode == PARSE_MODE_ASM_NAME;
        const bool label = mode == PARSE_MODE_GOTO || (token + 1 < parser->count && trace->source_tree->roles[token + 1] == PARSE_ROLE_LABEL) ||
                           (token != 0 && parser->kinds[token - 1] == PARSE_KIND_AMPAMP && parser->roles[token - 1] == PARSE_ROLE_PREFIX) ||
                           ((flags & BUSTER_PARSE_FRAME_ASM) != 0 && argument == 4);
        if (mode == PARSE_MODE_LOCAL_LABEL_NAME)
        {
            parse_binding_trace_declare(parser, token, C_BINDING_LABEL, C_BINDING_LABEL_KIND, true);
        }
        else if (label)
        {
            parse_binding_trace_use(parser, token, C_BINDING_LABEL);
        }
        else if (!attribute && !member && (role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_SPECIFIER))
        {
            parse_binding_trace_use(parser, token, C_BINDING_ORDINARY);
        }
    }
}
