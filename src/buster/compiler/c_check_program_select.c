// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked #133 definitions/references -> immutable #129 reachability planner -> emission views. Include after
// CheckProgram and before its machine consumers. All ordinary-mode bodies have already been checked; optional
// #155 mode has checked the independently demanded subset in original source order. Both modes use this exact
// emission policy. A call/address edge is the checked expression walk's evaluated reference, not identifier text.
// Static initializer edges are the final live writes retained by CheckDataPrepared, including strings and cycles.
//
// Definition IDs interleave code and data in original definition order. Canonical objects join redeclarations;
// prototype declarations are not graph definitions. C99 inline-only bodies have an external symbol but no local
// emission/dependencies. GNU inline uses the language mode and retained declaration spellings and attribute. There is no
// inliner here; any future inliner must attribute its retained dependencies to the emitted caller before this pass.
// A body also selects its own block-scope statics, referenced or not, as clang emits every static local of a function
// it emits, even at -O0 (#129's comparison with clang's objects, c_lowering_symbols.c).
//
// check_program_select_mode with C_LOWERING_ALL selects every definition that may be emitted, referenced or not (an
// inline-only one is never emitted): the driver's --lower-all, #129's test option for what the selection saves.
//
// The result borrows the full typed graph/control stream and immutable initializer plans. It owns compact body,
// reference, data and builtin views plus a deterministic object-ID-ordered symbol map. Source inputs outlive it;
// output and scratch are distinct, no padding is required, and failure restores both marks with no partial view.

#ifndef BUSTER_CHECK_PROGRAM_SELECT_INCLUDED
#define BUSTER_CHECK_PROGRAM_SELECT_INCLUDED 1

#include "buster/compiler/c_lowering.c"

typedef struct CheckProgramSelection CheckProgramSelection;
struct CheckProgramSelection
{
    CheckProgram program;
    CheckDataPrepared data;
    CLoweringStatistics statistics;
    uint32_t defined_functions;
    uint32_t selected_functions;
    uint32_t defined_objects;
    uint32_t selected_objects;
    uint32_t token;
    CheckProgramStatus status;
};

static uint32_t check_program_attribute_roots(const CheckInputAttributes* attributes, bool function, bool* gnu_inline)
{
    uint32_t flags = 0;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored)
        {
            flags |= attribute->kind == CHECK_ATTRIBUTE_USED || attribute->kind == CHECK_ATTRIBUTE_RETAIN ? BUSTER_C_LOWERING_USED : 0;
            flags |= function && attribute->kind == CHECK_ATTRIBUTE_CONSTRUCTOR ? BUSTER_C_LOWERING_CONSTRUCTOR : 0;
            flags |= function && attribute->kind == CHECK_ATTRIBUTE_DESTRUCTOR ? BUSTER_C_LOWERING_DESTRUCTOR : 0;
            *gnu_inline |= attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE;
        }
    }
    return flags;
}

// Whether a declaration's `attributes` make clang emit its function where the unit defines it rather than when its
// emission first refers to it (ASTContext::DeclMustBeEmitted): used, constructor or destructor, but not retain, which
// keeps a function that is emitted without making it one (CheckProgram.body_eager).
static bool check_program_attribute_eager(const CheckInputAttributes* attributes)
{
    bool eager = false;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        eager |= !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_USED || attribute->kind == CHECK_ATTRIBUTE_CONSTRUCTOR ||
                                         attribute->kind == CHECK_ATTRIBUTE_DESTRUCTOR);
    }
    return eager;
}

// Whether a declaration's `attributes` hold a gnu_inline attribute, which turns GNU's rules of inline functions on.
static bool check_program_attribute_gnu_inline(const CheckInputAttributes* attributes)
{
    bool gnu_inline = false;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        gnu_inline |= !attributes->records[i].ignored && attributes->records[i].kind == CHECK_ATTRIBUTE_GNU_INLINE;
    }
    return gnu_inline;
}

