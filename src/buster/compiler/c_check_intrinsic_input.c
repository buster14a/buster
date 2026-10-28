// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact catalogue declarations and original source call operands. Include after CheckInputReader, the ordinary
// binding API, check_input_predefined_type and check_constant_punctuation. Initialization uses the catalogue's
// scalar/vector batch twins; the source-ordered prepare step consults the current ordinary scope before its cache.
// It creates no CBinding and changes no graph, ignored-token or inferred-type column. A prepared record supplies
// the exact ordinary prototype; call conversions, source ICE witnesses, target features, custom x86 constraints,
// reachability, effects, constant folding and lowering remain separate consumer obligations.
//
// A catalogue hit is not a capability grant. In particular no prefix establishes membership in the separately
// attested frozen x86 constraint inventory. All immutable attributes/features/language/header/shard provenance is
// retained through info. Custom checking, ignored signatures and unsupported descriptors never acquire an ordinary
// call record. Failed descriptor facts may be cached; a resource failure publishes neither that declaration nor a
// partial call. Type interning remains monotonic as documented by builtin_intrinsic_types_decode.
//
// Source argument slices are half-open parser-token intervals, excluding commas and parentheses. Their actual
// count is retained even when it disagrees with the prototype. No argument is evaluated here. Call ids are index+1
// at the original CLOSE_CALL only, with zero everywhere else and at the token sentinel. Name-cache entries are
// UINT32_MAX for an unscanned name, zero for a miss, the high bit plus a catalogue index for a hit not yet decoded,
// otherwise a dense declaration index+1. Initialization requires
// fresh output/cache fields; a TU with no exact operand candidates allocates no intrinsic storage.
#ifndef BUSTER_C_CHECK_INTRINSIC_INPUT_INCLUDED
#define BUSTER_C_CHECK_INTRINSIC_INPUT_INCLUDED

typedef enum CheckInputIntrinsicStatus
{
    CHECK_INPUT_INTRINSIC_NOT_FOUND,
    CHECK_INPUT_INTRINSIC_PREPARED,
    CHECK_INPUT_INTRINSIC_FAILED,
} CheckInputIntrinsicStatus;

typedef struct CheckInputIntrinsicDeclaration CheckInputIntrinsicDeclaration;
struct CheckInputIntrinsicDeclaration
{
    const BuiltinIntrinsicInfo* info;
    BuiltinIntrinsicTypeResult types;
};

typedef struct CheckInputIntrinsicArgument CheckInputIntrinsicArgument;
struct CheckInputIntrinsicArgument
{
    uint32_t first;
    uint32_t end;
};

typedef struct CheckInputIntrinsicCall CheckInputIntrinsicCall;
struct CheckInputIntrinsicCall
{
    uint32_t declaration;
    uint32_t keyword;
    uint32_t opening;
    uint32_t closing;
    uint32_t function_body;
    uint32_t argument_count;
    const CheckInputIntrinsicArgument* arguments;
};

static bool check_input_intrinsic_call_shape(const Parser* parser, uint32_t keyword)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t closing = keyword < count ? parser->tree.parents[keyword] : count;
    const bool call = keyword < count && keyword + 1 < closing && closing < count &&
                      parser->tree.roles[keyword] == PARSE_ROLE_OPERAND &&
                      parser->tree.roles[keyword + 1] == PARSE_ROLE_OPEN_CALL &&
                      parser->tree.roles[closing] == PARSE_ROLE_CLOSE_CALL &&
                      parser->tree.parents[keyword + 1] == closing;
    return call;
}

// A call's source-position identity survives declaration-query suspension and binding replay. Do not look up
// its spelling in the current scope when a type-only consumer resumes after that scope has changed.
static TypeId check_input_intrinsic_type(const CheckInputReader* reader, uint32_t keyword)
{
    TypeId type = 0;
    if (reader->output->intrinsic_ids != 0 && check_input_intrinsic_call_shape(reader->parser, keyword))
    {
        const uint32_t close = reader->parser->tree.parents[keyword];
        const uint32_t id = reader->output->intrinsic_ids[close];
        if (id != 0)
        {
            const CheckInputIntrinsicCall* call = reader->output->intrinsic_calls + id - 1;
            type = reader->output->intrinsic_declarations[call->declaration].types.function_type;
        }
    }
    return type;
}

// The tokens from `first` of the next 64 (a bit each) that are identifier operands outside the deferred bodies, which
// the intrinsics' tables are sized for. The vector path tests 64 at once; the scalar path, its twin, takes every token,
// and the caller tests each.
static uint64_t check_input_intrinsic_operands(const CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t* deferred_tokens = reader->output->deferred_tokens;
    uint64_t result = check_input_scan_all(first, count);
    if (!BUSTER_SCALAR_PATH(parser->options.scalar))
    {
        result = check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_IDENTIFIER) &
                 check_input_scan_equal(parser->tree.roles, first, count, PARSE_ROLE_OPERAND);
        if (deferred_tokens != 0 && reader->deferred == 0)
        {
            result &= ~check_input_scan_nonzero(deferred_tokens, first, count);
        }
    }
    return result;
}