// Whether clang emits `body`'s function where the unit defines it (CheckProgram.body_eager), as CodeGenModule::EmitGlobal
// decides when the parser hands over the definition: by the declarations that exist then, those of the function up to
// and including the definition (a run of check_data_prepare's keys, in object and token order), not the unit's final
// ones, which the selection's flags have. A later declaration is no emission point: one that makes the definition
// external (C99's `extern int f(int);` after `inline int f(int) {...}`, GNU89's plain inline one after `extern inline`)
// only queues the function for the unit's end, with the other deferred functions, and one with a used, constructor or
// destructor attribute adds nothing to a definition before it (a declaration inherits attributes from the ones before
// it, not the other way). So it is emitted at once if its definition is external by the definitions loop's rules of
// GNU's and C99's inline functions (the same, over those declarations alone), or, where its linkage is not external, if
// one of them has a used, constructor or destructor attribute (an inline-only definition is never emitted). An ordinary
// external definition (not an inline one) is emitted at once whatever its declarations, and is not looked at further.
// Out of line, as only a unit with a target attribute asks it, once per selected body.
__attribute__((noinline)) static bool check_program_body_eager(const CheckProgram* program, const CheckDataPrepared* data, const CheckProgramBody* body)
{
    const CheckInput* input = program->input;
    const uint8_t definition = input->function_flags != 0 ? input->function_flags[body->declaration] : 0;
    const bool external = input->linkage[body->declaration] == 2;
    bool eager = external && (definition & CHECK_INPUT_FUNCTION_INLINE) == 0;
    if (!eager)
    {
        const uint32_t object = body->object;
        uint32_t low = 0;
        uint32_t high = data->declaration_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            low = (uint32_t)(data->declaration_keys[middle] >> 32) < object ? middle + 1 : low;
            high = (uint32_t)(data->declaration_keys[middle] >> 32) < object ? high : middle;
        }
        // Over the declarations up to the definition: the attributes (a function that is not external), and the inline
        // rules (one that is): a plain inline declaration, whether a file-scope one lacks inline or spells extern, a gnu_inline.
        bool attribute = false;
        bool gnu_inline = false;
        bool plain_inline = false;
        bool c99_inline_only = true;
        for (uint32_t d = low; d < data->declaration_count && (uint32_t)(data->declaration_keys[d] >> 32) == object && (uint32_t)data->declaration_keys[d] <= body->declaration;
             d += 1)
        {
            const uint32_t token = (uint32_t)data->declaration_keys[d];
            if (external)
            {
                const bool function = input->declaration_kinds[token] == C_BINDING_FUNCTION;
                const uint8_t spelling = function && input->function_flags != 0 ? input->function_flags[token] : 0;
                gnu_inline |= check_program_attribute_gnu_inline(input->attributes[token]);
                plain_inline |= function && (spelling & (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN)) == CHECK_INPUT_FUNCTION_INLINE;
                c99_inline_only &= !(function && input->declaration_bodies[token] == BUSTER_CHECK_NONE &&
                                     ((spelling & CHECK_INPUT_FUNCTION_INLINE) == 0 || (spelling & CHECK_INPUT_FUNCTION_EXTERN) != 0));
            }
            else
            {
                attribute |= check_program_attribute_eager(input->attributes[token]);
            }
        }
        // The language mode's rules of inline functions matter to an external definition alone.
        const Preprocessor* pp = program->parser->preprocessor;
        const bool gnu = external && (gnu_inline || (pp != NULL && predefines_gnu_inline(&pp->options.predefines, pp->options.language)));
        const bool inline_only = gnu ? (definition & (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN)) ==
                                           (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN) && !plain_inline :
                                       c99_inline_only;
        eager = external ? !inline_only : attribute;
    }
    return eager;
}

// The tokens a unit's post-checking stages visit for its declarations: those that declare (`declarations[t] == t`), and
// with `nodes`, those whose nodes[t] is below `limit` too (the lowering's initializers), in token order into `tokens`,
// which has room for `count` + 16: how many. The emission's attributes and the lowering's initializer pass visit these
// rather than every token of the unit (#442). The selection reads check_data_prepare's static declarations instead
// (#592). The scalar path is the vector path's exact twin.
static uint32_t check_program_declaring_scalar(const uint32_t* declarations, const uint32_t* nodes, uint32_t limit, uint32_t count, uint32_t* tokens)
{
    uint32_t written = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        tokens[written] = token;
        written += declarations[token] == token || (nodes != 0 && nodes[token] < limit);
    }
    return written;
}

// The same, 16 tokens at a time, the loads masked to the columns and each block's tokens compressed in a register, then
// stored whole (vpcompressd to memory is 8 µops on Zen 5, the register form 2).
static uint32_t check_program_declaring_vector(const uint32_t* declarations, const uint32_t* nodes, uint32_t limit, uint32_t count, uint32_t* tokens)
{
#if !BUSTER_HOST_VECTOR
    return check_program_declaring_scalar(declarations, nodes, limit, count, tokens);
#else
    uint32_t written = 0;
    const __m512i bound = _mm512_set1_epi32((int)limit);
    __m512i index = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint32_t token = 0; token < count; token += 16)
    {
        const __mmask16 live = (__mmask16)_bzhi_u32(0xffff, count - token < 16 ? count - token : 16);
        __mmask16 keep = _mm512_mask_cmpeq_epi32_mask(live, _mm512_maskz_loadu_epi32(live, declarations + token), index);
        if (nodes != 0)
        {
            keep |= _mm512_mask_cmplt_epu32_mask(live, _mm512_maskz_loadu_epi32(live, nodes + token), bound);
        }
        _mm512_storeu_si512(tokens + written, _mm512_maskz_compress_epi32(keep, index));
        written += (uint32_t)__builtin_popcount(keep);
        index = _mm512_add_epi32(index, _mm512_set1_epi32(16));
    }
    return written;
#endif
}

static uint32_t check_program_declaring(const uint32_t* declarations, const uint32_t* nodes, uint32_t limit, uint32_t count, uint32_t* tokens, bool scalar)
{
    (void)scalar;

    return BUSTER_SCALAR_PATH(scalar) ? check_program_declaring_scalar(declarations, nodes, limit, count, tokens) :
                    check_program_declaring_vector(declarations, nodes, limit, count, tokens);
}

// How many of objects 1 to `objects` - 1 are `needed` (a byte each): the selection's symbols, before it numbers them.
static uint32_t check_program_needed_count_scalar(const uint8_t* needed, uint32_t objects)
{
    uint32_t count = 0;
    for (uint32_t object = 1; object < objects; object += 1)
    {
        count += needed[object] != 0;
    }
    return count;
}

// The same, 64 objects at a time, the loads masked to the column.
static uint32_t check_program_needed_count_vector(const uint8_t* needed, uint32_t objects)
{
#if !BUSTER_HOST_VECTOR
    return check_program_needed_count_scalar(needed, objects);
#else
    uint32_t count = 0;
    for (uint32_t object = 0; object < objects; object += 64)
    {
        const __mmask64 live = _bzhi_u64(~UINT64_C(0), objects - object < 64 ? objects - object : 64) & (object == 0 ? ~UINT64_C(1) : ~UINT64_C(0));
        const __m512i bytes = _mm512_maskz_loadu_epi8(live, needed + object);
        count += (uint32_t)__builtin_popcountll(_mm512_mask_test_epi8_mask(live, bytes, bytes));
    }
    return count;
#endif
}

// Numbers the needed objects 1 to `objects` - 1 in object order: object_symbols[object] the object's symbol (from 1;
// the column is zero for the others already) and symbol_objects[symbol - 1] its object.
static void check_program_needed_number_scalar(const uint8_t* needed, uint32_t objects, SymbolId* object_symbols, uint32_t* symbol_objects)
{
    SymbolId symbol = 0;
    for (uint32_t object = 1; object < objects; object += 1)
    {
        if (needed[object])
        {
            symbol_objects[symbol] = object;
            object_symbols[object] = ++symbol;
        }
    }
}