static bool check_input_intrinsic_operand(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint8_t* deferred_tokens = reader->output->deferred_tokens;
    return parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_OPERAND &&
           (deferred_tokens == 0 || reader->deferred != 0 || !deferred_tokens[token]);
}

// Looks the batch's `lanes` names up, and records each intrinsic found in the cache: false without a cache to record it.
static bool check_input_intrinsic_lookup(const Parser* parser, BuiltinIntrinsicName* names, const uint32_t* names_in_batch, uint32_t lanes,
                                         uint32_t* cache, uint32_t* declarations)
{
    const BuiltinIntrinsicInfo* infos[8];
    builtin_intrinsic_lookup_batch(names, infos, lanes, BUSTER_SCALAR_PATH(parser->options.scalar));
    bool valid = true;
    for (uint32_t lane = 0; valid && lane < lanes; lane += 1)
    {
        if (infos[lane] != 0)
        {
            valid = cache != 0;
            if (valid)
            {
                cache[names_in_batch[lane]] = UINT32_C(0x80000000) | (uint32_t)(infos[lane] - builtin_intrinsic_infos);
                *declarations += 1;
            }
        }
    }
    return valid;
}

static bool check_input_intrinsic_initialize(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const InternTable* table = reader->pp->table;
    CheckInput* output = reader->output;
    const uint64_t mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    BuiltinIntrinsicName names[8];
    uint32_t names_in_batch[8];
    uint32_t* cache = 0;
    uint32_t lanes = 0;
    uint32_t declarations = 0;
    uint32_t calls = 0;
    bool valid = true;
    bool without_cache = false;
    // #113's deferred bodies are read after this pass, which sizes the tables for them too, as source order does.
    for (uint32_t first = 0; valid && first < parser->tokens.count; first += 64)
    {
        for (uint64_t selected = check_input_intrinsic_operands(reader, first); valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            if (check_input_intrinsic_operand(reader, token))
            {
                const uint32_t name = parser->tokens.names[token];
                if (cache == 0 && !without_cache)
                {
                    cache = arena_push_array(reader->scratch, table->count, sizeof(uint32_t), 64);
                    without_cache = cache == 0;
                    if (!without_cache)
                    {
                        memset(cache, 0xff, (uint64_t)table->count * sizeof(uint32_t));
                    }
                }
                // Without scratch, look up every operand, including repeated names. A hit fails immediately because its
                // tables need the cache; an all-miss unit succeeds without a quadratic search of earlier operands.
                const bool fresh = without_cache || cache[name] == UINT32_MAX;
                if (fresh)
                {
                    const InternSpan span = table->spans[name];
                    names[lanes] = (BuiltinIntrinsicName){ (const char*)table->records + span.offset, span.length };
                    names_in_batch[lanes] = name;
                    if (!without_cache)
                    {
                        cache[name] = 0;
                    }
                    lanes += 1;
                }
            }
            if (lanes == BUSTER_ARRAY_LENGTH(names))
            {
                valid = check_input_intrinsic_lookup(parser, names, names_in_batch, lanes, cache, &declarations);
                lanes = 0;
            }
        }
    }
    if (valid && lanes != 0)
    {
        valid = check_input_intrinsic_lookup(parser, names, names_in_batch, lanes, cache, &declarations);
        lanes = 0;
    }
    if (valid && declarations != 0)
    {
        for (uint32_t first = 0; first < parser->tokens.count; first += 64)
        {
            for (uint64_t selected = check_input_intrinsic_operands(reader, first); selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
                if (check_input_intrinsic_operand(reader, token) && cache[parser->tokens.names[token]] != UINT32_MAX &&
                    (cache[parser->tokens.names[token]] & UINT32_C(0x80000000)) != 0)
                {
                    calls += check_input_intrinsic_call_shape(parser, token);
                }
            }
        }
        uint32_t* ids = arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint32_t), 64);
        CheckInputIntrinsicDeclaration* declaration_records = arena_push_array(reader->arena, declarations, sizeof(CheckInputIntrinsicDeclaration), 8);
        CheckInputIntrinsicCall* call_records = calls != 0 ? arena_push_array(reader->arena, calls, sizeof(CheckInputIntrinsicCall), 8) : 0;
        valid = cache != 0 && ids != 0 && declaration_records != 0 && (calls == 0 || call_records != 0);
        if (valid)
        {
            memset(ids, 0, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
            reader->intrinsic_names = cache;
            output->intrinsic_ids = ids;
            output->intrinsic_declarations = declaration_records;
            output->intrinsic_declaration_count = 0;
            output->intrinsic_declaration_capacity = declarations;
            output->intrinsic_calls = call_records;
            output->intrinsic_call_count = 0;
            output->intrinsic_call_capacity = calls;
        }
    }
    else if (valid)
    {
        reader->scratch->length = scratch_mark;
    }
    if (!valid)
    {
        reader->arena->length = mark;
        reader->scratch->length = scratch_mark;
        output->resource = true;
    }
    return valid;
}

static CheckInputIntrinsicDeclaration check_input_intrinsic_decode(CheckInputReader* reader, const BuiltinIntrinsicInfo* info)
{
    CheckInputIntrinsicDeclaration declaration = { .info = info };
    BuiltinIntrinsicTypeInput input =
    {
        .encoding = info->encoding,
        .encoding_length = info->encoding_length,
        .attributes = info->attributes,
        .attributes_length = info->attributes_length,
        .custom_typechecking = info->custom_typechecking,
        .strict_prototypes = reader->parser->language.standard >= 2023,
    };
    const bool va_list = memchr(info->encoding, 'a', info->encoding_length) != 0 || memchr(info->encoding, 'A', info->encoding_length) != 0;
    if (va_list && !info->custom_typechecking)
    {
        input.va_list_type = check_input_predefined_type(reader, reader->predefined_names[4]);
    }
    builtin_intrinsic_types_decode(reader->types, &input, &declaration.types);
    return declaration;
}

static CheckInputIntrinsicStatus check_input_intrinsic_prepare(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    CheckInput* output = reader->output;
    CheckInputIntrinsicStatus status = CHECK_INPUT_INTRINSIC_NOT_FOUND;
    const bool operand = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_IDENTIFIER &&
                         parser->tree.roles[keyword] == PARSE_ROLE_OPERAND && (output->deferred_tokens == 0 || !output->deferred_tokens[keyword]);
    const uint32_t name = operand ? parser->tokens.names[keyword] : 0;
    if (operand && reader->intrinsic_names != 0 && c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) == 0)
    {
        const uint32_t cached = reader->intrinsic_names[name];
        const bool fresh = (cached & UINT32_C(0x80000000)) != 0;
        const BuiltinIntrinsicInfo* info = 0;
        if (fresh)
        {
            if (cached == UINT32_MAX)
            {
                const InternSpan span = reader->pp->table->spans[name];
                info = builtin_intrinsic_lookup((const char*)reader->pp->table->records + span.offset, span.length);
            }
            else
            {
                info = builtin_intrinsic_at(cached & UINT32_C(0x7fffffff));
            }
            if (info == 0)
            {
                reader->intrinsic_names[name] = 0;
            }
        }
        else if (cached != 0)
        {
            info = output->intrinsic_declarations[cached - 1].info;
        }
        if (info != 0)
        {
            status = CHECK_INPUT_INTRINSIC_FAILED;
            const uint32_t index = fresh ? output->intrinsic_declaration_count : cached - 1;
            const bool capacity = !fresh || index < output->intrinsic_declaration_capacity;
            output->resource |= !capacity;
            CheckInputIntrinsicDeclaration declaration = { 0 };
            if (capacity)
            {
                declaration = fresh ? check_input_intrinsic_decode(reader, info) : output->intrinsic_declarations[index];
                output->resource |= declaration.types.status == BUILTIN_INTRINSIC_TYPES_CAPACITY;
            }
            const bool ordinary = capacity && !output->resource && declaration.types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS &&
                                  !declaration.types.signature_ignored;
            const bool call = check_input_intrinsic_call_shape(parser, keyword);
            const uint32_t closing = call ? parser->tree.parents[keyword] : parser->tokens.count;
            const uint32_t existing = call ? output->intrinsic_ids[closing] : 0;
            uint32_t count = 0;
            CheckInputIntrinsicArgument* arguments = 0;
            const uint64_t mark = reader->arena->length;
            if (ordinary && call && existing == 0)
            {
                for (uint32_t first = keyword + 2; first < closing;)
                {
                    const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
                    count += 1;
                    first = separator + 1;
                }
                const bool room = output->intrinsic_call_count < output->intrinsic_call_capacity;
                arguments = room && count != 0 ? arena_push_array(reader->arena, count, sizeof(CheckInputIntrinsicArgument), 4) : 0;
                output->resource |= !room || (count != 0 && arguments == 0);
                uint32_t argument = 0;
                for (uint32_t first = keyword + 2; !output->resource && first < closing;)
                {
                    const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
                    arguments[argument] = (CheckInputIntrinsicArgument){ first, separator };
                    argument += 1;
                    first = separator + 1;
                }
            }
            if (!output->resource)
            {
                if (fresh)
                {
                    output->intrinsic_declarations[index] = declaration;
                    output->intrinsic_declaration_count += 1;
                    reader->intrinsic_names[name] = index + 1;
                }
                if (ordinary && call)
                {
                    if (existing == 0)
                    {
                        const uint32_t call_index = output->intrinsic_call_count;
                        output->intrinsic_calls[call_index] = (CheckInputIntrinsicCall)
                        {
                            .declaration = index,
                            .keyword = keyword,
                            .opening = keyword + 1,
                            .closing = closing,
                            .function_body = reader->function_body,
                            .argument_count = count,
                            .arguments = arguments,
                        };
                        output->intrinsic_call_count += 1;
                        output->intrinsic_ids[closing] = call_index + 1;
                    }
                    status = CHECK_INPUT_INTRINSIC_PREPARED;
                }
                else
                {
                    check_input_report(reader, keyword, CHECK_INPUT_UNSUPPORTED);
                }
            }
            else
            {
                reader->arena->length = mark;
            }
        }
    }
    return status;
}

#endif