// The same, finding the needed objects 64 at a time: few objects of a unit are needed, so a block of none is the rule.
static void check_program_needed_number_vector(const uint8_t* needed, uint32_t objects, SymbolId* object_symbols, uint32_t* symbol_objects)
{
#if !BUSTER_HOST_VECTOR
    check_program_needed_number_scalar(needed, objects, object_symbols, symbol_objects);
#else
    SymbolId symbol = 0;
    for (uint32_t object = 0; object < objects; object += 64)
    {
        const __mmask64 live = _bzhi_u64(~UINT64_C(0), objects - object < 64 ? objects - object : 64) & (object == 0 ? ~UINT64_C(1) : ~UINT64_C(0));
        const __m512i bytes = _mm512_maskz_loadu_epi8(live, needed + object);
        uint64_t found = _mm512_mask_test_epi8_mask(live, bytes, bytes);
        while (found != 0)
        {
            const uint32_t at = object + (uint32_t)__builtin_ctzll(found);
            symbol_objects[symbol] = at;
            object_symbols[at] = ++symbol;
            found &= found - 1;
        }
    }
#endif
}

static uint32_t check_program_needed_count(const uint8_t* needed, uint32_t objects, bool scalar)
{
    (void)scalar;

    return BUSTER_SCALAR_PATH(scalar) ? check_program_needed_count_scalar(needed, objects) : check_program_needed_count_vector(needed, objects);
}

static void check_program_needed_number(const uint8_t* needed, uint32_t objects, SymbolId* object_symbols, uint32_t* symbol_objects, bool scalar)
{
    (void)scalar;

    if (BUSTER_SCALAR_PATH(scalar))
    {
        check_program_needed_number_scalar(needed, objects, object_symbols, symbol_objects);
    }
    else
    {
        check_program_needed_number_vector(needed, objects, object_symbols, symbol_objects);
    }
}

// The body whose braces hold `token`, plus one, or 0 where none does: the last body to open at or before it, if it closes
// at or after it. check_program's bodies are in token order and apart (check_program_openings), so a search of their
// openings finds what a token-sized column of every body's tokens would hold (#592), for the few tokens it is asked of.
static uint32_t check_program_token_body(const CheckProgram* program, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = program->body_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (program->bodies[middle].opening <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const uint32_t body = low != 0 && token <= program->parser->tree.parents[program->bodies[low - 1].opening] ? low : 0;
    return body;
}

// Whether the program's bodies are in token order and apart, as check_program_token_body needs them.
static bool check_program_bodies_ordered(const CheckProgram* program)
{
    bool ordered = true;
    for (uint32_t b = 1; ordered && b < program->body_count; b += 1)
    {
        ordered = program->bodies[b].opening > program->parser->tree.parents[program->bodies[b - 1].opening];
    }
    return ordered;
}

static bool check_program_select_mode(CheckProgramSelection* result, const CheckProgram* full, const CheckDataPrepared* data, CLoweringMode mode,
                                      bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    CheckProgramSelection candidate = { .token = BUSTER_CHECK_NONE, .status = CHECK_PROGRAM_INVALID };
    const CheckInput* input = full->input;
    const uint32_t objects = full->object_count;
    const uint64_t definitions64 = (uint64_t)full->body_count + data->count;
    // A reference of each body and each object, and an edge from a body to each of its block-scope statics.
    const uint64_t references64 = (uint64_t)full->reference_count + data->reference_count + data->count;
    bool valid = output != scratch && full->status == CHECK_PROGRAM_OK && data->status == CHECK_DATA_INPUT_OK && input != 0 &&
                 objects == data->object_count && objects == (uint64_t)input->token_count + 1 && definitions64 < UINT32_MAX && references64 < UINT32_MAX &&
                 check_program_bodies_ordered(full);
    const uint32_t definitions = valid ? (uint32_t)definitions64 : 0;
    // Only code generation's check of target features reads CheckProgram.body_eager, in a unit with a target attribute
    // (CheckProgram.attribute_keys), and the lowering of a lock-free query whose answer is its function's emission's
    // (CheckBuiltinAtomicEffect.late_completion): others make none. It is made by check_program_body_eager, from each
    // selected body's declarations up to its definition, not by the definitions' flags, which hold the unit's final ones.
    bool late = false;
    for (uint32_t i = 0; full->stream != 0 && i < full->stream->atomic_count; i += 1)
    {
        late |= full->stream->atomics[i].late_completion;
    }
    const bool eager_needed = full->attribute_keys != 0 || late;
    const uint32_t reference_capacity = valid ? (uint32_t)references64 : 0;
    uint64_t* order = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)definitions + 1) : 0;
    uint64_t* sorting = order != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)definitions + 1) : 0;
    uint32_t* definition_objects = sorting != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)definitions + 1) : 0;
    uint32_t* definition_flags = definition_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)definitions + 1) : 0;
    uint32_t* object_definitions = definition_flags != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects) : 0;
    // Each static object's root flags and inline rules, in the order of the data's declared objects (#592), and past them
    // the defaults, an object's that isn't one (a string literal's): no roots, and the inline rule's default.
    uint32_t* roots = object_definitions != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)data->declared_count + 1) : 0;
    uint8_t* inline_rules = roots != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)data->declared_count + 1) : 0;
    uint8_t* needed = inline_rules != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, objects) : 0;
    uint8_t* body_selected = needed != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)full->body_count + 1) : 0;
    uint8_t* data_selected = body_selected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)data->count + 1) : 0;
    uint32_t* body_map = data_selected != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)full->body_count + 1) : 0;
    uint32_t* sources = body_map != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    uint32_t* targets = sources != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    uint32_t* kinds = targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)reference_capacity + 1) : 0;
    if (valid)
    {
        candidate.status = CHECK_PROGRAM_RESOURCE;
    }
    valid &= kinds != 0;
    uint32_t edges = 0;
    uint32_t body_references = 0;
    uint32_t data_references = 0;
    CLowering plan = { 0 };
    if (valid)
    {
        memset(object_definitions, 0, (uint64_t)objects * sizeof(uint32_t));
        memset(roots, 0, ((uint64_t)data->declared_count + 1) * sizeof(uint32_t));
        memset(inline_rules, 1, (uint64_t)data->declared_count + 1);
        memset(needed, 0, objects);
        memset(body_selected, 0, (uint64_t)full->body_count + 1);
        memset(data_selected, 0, (uint64_t)data->count + 1);
        memset(body_map, 0xff, ((uint64_t)full->body_count + 1) * sizeof(uint32_t));
        // The static declarations, which check_data_prepare found (#592): of all the tokens that declare, the only ones whose
        // attributes and spellings make an object a root or an inline-only function. Both they and the declared objects
        // are in object order, so each declaration's object is found by stepping through the objects.
        uint32_t group = 0;
        for (uint32_t d = 0; d < data->declaration_count; d += 1)

        {
            const uint32_t token = (uint32_t)data->declaration_keys[d];
            const uint32_t object = (uint32_t)(data->declaration_keys[d] >> 32);
            while (group < data->declared_count && data->declared_objects[group] < object)
            {
                group += 1;
            }
            const bool function = input->declaration_kinds[token] == C_BINDING_FUNCTION;
            bool gnu_inline = false;
            roots[group] |= check_program_attribute_roots(input->attributes[token], function, &gnu_inline);
            inline_rules[group] |= gnu_inline ? 2 : 0;
            const uint8_t spelling = function && input->function_flags != 0 ? input->function_flags[token] : 0;
            // Any plain inline declaration gives GNU inline's body external linkage, even if the definition itself
            // spells extern inline. Clang applies this rule to declarations both before and after the definition.
            inline_rules[group] |= function && (spelling & (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN)) == CHECK_INPUT_FUNCTION_INLINE ? 4 : 0;
            if (function && input->declaration_bodies[token] == BUSTER_CHECK_NONE)
            {
                if ((spelling & CHECK_INPUT_FUNCTION_INLINE) == 0 || (spelling & CHECK_INPUT_FUNCTION_EXTERN) != 0)
                {
                    inline_rules[group] &= (uint8_t)~1u;
                }
            }
        }
        for (uint32_t body = 0; body < full->body_count; body += 1)
        {
            const CheckProgramBody* function = full->bodies + body;
            order[body] = ((uint64_t)function->declaration << 32) | body;
        }
        for (uint32_t object = 0; object < data->count; object += 1)
        {
            order[full->body_count + object] = ((uint64_t)data->objects[object].token << 32) | (full->body_count + object);
        }
        order = c_bindings_sort(order, sorting, definitions, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX);
        for (uint32_t definition = 0; valid && definition < definitions; definition += 1)
        {
            const uint32_t raw = (uint32_t)order[definition];
            const bool function = raw < full->body_count;
            const uint32_t object = function ? full->bodies[raw].object : data->objects[raw - full->body_count].object;
            valid = object != 0 && object < objects && object_definitions[object] == 0;
            if (valid)
            {
                // The definition's object among the static objects, or past them (check_data_declared_index).
                const uint32_t declared = check_data_declared_index(data, object);
                object_definitions[object] = definition + 1;
                definition_objects[definition] = object;
                uint32_t flags = (function ? BUSTER_C_LOWERING_FUNCTION : BUSTER_C_LOWERING_OBJECT) | roots[declared];
                const uint32_t declaration = full->object_declarations[object];
                if (declaration != BUSTER_CHECK_NONE && input->linkage[declaration] == 2)
                {
                    bool inline_only = false;
                    if (function)
                    {
                        const uint32_t body_declaration = full->bodies[raw].declaration;
                        const uint8_t spelling = input->function_flags != 0 ? input->function_flags[body_declaration] : 0;
                        const Preprocessor* pp = full->parser->preprocessor;
                        const bool gnu = (inline_rules[declared] & 2) != 0 ||
                                         (pp != NULL && predefines_gnu_inline(&pp->options.predefines, pp->options.language));
                        inline_only = gnu ? (spelling & (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN)) ==
                                                (CHECK_INPUT_FUNCTION_INLINE | CHECK_INPUT_FUNCTION_EXTERN) && (inline_rules[declared] & 4) == 0 :
                                      (inline_rules[declared] & 1) != 0;
                    }
                    flags |= inline_only ? BUSTER_C_LOWERING_INLINE_ONLY : BUSTER_C_LOWERING_EXTERNAL;
                }
                definition_flags[definition] = flags;
            }
        }
        candidate.status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        for (uint32_t definition = 0; valid && definition < definitions; definition += 1)
        {
            const uint32_t raw = (uint32_t)order[definition];
            const bool function = raw < full->body_count;
            const uint32_t first = function ? full->bodies[raw].reference_first : data->reference_offsets[raw - full->body_count];
            const uint32_t count = function ? full->bodies[raw].reference_count : data->reference_counts[raw - full->body_count];
            const uint32_t* references = function ? full->references : data->references;
            const uint32_t limit = function ? full->reference_count : data->reference_count;
            valid = first <= limit && count <= limit - first;
            for (uint32_t reference = 0; valid && reference < count; reference += 1)
            {
                const uint32_t object = references[first + reference];
                valid = object != 0 && object < objects && edges < reference_capacity;
                if (valid)
                {
                    // An address of a label (GNU C's &&label, its object the label's token plus one) is its function's:
                    // the function's code defines the label's symbol.
                    const uint32_t label = object - 1;
                    const uint32_t owner = full->parser->tree.roles[label] == PARSE_ROLE_LABEL ? check_program_token_body(full, label) : 0;
                    const uint32_t target = object_definitions[owner != 0 ? full->bodies[owner - 1].object : object];
                    sources[edges] = definition;
                    targets[edges] = target != 0 ? target - 1 : BUSTER_C_LOWERING_NONE;
                    kinds[edges++] = target != 0 ? C_LOWERING_REFERENCE_EVALUATED : C_LOWERING_REFERENCE_EXTERNAL;
                }
            }
            // A block-scope static (declared in a body, without linkage, not a compound literal), from its body.
            const uint32_t object = function ? 0 : data->objects[raw - full->body_count].object;
            const uint32_t declaration = object != 0 ? full->object_declarations[object] : BUSTER_CHECK_NONE;
            const uint32_t body = declaration < input->token_count && input->declaration_bodies[declaration] != BUSTER_CHECK_NONE &&
                                  input->linkage[declaration] == 0 && !input->compound_objects[declaration] ? check_program_token_body(full, declaration) : 0;
            if (valid && body != 0)
            {
                const uint32_t source = object_definitions[full->bodies[body - 1].object];
                valid = source != 0 && edges < reference_capacity;
                if (valid)
                {
                    sources[edges] = source - 1;
                    targets[edges] = definition;
                    kinds[edges++] = C_LOWERING_REFERENCE_EVALUATED;
                }
            }
        }
        const CLoweringInput graph = { .definition_count = definitions, .reference_count = edges, .definition_flags = definition_flags,
                                      .reference_source = sources, .reference_target = targets, .reference_kind = kinds };
        candidate.status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        valid = valid && c_lowering_prepare(scratch, graph, &plan) == C_LOWERING_OK;
        valid = valid && (BUSTER_SCALAR_PATH(scalar) ? c_lowering_scalar(&plan, mode) : c_lowering_vector(&plan, mode)) == C_LOWERING_OK;
        if (valid)
        {
            for (uint32_t selected = 0; selected < plan.selected_count; selected += 1)
            {
                const uint32_t definition = plan.selected[selected];
                const uint32_t raw = (uint32_t)order[definition];
                needed[definition_objects[definition]] = 1;
                if (raw < full->body_count)
                {
                    body_selected[raw] = 1;
                    candidate.selected_functions += 1;
                    body_references += full->bodies[raw].reference_count;
                }
                else
                {
                    data_selected[raw - full->body_count] = 1;
                    candidate.selected_objects += 1;
                    data_references += data->reference_counts[raw - full->body_count];
                }
            }
        }
    }
    if (valid)
    {
        candidate.program = *full;
        candidate.data = *data;
        candidate.program.body_count = candidate.selected_functions;
        candidate.program.reference_count = body_references;
        candidate.program.builtin_count = 0;
        candidate.program.symbol_count = 0;
        candidate.data.count = candidate.selected_objects;
        candidate.data.reference_count = data_references;
        candidate.program.bodies = candidate.selected_functions != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramBody, candidate.selected_functions) : 0;
        // Each selected body's instruction set, as its body is the full program's: the targets themselves are shared.
        const bool targeted = full->body_targets != 0 && candidate.selected_functions != 0;
        candidate.program.body_targets = targeted ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.selected_functions) : 0;
        candidate.program.body_eager = eager_needed && candidate.selected_functions != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, candidate.selected_functions) : 0;
        candidate.program.references = body_references != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, body_references) : 0;
        candidate.program.regions = full->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramRegion, full->region_count) : 0;
        candidate.data.objects = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataObject, candidate.selected_objects) : 0;
        candidate.data.reference_offsets = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.selected_objects) : 0;
        candidate.data.reference_counts = candidate.selected_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.selected_objects) : 0;
        candidate.data.references = data_references != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, data_references) : 0;
        candidate.program.object_symbols = BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, objects);
        valid = (candidate.selected_functions == 0 || (candidate.program.bodies != 0 && (!eager_needed || candidate.program.body_eager != 0))) &&
                (!targeted || candidate.program.body_targets != 0) &&
                (body_references == 0 || candidate.program.references != 0) &&
                (full->region_count == 0 || candidate.program.regions != 0) && candidate.program.object_symbols != 0 &&
                (candidate.selected_objects == 0 || (candidate.data.objects != 0 && candidate.data.reference_offsets != 0 && candidate.data.reference_counts != 0)) &&
                (data_references == 0 || candidate.data.references != 0);
    }
    if (valid)
    {
        uint32_t bodies = 0;
        uint32_t references = 0;
        for (uint32_t body = 0; body < full->body_count; body += 1)
        {
            if (body_selected[body])
            {
                const CheckProgramBody* source = full->bodies + body;
                body_map[body] = bodies;
                if (candidate.program.body_targets != 0)
                {
                    candidate.program.body_targets[bodies] = full->body_targets[body];
                }
                if (candidate.program.body_eager != 0)
                {
                    candidate.program.body_eager[bodies] = check_program_body_eager(full, data, source);
                }
                candidate.program.bodies[bodies] = *source;
                candidate.program.bodies[bodies++].reference_first = references;
                for (uint32_t r = 0; r < source->reference_count; r += 1)
                {
                    const uint32_t object = full->references[source->reference_first + r];
                    candidate.program.references[references++] = object;
                    needed[object] = 1;
                }
            }
        }
        for (uint32_t region = 0; region < full->region_count; region += 1)
        {
            candidate.program.regions[region] = full->regions[region];
            const uint32_t body = full->regions[region].body;
            candidate.program.regions[region].body = body < full->body_count ? body_map[body] : BUSTER_CHECK_NONE;
        }
        uint32_t written = 0;
        references = 0;
        for (uint32_t record = 0; record < data->count; record += 1)
        {
            if (data_selected[record])
            {
                candidate.data.objects[written] = data->objects[record];
                candidate.data.reference_offsets[written] = references;
                candidate.data.reference_counts[written++] = data->reference_counts[record];
                for (uint32_t r = 0; r < data->reference_counts[record]; r += 1)
                {
                    const uint32_t object = data->references[data->reference_offsets[record] + r];
                    candidate.data.references[references++] = object;
                    needed[object] = 1;
                }
            }
        }
        for (uint32_t builtin = 0; builtin < full->builtin_count; builtin += 1)
        {
            const uint32_t node = full->builtin_nodes[builtin];
            const uint32_t body = check_program_token_body(full, input->graph.tokens[node]);
            candidate.program.builtin_count += body != 0 && body_selected[body - 1];
        }
        candidate.program.builtin_nodes = candidate.program.builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.program.builtin_count) : 0;
        valid = candidate.program.builtin_count == 0 || candidate.program.builtin_nodes != 0;
        uint32_t builtins = 0;
        for (uint32_t builtin = 0; valid && builtin < full->builtin_count; builtin += 1)
        {
            const uint32_t node = full->builtin_nodes[builtin];
            const uint32_t body = check_program_token_body(full, input->graph.tokens[node]);
            if (body != 0 && body_selected[body - 1])
            {
                candidate.program.builtin_nodes[builtins++] = node;
            }
        }
        memset(candidate.program.object_symbols, 0, (uint64_t)objects * sizeof(SymbolId));
        candidate.program.symbol_count = check_program_needed_count(needed, objects, BUSTER_SCALAR_PATH(scalar));
        candidate.program.symbol_objects = candidate.program.symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, candidate.program.symbol_count) : 0;
        valid = valid && (candidate.program.symbol_count == 0 || candidate.program.symbol_objects != 0);
        if (valid)
        {
            check_program_needed_number(needed, objects, candidate.program.object_symbols, candidate.program.symbol_objects, BUSTER_SCALAR_PATH(scalar));
        }
    }
    if (valid)
    {
        candidate.defined_functions = full->body_count;
        candidate.defined_objects = data->count;
        candidate.statistics = plan.statistics;
        candidate.status = CHECK_PROGRAM_OK;
        candidate.program.output_bytes = output->length - mark;
        candidate.data.capacity.atoms = candidate.data.count;
        candidate.data.capacity.fixups = candidate.data.reference_count;
        candidate.data.capacity.wide_addends = candidate.data.reference_count;
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, mark);
        *result = (CheckProgramSelection){ .status = candidate.status, .token = candidate.token };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// The definitions reachable from the unit's roots: what -c and the direct path lower.
static bool check_program_select(CheckProgramSelection* result, const CheckProgram* full, const CheckDataPrepared* data, bool scalar,
                                 Arena* output, Arena* scratch)
{
    (void)scalar;

    return check_program_select_mode(result, full, data, C_LOWERING_REACHABLE, BUSTER_SCALAR_PATH(scalar), output, scratch);
}

#endif
