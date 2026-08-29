// Parsing and semantic analysis, the second stage of the frontend. Two
// passes over the preprocessed token stream, entered through c_parse_ast and
// c_analyze_semantics at the bottom of the file (c_parse runs both):
// - c_parse_ast is one linear scan that splits the stream into top-level
//   CParserDeclaration records — token extents, the name token, the body
//   range, and typedef/constexpr/variadic flags — by delimiter counting
//   alone. It builds no tree; every later consumer re-walks token ranges.
//   One record is one declarator, not one declaration: a comma-separated
//   list is split into a record per declarator, each keeping the shared
//   specifiers in token_start/token_count and its own declarator segment in
//   declarator_start/declarator_count.
// - c_analyze_semantics sizes its tables from a token census
//   (c_parse_token_census, vectorized), then builds the CParseResult the
//   lowering stage consumes: interned types, entities, scopes, and
//   diagnostics.
//
// Types and declarators are parsed by CTypeParseMachine (types in
// c_internal.h), an explicit frame stack in place of recursion: each
// CTypeParseFrameKind has a c_type_parse_*_step function dispatched by
// c_type_parse_machine_run. Speculative parses record every published-type
// mutation (c_type_parse_record_mutation) so c_type_parse_rollback can
// restore the shared type table after a failed attempt.
//
// Layout, in file order; each anchor is a definition to search for:
//   c_parse_token_class_compute,                  keyword and token
//   c_parse_position_index_build                  classification, the
//                                                 matching-delimiter index
//   c_parse_builtin_type_layout,                  target-dependent type
//   c_parse_type_layout                           sizes/alignments, aggregate
//                                                 and bit-field layout
//   c_parse_direct_expression_type ..             expression typing without
//   c_parse_conditional_expression_type           lowering (usual arithmetic
//                                                 conversions, precedence)
//   c_parse_static_assert_evaluate                _Static_assert, including
//                                                 deferral past unresolved
//                                                 array bounds
//   c_parse_initializer_designator,               initializer shapes and
//   c_parse_infer_initializer_array_count_core    array-bound inference
//   c_parse_add_type, c_parse_aggregate_lookup,   type interning and
//   c_parse_primitive_type                        construction, attributes
//   c_parse_word_bits_compute,                    specifier words answered
//   c_parse_word_bits_token                       from the interned symbol
//                                                 id (C_WORD_* bits), with
//                                                 the spelling ladders as
//                                                 the symbol-0 fallback
//   c_type_parse_alignment_step ..                the type-parse machine
//   c_type_parse_machine_run                      steps
//   c_parse_scalar_type_core_begin,               declarators: pointers,
//   c_parse_pointer_chain, c_parse_array_suffixes arrays, parameters
//   c_parse_validate_constexpr_declaration,       constexpr, type
//   c_parse_types_compatible                      compatibility
//   c_parse_validate_cleanup_attribute            __attribute__((cleanup))
//   c_parse_name_symbol .. c_parse_lookup_*       symbol interning, scopes,
//                                                 entity lookup
//   c_parse_local_declarations                    block-scope declarations,
//                                                 auto inference, local
//                                                 functions
//   c_parse_label_address_prefix_proven,          statement boundaries, asm
//   c_parse_statement_end                         goto, label addresses
//   c_parse_bind_function_body                    binds body identifiers to
//                                                 entities, indexes scopes
//   c_parse_token_census_reference,               the token-shape census that
//   c_parse_token_census                          sizes c_analyze_semantics'
//                                                 tables: contiguous sidecar
//                                                 shape compares plus a
//                                                 symbol-byte projection for
//                                                 the rare `for` spelling,
//                                                 with the scalar reference
//                                                 kept as the differential gate
//   c_parse_ast, c_analyze_semantics, c_parse     the stage entry points

#include "c_internal.h"

BUSTER_C_INTERNAL bool c_declaration_keyword(String8 spelling)
{
    if (!c_declaration_keyword_slots_built)
    {
        c_declaration_keyword_slots_build();
    }
    u32 slot = (u32)(c_macro_name_hash(spelling) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1));
    while (c_declaration_keyword_slots[slot])
    {
        if (string_equal(spelling, c_declaration_keyword_spellings[c_declaration_keyword_slots[slot] - 1]))
        {
            return true;
        }
        slot = (slot + 1) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1);
    }
    return false;
}
BUSTER_C_INTERNAL bool c_declaration_keyword_for_dialect(String8 spelling, CPreprocessDialect dialect)
{
    return c_declaration_keyword(spelling) ||
           (c_preprocess_dialect_is_c23(dialect) &&
             (string_equal(spelling, S8("true")) || string_equal(spelling, S8("false")) || string_equal(spelling, S8("nullptr")) ||
              string_equal(spelling, S8("alignof")) || string_equal(spelling, S8("constexpr")) || string_equal(spelling, S8("typeof_unqual")))) ||
           ((c_preprocess_dialect_is_gnu(dialect) || c_preprocess_dialect_is_c23(dialect)) && string_equal(spelling, S8("typeof")));
}

// Spelling predicates the parser's scan loops ask per token, per scan, cached
// as one class byte per token index. Token spellings are fixed once
// preprocessing finishes, so the byte is computed at most once per token no
// matter how many declaration scans revisit it, and speculative-parse
// rollbacks cannot invalidate it.
enum
{
    C_TOKEN_CLASS_COMPUTED = 1 << 0,
    C_TOKEN_CLASS_DECLARATION_KEYWORD = 1 << 1,
    C_TOKEN_CLASS_C23_EXTRA_KEYWORD = 1 << 2,
    C_TOKEN_CLASS_TYPEOF = 1 << 3,
    C_TOKEN_CLASS_VECTOR_SIZE = 1 << 4,
    C_TOKEN_CLASS_ALIGNAS = 1 << 5,
};

// Specifier-word facts stored per predefined symbol id in
// CSymbolTable.word_bits, derived at table-create time from the same
// spelling predicates the `_token` variants below fall back to, so the two
// paths cannot drift. The qualifier bits carry which qualifier matched
// because c_parse_type_qualifier_word reports its answer by setting the
// matching CType flag.
enum
{
    C_WORD_TYPE = 1 << 0,
    C_WORD_AUTO_TYPE = 1 << 1,
    C_WORD_TYPEOF = 1 << 2,
    C_WORD_CONSTEXPR = 1 << 3,
    C_WORD_TYPEOF_UNQUAL = 1 << 4,
    C_WORD_QUALIFIER_CONST = 1 << 5,
    C_WORD_QUALIFIER_VOLATILE = 1 << 6,
    C_WORD_QUALIFIER_RESTRICT = 1 << 7,
    C_WORD_QUALIFIER_ATOMIC = 1 << 8,
    C_WORD_STORAGE_PREFIX = 1 << 9,
};

#define C_WORD_QUALIFIER_ANY (C_WORD_QUALIFIER_CONST | C_WORD_QUALIFIER_VOLATILE | C_WORD_QUALIFIER_RESTRICT | C_WORD_QUALIFIER_ATOMIC)

// The `_token` specifier predicates: same answers as their String8
// counterparts, but an interned token settles on one word_bits load instead
// of a spelling ladder. Symbol 0 (pasted, synthesized, or test-built tokens)
// falls back to the spelling compute, so a missed path costs speed and never
// correctness; a symbol above predefined_limit is a constant-time "no"
// because every specifier-word spelling is interned into the predefined
// range. Defined after the spelling ladders they derive from.
BUSTER_C_INTERNAL u16 c_parse_word_bits_token(CPreprocessResult preprocess, CToken token);
BUSTER_C_INTERNAL bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token);
BUSTER_C_INTERNAL bool c_parse_auto_type_word_token(CPreprocessResult preprocess, CToken token);
BUSTER_C_INTERNAL bool c_parse_type_qualifier_word_token(CPreprocessResult preprocess, CToken token, CType* type);
BUSTER_C_INTERNAL bool c_parse_atomic_declaration_prefix_token(CPreprocessResult preprocess, CToken token, CType* qualifiers);

// The declaration-keyword pair answered from class_bits by symbol id; the
// bits come from c_parse_token_class_compute over the same spellings, so
// the id path and the spelling path cannot drift.
BUSTER_C_INTERNAL bool c_declaration_keyword_token(CPreprocessResult preprocess, CToken token)
{
    bool result;
    if (token.symbol && preprocess.symbols)
    {
        result = token.symbol <= preprocess.symbols->predefined_limit &&
                 (preprocess.symbols->class_bits[token.symbol] & C_TOKEN_CLASS_DECLARATION_KEYWORD) != 0;
    }
    else
    {
        result = c_declaration_keyword(c_token_spelling(preprocess.spelling_base, token));
    }
    return result;
}

BUSTER_C_INTERNAL bool c_declaration_keyword_for_dialect_token(CPreprocessResult preprocess, CToken token)
{
    bool result;
    if (token.symbol && preprocess.symbols)
    {
        u8 token_class = token.symbol <= preprocess.symbols->predefined_limit ? preprocess.symbols->class_bits[token.symbol] : 0;
        result = (token_class & C_TOKEN_CLASS_DECLARATION_KEYWORD) != 0 ||
                 (c_preprocess_dialect_is_c23(preprocess.dialect) && (token_class & C_TOKEN_CLASS_C23_EXTRA_KEYWORD) != 0) ||
                 ((c_preprocess_dialect_is_gnu(preprocess.dialect) || c_preprocess_dialect_is_c23(preprocess.dialect)) &&
                  (token_class & C_TOKEN_CLASS_TYPEOF) != 0);
    }
    else
    {
        result = c_declaration_keyword_for_dialect(c_token_spelling(preprocess.spelling_base, token), preprocess.dialect);
    }
    return result;
}

BUSTER_C_SHARED u8 c_parse_token_class_compute(String8 spelling)
{
    u8 token_class = C_TOKEN_CLASS_COMPUTED;
    if (c_declaration_keyword(spelling))
    {
        token_class |= C_TOKEN_CLASS_DECLARATION_KEYWORD;
    }
    if (string_equal(spelling, S8("true")) || string_equal(spelling, S8("false")) || string_equal(spelling, S8("nullptr")) ||
        string_equal(spelling, S8("alignof")) || string_equal(spelling, S8("constexpr")) || string_equal(spelling, S8("typeof_unqual")))
    {
        token_class |= C_TOKEN_CLASS_C23_EXTRA_KEYWORD;
    }
    if (string_equal(spelling, S8("typeof")))
    {
        token_class |= C_TOKEN_CLASS_TYPEOF;
    }
    if (string_equal(spelling, S8("vector_size")) || string_equal(spelling, S8("__vector_size")) || string_equal(spelling, S8("__vector_size__")))
    {
        token_class |= C_TOKEN_CLASS_VECTOR_SIZE;
    }
    if (string_equal(spelling, S8("_Alignas")))
    {
        token_class |= C_TOKEN_CLASS_ALIGNAS;
    }
    return token_class;
}

BUSTER_C_INTERNAL u8 c_parse_token_class(CParseResult* result, CPreprocessResult preprocess, u32 token_index)
{
    if (!result->token_classes || token_index >= result->identifier_use_by_token_capacity)
    {
        return c_parse_token_class_compute(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]));
    }
    u8 token_class = result->token_classes[token_index];
    if (!(token_class & C_TOKEN_CLASS_COMPUTED))
    {
        u32 symbol = preprocess.tokens[token_index].symbol;
        if (symbol && result->symbols)
        {
            token_class =
                symbol <= result->symbols->predefined_limit ? (u8)(result->symbols->class_bits[symbol] | C_TOKEN_CLASS_COMPUTED) : C_TOKEN_CLASS_COMPUTED;
        }
        else
        {
            token_class = c_parse_token_class_compute(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]));
        }
        result->token_classes[token_index] = token_class;
    }
    return token_class;
}

// Append one ascending position, doubling the run from an empty start. The two
// populations this serves — `vector_size` and `_Alignas` spellings — are tens
// of tokens in a million, so the run is small and the copies are rare; sizing
// them exactly instead cost a second pass over the whole token stream.
BUSTER_C_INTERNAL void c_parse_position_index_append(Arena* arena, u32** positions, u32* count, u32* capacity, u32 position)
{
    if (*count == *capacity)
    {
        u32 grown = *capacity ? *capacity * 2 : 8;
        u32* moved = arena_allocate(arena, u32, grown);
        if (*count)
        {
            memcpy(moved, *positions, sizeof(*moved) * *count);
        }
        *positions = moved;
        *capacity = grown;
    }
    (*positions)[*count] = position;
    *count += 1;
}

// The two GNU attribute spellings as one membership test. The scans below
// look at every identifier token of a declaration range or of the whole
// translation unit, so the pair is a set of interned ids rather than two
// string_equal calls that each materialize the spelling through
// c_token_length's oversized guard.
#define C_PARSE_ATTRIBUTE_KEYWORDS (C_SYMBOL_WELL_KNOWN_BIT(ATTRIBUTE) | C_SYMBOL_WELL_KNOWN_BIT(ATTRIBUTE_SHORT))

// The three spellings of the assembler keyword, as one interned-id set. A
// declarator can be followed by `asm("name")`, which renames the object rather
// than declaring anything, so every declarator scan has to recognize the
// keyword to step over the group instead of reading it as a declarator name
// followed by a parameter list.
#define C_PARSE_ASM_KEYWORDS (C_SYMBOL_WELL_KNOWN_BIT(ASM) | C_SYMBOL_WELL_KNOWN_BIT(ASM_GNU) | C_SYMBOL_WELL_KNOWN_BIT(ASM_GNU_ALT))

// The position index and token census classify the same contiguous sidecar in
// the same tiles. Keeping the tile width here makes a later tile consumer
// share the census' 2,048-token working set instead of inventing another pass
// shape.
#define C_PARSE_TOKEN_TILE_TOKENS 2048

typedef struct CParseDelimiterStackEntry CParseDelimiterStackEntry;
struct CParseDelimiterStackEntry
{
    u32 position;
    CPunctuator opening;
};

BUSTER_C_INTERNAL BUSTER_INLINE void c_parse_position_index_visit(CParseResult* result, CPreprocessResult preprocess,
                                                                   CTokenPositionIndex* index, CTokenShape const* token_shapes,
                                                                   CParseDelimiterStackEntry* stack, u32* stack_count,
                                                                   u32* vector_size_capacity, u32* alignas_capacity,
                                                                   u32* label_candidate_capacity, u32* attribute_capacity,
                                                                   u32 token_index, CTokenShape shape)
{
    if (shape == C_TOKEN_IDENTIFIER)
    {
        CToken token = preprocess.tokens[token_index];
        u8 token_class = c_parse_token_class(result, preprocess, token_index);
        if (token_class & C_TOKEN_CLASS_VECTOR_SIZE)
        {
            c_parse_position_index_append(result->arena, &index->vector_size_positions, &index->vector_size_count, vector_size_capacity, token_index);
        }
        if (token_class & C_TOKEN_CLASS_ALIGNAS)
        {
            c_parse_position_index_append(result->arena, &index->alignas_positions, &index->alignas_count, alignas_capacity, token_index);
        }
        // The next token is one line of this cache at most, and reading
        // its punctuator here spares the body-lowering loops a re-scan of
        // every token for the same identifier-then-colon shape.
        if ((u64)token_index + 1 < preprocess.token_count &&
            c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, token_index + 1)) == C_PUNCTUATOR_COLON)
        {
            c_parse_position_index_append(result->arena, &index->label_candidate_positions, &index->label_candidate_count, label_candidate_capacity,
                                          token_index);
        }
        if (c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ATTRIBUTE_KEYWORDS))
        {
            c_parse_position_index_append(result->arena, &index->attribute_positions, &index->attribute_count, attribute_capacity, token_index);
        }
        return;
    }
    if (!c_token_shape_is_punctuator(shape))
    {
        return;
    }
    CPunctuator punctuator = c_token_shape_punctuator(shape);
    if (punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS || punctuator == C_PUNCTUATOR_LEFT_BRACKET || punctuator == C_PUNCTUATOR_LEFT_BRACE)
    {
        stack[(*stack_count)++] = (CParseDelimiterStackEntry){
            .position = token_index,
            .opening = punctuator,
        };
        return;
    }
    CPunctuator expected = punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS ? C_PUNCTUATOR_LEFT_PARENTHESIS
                           : punctuator == C_PUNCTUATOR_RIGHT_BRACKET   ? C_PUNCTUATOR_LEFT_BRACKET
                                                                         : punctuator == C_PUNCTUATOR_RIGHT_BRACE ? C_PUNCTUATOR_LEFT_BRACE : C_PUNCTUATOR_NONE;
    if (expected == C_PUNCTUATOR_NONE)
    {
        return;
    }
    if (!*stack_count || stack[*stack_count - 1].opening != expected)
    {
        index->delimiter_mismatch_count += 1;
        *stack_count = 0;
        return;
    }
    index->matching_delimiters[stack[--*stack_count].position] = token_index;
}

BUSTER_C_INTERNAL void c_parse_position_index_build(CParseResult* result, CPreprocessResult preprocess)
{
    CTokenPositionIndex* index = result->position_index;
    CTokenShape const* token_shapes = c_preprocess_token_shapes(&preprocess);
    index->matching_delimiters = arena_allocate(result->arena, u32, preprocess.token_count ? preprocess.token_count : 1);
    memset(index->matching_delimiters, 0xff, sizeof(*index->matching_delimiters) * preprocess.token_count);
    TemporalArena temporary = scratch_begin(&result->arena, 1);
    CParseDelimiterStackEntry* stack = arena_allocate(temporary.arena, CParseDelimiterStackEntry, preprocess.token_count ? preprocess.token_count : 1);
    u32 stack_count = 0;
    u32 vector_size_capacity = 0;
    u32 alignas_capacity = 0;
    u32 label_candidate_capacity = 0;
    u32 attribute_capacity = 0;
#if BUSTER_SIMD_512
    if (token_shapes)
    {
        Simd512 identifier_shape = simd512_splat((u8)C_TOKEN_IDENTIFIER);
        Simd512 open_parenthesis = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_PARENTHESIS));
        Simd512 open_bracket = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_BRACKET));
        Simd512 open_brace = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_BRACE));
        Simd512 close_parenthesis = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_PARENTHESIS));
        Simd512 close_bracket = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_BRACKET));
        Simd512 close_brace = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_BRACE));
        for (u64 tile_base = 0; tile_base < preprocess.token_count; tile_base += C_PARSE_TOKEN_TILE_TOKENS)
        {
            u64 tile_tokens = BUSTER_MIN((u64)C_PARSE_TOKEN_TILE_TOKENS, preprocess.token_count - tile_base);
            for (u32 window = 0; window < tile_tokens; window += 64)
            {
                u32 window_tokens = (u32)BUSTER_MIN(UINT64_C(64), tile_tokens - window);
                Mask64 window_mask = window_tokens == 64 ? UINT64_MAX : (Mask64)(((Mask64)1 << window_tokens) - 1);
                Simd512 shape_lanes = simd512_load_masked(token_shapes + tile_base + window, window_mask);
                Mask64 identifiers = simd512_equal_byte(shape_lanes, identifier_shape);
                Mask64 opens = mask64_or(mask64_or(simd512_equal_byte(shape_lanes, open_parenthesis), simd512_equal_byte(shape_lanes, open_bracket)),
                                         simd512_equal_byte(shape_lanes, open_brace));
                Mask64 closes = mask64_or(mask64_or(simd512_equal_byte(shape_lanes, close_parenthesis), simd512_equal_byte(shape_lanes, close_bracket)),
                                          simd512_equal_byte(shape_lanes, close_brace));
                for (Mask64 remaining = mask64_or(identifiers, mask64_or(opens, closes)); remaining; remaining &= remaining - 1)
                {
                    u32 lane = mask64_first_set(remaining);
                    u32 token_index = (u32)(tile_base + window + lane);
                    c_parse_position_index_visit(result, preprocess, index, token_shapes, stack, &stack_count, &vector_size_capacity, &alignas_capacity,
                                                 &label_candidate_capacity, &attribute_capacity, token_index, token_shapes[token_index]);
                }
            }
        }
    }
    else
#endif
    {
        for (u64 token_index = 0; token_index < preprocess.token_count; token_index += 1)
        {
            u32 index_value = (u32)token_index;
            c_parse_position_index_visit(result, preprocess, index, token_shapes, stack, &stack_count, &vector_size_capacity, &alignas_capacity,
                                         &label_candidate_capacity, &attribute_capacity, index_value,
                                         c_preprocess_token_shape_at(token_shapes, &preprocess, index_value));
        }
    }
    index->delimiter_mismatch_count += stack_count;
    scratch_end(temporary);
    index->built = true;
}

// Build the position index now if the parse never forced it, so a consumer
// outside the parser (body lowering) can read its populations directly.
BUSTER_C_SHARED void c_parse_position_index_ensure(CParseResult* result, CPreprocessResult preprocess)
{
    if (result->position_index && !result->position_index->built)
    {
        c_parse_position_index_build(result, preprocess);
    }
}

// Matching closer for the opening delimiter at open, or UINT32_MAX; see
// CTokenPositionIndex.matching_delimiters.
BUSTER_C_INTERNAL u32 c_parse_matching_delimiter_indexed(CParseResult* result, CPreprocessResult preprocess, u32 open)
{
    if (!result->position_index->built)
    {
        c_parse_position_index_build(result, preprocess);
    }
    return open < preprocess.token_count ? result->position_index->matching_delimiters[open] : UINT32_MAX;
}

// First recorded position in [start, end), or UINT32_MAX. Positions are
// stored ascending, so the lowest match is the same one the removed linear
// scans found first.
BUSTER_C_INTERNAL u32 c_parse_first_position_in_range(u32* positions, u32 count, u32 start, u32 end)
{
    u32 low = 0;
    u32 high = count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (positions[middle] < start)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    u32 result;
    if (low < count && positions[low] < end)
    {
        result = positions[low];
    }
    else
    {
        result = UINT32_MAX;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_parse_declaration_keyword_at(CParseResult* result, CPreprocessResult preprocess, u32 token_index)
{
    u8 token_class = c_parse_token_class(result, preprocess, token_index);
    if (token_class & C_TOKEN_CLASS_DECLARATION_KEYWORD)
    {
        return true;
    }
    if (c_preprocess_dialect_is_c23(preprocess.dialect) && (token_class & C_TOKEN_CLASS_C23_EXTRA_KEYWORD))
    {
        return true;
    }
    return (c_preprocess_dialect_is_gnu(preprocess.dialect) || c_preprocess_dialect_is_c23(preprocess.dialect)) &&
           (token_class & C_TOKEN_CLASS_TYPEOF);
}

BUSTER_C_SHARED void c_parse_diagnostic(CParseResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)
{
    BUSTER_CHECK(result->diagnostic_count < result->diagnostic_capacity);
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = location,
        .kind = kind,
    };
}

// Identifier uses are looked up by token index from parsing, semantic queries, and IR lowering.
// `identifier_use_by_token` keeps the first use recorded for each token so those lookups stay
// constant time instead of rescanning every use recorded so far.
BUSTER_C_SHARED u32 c_parse_identifier_use_index(CParseResult* result, u32 token_index)
{
    if (token_index >= result->identifier_use_by_token_capacity)
    {
        return C_ID_UNDERLYING_INVALID;
    }
    return result->identifier_use_by_token[token_index];
}

BUSTER_C_INTERNAL CTypeId c_parse_scalar_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end,
                                                u32* declarator_start);

BUSTER_C_INTERNAL CTypeId c_parse_scalar_type_in_scope(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                         u32 start, u32 end,
                                                         u32* declarator_start);

BUSTER_C_SHARED CEntityId c_parse_lookup_typedef_name(CParseResult* result, String8 name, bool oldest);
BUSTER_C_SHARED CEntity* c_parse_first_constant_entity(CParseResult* result, String8 name);

BUSTER_C_SHARED CTypeId c_parse_pointer_chain(CParseResult* result, CPreprocessResult preprocess, CTypeId base, u32* index, u32 end);

BUSTER_C_SHARED CTypeId c_parse_array_suffixes(CParseResult* result, CPreprocessResult preprocess, CTypeId element_type, u32* index, u32 end);

// Persistent c_parse_type_layout results, indexed by type id. An entry may
// exist only for a layout that can no longer change: builtin scalar kinds,
// and enum/vector/array/aggregate layouts whose whole dependency closure
// resolved without a provisional input (a guessed 4/4 incomplete-enum layout
// or an initializer-inferred array bound stays per-query, because completion
// rewrites those answers in place). Every in-place edit of an existing type
// record passes through c_type_parse_record_mutation, which drops that id's
// entry, so speculative completions and their rollbacks are never served;
// entries are written only while the machine is idle (no frames, no
// undoable mutations) and only for the parse's own token stream, never for
// the synthetic streams c_parse_integer_constant_range builds.
BUSTER_C_SHARED bool c_type_parse_buffer_size_add(u64* size, u64 count, u64 element_size, u64 alignment)
{
    if (*size > UINT64_MAX - (alignment - 1) || (count && element_size > UINT64_MAX / count))
    {
        return false;
    }
    u64 aligned_size = (*size + alignment - 1) & ~(alignment - 1);
    u64 byte_count = count * element_size;
    if (aligned_size > UINT64_MAX - byte_count)
    {
        return false;
    }
    *size = aligned_size + byte_count;
    return true;
}

BUSTER_C_INTERNAL bool c_type_parse_frame_push(CTypeParseMachine* machine, CTypeParseFrame frame)
{
    if (machine->frame_count >= machine->frame_capacity)
    {
        machine->failed = true;
        return false;
    }
    machine->frames[machine->frame_count++] = frame;
    return true;
}

BUSTER_C_INTERNAL void c_type_parse_frame_complete(CTypeParseMachine* machine, CTypeId type, u32 index, bool valid)
{
    BUSTER_CHECK(machine->frame_count);
    CTypeParseFrame* frame = machine->frames + machine->frame_count - 1;
    if (frame->kind == C_TYPE_PARSE_FRAME_SIZEOF)
    {
        machine->expression_task_count = frame->task_mark;
        arena_set_position(machine->scratch_arena, frame->arena_mark);
    }
    machine->frame_count -= 1;
    machine->result_type = type;
    machine->result_index = index;
    machine->result_valid = valid;
}

BUSTER_C_INTERNAL bool c_type_parse_record_mutation(CTypeParseMachine* machine, CParseResult* result, CTypeId id)
{
    if (id.value >= result->type_count)
    {
        machine->failed = true;
        return false;
    }
    if (id.value < machine->mutation_type_limit)
    {
        if (machine->mutation_count >= machine->mutation_capacity)
        {
            machine->failed = true;
            return false;
        }
        machine->mutations[machine->mutation_count++] = (CTypeMutation){
            .id = id,
            .previous = result->types[id.value],
        };
        if (id.value < machine->layout_cache.capacity)
        {
            machine->layout_cache.states[id.value] = 0;
        }
    }

    return true;
}

BUSTER_C_SHARED void c_type_parse_rollback(CTypeParseMachine* machine, CParseResult* result, CParseResult checkpoint, u32 mutation_mark)
{
    CType* checkpoint_types = checkpoint.types;
    *result = checkpoint;
    while (machine->mutation_count > mutation_mark)
    {
        CTypeMutation mutation = machine->mutations[--machine->mutation_count];
        if (checkpoint_types && mutation.id.value < checkpoint.type_count)
        {
            checkpoint_types[mutation.id.value] = mutation.previous;
        }
    }
}

BUSTER_C_INTERNAL void c_type_parse_machine_run(CTypeParseMachine* machine, u32 frame_start);

BUSTER_C_INTERNAL bool c_type_parse_root_finish(CTypeParseMachine* machine, CParseResult* result, CParseResult checkpoint, u32 mutation_mark,
                                                  CSourceLocation location)
{
    bool valid = machine->result_valid && !machine->failed;
    if (!valid)
    {
        u32 diagnostic_count = result->diagnostic_count;
        c_type_parse_rollback(machine, result, checkpoint, mutation_mark);
        result->diagnostic_count = diagnostic_count;
    }
    else
    {
        machine->mutation_count = mutation_mark;
    }
    if (machine->failed)
    {
        machine->failed = false;
        c_parse_diagnostic(result, location, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("C type parsing exceeded its explicit-machine capacity"));
        valid = false;
    }
    return valid;
}

BUSTER_C_INTERNAL bool c_parse_alignment_word(String8 spelling)
{
    return string_equal(spelling, S8("_Alignas")) || string_equal(spelling, S8("aligned")) || string_equal(spelling, S8("__aligned")) ||
           string_equal(spelling, S8("__aligned__"));
}

BUSTER_C_SHARED bool c_parse_alignof_word(String8 spelling)
{
    return string_equal(spelling, S8("_Alignof")) || string_equal(spelling, S8("__alignof")) || string_equal(spelling, S8("__alignof__"));
}

BUSTER_C_INTERNAL bool c_parse_alignment_specifiers(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end,
                                                      u32* alignment_start, u32* alignment_count);
BUSTER_C_INTERNAL bool c_parse_alignment_specifiers(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end,
                                                      u32* alignment_start, u32* alignment_count)
{
    u32 frame_start = machine->frame_count;
    CParseResult checkpoint = *result;
    u32 mutation_mark = machine->mutation_count;
    machine->mutation_type_limit = checkpoint.type_count;
    machine->result_valid = false;
    bool pushed = c_type_parse_frame_push(machine, (CTypeParseFrame){
                                              .result = result,
                                              .preprocess = preprocess,
                                              .start = start,
                                              .end = end,
                                              .kind = C_TYPE_PARSE_FRAME_ALIGNMENT,
                                          });
    if (pushed)
    {
        c_type_parse_machine_run(machine, frame_start);
    }
    bool valid = c_type_parse_root_finish(machine, result, checkpoint, mutation_mark,
                                          start < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[start]) : (CSourceLocation){0});
    *alignment_start = machine->result_index;
    *alignment_count = machine->result_type.value;
    return valid;
}


BUSTER_C_SHARED CTypeId c_parse_pointer_chain(CParseResult* result, CPreprocessResult preprocess, CTypeId base, u32* index, u32 end);
BUSTER_C_SHARED CTypeId c_parse_array_suffixes(CParseResult* result, CPreprocessResult preprocess, CTypeId element_type, u32* index, u32 end);
BUSTER_C_INTERNAL CTypeId c_parse_parenthesized_declaration_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                                   CTypeId base, u32 declarator_start, u32 name_index, u32 suffix_end, bool has_name);
BUSTER_C_INTERNAL bool c_parse_parenthesized_declarator_name(CPreprocessResult preprocess, u32 declarator_start, u32 end, u32* name_index);

BUSTER_C_INTERNAL bool c_parse_type_word(String8 spelling);

BUSTER_C_SHARED bool c_parse_type_word_for_dialect(String8 spelling, CPreprocessDialect dialect);

BUSTER_C_INTERNAL bool c_parse_type_is_incomplete_array(CParseResult* result, CTypeId type)
{
    if (!result || type.value >= result->type_count)
    {
        return false;
    }
    CType* array = result->types + type.value;
    if (array->kind != C_TYPE_ARRAY || array->array_bound >= result->array_bound_count)
    {
        return false;
    }
    CArrayBound bound = result->array_bounds[array->array_bound];
    return !bound.token_count && !bound.is_star && !bound.has_inferred_count;
}

BUSTER_C_INTERNAL bool c_parse_type_is_flexible_array_member(CParseResult* result, CType* aggregate, u32 member_index)
{
    if (!result || !aggregate || aggregate->kind != C_TYPE_STRUCT || member_index + 1 != aggregate->member_count)
    {
        return false;
    }
    u32 named_member_count = 0;
    for (u32 index = 0; index < aggregate->member_count; index += 1)
    {
        named_member_count += result->members[aggregate->member_start + index].name.length != 0;
    }
    CMember* member = result->members + aggregate->member_start + member_index;
    if (!member->name.length || named_member_count < 2 || member->is_bit_field || !c_parse_type_is_incomplete_array(result, member->type))
    {
        return false;
    }
    CType* array = result->types + member->type.value;
    return array->element_type.value < result->type_count;
}

BUSTER_C_INTERNAL void c_parse_validate_flexible_array_members(CParseResult* result, CType* aggregate)
{
    if (!result || !aggregate)
    {
        return;
    }
    u32 named_member_count = 0;
    for (u32 member_index = 0; member_index < aggregate->member_count; member_index += 1)
    {
        named_member_count += result->members[aggregate->member_start + member_index].name.length != 0;
    }
    for (u32 member_index = 0; member_index < aggregate->member_count; member_index += 1)
    {
        CMember* member = result->members + aggregate->member_start + member_index;
        if (!c_parse_type_is_incomplete_array(result, member->type))
        {
            continue;
        }
        String8 message = {0};
        if (aggregate->kind != C_TYPE_STRUCT)
        {
            message = S8("flexible array member is only allowed in a structure");
        }
        else if (!member->name.length)
        {
            message = S8("flexible array member must have a name");
        }
        else if (member_index + 1 != aggregate->member_count)
        {
            message = S8("flexible array member must be the last structure member");
        }
        else if (named_member_count < 2)
        {
            message = S8("structure with a flexible array member must have another named member");
        }
        if (message.length)
        {
            c_parse_diagnostic(result, member->location, C_DIAGNOSTIC_INVALID_FLEXIBLE_ARRAY_MEMBER, message);
        }
    }
}

BUSTER_C_SHARED bool c_parse_builtin_type_layout(Target target, CTypeKind kind, u64* size_out, u32* alignment_out)
{
    TargetDataLayout layout = target_data_layout(target);
    u64 size = 0;
    u32 alignment = 0;
    switch (kind)
    {
    case C_TYPE_BOOL:
        size = layout.boolean.size;
        alignment = layout.boolean.alignment;
        break;
    case C_TYPE_CHAR:
        size = layout.plain_char.size;
        alignment = layout.plain_char.alignment;
        break;
    case C_TYPE_SIGNED_CHAR:
        size = layout.signed_char.size;
        alignment = layout.signed_char.alignment;
        break;
    case C_TYPE_UNSIGNED_CHAR:
        size = layout.unsigned_char.size;
        alignment = layout.unsigned_char.alignment;
        break;
    case C_TYPE_SHORT:
        size = layout.short_integer.size;
        alignment = layout.short_integer.alignment;
        break;
    case C_TYPE_UNSIGNED_SHORT:
        size = layout.unsigned_short_integer.size;
        alignment = layout.unsigned_short_integer.alignment;
        break;
    case C_TYPE_INT:
        size = layout.integer.size;
        alignment = layout.integer.alignment;
        break;
    case C_TYPE_UNSIGNED_INT:
        size = layout.unsigned_integer.size;
        alignment = layout.unsigned_integer.alignment;
        break;
    case C_TYPE_FLOAT:
        size = kind == C_TYPE_FLOAT ? layout.float_type.size : layout.unsigned_integer.size;
        alignment = kind == C_TYPE_FLOAT ? layout.float_type.alignment : layout.unsigned_integer.alignment;
        break;
    case C_TYPE_LONG:
        size = layout.long_integer.size;
        alignment = layout.long_integer.alignment;
        break;
    case C_TYPE_UNSIGNED_LONG:
        size = layout.unsigned_long_integer.size;
        alignment = layout.unsigned_long_integer.alignment;
        break;
    case C_TYPE_LONG_LONG:
        size = layout.long_long_integer.size;
        alignment = layout.long_long_integer.alignment;
        break;
    case C_TYPE_UNSIGNED_LONG_LONG:
        size = layout.unsigned_long_long_integer.size;
        alignment = layout.unsigned_long_long_integer.alignment;
        break;
    case C_TYPE_DOUBLE:
        size = layout.double_type.size;
        alignment = layout.double_type.alignment;
        break;
    case C_TYPE_NULLPTR:
    case C_TYPE_POINTER:
    case C_TYPE_FUNCTION:
        size = layout.pointer.size;
        alignment = layout.pointer.alignment;
        break;
    case C_TYPE_LONG_DOUBLE:
        size = layout.long_double_type.size;
        alignment = layout.long_double_type.alignment;
        break;
    // A complex value is two contiguous elements of its real type, so it is
    // twice as wide and no more strictly aligned. Clang agrees on all three
    // for every target here: sizeof/_Alignof are {8,4}, {16,8} and
    // {2*sizeof(long double), _Alignof(long double)}.
    case C_TYPE_FLOAT_COMPLEX:
        size = layout.float_type.size * 2;
        alignment = layout.float_type.alignment;
        break;
    case C_TYPE_DOUBLE_COMPLEX:
        size = layout.double_type.size * 2;
        alignment = layout.double_type.alignment;
        break;
    case C_TYPE_LONG_DOUBLE_COMPLEX:
        size = layout.long_double_type.size * 2;
        alignment = layout.long_double_type.alignment;
        break;
    case C_TYPE_INT128:
        size = layout.integer128.size;
        alignment = layout.integer128.alignment;
        break;
    case C_TYPE_UNSIGNED_INT128:
        size = layout.unsigned_integer128.size;
        alignment = layout.unsigned_integer128.alignment;
        break;
    case C_TYPE_VA_LIST:
        size = layout.va_list.size;
        alignment = layout.va_list.alignment;
        break;
    case C_TYPE_VOID:
    case C_TYPE_ARRAY:
    case C_TYPE_VECTOR:
    case C_TYPE_STRUCT:
    case C_TYPE_UNION:
    case C_TYPE_ENUM:
    case C_TYPE_INVALID:
    case C_TYPE_COUNT:
        return false;
    }
    *size_out = size;
    *alignment_out = alignment;
    return true;
}

BUSTER_C_INTERNAL CTypeId c_parse_machineless_base_type(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 start, u32 end,
                                                          u32* index_out);

BUSTER_C_INTERNAL bool c_parse_machineless_sizeof_operand_layout(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                                   u32 start, u32 end, u64* size_out, u32* alignment_out);

typedef struct CParseLayoutContext CParseLayoutContext;
struct CParseLayoutContext
{
    CTypeParseMachine* machine;
    Arena* arena;
    CPreprocessResult preprocess;
    CParseResult* result;
    u32* alignments;
    bool* resolved;
    bool* provisional;
    u32 type_count;
};

// Raises `*alignment` to each alignment specifier of [start, start + count),
// which is what c_ir_alignment_evaluate does for the IR layout; the two run
// over the same records and must agree. Answers false when a specifier names
// a type whose own layout is still unresolved, which sends the aggregate back
// to a later pass, or when the request is not a power of two at least as
// large as the natural alignment.
BUSTER_C_INTERNAL bool c_parse_layout_alignment_specifiers(CParseLayoutContext* context, u32 start, u32 count, u32* alignment, bool* provisional_out)
{
    bool valid = true;
    for (u32 specifier_index = 0; specifier_index < count; specifier_index += 1)
    {
        if (start > context->result->alignment_count || specifier_index >= context->result->alignment_count - start)
        {
            valid = false;
            break;
        }
        CAlignmentSpecifier specifier = context->result->alignments[start + specifier_index];
        u64 requested_alignment = 0;
        if (specifier.type.value < context->type_count)
        {
            if (!context->resolved[specifier.type.value])
            {
                valid = false;
                break;
            }
            *provisional_out |= context->provisional[specifier.type.value];
            requested_alignment = context->alignments[specifier.type.value];
        }
        else
        {
            u32 specifier_end = specifier.token_start + specifier.token_count;
            bool alignof_type = specifier.token_count >= 4 && context->preprocess.tokens[specifier.token_start].kind == C_TOKEN_IDENTIFIER &&
                                c_parse_alignof_word(c_token_spelling(context->preprocess.spelling_base, context->preprocess.tokens[specifier.token_start])) &&
                                c_token_is_punctuator(&context->preprocess.tokens[specifier.token_start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                                c_token_is_punctuator(&context->preprocess.tokens[specifier_end - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS);
            if (alignof_type)
            {
                u32 type_start = specifier.token_start + 2;
                u32 type_end = specifier_end - 1;
                u32 aligned_type_index = type_start;
                CTypeId aligned_type = c_parse_scalar_type(context->machine, context->result, context->preprocess, type_start, type_end, &aligned_type_index);
                if (aligned_type.value != C_ID_UNDERLYING_INVALID)
                {
                    aligned_type = c_parse_pointer_chain(context->result, context->preprocess, aligned_type, &aligned_type_index, type_end);
                    aligned_type = c_parse_array_suffixes(context->result, context->preprocess, aligned_type, &aligned_type_index, type_end);
                }
                if (aligned_type.value >= context->type_count || aligned_type_index != type_end || !context->resolved[aligned_type.value])
                {
                    valid = false;
                    break;
                }
                *provisional_out |= context->provisional[aligned_type.value];
                requested_alignment = context->alignments[aligned_type.value];
            }
            else
            {
                CPreprocessResult evaluation = {
                    .diagnostics = arena_allocate(context->arena, CDiagnostic, specifier.token_count + 1),
                    .target = context->preprocess.target,
                    .dialect = context->preprocess.dialect,
                };
                if (!c_integer_expression_evaluate(context->arena, context->preprocess.spelling_base, context->preprocess.tokens + specifier.token_start, specifier.token_count, 65536, &evaluation,
                                                   &requested_alignment) ||
                    evaluation.diagnostic_count)
                {
                    valid = false;
                    break;
                }
            }
        }
        if (requested_alignment > UINT32_MAX || (requested_alignment && (requested_alignment & (requested_alignment - 1))) ||
            requested_alignment < *alignment)
        {
            valid = false;
            break;
        }
        *alignment = BUSTER_MAX(*alignment, (u32)requested_alignment);
    }
    return valid;
}

BUSTER_C_INTERNAL bool c_parse_type_layout(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                             CTypeId requested, u64* size_out, u32* alignment_out)
{
    if (requested.value >= result->type_count)
    {
        return false;
    }
    // The seed pass below resolves builtin scalars and incomplete enums from
    // the requested record alone and takes the early exit, so answer those
    // without walking the table.
    {
        CType requested_type = result->types[requested.value];
        u64 direct_size = 0;
        u32 direct_alignment = 0;
        if (c_parse_builtin_type_layout(preprocess.target, requested_type.kind, &direct_size, &direct_alignment))
        {
            if (direct_alignment)
            {
                *size_out = direct_size;
                *alignment_out = direct_alignment;
                return true;
            }
        }
        else if (requested_type.kind == C_TYPE_ENUM && requested_type.element_type.value == C_ID_UNDERLYING_INVALID)
        {
            *size_out = 4;
            *alignment_out = 4;
            return true;
        }
    }
    CTypeLayoutCache* cache = machine && preprocess.tokens && preprocess.tokens == machine->layout_cache.tokens ? &machine->layout_cache : 0;
    if (cache && requested.value < cache->capacity && cache->states[requested.value])
    {
        *size_out = cache->sizes[requested.value];
        *alignment_out = cache->alignments[requested.value];
        return true;
    }
    // The type table can grow while the solve parses alignof/sizeof operand
    // types; the scratch arrays cover the types that existed at entry and
    // later additions stay unresolved for this query.
    u32 type_count = result->type_count;
    u64* sizes = arena_allocate(arena, u64, type_count);
    u32* alignments = arena_allocate(arena, u32, type_count);
    bool* resolved = arena_allocate(arena, bool, type_count);
    bool* provisional = arena_allocate(arena, bool, type_count);
    memset(resolved, 0, sizeof(*resolved) * type_count);
    memset(provisional, 0, sizeof(*provisional) * type_count);
    CParseLayoutContext layout_context = {
        .machine = machine,
        .arena = arena,
        .preprocess = preprocess,
        .result = result,
        .alignments = alignments,
        .resolved = resolved,
        .provisional = provisional,
        .type_count = type_count,
    };
    for (u32 type_index = 0; type_index < type_count; type_index += 1)
    {
        if (cache && type_index < cache->capacity && cache->states[type_index])
        {
            sizes[type_index] = cache->sizes[type_index];
            alignments[type_index] = cache->alignments[type_index];
            resolved[type_index] = true;
            continue;
        }
        CTypeKind kind = result->types[type_index].kind;
        u64 size = 0;
        u32 alignment = 0;
        if (!c_parse_builtin_type_layout(preprocess.target, kind, &size, &alignment) && kind == C_TYPE_ENUM &&
            result->types[type_index].element_type.value == C_ID_UNDERLYING_INVALID)
        {
            size = 4;
            alignment = 4;
            provisional[type_index] = true;
        }
        if (alignment)
        {
            sizes[type_index] = size;
            alignments[type_index] = alignment;
            resolved[type_index] = true;
        }
    }
    if (resolved[requested.value])
    {
        goto requested_resolved;
    }
    for (u32 pass = 0; pass < type_count; pass += 1)
    {
        bool progress = false;
        for (u32 type_index = 0; type_index < type_count; type_index += 1)
        {
            if (resolved[type_index])
            {
                continue;
            }
            CType type = result->types[type_index];
            if (type.kind == C_TYPE_ENUM && type.element_type.value < type_count && resolved[type.element_type.value])
            {
                sizes[type_index] = sizes[type.element_type.value];
                alignments[type_index] = alignments[type.element_type.value];
                provisional[type_index] = provisional[type.element_type.value];
                resolved[type_index] = true;
                if (type_index == requested.value)
                {
                    goto requested_resolved;
                }
                progress = true;
                continue;
            }
            if (type.kind == C_TYPE_VECTOR)
            {
                if (type.element_type.value >= type_count || !resolved[type.element_type.value] || !type.vector_byte_size ||
                    !sizes[type.element_type.value] || type.vector_byte_size % sizes[type.element_type.value])
                {
                    continue;
                }
                u64 element_count = type.vector_byte_size / sizes[type.element_type.value];
                if (!element_count || (element_count & (element_count - 1)))
                {
                    continue;
                }
                sizes[type_index] = type.vector_byte_size;
                alignments[type_index] = type.vector_byte_size;
                provisional[type_index] = provisional[type.element_type.value];
                resolved[type_index] = true;
                if (type_index == requested.value)
                {
                    goto requested_resolved;
                }
                progress = true;
                continue;
            }
            if (type.kind == C_TYPE_ARRAY)
            {
                if (type.element_type.value >= type_count || !resolved[type.element_type.value] || type.array_bound >= result->array_bound_count)
                {
                    continue;
                }
                bool array_provisional = provisional[type.element_type.value];
                CArrayBound bound = result->array_bounds[type.array_bound];
                u64 count = 0;
                bool unresolved_identifier = false;
                CToken* bound_tokens = arena_allocate(arena, CToken, bound.token_count * 2 + 1);
                u32 bound_token_count = 0;
                u64 bound_spelling_capacity = 0;
                for (u32 bound_index = 0; bound_index < bound.token_count; bound_index += 1)
                {
                    bound_spelling_capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[bound.token_start + bound_index]) + 21;
                }
                CSpellingSpace bound_space = c_space_local(arena, bound_spelling_capacity);
                for (u32 bound_index = 0; bound_index < bound.token_count; bound_index += 1)
                {
                    CToken token = preprocess.tokens[bound.token_start + bound_index];
                    bool bound_word_is_alignof = token.kind == C_TOKEN_IDENTIFIER && c_parse_alignof_word(c_token_spelling(preprocess.spelling_base, token));
                    if (token.kind == C_TOKEN_IDENTIFIER && (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("sizeof")) || bound_word_is_alignof) &&
                        bound_index + 2 < bound.token_count &&
                        c_token_is_punctuator(&preprocess.tokens[bound.token_start + bound_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        u32 operand_start = bound.token_start + bound_index + 2;
                        u32 bound_end = bound.token_start + bound.token_count;
                        u32 close = operand_start;
                        u32 depth = 1;
                        while (close < bound_end && depth)
                        {
                            CToken operand_token = preprocess.tokens[close];
                            if (c_token_is_punctuator(&operand_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                            {
                                depth += 1;
                            }
                            else if (c_token_is_punctuator(&operand_token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                            {
                                depth -= 1;
                                if (!depth)
                                {
                                    break;
                                }
                            }
                            close += 1;
                        }
                        u32 operand_type_index = operand_start;
                        CParseResult operand_parse = *result;
                        // Callers inside the explicit type-parse machine pass no
                        // machine, because its frame stack cannot be reentered;
                        // their operands resolve through the machineless base
                        // type instead — typedef, named tag, or primitive, with
                        // no declarator suffixes — so an enum constant like
                        // `sizeof(char[sizeof(T)])` folds rather than failing
                        // its whole enum.
                        CTypeId operand_type;
                        if (!depth && close > operand_start)
                        {
                            operand_type = machine ? c_parse_scalar_type(machine, &operand_parse, preprocess, operand_start, close, &operand_type_index)
                                                   : c_parse_machineless_base_type(&operand_parse, preprocess,
                                                                                    result->scope_count ? (CScopeId){.value = 0} : C_SCOPE_ID_INVALID,
                                                                                    operand_start, close, &operand_type_index);
                        }
                        else
                        {
                            operand_type = C_TYPE_ID_INVALID;
                        }
                        bool pointer_type = false;
                        while (operand_type.value != C_ID_UNDERLYING_INVALID && operand_type_index < close)
                        {
                            CToken type_token = preprocess.tokens[operand_type_index];
                            if (c_token_is_punctuator(&type_token, C_PUNCTUATOR_STAR))
                            {
                                pointer_type = true;
                                operand_type_index += 1;
                                continue;
                            }
                            if (type_token.kind == C_TOKEN_IDENTIFIER &&
                                (string_equal(c_token_spelling(preprocess.spelling_base, type_token), S8("const")) || string_equal(c_token_spelling(preprocess.spelling_base, type_token), S8("volatile")) ||
                                 string_equal(c_token_spelling(preprocess.spelling_base, type_token), S8("restrict"))))
                            {
                                operand_type_index += 1;
                                continue;
                            }
                            operand_type = C_TYPE_ID_INVALID;
                            break;
                        }
                        u64 operand_size = 0;
                        u32 operand_alignment = 0;
                        bool operand_resolved = operand_type.value != C_ID_UNDERLYING_INVALID && operand_type_index == close;
                        if (operand_resolved && pointer_type)
                        {
                            operand_size = c_preprocess_detail(preprocess)->data_layout.pointer.size;
                            operand_alignment = c_preprocess_detail(preprocess)->data_layout.pointer.alignment;
                        }
                        else if (operand_resolved && operand_type.value < type_count && resolved[operand_type.value])
                        {
                            operand_size = sizes[operand_type.value];
                            operand_alignment = alignments[operand_type.value];
                            array_provisional |= provisional[operand_type.value];
                        }
                        else if (operand_resolved && operand_type.value < operand_parse.type_count)
                        {
                            // Kind-matching answers only for kinds whose layout
                            // the kind alone determines. A struct, union,
                            // array, or vector operand that has not resolved
                            // yet must stay unresolved for this pass — matching
                            // any same-kind type folds a neighbouring type's
                            // size into the bound — and the fixpoint retries it
                            // once the real layout lands.
                            CTypeKind operand_kind = operand_parse.types[operand_type.value].kind;
                            operand_resolved = false;
                            bool kind_determines_layout = operand_kind != C_TYPE_STRUCT && operand_kind != C_TYPE_UNION &&
                                                          operand_kind != C_TYPE_ARRAY && operand_kind != C_TYPE_VECTOR;
                            for (u32 candidate_index = 0; kind_determines_layout && candidate_index < type_count; candidate_index += 1)
                            {
                                if (result->types[candidate_index].kind == operand_kind && resolved[candidate_index])
                                {
                                    operand_size = sizes[candidate_index];
                                    operand_alignment = alignments[candidate_index];
                                    array_provisional |= provisional[candidate_index];
                                    operand_resolved = true;
                                    break;
                                }
                            }
                            if (!operand_resolved && kind_determines_layout)
                            {
                                operand_resolved = c_parse_builtin_type_layout(preprocess.target, operand_kind, &operand_size, &operand_alignment);
                            }
                        }
                        else
                        {
                            operand_resolved = false;
                        }
                        if (!operand_resolved)
                        {
                            unresolved_identifier = true;
                            break;
                        }
                        bound_tokens[bound_token_count++] = c_space_token(
                            &bound_space, string_format(arena, S8("{u64}"), bound_word_is_alignof ? operand_alignment : operand_size),
                            C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
                        bound_index += close - (bound.token_start + bound_index);
                        continue;
                    }
                    if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        u32 absolute = bound.token_start + bound_index;
                        u32 bound_end = bound.token_start + bound.token_count;
                        u32 close = bound_end;
                        u32 cast_depth = 0;
                        for (u32 scan = absolute; scan < bound_end; scan += 1)
                        {
                            if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
                            {
                                cast_depth += 1;
                            }
                            else if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                            {
                                if (!cast_depth)
                                {
                                    break;
                                }
                                cast_depth -= 1;
                                if (!cast_depth)
                                {
                                    close = scan;
                                    break;
                                }
                            }
                        }
                        bool cast_type = close > absolute + 1 && close < bound_end;
                        for (u32 cast_type_index = absolute + 1; cast_type && cast_type_index < close; cast_type_index += 1)
                        {
                            CToken type_token = preprocess.tokens[cast_type_index];
                            bool type_word = type_token.kind == C_TOKEN_IDENTIFIER && c_parse_type_word_for_dialect_token(preprocess, type_token);
                            bool typedef_name =
                                !type_word && c_parse_lookup_typedef_name(result, c_token_spelling(preprocess.spelling_base, type_token), false).value != C_ID_UNDERLYING_INVALID;
                            cast_type = type_word || typedef_name;
                        }
                        if (cast_type)
                        {
                            bound_index += close - absolute;
                            continue;
                        }
                    }
                    if (token.kind == C_TOKEN_IDENTIFIER)
                    {
                        CEntity* constant = c_parse_first_constant_entity(result, c_token_spelling(preprocess.spelling_base, token));
                        bool constant_is_negative = false;
                        u64 constant_value = 0;
                        bool folded_constant = constant != 0;
                        if (constant)
                        {
                            constant_is_negative = constant->constant_is_negative;
                            constant_value = constant->constant_value;
                        }
                        else
                        {
                            // A member of an enum still being declared is not an
                            // entity yet; the member table already holds every
                            // enumerator declared before this bound, so
                            // `enum { N = 5, E = sizeof(int[N]) }` folds.
                            String8 name = c_token_spelling(preprocess.spelling_base, token);
                            for (u32 member_index = 0; member_index < result->enum_member_count && !folded_constant; member_index += 1)
                            {
                                CEnumMember* member = &result->enum_members[member_index];
                                if (string_equal(member->name, name))
                                {
                                    constant_is_negative = member->is_negative;
                                    constant_value = member->value;
                                    folded_constant = true;
                                }
                            }
                        }
                        if (!folded_constant)
                        {
                            unresolved_identifier = true;
                            break;
                        }
                        if (constant_is_negative)
                        {
                            bound_tokens[bound_token_count++] = c_space_token(&bound_space, S8("-"), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_MINUS);
                        }
                        bound_tokens[bound_token_count++] = c_space_token(&bound_space, string_format(arena, S8("{u64}"), constant_value),
                                                                          C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
                        continue;
                    }
                    bound_tokens[bound_token_count++] = c_space_retoken(&bound_space, preprocess.spelling_base, token);
                }
                CPreprocessResult evaluation = {
                    .diagnostics = arena_allocate(arena, CDiagnostic, bound.token_count + 1),
                    .target = preprocess.target,
                    .dialect = preprocess.dialect,
                };
                if (bound.has_inferred_count)
                {
                    // Inference can rewrite the bound record in place, so an
                    // inferred layout never outlives this query.
                    count = bound.inferred_count;
                    array_provisional = true;
                }
                else if (bound.is_star || unresolved_identifier || !bound.token_count ||
                         !c_integer_expression_evaluate(arena, bound_space.base, bound_tokens, bound_token_count, 65536, &evaluation, &count) ||
                         evaluation.diagnostic_count ||
                         (count && sizes[type.element_type.value] > UINT64_MAX / count))
                {
                    continue;
                }
                sizes[type_index] = sizes[type.element_type.value] * count;
                alignments[type_index] = alignments[type.element_type.value];
                provisional[type_index] = array_provisional;
                resolved[type_index] = true;
                if (type_index == requested.value)
                {
                    goto requested_resolved;
                }
                progress = true;
                continue;
            }
            if ((type.kind != C_TYPE_STRUCT && type.kind != C_TYPE_UNION) || !type.is_complete)
            {
                continue;
            }
            // Bits, not bytes: this mirrors the System V bit-field placement
            // the IR layout in c_gen performs, and the two must agree or a
            // sizeof folded during the parse contradicts the object it sizes.
            u64 bit_position = 0;
            u32 alignment = 1;
            u32 pack_alignment = type.definition_start < preprocess.token_count ? c_preprocess_pack_alignment(&preprocess, type.definition_start) : 0;
            CAggregateAttributes aggregate_attributes = c_parse_aggregate_attributes(result, (CTypeId){.value = type_index});
            if (aggregate_attributes.is_packed)
            {
                pack_alignment = 1;
            }
            bool fields_resolved = true;
            bool aggregate_provisional = false;
            for (u32 member_index = 0; member_index < type.member_count; member_index += 1)
            {
                CMember member = result->members[type.member_start + member_index];
                if (member.type.value >= type_count)
                {
                    fields_resolved = false;
                    break;
                }
                bool flexible = c_parse_type_is_flexible_array_member(result, &type, member_index);
                CType* member_type = result->types + member.type.value;
                CTypeId layout_type = flexible ? member_type->element_type : member.type;
                if (layout_type.value >= type_count || !resolved[layout_type.value])
                {
                    fields_resolved = false;
                    break;
                }
                aggregate_provisional |= provisional[layout_type.value];
                u64 member_size = flexible ? 0 : sizes[layout_type.value];
                u32 natural_alignment = alignments[layout_type.value];
                u32 member_alignment = natural_alignment;
                // A byte ceiling is what makes a bit-field take the next bit
                // rather than the next storage unit, so the predicate is
                // "packed to one byte", not "ended up byte-aligned"; the IR
                // layout in c_gen splits the same way.
                bool packed_member = member.is_packed || pack_alignment == 1;
                if (packed_member)
                {
                    member_alignment = 1;
                }
                else if (pack_alignment)
                {
                    member_alignment = BUSTER_MIN(member_alignment, pack_alignment);
                }
                bool alignment_resolved = c_parse_layout_alignment_specifiers(&layout_context, member.alignment_start, member.alignment_count,
                                                                              &member_alignment, &aggregate_provisional);
                if (!alignment_resolved)
                {
                    fields_resolved = false;
                    break;
                }
                if (!member_alignment)
                {
                    fields_resolved = false;
                    break;
                }
                if (member.is_bit_field)
                {
                    u64 unit_bits = member_size * 8;
                    u64 alignment_bits = (u64)member_alignment * 8;
                    if (member.bit_width > unit_bits || !unit_bits)
                    {
                        fields_resolved = false;
                        break;
                    }
                    // An unnamed bit-field's declared type does not raise the
                    // aggregate's alignment; a named one's does.
                    if (member.name.length)
                    {
                        alignment = BUSTER_MAX(alignment, member_alignment);
                    }
                    if (type.kind == C_TYPE_UNION)
                    {
                        if (member.bit_width)
                        {
                            bit_position = BUSTER_MAX(bit_position, member_size * 8);
                        }
                        continue;
                    }
                    if (!member.bit_width)
                    {
                        // Packing does not move a zero-width bit-field: GCC
                        // and Clang keep aligning it to its declared type, so
                        // `struct __attribute__((packed)) { int a : 3; int : 0;
                        // int b : 3; }` still measures five bytes.
                        u64 zero_width_bits = (u64)natural_alignment * 8;
                        u64 zero_width_remainder = zero_width_bits ? bit_position % zero_width_bits : 0;
                        if (zero_width_remainder)
                        {
                            bit_position += zero_width_bits - zero_width_remainder;
                        }
                        continue;
                    }
                    if (packed_member)
                    {
                        // A packed bit-field takes the next bit, with no
                        // storage unit to straddle; the IR layout picks the
                        // unit it is read through from the same position.
                        bit_position += member.bit_width;
                        continue;
                    }
                    u64 bit_remainder = bit_position % alignment_bits;
                    if (bit_remainder + member.bit_width > unit_bits)
                    {
                        bit_position += alignment_bits - bit_remainder;
                    }
                    bit_position += member.bit_width;
                    continue;
                }
                alignment = BUSTER_MAX(alignment, member_alignment);
                if (type.kind == C_TYPE_UNION)
                {
                    bit_position = BUSTER_MAX(bit_position, member_size * 8);
                    continue;
                }
                u64 alignment_bits = (u64)member_alignment * 8;
                u64 remainder = bit_position % alignment_bits;
                if (remainder)
                {
                    bit_position += alignment_bits - remainder;
                }
                bit_position += member_size * 8;
            }
            if (!fields_resolved)
            {
                continue;
            }
            if (!c_parse_layout_alignment_specifiers(&layout_context, aggregate_attributes.alignment_start, aggregate_attributes.alignment_count,
                                                     &alignment, &aggregate_provisional))
            {
                continue;
            }
            u64 size = (bit_position + 7) / 8;
            u64 remainder = size % alignment;
            if (remainder)
            {
                size += alignment - remainder;
            }
            sizes[type_index] = size;
            alignments[type_index] = alignment;
            provisional[type_index] = aggregate_provisional;
            resolved[type_index] = true;
            if (type_index == requested.value)
            {
                goto requested_resolved;
            }
            progress = true;
        }
        if (!progress)
        {
            break;
        }
    }
requested_resolved:
    if (cache && !machine->frame_count && !machine->mutation_count)
    {
        if (cache->capacity < type_count)
        {
            u32 new_capacity = BUSTER_MAX(type_count, cache->capacity ? cache->capacity * 2 : 4096);
            u64* new_sizes = arena_allocate(result->arena, u64, new_capacity);
            u32* new_alignments = arena_allocate(result->arena, u32, new_capacity);
            u8* new_states = arena_allocate(result->arena, u8, new_capacity);
            memset(new_states, 0, new_capacity);
            if (cache->capacity)
            {
                memcpy(new_sizes, cache->sizes, sizeof(*new_sizes) * cache->capacity);
                memcpy(new_alignments, cache->alignments, sizeof(*new_alignments) * cache->capacity);
                memcpy(new_states, cache->states, cache->capacity);
            }
            cache->sizes = new_sizes;
            cache->alignments = new_alignments;
            cache->states = new_states;
            cache->capacity = new_capacity;
        }
        for (u32 type_index = 0; type_index < type_count; type_index += 1)
        {
            if (resolved[type_index] && !provisional[type_index] && !cache->states[type_index])
            {
                cache->sizes[type_index] = sizes[type_index];
                cache->alignments[type_index] = alignments[type_index];
                cache->states[type_index] = 1;
            }
        }
    }
    if (!resolved[requested.value])
    {
        return false;
    }
    *size_out = sizes[requested.value];
    *alignment_out = alignments[requested.value];
    return true;
}

CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name);
BUSTER_C_INTERNAL CEntityId c_parse_lookup_entity_symbol(CParseResult* result, CScopeId scope, u32 symbol, String8 name);
BUSTER_C_SHARED u32 c_parse_name_symbol(CParseResult* result, String8 name);
CEntityId c_parse_lookup_entity_at(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, String8 name, u32 token_index);

// Resolve an identifier token's entity: interned tokens skip the name hash
// entirely, symbol-less tokens intern on demand so the symbol-keyed buckets
// stay authoritative.
BUSTER_C_SHARED CEntityId c_parse_lookup_entity_token(CParseResult* result, char8 const* spelling_base, CScopeId scope, CToken const* token)
{
    u32 symbol = token->symbol;
    String8 spelling = c_token_spelling(spelling_base, *token);
    if (!symbol)
    {
        symbol = c_parse_name_symbol(result, spelling);
    }
    return c_parse_lookup_entity_symbol(result, scope, symbol, spelling);
}

BUSTER_C_SHARED CTypeId c_parse_add_type(CParseResult* result, CType type);

BUSTER_C_INTERNAL CTypeId c_parse_unqualified_type(CParseResult* result, CTypeId type_id);

BUSTER_C_INTERNAL CTypeId c_parse_string_literal_expression_type(Arena* arena, CPreprocessResult preprocess, CParseResult* result, u32 start, u32 end);

BUSTER_C_INTERNAL bool c_parse_direct_expression_type(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CScopeId scope, u32 start, u32 end,
                                                        CTypeId* type_out)
{
    u8* prefix_operators = arena_allocate(arena, u8, end - start + 1);
    u32 prefix_count = 0;
    bool normalize = true;
    while (normalize && start < end)
    {
        normalize = false;
        while (start < end && c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 depth = 0;
            u32 close = start;
            for (; close < end; close += 1)
            {
                CToken token = preprocess.tokens[close];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    if (!depth)
                    {
                        return false;
                    }
                    depth -= 1;
                    if (!depth)
                    {
                        break;
                    }
                }
            }
            if (close != end - 1)
            {
                break;
            }
            start += 1;
            end -= 1;
            normalize = true;
        }
        if (start < end &&
            (c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_STAR) || c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_AMPERSAND)))
        {
            prefix_operators[prefix_count++] = c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_STAR) ? '*' : '&';
            start += 1;
            normalize = true;
        }
    }
    u32 base_start = start;
    u32 base_end = end;
    u32 postfix = end;
    u32 parentheses = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            parentheses += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) && parentheses)
        {
            parentheses -= 1;
        }
        else if (!parentheses && (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_DOT) ||
                                  c_token_is_punctuator(&token, C_PUNCTUATOR_ARROW)))
        {
            postfix = index;
            base_end = index;
            break;
        }
    }
    while (base_start < base_end && c_token_is_punctuator(&preprocess.tokens[base_start], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
           c_token_is_punctuator(&preprocess.tokens[base_end - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        base_start += 1;
        base_end -= 1;
    }
    if (base_end != base_start + 1 || preprocess.tokens[base_start].kind != C_TOKEN_IDENTIFIER)
    {
        return false;
    }
    CEntityId entity_id = C_ENTITY_ID_INVALID;
    u32 base_use_index = c_parse_identifier_use_index(result, base_start);
    if (base_use_index != C_ID_UNDERLYING_INVALID)
    {
        entity_id = result->identifier_uses[base_use_index].entity;
    }
    CScopeId lookup_scope = scope;
    if (lookup_scope.value == C_ID_UNDERLYING_INVALID && result->scope_count)
    {
        lookup_scope = (CScopeId){
            .value = 0,
        };
    }
    if (entity_id.value == C_ID_UNDERLYING_INVALID && lookup_scope.value != C_ID_UNDERLYING_INVALID)
    {
        entity_id = c_parse_lookup_entity_token(result, preprocess.spelling_base, lookup_scope, &preprocess.tokens[base_start]);
    }
    CTypeId type = entity_id.value < result->entity_count && result->entities[entity_id.value].kind != C_ENTITY_TYPEDEF &&
                           result->entities[entity_id.value].kind != C_ENTITY_ENUMERATOR
                       ? result->entities[entity_id.value].type
                       : C_TYPE_ID_INVALID;
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    u32 index = postfix;
    while (index < end)
    {
        if (type.value >= result->type_count)
        {
            return false;
        }
        CType* type_value = &result->types[type.value];
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACKET))
        {
            u32 depth = 1;
            u32 close = index + 1;
            while (close < end && depth)
            {
                if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_BRACKET))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_BRACKET))
                {
                    depth -= 1;
                }
                close += 1;
            }
            if (depth || (type_value->kind != C_TYPE_ARRAY && type_value->kind != C_TYPE_POINTER))
            {
                return false;
            }
            type = type_value->element_type;
            index = close;
            continue;
        }
        bool indirect = c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_ARROW);
        if (!indirect && !c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_DOT))
        {
            return false;
        }
        if (indirect)
        {
            if (type_value->kind != C_TYPE_POINTER || type_value->element_type.value >= result->type_count)
            {
                return false;
            }
            type = type_value->element_type;
            type_value = &result->types[type.value];
        }
        if (index + 1 >= end || preprocess.tokens[index + 1].kind != C_TOKEN_IDENTIFIER ||
            (type_value->kind != C_TYPE_STRUCT && type_value->kind != C_TYPE_UNION))
        {
            return false;
        }
        CTypeId field_type = C_TYPE_ID_INVALID;
        TemporalArena field_search = arena_begin_temporal(arena);
        CTypeId* work = arena_allocate(field_search.arena, CTypeId, result->type_count + 1);
        bool* visited = arena_allocate(field_search.arena, bool, result->type_count + 1);
        memset(visited, 0, sizeof(*visited) * (result->type_count + 1));
        u32 work_index = 0;
        u32 work_count = 1;
        work[0] = type;
        visited[type.value] = true;
        while (work_index < work_count && field_type.value == C_ID_UNDERLYING_INVALID)
        {
            CTypeId candidate_id = work[work_index++];
            CType* candidate = &result->types[candidate_id.value];
            for (u32 field_index = 0; field_index < candidate->member_count; field_index += 1)
            {
                CMember* member = &result->members[candidate->member_start + field_index];
                if (string_equal(member->name, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index + 1])))
                {
                    field_type = member->type;
                    break;
                }
                if (member->name.length || member->type.value >= result->type_count || visited[member->type.value])
                {
                    continue;
                }
                CType* nested = &result->types[member->type.value];
                if (nested->kind != C_TYPE_STRUCT && nested->kind != C_TYPE_UNION)
                {
                    continue;
                }
                visited[member->type.value] = true;
                work[work_count++] = member->type;
            }
        }
        if (field_type.value == C_ID_UNDERLYING_INVALID)
        {
            scratch_end(field_search);
            return false;
        }
        type = field_type;
        scratch_end(field_search);
        index += 2;
    }
    u32 synthetic_pointer_depth = 0;
    while (prefix_count)
    {
        u8 operation = prefix_operators[--prefix_count];
        if (operation == '&')
        {
            synthetic_pointer_depth += 1;
            continue;
        }
        if (synthetic_pointer_depth)
        {
            synthetic_pointer_depth -= 1;
            continue;
        }
        if (type.value >= result->type_count)
        {
            return false;
        }
        CType* pointer = &result->types[type.value];
        if (pointer->kind != C_TYPE_POINTER && pointer->kind != C_TYPE_ARRAY)
        {
            return false;
        }
        type = pointer->element_type;
    }
    while (synthetic_pointer_depth)
    {
        type = c_parse_add_type(result, (CType){
                                            .element_type = type,
                                            .return_type = C_TYPE_ID_INVALID,
                                            .array_bound = C_ARRAY_BOUND_INVALID,
                                            .kind = C_TYPE_POINTER,
                                        });
        synthetic_pointer_depth -= 1;
    }
    *type_out = type;
    return true;
}

BUSTER_C_INTERNAL u32 c_parse_matching_delimiter(CPreprocessResult preprocess, u32 open, u32 end, CPunctuator opening, CPunctuator closing)
{
    // The pair is loop-invariant, so it becomes one set the scan tests each
    // token against; see c_ir_matching_delimiter for the same shape.
    u64 delimiters = C_PUNCTUATOR_BIT(opening) | C_PUNCTUATOR_BIT(closing);
    u32 depth = 0;
    for (u32 index = open; index < end; index += 1)
    {
        u8 punctuator = preprocess.tokens[index].punctuator;
        if (c_punctuator_in_set(punctuator, delimiters))
        {
            if (punctuator == opening)
            {
                depth += 1;
            }
            else
            {
                if (!depth)
                {
                    return end;
                }
                depth -= 1;
                if (!depth)
                {
                    return index;
                }
            }
        }
    }
    return end;
}

BUSTER_C_INTERNAL CTypeId c_parse_expression_scalar_type(CParseResult* result, CTypeKind kind)
{
    return c_parse_add_type(result, (CType){
                                        .element_type = C_TYPE_ID_INVALID,
                                        .return_type = C_TYPE_ID_INVALID,
                                        .unqualified_type = C_TYPE_ID_INVALID,
                                        .array_bound = C_ARRAY_BOUND_INVALID,
                                        .kind = kind,
                                        .is_complete = true,
                                    });
}

BUSTER_C_INTERNAL bool c_parse_expression_integer_kind(CTypeKind kind)
{
    return kind == C_TYPE_BOOL || kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_UNSIGNED_CHAR || kind == C_TYPE_SHORT ||
           kind == C_TYPE_UNSIGNED_SHORT || kind == C_TYPE_INT || kind == C_TYPE_UNSIGNED_INT || kind == C_TYPE_LONG || kind == C_TYPE_UNSIGNED_LONG ||
           kind == C_TYPE_LONG_LONG || kind == C_TYPE_UNSIGNED_LONG_LONG || kind == C_TYPE_INT128 || kind == C_TYPE_UNSIGNED_INT128 || kind == C_TYPE_ENUM;
}

BUSTER_C_INTERNAL bool c_parse_expression_signed_kind(CTypeKind kind)
{
    return kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_SHORT || kind == C_TYPE_INT || kind == C_TYPE_LONG || kind == C_TYPE_LONG_LONG ||
           kind == C_TYPE_INT128 || kind == C_TYPE_ENUM;
}

BUSTER_C_INTERNAL CTypeKind c_parse_expression_unsigned_kind(CTypeKind kind)
{
    switch (kind)
    {
    case C_TYPE_CHAR:
    case C_TYPE_SIGNED_CHAR:
    case C_TYPE_UNSIGNED_CHAR:
        return C_TYPE_UNSIGNED_CHAR;
    case C_TYPE_SHORT:
    case C_TYPE_UNSIGNED_SHORT:
        return C_TYPE_UNSIGNED_SHORT;
    case C_TYPE_INT:
    case C_TYPE_UNSIGNED_INT:
    case C_TYPE_ENUM:
        return C_TYPE_UNSIGNED_INT;
    case C_TYPE_LONG:
    case C_TYPE_UNSIGNED_LONG:
        return C_TYPE_UNSIGNED_LONG;
    case C_TYPE_LONG_LONG:
    case C_TYPE_UNSIGNED_LONG_LONG:
        return C_TYPE_UNSIGNED_LONG_LONG;
    case C_TYPE_INT128:
    case C_TYPE_UNSIGNED_INT128:
        return C_TYPE_UNSIGNED_INT128;
    case C_TYPE_INVALID:
    case C_TYPE_VOID:
    case C_TYPE_BOOL:
    case C_TYPE_FLOAT:
    case C_TYPE_DOUBLE:
    case C_TYPE_LONG_DOUBLE:
    case C_TYPE_FLOAT_COMPLEX:
    case C_TYPE_DOUBLE_COMPLEX:
    case C_TYPE_LONG_DOUBLE_COMPLEX:
    case C_TYPE_VA_LIST:
    case C_TYPE_NULLPTR:
    case C_TYPE_POINTER:
    case C_TYPE_ARRAY:
    case C_TYPE_VECTOR:
    case C_TYPE_FUNCTION:
    case C_TYPE_STRUCT:
    case C_TYPE_UNION:
    case C_TYPE_COUNT:
        return C_TYPE_INVALID;
    }
    return C_TYPE_INVALID;
}

BUSTER_C_INTERNAL CTypeKind c_parse_expression_promoted_kind(CTypeKind kind)
{
    if (kind == C_TYPE_BOOL || kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_UNSIGNED_CHAR || kind == C_TYPE_SHORT ||
        kind == C_TYPE_UNSIGNED_SHORT || kind == C_TYPE_ENUM)
    {
        return C_TYPE_INT;
    }
    return kind;
}

BUSTER_C_INTERNAL CTypeId c_parse_expression_arithmetic_type(CParseResult* result, Target target, CTypeId left_id, CTypeId right_id)
{
    if (left_id.value >= result->type_count || right_id.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CTypeKind left = result->types[left_id.value].kind;
    CTypeKind right = result->types[right_id.value].kind;
    if (left == C_TYPE_LONG_DOUBLE || right == C_TYPE_LONG_DOUBLE)
    {
        return c_parse_expression_scalar_type(result, C_TYPE_LONG_DOUBLE);
    }
    if (left == C_TYPE_DOUBLE || right == C_TYPE_DOUBLE)
    {
        return c_parse_expression_scalar_type(result, C_TYPE_DOUBLE);
    }
    if (left == C_TYPE_FLOAT || right == C_TYPE_FLOAT)
    {
        return c_parse_expression_scalar_type(result, C_TYPE_FLOAT);
    }
    if (!c_parse_expression_integer_kind(left) || !c_parse_expression_integer_kind(right))
    {
        return C_TYPE_ID_INVALID;
    }
    left = c_parse_expression_promoted_kind(left);
    right = c_parse_expression_promoted_kind(right);
    u64 left_size = 0;
    u64 right_size = 0;
    u32 ignored_alignment = 0;
    if (!c_parse_builtin_type_layout(target, left, &left_size, &ignored_alignment) ||
        !c_parse_builtin_type_layout(target, right, &right_size, &ignored_alignment))
    {
        return C_TYPE_ID_INVALID;
    }
    CTypeKind result_kind = left;
    bool left_signed = c_parse_expression_signed_kind(left);
    bool right_signed = c_parse_expression_signed_kind(right);
    if (left_signed == right_signed)
    {
        if (right_size > left_size || (right_size == left_size && (u32)right > (u32)left))
        {
            result_kind = right;
        }
    }
    else
    {
        CTypeKind signed_kind = left_signed ? left : right;
        CTypeKind unsigned_kind = left_signed ? right : left;
        u64 signed_size = left_signed ? left_size : right_size;
        u64 unsigned_size = left_signed ? right_size : left_size;
        if (unsigned_size >= signed_size)
        {
            result_kind = unsigned_kind;
        }
        else
        {
            result_kind = signed_kind;
        }
        if (signed_size == unsigned_size && c_parse_expression_signed_kind(result_kind))
        {
            result_kind = c_parse_expression_unsigned_kind(result_kind);
        }
    }
    return result_kind == C_TYPE_INVALID ? C_TYPE_ID_INVALID : c_parse_expression_scalar_type(result, result_kind);
}

BUSTER_C_INTERNAL bool c_parse_expression_token_ends_operand(CToken token)
{
    return token.kind == C_TOKEN_IDENTIFIER || token.kind == C_TOKEN_PREPROCESSING_NUMBER || token.kind == C_TOKEN_CHARACTER_LITERAL ||
           token.kind == C_TOKEN_STRING_LITERAL || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
           c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE) ||
           c_token_is_punctuator(&token, C_PUNCTUATOR_PLUS_PLUS) || c_token_is_punctuator(&token, C_PUNCTUATOR_MINUS_MINUS);
}

BUSTER_C_INTERNAL u32 c_parse_expression_operator_precedence(CToken token)
{
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
    {
        return 1;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_PLUS_ASSIGN) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_MINUS_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_STAR_ASSIGN) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_SLASH_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_PERCENT_ASSIGN) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_SHIFT_LEFT_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_SHIFT_RIGHT_ASSIGN) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_AMPERSAND_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_CARET_ASSIGN) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_PIPE_ASSIGN))
    {
        return 2;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_PIPE_PIPE))
    {
        return 4;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_AMPERSAND_AMPERSAND))
    {
        return 5;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_PIPE))
    {
        return 6;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_CARET))
    {
        return 7;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_AMPERSAND))
    {
        return 8;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_EQUAL) || c_token_is_punctuator(&token, C_PUNCTUATOR_NOT_EQUAL))
    {
        return 9;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_LESS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LESS_EQUAL) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_GREATER) || c_token_is_punctuator(&token, C_PUNCTUATOR_GREATER_EQUAL))
    {
        return 10;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_SHIFT_LEFT) || c_token_is_punctuator(&token, C_PUNCTUATOR_SHIFT_RIGHT))
    {
        return 11;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_PLUS) || c_token_is_punctuator(&token, C_PUNCTUATOR_MINUS))
    {
        return 12;
    }
    if (c_token_is_punctuator(&token, C_PUNCTUATOR_STAR) || c_token_is_punctuator(&token, C_PUNCTUATOR_SLASH) ||
        c_token_is_punctuator(&token, C_PUNCTUATOR_PERCENT))
    {
        return 13;
    }
    return 0;
}

BUSTER_C_INTERNAL CTypeId c_parse_expression_leaf_without_cast(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CScopeId scope, u32 start,
                                                                 u32 end)
{
    if (start >= end)
    {
        return C_TYPE_ID_INVALID;
    }
    CToken first = preprocess.tokens[start];
    if (first.kind == C_TOKEN_PREPROCESSING_NUMBER)
    {
        CTypeKind kind = C_TYPE_INT;
        String8 first_spelling = c_token_spelling(preprocess.spelling_base, first);
        bool hexadecimal = first_spelling.length >= 2 && first_spelling.pointer[0] == '0' && (first_spelling.pointer[1] == 'x' || first_spelling.pointer[1] == 'X');
        bool floating = false;
        for (u64 index = 0; index < first_spelling.length; index += 1)
        {
            u8 byte = first_spelling.pointer[index];
            floating |= byte == '.' || byte == 'p' || byte == 'P' || (!hexadecimal && (byte == 'e' || byte == 'E'));
        }
        if (floating)
        {
            // The whole run of trailing suffix letters, not just the last
            // one: GNU's imaginary `i`/`j` may sit on either side of the
            // width suffix, and musl spells `_Complex_I` as `1.0fi` where
            // glibc spells it `1.0iF`.
            bool single = false;
            bool extended = false;
            bool imaginary = false;
            for (u64 scan = first_spelling.length; scan; scan -= 1)
            {
                u8 letter = first_spelling.pointer[scan - 1];
                if (letter == 'f' || letter == 'F')
                {
                    single = true;
                }
                else if (letter == 'l' || letter == 'L')
                {
                    extended = true;
                }
                else if (letter == 'i' || letter == 'I' || letter == 'j' || letter == 'J')
                {
                    imaginary = true;
                }
                else
                {
                    break;
                }
            }
            kind = single ? C_TYPE_FLOAT : extended ? C_TYPE_LONG_DOUBLE : C_TYPE_DOUBLE;
            if (imaginary)
            {
                kind = c_type_kind_complex_of(kind);
            }
        }
        else
        {
            bool is_unsigned = false;
            u32 long_count = 0;
            for (u64 index = first_spelling.length; index != 0; index -= 1)
            {
                u8 byte = first_spelling.pointer[index - 1];
                if (byte == 'u' || byte == 'U')
                {
                    is_unsigned = true;
                }
                else if (byte == 'l' || byte == 'L')
                {
                    long_count += 1;
                }
                else
                {
                    break;
                }
            }
            kind = long_count >= 2   ? (is_unsigned ? C_TYPE_UNSIGNED_LONG_LONG : C_TYPE_LONG_LONG)
                   : long_count == 1 ? (is_unsigned ? C_TYPE_UNSIGNED_LONG : C_TYPE_LONG)
                   : is_unsigned     ? C_TYPE_UNSIGNED_INT
                                     : C_TYPE_INT;
        }
        return end == start + 1 ? c_parse_expression_scalar_type(result, kind) : C_TYPE_ID_INVALID;
    }
    if (first.kind == C_TOKEN_CHARACTER_LITERAL && end == start + 1)
    {
        return c_parse_expression_scalar_type(result, C_TYPE_INT);
    }
    if (first.kind == C_TOKEN_STRING_LITERAL)
    {
        u32 literal_end = start + 1;
        while (literal_end < end && preprocess.tokens[literal_end].kind == C_TOKEN_STRING_LITERAL)
        {
            literal_end += 1;
        }
        if (literal_end == end)
        {
            return c_parse_string_literal_expression_type(arena, preprocess, result, start, end);
        }
    }
    if (first.kind == C_TOKEN_IDENTIFIER)
    {
        if ((string_equal(c_token_spelling(preprocess.spelling_base, first), S8("sizeof")) || string_equal(c_token_spelling(preprocess.spelling_base, first), S8("_Alignof")) || string_equal(c_token_spelling(preprocess.spelling_base, first), S8("alignof"))) &&
            start + 1 < end)
        {
            return c_parse_expression_scalar_type(result,
                                                  target_uses_llp64_data_model(preprocess.target) ? C_TYPE_UNSIGNED_LONG_LONG : C_TYPE_UNSIGNED_LONG);
        }
        if (c_preprocess_dialect_is_c23(preprocess.dialect) && (string_equal(c_token_spelling(preprocess.spelling_base, first), S8("true")) || string_equal(c_token_spelling(preprocess.spelling_base, first), S8("false"))) &&
            end == start + 1)
        {
            return c_parse_expression_scalar_type(result, C_TYPE_BOOL);
        }
        if (c_preprocess_dialect_is_c23(preprocess.dialect) && string_equal(c_token_spelling(preprocess.spelling_base, first), S8("nullptr")) && end == start + 1)
        {
            return c_parse_expression_scalar_type(result, C_TYPE_NULLPTR);
        }
        if (start + 2 < end && c_token_is_punctuator(&preprocess.tokens[start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 close = c_parse_matching_delimiter(preprocess, start + 1, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
            if (close == end - 1)
            {
                CEntityId entity = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &first);
                if (entity.value < result->entity_count)
                {
                    CTypeId function_id = result->entities[entity.value].type;
                    if (function_id.value < result->type_count)
                    {
                        CType* function = result->types + function_id.value;
                        if (function->kind == C_TYPE_POINTER && function->element_type.value < result->type_count)
                        {
                            function = result->types + function->element_type.value;
                        }
                        if (function->kind == C_TYPE_FUNCTION)
                        {
                            return function->return_type;
                        }
                    }
                }
            }
        }
    }
    CTypeId type = C_TYPE_ID_INVALID;
    return c_parse_direct_expression_type(arena, preprocess, result, scope, start, end, &type) ? type : C_TYPE_ID_INVALID;
}

BUSTER_C_INTERNAL CTypeId c_parse_auto_decay_type(CParseResult* result, CTypeId type);
BUSTER_C_INTERNAL CTypeId c_parse_conditional_expression_type(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId left,
                                                                CTypeId right);

BUSTER_C_INTERNAL void c_type_parse_sizeof_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    Arena* arena = frame->arena;
    CPreprocessResult preprocess = frame->preprocess;
    CParseResult* result = frame->result;
    CScopeId scope = frame->scope;
    u32 start = frame->start;
    u32 end = frame->end;
    if (start >= end)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
        return;
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        u32 task_range = end - start;
        u32 task_required = task_range + 1;
        if (task_range == UINT32_MAX || machine->expression_task_count > machine->expression_task_capacity ||
            task_required > machine->expression_task_capacity - machine->expression_task_count)
        {
            machine->failed = true;
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
            return;
        }
        frame->task_capacity = task_required;
        frame->task_mark = machine->expression_task_count;
        frame->arena_mark = machine->scratch_arena->position;
        frame->expression_tasks = machine->expression_tasks + machine->expression_task_count;
        machine->expression_task_count += frame->task_capacity;
        frame->task_count = 1;
        frame->expression_tasks[0] = (CParseExpressionTypeTask){
            .start = start,
            .end = end,
        };
        frame->type = C_TYPE_ID_INVALID;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    else if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        frame->type = machine->result_valid ? machine->result_type : C_TYPE_ID_INVALID;
        frame->task_count -= 1;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    CParseExpressionTypeTask* tasks = frame->expression_tasks;
    u32 task_count = frame->task_count;
    u32 capacity = frame->task_capacity;
    CTypeId last = frame->type;
    while (task_count)
    {
        CParseExpressionTypeTask* task = tasks + task_count - 1;
        if (!task->state)
        {
            while (task->start < task->end && c_token_is_punctuator(&preprocess.tokens[task->start], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                   c_parse_matching_delimiter(preprocess, task->start, task->end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS) ==
                       task->end - 1)
            {
                task->start += 1;
                task->end -= 1;
            }
            if (task->start >= task->end)
            {
                last = C_TYPE_ID_INVALID;
                task_count -= 1;
                continue;
            }
            CToken first = preprocess.tokens[task->start];
            if (c_token_is_punctuator(&first, C_PUNCTUATOR_PLUS) || c_token_is_punctuator(&first, C_PUNCTUATOR_MINUS) ||
                c_token_is_punctuator(&first, C_PUNCTUATOR_TILDE) || c_token_is_punctuator(&first, C_PUNCTUATOR_EXCLAMATION) ||
                c_token_is_punctuator(&first, C_PUNCTUATOR_PLUS_PLUS) || c_token_is_punctuator(&first, C_PUNCTUATOR_MINUS_MINUS))
            {
                task->operation = c_token_is_punctuator(&first, C_PUNCTUATOR_EXCLAMATION) ? C_PARSE_EXPRESSION_TYPE_LOGICAL_NOT : C_PARSE_EXPRESSION_TYPE_UNARY;
                task->state = 1;
                if (task_count >= capacity)
                {
                    last = C_TYPE_ID_INVALID;
                    break;
                }
                tasks[task_count++] = (CParseExpressionTypeTask){
                    .start = task->start + 1,
                    .end = task->end,
                };
                continue;
            }
            u32 parentheses = 0;
            u32 brackets = 0;
            u32 braces = 0;
            u32 best_precedence = UINT32_MAX;
            u32 best_operator = task->end;
            u32 question = task->end;
            u32 colon = task->end;
            u32 nested_questions = 0;
            for (u32 index = task->start; index < task->end; index += 1)
            {
                CToken token = preprocess.tokens[index];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    parentheses += 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) && parentheses)
                {
                    parentheses -= 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    brackets += 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) && brackets)
                {
                    brackets -= 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
                {
                    braces += 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE) && braces)
                {
                    braces -= 1;
                    continue;
                }
                if (parentheses || brackets || braces)
                {
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_QUESTION))
                {
                    if (question == task->end)
                    {
                        question = index;
                    }
                    nested_questions += 1;
                    continue;
                }
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_COLON) && nested_questions)
                {
                    nested_questions -= 1;
                    if (!nested_questions && question != task->end)
                    {
                        colon = index;
                    }
                    continue;
                }
                u32 precedence = c_parse_expression_operator_precedence(token);
                bool binary = precedence && index > task->start && index + 1 < task->end && c_parse_expression_token_ends_operand(preprocess.tokens[index - 1]);
                bool right_associative = precedence == 2;
                if (binary && (precedence < best_precedence || (precedence == best_precedence && !right_associative)))
                {
                    best_precedence = precedence;
                    best_operator = index;
                }
            }
            bool conditional = question != task->end && colon != task->end && (best_operator == task->end || best_precedence > 3);
            if (conditional)
            {
                task->operation = C_PARSE_EXPRESSION_TYPE_CONDITIONAL;
                task->split = question;
                task->colon = colon;
                task->state = 1;
                if (task_count >= capacity)
                {
                    last = C_TYPE_ID_INVALID;
                    break;
                }
                tasks[task_count++] = (CParseExpressionTypeTask){
                    .start = question + 1,
                    .end = colon,
                };
                continue;
            }
            if (best_operator != task->end)
            {
                CToken operation = preprocess.tokens[best_operator];
                task->split = best_operator;
                task->operation = best_precedence == 1   ? C_PARSE_EXPRESSION_TYPE_COMMA
                                  : best_precedence == 2 ? C_PARSE_EXPRESSION_TYPE_ASSIGN
                                  : best_precedence == 4 || best_precedence == 5 || (best_precedence >= 9 && best_precedence <= 10)
                                      ? C_PARSE_EXPRESSION_TYPE_COMPARE
                                  : best_precedence == 11 ? C_PARSE_EXPRESSION_TYPE_SHIFT
                                                          : C_PARSE_EXPRESSION_TYPE_ARITHMETIC;
                BUSTER_UNUSED(operation);
                task->state = 1;
                if (task_count >= capacity)
                {
                    last = C_TYPE_ID_INVALID;
                    break;
                }
                tasks[task_count++] = (CParseExpressionTypeTask){
                    .start = task->start,
                    .end = best_operator,
                };
                continue;
            }
            frame->task_count = task_count;
            frame->type = last;
            frame->stage = C_TYPE_PARSE_STAGE_CHILD;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = result,
                                                      .preprocess = preprocess,
                                                      .arena = arena,
                                                      .scope = scope,
                                                      .start = task->start,
                                                      .end = task->end,
                                                      .kind = C_TYPE_PARSE_FRAME_EXPRESSION_LEAF,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
            }
            return;
        }
        if (task->operation == C_PARSE_EXPRESSION_TYPE_UNARY || task->operation == C_PARSE_EXPRESSION_TYPE_LOGICAL_NOT)
        {
            if (last.value >= result->type_count)
            {
                last = C_TYPE_ID_INVALID;
            }
            else if (task->operation == C_PARSE_EXPRESSION_TYPE_LOGICAL_NOT)
            {
                last = c_parse_expression_scalar_type(result, C_TYPE_INT);
            }
            else
            {
                CTypeKind kind = result->types[last.value].kind;
                kind = c_parse_expression_promoted_kind(kind);
                last = (c_parse_expression_integer_kind(kind) || kind == C_TYPE_FLOAT || kind == C_TYPE_DOUBLE || kind == C_TYPE_LONG_DOUBLE)
                           ? c_parse_expression_scalar_type(result, kind)
                           : C_TYPE_ID_INVALID;
            }
            task_count -= 1;
            continue;
        }
        if (task->state == 1)
        {
            task->left_type = last;
            task->state = 2;
            u32 right_start = task->operation == C_PARSE_EXPRESSION_TYPE_CONDITIONAL ? task->colon + 1 : task->split + 1;
            if (task_count >= capacity)
            {
                last = C_TYPE_ID_INVALID;
                break;
            }
            tasks[task_count++] = (CParseExpressionTypeTask){
                .start = right_start,
                .end = task->end,
            };
            continue;
        }
        CTypeId left = task->left_type;
        CTypeId right = last;
        if (left.value >= result->type_count || right.value >= result->type_count)
        {
            last = C_TYPE_ID_INVALID;
            task_count -= 1;
            continue;
        }
        CType* left_type = result->types + left.value;
        CType* right_type = result->types + right.value;
        switch (task->operation)
        {
        case C_PARSE_EXPRESSION_TYPE_COMMA:
        {
            last = right;
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_ASSIGN:
        {
            last = c_parse_unqualified_type(result, left);
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_COMPARE:
        {
            last = c_parse_expression_scalar_type(result, C_TYPE_INT);
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_SHIFT:
        {
            CTypeKind kind = c_parse_expression_promoted_kind(left_type->kind);
            last = c_parse_expression_integer_kind(kind) ? c_parse_expression_scalar_type(result, kind) : C_TYPE_ID_INVALID;
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_ARITHMETIC:
        {
            CToken operation = preprocess.tokens[task->split];
            bool add_or_subtract = c_token_is_punctuator(&operation, C_PUNCTUATOR_PLUS) || c_token_is_punctuator(&operation, C_PUNCTUATOR_MINUS);
            if (add_or_subtract && left_type->kind == C_TYPE_POINTER && c_parse_expression_integer_kind(right_type->kind))
            {
                last = left;
            }
            else if (add_or_subtract && right_type->kind == C_TYPE_POINTER && c_parse_expression_integer_kind(left_type->kind) &&
                     c_token_is_punctuator(&operation, C_PUNCTUATOR_PLUS))
            {
                last = right;
            }
            else if (left_type->kind == C_TYPE_POINTER && right_type->kind == C_TYPE_POINTER && c_token_is_punctuator(&operation, C_PUNCTUATOR_MINUS))
            {
                last = c_parse_expression_scalar_type(result, target_uses_llp64_data_model(preprocess.target) ? C_TYPE_LONG_LONG : C_TYPE_LONG);
            }
            else
            {
                last = c_parse_expression_arithmetic_type(result, preprocess.target, left, right);
            }
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_CONDITIONAL:
        {
            if (frame->auto_conditional)
            {
                last = c_parse_conditional_expression_type(arena, preprocess, result, left, right);
            }
            else if (left.value == right.value || (left_type->kind == right_type->kind && left_type->kind != C_TYPE_POINTER))
            {
                last = left;
            }
            else if (left_type->kind == C_TYPE_POINTER && right_type->kind == C_TYPE_NULLPTR)
            {
                last = left;
            }
            else if (right_type->kind == C_TYPE_POINTER && left_type->kind == C_TYPE_NULLPTR)
            {
                last = right;
            }
            else
            {
                last = c_parse_expression_arithmetic_type(result, preprocess.target, left, right);
            }
        }
        break;
        case C_PARSE_EXPRESSION_TYPE_NONE:
        case C_PARSE_EXPRESSION_TYPE_UNARY:
        case C_PARSE_EXPRESSION_TYPE_LOGICAL_NOT:
        {
            last = C_TYPE_ID_INVALID;
        }
        break;
        }
        task_count -= 1;
    }
    if (last.value >= result->type_count)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
        return;
    }
    c_type_parse_frame_complete(machine, last, end, true);
}

BUSTER_C_INTERNAL bool c_parse_expression_type_query(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                       CScopeId scope, u32 start, u32 end, bool auto_conditional, CTypeId* type_out)
{
    u32 frame_start = machine->frame_count;
    CParseResult checkpoint = *result;
    u32 mutation_mark = machine->mutation_count;
    machine->mutation_type_limit = checkpoint.type_count;
    machine->result_valid = false;
    bool pushed = c_type_parse_frame_push(machine, (CTypeParseFrame){
                                              .result = result,
                                              .preprocess = preprocess,
                                              .arena = arena,
                                              .scope = scope,
                                              .start = start,
                                              .end = end,
                                              .kind = C_TYPE_PARSE_FRAME_SIZEOF,
                                              .auto_conditional = auto_conditional,
                                          });
    if (pushed)
    {
        c_type_parse_machine_run(machine, frame_start);
    }
    bool valid = c_type_parse_root_finish(machine, result, checkpoint, mutation_mark,
                                          start < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[start]) : (CSourceLocation){0});
    if (valid)
    {
        *type_out = machine->result_type;
    }
    return valid;
}

BUSTER_C_INTERNAL bool c_parse_sizeof_expression_type(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                        CScopeId scope, u32 start, u32 end,
                                                        CTypeId* type_out)
{
    return c_parse_expression_type_query(machine, arena, preprocess, result, scope, start, end, false, type_out);
}


BUSTER_C_INTERNAL bool c_parse_static_assert_evaluate(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                        CDeclaration declaration, CScopeId scope, u64* value_out, String8* message_out)
{
    u32 start = declaration.token_start;
    u32 end = start + declaration.token_count;
    if (start + 3 >= end || !c_token_is_punctuator(&preprocess.tokens[start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 comma = end;
    u32 close = end;
    u32 depth = 0;
    // Braces and brackets carry commas of their own -- a compound literal
    // operand (`_Static_assert(sizeof (struct S){1, 2} == 8, "...")`) or a
    // subscript -- and neither can close the assert's parenthesis, so they get
    // their own depth. Counting only parentheses takes the literal's first
    // comma for the message separator and truncates the expression.
    u32 group_depth = 0;
    for (u32 token_index = start + 1; token_index < end; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            group_depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
        {
            group_depth -= group_depth != 0;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (!depth)
            {
                return false;
            }
            depth -= 1;
            if (!depth)
            {
                close = token_index;
                break;
            }
        }
        else if (depth == 1 && !group_depth && comma == end && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
        {
            comma = token_index;
        }
    }
    u32 expression_end = comma < close ? comma : close;
    if (close == end || expression_end <= start + 2)
    {
        return false;
    }
    if (comma < close && comma + 1 < close && preprocess.tokens[comma + 1].kind == C_TOKEN_STRING_LITERAL)
    {
        *message_out = c_token_spelling(preprocess.spelling_base, preprocess.tokens[comma + 1]);
    }
    u32 expression_count = expression_end - (start + 2);
    CToken* tokens = arena_allocate(arena, CToken, expression_count * 2 + 1);
    u32 token_count = 0;
    u64 evaluation_spelling_capacity = 0;
    for (u32 expression_index = 0; expression_index < expression_count; expression_index += 1)
    {
        evaluation_spelling_capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[start + 2 + expression_index]) + 21;
    }
    CSpellingSpace evaluation_space = c_space_local(arena, evaluation_spelling_capacity);
    for (u32 expression_index = 0; expression_index < expression_count; expression_index += 1)
    {
        CToken token = preprocess.tokens[start + 2 + expression_index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 cast_close = expression_index + 1;
            u32 cast_depth = 1;
            while (cast_close < expression_count && cast_depth)
            {
                CToken cast_token = preprocess.tokens[start + 2 + cast_close];
                if (c_token_is_punctuator(&cast_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    cast_depth += 1;
                }
                else if (c_token_is_punctuator(&cast_token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    cast_depth -= 1;
                    if (!cast_depth)
                    {
                        break;
                    }
                }
                cast_close += 1;
            }
            if (!cast_depth && cast_close > expression_index + 1 && cast_close + 1 < expression_count)
            {
                u32 type_start = start + 3 + expression_index;
                u32 type_end = start + 2 + cast_close;
                u32 type_index = type_start;
                CTypeId cast_type = machine ? c_parse_scalar_type_in_scope(machine, result, preprocess, scope, type_start, type_end, &type_index)
                                            : c_parse_machineless_base_type(result, preprocess, scope, type_start, type_end, &type_index);
                if (cast_type.value == C_ID_UNDERLYING_INVALID && type_start + 1 == type_end && preprocess.tokens[type_start].kind == C_TOKEN_IDENTIFIER)
                {
                    for (u32 entity_index = result->entity_count; entity_index; entity_index -= 1)
                    {
                        CEntity* candidate = &result->entities[entity_index - 1];
                        if (candidate->kind == C_ENTITY_TYPEDEF && string_equal(candidate->name, c_token_spelling(preprocess.spelling_base, preprocess.tokens[type_start])))
                        {
                            cast_type = candidate->type;
                            type_index = type_end;
                            break;
                        }
                    }
                }
                if (cast_type.value != C_ID_UNDERLYING_INVALID)
                {
                    cast_type = c_parse_pointer_chain(result, preprocess, cast_type, &type_index, type_end);
                    if (cast_type.value != C_ID_UNDERLYING_INVALID && type_index == type_end)
                    {
                        expression_index = cast_close;
                        continue;
                    }
                }
            }
        }
        if (token.kind == C_TOKEN_IDENTIFIER && (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("sizeof")) || c_parse_alignof_word(c_token_spelling(preprocess.spelling_base, token))) &&
            expression_index + 2 < expression_count && c_token_is_punctuator(&preprocess.tokens[start + 3 + expression_index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 type_start = start + 4 + expression_index;
            u32 type_end = type_start;
            u32 type_depth = 1;
            while (type_end < expression_end && type_depth)
            {
                CToken type_token = preprocess.tokens[type_end];
                if (c_token_is_punctuator(&type_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    type_depth += 1;
                }
                else if (c_token_is_punctuator(&type_token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    type_depth -= 1;
                    if (!type_depth)
                    {
                        break;
                    }
                }
                type_end += 1;
            }
            if (type_depth || type_end == type_start)
            {
                return false;
            }
            // `sizeof (T){...}` sizes the compound literal, not the type name
            // the parenthesis closes: the size is the same either way, but the
            // initializer has to be consumed here or it stays in the
            // retokenized integer expression and fails to evaluate. A postfix
            // suffix on the literal is left unclaimed, so it still reports
            // "not an integer constant expression" instead of folding the
            // whole literal's size.
            u32 literal_close = UINT32_MAX;
            if (type_end + 1 < expression_end && c_token_is_punctuator(&preprocess.tokens[type_end + 1], C_PUNCTUATOR_LEFT_BRACE))
            {
                u32 initializer_close = c_parse_matching_delimiter(preprocess, type_end + 1, expression_end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE);
                literal_close = initializer_close < expression_end ? initializer_close : UINT32_MAX;
            }
            u32 type_index = type_start;
            // The machineless half already resolves in the assertion's scope;
            // the machine half has to as well, or a block-local typedef
            // shadowing an outer name of the same spelling sizes the outer
            // type.
            CTypeId type = machine ? c_parse_scalar_type_in_scope(machine, result, preprocess, scope, type_start, type_end, &type_index)
                                   : c_parse_machineless_base_type(result, preprocess, scope, type_start, type_end, &type_index);
            if (type.value != C_ID_UNDERLYING_INVALID)
            {
                type = c_parse_pointer_chain(result, preprocess, type, &type_index, type_end);
                type = c_parse_array_suffixes(result, preprocess, type, &type_index, type_end);
            }
            u64 size = 0;
            u32 alignment = 0;
            bool have_layout = false;
            if ((type.value == C_ID_UNDERLYING_INVALID || type_index != type_end) && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("sizeof")))
            {
                if (machine)
                {
                    type = C_TYPE_ID_INVALID;
                    c_parse_sizeof_expression_type(machine, arena, preprocess, result, scope, type_start, type_end, &type);
                }
                else
                {
                    // A machineless caller cannot enter the type-parse
                    // machine for a sizeof over a full expression; the
                    // operand-layout walk answers directly instead.
                    have_layout = c_parse_machineless_sizeof_operand_layout(arena, result, preprocess, scope, type_start, type_end, &size, &alignment);
                    if (!have_layout)
                    {
                        return false;
                    }
                }
                type_index = type_end;
            }
            if (!have_layout && (type.value == C_ID_UNDERLYING_INVALID || type_index != type_end ||
                                 !c_parse_type_layout(machine, arena, preprocess, result, type, &size, &alignment)))
            {
                return false;
            }
            bool evaluation_word_is_alignof = c_parse_alignof_word(c_token_spelling(preprocess.spelling_base, token));
            token = c_space_token(&evaluation_space, string_format(arena, S8("{u64}"), evaluation_word_is_alignof ? alignment : size),
                                  C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
            expression_index = (literal_close != UINT32_MAX ? literal_close : type_end) - (start + 2);
            tokens[token_count++] = token;
            continue;
        }
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            CEntityId enumerator_id = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);
            CEntity* constant = enumerator_id.value < result->entity_count &&
                                        (result->entities[enumerator_id.value].kind == C_ENTITY_ENUMERATOR ||
                                         (result->entities[enumerator_id.value].is_constexpr && result->entities[enumerator_id.value].has_constant_value))
                                    ? &result->entities[enumerator_id.value]
                                    : 0;
            if (!constant)
            {
                return false;
            }
            if (constant->constant_is_negative)
            {
                tokens[token_count++] = c_space_token(&evaluation_space, S8("-"), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_MINUS);
            }
            tokens[token_count++] = c_space_token(&evaluation_space, string_format(arena, S8("{u64}"), constant->constant_value),
                                                  C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
            continue;
        }
        tokens[token_count++] = c_space_retoken(&evaluation_space, preprocess.spelling_base, token);
    }
    CPreprocessResult evaluation = {
        .diagnostics = arena_allocate(arena, CDiagnostic, token_count + 1),
        .target = preprocess.target,
        .dialect = preprocess.dialect,
    };
    return c_integer_expression_evaluate(arena, evaluation_space.base, tokens, token_count, 65536, &evaluation, value_out) && !evaluation.diagnostic_count;
}

BUSTER_C_INTERNAL bool c_parse_integer_constant_range(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                        CScopeId scope, u32 start, u32 end, u64* value_out)
{
    if (start >= end)
    {
        return false;
    }
    u32 expression_count = end - start;
    CToken* tokens = arena_allocate(arena, CToken, expression_count + 4);
    // The wrapper tokens reference the fixed prelude of the shared spelling
    // space; their recovered locations are the prelude's placeholder, which
    // no diagnostic path reads (the expression tokens keep their own).
    tokens[0] = (CToken){
        .offset = C_SPELLING_STATIC_ASSERT,
        .length = C_SPELLING_STATIC_ASSERT_LENGTH,
        .kind = C_TOKEN_IDENTIFIER,
    };
    tokens[1] = (CToken){
        .offset = C_SPELLING_LEFT_PARENTHESIS,
        .length = 1,
        .kind = C_TOKEN_PUNCTUATOR,
        .punctuator = C_PUNCTUATOR_LEFT_PARENTHESIS,
    };
    memcpy(tokens + 2, preprocess.tokens + start, sizeof(*tokens) * expression_count);
    tokens[expression_count + 2] = (CToken){
        .offset = C_SPELLING_RIGHT_PARENTHESIS,
        .length = 1,
        .kind = C_TOKEN_PUNCTUATOR,
        .punctuator = C_PUNCTUATOR_RIGHT_PARENTHESIS,
    };
    tokens[expression_count + 3] = (CToken){
        .offset = C_SPELLING_SEMICOLON,
        .length = 1,
        .kind = C_TOKEN_PUNCTUATOR,
        .punctuator = C_PUNCTUATOR_SEMICOLON,
    };
    CPreprocessResult synthetic = preprocess;
    synthetic.tokens = tokens;
    synthetic.token_count = expression_count + 4;
    String8 ignored_message = {0};
    return c_parse_static_assert_evaluate(machine, arena, synthetic, result,
                                          (CDeclaration){
                                              .token_count = expression_count + 4,
                                          },
                                          scope, value_out, &ignored_message);
}

BUSTER_C_INTERNAL bool c_parse_static_assert_has_unresolved_array(CPreprocessResult preprocess, CParseResult* result, CDeclaration declaration, CScopeId scope)
{
    u32 start = declaration.token_start + 2;
    u32 end = declaration.token_start + declaration.token_count;
    for (u32 token_index = start; token_index < end; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        if (token.kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        CEntityId entity_id = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);
        if (entity_id.value >= result->entity_count)
        {
            continue;
        }
        CEntity* entity = result->entities + entity_id.value;
        if (entity->kind != C_ENTITY_LOCAL && entity->kind != C_ENTITY_OBJECT)
        {
            continue;
        }
        CTypeId type_id = entity->type;
        while (type_id.value < result->type_count && result->types[type_id.value].has_unqualified_type)
        {
            type_id = result->types[type_id.value].unqualified_type;
        }
        if (type_id.value < result->type_count)
        {
            CType* type = result->types + type_id.value;
            if (type->kind == C_TYPE_ARRAY && type->array_bound < result->array_bound_count &&
                !result->array_bounds[type->array_bound].token_count && !result->array_bounds[type->array_bound].is_star &&
                !result->array_bounds[type->array_bound].has_inferred_count)
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_C_INTERNAL void c_parse_defer_static_assert(CPreprocessResult preprocess, CParseResult* result, CDeclaration declaration, CScopeId scope)
{
    if (!result || result->deferred_static_assert_count >= result->deferred_static_assert_capacity)
    {
        return;
    }
    CSourceLocation location = declaration.location;
    if (declaration.token_start < preprocess.token_count)
    {
        location = c_preprocess_token_location(&preprocess, preprocess.tokens[declaration.token_start]);
    }
    result->deferred_static_asserts[result->deferred_static_assert_count++] = (CDeferredStaticAssert){
        .token_start = declaration.token_start,
        .token_count = declaration.token_count,
        .scope = scope,
        .location = location,
    };
}

BUSTER_C_SHARED void c_parse_static_assert_check(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                     CDeclaration declaration, CScopeId scope)
{
    bool deferred = false;
    for (u32 token_offset = 0; token_offset < declaration.token_count; token_offset += 1)
    {
        CToken token = preprocess.tokens[declaration.token_start + token_offset];
        if (token.kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_offsetof")))
        {
            deferred = true;
            break;
        }
    }
    deferred |= c_parse_static_assert_has_unresolved_array(preprocess, result, declaration, scope);
    if (deferred)
    {
        c_parse_defer_static_assert(preprocess, result, declaration, scope);
        return;
    }
    CToken first = preprocess.tokens[declaration.token_start];
    u64 value = 0;
    String8 message = {0};
    if (!c_parse_static_assert_evaluate(machine, arena, preprocess, result, declaration, scope, &value, &message))
    {
        String8* parts = arena_allocate(arena, String8, declaration.token_count * 2);
        u32 part_count = 0;
        for (u32 token_offset = 2; token_offset < declaration.token_count; token_offset += 1)
        {
            CToken token = preprocess.tokens[declaration.token_start + token_offset];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                break;
            }
            if (part_count)
            {
                parts[part_count++] = S8(" ");
            }
            parts[part_count++] = c_token_spelling(preprocess.spelling_base, token);
        }
        String8 expression = string_join_arena(arena,
                                               (SliceString8){
                                                   .pointer = parts,
                                                   .length = part_count,
                                               },
                                               false);
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, first), C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
                           string_format(arena, S8("static assertion expression is not an integer constant expression: {S8}"), expression));
    }
    else if (!value)
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, first), C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
                           message.length ? string_format(arena, S8("static assertion failed: {S8}"), message) : S8("static assertion failed"));
    }
}

BUSTER_C_SHARED bool c_parse_label_address_prefix_with_typedef(CParseResult* result, CPreprocessResult const* preprocess, CScopeId scope,
                                                                    u32 expression_start, u32 index);

BUSTER_C_SHARED bool c_ir_tokens_are_string_literals(CPreprocessResult preprocess, u32 start, u32 end);

typedef struct CParseInitializerContinuation CParseInitializerContinuation;

BUSTER_C_SHARED bool c_ir_decode_string_literal_range_for_target(Arena* arena, CPreprocessResult preprocess, Target target, u32 start, u32 end,
                                                                     CIrDecodedString* decoded_out);

BUSTER_C_INTERNAL bool c_parse_arena_can_allocate(Arena* arena, u64 size, u64 alignment)
{
    if (!arena || !alignment || !arena->granularity || alignment - 1 > UINT64_MAX - arena->position)
    {
        return false;
    }
    u64 position = (arena->position + alignment - 1) & ~(alignment - 1);
    if (position > arena->reserved_size || size > arena->reserved_size - position)
    {
        return false;
    }
    u64 end = position + size;
    u64 granularity = arena->granularity;
    if (granularity - 1 > UINT64_MAX - end)
    {
        return false;
    }
    u64 committed_end = (end + granularity - 1) & ~(granularity - 1);
    return committed_end <= arena->reserved_size;
}

BUSTER_C_SHARED bool c_parse_result_reserve_types(CParseResult* result, u32 additional)
{
    if (!result || !additional || additional <= result->type_capacity - BUSTER_MIN(result->type_count, result->type_capacity))
    {
        return result != 0;
    }
    u64 required = (u64)result->type_count + additional;
    if (required > UINT32_MAX || !result->arena)
    {
        return false;
    }
    u32 capacity = result->type_capacity ? result->type_capacity : 1;
    while ((u64)capacity < required)
    {
        if (capacity > UINT32_MAX / 2)
        {
            capacity = (u32)required;
            break;
        }
        capacity *= 2;
    }
    u64 allocation_size = (u64)capacity * (u64)sizeof(CType);
    if (!c_parse_arena_can_allocate(result->arena, allocation_size, BUSTER_ALIGN_OF(CType)))
    {
        return false;
    }
    CType* types = (CType*)arena_allocate_bytes(result->arena, allocation_size, BUSTER_ALIGN_OF(CType));
    if (result->types && result->type_count)
    {
        memcpy(types, result->types, sizeof(*types) * result->type_count);
    }
    result->types = types;
    result->type_capacity = capacity;
    return true;
}

BUSTER_C_INTERNAL bool c_parse_result_reserve_array_bounds(CParseResult* result, u32 additional)
{
    if (!result || !additional || additional <= result->array_bound_capacity - BUSTER_MIN(result->array_bound_count, result->array_bound_capacity))
    {
        return result != 0;
    }
    u64 required = (u64)result->array_bound_count + additional;
    if (required > UINT32_MAX || !result->arena)
    {
        return false;
    }
    u32 capacity = result->array_bound_capacity ? result->array_bound_capacity : 1;
    while ((u64)capacity < required)
    {
        if (capacity > UINT32_MAX / 2)
        {
            capacity = (u32)required;
            break;
        }
        capacity *= 2;
    }
    u64 allocation_size = (u64)capacity * (u64)sizeof(CArrayBound);
    if (!c_parse_arena_can_allocate(result->arena, allocation_size, BUSTER_ALIGN_OF(CArrayBound)))
    {
        return false;
    }
    CArrayBound* bounds = (CArrayBound*)arena_allocate_bytes(result->arena, allocation_size, BUSTER_ALIGN_OF(CArrayBound));
    if (result->array_bounds && result->array_bound_count)
    {
        memcpy(bounds, result->array_bounds, sizeof(*bounds) * result->array_bound_count);
    }
    result->array_bounds = bounds;
    result->array_bound_capacity = capacity;
    return true;
}

/* Whether a call through this function type ends control flow because the
   declarator that derived it spelled `noreturn`.  The set is empty in almost
   every translation unit -- the marker normally sits on a function
   declaration, which c_ir_declaration_is_noreturn reads instead -- so the
   count guards the scan and the scan itself is over a handful of ids. */
BUSTER_C_SHARED bool c_parse_type_is_noreturn(CParseResult const* result, CTypeId type)
{
    bool answer = false;
    if (result && type.value != C_ID_UNDERLYING_INVALID)
    {
        for (u32 index = 0; index < result->noreturn_function_type_count && !answer; index += 1)
        {
            answer = result->noreturn_function_types[index].value == type.value;
        }
    }

    return answer;
}

BUSTER_C_SHARED bool c_ir_noreturn_marker_in_range(CPreprocessResult preprocess, u32 start, u32 end);
BUSTER_C_SHARED bool c_ir_declaration_is_noreturn(CPreprocessResult preprocess, CDeclaration declaration);

/* The function type a declarator arrives at, following only the derivations a
   declarator can write around one: pointers, arrays of them, and the
   qualifiers spelled on either.  `allow_direct` admits the declarator whose
   own type is the function -- a typedef of a function type -- while every
   other caller wants the pointer shapes only, because a declaration whose own
   type is the function is what c_ir_declaration_is_noreturn already reads.
   The walk moves to a type built before the one in hand, so it terminates on
   any graph; a chain that does not descend simply stops, which costs a mark
   rather than risking one. */
BUSTER_C_INTERNAL CTypeId c_parse_declarator_function_type(CParseResult const* result, CTypeId type, bool allow_direct)
{
    CTypeId current = type;
    bool derived = false;
    bool walking = current.value < result->type_count;
    while (walking)
    {
        CType const* value = result->types + current.value;
        CTypeId next = value->has_unqualified_type ? value->unqualified_type
                       : (value->kind == C_TYPE_POINTER || value->kind == C_TYPE_ARRAY)
                           ? value->element_type
                           : C_TYPE_ID_INVALID;
        walking = next.value < current.value;
        if (walking)
        {
            derived |= value->kind == C_TYPE_POINTER;
            current = next;
        }
    }
    bool function = (derived || allow_direct) && current.value < result->type_count && result->types[current.value].kind == C_TYPE_FUNCTION;

    return function ? current : C_TYPE_ID_INVALID;
}

/* The function type this declarator derived and could still be marked
   noreturn, or an invalid id when there is none -- which is what lets a
   caller skip the token scan for the marker, the only part of the question
   that is not a handful of loads.  Only a type the declarator itself built --
   one added at or after `derived_start` -- qualifies: a declarator that merely
   names an existing one, as `__attribute__((noreturn)) handler die;` names a
   shared `typedef void handler(int);`, would otherwise make every other
   declaration written with that typedef noreturn too, and the live code after
   a call through one of those would be dropped. */
BUSTER_C_INTERNAL CTypeId c_parse_noreturn_candidate_function_type(CParseResult const* result, CTypeId type, bool allow_direct, u32 derived_start)
{
    CTypeId function = c_parse_declarator_function_type(result, type, allow_direct);
    bool candidate = function.value >= derived_start && function.value < result->type_count && !c_parse_type_is_noreturn(result, function);

    return candidate ? function : C_TYPE_ID_INVALID;
}

/* Note that a call through `function` ends control flow.  The table grows one
   entry at a time from the three declarator sites and is empty in almost every
   translation unit, so it starts unallocated and doubles from four rather than
   being sized up front the way the parse tables every declaration writes to
   are.  Running out of arena drops the note rather than failing the parse: the
   marker only removes unreachable code, so losing one costs the dead code it
   would have deleted and nothing else. */
BUSTER_C_INTERNAL void c_parse_add_noreturn_function_type(CParseResult* result, CTypeId function)
{
    bool room = result->noreturn_function_type_count < result->noreturn_function_type_capacity;
    if (!room && result->arena && result->noreturn_function_type_capacity < UINT32_MAX / 2)
    {
        u32 capacity = result->noreturn_function_type_capacity ? result->noreturn_function_type_capacity * 2 : 4;
        u64 allocation_size = (u64)capacity * (u64)sizeof(CTypeId);
        if (c_parse_arena_can_allocate(result->arena, allocation_size, BUSTER_ALIGN_OF(CTypeId)))
        {
            CTypeId* types = (CTypeId*)arena_allocate_bytes(result->arena, allocation_size, BUSTER_ALIGN_OF(CTypeId));
            if (result->noreturn_function_types && result->noreturn_function_type_count)
            {
                memcpy(types, result->noreturn_function_types, sizeof(*types) * result->noreturn_function_type_count);
            }
            result->noreturn_function_types = types;
            result->noreturn_function_type_capacity = capacity;
            room = true;
        }
    }
    if (room)
    {
        result->noreturn_function_types[result->noreturn_function_type_count++] = function;
    }
}

BUSTER_C_INTERNAL CTypeId c_parse_string_literal_expression_type(Arena* arena, CPreprocessResult preprocess, CParseResult* result, u32 start, u32 end)
{
    CIrDecodedString decoded = {0};
    if (!c_ir_decode_string_literal_range_for_target(arena, preprocess, preprocess.target, start, end, &decoded) || decoded.element_count == UINT64_MAX ||
        !c_parse_result_reserve_array_bounds(result, 1))
    {
        return C_TYPE_ID_INVALID;
    }
    CTypeId element = c_parse_expression_scalar_type(result, decoded.element_kind);
    u32 bound_index = result->array_bound_count++;
    result->array_bounds[bound_index] = (CArrayBound){
        .inferred_count = decoded.element_count + 1,
        .has_inferred_count = true,
    };
    return c_parse_add_type(result, (CType){
                                        .element_type = element,
                                        .return_type = C_TYPE_ID_INVALID,
                                        .unqualified_type = C_TYPE_ID_INVALID,
                                        .array_bound = bound_index,
                                        .kind = C_TYPE_ARRAY,
                                        .is_complete = true,
                                    });
}

typedef struct CParseInitializerInferenceFrame CParseInitializerInferenceFrame;
struct CParseInitializerInferenceFrame
{
    CTypeId type;
    CTypeId element_type;
    u32 cursor;
    u32 limit;
    u64 next_index;
    bool borrowed;
    bool root;
};

typedef struct CParseInitializerInferenceDesignator CParseInitializerInferenceDesignator;
struct CParseInitializerContinuation
{
    CTypeId type;
    u64 next_index;
};

struct CParseInitializerInferenceDesignator
{
    CTypeId value_type;
    u32 value_start;
    u64 selected;
    u64 selected_end;
    CParseInitializerContinuation* continuations;
    u32 continuation_count;
    bool has_designator;
};

BUSTER_C_INTERNAL bool c_parse_initializer_member_is_slot(CMember* member)
{
    return member && !(member->is_bit_field && !member->name.length);
}

BUSTER_C_INTERNAL u32 c_parse_initializer_member_count(CParseResult* result, CType* type)
{
    if (!result || !type || (type->kind != C_TYPE_STRUCT && type->kind != C_TYPE_UNION))
    {
        return 0;
    }
    u32 count = 0;
    for (u32 field_index = 0; field_index < type->member_count; field_index += 1)
    {
        count += c_parse_initializer_member_is_slot(result->members + type->member_start + field_index);
    }
    return type->kind == C_TYPE_UNION ? (count != 0) : count;
}

BUSTER_C_INTERNAL u32 c_parse_initializer_member_slot(CParseResult* result, CType* type, u32 field_index)
{
    if (!result || !type || field_index >= type->member_count ||
        !c_parse_initializer_member_is_slot(result->members + type->member_start + field_index))
    {
        return UINT32_MAX;
    }
    if (type->kind == C_TYPE_UNION)
    {
        return 0;
    }
    u32 slot = 0;
    for (u32 index = 0; index < field_index; index += 1)
    {
        slot += c_parse_initializer_member_is_slot(result->members + type->member_start + index);
    }
    return slot;
}

BUSTER_C_INTERNAL CMember* c_parse_initializer_member_at(CParseResult* result, CType* type, u32 slot)
{
    if (result && type && (type->kind == C_TYPE_STRUCT || type->kind == C_TYPE_UNION))
    {
        u32 current = 0;
        for (u32 field_index = 0; field_index < type->member_count; field_index += 1)
        {
            CMember* member = result->members + type->member_start + field_index;
            if (!c_parse_initializer_member_is_slot(member))
            {
                continue;
            }
            if (current++ == slot || type->kind == C_TYPE_UNION)
            {
                return member;
            }
        }
    }

    return 0;
}

BUSTER_C_INTERNAL bool c_parse_promoted_member_type(CTypeParseMachine* machine, CParseResult* result, CTypeId root, String8 name, CTypeId* type_out,
                                                      u32* root_field_out, bool* ambiguous_out)
{
    if (ambiguous_out)
    {
        *ambiguous_out = false;
    }
    u32 capacity = machine->promoted_member_capacity;
    if (!capacity || result->type_count > capacity || !machine->promoted_member_work || !machine->promoted_member_visited)
    {
        return false;
    }
    CParsePromotedMemberWork* work = machine->promoted_member_work;
    u32* visited = machine->promoted_member_visited;
    machine->promoted_member_generation += 1;
    if (!machine->promoted_member_generation)
    {
        memset(visited, 0, sizeof(*visited) * capacity);
        machine->promoted_member_generation = 1;
    }
    u32 generation = machine->promoted_member_generation;
    u32 work_count = 1;
    u32 work_index = 0;
    bool found = false;
    bool ambiguous = false;
    u32 found_depth = UINT32_MAX;
    work[0] = (CParsePromotedMemberWork){.type = root, .root_field = UINT32_MAX, .depth = 0};
    while (work_index < work_count)
    {
        CParsePromotedMemberWork current = work[work_index++];
        CTypeId type_id = current.type;
        while (type_id.value < result->type_count && result->types[type_id.value].has_unqualified_type)
        {
            type_id = result->types[type_id.value].unqualified_type;
        }
        if (type_id.value >= result->type_count || visited[type_id.value] == generation)
        {
            continue;
        }
        visited[type_id.value] = generation;
        if (found && current.depth > found_depth)
        {
            break;
        }
        CType* type = result->types + type_id.value;
        if (type->kind != C_TYPE_STRUCT && type->kind != C_TYPE_UNION)
        {
            continue;
        }
        for (u32 field_index = 0; field_index < type->member_count; field_index += 1)
        {
            CMember* field = result->members + type->member_start + field_index;
            if (string_equal(field->name, name))
            {
                if (!found)
                {
                    *type_out = field->type;
                    *root_field_out = current.root_field == UINT32_MAX ? field_index : current.root_field;
                    found = true;
                    found_depth = current.depth;
                }
                else if (current.depth == found_depth)
                {
                    ambiguous = true;
                }
                continue;
            }
            if (found && current.depth >= found_depth)
            {
                continue;
            }
            CTypeId child_id = field->type;
            while (child_id.value < result->type_count && result->types[child_id.value].has_unqualified_type)
            {
                child_id = result->types[child_id.value].unqualified_type;
            }
            if (field->name.length || child_id.value >= result->type_count)
            {
                continue;
            }
            CTypeKind child_kind = result->types[child_id.value].kind;
            if ((child_kind != C_TYPE_STRUCT && child_kind != C_TYPE_UNION) || visited[child_id.value] == generation || work_count >= capacity)
            {
                continue;
            }
            work[work_count++] = (CParsePromotedMemberWork){
                .type = child_id,
                .root_field = current.root_field == UINT32_MAX ? field_index : current.root_field,
                .depth = current.depth + 1,
            };
        }
    }
    if (ambiguous_out)
    {
        *ambiguous_out = ambiguous;
    }
    return found && !ambiguous;
}

BUSTER_C_INTERNAL bool c_parse_initializer_type_slots(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                         CScopeId scope, CParseInitializerInferenceFrame* frame, u64* slots_out)
{
    if (frame->root)
    {
        *slots_out = UINT64_MAX;
        return true;
    }
    if (frame->type.value < result->type_count)
    {
        CType* type = result->types + frame->type.value;
        if (type->kind == C_TYPE_ARRAY)
        {
            if (type->array_bound >= result->array_bound_count)
            {
                return false;
            }
            CArrayBound bound = result->array_bounds[type->array_bound];
            u64 count = bound.inferred_count;
            if (!bound.has_inferred_count &&
                (!bound.token_count || !c_parse_integer_constant_range(machine, arena, preprocess, result, scope, bound.token_start,
                                                                         bound.token_start + bound.token_count, &count)))
            {
                return false;
            }
            *slots_out = count;
            return true;
        }
        if (type->kind == C_TYPE_STRUCT || type->kind == C_TYPE_UNION)
        {
            *slots_out = c_parse_initializer_member_count(result, type);
            return true;
        }
    }

    return false;
}

BUSTER_C_INTERNAL bool c_parse_initializer_index(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                    CScopeId scope, u32 start, u32 end, u64* value_out)
{
    // The parser's legacy integer helper predates typed cast evaluation.  Defer
    // any parenthesized index to the canonical IR evaluator so a cast such as
    // (unsigned char)256 cannot be mistaken for 256 during bound inference.
    for (u32 index = start; index < end; index += 1)
    {
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            return false;
        }
    }
    return c_parse_integer_constant_range(machine, arena, preprocess, result, scope, start, end, value_out);
}

BUSTER_C_INTERNAL bool c_parse_initializer_index_range(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                         CScopeId scope, u32 start, u32 end, u64* first_out, u64* last_out, bool* range_out)
{
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    u32 ellipsis = end;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            parentheses += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            parentheses -= parentheses != 0;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            brackets += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
        {
            brackets -= brackets != 0;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            braces += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            braces -= braces != 0;
        }
        else if (!parentheses && !brackets && !braces && c_token_is_punctuator(&token, C_PUNCTUATOR_ELLIPSIS))
        {
            if (ellipsis != end)
            {
                return false;
            }
            ellipsis = index;
        }
    }
    u64 first = 0;
    u64 last = 0;
    bool is_range = ellipsis != end;
    if (!is_range)
    {
        if (!c_parse_initializer_index(machine, arena, preprocess, result, scope, start, end, &first))
        {
            return false;
        }
        last = first;
    }
    else if (ellipsis == start || ellipsis + 1 >= end ||
             !c_parse_initializer_index(machine, arena, preprocess, result, scope, start, ellipsis, &first) ||
             !c_parse_initializer_index(machine, arena, preprocess, result, scope, ellipsis + 1, end, &last) || first > last)
    {
        return false;
    }
    if (first_out) *first_out = first;
    if (last_out) *last_out = last;
    if (range_out) *range_out = is_range;
    return true;
}

BUSTER_C_INTERNAL bool c_parse_initializer_designator(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                         CScopeId scope, CParseInitializerInferenceFrame* frame, u32 start, u32 limit,
                                                         CParseInitializerContinuation* continuation_work, u32 continuation_capacity,
                                                         CParseInitializerInferenceDesignator* designator)
{
    CTypeId current = frame->root ? frame->element_type : frame->type;
    u64 selected = frame->next_index;
    u64 selected_end = selected;
    u32 cursor = start;
    bool first = true;
    bool has_designator = false;
    if (!continuation_work || !continuation_capacity || continuation_capacity > UINT32_MAX / sizeof(CParseInitializerContinuation))
    {
        return false;
    }
    designator->continuations = continuation_work;
    designator->continuation_count = 0;
    while (cursor < limit && (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_BRACKET) ||
                              c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_DOT)))
    {
        if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_BRACKET))
        {
            u32 close = c_parse_matching_delimiter(preprocess, cursor, limit, C_PUNCTUATOR_LEFT_BRACKET, C_PUNCTUATOR_RIGHT_BRACKET);
            u64 index = 0;
            u64 last = 0;
            bool is_range = false;
            if (close >= limit || close == cursor + 1 ||
                !c_parse_initializer_index_range(machine, arena, preprocess, result, scope, cursor + 1, close, &index, &last, &is_range))
            {
                return false;
            }
            if (frame->root && first)
            {
                selected = index;
                selected_end = last;
                current = frame->element_type;
            }
            else
            {
                if (!first)
                {
                    if (designator->continuation_count >= continuation_capacity || last == UINT64_MAX)
                    {
                        return false;
                    }
                    designator->continuations[designator->continuation_count++] = (CParseInitializerContinuation){
                        .type = current,
                        .next_index = last + 1,
                    };
                }
                if (current.value >= result->type_count || result->types[current.value].kind != C_TYPE_ARRAY)
                {
                    return false;
                }
                CParseInitializerInferenceFrame nested = {
                    .type = current,
                };
                u64 slots = 0;
                if (!c_parse_initializer_type_slots(machine, arena, preprocess, result, scope, &nested, &slots) || last >= slots)
                {
                    return false;
                }
                if (first)
                {
                    selected = index;
                    selected_end = last;
                }
                current = result->types[current.value].element_type;
            }
            cursor = close + 1;
        }
        else
        {
            CTypeId container_id = current;
            if (current.value >= result->type_count)
            {
                return false;
            }
            CType* type = result->types + current.value;
            if ((type->kind != C_TYPE_STRUCT && type->kind != C_TYPE_UNION) || cursor + 1 >= limit ||
                preprocess.tokens[cursor + 1].kind != C_TOKEN_IDENTIFIER)
            {
                return false;
            }
            u32 field_index = UINT32_MAX;
            CTypeId member_type = C_TYPE_ID_INVALID;
            bool ambiguous = false;
            if (!c_parse_promoted_member_type(machine, result, current, c_token_spelling(preprocess.spelling_base, preprocess.tokens[cursor + 1]), &member_type, &field_index, &ambiguous))
            {
                if (ambiguous)
                {
                    c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[cursor + 1]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                       S8("ambiguous promoted member designator"));
                }
                return false;
            }
            current = member_type;
            if (first)
            {
                u32 member_slot = c_parse_initializer_member_slot(result, type, field_index);
                if (member_slot == UINT32_MAX)
                {
                    return false;
                }
                if (frame->root)
                {
                    if (designator->continuation_count >= continuation_capacity || member_slot == UINT32_MAX || member_slot == UINT32_MAX - 1)
                    {
                        return false;
                    }
                    designator->continuations[designator->continuation_count++] = (CParseInitializerContinuation){
                        .type = container_id,
                        .next_index = member_slot + 1,
                    };
                }
                else
                {
                    selected = member_slot;
                }
            }
            else
            {
                u32 member_slot = c_parse_initializer_member_slot(result, type, field_index);
                if (member_slot == UINT32_MAX || member_slot == UINT32_MAX - 1 || designator->continuation_count >= continuation_capacity)
                {
                    return false;
                }
                designator->continuations[designator->continuation_count++] = (CParseInitializerContinuation){
                    .type = container_id,
                    .next_index = member_slot + 1,
                };
            }
            cursor += 2;
        }
        has_designator = true;
        first = false;
        if (cursor >= limit || c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_ASSIGN))
        {
            break;
        }
        if (!c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_BRACKET) &&
            !c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_DOT))
        {
            return false;
        }
    }
    if (has_designator)
    {
        if (cursor >= limit || !c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_ASSIGN) || cursor + 1 >= limit)
        {
            return false;
        }
        designator->value_type = current;
        designator->value_start = cursor + 1;
        designator->selected = selected;
        designator->selected_end = selected_end;
        designator->has_designator = true;
        return true;
    }
    if (frame->root)
    {
        designator->value_type = frame->element_type;
    }
    else
    {
        u64 slots = 0;
        if (!c_parse_initializer_type_slots(machine, arena, preprocess, result, scope, frame, &slots) || frame->next_index >= slots ||
            frame->type.value >= result->type_count)
        {
            return false;
        }
        CType* type = result->types + frame->type.value;
        if (type->kind == C_TYPE_ARRAY)
        {
            designator->value_type = type->element_type;
        }
        else
        {
            CMember* member = c_parse_initializer_member_at(result, type, (u32)frame->next_index);
            if (!member)
            {
                return false;
            }
            designator->value_type = member->type;
        }
    }
    designator->value_start = start;
    designator->selected = selected;
    designator->selected_end = selected_end;
    designator->has_designator = false;
    return true;
}

BUSTER_C_INTERNAL u32 c_parse_initializer_value_end(CPreprocessResult preprocess, u32 start, u32 limit)
{
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    for (u32 index = start; index < limit; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            parentheses += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            parentheses -= parentheses != 0;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
            brackets += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
            brackets -= brackets != 0;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            braces += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            braces -= braces != 0;
        else if (!parentheses && !brackets && !braces && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
            return index;
    }
    return limit;
}

BUSTER_C_SHARED bool c_initializer_consume_separator(CToken* tokens, u32 limit, u32* cursor, u64 next_index)
{
    bool result;
    if (!tokens || !cursor || *cursor >= limit || !c_token_is_punctuator(&tokens[*cursor], C_PUNCTUATOR_COMMA))
    {
        result = true;
    }
    else
    {
        *cursor += 1;
        result = next_index != 0 && (*cursor >= limit || !c_token_is_punctuator(&tokens[*cursor], C_PUNCTUATOR_COMMA));
    }

    return result;
}

BUSTER_C_SHARED bool c_initializer_has_top_level_comma(CToken* tokens, u32 start, u32 end)
{
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            parentheses += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            parentheses -= parentheses != 0;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
            brackets += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
            brackets -= brackets != 0;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            braces += 1;
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            braces -= braces != 0;
        else if (!parentheses && !brackets && !braces && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
            return true;
    }
    return false;
}

BUSTER_C_INTERNAL bool c_parse_initializer_value_is_aggregate_expression(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess,
                                                                           CParseResult* result, CScopeId scope, u32 start, u32 end)
{
    CTypeId expression_type = C_TYPE_ID_INVALID;
    if (!c_parse_expression_type_query(machine, arena, preprocess, result, scope, start, end, false, &expression_type) ||
        expression_type.value >= result->type_count)
    {
        return false;
    }
    CTypeKind kind = result->types[expression_type.value].kind;
    return kind == C_TYPE_ARRAY || kind == C_TYPE_STRUCT || kind == C_TYPE_UNION;
}

BUSTER_C_INTERNAL bool c_parse_initializer_string_element_compatible(CPreprocessResult preprocess, CParseResult* result, CTypeId element_type,
                                                                       CIrDecodedString decoded)
{
    while (element_type.value < result->type_count && result->types[element_type.value].has_unqualified_type)
    {
        element_type = result->types[element_type.value].unqualified_type;
    }
    if (element_type.value >= result->type_count)
    {
        return false;
    }
    CTypeKind kind = result->types[element_type.value].kind;
    u64 size = 0;
    u32 alignment = 0;
    if (!c_parse_builtin_type_layout(preprocess.target, kind, &size, &alignment) || size != decoded.element_width)
    {
        return false;
    }
    if (decoded.encoding == C_IR_STRING_ENCODING_ORDINARY || decoded.encoding == C_IR_STRING_ENCODING_UTF8)
    {
        return decoded.element_width == 1 && (kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_UNSIGNED_CHAR);
    }
    return kind == decoded.element_kind;
}

BUSTER_C_INTERNAL bool c_parse_infer_initializer_array_count_core(CTypeParseMachine* machine, Arena* result_arena, Arena* temporary_arena,
                                                                    CPreprocessResult preprocess, CParseResult* result, CScopeId scope,
                                                                    CTypeId element_type, u32 start, u32 end, u64* count_out)
{
    if (start >= end)
    {
        return false;
    }
    if (c_ir_tokens_are_string_literals(preprocess, start, end))
    {
        CIrDecodedString decoded = {0};
        if (!c_ir_decode_string_literal_range_for_target(temporary_arena, preprocess, preprocess.target, start, end, &decoded) ||
            decoded.element_count == UINT64_MAX || !c_parse_initializer_string_element_compatible(preprocess, result, element_type, decoded))
        {
            return false;
        }
        *count_out = decoded.element_count + 1;
        return true;
    }
    if (end <= start + 1 || !c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_BRACE) ||
        !c_token_is_punctuator(&preprocess.tokens[end - 1], C_PUNCTUATOR_RIGHT_BRACE) ||
        c_parse_matching_delimiter(preprocess, start, end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE) != end - 1)
    {
        return false;
    }
    u32 string_start = start + 1;
    u32 string_end = end - 1;
    bool trailing_comma = false;
    if (string_end > string_start && c_token_is_punctuator(&preprocess.tokens[string_end - 1], C_PUNCTUATOR_COMMA))
    {
        trailing_comma = true;
        string_end -= 1;
    }
    if ((trailing_comma && string_start >= string_end) ||
        (string_end > string_start && c_token_is_punctuator(&preprocess.tokens[string_end - 1], C_PUNCTUATOR_COMMA)))
    {
        return false;
    }
    if (string_start < string_end && c_ir_tokens_are_string_literals(preprocess, string_start, string_end))
    {
        CIrDecodedString decoded = {0};
        if (!c_ir_decode_string_literal_range_for_target(temporary_arena, preprocess, preprocess.target, string_start, string_end, &decoded) ||
            decoded.element_count == UINT64_MAX || !c_parse_initializer_string_element_compatible(preprocess, result, element_type, decoded))
        {
            return false;
        }
        *count_out = decoded.element_count + 1;
        return true;
    }
    u64 span = (u64)end - start;
    if (span > UINT32_MAX - 2 || span + 2 > UINT32_MAX / sizeof(CParseInitializerInferenceFrame) ||
        span + 1 > UINT32_MAX / sizeof(CParseInitializerContinuation))
    {
        return false;
    }
    u32 capacity = (u32)(span + 2);
    CParseInitializerInferenceFrame* frames = arena_allocate(temporary_arena, CParseInitializerInferenceFrame, capacity);
    CParseInitializerContinuation* continuation_work = arena_allocate(temporary_arena, CParseInitializerContinuation, (u32)(span + 1));
    u32 frame_count = 1;
    frames[0] = (CParseInitializerInferenceFrame){
        .element_type = element_type,
        .cursor = start + 1,
        .limit = end - 1,
        .root = true,
    };
    u64 count = 0;
    while (frame_count)
    {
        CParseInitializerInferenceFrame* frame = frames + frame_count - 1;
        if (!c_initializer_consume_separator(preprocess.tokens, frame->limit, &frame->cursor, frame->next_index))
        {
            return false;
        }
        bool designated = frame->cursor < frame->limit && (c_token_is_punctuator(&preprocess.tokens[frame->cursor], C_PUNCTUATOR_LEFT_BRACKET) ||
                                                           c_token_is_punctuator(&preprocess.tokens[frame->cursor], C_PUNCTUATOR_DOT));
        if (designated && frame->borrowed)
        {
            u32 designated_cursor = frame->cursor;
            while (frame_count && frames[frame_count - 1].borrowed)
            {
                frame_count -= 1;
                if (frame_count)
                {
                    frames[frame_count - 1].cursor = designated_cursor;
                }
            }
            if (!frame_count)
            {
                return false;
            }
            frame = frames + frame_count - 1;
            if (!c_initializer_consume_separator(preprocess.tokens, frame->limit, &frame->cursor, frame->next_index))
            {
                return false;
            }
            designated = frame->cursor < frame->limit && (c_token_is_punctuator(&preprocess.tokens[frame->cursor], C_PUNCTUATOR_LEFT_BRACKET) ||
                                                          c_token_is_punctuator(&preprocess.tokens[frame->cursor], C_PUNCTUATOR_DOT));
        }
        u64 slots = 0;
        if (!c_parse_initializer_type_slots(machine, result_arena, preprocess, result, scope, frame, &slots))
        {
            return false;
        }
        if (frame->cursor >= frame->limit)
        {
            u32 cursor = frame->cursor;
            bool borrowed = frame->borrowed;
            frame_count -= 1;
            if (borrowed && frame_count) frames[frame_count - 1].cursor = cursor;
            continue;
        }
        if (frame->next_index >= slots && !designated)
        {
            if (frame->borrowed)
            {
                u32 cursor = frame->cursor;
                frame_count -= 1;
                if (frame_count) frames[frame_count - 1].cursor = cursor;
                continue;
            }
            return false;
        }
        CParseInitializerInferenceDesignator designator = {0};
        if (!c_parse_initializer_designator(machine, result_arena, preprocess, result, scope, frame, frame->cursor, frame->limit, continuation_work,
                                             (u32)(span + 1), &designator) ||
            designator.selected_end == UINT64_MAX)
        {
            return false;
        }
        if (frame->root)
        {
            count = BUSTER_MAX(count, designator.selected_end + 1);
        }
        frame->next_index = designator.selected_end + 1;
        if (designator.value_type.value >= result->type_count || designator.value_start >= frame->limit)
        {
            return false;
        }
        CType* value_type = result->types + designator.value_type.value;
        bool aggregate = value_type->kind == C_TYPE_ARRAY || value_type->kind == C_TYPE_STRUCT || value_type->kind == C_TYPE_UNION;
        u32 value_end = c_parse_initializer_value_end(preprocess, designator.value_start, frame->limit);
        if (value_end <= designator.value_start)
        {
            return false;
        }
        if (aggregate && c_token_is_punctuator(&preprocess.tokens[designator.value_start], C_PUNCTUATOR_LEFT_BRACE))
        {
            u32 close = c_parse_matching_delimiter(preprocess, designator.value_start, frame->limit, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE);
            if (close >= frame->limit || frame_count >= capacity)
            {
                return false;
            }
            frame->cursor = close + 1;
            if (frame_count + designator.continuation_count >= capacity)
            {
                return false;
            }
            for (u32 continuation_index = 0; continuation_index < designator.continuation_count; continuation_index += 1)
            {
                CParseInitializerContinuation continuation = designator.continuations[continuation_index];
                frames[frame_count++] = (CParseInitializerInferenceFrame){
                    .type = continuation.type,
                    .cursor = close + 1,
                    .limit = frame->limit,
                    .next_index = continuation.next_index,
                    .borrowed = true,
                };
            }
            frames[frame_count++] = (CParseInitializerInferenceFrame){
                .type = designator.value_type,
                .cursor = designator.value_start + 1,
                .limit = close,
            };
            continue;
        }
        if (aggregate && !c_ir_tokens_are_string_literals(preprocess, designator.value_start, frame->limit) &&
            !c_parse_initializer_value_is_aggregate_expression(machine, result_arena, preprocess, result, scope, designator.value_start, value_end))
        {
            if (frame_count + designator.continuation_count >= capacity)
            {
                return false;
            }
            frame->cursor = value_end;
            for (u32 continuation_index = 0; continuation_index < designator.continuation_count; continuation_index += 1)
            {
                CParseInitializerContinuation continuation = designator.continuations[continuation_index];
                frames[frame_count++] = (CParseInitializerInferenceFrame){
                    .type = continuation.type,
                    .cursor = designator.value_start,
                    .limit = frame->limit,
                    .next_index = continuation.next_index,
                    .borrowed = true,
                };
            }
            frames[frame_count++] = (CParseInitializerInferenceFrame){
                .type = designator.value_type,
                .cursor = designator.value_start,
                .limit = frame->limit,
                .borrowed = true,
            };
            continue;
        }
        frame->cursor = value_end;
        if (frame_count + designator.continuation_count > capacity)
        {
            return false;
        }
        for (u32 continuation_index = 0; continuation_index < designator.continuation_count; continuation_index += 1)
        {
            CParseInitializerContinuation continuation = designator.continuations[continuation_index];
            frames[frame_count++] = (CParseInitializerInferenceFrame){
                .type = continuation.type,
                .cursor = value_end,
                .limit = frame->limit,
                .next_index = continuation.next_index,
                .borrowed = true,
            };
        }
    }
    *count_out = count;
    return count != 0;
}

BUSTER_C_INTERNAL bool c_parse_infer_initializer_array_count(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess,
                                                               CParseResult* result, CScopeId scope, CTypeId element_type, u32 start, u32 end,
                                                               u64* count_out)
{
    TemporalArena temporary = arena_begin_temporal(machine->scratch_arena);
    bool success = c_parse_infer_initializer_array_count_core(machine, arena, temporary.arena, preprocess, result, scope, element_type, start, end,
                                                              count_out);
    scratch_end(temporary);
    return success;
}

BUSTER_C_SHARED void c_parse_infer_file_array_bounds(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result)
{
    for (u32 declaration_index = 0; declaration_index < result->declaration_count; declaration_index += 1)
    {
        CDeclaration declaration = result->declarations[declaration_index];
        if (declaration.kind != C_DECLARATION_OBJECT || declaration.type.value >= result->type_count)
        {
            continue;
        }
        CType type = result->types[declaration.type.value];
        if (type.kind != C_TYPE_ARRAY || type.array_bound >= result->array_bound_count)
        {
            continue;
        }
        u32 bound_index = type.array_bound;
        CArrayBound bound = result->array_bounds[bound_index];
        if (bound.token_count || bound.has_inferred_count)
        {
            continue;
        }
        u32 start = declaration.declarator_count ? declaration.declarator_start : declaration.token_start;
        u32 end = declaration.declarator_count ? declaration.declarator_start + declaration.declarator_count
                                               : declaration.token_start + declaration.token_count;
        u32 initializer = end;
        u32 depth = 0;
        for (u32 index = start; index < end; index += 1)
        {
            CToken token = preprocess.tokens[index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                if (depth)
                {
                    depth -= 1;
                }
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
            {
                initializer = index + 1;
                break;
            }
        }
        if (initializer >= end)
        {
            continue;
        }
        if (end > initializer && c_token_is_punctuator(&preprocess.tokens[end - 1], C_PUNCTUATOR_SEMICOLON))
        {
            end -= 1;
        }
        u64 count = 0;
        if (c_parse_infer_initializer_array_count(machine, arena, preprocess, result, (CScopeId){.value = 0}, type.element_type, initializer, end, &count) &&
            bound_index < result->array_bound_count)
        {
            result->array_bounds[bound_index].inferred_count = count;
            result->array_bounds[bound_index].has_inferred_count = true;
        }
    }
}

BUSTER_C_INTERNAL void c_parse_aggregate_lookup_insert(CParseResult* result, CTypeId id)
{
    CAggregateLookup* lookup = result->aggregate_lookup;
    CType* type = &result->types[id.value];
    if (lookup && !lookup->saturated && type->tag.length)
    {
        u32 mask = lookup->slot_count - 1;
        u32 slot_index = (u32)(c_macro_name_hash(type->tag) & mask) ^ ((u32)type->kind & mask);
        CAggregateLookupSlot* slot = lookup->slots + slot_index;
        while (slot->used && (slot->kind != (u32)type->kind || !string_equal(slot->tag, type->tag)))
        {
            slot_index = (slot_index + 1) & mask;
            slot = lookup->slots + slot_index;
        }
        if (slot->used)
        {
            // Keep the recorded id only while it still names an older live type
            // with this key; a rolled-back id is replaced by the new one.
            u32 existing = slot->type_index;
            if (existing < id.value && result->types[existing].kind == type->kind && string_equal(result->types[existing].tag, type->tag))
            {
                return;
            }
            slot->type_index = id.value;
            return;
        }
        if (lookup->fill >= lookup->slot_count / 2)
        {
            lookup->saturated = true;
            return;
        }
        lookup->fill += 1;
        slot->used = true;
        slot->tag = type->tag;
        slot->kind = (u32)type->kind;
        slot->type_index = id.value;
    }
}

// The variable-argument list type, created once per translation unit. A libc
// spells the same builtin under several typedef names -- musl declares
// `va_list` in <stdarg.h> and `__isoc_va_list` in the public prototypes, both
// as `__builtin_va_list` -- and a prototype written with one name has to stay
// compatible with a definition written with the other, so all of them have to
// land on one type id rather than on a fresh one each.
BUSTER_C_INTERNAL CTypeId c_parse_variable_argument_list_type(CParseResult* result)
{
    for (u32 index = 0; index < result->type_count; index += 1)
    {
        if (result->types[index].kind == C_TYPE_VA_LIST)
        {
            return (CTypeId){.value = index};
        }
    }
    return c_parse_add_type(result, (CType){
                                        .element_type = C_TYPE_ID_INVALID,
                                        .return_type = C_TYPE_ID_INVALID,
                                        .array_bound = C_ARRAY_BOUND_INVALID,
                                        .kind = C_TYPE_VA_LIST,
                                        .is_complete = true,
                                    });
}

// The typedef names that introduce the variable-argument list type. The set is
// closed rather than "any typedef of __builtin_va_list" because the underlying
// spelling is what these names are declared from, and matching on the name is
// what keeps a library's own alias out of the builtin's identity.
BUSTER_C_INTERNAL bool c_parse_variable_argument_list_name(String8 name)
{
    return string_equal(name, S8("va_list")) || string_equal(name, S8("__gnuc_va_list")) || string_equal(name, S8("__builtin_va_list")) ||
           string_equal(name, S8("__isoc_va_list"));
}

BUSTER_C_SHARED CTypeId c_parse_add_type(CParseResult* result, CType type)
{
    if (!c_parse_result_reserve_types(result, 1))
    {
        return C_TYPE_ID_INVALID;
    }
    CTypeId id = {
        .value = result->type_count,
    };
    result->types[result->type_count++] = type;
    c_parse_aggregate_lookup_insert(result, id);
    return id;
}

BUSTER_C_SHARED bool c_parse_clone_incomplete_array_declarator(CTypeParseMachine* machine, CParseResult* result, CTypeId type, CTypeId* type_out)
{
    if (!machine || !result || !type_out || type.value >= result->type_count)
    {
        return false;
    }
    u32 capacity = machine->incomplete_array_chain_capacity;
    CTypeId* chain = machine->incomplete_array_chain;
    if (!capacity || !chain)
    {
        return false;
    }
    u32 chain_count = 0;
    bool incomplete = false;
    CTypeId current = type;
    while (current.value < result->type_count && result->types[current.value].kind == C_TYPE_ARRAY)
    {
        if (chain_count >= capacity)
        {
            return false;
        }
        chain[chain_count++] = current;
        CType* array = result->types + current.value;
        if (c_parse_type_is_incomplete_array(result, current))
        {
            incomplete = true;
        }
        current = array->element_type;
    }
    if (!incomplete)
    {
        *type_out = type;
        return true;
    }
    u32 incomplete_count = 0;
    for (u32 index = 0; index < chain_count; index += 1)
    {
        CType* array = result->types + chain[index].value;
        incomplete_count += array->array_bound < result->array_bound_count && c_parse_type_is_incomplete_array(result, chain[index]);
    }
    if (!c_parse_result_reserve_types(result, chain_count) || !c_parse_result_reserve_array_bounds(result, incomplete_count))
    {
        return false;
    }
    CTypeId cloned_element = current;
    for (u32 reverse = chain_count; reverse; reverse -= 1)
    {
        CType original = result->types[chain[reverse - 1].value];
        CType copy = original;
        copy.element_type = cloned_element;
        if (c_parse_type_is_incomplete_array(result, chain[reverse - 1]))
        {
            u32 bound_index = result->array_bound_count++;
            result->array_bounds[bound_index] = result->array_bounds[original.array_bound];
            result->array_bounds[bound_index].has_inferred_count = false;
            result->array_bounds[bound_index].inferred_count = 0;
            copy.array_bound = bound_index;
        }
        cloned_element = c_parse_add_type(result, copy);
        if (cloned_element.value == C_ID_UNDERLYING_INVALID)
        {
            return false;
        }
    }
    *type_out = cloned_element;
    return true;
}

BUSTER_C_SHARED CTypeId c_parse_add_qualified_type(CParseResult* result, CTypeId base, CType qualifiers)
{
    if (base.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CType qualified = result->types[base.value];
    qualified.is_const |= qualifiers.is_const;
    qualified.is_volatile |= qualifiers.is_volatile;
    qualified.is_restrict |= qualifiers.is_restrict;
    qualified.is_atomic |= qualifiers.is_atomic;
    qualified.unqualified_type = qualified.has_unqualified_type ? qualified.unqualified_type : base;
    qualified.has_unqualified_type = true;
    return c_parse_add_type(result, qualified);
}

BUSTER_C_INTERNAL CTypeId c_parse_unqualified_type(CParseResult* result, CTypeId type_id)
{
    if (type_id.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CType type = result->types[type_id.value];
    if (type.has_unqualified_type && type.unqualified_type.value < result->type_count)
    {
        return type.unqualified_type;
    }
    if (!type.is_const && !type.is_volatile && !type.is_restrict && !type.is_atomic)
    {
        return type_id;
    }
    type.is_const = false;
    type.is_volatile = false;
    type.is_restrict = false;
    type.is_atomic = false;
    type.unqualified_type = C_TYPE_ID_INVALID;
    type.has_unqualified_type = false;
    return c_parse_add_type(result, type);
}

BUSTER_C_INTERNAL bool c_parse_type_word(String8 spelling)
{
    switch (spelling.length)
    {
    case 3:
    {
        return string_equal(spelling, S8("int"));
    }
    case 4:
    {
        return string_equal(spelling, S8("void")) || string_equal(spelling, S8("char")) || string_equal(spelling, S8("long")) ||
               string_equal(spelling, S8("auto")) || string_equal(spelling, S8("enum"));
    }
    case 5:
    {
        return string_equal(spelling, S8("_Bool")) || string_equal(spelling, S8("short")) || string_equal(spelling, S8("float")) ||
               string_equal(spelling, S8("const")) || string_equal(spelling, S8("union"));
    }
    case 6:
    {
        return string_equal(spelling, S8("signed")) || string_equal(spelling, S8("double")) || string_equal(spelling, S8("extern")) ||
               string_equal(spelling, S8("static")) || string_equal(spelling, S8("inline")) || string_equal(spelling, S8("struct"));
    }
    case 7:
    {
        return string_equal(spelling, S8("_Atomic")) || string_equal(spelling, S8("typedef")) || string_equal(spelling, S8("__const"));
    }
    case 8:
    {
        return string_equal(spelling, S8("_Alignas")) || string_equal(spelling, S8("unsigned")) || string_equal(spelling, S8("volatile")) ||
               string_equal(spelling, S8("restrict")) || string_equal(spelling, S8("register")) || string_equal(spelling, S8("__thread")) ||
               string_equal(spelling, S8("__inline")) || string_equal(spelling, S8("__signed")) || string_equal(spelling, S8("__int128")) ||
               string_equal(spelling, S8("__typeof")) || string_equal(spelling, S8("_Complex"));
    }
    case 9:
    {
        return string_equal(spelling, S8("_Noreturn")) || string_equal(spelling, S8("__const__")) || string_equal(spelling, S8("__complex"));
    }
    case 10:
    {
        return string_equal(spelling, S8("__inline__")) || string_equal(spelling, S8("__typeof__")) || string_equal(spelling, S8("__volatile")) ||
               string_equal(spelling, S8("__restrict")) || string_equal(spelling, S8("__signed__")) || string_equal(spelling, S8("_Imaginary"));
    }
    case 11:
    {
        return string_equal(spelling, S8("__complex__"));
    }
    case 12:
    {
        return string_equal(spelling, S8("__volatile__")) || string_equal(spelling, S8("__restrict__"));
    }
    case 13:
    {
        return string_equal(spelling, S8("_Thread_local")) || string_equal(spelling, S8("__extension__"));
    }
    default:
    {
        return false;
    }
    }
}

BUSTER_C_SHARED bool c_parse_type_word_for_dialect(String8 spelling, CPreprocessDialect dialect)
{
    return c_parse_type_word(spelling) ||
           (c_preprocess_dialect_is_gnu(dialect) && string_equal(spelling, S8("__auto_type"))) ||
           ((c_preprocess_dialect_is_gnu(dialect) || c_preprocess_dialect_is_c23(dialect)) && string_equal(spelling, S8("typeof"))) ||
           (c_preprocess_dialect_is_c23(dialect) && (string_equal(spelling, S8("constexpr")) || string_equal(spelling, S8("typeof_unqual"))));
}

BUSTER_C_SHARED bool c_parse_type_qualifier_word(String8 spelling, CType* type)
{
    if (string_equal(spelling, S8("const")) || string_equal(spelling, S8("__const")) || string_equal(spelling, S8("__const__")))
    {
        type->is_const = true;
        return true;
    }
    if (string_equal(spelling, S8("volatile")) || string_equal(spelling, S8("__volatile")) || string_equal(spelling, S8("__volatile__")))
    {
        type->is_volatile = true;
        return true;
    }
    if (string_equal(spelling, S8("restrict")) || string_equal(spelling, S8("__restrict")) || string_equal(spelling, S8("__restrict__")))
    {
        type->is_restrict = true;
        return true;
    }
    if (string_equal(spelling, S8("_Atomic")))
    {
        type->is_atomic = true;
        return true;
    }
    return false;
}

BUSTER_C_SHARED u32 c_parse_skip_attributes(CPreprocessResult preprocess, u32 index, u32 end);

// Skips the run of `_Alignas ( ... )` alignment specifiers at `index`. An
// alignment specifier belongs to the declaration specifiers, so every scan
// that walks the specifier prefix looking for the type — a builtin keyword, a
// typedef name, or a struct/union/enum tag — has to step over it to reach the
// declarator. `c_parse_alignment_specifiers` is what actually collects and
// evaluates the alignments; this only moves past them.
//
// Returns `index` unchanged when there is no specifier here, or when the
// specifier is malformed (no `(`, or unbalanced parentheses). Callers treat
// that as "the scan cannot advance" and stop, which both avoids spinning and
// leaves the malformed specifier for the collector to diagnose.
BUSTER_C_INTERNAL u32 c_parse_skip_alignment_specifiers(CPreprocessResult preprocess, u32 index, u32 end)
{
    while (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER &&
           string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("_Alignas")))
    {
        if (index + 2 >= end || !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            break;
        }
        u32 depth = 1;
        u32 scan = index + 2;
        while (scan < end && depth)
        {
            if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                depth -= 1;
            }
            scan += 1;
        }
        if (depth)
        {
            break;
        }
        index = scan;
    }
    return index;
}

// The three spellings of the complex specifier: the C99 keyword and the two
// GNU aliases. `_Imaginary` is deliberately not here -- it is recognized as a
// type word so the specifier scans can refuse a declaration that uses it,
// never accept one.
BUSTER_C_INTERNAL bool c_parse_complex_specifier_word(String8 spelling)
{
    return string_equal(spelling, S8("_Complex")) || string_equal(spelling, S8("__complex")) || string_equal(spelling, S8("__complex__"));
}

// Which complex kind a specifier set names, or C_TYPE_INVALID when the set is
// one this frontend refuses: a complex integer (the GNU `_Complex int`
// extension) or a combination C does not define. Shared by every specifier
// scan so the three of them cannot answer differently.
BUSTER_C_INTERNAL CTypeKind c_parse_complex_kind(bool seen_float, bool seen_double, bool seen_bool, bool seen_char, bool seen_short, bool seen_int,
                                                   bool seen_signed, bool seen_unsigned, bool seen_int128, u32 long_count)
{
    if (seen_bool || seen_char || seen_short || seen_int || seen_signed || seen_unsigned || seen_int128)
    {
        return C_TYPE_INVALID;
    }
    if (seen_float)
    {
        return long_count || seen_double ? C_TYPE_INVALID : C_TYPE_FLOAT_COMPLEX;
    }
    if (long_count > 1)
    {
        return C_TYPE_INVALID;
    }
    // `_Complex` on its own is `double _Complex`; `long _Complex` alone is not
    // a type, so a `long` here has to be paired with `double`.
    return long_count ? (seen_double ? C_TYPE_LONG_DOUBLE_COMPLEX : C_TYPE_INVALID) : C_TYPE_DOUBLE_COMPLEX;
}

BUSTER_C_INTERNAL CTypeId c_parse_primitive_type(CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end, u32* declarator_start)
{
    bool seen_type = false;
    bool seen_void = false;
    bool seen_bool = false;
    bool seen_char = false;
    bool seen_short = false;
    bool seen_int = false;
    bool seen_signed = false;
    bool seen_unsigned = false;
    bool seen_float = false;
    bool seen_double = false;
    bool seen_int128 = false;
    bool seen_complex = false;
    bool seen_imaginary = false;
    u32 long_count = 0;
    CType type = {
        .element_type = C_TYPE_ID_INVALID,
        .return_type = C_TYPE_ID_INVALID,
        .array_bound = C_ARRAY_BOUND_INVALID,
    };
    u32 index = start;
    while (index < end)
    {
        u32 alignment_end = c_parse_skip_alignment_specifiers(preprocess, index, end);
        if (alignment_end != index)
        {
            index = alignment_end;
            continue;
        }
        // A malformed `_Alignas` leaves the index where it was; stop the scan
        // there rather than falling through and treating `_Alignas` as the
        // type word it is spelled like.
        if (preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("_Alignas")))
        {
            break;
        }
        u32 attribute_end = c_parse_skip_attributes(preprocess, index, end);
        if (attribute_end != index)
        {
            index = attribute_end;
            continue;
        }
        CToken token = preprocess.tokens[index];
        if (token.kind != C_TOKEN_IDENTIFIER || !c_parse_type_word_for_dialect_token(preprocess, token))
        {
            break;
        }
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        if (string_equal(spelling, S8("void")))
        {
            seen_void = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("_Bool")))
        {
            seen_bool = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("char")))
        {
            seen_char = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("short")))
        {
            seen_short = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("int")))
        {
            seen_int = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("__int128")))
        {
            seen_int128 = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("signed")) || string_equal(spelling, S8("__signed")) || string_equal(spelling, S8("__signed__")))
        {
            seen_signed = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("unsigned")))
        {
            seen_unsigned = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("long")))
        {
            long_count += 1;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("float")))
        {
            seen_float = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("double")))
        {
            seen_double = true;
            seen_type = true;
        }
        else if (c_parse_complex_specifier_word(spelling))
        {
            seen_complex = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("_Imaginary")))
        {
            seen_imaginary = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("const")) || string_equal(spelling, S8("__const")) || string_equal(spelling, S8("__const__")))
        {
            type.is_const = true;
        }
        else if (string_equal(spelling, S8("volatile")) || string_equal(spelling, S8("__volatile")) || string_equal(spelling, S8("__volatile__")))
        {
            type.is_volatile = true;
        }
        else if (string_equal(spelling, S8("restrict")) || string_equal(spelling, S8("__restrict")) || string_equal(spelling, S8("__restrict__")))
        {
            type.is_restrict = true;
        }
        else if (string_equal(spelling, S8("_Atomic")))
        {
            type.is_atomic = true;
        }
        index += 1;
    }
    *declarator_start = index;
    if (!seen_type || (seen_signed && seen_unsigned))
    {
        return C_TYPE_ID_INVALID;
    }
    // `_Imaginary` is recognized only so that a declaration spelled with it
    // is rejected here instead of parsed as an implicit int with a stray
    // identifier; no target this compiler emits for has imaginary types.
    if (seen_imaginary)
    {
        return C_TYPE_ID_INVALID;
    }
    if (seen_complex)
    {
        // `_Complex` alone is `double _Complex`, which is what GCC and Clang
        // both accept; a complex integer type is a GNU extension this
        // frontend does not implement, so it refuses rather than dropping
        // the specifier.
        type.kind = c_parse_complex_kind(seen_float, seen_double, seen_bool, seen_char, seen_short, seen_int, seen_signed, seen_unsigned, seen_int128,
                                         long_count);
        if (type.kind == C_TYPE_INVALID)
        {
            return C_TYPE_ID_INVALID;
        }
        return c_parse_add_type(result, type);
    }
    if (seen_void)
    {
        type.kind = C_TYPE_VOID;
    }
    else if (seen_bool)
    {
        type.kind = C_TYPE_BOOL;
    }
    else if (seen_char)
    {
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_CHAR : seen_signed ? C_TYPE_SIGNED_CHAR : C_TYPE_CHAR;
    }
    else if (seen_float)
    {
        type.kind = C_TYPE_FLOAT;
    }
    else if (seen_double)
    {
        type.kind = long_count ? C_TYPE_LONG_DOUBLE : C_TYPE_DOUBLE;
    }
    else if (seen_short)
    {
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_SHORT : C_TYPE_SHORT;
    }
    else if (seen_int128)
    {
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_INT128 : C_TYPE_INT128;
    }
    else if (long_count >= 2)
    {
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_LONG_LONG : C_TYPE_LONG_LONG;
    }
    else if (long_count == 1)
    {
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_LONG : C_TYPE_LONG;
    }
    else
    {
        BUSTER_UNUSED(seen_int);
        type.kind = seen_unsigned ? C_TYPE_UNSIGNED_INT : C_TYPE_INT;
    }
    return c_parse_add_type(result, type);
}

BUSTER_C_SHARED CTypeId c_parse_pointer_chain(CParseResult* result, CPreprocessResult preprocess, CTypeId base, u32* index, u32 end);

BUSTER_C_SHARED CTypeId c_parse_array_suffixes(CParseResult* result, CPreprocessResult preprocess, CTypeId element_type, u32* index, u32 end);

BUSTER_C_INTERNAL CTypeId c_parse_aggregate_lookup(CParseResult* result, CTypeKind kind, String8 tag)
{
    if (tag.length)
    {
        CAggregateLookup* lookup = result->aggregate_lookup;
        if (lookup)
        {
            u32 mask = lookup->slot_count - 1;
            u32 slot_index = (u32)(c_macro_name_hash(tag) & mask) ^ ((u32)kind & mask);
            CAggregateLookupSlot* slot = lookup->slots + slot_index;
            bool stale = false;
            while (slot->used)
            {
                if (slot->kind == (u32)kind && string_equal(slot->tag, tag))
                {
                    u32 type_index = slot->type_index;
                    if (type_index < result->type_count && result->types[type_index].kind == kind && string_equal(result->types[type_index].tag, tag))
                    {
                        return (CTypeId){
                            .value = type_index,
                        };
                    }
                    stale = true;
                    break;
                }
                slot_index = (slot_index + 1) & mask;
                slot = lookup->slots + slot_index;
            }
            if (!stale && !lookup->saturated)
            {
                return C_TYPE_ID_INVALID;
            }
        }
        for (u32 type_index = 0; type_index < result->type_count; type_index += 1)
        {
            CType* type = &result->types[type_index];
            if (type->kind == kind && string_equal(type->tag, tag))
            {
                return (CTypeId){
                    .value = type_index,
                };
            }
        }
    }

    return C_TYPE_ID_INVALID;
}

// True when `index` starts a GNU assembler label: the keyword, a parenthesis, at
// least one string literal, and the matching close. The shape is checked rather
// than assumed because the same keyword also introduces an assembly statement,
// which carries qualifiers, colons, or operands the label form never has;
// `after_out` receives the token past the group.
BUSTER_C_INTERNAL bool c_parse_asm_label_at(CPreprocessResult preprocess, u32 index, u32 end, u32* after_out)
{
    if (index + 2 >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
        !c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[index], C_PARSE_ASM_KEYWORDS) ||
        !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 scan = index + 2;
    while (scan < end && preprocess.tokens[scan].kind == C_TOKEN_STRING_LITERAL)
    {
        scan += 1;
    }
    if (scan == index + 2 || scan >= end || !c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        return false;
    }
    *after_out = scan + 1;
    return true;
}

// True when `index` starts a C23 attribute specifier `[[ ... ]]`, with
// `after_out` receiving the token past the closing `]]`. Two consecutive `[`
// cannot begin anything else in C — a subscript needs an expression and an
// array declarator a bound, a qualifier or `static` — so the opening pair
// alone decides it, in every dialect: the syntax is a C23 addition, but
// accepting it in the earlier `-std` modes is what clang does and what
// system headers that spell it unconditionally need. The contents are a
// balanced token sequence (`[[deprecated("use g")]]`, and brackets of its
// own), so the scan closes on a `]]` at bracket depth zero rather than on the
// first one it meets. An unterminated list reports the whole remaining range
// as consumed, which is what leaves the caller looking at `end` and failing
// the declaration instead of reading attribute text as a declarator.
BUSTER_C_SHARED bool c_parse_c23_attribute_at(CPreprocessResult preprocess, u32 index, u32 end, u32* after_out)
{
    if (index + 1 >= end || !c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACKET) ||
        !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_BRACKET))
    {
        return false;
    }
    u32 depth = 0;
    for (u32 scan = index + 2; scan < end; scan += 1)
    {
        if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_LEFT_BRACKET))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_BRACKET))
        {
            if (depth)
            {
                depth -= 1;
            }
            else if (scan + 1 < end && c_token_is_punctuator(&preprocess.tokens[scan + 1], C_PUNCTUATOR_RIGHT_BRACKET))
            {
                *after_out = scan + 2;
                return true;
            }
        }
    }
    *after_out = end;
    return true;
}

BUSTER_C_SHARED u32 c_parse_skip_attributes(CPreprocessResult preprocess, u32 index, u32 end)
{
    for (;;)
    {
        u32 attribute_end = 0;
        if (c_parse_c23_attribute_at(preprocess, index, end, &attribute_end))
        {
            index = attribute_end;
            continue;
        }
        // Every decoration this skips is spelled as an identifier, so the one
        // shared guard answers for all three of them and the token is loaded
        // once instead of once per candidate.
        if (index >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER)
        {
            break;
        }
        CToken token = preprocess.tokens[index];
        if (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__extension__")))
        {
            index += 1;
            continue;
        }
        u32 asm_label_end = 0;
        if (c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ASM_KEYWORDS) &&
            c_parse_asm_label_at(preprocess, index, end, &asm_label_end))
        {
            index = asm_label_end;
            continue;
        }
        bool attribute = string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__attribute__")) ||
                         string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__attribute")) ||
                         string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__declspec"));
        if (!attribute || index + 1 >= end || !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            break;
        }
        index += 1;
        u32 depth = 0;
        do
        {
            if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                depth -= 1;
            }
            index += 1;
        } while (index < end && depth);
    }
    return index;
}

// The index at which the run of attribute lists a declarator ends with begins,
// or `end` when it ends with something else. `(*f)(int) __attribute__((aligned(8)))`
// writes the list after the whole derivation, where a declarator parser that
// requires the declarator to end exactly at the range it is given reads it as a
// suffix it does not know and fails. Handing that parser the trimmed range
// leaves the list to the caller, which already walks it.
//
// The range is walked forwards because an attribute list is only recognizable
// from its head; a list that is not trailing -- a leading one, or one written
// between a group and its parameter list -- is hopped over rather than
// returned, so only a run that reaches `end` is trimmed. The walk is bounded
// by the caller's own comma boundary, so a sibling declarator's list is never
// taken for this one's.
BUSTER_C_INTERNAL u32 c_parse_trailing_attribute_start(CPreprocessResult preprocess, u32 start, u32 end)
{
    u32 index = start;
    while (index < end)
    {
        u32 after = c_parse_skip_attributes(preprocess, index, end);
        if (after == index)
        {
            index += 1;
            continue;
        }
        if (after == end)
        {
            return index;
        }
        index = after;
    }
    return end;
}

// The three aggregate introducers as one membership test, for the specifier
// scans that ask "does an aggregate head start here" per identifier token.
#define C_PARSE_AGGREGATE_KEYWORDS (C_SYMBOL_WELL_KNOWN_BIT(STRUCT) | C_SYMBOL_WELL_KNOWN_BIT(UNION) | C_SYMBOL_WELL_KNOWN_BIT(ENUM))

// True when `index` starts an aggregate definition -- the keyword, an optional
// tag, and a brace-enclosed body. `end_out` receives the index of the closing
// brace. The members inside that body are declarations, so every scan that
// classifies identifiers as uses has to step over the whole group.
BUSTER_C_INTERNAL bool c_parse_aggregate_definition_at(CPreprocessResult preprocess, u32 index, u32 end, u32* end_out)
{
    if (index + 1 >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
        !c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[index], C_PARSE_AGGREGATE_KEYWORDS))
    {
        return false;
    }
    u32 open = index + 1;
    if (open < end && preprocess.tokens[open].kind == C_TOKEN_IDENTIFIER)
    {
        open += 1;
    }
    if (open >= end || !c_token_is_punctuator(&preprocess.tokens[open], C_PUNCTUATOR_LEFT_BRACE))
    {
        return false;
    }
    u32 depth = 0;
    for (u32 scan = open; scan < end; scan += 1)
    {
        if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_LEFT_BRACE))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_BRACE))
        {
            depth -= 1;
            if (!depth)
            {
                *end_out = scan;
                return true;
            }
        }
    }
    return false;
}

typedef struct CCleanupAttributeInfo CCleanupAttributeInfo;
struct CCleanupAttributeInfo
{
    u32 count;
    u32 first_start;
    u32 first_end;
    u32 last_end;
    u32 function_token;
    bool malformed;
};

BUSTER_C_INTERNAL bool c_parse_cleanup_attribute_at(CPreprocessResult preprocess, u32 index, u32 end, CCleanupAttributeInfo* result)
{
    *result = (CCleanupAttributeInfo){
        .first_start = UINT32_MAX,
        .first_end = UINT32_MAX,
        .last_end = UINT32_MAX,
        .function_token = UINT32_MAX,
    };
    if (index + 1 >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
        !c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[index], C_PARSE_ATTRIBUTE_KEYWORDS) ||
        !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 group_open = index + 1;
    u32 group_close = UINT32_MAX;
    u32 depth = 0;
    for (u32 cursor = group_open; cursor < end; cursor += 1)
    {
        if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (!depth)
            {
                break;
            }
            depth -= 1;
            if (!depth)
            {
                group_close = cursor;
                break;
            }
        }
    }
    u32 payload_start = group_open + 1;
    u32 payload_end = group_close < end ? group_close : end;
    if (payload_start < payload_end && c_token_is_punctuator(&preprocess.tokens[payload_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 nested_depth = 0;
        u32 nested_close = UINT32_MAX;
        for (u32 cursor = payload_start; cursor < payload_end; cursor += 1)
        {
            if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                nested_depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                if (!nested_depth)
                {
                    break;
                }
                nested_depth -= 1;
                if (!nested_depth)
                {
                    nested_close = cursor;
                    break;
                }
            }
        }
        if (nested_close != UINT32_MAX && nested_close + 1 == payload_end)
        {
            payload_start += 1;
            payload_end = nested_close;
        }
    }
    u32 segment_start = payload_start;
    depth = 0;
    for (u32 cursor = payload_start; cursor <= payload_end; cursor += 1)
    {
        bool separator = cursor == payload_end || (!depth && c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_COMMA));
        if (separator)
        {
            if (segment_start < cursor && preprocess.tokens[segment_start].kind == C_TOKEN_IDENTIFIER &&
                (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]), S8("cleanup")) ||
                 string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]), S8("__cleanup__"))))
            {
                result->count += 1;
                if (result->count == 1)
                {
                    result->first_start = index;
                    result->first_end = group_close < end ? group_close + 1 : end;
                }
                else
                {
                    result->malformed = true;
                }
                result->last_end = group_close < end ? group_close + 1 : end;
                bool valid = segment_start + 1 < cursor && c_token_is_punctuator(&preprocess.tokens[segment_start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
                u32 cleanup_close = UINT32_MAX;
                if (valid)
                {
                    u32 cleanup_depth = 0;
                    for (u32 cleanup_index = segment_start + 1; cleanup_index < cursor; cleanup_index += 1)
                    {
                        if (c_token_is_punctuator(&preprocess.tokens[cleanup_index], C_PUNCTUATOR_LEFT_PARENTHESIS))
                        {
                            cleanup_depth += 1;
                        }
                        else if (c_token_is_punctuator(&preprocess.tokens[cleanup_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                        {
                            if (!cleanup_depth)
                            {
                                valid = false;
                                break;
                            }
                            cleanup_depth -= 1;
                            if (!cleanup_depth)
                            {
                                cleanup_close = cleanup_index;
                                break;
                            }
                        }
                    }
                    valid = valid && cleanup_close == cursor - 1 && cleanup_close == segment_start + 3 &&
                            preprocess.tokens[segment_start + 2].kind == C_TOKEN_IDENTIFIER;
                }
                if (!valid)
                {
                    result->malformed = true;
                }
                else if (result->count == 1)
                {
                    result->function_token = segment_start + 2;
                }
            }
            segment_start = cursor + 1;
            continue;
        }
        if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_PARENTHESIS) ||
            c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_BRACKET))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
                 c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_RIGHT_BRACKET))
        {
            depth -= depth != 0;
        }
    }
    return result->count != 0;
}

BUSTER_C_INTERNAL void c_parse_cleanup_attribute_scan(CPreprocessResult preprocess, u32 start, u32 end, bool skip_braces,
                                                         CCleanupAttributeInfo* result)
{
    *result = (CCleanupAttributeInfo){
        .first_start = UINT32_MAX,
        .first_end = UINT32_MAX,
        .last_end = UINT32_MAX,
        .function_token = UINT32_MAX,
    };
    u32 braces = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_IDENTIFIER && c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ATTRIBUTE_KEYWORDS) &&
            (!skip_braces || !braces))
        {
            CCleanupAttributeInfo attribute = {0};
            if (c_parse_cleanup_attribute_at(preprocess, index, end, &attribute))
            {
                if (!result->count)
                {
                    result->first_start = attribute.first_start;
                    result->first_end = attribute.first_end;
                    result->function_token = attribute.function_token;
                }
                result->last_end = attribute.last_end;
                result->count += attribute.count;
                result->malformed |= attribute.malformed;
                if (result->count > 1)
                {
                    result->malformed = true;
                }
            }
            u32 attribute_end = attribute.first_end;
            if (attribute_end > index && attribute_end <= end)
            {
                index = attribute_end - 1;
                continue;
            }
        }
        if (skip_braces && c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            braces += 1;
        }
        else if (skip_braces && c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            braces -= braces != 0;
        }
    }
}

// One GNU attribute list, decomposed: `payload_start`/`payload_end` bound the
// comma-separated attribute names inside the double parentheses and
// `group_end` lands one past the whole `__attribute__((...))` group.
typedef struct CAttributeGroup CAttributeGroup;
struct CAttributeGroup
{
    u32 payload_start;
    u32 payload_end;
    u32 group_end;
};

// Decomposes the attribute list at `index`, or answers false when no group
// starts there. The inner parentheses are unwrapped only when they close the
// payload exactly, which is how `__attribute__((packed))` and the single-paren
// `__attribute((packed))` spelling both reduce to the same payload range.
BUSTER_C_INTERNAL bool c_parse_attribute_group_at(CPreprocessResult preprocess, u32 index, u32 end, CAttributeGroup* group)
{
    if (index + 1 >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
        !c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[index], C_PARSE_ATTRIBUTE_KEYWORDS) ||
        !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 group_open = index + 1;
    u32 group_close = UINT32_MAX;
    u32 depth = 0;
    for (u32 cursor = group_open; cursor < end; cursor += 1)
    {
        if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (!depth)
            {
                break;
            }
            depth -= 1;
            if (!depth)
            {
                group_close = cursor;
                break;
            }
        }
    }
    if (group_close == UINT32_MAX)
    {
        return false;
    }
    u32 payload_start = group_open + 1;
    u32 payload_end = group_close;
    if (payload_start < payload_end && c_token_is_punctuator(&preprocess.tokens[payload_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 nested_close = c_parse_matching_delimiter(preprocess, payload_start, payload_end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        if (nested_close + 1 == payload_end)
        {
            payload_start += 1;
            payload_end = nested_close;
        }
    }
    *group = (CAttributeGroup){
        .payload_start = payload_start,
        .payload_end = payload_end,
        .group_end = group_close + 1,
    };
    return true;
}

BUSTER_C_INTERNAL bool c_parse_packed_word(String8 spelling)
{
    return string_equal(spelling, S8("packed")) || string_equal(spelling, S8("__packed")) || string_equal(spelling, S8("__packed__"));
}

// Collects the layout-bearing GNU attributes of [start, end): `packed`, and
// every `aligned(N)` whose argument tokens are appended to the result's
// alignment run. `*alignment_count` counts the records appended starting at
// `*alignment_start`; a caller that scans two ranges back to back gets one
// contiguous run, because this is the only writer of that array between them.
// Either answer may be declined by passing a null pointer for it: a null
// `alignment_start` asks for `packed` alone and appends nothing, which is what
// a caller whose range a separate alignment scan already owns wants, and a
// null `is_packed` asks for the alignment records alone, which is what an
// object declarator wants -- `packed` decides an aggregate's member placement
// and means nothing on a variable.
//
// The walk steps over whole attribute groups rather than looking for the two
// spellings anywhere in the range: a declarator carries parenthesized token
// runs of its own -- a function-pointer parameter list, an array bound -- and
// a loose identifier scan reads an attribute written inside one of those as
// belonging to the declaration. Brace-enclosed bodies are skipped for the same
// reason, so a nested aggregate's own attributes stay its own.
BUSTER_C_INTERNAL void c_parse_layout_attributes(CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end, bool* is_packed,
                                                    u32* alignment_start, u32* alignment_count)
{
    bool record = alignment_start != 0;
    if (record)
    {
        *alignment_start = result->alignment_count;
        *alignment_count = 0;
    }
    // The outer walk runs over every declarator and every aggregate member of
    // the translation unit and asks one question per token, so the token kind
    // is read once and the two answers -- brace nesting and "does an attribute
    // list start here" -- branch off that single load.
    u32 braces = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_PUNCTUATOR)
        {
            braces += token.punctuator == C_PUNCTUATOR_LEFT_BRACE;
            braces -= braces != 0 && token.punctuator == C_PUNCTUATOR_RIGHT_BRACE;
            continue;
        }
        CAttributeGroup group = {0};
        if (braces || token.kind != C_TOKEN_IDENTIFIER || !c_parse_attribute_group_at(preprocess, index, end, &group))
        {
            continue;
        }
        u32 segment_start = group.payload_start;
        u32 depth = 0;
        for (u32 cursor = group.payload_start; cursor <= group.payload_end; cursor += 1)
        {
            bool separator = cursor == group.payload_end || (!depth && c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_COMMA));
            if (!separator)
            {
                if (c_punctuator_in_set(preprocess.tokens[cursor].punctuator, C_PUNCTUATOR_SET_DELIMITER_OPEN))
                {
                    depth += 1;
                }
                else if (c_punctuator_in_set(preprocess.tokens[cursor].punctuator, C_PUNCTUATOR_SET_DELIMITER_CLOSE))
                {
                    depth -= depth != 0;
                }
                continue;
            }
            if (segment_start < cursor && preprocess.tokens[segment_start].kind == C_TOKEN_IDENTIFIER)
            {
                String8 name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]);
                if (is_packed && c_parse_packed_word(name))
                {
                    *is_packed = true;
                }
                else if (record && c_parse_alignment_word(name) && segment_start + 3 < cursor &&
                         c_token_is_punctuator(&preprocess.tokens[segment_start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                         c_token_is_punctuator(&preprocess.tokens[cursor - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS) &&
                         result->alignment_count < result->alignment_capacity)
                {
                    result->alignments[result->alignment_count++] = (CAlignmentSpecifier){
                        .type = C_TYPE_ID_INVALID,
                        .token_start = segment_start + 2,
                        .token_count = cursor - 1 - (segment_start + 2),
                    };
                    *alignment_count += 1;
                }
            }
            segment_start = cursor + 1;
        }
        index = group.group_end - 1;
    }
}

CAggregateAttributes c_parse_aggregate_attributes(CParseResult const* result, CTypeId type)
{
    CAggregateAttributes attributes = {0};
    if (result && type.value < result->type_count)
    {
        for (u32 index = 0; index < result->aggregate_attribute_count; index += 1)
        {
            if (result->aggregate_attributes[index].type_index == type.value)
            {
                attributes = result->aggregate_attributes[index];
                break;
            }
        }
    }
    return attributes;
}

BUSTER_C_SHARED CTypeKind c_ir_primitive_type_kind(CPreprocessResult preprocess, u32 start, u32 end, u32* declarator_start)
{
    bool seen_type = false;
    bool seen_void = false;
    bool seen_bool = false;
    bool seen_char = false;
    bool seen_short = false;
    bool seen_int = false;
    bool seen_signed = false;
    bool seen_unsigned = false;
    bool seen_float = false;
    bool seen_double = false;
    bool seen_int128 = false;
    bool seen_complex = false;
    bool seen_imaginary = false;
    u32 long_count = 0;
    u32 index = start;
    while (index < end)
    {
        u32 alignment_end = c_parse_skip_alignment_specifiers(preprocess, index, end);
        if (alignment_end != index)
        {
            index = alignment_end;
            continue;
        }
        // A malformed `_Alignas` leaves the index where it was; stop the scan
        // there rather than falling through and treating `_Alignas` as the
        // type word it is spelled like.
        if (preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("_Alignas")))
        {
            break;
        }
        u32 attribute_end = c_parse_skip_attributes(preprocess, index, end);
        if (attribute_end != index)
        {
            index = attribute_end;
            continue;
        }
        CToken token = preprocess.tokens[index];
        if (token.kind != C_TOKEN_IDENTIFIER || !c_parse_type_word_for_dialect_token(preprocess, token))
        {
            break;
        }
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        if (string_equal(spelling, S8("void")))
        {
            seen_void = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("_Bool")))
        {
            seen_bool = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("char")))
        {
            seen_char = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("short")))
        {
            seen_short = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("int")))
        {
            seen_int = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("__int128")))
        {
            seen_int128 = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("signed")) || string_equal(spelling, S8("__signed")) || string_equal(spelling, S8("__signed__")))
        {
            seen_signed = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("unsigned")))
        {
            seen_unsigned = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("long")))
        {
            long_count += 1;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("float")))
        {
            seen_float = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("double")))
        {
            seen_double = true;
            seen_type = true;
        }
        else if (c_parse_complex_specifier_word(spelling))
        {
            seen_complex = true;
            seen_type = true;
        }
        else if (string_equal(spelling, S8("_Imaginary")))
        {
            seen_imaginary = true;
            seen_type = true;
        }
        index += 1;
    }
    *declarator_start = index;
    CTypeKind result;
    if (!seen_type || (seen_signed && seen_unsigned) || seen_imaginary)
    {
        result = C_TYPE_INVALID;
    }
    else if (seen_complex)
    {
        result = c_parse_complex_kind(seen_float, seen_double, seen_bool, seen_char, seen_short, seen_int, seen_signed, seen_unsigned, seen_int128, long_count);
    }
    else if (seen_void)
    {
        result = C_TYPE_VOID;
    }
    else if (seen_bool)
    {
        result = C_TYPE_BOOL;
    }
    else if (seen_char)
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_CHAR : seen_signed ? C_TYPE_SIGNED_CHAR : C_TYPE_CHAR;
    }
    else if (seen_float)
    {
        result = C_TYPE_FLOAT;
    }
    else if (seen_double)
    {
        result = long_count ? C_TYPE_LONG_DOUBLE : C_TYPE_DOUBLE;
    }
    else if (seen_short)
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_SHORT : C_TYPE_SHORT;
    }
    else if (seen_int128)
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_INT128 : C_TYPE_INT128;
    }
    else if (long_count >= 2)
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_LONG_LONG : C_TYPE_LONG_LONG;
    }
    else if (long_count == 1)
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_LONG : C_TYPE_LONG;
    }
    else
    {
        result = seen_unsigned ? C_TYPE_UNSIGNED_INT : C_TYPE_INT;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_parse_attribute_unsigned(String8 spelling, u32* value_out)
{
    u32 base = 10;
    u64 index = 0;
    if (spelling.length > 2 && spelling.pointer[0] == '0' && (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X'))
    {
        base = 16;
        index = 2;
    }
    u32 value = 0;
    bool any = false;
    for (; index < spelling.length; index += 1)
    {
        char8 character = spelling.pointer[index];
        u32 digit = character >= '0' && character <= '9'   ? (u32)(character - '0')
                    : character >= 'a' && character <= 'f' ? (u32)(character - 'a') + 10
                    : character >= 'A' && character <= 'F' ? (u32)(character - 'A') + 10
                                                           : UINT32_MAX;
        if (digit >= base)
        {
            break;
        }
        if (value > (UINT32_MAX - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
        any = true;
    }
    while (index < spelling.length &&
           (spelling.pointer[index] == 'u' || spelling.pointer[index] == 'U' || spelling.pointer[index] == 'l' || spelling.pointer[index] == 'L'))
    {
        index += 1;
    }
    if (!any || index != spelling.length)
    {
        return false;
    }
    *value_out = value;
    return true;
}

// The byte count of one `vector_size ( ... )` occurrence, with `index` at the
// attribute spelling. A lone integer literal resolves without the constant
// machinery; anything else — `16 * sizeof(float)` is the shape GCC's own
// documentation uses — evaluates as an integer constant expression. Callers
// include type-parse machine steps, which cannot reenter the machine, so the
// evaluation always takes the machineless path.
BUSTER_C_INTERNAL bool c_parse_vector_size_argument(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 index, u32 end, u32* value_out)
{
    if (index + 3 >= end || !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    if (preprocess.tokens[index + 2].kind == C_TOKEN_PREPROCESSING_NUMBER &&
        c_token_is_punctuator(&preprocess.tokens[index + 3], C_PUNCTUATOR_RIGHT_PARENTHESIS) &&
        c_parse_attribute_unsigned(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index + 2]), value_out))
    {
        return true;
    }
    u32 close = c_parse_matching_delimiter(preprocess, index + 1, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
    bool result_value = false;
    if (close < end && close > index + 2)
    {
        TemporalArena temporary = scratch_begin(&result->arena, 1);
        u64 value = 0;
        if (c_parse_integer_constant_range(0, temporary.arena, preprocess, result, scope, index + 2, close, &value) && value && value <= UINT32_MAX)
        {
            *value_out = (u32)value;
            result_value = true;
        }
        scratch_end(temporary);
    }
    return result_value;
}

BUSTER_C_INTERNAL CTypeId c_parse_apply_vector_attribute(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, CTypeId base, u32 start,
                                                           u32 end)
{
    u32 vector_byte_size = 0;
    if (result->position_index && !result->position_index->built)
    {
        c_parse_position_index_build(result, preprocess);
    }
    if (result->position_index)
    {
        u32 cursor = start;
        while (cursor + 3 < end)
        {
            u32 index =
                c_parse_first_position_in_range(result->position_index->vector_size_positions, result->position_index->vector_size_count, cursor, end - 3);
            if (index == UINT32_MAX)
            {
                break;
            }
            if (c_parse_vector_size_argument(result, preprocess, scope, index, end, &vector_byte_size))
            {
                break;
            }
            vector_byte_size = 0;
            cursor = index + 1;
        }
    }
    else
    {
        for (u32 index = start; index + 3 < end; index += 1)
        {
            if (preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
                !(c_parse_token_class_compute(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index])) & C_TOKEN_CLASS_VECTOR_SIZE) ||
                !c_parse_vector_size_argument(result, preprocess, scope, index, end, &vector_byte_size))
            {
                continue;
            }
            break;
        }
    }
    if (!vector_byte_size)
    {
        return base;
    }
    if (base.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CTypeKind base_kind = result->types[base.value].kind;
    bool arithmetic = (base_kind >= C_TYPE_CHAR && base_kind <= C_TYPE_UNSIGNED_INT128) || base_kind == C_TYPE_FLOAT || base_kind == C_TYPE_DOUBLE;
    if (!arithmetic)
    {
        return C_TYPE_ID_INVALID;
    }
    return c_parse_add_type(result, (CType){
                                        .element_type = base,
                                        .return_type = C_TYPE_ID_INVALID,
                                        .array_bound = C_ARRAY_BOUND_INVALID,
                                        .vector_byte_size = vector_byte_size,
                                        .kind = C_TYPE_VECTOR,
                                        .is_complete = true,
                                    });
}

BUSTER_C_SHARED CEntityId c_parse_lookup_typedef_name(CParseResult* result, String8 name, bool oldest);

BUSTER_C_INTERNAL CTypeId c_parse_qualified_typedef_type(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 start, u32 end,
                                                           u32* declarator_start)
{
    CType qualifiers = {
        .element_type = C_TYPE_ID_INVALID,
        .return_type = C_TYPE_ID_INVALID,
        .array_bound = C_ARRAY_BOUND_INVALID,
    };
    bool has_qualifier = false;
    u32 typedef_index = start;
    while (typedef_index < end && preprocess.tokens[typedef_index].kind == C_TOKEN_IDENTIFIER)
    {
        u32 alignment_end = c_parse_skip_alignment_specifiers(preprocess, typedef_index, end);
        if (alignment_end != typedef_index)
        {
            typedef_index = c_parse_skip_attributes(preprocess, alignment_end, end);
            continue;
        }
        if (c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[typedef_index], &qualifiers))
        {
            has_qualifier = true;
        }
        else if (!c_parse_atomic_declaration_prefix_token(preprocess, preprocess.tokens[typedef_index], &qualifiers))
        {
            break;
        }
        typedef_index += 1;
        typedef_index = c_parse_skip_attributes(preprocess, typedef_index, end);
    }
    if (typedef_index >= end)
    {
        return C_TYPE_ID_INVALID;
    }
    CEntityId typedef_entity = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &preprocess.tokens[typedef_index]);
    CTypeId type = C_TYPE_ID_INVALID;
    if (typedef_entity.value < result->entity_count && result->entities[typedef_entity.value].kind == C_ENTITY_TYPEDEF)
    {
        type = result->entities[typedef_entity.value].type;
    }
    else if (typedef_entity.value == C_ID_UNDERLYING_INVALID)
    {
        typedef_entity = c_parse_lookup_typedef_name(result, c_token_spelling(preprocess.spelling_base, preprocess.tokens[typedef_index]), true);
        if (typedef_entity.value < result->entity_count && result->entities[typedef_entity.value].kind == C_ENTITY_TYPEDEF)
        {
            type = result->entities[typedef_entity.value].type;
        }
    }
    if (type.value != C_ID_UNDERLYING_INVALID)
    {
        u32 qualifier_index = typedef_index + 1;
        while (qualifier_index < end && preprocess.tokens[qualifier_index].kind == C_TOKEN_IDENTIFIER)
        {
            u32 alignment_end = c_parse_skip_alignment_specifiers(preprocess, qualifier_index, end);
            if (alignment_end != qualifier_index)
            {
                qualifier_index = c_parse_skip_attributes(preprocess, alignment_end, end);
                continue;
            }
            if (c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[qualifier_index], &qualifiers))
            {
                has_qualifier = true;
            }
            else if (!c_parse_atomic_declaration_prefix_token(preprocess, preprocess.tokens[qualifier_index], &qualifiers))
            {
                break;
            }
            qualifier_index += 1;
            qualifier_index = c_parse_skip_attributes(preprocess, qualifier_index, end);
        }
        *declarator_start = qualifier_index;
        if (has_qualifier)
        {
            type = c_parse_add_qualified_type(result, type, qualifiers);
        }
    }

    return type;
}


BUSTER_C_INTERNAL CTypeId c_parse_scalar_type_core_begin(CTypeParseMachine* machine, CTypeParseFrame* frame, u32* declarator_start);

// The dialect-independent storage/function-specifier words the atomic
// declaration prefix accepts besides the qualifiers; split out so
// c_parse_word_bits_compute can derive C_WORD_STORAGE_PREFIX from the same
// ladder the spelling path runs.
BUSTER_C_INTERNAL bool c_parse_storage_prefix_word(String8 spelling)
{
    return string_equal(spelling, S8("auto")) || string_equal(spelling, S8("extern")) || string_equal(spelling, S8("inline")) ||
           string_equal(spelling, S8("__inline")) || string_equal(spelling, S8("__inline__")) || string_equal(spelling, S8("_Noreturn")) ||
           string_equal(spelling, S8("register")) || string_equal(spelling, S8("static")) || string_equal(spelling, S8("typedef")) ||
           string_equal(spelling, S8("_Thread_local")) || string_equal(spelling, S8("__thread")) || string_equal(spelling, S8("__extension__"));
}

// The single source of truth for word_bits: every bit is derived from the
// spelling predicate the corresponding `_token` variant would otherwise
// fall back to. The qualifier probe reads the answer back out of a scratch
// CType because c_parse_type_qualifier_word reports which word matched by
// setting the matching flag.
BUSTER_C_SHARED u16 c_parse_word_bits_compute(String8 spelling)
{
    u16 bits = 0;
    if (c_parse_type_word(spelling))
    {
        bits |= C_WORD_TYPE;
    }
    if (c_parse_auto_type_word(spelling))
    {
        bits |= C_WORD_AUTO_TYPE;
    }
    if (string_equal(spelling, S8("typeof")))
    {
        bits |= C_WORD_TYPEOF;
    }
    if (string_equal(spelling, S8("constexpr")))
    {
        bits |= C_WORD_CONSTEXPR;
    }
    if (string_equal(spelling, S8("typeof_unqual")))
    {
        bits |= C_WORD_TYPEOF_UNQUAL;
    }
    CType qualifier_probe = {0};
    if (c_parse_type_qualifier_word(spelling, &qualifier_probe))
    {
        bits |= qualifier_probe.is_const ? C_WORD_QUALIFIER_CONST : 0;
        bits |= qualifier_probe.is_volatile ? C_WORD_QUALIFIER_VOLATILE : 0;
        bits |= qualifier_probe.is_restrict ? C_WORD_QUALIFIER_RESTRICT : 0;
        bits |= qualifier_probe.is_atomic ? C_WORD_QUALIFIER_ATOMIC : 0;
    }
    if (c_parse_storage_prefix_word(spelling))
    {
        bits |= C_WORD_STORAGE_PREFIX;
    }
    return bits;
}

BUSTER_C_INTERNAL u16 c_parse_word_bits_token(CPreprocessResult preprocess, CToken token)
{
    u16 bits;
    if (token.symbol && preprocess.symbols)
    {
        bits = token.symbol <= preprocess.symbols->predefined_limit ? preprocess.symbols->word_bits[token.symbol] : 0;
    }
    else
    {
        bits = c_parse_word_bits_compute(c_token_spelling(preprocess.spelling_base, token));
    }
    return bits;
}

BUSTER_C_INTERNAL bool c_parse_type_word_for_dialect_token(CPreprocessResult preprocess, CToken token)
{
    u16 mask = C_WORD_TYPE;
    if (c_preprocess_dialect_is_gnu(preprocess.dialect))
    {
        mask |= C_WORD_AUTO_TYPE | C_WORD_TYPEOF;
    }
    if (c_preprocess_dialect_is_c23(preprocess.dialect))
    {
        mask |= C_WORD_TYPEOF | C_WORD_CONSTEXPR | C_WORD_TYPEOF_UNQUAL;
    }
    return (c_parse_word_bits_token(preprocess, token) & mask) != 0;
}

BUSTER_C_INTERNAL bool c_parse_auto_type_word_token(CPreprocessResult preprocess, CToken token)
{
    return (c_parse_word_bits_token(preprocess, token) & C_WORD_AUTO_TYPE) != 0;
}

// Sets the matching qualifier flags exactly as the spelling form does: at
// most one qualifier bit is set per word, so at most one flag is written.
BUSTER_C_INTERNAL bool c_parse_qualifier_bits_apply(u16 bits, CType* type)
{
    u16 qualifier_bits = bits & C_WORD_QUALIFIER_ANY;
    if (qualifier_bits & C_WORD_QUALIFIER_CONST)
    {
        type->is_const = true;
    }
    if (qualifier_bits & C_WORD_QUALIFIER_VOLATILE)
    {
        type->is_volatile = true;
    }
    if (qualifier_bits & C_WORD_QUALIFIER_RESTRICT)
    {
        type->is_restrict = true;
    }
    if (qualifier_bits & C_WORD_QUALIFIER_ATOMIC)
    {
        type->is_atomic = true;
    }
    return qualifier_bits != 0;
}

BUSTER_C_INTERNAL bool c_parse_type_qualifier_word_token(CPreprocessResult preprocess, CToken token, CType* type)
{
    return c_parse_qualifier_bits_apply(c_parse_word_bits_token(preprocess, token), type);
}

BUSTER_C_INTERNAL bool c_parse_atomic_declaration_prefix_token(CPreprocessResult preprocess, CToken token, CType* qualifiers)
{
    u16 bits = c_parse_word_bits_token(preprocess, token);
    u16 mask = C_WORD_QUALIFIER_ANY | C_WORD_STORAGE_PREFIX;
    if (c_preprocess_dialect_is_c23(preprocess.dialect))
    {
        mask |= C_WORD_CONSTEXPR;
    }
    c_parse_qualifier_bits_apply(bits, qualifiers);
    return (bits & mask) != 0;
}

BUSTER_C_INTERNAL void c_type_parse_alignment_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        frame->alignment_start = result->alignment_count;
        frame->alignment_count = 0;
        frame->index = frame->start;
        frame->depth = 0;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    else if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        CTypeId type = machine->result_valid ? machine->result_type : C_TYPE_ID_INVALID;
        u32 type_index = machine->result_index;
        if (type.value != C_ID_UNDERLYING_INVALID)
        {
            type = c_parse_pointer_chain(result, preprocess, type, &type_index, frame->close);
            type = c_parse_array_suffixes(result, preprocess, type, &type_index, frame->close);
        }
        if (type_index != frame->close)
        {
            type = C_TYPE_ID_INVALID;
        }
        result->alignments[result->alignment_count++] = (CAlignmentSpecifier){
            .type = type,
            .token_start = frame->specifier_index + 2,
            .token_count = frame->close - (frame->specifier_index + 2),
        };
        frame->alignment_count += 1;
        frame->index = frame->close + 1;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    while (frame->index < frame->end)
    {
        u32 index = frame->index;
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
        {
            frame->depth += 1;
            frame->index += 1;
            continue;
        }
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_BRACE))
        {
            frame->depth -= frame->depth != 0;
            frame->index += 1;
            continue;
        }
        if (frame->depth || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER || !c_parse_alignment_word(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index])))
        {
            frame->index += 1;
            continue;
        }
        if (index + 2 >= frame->end || !c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->alignment_start, false);
            return;
        }
        u32 close = index + 2;
        u32 depth = 1;
        while (close < frame->end && depth)
        {
            if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                depth -= 1;
                if (!depth)
                {
                    break;
                }
            }
            close += 1;
        }
        if (depth || close == index + 2 || result->alignment_count >= result->alignment_capacity)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->alignment_start, false);
            return;
        }
        frame->specifier_index = index;
        frame->close = close;
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = preprocess,
                                                  .scope = result->scope_count ? (CScopeId){.value = 0} : C_SCOPE_ID_INVALID,
                                                  .start = index + 2,
                                                  .end = close,
                                                  .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                              }))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->alignment_start, false);
        }
        return;
    }
    c_type_parse_frame_complete(machine, (CTypeId){.value = frame->alignment_count}, frame->alignment_start, true);
}

BUSTER_C_INTERNAL void c_type_parse_expression_leaf_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        CTypeId type = machine->result_valid ? machine->result_type : C_TYPE_ID_INVALID;
        u32 type_index = machine->result_index;
        if (type.value != C_ID_UNDERLYING_INVALID && type_index < frame->close)
        {
            type = c_parse_pointer_chain(frame->result, frame->preprocess, type, &type_index, frame->close);
            type = c_parse_array_suffixes(frame->result, frame->preprocess, type, &type_index, frame->close);
        }
        if (type.value != C_ID_UNDERLYING_INVALID && type_index == frame->close)
        {
            c_type_parse_frame_complete(machine, type, frame->end, true);
            return;
        }
    }
    else if (frame->start < frame->end && c_token_is_punctuator(&frame->preprocess.tokens[frame->start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 close = c_parse_matching_delimiter(frame->preprocess, frame->start, frame->end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        if (close < frame->end - 1)
        {
            frame->close = close;
            frame->stage = C_TYPE_PARSE_STAGE_CHILD;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = frame->result,
                                                      .preprocess = frame->preprocess,
                                                      .scope = frame->scope,
                                                      .start = frame->start + 1,
                                                      .end = close,
                                                      .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            }
            return;
        }
    }
    CTypeId type = c_parse_expression_leaf_without_cast(frame->arena, frame->preprocess, frame->result, frame->scope, frame->start, frame->end);
    c_type_parse_frame_complete(machine, type, frame->end, type.value != C_ID_UNDERLYING_INVALID);
}

BUSTER_C_INTERNAL void c_type_parse_aggregate_range_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        frame->segment_start = frame->start;
        frame->index = frame->start;
        frame->depth = 0;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    else if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        if (!machine->result_valid)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->segment_start = frame->index + 1;
        frame->index += 1;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    while (frame->index <= frame->end)
    {
        CToken token = frame->preprocess.tokens[frame->index];
        if (frame->index < frame->end && (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) ||
                                          c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE)))
        {
            frame->depth += 1;
            frame->index += 1;
            continue;
        }
        if (frame->index < frame->end && (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
                                          c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE)))
        {
            if (!frame->depth)
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
            frame->depth -= 1;
            frame->index += 1;
            continue;
        }
        bool member_end = frame->index == frame->end || (!frame->depth && c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON));
        if (!member_end)
        {
            frame->index += 1;
            continue;
        }
        if (frame->segment_start != frame->index)
        {
            frame->stage = C_TYPE_PARSE_STAGE_CHILD;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = frame->result,
                                                      .preprocess = frame->preprocess,
                                                      .start = frame->segment_start,
                                                      .end = frame->index,
                                                      .kind = C_TYPE_PARSE_FRAME_AGGREGATE_SEGMENT,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            }
            return;
        }
        frame->segment_start = frame->index + 1;
        frame->index += 1;
    }
    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->end, true);
}

// Narrows an aggregate segment's alignment run to the records one member
// declarator is entitled to. The segment's child alignment frame appends one
// record per `_Alignas`/`aligned` word anywhere in the segment, in token order,
// so the run reads as the specifiers shared by the whole declarator list
// followed by each declarator's own -- and only the shared ones plus this
// declarator's belong to this member. `int a, b __attribute__((aligned(32)));`
// raises `b` alone, exactly as clang lays it out, where handing every member
// the whole run moved `a` and the size of the aggregate with it.
//
// A member row names a *contiguous* run, and the kept records are contiguous
// for the first declarator and for every declarator in a segment where no
// declarator carries a list of its own, which is nearly all of them; only when
// they are not does the run get rebuilt by appending copies. The copies land
// after the segment's own run and inside the segment frame's checkpoint, so
// c_type_parse_rollback drops them with everything else the segment appended.
// The appends are the one place this table can grow past what its
// token-derived capacity was sized for -- shared records are copied once per
// attributed declarator -- so exhausting it fails the segment, which is what
// the alignment frame itself does with the same table.
BUSTER_C_INTERNAL bool c_parse_member_alignment_run(CParseResult* result, u32 segment_start, u32 segment_count, u32 shared_end, u32 declarator_start,
                                                        u32 declarator_end, u32* member_start, u32* member_count)
{
    u32 kept = 0;
    u32 first = 0;
    u32 last = 0;
    for (u32 index = 0; index < segment_count; index += 1)
    {
        u32 token_start = result->alignments[segment_start + index].token_start;
        if (token_start < shared_end || (token_start >= declarator_start && token_start < declarator_end))
        {
            first = kept ? first : index;
            last = index + 1;
            kept += 1;
        }
    }
    bool valid = true;
    if (kept == last - first)
    {
        *member_start = segment_start + first;
        *member_count = kept;
    }
    else if (kept <= result->alignment_capacity - result->alignment_count)
    {
        *member_start = result->alignment_count;
        *member_count = kept;
        for (u32 index = first; index < last; index += 1)
        {
            u32 token_start = result->alignments[segment_start + index].token_start;
            if (token_start < shared_end || (token_start >= declarator_start && token_start < declarator_end))
            {
                result->alignments[result->alignment_count++] = result->alignments[segment_start + index];
            }
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

BUSTER_C_INTERNAL void c_type_parse_aggregate_segment_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        frame->checkpoint = *result;
        frame->mutation_mark = machine->mutation_count;
        // A `packed` attribute anywhere in the segment places every member it
        // declares at byte alignment. The scan runs before the alignment child
        // frame appends anything, and it records no alignment specifiers of
        // its own, so the run that frame owns stays contiguous.
        c_parse_layout_attributes(result, preprocess, frame->start, frame->end, &frame->is_packed, 0, 0);
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = preprocess,
                                                  .start = frame->start,
                                                  .end = frame->end,
                                                  .kind = C_TYPE_PARSE_FRAME_ALIGNMENT,
                                              }))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        }
        return;
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        if (!machine->result_valid)
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->alignment_start = machine->result_index;
        frame->alignment_count = machine->result_type.value;
        u32 type_start = c_parse_skip_attributes(preprocess, frame->start, frame->end);
        if (type_start >= frame->end)
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->first = preprocess.tokens[type_start];
        frame->stage = C_TYPE_PARSE_STAGE_FALLBACK;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = preprocess,
                                                  .scope = result->scope_count ? (CScopeId){.value = 0} : C_SCOPE_ID_INVALID,
                                                  .start = type_start,
                                                  .end = frame->end,
                                                  .mutation_mark = machine->mutation_count,
                                                  .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                              }))
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        }
        return;
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_FALLBACK)
    {
        if (!machine->result_valid)
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->base_type = machine->result_type;
        frame->declarator_start = machine->result_index;
        frame->shared_specifier_end = machine->result_index;
        if (frame->declarator_start == frame->end && frame->base_type.value < result->type_count &&
            (result->types[frame->base_type.value].kind == C_TYPE_STRUCT || result->types[frame->base_type.value].kind == C_TYPE_UNION))
        {
            BUSTER_CHECK(result->member_count < result->member_capacity);
            result->members[result->member_count++] = (CMember){
                .location = c_preprocess_token_location(&frame->preprocess, frame->first),
                .type = frame->base_type,
                .alignment_start = frame->alignment_start,
                .alignment_count = frame->alignment_count,
            };
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->end, true);
            return;
        }
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    u32 declarator = 0;
    CTypeId declarator_type = C_TYPE_ID_INVALID;
    // A zero-length placeholder at the leading token's offset: its spelling
    // reads empty while its location recovers to the declaration head.
    CToken name = {
        .offset = frame->first.offset,
    };
    if (frame->stage == C_TYPE_PARSE_STAGE_PARAMETER_RESULT)
    {
        if (!machine->result_valid)
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        declarator_type = machine->result_type;
        declarator = frame->declarator_end;
        name = frame->name;
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
    else
    {
        while (frame->declarator_start < frame->end)
        {
            frame->declarator_end = frame->declarator_start;
            u32 depth = 0;
            while (frame->declarator_end < frame->end)
            {
                CToken token = preprocess.tokens[frame->declarator_end];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
                {
                    if (!depth)
                    {
                        c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
                        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                        return;
                    }
                    depth -= 1;
                }
                else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
                {
                    break;
                }
                frame->declarator_end += 1;
            }
            if (frame->declarator_start == frame->declarator_end)
            {
                c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
            // The segment head skips the attributes shared by the whole
            // list once, before the base type is parsed, so only the first
            // declarator is covered by it. A declarator after a comma may
            // carry a list of its own -- `struct s { int a, __attribute__((aligned(8))) b; };`
            // -- and reading that list as the declarator fails the segment,
            // which drops every member the aggregate has.
            declarator = c_parse_skip_attributes(preprocess, frame->declarator_start, frame->declarator_end);
            declarator_type = c_parse_pointer_chain(result, preprocess, frame->base_type, &declarator, frame->declarator_end);
            if (declarator < frame->declarator_end && c_token_is_punctuator(&preprocess.tokens[declarator], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                // The parenthesized frame requires the declarator to end
                // exactly at the range it is given and knows nothing about a
                // trailing attribute list, so the child is handed the range
                // trimmed of one. The finish stage below resumes at
                // frame->declarator_end, which is where the trimmed tokens
                // end, so the list needs no second pass.
                u32 declarator_end = c_parse_trailing_attribute_start(preprocess, declarator, frame->declarator_end);
                u32 name_index = 0;
                if (!c_parse_parenthesized_declarator_name(preprocess, declarator, declarator_end, &name_index))
                {
                    c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                    return;
                }
                frame->name = preprocess.tokens[name_index];
                frame->stage = C_TYPE_PARSE_STAGE_PARAMETER_RESULT;
                if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                          .result = result,
                                                          .preprocess = preprocess,
                                                          .type = declarator_type,
                                                          .start = declarator,
                                                          .end = declarator_end,
                                                          .declarator_start = declarator,
                                                          .name_index = name_index,
                                                          .kind = C_TYPE_PARSE_FRAME_PARENTHESIZED,
                                                          .has_name = true,
                                                      }))
                {
                    c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                }
                return;
            }
            break;
        }
    }
    if (!name.length && declarator < frame->declarator_end && preprocess.tokens[declarator].kind == C_TOKEN_IDENTIFIER)
    {
        name = preprocess.tokens[declarator++];
    }
    else if (!name.length && (declarator >= frame->declarator_end || !c_token_is_punctuator(&preprocess.tokens[declarator], C_PUNCTUATOR_COLON)))
    {
        c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    bool is_bit_field = false;
    u32 bit_width = 0;
    u32 bit_width_token_start = 0;
    u32 bit_width_token_count = 0;
    if (declarator < frame->declarator_end && c_token_is_punctuator(&preprocess.tokens[declarator], C_PUNCTUATOR_COLON))
    {
        declarator += 1;
        bit_width_token_start = declarator;
        bit_width_token_count = frame->declarator_end - declarator;
        if (!bit_width_token_count)
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        if (bit_width_token_count == 1 && preprocess.tokens[declarator].kind == C_TOKEN_PREPROCESSING_NUMBER)
        {
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[declarator]);
            u64 width = 0;
            for (u64 index = 0; index < spelling.length; index += 1)
            {
                char8 digit = spelling.pointer[index];
                if (digit < '0' || digit > '9' || width > (UINT32_MAX - (u32)(digit - '0')) / 10)
                {
                    c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                    return;
                }
                width = width * 10 + (u32)(digit - '0');
            }
            bit_width = (u32)width;
        }
        is_bit_field = true;
        declarator = frame->declarator_end;
    }
    // The segment's run covers every declarator it declares; this member takes
    // the shared records plus the ones its own declarator carries. The counts
    // are local rather than written back over the frame's, because the frame's
    // run is what the *next* declarator of the same segment narrows in turn.
    u32 alignment_start = frame->alignment_start;
    u32 alignment_count = frame->alignment_count;
    if (!c_parse_member_alignment_run(result, frame->alignment_start, frame->alignment_count, frame->shared_specifier_end, frame->declarator_start,
                                      frame->declarator_end, &alignment_start, &alignment_count))
    {
        c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    if (is_bit_field && alignment_count)
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, frame->first), C_DIAGNOSTIC_INVALID_ALIGNMENT, S8("alignment specifier cannot be applied to a bit-field"));
        alignment_count = 0;
    }
    declarator_type = c_parse_apply_vector_attribute(result, preprocess, frame->scope, declarator_type, frame->declarator_start, frame->declarator_end);
    declarator = c_parse_skip_attributes(preprocess, declarator, frame->declarator_end);
    declarator_type = c_parse_array_suffixes(result, preprocess, declarator_type, &declarator, frame->declarator_end);
    declarator = c_parse_skip_attributes(preprocess, declarator, frame->declarator_end);
    if (declarator_type.value == C_ID_UNDERLYING_INVALID || declarator != frame->declarator_end)
    {
        c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    // A member declarator may spell `noreturn` on the function type it derives,
    // `struct ops { __attribute__((noreturn)) void (*fail)(int); };`, and the
    // call through the member has only that type to read.  The candidate is
    // resolved first because it is a handful of loads while the marker scan is
    // over tokens, and every member of every aggregate reaches here.  Only a
    // function type this segment built qualifies: a member written with a
    // shared `typedef void handler(int);` names a type older than the segment,
    // and marking that would end control flow at every other call written with
    // the typedef.  The shared specifiers and this declarator are scanned as
    // two ranges rather than as the span between them, which would read a
    // preceding declarator's own attribute as this one's; c_ir_declaration_is_noreturn
    // and the block-scope site split the same way, for the same reason.
    CTypeId noreturn_function = c_parse_noreturn_candidate_function_type(result, declarator_type, false, frame->checkpoint.type_count);
    if (noreturn_function.value != C_ID_UNDERLYING_INVALID && (c_ir_noreturn_marker_in_range(preprocess, frame->start, frame->shared_specifier_end) ||
                                                               c_ir_noreturn_marker_in_range(preprocess, frame->declarator_start, frame->declarator_end)))
    {
        c_parse_add_noreturn_function_type(result, noreturn_function);
    }
    BUSTER_CHECK(result->member_count < result->member_capacity);
    result->members[result->member_count++] = (CMember){
        .name = c_token_spelling(preprocess.spelling_base, name),
        .location = c_preprocess_token_location(&preprocess, name),
        .type = declarator_type,
        .alignment_start = alignment_start,
        .alignment_count = alignment_count,
        .bit_width = bit_width,
        .bit_width_token_start = bit_width_token_start,
        .bit_width_token_count = bit_width_token_count,
        .is_bit_field = is_bit_field,
        .is_packed = frame->is_packed,
    };
    frame->declarator_start = frame->declarator_end < frame->end ? frame->declarator_end + 1 : frame->end;
    frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    if (frame->declarator_start == frame->end)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->end, true);
    }
}

BUSTER_C_INTERNAL void c_type_parse_scalar_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        if (frame->start >= frame->end)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        u32 start = c_parse_skip_attributes(preprocess, frame->start, frame->end);
        u32 specifier_index = start;
        frame->qualifiers = (CType){
            .element_type = C_TYPE_ID_INVALID,
            .return_type = C_TYPE_ID_INVALID,
            .array_bound = C_ARRAY_BOUND_INVALID,
        };
        while (specifier_index < frame->end && preprocess.tokens[specifier_index].kind == C_TOKEN_IDENTIFIER &&
               !string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("_Atomic")))
        {
            if (!c_parse_atomic_declaration_prefix_token(preprocess, preprocess.tokens[specifier_index], &frame->qualifiers))
            {
                break;
            }
            specifier_index += 1;
            specifier_index = c_parse_skip_attributes(preprocess, specifier_index, frame->end);
        }
        bool is_typeof =
            specifier_index + 2 < frame->end && preprocess.tokens[specifier_index].kind == C_TOKEN_IDENTIFIER &&
            (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("__typeof__")) ||
             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("__typeof")) ||
             ((c_preprocess_dialect_is_gnu(preprocess.dialect) || c_preprocess_dialect_is_c23(preprocess.dialect)) &&
              string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("typeof"))) ||
             (c_preprocess_dialect_is_c23(preprocess.dialect) && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("typeof_unqual")))) &&
            c_token_is_punctuator(&preprocess.tokens[specifier_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
        bool is_atomic = specifier_index + 1 < frame->end && preprocess.tokens[specifier_index].kind == C_TOKEN_IDENTIFIER &&
                         string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("_Atomic")) &&
                         c_token_is_punctuator(&preprocess.tokens[specifier_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
        if (!is_typeof && !is_atomic)
        {
            frame->stage = C_TYPE_PARSE_STAGE_FINISH;
            // The core frame resolves the declaration's typedef name, so it
            // has to keep the scope this frame was entered with. Scope id 0 is
            // the file scope rather than an invalid sentinel, so omitting the
            // field here did not fail -- it silently resolved every local
            // declaration's base type at file scope, and a block-local typedef
            // shadowing an outer name of the same spelling lost.
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = result,
                                                      .preprocess = preprocess,
                                                      .scope = frame->scope,
                                                      .start = start,
                                                      .end = frame->end,
                                                      .mutation_mark = machine->mutation_count,
                                                      .kind = C_TYPE_PARSE_FRAME_CORE,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
            }
            return;
        }
        u32 close = specifier_index + 2;
        u32 depth = 1;
        while (close < frame->end && depth)
        {
            if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                depth -= 1;
            }
            close += 1;
        }
        if (depth || close == specifier_index + 3)
        {
            if (is_atomic)
            {
                c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[specifier_index]), C_DIAGNOSTIC_INVALID_ATOMIC_TYPE,
                                   S8("_Atomic requires a type name"));
            }
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
            return;
        }
        frame->checkpoint = *result;
        frame->mutation_mark = machine->mutation_count;
        frame->specifier_index = specifier_index;
        frame->close = close;
        frame->unqualified = is_typeof && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[specifier_index]), S8("typeof_unqual"));
        frame->is_bit_field = is_atomic;
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        u32 operand_start = specifier_index + 2;
        u32 operand_end = close - 1;
        bool nullptr_operand = is_typeof && c_preprocess_dialect_is_c23(preprocess.dialect) && operand_end == operand_start + 1 &&
                               preprocess.tokens[operand_start].kind == C_TOKEN_IDENTIFIER &&
                               string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[operand_start]), S8("nullptr"));
        if (nullptr_operand)
        {
            machine->result_type = c_parse_add_type(result, (CType){
                                                                  .element_type = C_TYPE_ID_INVALID,
                                                                  .return_type = C_TYPE_ID_INVALID,
                                                                  .array_bound = C_ARRAY_BOUND_INVALID,
                                                                  .kind = C_TYPE_NULLPTR,
                                                                  .is_complete = true,
                                                              });
            machine->result_index = operand_end;
            machine->result_valid = true;
        }
        else if (c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                       .result = result,
                                                       .preprocess = preprocess,
                                                       .scope = frame->scope,
                                                       .start = operand_start,
                                                       .end = operand_end,
                                                       .mutation_mark = machine->mutation_count,
                                                       .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                                   }))
        {
            return;
        }
        else
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, start, false);
            return;
        }
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_FINISH)
    {
        CTypeId type = machine->result_type;
        if (machine->result_valid && type.value < result->type_count)
        {
            CType* value = result->types + type.value;
            bool needs_qualification = (frame->qualifiers.is_const && !value->is_const) || (frame->qualifiers.is_volatile && !value->is_volatile) ||
                                        (frame->qualifiers.is_restrict && !value->is_restrict) || (frame->qualifiers.is_atomic && !value->is_atomic);
            if (needs_qualification)
            {
                type = c_parse_add_qualified_type(result, type, frame->qualifiers);
            }
        }
        c_type_parse_frame_complete(machine, type, machine->result_index, machine->result_valid);
        return;
    }
    u32 operand_start = frame->specifier_index + 2;
    u32 operand_end = frame->close - 1;
    CTypeId type = machine->result_valid ? machine->result_type : C_TYPE_ID_INVALID;
    u32 type_index = machine->result_index;
    if (frame->stage == C_TYPE_PARSE_STAGE_CHILD && type.value != C_ID_UNDERLYING_INVALID && type_index < operand_end)
    {
        type = c_parse_pointer_chain(result, preprocess, type, &type_index, operand_end);
        type = c_parse_array_suffixes(result, preprocess, type, &type_index, operand_end);
    }
    if (!frame->is_bit_field)
    {
        if (frame->stage == C_TYPE_PARSE_STAGE_CHILD && (type.value == C_ID_UNDERLYING_INVALID || type_index != operand_end))
        {
            c_type_parse_rollback(machine, result, frame->checkpoint, frame->mutation_mark);
            frame->stage = C_TYPE_PARSE_STAGE_FALLBACK;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = result,
                                                      .preprocess = preprocess,
                                                      .arena = machine->scratch_arena,
                                                      .scope = frame->scope,
                                                      .start = operand_start,
                                                      .end = operand_end,
                                                      .kind = C_TYPE_PARSE_FRAME_SIZEOF,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->specifier_index, false);
            }
            return;
        }
        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->specifier_index, false);
            return;
        }
        if (frame->unqualified)
        {
            type = c_parse_unqualified_type(result, type);
        }
        u32 suffix = frame->close;
        while (suffix < frame->end && preprocess.tokens[suffix].kind == C_TOKEN_IDENTIFIER &&
               c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[suffix], &frame->qualifiers))
        {
            suffix += 1;
        }
        if (frame->qualifiers.is_const || frame->qualifiers.is_volatile || frame->qualifiers.is_restrict || frame->qualifiers.is_atomic)
        {
            type = c_parse_add_qualified_type(result, type, frame->qualifiers);
        }
        c_type_parse_frame_complete(machine, type, suffix, true);
        return;
    }
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->specifier_index, false);
        return;
    }
    CType* inner_type = type.value < result->type_count ? result->types + type.value : 0;
    if (!inner_type || type_index != operand_end || inner_type->kind == C_TYPE_ARRAY || inner_type->kind == C_TYPE_FUNCTION ||
        inner_type->kind == C_TYPE_VOID || inner_type->kind == C_TYPE_NULLPTR || inner_type->is_const || inner_type->is_volatile ||
        inner_type->is_restrict || inner_type->is_atomic)
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[frame->specifier_index]), C_DIAGNOSTIC_INVALID_ATOMIC_TYPE,
                           S8("_Atomic type name must specify an unqualified non-array object type"));
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->specifier_index, false);
        return;
    }
    frame->qualifiers.is_atomic = true;
    u32 suffix = frame->close;
    while (suffix < frame->end && preprocess.tokens[suffix].kind == C_TOKEN_IDENTIFIER &&
           c_parse_atomic_declaration_prefix_token(preprocess, preprocess.tokens[suffix], &frame->qualifiers))
    {
        suffix += 1;
    }
    type = c_parse_add_qualified_type(result, type, frame->qualifiers);
    c_type_parse_frame_complete(machine, type, suffix, true);
}

// A type qualifier is allowed between a struct/union/enum specifier and the
// declarator -- `struct S const x;`, `struct { const char *tag; } const
// defs[]` -- the same way it is allowed after a primitive one. The aggregate
// paths used to stop at the closing brace or the tag, so the qualifier stood
// where the declarator was expected and the declaration bound no name at all.
BUSTER_C_INTERNAL CTypeId c_parse_apply_trailing_qualifiers(CParseResult* result, CPreprocessResult preprocess, CTypeId type, u32* index, u32 end)
{
    if (type.value >= result->type_count)
    {
        return type;
    }
    CType qualified = result->types[type.value];
    bool has_qualifier = false;
    while (*index < end && preprocess.tokens[*index].kind == C_TOKEN_IDENTIFIER &&
           c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[*index], &qualified))
    {
        has_qualifier = true;
        *index += 1;
    }
    return has_qualifier ? c_parse_add_qualified_type(result, type, qualified) : type;
}

BUSTER_C_INTERNAL void c_type_parse_core_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        u32 declarator_start = frame->start;
        CTypeId type = c_parse_scalar_type_core_begin(machine, frame, &declarator_start);
        if (type.value == C_ID_UNDERLYING_INVALID || machine->failed)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, declarator_start, false);
            return;
        }
        if (!frame->has_function_suffix)
        {
            c_type_parse_frame_complete(machine, type, declarator_start, true);
            return;
        }
        bool nested_aggregate = false;
        for (u32 index = 0; index + 1 < machine->frame_count; index += 1)
        {
            nested_aggregate |= machine->frames[index].kind == C_TYPE_PARSE_FRAME_AGGREGATE_SEGMENT;
        }
        if (nested_aggregate)
        {
            type = c_parse_apply_trailing_qualifiers(result, frame->preprocess, type, &declarator_start, frame->end);
            c_type_parse_frame_complete(machine, type, declarator_start, true);
            return;
        }
        CType* root = type.value < result->type_count ? result->types + type.value : 0;
        if (!root || root->kind == C_TYPE_ENUM)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, declarator_start, false);
            return;
        }
        if (!c_type_parse_record_mutation(machine, result, type))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, declarator_start, false);
            return;
        }
        root->member_start = result->member_count;
        frame->pending_index = frame->definition_type_start;
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = frame->preprocess,
                                                  .start = root->definition_start,
                                                  .end = root->definition_start + root->definition_token_count,
                                                  .kind = C_TYPE_PARSE_FRAME_AGGREGATE_RANGE,
                                              }))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, declarator_start, false);
        }
        return;
    }
    if (!machine->result_valid)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    CTypeId completed_id = frame->original_type_valid ? frame->original_type_id : (CTypeId){.value = frame->pending_index - 1};
    frame->original_type_valid = false;
    if (completed_id.value >= result->type_count)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    CType* completed = result->types + completed_id.value;
    if (!c_type_parse_record_mutation(machine, result, completed_id))
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    completed->member_count = result->member_count - completed->member_start;
    completed->is_complete = true;
    c_parse_validate_flexible_array_members(result, completed);
    while (frame->pending_index < result->type_count)
    {
        CTypeId pending_id = {.value = frame->pending_index++};
        CType* pending = result->types + pending_id.value;
        if (pending->is_complete || !pending->definition_start)
        {
            continue;
        }
        if (pending->kind != C_TYPE_STRUCT && pending->kind != C_TYPE_UNION)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        if (!c_type_parse_record_mutation(machine, result, pending_id))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        pending->member_start = result->member_count;
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = frame->preprocess,
                                                  .start = pending->definition_start,
                                                  .end = pending->definition_start + pending->definition_token_count,
                                                  .kind = C_TYPE_PARSE_FRAME_AGGREGATE_RANGE,
                                              }))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        }
        return;
    }
    u32 completion_index = frame->close + 1;
    CTypeId completion_type = c_parse_apply_trailing_qualifiers(result, frame->preprocess, frame->type, &completion_index, frame->end);
    c_type_parse_frame_complete(machine, completion_type, completion_index, true);
}

BUSTER_C_INTERNAL void c_type_parse_parameter_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        if (result->position_index && !result->position_index->built)
        {
            c_parse_position_index_build(result, preprocess);
        }
        u32 alignas_index = UINT32_MAX;
        if (result->position_index)
        {
            alignas_index =
                c_parse_first_position_in_range(result->position_index->alignas_positions, result->position_index->alignas_count, frame->start, frame->end);
        }
        else
        {
            for (u32 index = frame->start; index < frame->end; index += 1)
            {
                if (preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("_Alignas")))
                {
                    alignas_index = index;
                    break;
                }
            }
        }
        if (alignas_index != UINT32_MAX)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[alignas_index]), C_DIAGNOSTIC_INVALID_ALIGNMENT,
                               S8("alignment specifier cannot be applied to a parameter"));
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->stage = C_TYPE_PARSE_STAGE_CHILD;
        if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                  .result = result,
                                                  .preprocess = preprocess,
                                                  .scope = result->scope_count ? (CScopeId){.value = 0} : C_SCOPE_ID_INVALID,
                                                  .start = frame->start,
                                                  .end = frame->end,
                                                  .mutation_mark = machine->mutation_count,
                                                  .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                              }))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        }
        return;
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        if (!machine->result_valid)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->type = machine->result_type;
        frame->declarator_start = machine->result_index;
        frame->type = c_parse_pointer_chain(result, preprocess, frame->type, &frame->declarator_start, frame->end);
        frame->type = c_parse_apply_vector_attribute(result, preprocess, frame->scope, frame->type, frame->start, frame->end);
        frame->name = (CToken){0};
        if (frame->declarator_start < frame->end && c_token_is_punctuator(&preprocess.tokens[frame->declarator_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 nested_name_index = 0;
            bool nested_has_name = c_parse_parenthesized_declarator_name(preprocess, frame->declarator_start, frame->end, &nested_name_index);
            if (!nested_has_name)
            {
                u32 close_index = frame->declarator_start + 1;
                CType ignored = {0};
                while (close_index < frame->end && c_token_is_punctuator(&preprocess.tokens[close_index], C_PUNCTUATOR_STAR))
                {
                    close_index += 1;
                    while (close_index < frame->end && preprocess.tokens[close_index].kind == C_TOKEN_IDENTIFIER &&
                           c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[close_index], &ignored))
                    {
                        close_index += 1;
                    }
                }
                if (close_index == frame->declarator_start + 1 || close_index >= frame->end ||
                    !c_token_is_punctuator(&preprocess.tokens[close_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                    return;
                }
                nested_name_index = close_index;
            }
            if (nested_has_name)
            {
                frame->name = preprocess.tokens[nested_name_index];
            }
            frame->stage = C_TYPE_PARSE_STAGE_FALLBACK;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = result,
                                                      .preprocess = preprocess,
                                                      .type = frame->type,
                                                      .start = frame->declarator_start,
                                                      .end = frame->end,
                                                      .declarator_start = frame->declarator_start,
                                                      .name_index = nested_name_index,
                                                      .kind = C_TYPE_PARSE_FRAME_PARENTHESIZED,
                                                      .has_name = nested_has_name,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            }
            return;
        }
    }
    else if (frame->stage == C_TYPE_PARSE_STAGE_FALLBACK)
    {
        if (!machine->result_valid)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        frame->type = machine->result_type;
        frame->declarator_start = frame->end;
    }
    if (frame->declarator_start < frame->end && preprocess.tokens[frame->declarator_start].kind == C_TOKEN_IDENTIFIER)
    {
        frame->name = preprocess.tokens[frame->declarator_start++];
    }
    frame->declarator_start = c_parse_skip_attributes(preprocess, frame->declarator_start, frame->end);
    frame->type = c_parse_array_suffixes(result, preprocess, frame->type, &frame->declarator_start, frame->end);
    frame->declarator_start = c_parse_skip_attributes(preprocess, frame->declarator_start, frame->end);
    if (frame->declarator_start != frame->end || frame->type.value == C_ID_UNDERLYING_INVALID || result->parameter_count >= result->parameter_capacity)
    {
        c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
        return;
    }
    result->parameters[result->parameter_count++] = (CParameter){
        .name = c_token_spelling(preprocess.spelling_base, frame->name),
        .location = c_preprocess_token_location(&preprocess, frame->name),
        .type = frame->type,
        .entity = C_ENTITY_ID_INVALID,
    };
    c_type_parse_frame_complete(machine, frame->type, frame->end, true);
}

// One record per parameter the list at `open` declares.  `()`, `(void)` and
// the `...` of a variadic list declare none.  The records are reserved before
// the segments are parsed because a parameter that is itself a function
// pointer appends the records of its own parameters first: without a reserved
// block the enclosing list would count those too, and the function type would
// claim more parameters than the calls that pass them.
BUSTER_C_INTERNAL u32 c_parse_parameter_list_reserved_count(CPreprocessResult preprocess, u32 open, u32 end)
{
    u32 reserved = 0;
    u32 segment_start = open + 1;
    u32 depth = 1;
    for (u32 index = segment_start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            depth += 1;
            continue;
        }
        if ((c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET)) && depth > 1)
        {
            depth -= 1;
            continue;
        }
        bool separator = depth == 1 && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA);
        bool list_end = depth == 1 && c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        if (!separator && !list_end)
        {
            continue;
        }
        u32 count = index - segment_start;
        bool omitted = !count || (count == 1 && (c_token_is_punctuator(&preprocess.tokens[segment_start], C_PUNCTUATOR_ELLIPSIS) ||
                                                 (list_end && !reserved && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]), S8("void")))));
        reserved += omitted ? 0 : 1;
        if (list_end)
        {
            break;
        }
        segment_start = index + 1;
    }
    return reserved;
}

// `()` -- an empty parameter list -- declares a function with no prototype,
// which C11 6.2.7p3 makes compatible with a non-variadic prototype; `(void)`
// declares a prototype with zero parameters and is compatible with no other
// list. Both produce zero parameter records, so the shape is read back off
// the tokens: the list is empty exactly when its closing parenthesis abuts
// its opening one.
BUSTER_C_INTERNAL bool c_parse_parameter_list_unprototyped(CPreprocessResult preprocess, u32 list_close)
{
    return list_close && list_close - 1 < preprocess.token_count &&
           c_token_is_punctuator(&preprocess.tokens[list_close - 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
}

// The outer suffix of a parenthesized declarator: `(*fp)(int)` and
// `(*getf(int))(void)` both continue after the group's closing parenthesis,
// with a parameter list making the declared type a function and anything else
// leaving it to the array suffixes the finish stage applies.
BUSTER_C_INTERNAL void c_type_parse_parenthesized_suffix_begin(CTypeParseFrame* frame, CParseResult* result, CPreprocessResult preprocess)
{
    frame->index = frame->close_index + 1;
    if (frame->index < frame->end && c_token_is_punctuator(&preprocess.tokens[frame->index], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        frame->parameter_start = result->parameter_count;
        frame->written_parameter_count = 0;
        result->parameter_count += c_parse_parameter_list_reserved_count(preprocess, frame->index, frame->end);
        frame->segment_start = frame->index + 1;
        frame->scan_index = frame->segment_start;
        frame->depth = 1;
        frame->stage = C_TYPE_PARSE_STAGE_PARAMETERS;
    }
    else
    {
        frame->stage = C_TYPE_PARSE_STAGE_FINISH;
    }
}

// One parameter list has been scanned to its closing parenthesis.  The name's
// own list is scanned first but applied last -- it is the outermost derivation
// of `void (*getf(int))(void)` -- so it only records its range here and hands
// the frame on to the outer suffix; the outer list is the return type and
// becomes a function type immediately.
BUSTER_C_INTERNAL void c_type_parse_parenthesized_list_complete(CTypeParseFrame* frame, CParseResult* result, CPreprocessResult preprocess, u32 list_close)
{
    if (frame->scanning_inner_parameters)
    {
        frame->inner_parameter_start = frame->parameter_start;
        frame->inner_parameter_count = frame->written_parameter_count;
        frame->inner_variadic = frame->variadic;
        frame->inner_unprototyped = c_parse_parameter_list_unprototyped(preprocess, list_close);
        frame->has_inner_parameters = true;
        frame->scanning_inner_parameters = false;
        frame->variadic = false;
        c_type_parse_parenthesized_suffix_begin(frame, result, preprocess);
        return;
    }
    frame->index = list_close + 1;
    frame->type = c_parse_add_type(result, (CType){
                                             .element_type = C_TYPE_ID_INVALID,
                                             .return_type = frame->type,
                                             .array_bound = C_ARRAY_BOUND_INVALID,
                                             .parameter_start = frame->parameter_start,
                                             .parameter_count = frame->written_parameter_count,
                                             .kind = C_TYPE_FUNCTION,
                                             .is_variadic = frame->variadic,
                                             .is_unprototyped = c_parse_parameter_list_unprototyped(preprocess, list_close),
                                         });
    frame->has_function_suffix = true;
    frame->stage = C_TYPE_PARSE_STAGE_FINISH;
}

BUSTER_C_INTERNAL void c_type_parse_parenthesized_step(CTypeParseMachine* machine, CTypeParseFrame* frame)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    if (frame->stage == C_TYPE_PARSE_STAGE_CHILD)
    {
        // The nested group carries the name, so its type is this declarator's.
        if (!machine->result_valid)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        c_type_parse_frame_complete(machine, machine->result_type, frame->end, true);
        return;
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_BEGIN)
    {
        if (frame->declarator_start >= frame->end || !c_token_is_punctuator(&preprocess.tokens[frame->declarator_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        // `(*(*sym)(int))(void)` holds a whole declarator of its own after the
        // pointer chain.  This frame then owns the outer suffix and that
        // pointer chain only; the group starting here is a child frame parsed
        // over the type they produce, which is what makes the derivation
        // recursive rather than one group deep.
        u32 nested_index = frame->declarator_start + 1;
        while (nested_index < frame->end && c_token_is_punctuator(&preprocess.tokens[nested_index], C_PUNCTUATOR_STAR))
        {
            nested_index += 1;
            CType ignored = {0};
            while (nested_index < frame->end && preprocess.tokens[nested_index].kind == C_TOKEN_IDENTIFIER &&
                   c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[nested_index], &ignored))
            {
                nested_index += 1;
            }
        }
        frame->nested = nested_index < frame->end && nested_index > frame->declarator_start + 1 &&
                        c_token_is_punctuator(&preprocess.tokens[nested_index], C_PUNCTUATOR_LEFT_PARENTHESIS);
        if (frame->nested)
        {
            frame->nested_start = nested_index;
            frame->close_index = c_parse_matching_delimiter_indexed(result, preprocess, frame->declarator_start);
            frame->pointer_start = frame->declarator_start + 1;
            if (frame->close_index >= frame->end)
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
            c_type_parse_parenthesized_suffix_begin(frame, result, preprocess);
        }
        else if (frame->name_index >= frame->end)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        else
        {
            // `void (*getf(int))(void)` is a function returning a pointer to
            // function: the name inside the pointer declarator carries its own
            // parameter list.  Take that group out of the way of the scan looking
            // for the group's closing parenthesis, and remember it so the return
            // type can be completed before it is applied.
            frame->inner_open = C_ID_UNDERLYING_INVALID;
            frame->inner_close = C_ID_UNDERLYING_INVALID;
            if (frame->has_name && frame->name_index + 1 < frame->end &&
                c_token_is_punctuator(&preprocess.tokens[frame->name_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                u32 inner_close = c_parse_matching_delimiter_indexed(result, preprocess, frame->name_index + 1);
                if (inner_close >= frame->end)
                {
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                    return;
                }
                frame->inner_open = frame->name_index + 1;
                frame->inner_close = inner_close;
            }
            frame->close_index = frame->inner_open != C_ID_UNDERLYING_INVALID ? frame->inner_close + 1
                                 : frame->has_name                            ? frame->name_index + 1
                                                                              : frame->name_index;
            u32 bracket_depth = 0;
            while (frame->has_name && frame->close_index < frame->end)
            {
                const CToken* token = &preprocess.tokens[frame->close_index];
                if (c_token_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    u32 bracket_close = c_parse_matching_delimiter_indexed(result, preprocess, frame->close_index);
                    if (bracket_close < frame->end)
                    {
                        frame->close_index = bracket_close + 1;
                        continue;
                    }
                    bracket_depth += 1;
                }
                else if (c_token_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACKET) && bracket_depth)
                {
                    bracket_depth -= 1;
                }
                else if (!bracket_depth && c_token_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    break;
                }
                frame->close_index += 1;
            }
            frame->pointer_start = frame->declarator_start + 1;
            if (frame->close_index >= frame->end || !c_token_is_punctuator(&preprocess.tokens[frame->close_index], C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
                (frame->pointer_start < frame->name_index && !c_token_is_punctuator(&preprocess.tokens[frame->pointer_start], C_PUNCTUATOR_STAR)))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
            if (frame->inner_open != C_ID_UNDERLYING_INVALID)
            {
                frame->parameter_start = result->parameter_count;
                frame->written_parameter_count = 0;
                result->parameter_count += c_parse_parameter_list_reserved_count(preprocess, frame->inner_open, frame->end);
                frame->segment_start = frame->inner_open + 1;
                frame->scan_index = frame->segment_start;
                frame->depth = 1;
                frame->scanning_inner_parameters = true;
                frame->stage = C_TYPE_PARSE_STAGE_PARAMETERS;
            }
            else
            {
                c_type_parse_parenthesized_suffix_begin(frame, result, preprocess);
            }
        }
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_PARAMETER_RESULT)
    {
        if (!machine->result_valid)
        {
            result->parameter_count = frame->parameter_start;
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        // The child appended its own record on top of any it created for a
        // function-pointer parameter's own parameters; move it down into the
        // block reserved for this list, which is what the function type spans.
        if (result->parameter_count <= frame->parameter_start + frame->written_parameter_count)
        {
            result->parameter_count = frame->parameter_start;
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        result->parameters[frame->parameter_start + frame->written_parameter_count] = result->parameters[result->parameter_count - 1];
        frame->written_parameter_count += 1;
        result->parameter_count -= 1;
        frame->segment_start = frame->scan_index + 1;
        if (c_token_is_punctuator(&preprocess.tokens[frame->scan_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            c_type_parse_parenthesized_list_complete(frame, result, preprocess, frame->scan_index);
        }
        else
        {
            frame->scan_index += 1;
            frame->stage = C_TYPE_PARSE_STAGE_PARAMETERS;
        }
    }
    // The name's own parameter list hands the frame back to this stage for the
    // outer suffix, which is a second list to scan, so the stage repeats
    // instead of running once.
    while (frame->stage == C_TYPE_PARSE_STAGE_PARAMETERS)
    {
        while (frame->scan_index < frame->end)
        {
            const CToken* token = &preprocess.tokens[frame->scan_index];
            if (c_token_is_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                // A properly nested group is paren-balanced inside, so
                // hopping to its closer leaves the depth walk identical.
                u32 nested_close = c_parse_matching_delimiter_indexed(result, preprocess, frame->scan_index);
                if (nested_close < frame->end)
                {
                    frame->scan_index = nested_close + 1;
                    continue;
                }
                frame->depth += 1;
                frame->scan_index += 1;
                continue;
            }
            if (c_token_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS) && frame->depth > 1)
            {
                frame->depth -= 1;
                frame->scan_index += 1;
                continue;
            }
            bool segment_end = frame->depth == 1 && c_token_is_punctuator(token, C_PUNCTUATOR_COMMA);
            bool list_end = frame->depth == 1 && c_token_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
            if (!segment_end && !list_end)
            {
                frame->scan_index += 1;
                continue;
            }
            u32 segment_count = frame->scan_index - frame->segment_start;
            if (list_end && !segment_count && !frame->written_parameter_count)
            {
                c_type_parse_parenthesized_list_complete(frame, result, preprocess, frame->scan_index);
                break;
            }
            if (segment_count == 1 && c_token_is_punctuator(&preprocess.tokens[frame->segment_start], C_PUNCTUATOR_ELLIPSIS))
            {
                frame->variadic = true;
            }
            else if (!(list_end && !frame->written_parameter_count && segment_count == 1 &&
                       string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[frame->segment_start]), S8("void"))))
            {
                if (!segment_count)
                {
                    result->parameter_count = frame->parameter_start;
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                    return;
                }
                frame->stage = C_TYPE_PARSE_STAGE_PARAMETER_RESULT;
                if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                          .result = result,
                                                          .preprocess = preprocess,
                                                          .start = frame->segment_start,
                                                          .end = frame->scan_index,
                                                          .kind = C_TYPE_PARSE_FRAME_PARAMETER,
                                                      }))
                {
                    result->parameter_count = frame->parameter_start;
                    c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                }
                return;
            }
            frame->segment_start = frame->scan_index + 1;
            if (list_end)
            {
                c_type_parse_parenthesized_list_complete(frame, result, preprocess, frame->scan_index);
                break;
            }
            frame->scan_index += 1;
        }
        // Still scanning with nothing left is an unterminated list; still
        // scanning with tokens left is the outer suffix, which the completed
        // inner list just handed back to this stage.
        if (frame->stage == C_TYPE_PARSE_STAGE_PARAMETERS && frame->scan_index >= frame->end)
        {
            result->parameter_count = frame->parameter_start;
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
    }
    if (frame->stage == C_TYPE_PARSE_STAGE_FINISH)
    {
        if (!frame->has_function_suffix)
        {
            frame->type = c_parse_array_suffixes(result, preprocess, frame->type, &frame->index, frame->end);
            if (frame->index != frame->end)
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
        }
        u32 pointer_end = frame->nested ? frame->nested_start : frame->name_index;
        frame->type = c_parse_pointer_chain(result, preprocess, frame->type, &frame->pointer_start, pointer_end);
        if (frame->pointer_start != pointer_end)
        {
            c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            return;
        }
        if (frame->nested)
        {
            if (frame->type.value == C_ID_UNDERLYING_INVALID || (frame->has_function_suffix && frame->index != frame->end))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
            u32 nested_name_index = 0;
            bool nested_has_name = c_parse_parenthesized_declarator_name(preprocess, frame->nested_start, frame->close_index, &nested_name_index);
            frame->stage = C_TYPE_PARSE_STAGE_CHILD;
            if (!c_type_parse_frame_push(machine, (CTypeParseFrame){
                                                      .result = result,
                                                      .preprocess = preprocess,
                                                      .type = frame->type,
                                                      .start = frame->nested_start,
                                                      .end = frame->close_index,
                                                      .declarator_start = frame->nested_start,
                                                      .name_index = nested_has_name ? nested_name_index : frame->nested_start,
                                                      .kind = C_TYPE_PARSE_FRAME_PARENTHESIZED,
                                                      .has_name = nested_has_name,
                                                  }))
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
            }
            return;
        }
        if (frame->has_name)
        {
            u32 array_index = frame->has_inner_parameters ? frame->inner_close + 1 : frame->name_index + 1;
            frame->type = c_parse_array_suffixes(result, preprocess, frame->type, &array_index, frame->close_index);
            if (array_index != frame->close_index)
            {
                c_type_parse_frame_complete(machine, C_TYPE_ID_INVALID, frame->start, false);
                return;
            }
        }
        if (frame->has_inner_parameters)
        {
            // Everything derived so far -- the outer suffix and the pointer
            // chain -- is what the name's own parameter list returns.
            frame->type = c_parse_add_type(result, (CType){
                                                     .element_type = C_TYPE_ID_INVALID,
                                                     .return_type = frame->type,
                                                     .array_bound = C_ARRAY_BOUND_INVALID,
                                                     .parameter_start = frame->inner_parameter_start,
                                                     .parameter_count = frame->inner_parameter_count,
                                                     .kind = C_TYPE_FUNCTION,
                                                     .is_variadic = frame->inner_variadic,
                                                     .is_unprototyped = frame->inner_unprototyped,
                                                 });
        }
        bool valid = frame->type.value != C_ID_UNDERLYING_INVALID && (!frame->has_function_suffix || frame->index == frame->end);
        c_type_parse_frame_complete(machine, frame->type, frame->end, valid);
    }
}

BUSTER_C_INTERNAL void c_type_parse_machine_run(CTypeParseMachine* machine, u32 frame_start)
{
    while (machine->frame_count > frame_start && !machine->failed)
    {
        CTypeParseFrame* frame = machine->frames + machine->frame_count - 1;
        switch (frame->kind)
        {
        case C_TYPE_PARSE_FRAME_SCALAR:
            c_type_parse_scalar_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_CORE:
            c_type_parse_core_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_SIZEOF:
            c_type_parse_sizeof_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_EXPRESSION_LEAF:
            c_type_parse_expression_leaf_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_ALIGNMENT:
            c_type_parse_alignment_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_AGGREGATE_SEGMENT:
            c_type_parse_aggregate_segment_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_AGGREGATE_RANGE:
            c_type_parse_aggregate_range_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_PARENTHESIZED:
            c_type_parse_parenthesized_step(machine, frame);
            break;
        case C_TYPE_PARSE_FRAME_PARAMETER:
            c_type_parse_parameter_step(machine, frame);
            break;
        }
    }
    if (machine->failed)
    {
        machine->frame_count = frame_start;
        machine->result_type = C_TYPE_ID_INVALID;
        machine->result_valid = false;
    }
}

BUSTER_C_INTERNAL CTypeId c_parse_scalar_type_in_scope(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                         u32 start, u32 end,
                                                         u32* declarator_start)
{
    u32 frame_start = machine->frame_count;
    CParseResult checkpoint = *result;
    u32 mutation_mark = machine->mutation_count;
    machine->mutation_type_limit = checkpoint.type_count;
    machine->result_valid = false;
    bool pushed = c_type_parse_frame_push(machine, (CTypeParseFrame){
                                              .result = result,
                                              .preprocess = preprocess,
                                              .scope = scope,
                                              .start = start,
                                              .end = end,
                                              .mutation_mark = mutation_mark,
                                              .kind = C_TYPE_PARSE_FRAME_SCALAR,
                                          });
    if (pushed)
    {
        c_type_parse_machine_run(machine, frame_start);
    }
    bool valid = c_type_parse_root_finish(machine, result, checkpoint, mutation_mark,
                                          start < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[start]) : (CSourceLocation){0});
    *declarator_start = machine->result_index;
    return valid ? machine->result_type : C_TYPE_ID_INVALID;
}

BUSTER_C_INTERNAL CTypeId c_parse_scalar_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess, u32 start, u32 end,
                                                u32* declarator_start)
{
    return c_parse_scalar_type_in_scope(machine, result, preprocess, result->scope_count ? (CScopeId){.value = 0} : C_SCOPE_ID_INVALID, start, end,
                                        declarator_start);
}

// The base type of a machineless sizeof/_Alignof operand: a (qualified)
// typedef name, a named struct/union/enum tag, or a primitive spelling.
// `*index_out` lands past what was consumed. The type-parse machine is never
// entered, so this is callable from inside one of its steps — which is what
// lets a sizeof inside an array bound resolve while an enum body is being
// evaluated (the machine-bearing path uses c_parse_scalar_type instead).
BUSTER_C_INTERNAL CTypeId c_parse_machineless_base_type(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 start, u32 end,
                                                          u32* index_out)
{
    u32 index = start;
    CTypeId type = c_parse_qualified_typedef_type(result, preprocess, scope, start, end, &index);
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        u32 tag_index = c_parse_skip_attributes(preprocess, start, end);
        String8 tag_word = tag_index < end && preprocess.tokens[tag_index].kind == C_TOKEN_IDENTIFIER
                               ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[tag_index])
                               : (String8){0};
        CTypeKind tag_kind = string_equal(tag_word, S8("struct"))  ? C_TYPE_STRUCT
                             : string_equal(tag_word, S8("union")) ? C_TYPE_UNION
                             : string_equal(tag_word, S8("enum"))  ? C_TYPE_ENUM
                                                                   : C_TYPE_INVALID;
        if (tag_kind != C_TYPE_INVALID)
        {
            u32 name_index = c_parse_skip_attributes(preprocess, tag_index + 1, end);
            if (name_index < end && preprocess.tokens[name_index].kind == C_TOKEN_IDENTIFIER)
            {
                type = c_parse_aggregate_lookup(result, tag_kind, c_token_spelling(preprocess.spelling_base, preprocess.tokens[name_index]));
                index = c_parse_skip_attributes(preprocess, name_index + 1, end);
            }
        }
    }
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        index = start;
        type = c_parse_primitive_type(result, preprocess, start, end, &index);
    }
    *index_out = index;
    return type;
}

// How many elements a brace initializer spells, counted from its tokens
// alone — for the inferred-length arrays c_parse_infer_file_array_bounds has
// not reached yet when a sizeof inside an enum-constant initializer needs
// the array's layout mid-parse. Real inference cannot run there (its
// designator and slot resolution reenters the type-parse machine), so this
// counts only the shapes whose count is one top-level item per element:
// no designators, no top-level assignment, and — when the element type is
// itself an aggregate or array — every item a braced group, because a flat
// item list fills members rather than elements. Everything else answers
// zero and the caller stays unresolved, exactly as before the count.
BUSTER_C_INTERNAL u64 c_parse_count_plain_initializer_items(CPreprocessResult preprocess, u32 start, u32 end, bool element_is_aggregate)
{
    u64 count = 0;
    if (start < end && c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_BRACE) &&
        c_token_is_punctuator(&preprocess.tokens[end - 1], C_PUNCTUATOR_RIGHT_BRACE))
    {
        u32 depth = 0;
        bool item_open = false;
        bool item_braced = false;
        bool valid = true;
        for (u32 index = start; valid && index < end; index += 1)
        {
            CToken token = preprocess.tokens[index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                // A designator's bracket, or any bracket ahead of a value,
                // makes the item count untrustworthy.
                valid &= !c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) || depth != 1;
                if (depth == 1 && !item_open)
                {
                    item_open = true;
                    item_braced = c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE);
                }
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                depth -= depth != 0;
            }
            else if (depth == 1)
            {
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
                {
                    count += item_open;
                    valid &= !item_open || !element_is_aggregate || item_braced;
                    item_open = false;
                    item_braced = false;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_DOT) || c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
                {
                    valid = false;
                }
                else if (!item_open)
                {
                    item_open = true;
                    item_braced = false;
                }
            }
        }
        count += item_open;
        valid &= !item_open || !element_is_aggregate || item_braced;
        count = valid ? count : 0;
    }
    return count;
}

// The expression shapes the enum-constant evaluator can apply sizeof or
// _Alignof to without the type-parse machine: an object, parameter, local,
// or enumerator name under redundant parentheses and leading dereferences,
// extended by a postfix chain of subscripts and member selections. Anything
// else stays unresolved and the caller fails the enumerator rather than
// guessing — but these shapes cover the array-length idiom
// `enum { N = sizeof(table) / sizeof(table[0]) }`, whose failure used to
// fail the whole enum type and leave every one of its enumerators
// undeclared in function bodies (found by tools/differential_c_harness.py,
// family enum_sizeof; tests/basic_c_enum_sizeof_object.c is the fixture).
BUSTER_C_INTERNAL bool c_parse_sizeof_operand_expression_layout(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                                  u32 start, u32 end, u64* size_out, u32* alignment_out)
{
    bool stripping = true;
    while (stripping)
    {
        stripping = false;
        if (end > start + 1 && c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 depth = 0;
            u32 close = 0;
            for (u32 index = start; index < end && !close; index += 1)
            {
                CToken token = preprocess.tokens[index];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    depth -= 1;
                    if (!depth)
                    {
                        close = index;
                    }
                }
            }
            if (close == end - 1)
            {
                start += 1;
                end -= 1;
                stripping = true;
            }
        }
    }
    u32 dereference_count = 0;
    while (start < end && c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_STAR))
    {
        dereference_count += 1;
        start += 1;
    }
    CTypeId type = C_TYPE_ID_INVALID;
    bool is_enumerator = false;
    u32 declaration_index = UINT32_MAX;
    u32 index = start;
    if (start < end && preprocess.tokens[start].kind == C_TOKEN_IDENTIFIER)
    {
        String8 name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]);
        CEntityId entity = c_parse_lookup_entity(result, scope, name);
        if (entity.value < result->entity_count)
        {
            CEntity* found = &result->entities[entity.value];
            if (found->kind == C_ENTITY_OBJECT || found->kind == C_ENTITY_PARAMETER || found->kind == C_ENTITY_LOCAL)
            {
                type = found->type;
                declaration_index = found->declaration_index;
            }
            else if (found->kind == C_ENTITY_ENUMERATOR)
            {
                is_enumerator = true;
            }
        }
        else
        {
            // A member of the enum still being declared is not an entity yet;
            // the member table already holds everything declared before this
            // initializer.
            for (u32 member_index = 0; member_index < result->enum_member_count && !is_enumerator; member_index += 1)
            {
                is_enumerator = string_equal(result->enum_members[member_index].name, name);
            }
        }
        index = start + 1;
    }
    bool valid = type.value != C_ID_UNDERLYING_INVALID;
    while (valid && index < end)
    {
        CToken token = preprocess.tokens[index];
        CType* record = type.value < result->type_count ? &result->types[type.value] : 0;
        if (!record)
        {
            valid = false;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            u32 depth = 0;
            u32 close = 0;
            for (u32 cursor = index; cursor < end && !close; cursor += 1)
            {
                CToken current = preprocess.tokens[cursor];
                if (c_token_is_punctuator(&current, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&current, C_PUNCTUATOR_RIGHT_BRACKET))
                {
                    depth -= 1;
                    if (!depth)
                    {
                        close = cursor;
                    }
                }
            }
            valid = close != 0 &&
                    (record->kind == C_TYPE_ARRAY || record->kind == C_TYPE_POINTER || record->kind == C_TYPE_VECTOR);
            type = valid ? record->element_type : type;
            index = valid ? close + 1 : index;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_DOT) || c_token_is_punctuator(&token, C_PUNCTUATOR_ARROW))
        {
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_ARROW))
            {
                valid = record->kind == C_TYPE_POINTER && record->element_type.value < result->type_count;
                record = valid ? &result->types[record->element_type.value] : record;
            }
            valid = valid && index + 1 < end && preprocess.tokens[index + 1].kind == C_TOKEN_IDENTIFIER &&
                    (record->kind == C_TYPE_STRUCT || record->kind == C_TYPE_UNION);
            if (valid)
            {
                String8 member_name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index + 1]);
                bool found_member = false;
                for (u32 member_index = record->member_start; member_index < record->member_start + record->member_count && !found_member;
                     member_index += 1)
                {
                    CMember* member = &result->members[member_index];
                    if (string_equal(member->name, member_name) && !member->is_bit_field)
                    {
                        type = member->type;
                        found_member = true;
                    }
                }
                valid = found_member;
            }
            index += valid ? 2 : 0;
        }
        else
        {
            valid = false;
        }
    }
    while (valid && dereference_count)
    {
        CType* record = type.value < result->type_count ? &result->types[type.value] : 0;
        valid = record && (record->kind == C_TYPE_POINTER || record->kind == C_TYPE_ARRAY);
        type = valid ? record->element_type : type;
        dereference_count -= 1;
    }
    // A bare object name whose type is an inferred-length array needs its
    // element count before c_parse_infer_file_array_bounds has run; count
    // the plain initializer shapes directly and keep the answer local to
    // this query, like every other inferred layout.
    u64 counted_items = 0;
    CTypeId counted_element = C_TYPE_ID_INVALID;
    if (valid && !dereference_count && index == start + 1 && index == end && type.value < result->type_count &&
        declaration_index < result->declaration_count)
    {
        CType* record = &result->types[type.value];
        bool unresolved_bound = record->kind == C_TYPE_ARRAY && record->array_bound < result->array_bound_count &&
                                !result->array_bounds[record->array_bound].token_count &&
                                !result->array_bounds[record->array_bound].has_inferred_count &&
                                !result->array_bounds[record->array_bound].is_star;
        if (unresolved_bound)
        {
            CDeclaration declaration = result->declarations[declaration_index];
            u32 declaration_start = declaration.declarator_count ? declaration.declarator_start : declaration.token_start;
            u32 declaration_end = declaration.declarator_count ? declaration.declarator_start + declaration.declarator_count
                                                               : declaration.token_start + declaration.token_count;
            u32 initializer = declaration_end;
            u32 depth = 0;
            for (u32 cursor = declaration_start; cursor < declaration_end && initializer == declaration_end; cursor += 1)
            {
                CToken current = preprocess.tokens[cursor];
                if (c_token_is_punctuator(&current, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&current, C_PUNCTUATOR_LEFT_BRACKET) ||
                    c_token_is_punctuator(&current, C_PUNCTUATOR_LEFT_BRACE))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&current, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&current, C_PUNCTUATOR_RIGHT_BRACKET) ||
                         c_token_is_punctuator(&current, C_PUNCTUATOR_RIGHT_BRACE))
                {
                    depth -= depth != 0;
                }
                else if (!depth && c_token_is_punctuator(&current, C_PUNCTUATOR_ASSIGN))
                {
                    initializer = cursor + 1;
                }
            }
            if (initializer < declaration_end)
            {
                if (c_token_is_punctuator(&preprocess.tokens[declaration_end - 1], C_PUNCTUATOR_SEMICOLON))
                {
                    declaration_end -= 1;
                }
                CType* element = record->element_type.value < result->type_count ? &result->types[record->element_type.value] : 0;
                bool element_is_aggregate = element && (element->kind == C_TYPE_STRUCT || element->kind == C_TYPE_UNION ||
                                                        element->kind == C_TYPE_ARRAY || element->kind == C_TYPE_VECTOR);
                if (element && c_ir_tokens_are_string_literals(preprocess, initializer, declaration_end))
                {
                    CIrDecodedString decoded = {0};
                    if (c_ir_decode_string_literal_range_for_target(arena, preprocess, preprocess.target, initializer, declaration_end, &decoded) &&
                        decoded.element_count != UINT64_MAX &&
                        c_parse_initializer_string_element_compatible(preprocess, result, record->element_type, decoded))
                    {
                        counted_items = decoded.element_count + 1;
                    }
                }
                else if (element)
                {
                    counted_items = c_parse_count_plain_initializer_items(preprocess, initializer, declaration_end, element_is_aggregate);
                }
                counted_element = record->element_type;
            }
        }
    }
    bool resolved;
    if (is_enumerator && index == end && !dereference_count && type.value == C_ID_UNDERLYING_INVALID)
    {
        // An enumerator has type int, which is 4 bytes with 4-byte alignment
        // on every supported target; the parse holds no ready-made int type
        // record to hand c_parse_type_layout here.
        *size_out = 4;
        *alignment_out = 4;
        resolved = true;
    }
    else if (counted_items)
    {
        u64 element_size = 0;
        u32 element_alignment = 0;
        resolved = c_parse_type_layout(0, arena, preprocess, result, counted_element, &element_size, &element_alignment) && element_size &&
                   element_size <= UINT64_MAX / counted_items;
        *size_out = element_size * counted_items;
        *alignment_out = element_alignment;
    }
    else
    {
        resolved = valid && type.value != C_ID_UNDERLYING_INVALID &&
                   c_parse_type_layout(0, arena, preprocess, result, type, size_out, alignment_out);
    }
    return resolved;
}

// Resolves the type name of a sizeof/_Alignof operand without the type-parse
// machine, for the constant expressions the machine itself evaluates while it
// runs. The machine is an explicit frame stack with one shared result slot, so
// a step cannot reenter it; this walks the type table directly instead.
// An operand that is not a type name at all falls through to
// c_parse_sizeof_operand_expression_layout above.
BUSTER_C_INTERNAL bool c_parse_machineless_sizeof_operand_layout(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                                   u32 start, u32 end, u64* size_out, u32* alignment_out)
{
    if (start >= end)
    {
        return false;
    }
    // `(T){...}` is a compound literal, and its size is T's. The base-type
    // walk below stops at the `(` and the expression walk after it wants an
    // object name, so without this arm neither answers the shape and the
    // caller reports the whole constant expression as unfoldable.
    if (c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 type_close = c_parse_matching_delimiter(preprocess, start, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        if (type_close + 1 < end && c_token_is_punctuator(&preprocess.tokens[type_close + 1], C_PUNCTUATOR_LEFT_BRACE) &&
            c_parse_matching_delimiter(preprocess, type_close + 1, end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE) == end - 1)
        {
            return c_parse_machineless_sizeof_operand_layout(arena, result, preprocess, scope, start + 1, type_close, size_out, alignment_out);
        }
    }
    // Resolving the operand can append pointer, array, and tag records; keep
    // them in a copy so an operand never mutates the caller's type table.
    CParseResult operand_parse = *result;
    u32 index = start;
    CTypeId type = c_parse_machineless_base_type(&operand_parse, preprocess, scope, start, end, &index);
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        return c_parse_sizeof_operand_expression_layout(arena, &operand_parse, preprocess, scope, start, end, size_out, alignment_out);
    }
    for (;;)
    {
        u32 pointer_start = index;
        type = c_parse_pointer_chain(&operand_parse, preprocess, type, &index, end);
        CType qualifiers = {0};
        while (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER &&
               c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[index], &qualifiers))
        {
            index += 1;
        }
        if (index == pointer_start)
        {
            break;
        }
    }
    type = c_parse_array_suffixes(&operand_parse, preprocess, type, &index, end);
    if (type.value == C_ID_UNDERLYING_INVALID || index != end)
    {
        return false;
    }
    return c_parse_type_layout(0, arena, preprocess, &operand_parse, type, size_out, alignment_out);
}

BUSTER_C_INTERNAL CTypeId c_parse_scalar_type_core_begin(CTypeParseMachine* machine, CTypeParseFrame* frame, u32* declarator_start)
{
    CParseResult* result = frame->result;
    CPreprocessResult preprocess = frame->preprocess;
    u32 start = frame->start;
    u32 end = frame->end;
    CTypeId qualified_typedef = c_parse_qualified_typedef_type(result, preprocess, frame->scope, start, end, declarator_start);
    if (qualified_typedef.value != C_ID_UNDERLYING_INVALID)
    {
        return qualified_typedef;
    }
    u32 aggregate_index = start;
    while (aggregate_index < end && preprocess.tokens[aggregate_index].kind == C_TOKEN_IDENTIFIER)
    {
        u32 attribute_end = c_parse_skip_alignment_specifiers(preprocess, aggregate_index, end);
        attribute_end = c_parse_skip_attributes(preprocess, attribute_end, end);
        if (attribute_end != aggregate_index)
        {
            aggregate_index = attribute_end;
            if (aggregate_index >= end)
            {
                break;
            }
            continue;
        }
        if (c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[aggregate_index], C_PARSE_AGGREGATE_KEYWORDS))
        {
            break;
        }
        if (!c_parse_type_word_for_dialect_token(preprocess, preprocess.tokens[aggregate_index]))
        {
            break;
        }
        aggregate_index += 1;
    }
    if (aggregate_index >= end || preprocess.tokens[aggregate_index].kind != C_TOKEN_IDENTIFIER ||
        !c_token_in_well_known_set(preprocess.spelling_base, preprocess.tokens[aggregate_index], C_PARSE_AGGREGATE_KEYWORDS))
    {
        if (aggregate_index == start && aggregate_index < end && preprocess.tokens[aggregate_index].kind == C_TOKEN_IDENTIFIER)
        {
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[aggregate_index]);
            // The old entity walk chose the oldest matching typedef, even when
            // a scoped lookup had already rejected the name. Keep that
            // fallback contract; the bucket helper validates partial parse
            // metadata before using the newest-first chain.
            CEntityId typedef_entity = c_parse_lookup_typedef_name_fallback(result, spelling);
            if (typedef_entity.value < result->entity_count)
            {
                CEntity* entity = &result->entities[typedef_entity.value];
                if (entity->kind == C_ENTITY_TYPEDEF)
                {
                    u32 qualifier_index = aggregate_index + 1;
                    CTypeId type = entity->type;
                    if (type.value >= result->type_count)
                    {
                        return C_TYPE_ID_INVALID;
                    }
                    CType qualified = result->types[type.value];
                    bool has_qualifier = false;
                    while (qualifier_index < end && preprocess.tokens[qualifier_index].kind == C_TOKEN_IDENTIFIER)
                    {
                        if (!c_parse_type_qualifier_word_token(preprocess, preprocess.tokens[qualifier_index], &qualified))
                        {
                            break;
                        }
                        has_qualifier = true;
                        qualifier_index += 1;
                    }
                    *declarator_start = qualifier_index;
                    return has_qualifier ? c_parse_add_qualified_type(result, type, qualified) : type;
                }
            }
        }
        return c_parse_primitive_type(result, preprocess, start, end, declarator_start);
    }
    String8 aggregate_spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[aggregate_index]);
    CTypeKind kind = string_equal(aggregate_spelling, S8("struct"))  ? C_TYPE_STRUCT
                     : string_equal(aggregate_spelling, S8("union")) ? C_TYPE_UNION
                                                                     : C_TYPE_ENUM;
    u32 index = c_parse_skip_attributes(preprocess, aggregate_index + 1, end);
    String8 tag = {0};
    if (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER)
    {
        tag = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
        index += 1;
    }
    index = c_parse_skip_attributes(preprocess, index, end);
    CTypeId enum_underlying_type = C_TYPE_ID_INVALID;
    if (kind == C_TYPE_ENUM && index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_COLON))
    {
        u32 underlying_start = index + 1;
        u32 underlying_end = underlying_start;
        while (underlying_end < end && !c_token_is_punctuator(&preprocess.tokens[underlying_end], C_PUNCTUATOR_LEFT_BRACE))
        {
            underlying_end += 1;
        }
        u32 underlying_declarator = underlying_start;
        enum_underlying_type = c_parse_qualified_typedef_type(result, preprocess, frame->scope, underlying_start, underlying_end, &underlying_declarator);
        if (enum_underlying_type.value == C_ID_UNDERLYING_INVALID)
        {
            enum_underlying_type = c_parse_primitive_type(result, preprocess, underlying_start, underlying_end, &underlying_declarator);
        }
        CType* underlying = enum_underlying_type.value < result->type_count ? &result->types[enum_underlying_type.value] : 0;
        bool integer_underlying = false;
        if (underlying)
        {
            switch (underlying->kind)
            {
            case C_TYPE_BOOL:
            case C_TYPE_CHAR:
            case C_TYPE_SIGNED_CHAR:
            case C_TYPE_UNSIGNED_CHAR:
            case C_TYPE_SHORT:
            case C_TYPE_UNSIGNED_SHORT:
            case C_TYPE_INT:
            case C_TYPE_UNSIGNED_INT:
            case C_TYPE_LONG:
            case C_TYPE_UNSIGNED_LONG:
            case C_TYPE_LONG_LONG:
            case C_TYPE_UNSIGNED_LONG_LONG:
            case C_TYPE_INT128:
            case C_TYPE_UNSIGNED_INT128:
            {
                integer_underlying = true;
                break;
            }
            case C_TYPE_INVALID:
            case C_TYPE_VOID:
            case C_TYPE_FLOAT:
            case C_TYPE_DOUBLE:
            case C_TYPE_LONG_DOUBLE:
            case C_TYPE_FLOAT_COMPLEX:
            case C_TYPE_DOUBLE_COMPLEX:
            case C_TYPE_LONG_DOUBLE_COMPLEX:
            case C_TYPE_VA_LIST:
            case C_TYPE_NULLPTR:
            case C_TYPE_POINTER:
            case C_TYPE_ARRAY:
            case C_TYPE_VECTOR:
            case C_TYPE_FUNCTION:
            case C_TYPE_STRUCT:
            case C_TYPE_UNION:
            case C_TYPE_ENUM:
            case C_TYPE_COUNT:
            {
                break;
            }
            }
        }
        if (!integer_underlying || underlying_declarator != underlying_end)
        {
            return C_TYPE_ID_INVALID;
        }
        index = underlying_end;
    }
    CTypeId type = c_parse_aggregate_lookup(result, kind, tag);
    bool definition = index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE);
    if (!definition)
    {
        if (!tag.length)
        {
            return C_TYPE_ID_INVALID;
        }
        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            type = c_parse_add_type(result, (CType){
                                                .tag = tag,
                                                .element_type = enum_underlying_type,
                                                .return_type = C_TYPE_ID_INVALID,
                                                .array_bound = C_ARRAY_BOUND_INVALID,
                                                .kind = kind,
                                            });
        }
        type = c_parse_apply_trailing_qualifiers(result, preprocess, type, &index, end);
        *declarator_start = index;
        return type;
    }
    u32 open = index;
    u32 depth = 1;
    index += 1;
    while (index < end && depth)
    {
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_BRACE))
        {
            depth -= 1;
        }
        index += 1;
    }
    if (depth)
    {
        return C_TYPE_ID_INVALID;
    }
    u32 close = index - 1;
    u32 definition_type_start = result->type_count;
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        type = c_parse_add_type(result, (CType){
                                            .tag = tag,
                                            .element_type = enum_underlying_type,
                                            .return_type = C_TYPE_ID_INVALID,
                                            .array_bound = C_ARRAY_BOUND_INVALID,
                                            .member_start = result->member_count,
                                            .kind = kind,
                                        });
    }
    else if (result->types[type.value].is_complete)
    {
        return C_TYPE_ID_INVALID;
    }
    if (type.value < definition_type_start && !c_type_parse_record_mutation(machine, result, type))
    {
        return C_TYPE_ID_INVALID;
    }
    CType* aggregate = &result->types[type.value];
    if (kind == C_TYPE_ENUM && enum_underlying_type.value != C_ID_UNDERLYING_INVALID)
    {
        if (aggregate->element_type.value != C_ID_UNDERLYING_INVALID && aggregate->element_type.value != enum_underlying_type.value)
        {
            return C_TYPE_ID_INVALID;
        }
        aggregate->element_type = enum_underlying_type;
    }
    aggregate->member_start = result->member_count;
    aggregate->definition_start = open + 1;
    aggregate->definition_token_count = close - (open + 1);
    if (kind == C_TYPE_ENUM)
    {
        aggregate->enum_member_start = result->enum_member_count;
        u32 enum_start = open + 1;
        s64 previous_value = -1;
        // Only a top-level comma separates enumerators. An initializer may
        // group commas of its own inside parentheses, brackets or braces --
        // `enum { E = (int)sizeof (struct S){1, 2}, F }` -- and splitting on
        // one of those cuts the initializer in half and fails the whole
        // definition.
        u32 enum_group_depth = 0;
        for (u32 token_index = enum_start; token_index <= close; token_index += 1)
        {
            if (token_index < close && c_punctuator_in_set(preprocess.tokens[token_index].punctuator, C_PUNCTUATOR_SET_DELIMITER_OPEN))
            {
                enum_group_depth += 1;
            }
            else if (token_index < close && c_punctuator_in_set(preprocess.tokens[token_index].punctuator, C_PUNCTUATOR_SET_DELIMITER_CLOSE))
            {
                enum_group_depth -= enum_group_depth != 0;
            }
            bool enum_end = token_index == close ||
                            (!enum_group_depth && c_token_is_punctuator(&preprocess.tokens[token_index], C_PUNCTUATOR_COMMA));
            if (!enum_end)
            {
                continue;
            }
            if (enum_start == token_index)
            {
                enum_start = token_index + 1;
                continue;
            }
            CToken name = preprocess.tokens[enum_start];
            if (name.kind != C_TOKEN_IDENTIFIER)
            {
                return C_TYPE_ID_INVALID;
            }
            // An enumerator carries its attributes between the name and the
            // '=', or after the name when it has no value at all:
            // `enum { A [[deprecated]] = 1, B [[deprecated]] }`. The commas
            // inside an attribute list are already covered, because the
            // enumerator split above counts '[' as an opening delimiter.
            u32 enum_value_index = c_parse_skip_attributes(preprocess, enum_start + 1, token_index);
            s64 value = previous_value + 1;
            if (enum_value_index < token_index)
            {
                if (!c_token_is_punctuator(&preprocess.tokens[enum_value_index], C_PUNCTUATOR_ASSIGN))
                {
                    return C_TYPE_ID_INVALID;
                }
                u32 expression_start = enum_value_index + 1;
                u32 expression_count = token_index - expression_start;
                TemporalArena temporary = scratch_begin(0, 0);
                CToken* evaluation_tokens = arena_allocate(temporary.arena, CToken, expression_count * 2 + 1);
                u32 evaluation_token_count = 0;
                u64 enum_spelling_capacity = 0;
                for (u32 expression_index = 0; expression_index < expression_count; expression_index += 1)
                {
                    enum_spelling_capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[expression_start + expression_index]) + 21;
                }
                CSpellingSpace enum_space = c_space_local(temporary.arena, enum_spelling_capacity);
                for (u32 expression_index = 0; expression_index < expression_count; expression_index += 1)
                {
                    u32 source_index = expression_start + expression_index;
                    CToken expression_token = preprocess.tokens[source_index];
                    if (c_token_is_punctuator(&expression_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        u32 cast_end = expression_index + 1;
                        bool cast_type = false;
                        bool cast_valid = true;
                        bool tag_name = false;
                        while (cast_end < expression_count &&
                               !c_token_is_punctuator(&preprocess.tokens[expression_start + cast_end], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                        {
                            CToken cast_token = preprocess.tokens[expression_start + cast_end];
                            if (cast_token.kind == C_TOKEN_IDENTIFIER)
                            {
                                bool typedef_name = false;
                                for (u32 entity_index = 0; entity_index < result->entity_count; entity_index += 1)
                                {
                                    CEntity* entity = &result->entities[entity_index];
                                    if (entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, c_token_spelling(preprocess.spelling_base, cast_token)))
                                    {
                                        typedef_name = true;
                                        break;
                                    }
                                }
                                bool type_word = c_parse_type_word_for_dialect_token(preprocess, cast_token);
                                cast_valid &= type_word || typedef_name || tag_name;
                                cast_type |= type_word || typedef_name;
                                tag_name = string_equal(c_token_spelling(preprocess.spelling_base, cast_token), S8("struct")) || string_equal(c_token_spelling(preprocess.spelling_base, cast_token), S8("union")) ||
                                           string_equal(c_token_spelling(preprocess.spelling_base, cast_token), S8("enum"));
                            }
                            else
                            {
                                cast_valid &= c_token_is_punctuator(&cast_token, C_PUNCTUATOR_STAR);
                                tag_name = false;
                            }
                            cast_end += 1;
                        }
                        if (cast_valid && cast_type && cast_end < expression_count && cast_end + 1 < expression_count)
                        {
                            expression_index = cast_end;
                            continue;
                        }
                    }
                    // sizeof/_Alignof has to be folded here: the evaluator below
                    // is the preprocessor's, which reads both words as ordinary
                    // identifiers and substitutes zero for them.
                    bool enum_word_is_alignof = expression_token.kind == C_TOKEN_IDENTIFIER && c_parse_alignof_word(c_token_spelling(preprocess.spelling_base, expression_token));
                    if (expression_token.kind == C_TOKEN_IDENTIFIER &&
                        (enum_word_is_alignof || string_equal(c_token_spelling(preprocess.spelling_base, expression_token), S8("sizeof"))))
                    {
                        u32 expression_end = expression_start + expression_count;
                        bool parenthesized = expression_index + 2 < expression_count &&
                                             c_token_is_punctuator(&preprocess.tokens[source_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
                        u32 operand_start = source_index + 2;
                        u32 operand_end = operand_start;
                        u32 operand_depth = 1;
                        while (parenthesized && operand_end < expression_end)
                        {
                            CToken operand_token = preprocess.tokens[operand_end];
                            if (c_token_is_punctuator(&operand_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                            {
                                operand_depth += 1;
                            }
                            else if (c_token_is_punctuator(&operand_token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                            {
                                operand_depth -= 1;
                                if (!operand_depth)
                                {
                                    break;
                                }
                            }
                            operand_end += 1;
                        }
                        // `sizeof (T){...}` sizes the compound literal. The
                        // size is the same as the type name's, but the
                        // initializer has to be consumed here too or it stays
                        // in the retokenized integer expression and fails to
                        // evaluate, taking the whole enum definition with it.
                        u32 literal_close = UINT32_MAX;
                        if (parenthesized && !operand_depth && operand_end + 1 < expression_end &&
                            c_token_is_punctuator(&preprocess.tokens[operand_end + 1], C_PUNCTUATOR_LEFT_BRACE))
                        {
                            u32 initializer_close =
                                c_parse_matching_delimiter(preprocess, operand_end + 1, expression_end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE);
                            literal_close = initializer_close < expression_end ? initializer_close : UINT32_MAX;
                        }
                        u64 operand_size = 0;
                        u32 operand_alignment = 0;
                        if (!parenthesized || operand_depth || operand_end == operand_start ||
                            !c_parse_machineless_sizeof_operand_layout(temporary.arena, result, preprocess, frame->scope, operand_start, operand_end,
                                                                       &operand_size, &operand_alignment))
                        {
                            // Substituting zero here would silently misfold the
                            // enumerator, so an unresolved operand fails instead.
                            scratch_end(temporary);
                            return C_TYPE_ID_INVALID;
                        }
                        evaluation_tokens[evaluation_token_count++] =
                            c_space_token(&enum_space, string_format(temporary.arena, S8("{u64}"), enum_word_is_alignof ? operand_alignment : operand_size),
                                          C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
                        expression_index = (literal_close != UINT32_MAX ? literal_close : operand_end) - expression_start;
                        continue;
                    }
                    bool folded_member = false;
                    if (expression_token.kind == C_TOKEN_IDENTIFIER)
                    {
                        for (u32 member_index = 0; member_index < result->enum_member_count; member_index += 1)
                        {
                            CEnumMember* member = &result->enum_members[member_index];
                            if (!string_equal(member->name, c_token_spelling(preprocess.spelling_base, expression_token)))
                            {
                                continue;
                            }
                            if (member->is_negative)
                            {
                                evaluation_tokens[evaluation_token_count++] = c_space_token(&enum_space, S8("-"), C_TOKEN_PUNCTUATOR, C_PUNCTUATOR_MINUS);
                            }
                            evaluation_tokens[evaluation_token_count++] = c_space_token(&enum_space, string_format(temporary.arena, S8("{u64}"), member->value),
                                                                                        C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
                            folded_member = true;
                            break;
                        }
                    }
                    if (!folded_member)
                    {
                        evaluation_tokens[evaluation_token_count++] = c_space_retoken(&enum_space, preprocess.spelling_base, expression_token);
                    }
                }
                CPreprocessResult evaluation = {
                    .diagnostics = arena_allocate(temporary.arena, CDiagnostic, evaluation_token_count + 1),
                    .target = preprocess.target,
                    .dialect = preprocess.dialect,
                };
                u64 evaluated = 0;
                bool valid = c_integer_expression_evaluate(temporary.arena, enum_space.base, evaluation_tokens, evaluation_token_count, 65536, &evaluation, &evaluated);
                scratch_end(temporary);
                if (!valid || evaluation.diagnostic_count)
                {
                    return C_TYPE_ID_INVALID;
                }
                if (evaluated <= INT64_MAX)
                {
                    value = (s64)evaluated;
                }
                else
                {
                    value = -1 - (s64)(UINT64_MAX - evaluated);
                }
            }
            BUSTER_CHECK(result->enum_member_count < result->enum_member_capacity);
            result->enum_members[result->enum_member_count++] = (CEnumMember){
                .name = c_token_spelling(preprocess.spelling_base, name),
                .location = c_preprocess_token_location(&preprocess, name),
                .value = value < 0 ? (u64)(-value) : (u64)value,
                .is_negative = value < 0,
            };
            previous_value = value;
            enum_start = token_index + 1;
        }
    }
    if (kind != C_TYPE_ENUM)
    {
        // The definition's own attributes sit on either side of the body:
        // `struct __attribute__((packed)) P { ... }` writes them between the
        // keyword and the brace, `struct P { ... } __attribute__((packed));`
        // after the closing one. Both ranges are scanned back to back so the
        // alignment records they append stay one contiguous run.
        bool is_packed = false;
        u32 attribute_alignment_start = 0;
        u32 attribute_alignment_count = 0;
        u32 suffix_end = c_parse_skip_attributes(preprocess, close + 1, end);
        c_parse_layout_attributes(result, preprocess, aggregate_index + 1, open, &is_packed, &attribute_alignment_start, &attribute_alignment_count);
        {
            bool suffix_packed = false;
            u32 suffix_alignment_start = 0;
            u32 suffix_alignment_count = 0;
            c_parse_layout_attributes(result, preprocess, close + 1, suffix_end, &suffix_packed, &suffix_alignment_start, &suffix_alignment_count);
            is_packed |= suffix_packed;
            if (!attribute_alignment_count)
            {
                attribute_alignment_start = suffix_alignment_start;
            }
            attribute_alignment_count += suffix_alignment_count;
        }
        if (is_packed || attribute_alignment_count)
        {
            // One entry per aggregate, rewritten rather than appended when
            // the definition is parsed again: the type machine rolls a
            // speculative parse back and retries it, and a table that grew on
            // every attempt could outrun the one-per-definition bound its
            // capacity is sized to and start dropping the answer.
            u32 record_index = 0;
            while (record_index < result->aggregate_attribute_count && result->aggregate_attributes[record_index].type_index != type.value)
            {
                record_index += 1;
            }
            if (record_index < result->aggregate_attribute_count || result->aggregate_attribute_count < result->aggregate_attribute_capacity)
            {
                result->aggregate_attribute_count += record_index == result->aggregate_attribute_count;
                result->aggregate_attributes[record_index] = (CAggregateAttributes){
                    .type_index = type.value,
                    .alignment_start = attribute_alignment_start,
                    .alignment_count = attribute_alignment_count,
                    .is_packed = is_packed,
                };
            }
        }
        frame->type = type;
        frame->original_type_id = type;
        frame->original_type_valid = true;
        frame->definition_type_start = definition_type_start;
        frame->pending_index = definition_type_start;
        frame->close = close;
        frame->has_function_suffix = true;
        *declarator_start = close + 1;
        return type;
    }
    aggregate = &result->types[type.value];
    aggregate->member_count = result->member_count - aggregate->member_start;
    aggregate->enum_member_count = result->enum_member_count - aggregate->enum_member_start;
    aggregate->is_complete = true;
    c_parse_validate_flexible_array_members(result, aggregate);
    u32 enum_declarator_start = close + 1;
    type = c_parse_apply_trailing_qualifiers(result, preprocess, type, &enum_declarator_start, end);
    *declarator_start = enum_declarator_start;
    return type;
}

BUSTER_C_SHARED CTypeId c_parse_pointer_chain(CParseResult* result, CPreprocessResult preprocess, CTypeId base, u32* index, u32 end)
{
    while (*index < end && c_token_is_punctuator(&preprocess.tokens[*index], C_PUNCTUATOR_STAR))
    {
        *index += 1;
        CType pointer = {
            .element_type = base,
            .return_type = C_TYPE_ID_INVALID,
            .array_bound = C_ARRAY_BOUND_INVALID,
            .kind = C_TYPE_POINTER,
        };
        while (*index < end && preprocess.tokens[*index].kind == C_TOKEN_IDENTIFIER)
        {
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[*index]);
            if (c_parse_type_qualifier_word(spelling, &pointer))
            {
            }
            else if (string_equal(spelling, S8("_Nonnull")) || string_equal(spelling, S8("_Nullable")) || string_equal(spelling, S8("_Null_unspecified")))
            {
                /* Nullability affects diagnostics, not the C object
                   representation. */
            }
            else
            {
                break;
            }
            *index += 1;
        }
        base = c_parse_add_type(result, pointer);
    }
    return base;
}

BUSTER_C_INTERNAL bool c_parse_parenthesized_declarator_name(CPreprocessResult preprocess, u32 declarator_start, u32 end, u32* name_index)
{
    if (declarator_start >= end || !c_token_is_punctuator(&preprocess.tokens[declarator_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 index = declarator_start + 1;
    while (index < end)
    {
        while (index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_STAR))
        {
            index += 1;
            CType ignored = {0};
            while (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER)
            {
                String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
                if (c_parse_type_qualifier_word(spelling, &ignored) || string_equal(spelling, S8("_Nonnull")) || string_equal(spelling, S8("_Nullable")) ||
                    string_equal(spelling, S8("_Null_unspecified")))
                {
                    index += 1;
                    continue;
                }
                break;
            }
        }
        // A declarator group may hold another one: the name of
        // `void (*(*xDlSym)(sqlite3_vfs*, void*, const char*))(void)` is two
        // groups deep, and it is still the one name the declarator declares.
        if (index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            index += 1;
            continue;
        }
        break;
    }
    // Parenthesized declarators may redundantly wrap a plain function name
    // (`extern int (f)(void)`) as well as a function pointer (`int (*f)`).
    // Requiring a `*` here drops the API declarations used by Lua and system
    // headers, leaving every call looking undeclared.
    if (index >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER)
    {
        return false;
    }
    *name_index = index;
    return true;
}

// Return the name of a parenthesized function declarator (`(f)(...)` or
// `(*f)(...)`).  The parser's declaration scan normally recognizes a function
// from the identifier immediately before its parameter list; a redundant
// pair of parentheses moves that identifier inside the first group, so keep
// this small shape test shared by the AST and semantic declaration passes.
BUSTER_C_INTERNAL bool c_parse_parenthesized_function_name(CPreprocessResult preprocess, u32 start, u32 end, u32* name_index)
{
    if (start >= end || !c_token_is_punctuator(&preprocess.tokens[start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    u32 candidate = 0;
    if (!c_parse_parenthesized_declarator_name(preprocess, start, end, &candidate))
    {
        return false;
    }
    u32 depth = 1;
    u32 close = start + 1;
    while (close < end && depth)
    {
        if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            depth -= 1;
        }
        close += 1;
    }
    if (depth || close >= end || !c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    *name_index = candidate;
    return true;
}

// A pointer declarator whose name carries its own parameter list is a
// function that returns the pointer -- `void (*getf(int))(void)` -- rather
// than an object holding one.  `void (*fp)(void)` and `void (*table[3])(void)`
// put a `)` or a `[` after the name instead, so the parameter list is exactly
// what separates the two shapes.
BUSTER_C_INTERNAL bool c_parse_declarator_name_has_parameters(CPreprocessResult preprocess, u32 name_index, u32 end)
{
    return name_index + 1 < end && c_token_is_punctuator(&preprocess.tokens[name_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS);
}

BUSTER_C_SHARED CTypeId c_parse_array_suffixes(CParseResult* result, CPreprocessResult preprocess, CTypeId element_type, u32* index, u32 end)
{
    u32 first_bound = result->array_bound_count;
    while (*index < end && c_token_is_punctuator(&preprocess.tokens[*index], C_PUNCTUATOR_LEFT_BRACKET))
    {
        u32 open = *index;
        u32 depth = 1;
        *index += 1;
        while (*index < end && depth)
        {
            CToken token = preprocess.tokens[*index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
            {
                depth -= 1;
            }
            *index += 1;
        }
        if (depth)
        {
            result->array_bound_count = first_bound;
            return C_TYPE_ID_INVALID;
        }
        u32 bound_start = open + 1;
        u32 bound_count = *index - bound_start - 1;
        bool is_static = false;
        for (u32 token_index = bound_start; token_index < bound_start + bound_count; token_index += 1)
        {
            is_static |= string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]), S8("static"));
        }
        bool is_star = bound_count == 1 && c_token_is_punctuator(&preprocess.tokens[bound_start], C_PUNCTUATOR_STAR);
        if (!c_parse_result_reserve_array_bounds(result, 1))
        {
            result->array_bound_count = first_bound;
            return C_TYPE_ID_INVALID;
        }
        result->array_bounds[result->array_bound_count++] = (CArrayBound){
            .token_start = bound_start,
            .token_count = bound_count,
            .is_static = is_static,
            .is_star = is_star,
        };
    }
    for (u32 bound_index = result->array_bound_count; bound_index > first_bound; bound_index -= 1)
    {
        element_type = c_parse_add_type(result, (CType){
                                                    .element_type = element_type,
                                                    .return_type = C_TYPE_ID_INVALID,
                                                    .array_bound = bound_index - 1,
                                                    .kind = C_TYPE_ARRAY,
                                                });
    }
    return element_type;
}

BUSTER_C_INTERNAL bool c_parse_parameter_segment(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                    CDeclaration declaration, u32 start, u32 end)
{
    BUSTER_UNUSED(declaration);
    for (u32 token_index = start; token_index < end; token_index += 1)
    {
        if (preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]), S8("_Alignas")))
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[token_index]), C_DIAGNOSTIC_INVALID_ALIGNMENT,
                               S8("alignment specifier cannot be applied to a parameter"));
            return false;
        }
    }
    u32 derived_start = result->type_count;
    u32 declarator_start = start;
    CTypeId type = c_parse_scalar_type(machine, result, preprocess, start, end, &declarator_start);
    if (type.value == C_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    type = c_parse_pointer_chain(result, preprocess, type, &declarator_start, end);
    type = c_parse_apply_vector_attribute(result, preprocess, declaration.scope, type, start, end);
    CToken name = {0};
    if (declarator_start < end && c_token_is_punctuator(&preprocess.tokens[declarator_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 name_index = 0;
        bool has_name = c_parse_parenthesized_declarator_name(preprocess, declarator_start, end, &name_index);
        if (!has_name)
        {
            u32 close_index = declarator_start + 1;
            CType ignored = {0};
            while (close_index < end && c_token_is_punctuator(&preprocess.tokens[close_index], C_PUNCTUATOR_STAR))
            {
                close_index += 1;
                while (close_index < end && preprocess.tokens[close_index].kind == C_TOKEN_IDENTIFIER &&
                       c_parse_type_qualifier_word(c_token_spelling(preprocess.spelling_base, preprocess.tokens[close_index]), &ignored))
                {
                    close_index += 1;
                }
            }
            if (close_index == declarator_start + 1 || close_index >= end ||
                !c_token_is_punctuator(&preprocess.tokens[close_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                return false;
            }
            name_index = close_index;
        }
        if (has_name)
        {
            name = preprocess.tokens[name_index];
        }
        type = c_parse_parenthesized_declaration_type(machine, result, preprocess, type, declarator_start, name_index, end, has_name);
        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            return false;
        }
        declarator_start = end;
    }
    if (declarator_start < end && preprocess.tokens[declarator_start].kind == C_TOKEN_IDENTIFIER)
    {
        name = preprocess.tokens[declarator_start];
        declarator_start += 1;
    }
    declarator_start = c_parse_skip_attributes(preprocess, declarator_start, end);
    type = c_parse_array_suffixes(result, preprocess, type, &declarator_start, end);
    declarator_start = c_parse_skip_attributes(preprocess, declarator_start, end);
    if (declarator_start != end || type.value == C_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    // A parameter declarator may spell the marker on the function type it
    // derives, `void run(__attribute__((noreturn)) void (*fail)(int))`, and
    // the call through it has only the type to read.
    CTypeId noreturn_function = c_parse_noreturn_candidate_function_type(result, type, true, derived_start);
    if (noreturn_function.value != C_ID_UNDERLYING_INVALID && c_ir_noreturn_marker_in_range(preprocess, start, end))
    {
        c_parse_add_noreturn_function_type(result, noreturn_function);
    }
    BUSTER_CHECK(result->parameter_count < result->parameter_capacity);
    result->parameters[result->parameter_count++] = (CParameter){
        .name = c_token_spelling(preprocess.spelling_base, name),
        .location = c_preprocess_token_location(&preprocess, name),
        .type = type,
        .entity = C_ENTITY_ID_INVALID,
    };
    return true;
}

BUSTER_C_INTERNAL CTypeId c_parse_parenthesized_declaration_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                                   CTypeId base, u32 declarator_start, u32 name_index, u32 suffix_end,
                                                                   bool has_name)
{
    u32 frame_start = machine->frame_count;
    CParseResult checkpoint = *result;
    u32 mutation_mark = machine->mutation_count;
    machine->mutation_type_limit = checkpoint.type_count;
    machine->result_valid = false;
    bool pushed = c_type_parse_frame_push(machine, (CTypeParseFrame){
                                              .result = result,
                                              .preprocess = preprocess,
                                              .type = base,
                                              .start = declarator_start,
                                              .end = suffix_end,
                                              .declarator_start = declarator_start,
                                              .name_index = name_index,
                                              .kind = C_TYPE_PARSE_FRAME_PARENTHESIZED,
                                              .has_name = has_name,
                                          });
    if (pushed)
    {
        c_type_parse_machine_run(machine, frame_start);
    }
    bool valid = c_type_parse_root_finish(machine, result, checkpoint, mutation_mark,
                                          declarator_start < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[declarator_start]) : (CSourceLocation){0});
    return valid ? machine->result_type : C_TYPE_ID_INVALID;
}

BUSTER_C_INTERNAL u32 c_parse_declarator_segment_end(CPreprocessResult preprocess, u32 start, u32 end)
{
    u32 delimiter_depth = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            delimiter_depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            delimiter_depth -= delimiter_depth != 0;
        }
        else if (!delimiter_depth && (c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA) ||
                                      c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON)))
        {
            return index;
        }
    }
    return end;
}

BUSTER_C_INTERNAL bool c_parse_auto_type_token_in_declaration(CPreprocessResult preprocess, u32 start, u32 end, u32* token_index_out)
{
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_IDENTIFIER && c_parse_auto_type_word_token(preprocess, token))
        {
            *token_index_out = index;
            return true;
        }
    }
    *token_index_out = UINT32_MAX;
    return false;
}

// `inherited_base` carries the declaration specifiers already parsed for the
// first declarator of a comma-separated list; it is invalid for every other
// declaration, which parses its own specifiers. Reusing the type matters
// beyond the saved work: "struct { int x; } a, b;" must give a and b the one
// anonymous type, not one per declarator.
BUSTER_C_INTERNAL void c_parse_declaration_type_derive(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                        CDeclaration* declaration, CTypeId inherited_base)
{
    u32 end = declaration->declarator_count ? declaration->declarator_start + declaration->declarator_count
                                            : declaration->token_start + declaration->token_count;
    u32 auto_declaration_end = declaration->body_start ? declaration->body_start - 1 : end;
    u32 auto_token_index = UINT32_MAX;
    if (c_parse_auto_type_token_in_declaration(preprocess, declaration->token_start, auto_declaration_end, &auto_token_index))
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_token_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                           c_preprocess_dialect_is_gnu(preprocess.dialect)
                               ? S8("GNU __auto_type is supported only for one initialized automatic object declaration")
                               : S8("GNU __auto_type is only available in GNU dialects"));
        return;
    }
    if (declaration->kind == C_DECLARATION_TYPE)
    {
        u32 declarator_start = declaration->token_start;
        declaration->type = c_parse_scalar_type(machine, result, preprocess, declaration->token_start, end, &declarator_start);
        return;
    }
    u32 declarator_end = declaration->body_start ? declaration->body_start - 1 : end;
    u32 name_search_start = declaration->declarator_count ? declaration->declarator_start : declaration->token_start;
    // The scan below keeps the last occurrence of the name so that a tag
    // repeating it (`struct head { ... } head;`) does not win over the
    // declarator. An initializer may repeat it too — an object is in scope
    // inside its own initializer, which is how `TAILQ_HEAD_INITIALIZER` names
    // the list it initializes — and that occurrence is not a declarator, so
    // stop the search at the top-level '=' that starts the initializer.
    u32 name_search_end = declarator_end;
    for (u32 index = name_search_start, depth = 0; index < declarator_end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            depth -= depth ? 1 : 0;
        }
        else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
        {
            name_search_end = index;
            break;
        }
    }
    u32 name_index = declarator_end;
    for (u32 index = name_search_start; index < name_search_end; index += 1)
    {
        if (preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), declaration->name))
        {
            if (declaration->kind != C_DECLARATION_FUNCTION ||
                (index + 1 < end && c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS)))
            {
                name_index = index;
            }
        }
    }
    // A function declaration may parenthesize its name before the parameter
    // list (`extern int (f)(void)` or `extern int (*f)(void)`).  In that shape
    // the identifier is followed by `)` rather than `(`, so the ordinary name
    // scan above intentionally skips it.  Recover the candidate from the
    // parenthesized declarator helper and let the same type machine build the
    // function (or function-pointer) type.
    if (name_index == declarator_end && declaration->kind == C_DECLARATION_FUNCTION)
    {
        for (u32 index = name_search_start; index < name_search_end; index += 1)
        {
            if (!c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                continue;
            }
            u32 candidate = C_ID_UNDERLYING_INVALID;
            if (c_parse_parenthesized_function_name(preprocess, index, name_search_end, &candidate) && candidate < name_search_end &&
                string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[candidate]), declaration->name))
            {
                name_index = candidate;
                break;
            }
        }
    }
    if (name_index != declarator_end)
    {
        if (!c_parse_alignment_specifiers(machine, result, preprocess, declaration->token_start, name_index, &declaration->alignment_start,
                                          &declaration->alignment_count))
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[declaration->token_start]), C_DIAGNOSTIC_INVALID_ALIGNMENT, S8("invalid alignment specifier"));
            return;
        }
        if (declaration->alignment_count && (declaration->kind == C_DECLARATION_FUNCTION || declaration->kind == C_DECLARATION_TYPEDEF))
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[declaration->token_start]), C_DIAGNOSTIC_INVALID_ALIGNMENT,
                               declaration->kind == C_DECLARATION_FUNCTION ? S8("alignment specifier cannot be applied to a function")
                                                                           : S8("alignment specifier cannot be applied to a typedef"));
            declaration->alignment_count = 0;
        }
        // A GNU `aligned` attribute may also follow the declarator
        // (`static char buf[8] __attribute__((aligned(64)));`), which is where
        // musl's thread-local buffers and SQLite's aligned scratch write it.
        // The scan runs right after the specifier one so both runs land in the
        // single contiguous range alignment_start/alignment_count name, and it
        // stops at the initializer, which is expression territory.
        if (declaration->kind == C_DECLARATION_OBJECT && name_index + 1 < name_search_end)
        {
            u32 trailing_start = 0;
            u32 trailing_count = 0;
            c_parse_layout_attributes(result, preprocess, name_index + 1, name_search_end, 0, &trailing_start, &trailing_count);
            if (!declaration->alignment_count)
            {
                declaration->alignment_start = trailing_start;
            }
            declaration->alignment_count += trailing_count;
        }
        u32 declarator_start = declaration->token_start;
        CTypeId base = inherited_base;
        if (base.value == C_ID_UNDERLYING_INVALID)
        {
            base = c_parse_scalar_type(machine, result, preprocess, declaration->token_start, name_index, &declarator_start);
            base = c_parse_apply_vector_attribute(result, preprocess, declaration->scope, base, declaration->token_start, name_index);
        }
        else
        {
            declarator_start = declaration->declarator_start;
        }
        if (base.value != C_ID_UNDERLYING_INVALID)
        {
            declaration->base_type = base;
            // GNU attributes may sit between the specifiers and the declarator
            // (`Coord2 __attribute__((stdcall)) f(...)`); the scalar-type parse stops
            // before them, so hop over them here or the declarator is never reached.
            declarator_start = c_parse_skip_attributes(preprocess, declarator_start, name_index);
            base = c_parse_pointer_chain(result, preprocess, base, &declarator_start, name_index);
            if (base.value == C_ID_UNDERLYING_INVALID)
            {
                return;
            }
            declarator_start = c_parse_skip_attributes(preprocess, declarator_start, name_index);
            bool parenthesized = declarator_start < name_index && c_token_is_punctuator(&preprocess.tokens[declarator_start], C_PUNCTUATOR_LEFT_PARENTHESIS);
            if (parenthesized)
            {
                // A definition's declarator stops at its body: the segment scan
                // sees no top-level `=`, `,` or `;` inside a brace-balanced
                // body, so bounding it by the whole declaration would hand the
                // declarator parser the function body as a declarator suffix
                // and fail every parenthesized definition.
                u32 suffix_end = c_parse_declarator_segment_end(preprocess, declarator_start, declarator_end);
                declaration->type =
                    c_parse_parenthesized_declaration_type(machine, result, preprocess, base, declarator_start, name_index, suffix_end, true);
                // The parenthesized declarator parser owns the parameter
                // records it creates, but the declaration still needs to
                // point at that range for function signatures and body
                // parameter entities.  The ordinary `name(...)` path fills
                // these fields alongside the function CType; mirror that
                // handoff for `(<name>)(...)` while leaving function-pointer
                // object declarators untouched.
                if (declaration->kind == C_DECLARATION_FUNCTION && declaration->type.value < result->type_count)
                {
                    CType* function_type = result->types + declaration->type.value;
                    if (function_type->kind == C_TYPE_FUNCTION)
                    {
                        declaration->parameter_start = function_type->parameter_start;
                        declaration->parameter_count = function_type->parameter_count;
                        declaration->is_variadic = function_type->is_variadic;
                    }
                }
                return;
            }
            if (declarator_start == name_index)
            {
                bool function_declarator =
                    declaration->kind == C_DECLARATION_FUNCTION || (declaration->kind == C_DECLARATION_TYPEDEF && name_index + 1 < end &&
                                                                    c_token_is_punctuator(&preprocess.tokens[name_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS));
                if (!function_declarator)
                {
                    // The declarator suffix ends at the first top-level '=', ','
                    // or ';', and only at a top-level one: an array bound may
                    // carry commas of its own inside brackets, parentheses or
                    // braces (`char pad[sizeof (int[3]){1, 2, 3}];`). Stopping
                    // at the first comma anywhere hands
                    // c_parse_array_suffixes an unterminated '[', which fails
                    // the declarator and drops the declaration. The
                    // depth-aware scan is the same one the block-scope path
                    // uses.
                    u32 suffix_end = c_parse_declarator_segment_end(preprocess, name_index + 1, end);
                    u32 suffix_index = name_index + 1;
                    base = c_parse_apply_vector_attribute(result, preprocess, declaration->scope, base, name_index + 1, suffix_end);
                    suffix_index = c_parse_skip_attributes(preprocess, suffix_index, suffix_end);
                    base = c_parse_array_suffixes(result, preprocess, base, &suffix_index, suffix_end);
                    suffix_index = c_parse_skip_attributes(preprocess, suffix_index, suffix_end);
                    if (suffix_index != suffix_end || base.value == C_ID_UNDERLYING_INVALID)
                    {
                        return;
                    }
                    declaration->type = base;
                    return;
                }
                u32 parameter_start = result->parameter_count;
                u32 reserved_parameter_count = 0;
                {
                    u32 count_start = name_index + 2;
                    u32 count_depth = 1;
                    for (u32 count_index = count_start; count_index < end; count_index += 1)
                    {
                        CToken count_token = preprocess.tokens[count_index];
                        if (c_token_is_punctuator(&count_token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                        {
                            count_depth += 1;
                            continue;
                        }
                        if (c_token_is_punctuator(&count_token, C_PUNCTUATOR_RIGHT_PARENTHESIS) && count_depth > 1)
                        {
                            count_depth -= 1;
                            continue;
                        }
                        bool count_separator = count_depth == 1 && c_token_is_punctuator(&count_token, C_PUNCTUATOR_COMMA);
                        bool count_end = count_depth == 1 && c_token_is_punctuator(&count_token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                        if (!count_separator && !count_end)
                        {
                            continue;
                        }
                        u32 count = count_index - count_start;
                        bool omitted =
                            !count || (count == 1 && (c_token_is_punctuator(&preprocess.tokens[count_start], C_PUNCTUATOR_ELLIPSIS) ||
                                                      (count_end && !reserved_parameter_count && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[count_start]), S8("void")))));
                        reserved_parameter_count += omitted ? 0 : 1;
                        if (count_end)
                        {
                            break;
                        }
                        count_start = count_index + 1;
                    }
                }
                result->parameter_count += reserved_parameter_count;
                u32 written_parameter_count = 0;
                u32 segment_start = name_index + 2;
                u32 depth = 1;
                bool valid = true;
                bool unprototyped = false;
                for (u32 index = segment_start; index < end; index += 1)
                {
                    CToken token = preprocess.tokens[index];
                    if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        depth += 1;
                        continue;
                    }
                    if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                    {
                        if (depth > 1)
                        {
                            depth -= 1;
                            continue;
                        }
                    }
                    bool segment_end = depth == 1 && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA);
                    bool list_end = depth == 1 && c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                    if (!segment_end && !list_end)
                    {
                        continue;
                    }
                    u32 segment_count = index - segment_start;
                    if (list_end && !segment_count && result->parameter_count == parameter_start)
                    {
                        unprototyped = true;
                        break;
                    }
                    if (segment_count == 1 && c_token_is_punctuator(&preprocess.tokens[segment_start], C_PUNCTUATOR_ELLIPSIS))
                    {
                        declaration->is_variadic = true;
                    }
                    else if (list_end && result->parameter_count == parameter_start && segment_count == 1 &&
                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]), S8("void")))
                    {
                    }
                    else if (!segment_count)
                    {
                        valid = false;
                        break;
                    }
                    else
                    {
                        if (!c_parse_parameter_segment(machine, result, preprocess, *declaration, segment_start, index))
                        {
                            valid = false;
                            break;
                        }
                        if (written_parameter_count >= reserved_parameter_count)
                        {
                            valid = false;
                            break;
                        }
                        result->parameters[parameter_start + written_parameter_count++] = result->parameters[result->parameter_count - 1];
                        result->parameter_count -= 1;
                    }
                    if (list_end)
                    {
                        break;
                    }
                    segment_start = index + 1;
                }
                if (!valid)
                {
                    result->parameter_count = parameter_start;
                    return;
                }
                declaration->parameter_start = parameter_start;
                declaration->parameter_count = written_parameter_count;
                declaration->type = c_parse_add_type(result, (CType){
                                                                 .element_type = C_TYPE_ID_INVALID,
                                                                 .return_type = base,
                                                                 .array_bound = C_ARRAY_BOUND_INVALID,
                                                                 .parameter_start = parameter_start,
                                                                 .parameter_count = declaration->parameter_count,
                                                                 .kind = C_TYPE_FUNCTION,
                                                                 .is_variadic = declaration->is_variadic,
                                                                 .is_unprototyped = unprototyped,
                                                             });
            }
        }
    }
}

// A declarator may spell `noreturn` on the function type it derives rather
// than on a function it declares -- the typedef and function-pointer shapes,
// where the call site sees only the type. The type has to be noted while the
// declarator that built it is still in hand, so the derivation runs first and
// the marker is read against the declaration it was written in.
BUSTER_C_SHARED void c_parse_declaration_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                  CDeclaration* declaration, CTypeId inherited_base)
{
    u32 derived_start = result->type_count;
    c_parse_declaration_type_derive(machine, result, preprocess, declaration, inherited_base);
    CTypeId function = c_parse_noreturn_candidate_function_type(result, declaration->type, declaration->kind == C_DECLARATION_TYPEDEF, derived_start);
    if (function.value != C_ID_UNDERLYING_INVALID && c_ir_declaration_is_noreturn(preprocess, *declaration))
    {
        c_parse_add_noreturn_function_type(result, function);
    }
}

BUSTER_C_INTERNAL bool c_parse_integer_constant_range(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                        CScopeId scope, u32 start, u32 end, u64* value_out);

BUSTER_C_SHARED bool c_parse_validate_constexpr_declaration(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                                CPreprocessResult preprocess, CDeclaration* declaration)
{
    if (!declaration->is_constexpr)
    {
        return true;
    }
    CSourceLocation location = c_preprocess_token_location(&preprocess, preprocess.tokens[declaration->token_start]);
    if (declaration->kind != C_DECLARATION_OBJECT)
    {
        c_parse_diagnostic(result, location, C_DIAGNOSTIC_INVALID_CONSTEXPR, S8("constexpr may only declare an object"));
        return false;
    }
    if (!declaration->is_definition)
    {
        c_parse_diagnostic(result, location, C_DIAGNOSTIC_INVALID_CONSTEXPR, S8("constexpr object declaration requires an initializer"));
        return false;
    }
    u32 end = declaration->token_start + declaration->token_count;
    for (u32 index = declaration->token_start; index < end; index += 1)
    {
        String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
        if (string_equal(spelling, S8("extern")) || string_equal(spelling, S8("_Thread_local")) || string_equal(spelling, S8("__thread")))
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[index]), C_DIAGNOSTIC_INVALID_CONSTEXPR,
                               S8("constexpr cannot be combined with this storage-class specifier"));
            return false;
        }
    }
    if (declaration->type.value >= result->type_count)
    {
        c_parse_diagnostic(result, location, C_DIAGNOSTIC_INVALID_CONSTEXPR, S8("constexpr object has an invalid type"));
        return false;
    }
    Arena* conflicts[] = {
        arena,
    };
    TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    CTypeId* work = arena_allocate(temporary.arena, CTypeId, result->type_count + result->member_count + 1);
    bool* visited = arena_allocate(temporary.arena, bool, result->type_count + 1);
    memset(visited, 0, sizeof(*visited) * (result->type_count + 1));
    u32 work_count = 1;
    work[0] = declaration->type;
    bool valid = true;
    String8 message = {0};
    while (work_count)
    {
        CTypeId type_id = work[--work_count];
        if (type_id.value >= result->type_count || visited[type_id.value])
        {
            continue;
        }
        visited[type_id.value] = true;
        CType* type = &result->types[type_id.value];
        if (type->is_atomic)
        {
            valid = false;
            message = S8("constexpr object or subobject cannot have atomic type");
            break;
        }
        if (type->is_volatile || type->is_restrict)
        {
            valid = false;
            message = S8("constexpr object or subobject cannot be volatile or restrict-qualified");
            break;
        }
        if (type->kind == C_TYPE_FUNCTION || type->kind == C_TYPE_VOID)
        {
            valid = false;
            message = S8("constexpr requires a complete object type");
            break;
        }
        if (type->kind == C_TYPE_ARRAY)
        {
            CTypeId element_type = type->element_type;
            if (type->array_bound >= result->array_bound_count)
            {
                valid = false;
                message = S8("constexpr object cannot have variably modified type");
                break;
            }
            CArrayBound bound = result->array_bounds[type->array_bound];
            u64 count = 0;
            bool inferred_later = !bound.token_count && !bound.is_star;
            if (bound.is_star || (!inferred_later && !bound.has_inferred_count &&
                                  (!c_parse_integer_constant_range(machine, temporary.arena, preprocess, result, declaration->scope, bound.token_start,
                                                                  bound.token_start + bound.token_count, &count) ||
                                   !count)))
            {
                valid = false;
                message = S8("constexpr object cannot have variably modified type");
                break;
            }
            work[work_count++] = element_type;
            continue;
        }
        if (type->kind == C_TYPE_STRUCT || type->kind == C_TYPE_UNION)
        {
            for (u32 member_index = 0; member_index < type->member_count; member_index += 1)
            {
                work[work_count++] = result->members[type->member_start + member_index].type;
            }
        }
    }
    scratch_end(temporary);
    if (!valid)
    {
        c_parse_diagnostic(result, location, C_DIAGNOSTIC_INVALID_CONSTEXPR, message);
    }
    return valid;
}

typedef struct CTypePair CTypePair;
struct CTypePair
{
    CTypeId left;
    CTypeId right;
    bool ignore_qualifiers;
};

BUSTER_C_SHARED bool c_parse_types_compatible(Arena* result_arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right)
{
    Arena* conflicts[] = {
        result_arena,
    };
    TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    CTypePair* stack = arena_allocate(temporary.arena, CTypePair, result->type_count * 2 + 1);
    u32 stack_count = 0;
    stack[stack_count++] = (CTypePair){
        .left = left,
        .right = right,
    };
    bool compatible = true;
    while (stack_count)
    {
        CTypePair pair = stack[--stack_count];
        if (pair.left.value >= result->type_count || pair.right.value >= result->type_count)
        {
            compatible = false;
            break;
        }
        CType left_type = result->types[pair.left.value];
        CType right_type = result->types[pair.right.value];
        if (left_type.kind != right_type.kind ||
            (!pair.ignore_qualifiers && (left_type.is_const != right_type.is_const || left_type.is_volatile != right_type.is_volatile ||
                                        left_type.is_restrict != right_type.is_restrict || left_type.is_atomic != right_type.is_atomic)))
        {
            compatible = false;
            break;
        }
        switch (left_type.kind)
        {
        case C_TYPE_POINTER:
        {
            stack[stack_count++] = (CTypePair){
                .left = left_type.element_type,
                .right = right_type.element_type,
            };
            break;
        }
        case C_TYPE_ARRAY:
        {
            if (left_type.array_bound >= result->array_bound_count || right_type.array_bound >= result->array_bound_count)
            {
                compatible = false;
                break;
            }
            CArrayBound left_bound = result->array_bounds[left_type.array_bound];
            CArrayBound right_bound = result->array_bounds[right_type.array_bound];
            if (left_bound.token_count && right_bound.token_count)
            {
                bool spelled_alike = left_bound.token_count == right_bound.token_count;
                for (u32 token_index = 0; spelled_alike && token_index < left_bound.token_count; token_index += 1)
                {
                    spelled_alike = string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[left_bound.token_start + token_index]),
                                                 c_token_spelling(preprocess.spelling_base, preprocess.tokens[right_bound.token_start + token_index]));
                }
                // Two bounds that are spelled differently can still be the same
                // length: DoomGeneric's tables.h declares `finetangent` with
                // `[FINEANGLES/2]` and tables.c defines it with `[4096]`, which
                // C requires to be one type. The spelling comparison above is
                // the fast path; only when it disagrees is each bound
                // evaluated, and a bound that does not evaluate to a constant
                // (a variable-length array) keeps the spelling verdict.
                if (!spelled_alike)
                {
                    u64 left_value = 0;
                    u64 right_value = 0;
                    CScopeId file_scope = {
                        .value = 0,
                    };
                    compatible = c_parse_integer_constant_range(0, temporary.arena, preprocess, result, file_scope, left_bound.token_start,
                                                                left_bound.token_start + left_bound.token_count, &left_value) &&
                                 c_parse_integer_constant_range(0, temporary.arena, preprocess, result, file_scope, right_bound.token_start,
                                                                right_bound.token_start + right_bound.token_count, &right_value) &&
                                 left_value == right_value;
                    if (!compatible)
                    {
                        break;
                    }
                }
            }
            if (!compatible)
            {
                break;
            }
            stack[stack_count++] = (CTypePair){
                .left = left_type.element_type,
                .right = right_type.element_type,
            };
            break;
        }
        case C_TYPE_VECTOR:
        {
            if (left_type.vector_byte_size != right_type.vector_byte_size)
            {
                compatible = false;
                break;
            }
            stack[stack_count++] = (CTypePair){
                .left = left_type.element_type,
                .right = right_type.element_type,
            };
            break;
        }
        case C_TYPE_FUNCTION:
        {
            // C11 6.2.7p3: one type has a parameter list and the other does
            // not, so only the return types have to agree -- provided the
            // prototyped one is not variadic. This is what makes musl's
            // `long __syscall_cp_asm();` and the prototype beside it one
            // function; the parameter types are checked at the call.
            if (left_type.is_unprototyped != right_type.is_unprototyped)
            {
                if (left_type.is_variadic || right_type.is_variadic)
                {
                    compatible = false;
                    break;
                }
                stack[stack_count++] = (CTypePair){
                    .left = left_type.return_type,
                    .right = right_type.return_type,
                };
                break;
            }
            if (left_type.parameter_count != right_type.parameter_count || left_type.is_variadic != right_type.is_variadic)
            {
                compatible = false;
                break;
            }
            stack[stack_count++] = (CTypePair){
                .left = left_type.return_type,
                .right = right_type.return_type,
            };
            for (u32 parameter_index = 0; parameter_index < left_type.parameter_count; parameter_index += 1)
            {
                CTypeId left_parameter = result->parameters[left_type.parameter_start + parameter_index].type;
                CTypeId right_parameter = result->parameters[right_type.parameter_start + parameter_index].type;
                // C 6.7.6.3p7/p8: a parameter declared as an array or a
                // function is adjusted to the corresponding pointer, so a
                // prototype's `char **` and a definition's `char *argv[]`
                // declare one and the same function. The declared spelling
                // survives into the parameter type, so decay the halves that
                // disagree here rather than rejecting them. Qualifiers are
                // ignored only at the top level, which is why the decayed
                // pointee pair keeps them.
                CTypeKind left_parameter_kind =
                    left_parameter.value < result->type_count ? result->types[left_parameter.value].kind : C_TYPE_INVALID;
                CTypeKind right_parameter_kind =
                    right_parameter.value < result->type_count ? result->types[right_parameter.value].kind : C_TYPE_INVALID;
                if (left_parameter_kind == C_TYPE_ARRAY && right_parameter_kind == C_TYPE_POINTER)
                {
                    stack[stack_count++] = (CTypePair){
                        .left = result->types[left_parameter.value].element_type,
                        .right = result->types[right_parameter.value].element_type,
                    };
                    continue;
                }
                if (left_parameter_kind == C_TYPE_POINTER && right_parameter_kind == C_TYPE_ARRAY)
                {
                    stack[stack_count++] = (CTypePair){
                        .left = result->types[left_parameter.value].element_type,
                        .right = result->types[right_parameter.value].element_type,
                    };
                    continue;
                }
                if (left_parameter_kind == C_TYPE_FUNCTION && right_parameter_kind == C_TYPE_POINTER)
                {
                    stack[stack_count++] = (CTypePair){
                        .left = left_parameter,
                        .right = result->types[right_parameter.value].element_type,
                    };
                    continue;
                }
                if (left_parameter_kind == C_TYPE_POINTER && right_parameter_kind == C_TYPE_FUNCTION)
                {
                    stack[stack_count++] = (CTypePair){
                        .left = result->types[left_parameter.value].element_type,
                        .right = right_parameter,
                    };
                    continue;
                }
                stack[stack_count++] = (CTypePair){
                    .left = left_parameter,
                    .right = right_parameter,
                    .ignore_qualifiers = true,
                };
            }
            break;
        }
        case C_TYPE_STRUCT:
        case C_TYPE_UNION:
        case C_TYPE_ENUM:
        {
            // Qualified uses of an anonymous aggregate retain a distinct
            // CTypeId, but their unqualified type points at the same
            // declaration.  Compare that identity rather than the wrapper
            // IDs so a typedef'd enum remains compatible across a prototype
            // and its definition (Unity's display-style enum exercises this
            // exact redeclaration path).
            CTypeId left_unqualified = c_parse_unqualified_type(result, pair.left);
            CTypeId right_unqualified = c_parse_unqualified_type(result, pair.right);
            compatible = left_type.tag.length ? string_equal(left_type.tag, right_type.tag) : left_unqualified.value == right_unqualified.value;
            if (compatible && left_type.kind == C_TYPE_ENUM &&
                (left_type.element_type.value != C_ID_UNDERLYING_INVALID || right_type.element_type.value != C_ID_UNDERLYING_INVALID))
            {
                if (left_type.element_type.value == C_ID_UNDERLYING_INVALID || right_type.element_type.value == C_ID_UNDERLYING_INVALID)
                {
                    compatible = false;
                }
                else
                {
                    stack[stack_count++] = (CTypePair){
                        .left = left_type.element_type,
                        .right = right_type.element_type,
                    };
                }
            }
            break;
        }
        case C_TYPE_INVALID:
        case C_TYPE_VOID:
        case C_TYPE_BOOL:
        case C_TYPE_CHAR:
        case C_TYPE_SIGNED_CHAR:
        case C_TYPE_UNSIGNED_CHAR:
        case C_TYPE_SHORT:
        case C_TYPE_UNSIGNED_SHORT:
        case C_TYPE_INT:
        case C_TYPE_UNSIGNED_INT:
        case C_TYPE_LONG:
        case C_TYPE_UNSIGNED_LONG:
        case C_TYPE_LONG_LONG:
        case C_TYPE_UNSIGNED_LONG_LONG:
        case C_TYPE_INT128:
        case C_TYPE_UNSIGNED_INT128:
        case C_TYPE_FLOAT:
        case C_TYPE_DOUBLE:
        case C_TYPE_LONG_DOUBLE:
        case C_TYPE_FLOAT_COMPLEX:
        case C_TYPE_DOUBLE_COMPLEX:
        case C_TYPE_LONG_DOUBLE_COMPLEX:
        case C_TYPE_VA_LIST:
        case C_TYPE_NULLPTR:
        case C_TYPE_COUNT:
        {
            break;
        }
        }
    }
    scratch_end(temporary);
    return compatible;
}

BUSTER_C_INTERNAL CSourceLocation c_parse_cleanup_attribute_location(CPreprocessResult preprocess, CCleanupAttributeInfo attribute)
{
    CSourceLocation result;
    if (attribute.first_start < preprocess.token_count)
    {
        result = c_preprocess_token_location(&preprocess, preprocess.tokens[attribute.first_start]);
    }
    else
    {
        result = (CSourceLocation){0};
    }

    return result;
}

BUSTER_C_INTERNAL void c_parse_cleanup_diagnostic(CParseResult* result, CPreprocessResult preprocess, CCleanupAttributeInfo attribute, String8 message)
{
    c_parse_diagnostic(result, c_parse_cleanup_attribute_location(preprocess, attribute), C_DIAGNOSTIC_INVALID_CLEANUP_ATTRIBUTE, message);
}

BUSTER_C_INTERNAL bool c_parse_cleanup_pointer_conversion(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId source_id,
                                                            CTypeId target_id)
{
    if (source_id.value >= result->type_count || target_id.value >= result->type_count)
    {
        return false;
    }
    CType source = result->types[source_id.value];
    CType target = result->types[target_id.value];
    if ((source.is_const && !target.is_const) || (source.is_volatile && !target.is_volatile) || (source.is_restrict && !target.is_restrict))
    {
        return false;
    }
    if (target.kind == C_TYPE_VOID)
    {
        return true;
    }
    CTypeId source_unqualified = c_parse_unqualified_type(result, source_id);
    CTypeId target_unqualified = c_parse_unqualified_type(result, target_id);
    return source_unqualified.value < result->type_count && target_unqualified.value < result->type_count &&
           c_parse_types_compatible(arena, result, preprocess, source_unqualified, target_unqualified);
}

BUSTER_C_INTERNAL bool c_parse_cleanup_function_matches(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId declared_type,
                                                           CEntity* function)
{
    if (!function || function->type.value >= result->type_count)
    {
        return false;
    }
    CType* function_type = &result->types[function->type.value];
    if (function_type->kind != C_TYPE_FUNCTION || function_type->parameter_count != 1 || function_type->parameter_start >= result->parameter_count)
    {
        return false;
    }
    CTypeId parameter_id = result->parameters[function_type->parameter_start].type;
    if (parameter_id.value >= result->type_count)
    {
        return false;
    }
    CType* parameter_type = &result->types[parameter_id.value];
    if (parameter_type->kind == C_TYPE_ARRAY || parameter_type->kind == C_TYPE_FUNCTION)
    {
        parameter_id = c_parse_add_type(result, (CType){
                                                   .element_type = parameter_type->kind == C_TYPE_ARRAY ? parameter_type->element_type : parameter_id,
                                                   .return_type = C_TYPE_ID_INVALID,
                                                   .array_bound = C_ARRAY_BOUND_INVALID,
                                                   .kind = C_TYPE_POINTER,
                                               });
    }
    if (parameter_id.value >= result->type_count || result->types[parameter_id.value].kind != C_TYPE_POINTER)
    {
        return false;
    }
    return c_parse_cleanup_pointer_conversion(arena, result, preprocess, declared_type, result->types[parameter_id.value].element_type);
}

BUSTER_C_INTERNAL bool c_parse_validate_cleanup_attribute(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CEntityId entity_id,
                                                             CCleanupAttributeInfo attribute, bool is_typedef, bool is_register, bool is_extern,
                                                             bool is_thread_local)
{
    if (!attribute.count)
    {
        return false;
    }
    if (!c_preprocess_dialect_is_gnu(preprocess.dialect))
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("GNU cleanup attribute is only available in GNU dialects"));
        return false;
    }
    if (attribute.malformed || attribute.count != 1 || attribute.function_token >= preprocess.token_count)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup attribute requires exactly one function argument"));
        return false;
    }
    if (entity_id.value >= result->entity_count)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("GNU cleanup attribute may only be applied to an automatic block-scope object"));
        return false;
    }
    CEntity* entity = &result->entities[entity_id.value];
    if (entity->kind != C_ENTITY_LOCAL || entity->scope.value == C_ID_UNDERLYING_INVALID || entity->scope.value == 0 || entity->is_static_storage ||
        is_typedef || is_register || is_extern || is_thread_local || entity->type.value >= result->type_count ||
        result->types[entity->type.value].kind == C_TYPE_FUNCTION)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("GNU cleanup attribute may only be applied to an automatic block-scope object"));
        return false;
    }
    String8 function_name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[attribute.function_token]);
    CEntityId cleanup_function_id = c_parse_lookup_entity_at(result, preprocess, entity->scope, function_name, attribute.function_token);
    CEntity* cleanup_function = cleanup_function_id.value < result->entity_count ? &result->entities[cleanup_function_id.value] : 0;
    if (!cleanup_function || cleanup_function->type.value >= result->type_count || result->types[cleanup_function->type.value].kind != C_TYPE_FUNCTION)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup attribute argument must name a function"));
        return false;
    }
    CType* cleanup_type = &result->types[cleanup_function->type.value];
    if (cleanup_type->parameter_count != 1)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup function must take exactly one parameter"));
        return false;
    }
    if (cleanup_type->parameter_start >= result->parameter_count)
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup function must take exactly one parameter"));
        return false;
    }
    if (!c_parse_cleanup_function_matches(arena, result, preprocess, entity->type, cleanup_function))
    {
        c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup function parameter must be a pointer to the declared variable type"));
        return false;
    }
    entity->has_cleanup = true;
    entity->cleanup_function = cleanup_function_id;
    return true;
}

BUSTER_C_INTERNAL bool c_parse_cleanup_attribute_was_checked(CParseResult* result, u32 token)
{
    bool found = false;
    for (u32 entity_index = 0; entity_index < result->entity_count && !found; entity_index += 1)
    {
        CEntity* entity = &result->entities[entity_index];
        found = entity->cleanup_attribute_checked && entity->cleanup_attribute_token <= token && token < entity->cleanup_attribute_end;
    }

    return found;
}

// Validate the attribute keyword at index and return the next position the
// scan should visit: past the attribute's first specifier when one parsed,
// index + 1 otherwise.
BUSTER_C_INTERNAL u32 c_parse_validate_cleanup_attribute_keyword(CParseResult* result, CPreprocessResult preprocess, u32 index)
{
    CCleanupAttributeInfo attribute = {0};
    if (c_parse_cleanup_attribute_at(preprocess, index, (u32)preprocess.token_count, &attribute))
    {
        if (!c_parse_cleanup_attribute_was_checked(result, attribute.first_start))
        {
            if (!c_preprocess_dialect_is_gnu(preprocess.dialect))
            {
                c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("GNU cleanup attribute is only available in GNU dialects"));
            }
            else if (attribute.malformed || attribute.count != 1 || attribute.function_token >= preprocess.token_count)
            {
                c_parse_cleanup_diagnostic(result, preprocess, attribute, S8("cleanup attribute requires exactly one function argument"));
            }
            else
            {
                c_parse_cleanup_diagnostic(result, preprocess, attribute,
                                           S8("GNU cleanup attribute may only be applied to an automatic block-scope object"));
            }
        }
        if (attribute.first_end > index && attribute.first_end <= preprocess.token_count)
        {
            return attribute.first_end;
        }
    }

    return index + 1;
}

BUSTER_C_SHARED void c_parse_validate_unattached_cleanup_attributes(CParseResult* result, CPreprocessResult preprocess)
{
    // The position index recorded every attribute keyword when it is built
    // (which the delimiter queries of any real parse force); walking those
    // positions skips the re-classification of every other token. An unbuilt
    // index keeps the full scan rather than paying a whole build for one
    // walk.
    CTokenPositionIndex* position_index = result->position_index && result->position_index->built ? result->position_index : 0;
    if (position_index)
    {
        u32 resume = 0;
        for (u32 cursor = 0; cursor < position_index->attribute_count; cursor += 1)
        {
            u32 position = position_index->attribute_positions[cursor];
            if (position < resume)
            {
                continue;
            }
            resume = c_parse_validate_cleanup_attribute_keyword(result, preprocess, position);
        }
        return;
    }
    for (u32 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind != C_TOKEN_IDENTIFIER ||
            !c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ATTRIBUTE_KEYWORDS))
        {
            continue;
        }
        index = c_parse_validate_cleanup_attribute_keyword(result, preprocess, index) - 1;
    }
}

BUSTER_C_INTERNAL CTypeId c_parse_conditional_expression_type(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId left,
                                                                CTypeId right)
{
    left = c_parse_auto_decay_type(result, left);
    right = c_parse_auto_decay_type(result, right);
    if (left.value >= result->type_count || right.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CType left_value = result->types[left.value];
    CType right_value = result->types[right.value];
    if (left_value.kind == C_TYPE_NULLPTR && right_value.kind == C_TYPE_POINTER)
    {
        return right;
    }
    if (right_value.kind == C_TYPE_NULLPTR && left_value.kind == C_TYPE_POINTER)
    {
        return left;
    }
    if (left_value.kind == C_TYPE_POINTER && right_value.kind == C_TYPE_POINTER)
    {
        CTypeId left_element = c_parse_unqualified_type(result, left_value.element_type);
        CTypeId right_element = c_parse_unqualified_type(result, right_value.element_type);
        if (left_element.value >= result->type_count || right_element.value >= result->type_count)
        {
            return C_TYPE_ID_INVALID;
        }
        CType left_element_value = result->types[left_element.value];
        CType right_element_value = result->types[right_element.value];
        bool void_compatible = (left_element_value.kind == C_TYPE_VOID && right_element_value.kind != C_TYPE_FUNCTION) ||
                               (right_element_value.kind == C_TYPE_VOID && left_element_value.kind != C_TYPE_FUNCTION);
        bool element_compatible = void_compatible || c_parse_types_compatible(arena, result, preprocess, left_element, right_element);
        if (!element_compatible)
        {
            return C_TYPE_ID_INVALID;
        }
        CTypeId element = left_element_value.kind == C_TYPE_VOID ? left_element : right_element_value.kind == C_TYPE_VOID ? right_element : left_element;
        CType element_qualifiers = {
            .is_const = result->types[left_value.element_type.value].is_const || result->types[right_value.element_type.value].is_const,
            .is_volatile = result->types[left_value.element_type.value].is_volatile || result->types[right_value.element_type.value].is_volatile,
            .is_restrict = result->types[left_value.element_type.value].is_restrict || result->types[right_value.element_type.value].is_restrict,
            .is_atomic = result->types[left_value.element_type.value].is_atomic || result->types[right_value.element_type.value].is_atomic,
        };
        if (element_qualifiers.is_const || element_qualifiers.is_volatile || element_qualifiers.is_restrict || element_qualifiers.is_atomic)
        {
            element = c_parse_add_qualified_type(result, element, element_qualifiers);
        }
        return c_parse_add_type(result, (CType){
                                                .element_type = element,
                                                .return_type = C_TYPE_ID_INVALID,
                                                .array_bound = C_ARRAY_BOUND_INVALID,
                                                .kind = C_TYPE_POINTER,
                                            });
    }
    if (left_value.kind == right_value.kind &&
        (left_value.kind == C_TYPE_STRUCT || left_value.kind == C_TYPE_UNION || left_value.kind == C_TYPE_ENUM))
    {
        CTypeId left_unqualified = c_parse_unqualified_type(result, left);
        CTypeId right_unqualified = c_parse_unqualified_type(result, right);
        if (c_parse_types_compatible(arena, result, preprocess, left_unqualified, right_unqualified))
        {
            CType qualifiers = {
                .is_const = left_value.is_const || right_value.is_const,
                .is_volatile = left_value.is_volatile || right_value.is_volatile,
                .is_restrict = left_value.is_restrict || right_value.is_restrict,
                .is_atomic = left_value.is_atomic || right_value.is_atomic,
            };
            return (qualifiers.is_const || qualifiers.is_volatile || qualifiers.is_restrict || qualifiers.is_atomic)
                       ? c_parse_add_qualified_type(result, left_unqualified, qualifiers)
                       : left_unqualified;
        }
        return C_TYPE_ID_INVALID;
    }
    if (left.value == right.value)
    {
        return left;
    }
    return c_parse_expression_arithmetic_type(result, preprocess.target, left, right);
}

// Names resolve to intern ids through the borrowed preprocess table; a parse
// without one (hand-built tests) keeps every symbol 0 and the lookups below
// fall back to name hashing and string compares, so both configurations stay
// internally consistent.
BUSTER_C_SHARED u32 c_parse_name_symbol(CParseResult* result, String8 name)
{
    return result->symbols ? c_symbol_intern(result->symbols, name) : 0;
}

BUSTER_C_INTERNAL u64 c_parse_entity_lookup_hash(u32 symbol, String8 name, CScopeId scope)
{
    u64 hash = symbol ? (u64)symbol * UINT64_C(0x9E3779B97F4A7C15) : c_macro_name_hash(name);
    hash ^= (u64)scope.value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    return hash;
}

// Bucket key for the name/typedef chains: the interned id when the parse has
// a symbol table, the byte-FNV of the spelling when it does not (hand-built
// tests). Every name reaching a probe site comes from a token the
// preprocessor already interned, so the probe-side c_parse_name_symbol is an
// identity-path hit, never an insert.
BUSTER_C_SHARED u64 c_parse_name_hash(u32 symbol, String8 name)
{
    return symbol ? (u64)symbol * UINT64_C(0x9E3779B97F4A7C15) : c_macro_name_hash(name);
}

BUSTER_C_SHARED void c_parse_scope_add_entity(CParseResult* result, CScopeId scope, CEntityId entity)
{
    CScope* value = &result->scopes[scope.value];
    if (value->last_entity.value != C_ID_UNDERLYING_INVALID)
    {
        result->entities[value->last_entity.value].next_in_scope = entity;
    }
    else
    {
        value->first_entity = entity;
    }
    value->last_entity = entity;
    value->entity_count += 1;

    BUSTER_CHECK(result->entity_lookup_bucket_count != 0);
    CEntity* added = &result->entities[entity.value];
    added->symbol = c_parse_name_symbol(result, added->name);
    u64 name_hash = c_parse_name_hash(added->symbol, added->name);
    u64 hash = c_parse_entity_lookup_hash(added->symbol, added->name, scope);
    u32 bucket = (u32)hash & (result->entity_lookup_bucket_count - 1);
    added->next_in_lookup = result->entity_lookup_buckets[bucket];
    result->entity_lookup_buckets[bucket] = entity;
    u32 name_bucket = (u32)name_hash & (result->entity_lookup_bucket_count - 1);
    added->next_by_name = result->name_lookup_buckets[name_bucket];
    result->name_lookup_buckets[name_bucket] = entity;
    if (added->kind == C_ENTITY_TYPEDEF)
    {
        added->next_typedef_in_lookup = result->typedef_lookup_buckets[name_bucket];
        result->typedef_lookup_buckets[name_bucket] = entity;
    }
}

BUSTER_C_INTERNAL CEntityId c_parse_lookup_entity_symbol(CParseResult* result, CScopeId scope, u32 symbol, String8 name)
{
    while (scope.value != C_ID_UNDERLYING_INVALID)
    {
        u64 hash = c_parse_entity_lookup_hash(symbol, name, scope);
        u32 bucket = (u32)hash & (result->entity_lookup_bucket_count - 1);
        CEntityId entity = result->entity_lookup_buckets[bucket];
        while (entity.value != C_ID_UNDERLYING_INVALID)
        {
            CEntity* candidate = &result->entities[entity.value];
            if (candidate->scope.value == scope.value && candidate->symbol == symbol && (symbol || string_equal(candidate->name, name)))
            {
                return entity;
            }
            entity = candidate->next_in_lookup;
        }
        scope = result->scopes[scope.value].parent;
    }
    return C_ENTITY_ID_INVALID;
}

CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name)
{
    return c_parse_lookup_entity_symbol(result, scope, c_parse_name_symbol(result, name), name);
}

BUSTER_C_INTERNAL bool c_parse_entity_visible_at(CPreprocessResult preprocess, CEntity* entity, u32 token_index)
{
    if (entity->declaration_token_plus_one)
    {
        return entity->declaration_token_plus_one - 1 <= token_index;
    }
    if (token_index >= preprocess.token_count)
    {
        return true;
    }
    CSourceLocation reference = c_preprocess_token_location(&preprocess, preprocess.tokens[token_index]);
    return entity->location.file != reference.file || entity->location.offset <= reference.offset;
}

CEntityId c_parse_lookup_entity_at(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, String8 name,
                                                       u32 token_index)
{
    u32 symbol = c_parse_name_symbol(result, name);
    while (scope.value != C_ID_UNDERLYING_INVALID)
    {
        u64 hash = c_parse_entity_lookup_hash(symbol, name, scope);
        u32 bucket = (u32)hash & (result->entity_lookup_bucket_count - 1);
        CEntityId entity = result->entity_lookup_buckets[bucket];
        while (entity.value != C_ID_UNDERLYING_INVALID)
        {
            CEntity* candidate = &result->entities[entity.value];
            if (candidate->scope.value == scope.value && candidate->symbol == symbol && (symbol || string_equal(candidate->name, name)) &&
                c_parse_entity_visible_at(preprocess, candidate, token_index))
            {
                return entity;
            }
            entity = candidate->next_in_lookup;
        }
        scope = result->scopes[scope.value].parent;
    }
    return C_ENTITY_ID_INVALID;
}

BUSTER_C_SHARED CEntityId c_parse_lookup_typedef_name(CParseResult* result, String8 name, bool oldest)
{
    u32 bucket = (u32)c_parse_name_hash(c_parse_name_symbol(result, name), name) & (result->entity_lookup_bucket_count - 1);
    CEntityId found = C_ENTITY_ID_INVALID;
    for (CEntityId entity = result->typedef_lookup_buckets[bucket]; entity.value != C_ID_UNDERLYING_INVALID;
         entity = result->entities[entity.value].next_typedef_in_lookup)
    {
        if (string_equal(result->entities[entity.value].name, name))
        {
            found = entity;
            if (!oldest)
            {
                break;
            }
        }
    }
    return found;
}

BUSTER_C_SHARED CEntityId c_parse_lookup_typedef_name_fallback(CParseResult* result, String8 name)
{
    if (result)
    {
        // A complete parse has a power-of-two bucket table and a terminated chain.
        // Hand-built or partially rolled-back results can omit either table, point
        // at an invalid entity, or leave a cycle. Prove the chain before entering
        // the canonical lookup, whose hot path intentionally assumes this shape.
        bool bucket_chain_valid = result->entities && result->typedef_lookup_buckets && result->entity_lookup_bucket_count &&
                                  (result->entity_lookup_bucket_count & (result->entity_lookup_bucket_count - 1)) == 0;
        if (bucket_chain_valid)
        {
            u32 bucket = (u32)c_parse_name_hash(c_parse_name_symbol(result, name), name) & (result->entity_lookup_bucket_count - 1);
            CEntityId entity = result->typedef_lookup_buckets[bucket];
            u32 steps = 0;
            while (entity.value != C_ID_UNDERLYING_INVALID)
            {
                if (steps >= result->entity_count || entity.value >= result->entity_count)
                {
                    bucket_chain_valid = false;
                    break;
                }
                CEntity* candidate = &result->entities[entity.value];
                if (candidate->kind != C_ENTITY_TYPEDEF)
                {
                    bucket_chain_valid = false;
                    break;
                }
                entity = candidate->next_typedef_in_lookup;
                steps += 1;
            }
            if (bucket_chain_valid)
            {
                // Keep the canonical newest-first lookup as the source of the
                // result after validation; oldest=true preserves the historical
                // ascending entity scan's first exact typedef.
                return c_parse_lookup_typedef_name(result, name, true);
            }
        }

        // Preserve the original fallback for absent or malformed lookup metadata.
        // Ascending entity order is the language's oldest-typedef tie breaker.
        if (result->entities)
        {
            for (u32 entity_index = 0; entity_index < result->entity_count; entity_index += 1)
            {
                CEntity* entity = &result->entities[entity_index];
                if (entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, name))
                {
                    return (CEntityId){.value = entity_index};
                }
            }
        }
    }

    return C_ENTITY_ID_INVALID;
}

BUSTER_C_SHARED CEntity* c_parse_first_constant_entity(CParseResult* result, String8 name)
{
    if (!result->name_lookup_buckets || !result->entity_lookup_bucket_count)
    {
        return 0;
    }
    u32 bucket = (u32)c_parse_name_hash(c_parse_name_symbol(result, name), name) & (result->entity_lookup_bucket_count - 1);
    // The chain is newest-first; the last predicate match is the lowest entity
    // index, matching an ascending scan over the entity table.
    CEntity* first = 0;
    for (CEntityId entity = result->name_lookup_buckets[bucket]; entity.value != C_ID_UNDERLYING_INVALID;
         entity = result->entities[entity.value].next_by_name)
    {
        CEntity* candidate = &result->entities[entity.value];
        if ((candidate->kind == C_ENTITY_ENUMERATOR || (candidate->is_constexpr && candidate->has_constant_value)) && string_equal(candidate->name, name))
        {
            first = candidate;
        }
    }
    return first;
}

BUSTER_C_SHARED bool c_parse_type_start(CParseResult* result, CScopeId scope, String8 spelling, CPreprocessDialect dialect)
{
    if (c_parse_type_word_for_dialect(spelling, dialect) || c_parse_auto_type_word(spelling))
    {
        return true;
    }
    CEntityId entity = c_parse_lookup_entity(result, scope, spelling);
    return entity.value != C_ID_UNDERLYING_INVALID && result->entities[entity.value].kind == C_ENTITY_TYPEDEF;
}

// The token form of c_parse_type_start: word tests by interned symbol id,
// entity lookup through the token's own symbol.
BUSTER_C_SHARED bool c_parse_type_start_token(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, CToken token)
{
    bool starts_type;
    if (c_parse_type_word_for_dialect_token(preprocess, token) || c_parse_auto_type_word_token(preprocess, token))
    {
        starts_type = true;
    }
    else
    {
        CEntityId entity = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);
        starts_type = entity.value != C_ID_UNDERLYING_INVALID && result->entities[entity.value].kind == C_ENTITY_TYPEDEF;
    }
    return starts_type;
}

BUSTER_C_INTERNAL void c_parse_bind_identifier(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 token_index)
{
    CToken token = preprocess.tokens[token_index];
    CEntityId entity = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);
    BUSTER_CHECK(result->identifier_use_count < result->identifier_use_capacity);
    u32 use_index = result->identifier_use_count++;
    result->identifier_uses[use_index] = (CIdentifierUse){
        .token_index = token_index,
        .entity = entity,
        .scope = scope,
    };
    if (token_index < result->identifier_use_by_token_capacity && result->identifier_use_by_token[token_index] == C_ID_UNDERLYING_INVALID)
    {
        result->identifier_use_by_token[token_index] = use_index;
    }
    bool predefined_function_name = string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__func__")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__FUNCTION__")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__PRETTY_FUNCTION__")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_va_start")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__va_start")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_va_arg")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_va_copy")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_va_end"));
    predefined_function_name |= string_starts_with_sequence(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_"));
    predefined_function_name |= string_starts_with_sequence(c_token_spelling(preprocess.spelling_base, token), S8("__c11_atomic_"));
    // GCC's legacy full barrier is the one __sync builtin the compiler
    // implements; SQLite reaches for it in sqlite3MemoryBarrier.
    predefined_function_name |= string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__sync_synchronize"));
    // GNU's complex part operators are spelled as identifiers but name no
    // entity; the expression walker consumes them as prefix operators.
    predefined_function_name |= string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__real__")) ||
                                string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__real")) ||
                                string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__imag__")) ||
                                string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__imag"));
    predefined_function_name |=
        c_preprocess_dialect_is_c23(preprocess.dialect) &&
        (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("true")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("false")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("nullptr")));
    if (entity.value == C_ID_UNDERLYING_INVALID && !predefined_function_name)
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, token), C_DIAGNOSTIC_UNDECLARED_IDENTIFIER,
                           string_format(arena, S8("use of undeclared identifier '{S8}'"), c_token_spelling(preprocess.spelling_base, token)));
    }
}

BUSTER_C_INTERNAL bool c_parse_identifier_is_bound(CParseResult* result, u32 token_index)
{
    return c_parse_identifier_use_index(result, token_index) != C_ID_UNDERLYING_INVALID;
}

// One past the `__builtin_offsetof(type, designator)` group whose opening
// parenthesis is at `open`.  The designator names struct members rather than
// objects in scope, so an identifier binder walking a token range has to step
// over the whole group instead of resolving what is inside it.
BUSTER_C_INTERNAL u32 c_parse_builtin_offsetof_end(CPreprocessResult preprocess, u32 open, u32 end)
{
    u32 depth = 0;
    u32 index = open;
    while (index < end)
    {
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (!depth)
            {
                break;
            }
            depth -= 1;
            if (!depth)
            {
                index += 1;
                break;
            }
        }
        index += 1;
    }
    return index;
}

BUSTER_C_INTERNAL void c_parse_bind_array_bound_identifiers(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 start,
                                                              u32 end)
{
    u32 bracket_depth = 0;
    for (u32 token_index = start; token_index < end; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        // An array bound may be spelled with offsetof -- SQLite sizes a save
        // buffer as `sizeof(Parse) - offsetof(Parse, sLastToken)` -- and the
        // member named there is not an object this scope can resolve.
        if (token.kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_offsetof")) &&
            token_index + 1 < end && c_token_is_punctuator(&preprocess.tokens[token_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            token_index = c_parse_builtin_offsetof_end(preprocess, token_index + 1, end) - 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            bracket_depth += 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
        {
            if (bracket_depth)
            {
                bracket_depth -= 1;
            }
            continue;
        }
        if (!bracket_depth || token.kind != C_TOKEN_IDENTIFIER || c_parse_declaration_keyword_at(result, preprocess, token_index) ||
            c_parse_identifier_is_bound(result, token_index))
        {
            continue;
        }
        bool member = token_index > start && (c_token_is_punctuator(&preprocess.tokens[token_index - 1], C_PUNCTUATOR_DOT) ||
                                              c_token_is_punctuator(&preprocess.tokens[token_index - 1], C_PUNCTUATOR_ARROW));
        bool tag_name =
            token_index > start && preprocess.tokens[token_index - 1].kind == C_TOKEN_IDENTIFIER &&
            (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index - 1]), S8("struct")) ||
             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index - 1]), S8("union")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index - 1]), S8("enum")));
        if (!member && !tag_name)
        {
            c_parse_bind_identifier(arena, result, preprocess, scope, token_index);
        }
    }
}

BUSTER_C_INTERNAL bool c_type_kind_is_integer(CTypeKind kind)
{
    return kind == C_TYPE_BOOL || kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_UNSIGNED_CHAR || kind == C_TYPE_SHORT ||
           kind == C_TYPE_UNSIGNED_SHORT || kind == C_TYPE_INT || kind == C_TYPE_UNSIGNED_INT || kind == C_TYPE_LONG || kind == C_TYPE_UNSIGNED_LONG ||
           kind == C_TYPE_LONG_LONG || kind == C_TYPE_UNSIGNED_LONG_LONG || kind == C_TYPE_INT128 || kind == C_TYPE_UNSIGNED_INT128 || kind == C_TYPE_ENUM;
}

BUSTER_C_INTERNAL bool c_type_kind_is_signed_integer(CTypeKind kind)
{
    return kind == C_TYPE_CHAR || kind == C_TYPE_SIGNED_CHAR || kind == C_TYPE_SHORT || kind == C_TYPE_INT || kind == C_TYPE_LONG || kind == C_TYPE_LONG_LONG ||
           kind == C_TYPE_INT128 || kind == C_TYPE_ENUM;
}

BUSTER_C_INTERNAL bool c_parse_integer_expression_is_unsigned(CParseResult* result, CPreprocessResult preprocess, CScopeId scope, u32 start, u32 end)
{
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_PREPROCESSING_NUMBER)
        {
            String8 token_spelling = c_token_spelling(preprocess.spelling_base, token);
            for (u64 byte_index = 0; byte_index < token_spelling.length; byte_index += 1)
            {
                char8 byte = token_spelling.pointer[byte_index];
                if (byte == 'u' || byte == 'U')
                {
                    return true;
                }
            }
            continue;
        }
        if (token.kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        CEntityId entity_id = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);
        if (entity_id.value >= result->entity_count)
        {
            continue;
        }
        CTypeId type_id = result->entities[entity_id.value].type;
        if (type_id.value >= result->type_count)
        {
            continue;
        }
        CTypeKind kind = result->types[type_id.value].kind;
        if (kind == C_TYPE_UNSIGNED_CHAR || kind == C_TYPE_UNSIGNED_SHORT || kind == C_TYPE_UNSIGNED_INT || kind == C_TYPE_UNSIGNED_LONG ||
            kind == C_TYPE_UNSIGNED_LONG_LONG || kind == C_TYPE_UNSIGNED_INT128)
        {
            return true;
        }
    }
    return false;
}

BUSTER_C_INTERNAL bool c_parse_record_constexpr_integer(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                          CPreprocessResult preprocess, CScopeId scope, CEntityId entity_id, u32 initializer_start,
                                                          u32 initializer_end)
{
    if (entity_id.value >= result->entity_count)
    {
        return false;
    }
    CEntity* entity = &result->entities[entity_id.value];
    if (entity->is_constexpr && entity->type.value < result->type_count)
    {
        CType value_type = result->types[entity->type.value];
        if (c_type_kind_is_integer(value_type.kind))
        {
            Arena* conflicts[] = {
                arena,
            };
            TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
            u64 value = 0;
            bool evaluated =
                c_parse_integer_constant_range(machine, temporary.arena, preprocess, result, scope, initializer_start, initializer_end, &value);
            scratch_end(temporary);
            if (!evaluated)
            {
                c_parse_diagnostic(result, entity->location, C_DIAGNOSTIC_INVALID_CONSTEXPR,
                                   S8("integer constexpr initializer must be an integer constant expression"));
                return false;
            }
            bool expression_unsigned = c_parse_integer_expression_is_unsigned(result, preprocess, scope, initializer_start, initializer_end);
            bool negative = value > INT64_MAX && !expression_unsigned;
            u64 magnitude = negative ? 0 - value : value;
            u64 size = 0;
            u32 alignment = 0;
            bool representable = c_parse_builtin_type_layout(preprocess.target, value_type.kind, &size, &alignment);
            BUSTER_UNUSED(alignment);
            if (representable && size <= 8)
            {
                u32 bits = (u32)(size * 8);
                bool target_signed = c_type_kind_is_signed_integer(value_type.kind);
                if (target_signed)
                {
                    u64 positive_max = bits == 64 ? (u64)INT64_MAX : ((u64)1 << (bits - 1)) - 1;
                    u64 negative_max = positive_max + 1;
                    representable = negative ? magnitude <= negative_max : magnitude <= positive_max;
                }
                else
                {
                    u64 maximum = value_type.kind == C_TYPE_BOOL ? 1 : bits == 64 ? UINT64_MAX : ((u64)1 << bits) - 1;
                    representable = !negative && magnitude <= maximum;
                }
            }
            if (!representable)
            {
                c_parse_diagnostic(result, entity->location, C_DIAGNOSTIC_INVALID_CONSTEXPR,
                                   S8("integer constexpr initializer is not exactly representable in the declared type"));
                return false;
            }
            entity->has_constant_value = true;
            entity->constant_is_negative = negative;
            entity->constant_value = magnitude;
        }
    }

    return true;
}

BUSTER_C_SHARED bool c_parse_validate_constexpr_initializer(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                                CPreprocessResult preprocess, CScopeId scope, CEntityId entity_id,
                                                                u32 initializer_start, u32 initializer_end)
{
    if (entity_id.value >= result->entity_count)
    {
        return false;
    }
    CEntity* entity = result->entities + entity_id.value;
    if (entity->is_constexpr && entity->type.value < result->type_count)
    {
        CType* type = result->types + entity->type.value;
        if (c_type_kind_is_integer(type->kind))
        {
            return c_parse_record_constexpr_integer(machine, arena, result, preprocess, scope, entity_id, initializer_start, initializer_end);
        }
        if (type->kind != C_TYPE_POINTER && type->kind != C_TYPE_NULLPTR)
        {
            return true;
        }
        while (initializer_start < initializer_end && c_token_is_punctuator(&preprocess.tokens[initializer_start], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 depth = 0;
            u32 close = initializer_start;
            for (; close < initializer_end; close += 1)
            {
                CToken token = preprocess.tokens[close];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    if (!depth)
                    {
                        break;
                    }
                    depth -= 1;
                    if (!depth)
                    {
                        break;
                    }
                }
            }
            if (close != initializer_end - 1)
            {
                break;
            }
            initializer_start += 1;
            initializer_end -= 1;
        }
        bool nullptr_value = c_preprocess_dialect_is_c23(preprocess.dialect) && initializer_end == initializer_start + 1 &&
                             preprocess.tokens[initializer_start].kind == C_TOKEN_IDENTIFIER &&
                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[initializer_start]), S8("nullptr"));
        u64 integer_value = UINT64_MAX;
        bool integer_null = c_parse_integer_constant_range(machine, arena, preprocess, result, scope, initializer_start, initializer_end, &integer_value) &&
                            integer_value == 0;
        if (!nullptr_value && !integer_null)
        {
            c_parse_diagnostic(result, entity->location, C_DIAGNOSTIC_INVALID_CONSTEXPR, S8("pointer constexpr initializer must have a null pointer value"));
            return false;
        }
    }

    return true;
}

typedef struct CAutoDeclarationInfo CAutoDeclarationInfo;
struct CAutoDeclarationInfo
{
    CType qualifiers;
    u32 auto_index;
    u32 name_index;
    u32 initializer_start;
    u32 initializer_end;
    bool has_auto_type;
    bool has_initializer;
    bool has_multiple_declarators;
    bool invalid_declarator;
    bool conflicting_specifier;
    bool is_typedef;
    bool is_static_storage;
    bool is_extern;
    bool is_thread_local;
};

BUSTER_C_INTERNAL u32 c_parse_auto_skip_specifier(CPreprocessResult preprocess, u32 index, u32 end)
{
    u32 skipped = c_parse_skip_attributes(preprocess, index, end);
    u32 result;
    if (skipped != index)
    {
        result = skipped;
    }
    else
    {
        result = c_parse_skip_alignment_specifiers(preprocess, index, end);
    }

    return result;
}

BUSTER_C_INTERNAL bool c_parse_auto_storage_word(String8 spelling, CAutoDeclarationInfo* info)
{
    if (string_equal(spelling, S8("auto")) || string_equal(spelling, S8("register")) || string_equal(spelling, S8("__extension__")))
    {
        return true;
    }
    if (string_equal(spelling, S8("typedef")))
    {
        info->is_typedef = true;
        return true;
    }
    if (string_equal(spelling, S8("static")))
    {
        info->is_static_storage = true;
        return true;
    }
    if (string_equal(spelling, S8("extern")))
    {
        info->is_extern = true;
        return true;
    }
    if (string_equal(spelling, S8("_Thread_local")) || string_equal(spelling, S8("__thread")))
    {
        info->is_thread_local = true;
        return true;
    }
    return false;
}

BUSTER_C_INTERNAL bool c_parse_auto_declaration_info(CPreprocessResult preprocess, u32 start, u32 end, CAutoDeclarationInfo* info)
{
    *info = (CAutoDeclarationInfo){
        .auto_index = UINT32_MAX,
        .name_index = UINT32_MAX,
        .initializer_start = UINT32_MAX,
        .initializer_end = UINT32_MAX,
    };
    u32 specifier_end = end;
    u32 depth = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            depth -= depth != 0;
        }
        else if (!depth && (c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN) || c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA) ||
                            c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON)))
        {
            specifier_end = index;
            break;
        }
    }
    u32 scan = start;
    while (scan < specifier_end)
    {
        u32 skipped = c_parse_auto_skip_specifier(preprocess, scan, specifier_end);
        if (skipped != scan)
        {
            scan = skipped;
            continue;
        }
        if (preprocess.tokens[scan].kind == C_TOKEN_IDENTIFIER && c_parse_auto_type_word_token(preprocess, preprocess.tokens[scan]))
        {
            if (info->has_auto_type)
            {
                info->conflicting_specifier = true;
            }
            else
            {
                info->has_auto_type = true;
                info->auto_index = scan;
            }
        }
        scan += 1;
    }
    bool result;
    if (!info->has_auto_type)
    {
        result = false;
    }
    else
    {
        scan = start;
        while (scan < info->auto_index)
        {
            u32 skipped = c_parse_auto_skip_specifier(preprocess, scan, info->auto_index);
            if (skipped != scan)
            {
                scan = skipped;
                continue;
            }
            CToken token = preprocess.tokens[scan];
            if (token.kind != C_TOKEN_IDENTIFIER || (!c_parse_type_qualifier_word_token(preprocess, token, &info->qualifiers) &&
                                                      !c_parse_auto_storage_word(c_token_spelling(preprocess.spelling_base, token), info)))
            {
                info->conflicting_specifier = true;
            }
            scan += 1;
        }
        scan = info->auto_index + 1;
        while (scan < specifier_end)
        {
            u32 skipped = c_parse_auto_skip_specifier(preprocess, scan, specifier_end);
            if (skipped != scan)
            {
                scan = skipped;
                continue;
            }
            CToken token = preprocess.tokens[scan];
            if (token.kind == C_TOKEN_IDENTIFIER && c_parse_type_qualifier_word_token(preprocess, token, &info->qualifiers))
            {
                scan += 1;
                continue;
            }
            if (token.kind == C_TOKEN_IDENTIFIER && c_parse_auto_type_word_token(preprocess, token))
            {
                info->conflicting_specifier = true;
                scan += 1;
                continue;
            }
            if (token.kind == C_TOKEN_IDENTIFIER && c_parse_auto_storage_word(c_token_spelling(preprocess.spelling_base, token), info))
            {
                scan += 1;
                continue;
            }
            info->name_index = scan;
            break;
        }
        if (info->name_index < specifier_end && preprocess.tokens[info->name_index].kind == C_TOKEN_IDENTIFIER &&
            !c_declaration_keyword_token(preprocess, preprocess.tokens[info->name_index]) &&
            !c_parse_type_word_for_dialect_token(preprocess, preprocess.tokens[info->name_index]))
        {
            scan = info->name_index + 1;
            while (scan < specifier_end)
            {
                u32 skipped = c_parse_auto_skip_specifier(preprocess, scan, specifier_end);
                if (skipped != scan)
                {
                    scan = skipped;
                    continue;
                }
                info->invalid_declarator = true;
                break;
            }
        }
        else
        {
            info->invalid_declarator = true;
        }
        depth = 0;
        for (u32 index = start; index < end; index += 1)
        {
            CToken token = preprocess.tokens[index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                depth -= depth != 0;
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
            {
                info->has_multiple_declarators = true;
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN) && !info->has_initializer)
            {
                info->has_initializer = true;
                info->initializer_start = index + 1;
            }
        }
        info->initializer_end = end;
        if (info->initializer_end > start && c_token_is_punctuator(&preprocess.tokens[info->initializer_end - 1], C_PUNCTUATOR_SEMICOLON))
        {
            info->initializer_end -= 1;
        }
        result = true;
    }

    return result;
}

BUSTER_C_INTERNAL CTypeId c_parse_auto_decay_type(CParseResult* result, CTypeId type)
{
    if (type.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    type = c_parse_unqualified_type(result, type);
    if (type.value >= result->type_count)
    {
        return C_TYPE_ID_INVALID;
    }
    CType value = result->types[type.value];
    if (value.kind == C_TYPE_ARRAY)
    {
        return c_parse_add_type(result, (CType){
                                                .element_type = value.element_type,
                                                .return_type = C_TYPE_ID_INVALID,
                                                .array_bound = C_ARRAY_BOUND_INVALID,
                                                .kind = C_TYPE_POINTER,
                                            });
    }
    if (value.kind == C_TYPE_FUNCTION)
    {
        return c_parse_add_type(result, (CType){
                                                .element_type = type,
                                                .return_type = C_TYPE_ID_INVALID,
                                                .array_bound = C_ARRAY_BOUND_INVALID,
                                                .kind = C_TYPE_POINTER,
                                            });
    }
    return type;
}

BUSTER_C_INTERNAL bool c_parse_auto_initializer_type(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                       CScopeId scope, u32 start, u32 end, CTypeId* type_out)
{
    CTypeId type = C_TYPE_ID_INVALID;
    if (start >= end || !c_parse_expression_type_query(machine, arena, preprocess, result, scope, start, end, true, &type))
    {
        return false;
    }
    type = c_parse_auto_decay_type(result, type);
    if (type.value >= result->type_count || result->types[type.value].kind == C_TYPE_VOID)
    {
        return false;
    }
    *type_out = type;
    return true;
}

BUSTER_C_INTERNAL bool c_parse_statement_expression_at(CPreprocessResult preprocess, u32 index, u32 end, u32* body_start, u32* body_end,
                                                         u32* group_end);

BUSTER_C_INTERNAL void c_parse_bind_auto_initializer_identifiers(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                                    u32 start, u32 end)
{
    for (u32 use_index = start; use_index < end; use_index += 1)
    {
        CToken use = preprocess.tokens[use_index];
        // A statement expression binds against a scope of its own, which the
        // declaration walk in c_parse_local_declarations opens. Binding its
        // body here as well would resolve its locals in the enclosing scope
        // and, worse, claim the tokens before that walk reaches them.
        u32 statement_body_start = 0;
        u32 statement_body_end = 0;
        u32 statement_group_end = 0;
        if (c_parse_statement_expression_at(preprocess, use_index, end, &statement_body_start, &statement_body_end, &statement_group_end))
        {
            use_index = statement_group_end;
            continue;
        }
        if (use.kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, use), S8("__builtin_offsetof")) && use_index + 1 < end &&
            c_token_is_punctuator(&preprocess.tokens[use_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 builtin_depth = 0;
            for (use_index += 1; use_index < end; use_index += 1)
            {
                if (c_token_is_punctuator(&preprocess.tokens[use_index], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    builtin_depth += 1;
                }
                else if (c_token_is_punctuator(&preprocess.tokens[use_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    if (builtin_depth)
                    {
                        builtin_depth -= 1;
                    }
                    if (!builtin_depth)
                    {
                        break;
                    }
                }
            }
            continue;
        }
        bool member = use_index > start && (c_token_is_punctuator(&preprocess.tokens[use_index - 1], C_PUNCTUATOR_DOT) ||
                                            c_token_is_punctuator(&preprocess.tokens[use_index - 1], C_PUNCTUATOR_ARROW));
        bool tag_name = use_index > start && preprocess.tokens[use_index - 1].kind == C_TOKEN_IDENTIFIER &&
                        (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("struct")) ||
                         string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("union")) ||
                         string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("enum")));
        if (use.kind == C_TOKEN_IDENTIFIER && !c_parse_declaration_keyword_at(result, preprocess, use_index) && !member && !tag_name &&
                !(use_index && c_parse_label_address_prefix_with_typedef(result, &preprocess, scope, start, use_index - 1)) &&
                !c_parse_identifier_is_bound(result, use_index))
        {
            c_parse_bind_identifier(arena, result, preprocess, scope, use_index);
        }
    }
}

BUSTER_C_INTERNAL CTypeId c_parse_local_function_suffix(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                          CTypeId return_type, u32 open, u32 end, u32* index_out)
{
    u32 close = open + 1;
    u32 depth = 1;
    while (close < end && depth)
    {
        if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            depth -= 1;
        }
        close += 1;
    }
    if (depth)
    {
        return C_TYPE_ID_INVALID;
    }
    close -= 1;
    u32 parameter_start = result->parameter_count;
    u32 parameter_count = 0;
    bool variadic = false;
    bool valid = true;
    u32 segment_start = open + 1;
    u32 nested_depth = 0;
    for (u32 index = open + 1; index <= close; index += 1)
    {
        bool separator = index == close || (!nested_depth && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_COMMA));
        if (separator)
        {
            if (segment_start == index)
            {
                valid = index == close && parameter_count == 0 && !variadic;
            }
            else if (index == segment_start + 1 && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[segment_start]), S8("void")) && parameter_count == 0 && !variadic)
            {
                /* An unnamed void parameter list has no parameters. */
            }
            else if (index == segment_start + 1 && c_token_is_punctuator(&preprocess.tokens[segment_start], C_PUNCTUATOR_ELLIPSIS))
            {
                valid = !variadic;
                variadic = true;
            }
            else if (!variadic)
            {
                valid = c_parse_parameter_segment(machine, result, preprocess, (CDeclaration){0}, segment_start, index);
                if (valid)
                {
                    parameter_count += 1;
                }
            }
            else
            {
                valid = false;
            }
            segment_start = index + 1;
            continue;
        }
        if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS) ||
            c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
        {
            nested_depth += 1;
        }
        else if ((c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
                  c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_BRACKET) ||
                  c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_BRACE)) &&
                 nested_depth)
        {
            nested_depth -= 1;
        }
    }
    if (!valid || nested_depth)
    {
        result->parameter_count = parameter_start;
        return C_TYPE_ID_INVALID;
    }
    *index_out = close + 1;
    return c_parse_add_type(result, (CType){
                                               .element_type = C_TYPE_ID_INVALID,
                                               .return_type = return_type,
                                               .array_bound = C_ARRAY_BOUND_INVALID,
                                               .parameter_start = parameter_start,
                                               .parameter_count = parameter_count,
                                               .kind = C_TYPE_FUNCTION,
                                               .is_variadic = variadic,
                                               .is_unprototyped = c_parse_parameter_list_unprototyped(preprocess, close),
                                           });
}

BUSTER_C_INTERNAL void c_parse_bind_block_statements(CTypeParseMachine* machine, Arena* result_arena, CParseResult* result,
                                                       CPreprocessResult preprocess, u32 declaration_index, CScopeId scope, u32 body_start,
                                                       u32 body_token_count);

// The GNU statement expression `({ ... })` opening at `index`, if one does.
// `body_start` and `body_end` bound the block's tokens -- the braces excluded
// -- and `group_end` is the closing parenthesis, which is what a token walk
// resumes past.
BUSTER_C_INTERNAL bool c_parse_statement_expression_at(CPreprocessResult preprocess, u32 index, u32 end, u32* body_start, u32* body_end,
                                                         u32* group_end)
{
    bool found = false;
    if (index + 2 < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
        c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_BRACE))
    {
        u32 close = c_parse_matching_delimiter(preprocess, index + 1, end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE);
        if (close + 1 < end && c_token_is_punctuator(&preprocess.tokens[close + 1], C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            *body_start = index + 2;
            *body_end = close;
            *group_end = close + 1;
            found = true;
        }
    }
    return found;
}

// A statement expression's body is a block of its own: `({ int t = x + 1; t; })`
// declares `t` in a scope the trailing expression resolves against. The
// function-body walk opens that scope itself wherever the statement expression
// stands in a statement, but not when it stands in a declaration's initializer
// -- that whole statement is handed to c_parse_local_declarations, so the body
// is walked from there through here instead.
BUSTER_C_INTERNAL void c_parse_bind_statement_expression_body(CTypeParseMachine* machine, Arena* result_arena, CParseResult* result,
                                                                CPreprocessResult preprocess, CScopeId scope, u32 declaration_index,
                                                                u32 body_start, u32 body_end)
{
    BUSTER_CHECK(result->scope_count < result->scope_capacity);
    CScopeId child = {
        .value = result->scope_count,
    };
    result->scopes[result->scope_count++] = (CScope){
        .parent = scope,
        .first_entity = C_ENTITY_ID_INVALID,
        .last_entity = C_ENTITY_ID_INVALID,
        .token_start = body_start,
        .token_end = body_end,
    };
    if (body_start < body_end)
    {
        c_parse_bind_block_statements(machine, result_arena, result, preprocess, declaration_index, child, body_start, body_end - body_start);
    }
}

BUSTER_C_INTERNAL bool c_parse_local_declarations(CTypeParseMachine* machine, Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                    CScopeId scope, u32 declaration_index, u32 start, u32 end)
{
    CAutoDeclarationInfo auto_info = {0};
    bool is_auto_type = c_parse_auto_declaration_info(preprocess, start, end, &auto_info);
    CTypeId auto_type = C_TYPE_ID_INVALID;
    bool is_typedef = false;
    bool is_static_storage = false;
    bool is_register = false;
    bool is_constexpr = false;
    bool is_extern = false;
    bool is_thread_local = false;
    // Storage class and `typedef` are declaration specifiers, so only the
    // tokens outside every delimiter belong to this declaration. A statement
    // expression in an initializer carries a block of declarations of its own
    // -- `int v = ({ static int s = 3; s; });` declares an automatic `v` and a
    // static `s` -- and an array parameter's `[static 3]` is a bound qualifier
    // rather than a storage class.
    u32 specifier_depth = 0;
    for (u32 token_index = start; token_index < end; token_index += 1)
    {
        CToken specifier_token = preprocess.tokens[token_index];
        if (c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_LEFT_BRACE))
        {
            specifier_depth += 1;
            continue;
        }
        if (c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_RIGHT_BRACKET) ||
            c_token_is_punctuator(&specifier_token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            specifier_depth -= specifier_depth != 0;
            continue;
        }
        if (specifier_depth || specifier_token.kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        is_typedef |= string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("typedef"));
        is_static_storage |= string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("static"));
        is_register |= string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("register"));
        is_extern |= string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("extern"));
        is_thread_local |= string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("_Thread_local")) ||
                           string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("__thread"));
        is_constexpr |= c_preprocess_dialect_is_c23(preprocess.dialect) &&
                        string_equal(c_token_spelling(preprocess.spelling_base, specifier_token), S8("constexpr"));
    }
    if (is_auto_type)
    {
        is_typedef |= auto_info.is_typedef;
        is_static_storage |= auto_info.is_static_storage;
        is_extern |= auto_info.is_extern;
        is_thread_local |= auto_info.is_thread_local;
        if (!c_preprocess_dialect_is_gnu(preprocess.dialect))
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type is only available in GNU dialects"));
            return false;
        }
        if (auto_info.has_multiple_declarators)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type may only be used with a single declarator"));
            return false;
        }
        if (is_typedef || is_static_storage || is_extern || is_thread_local)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type requires an automatic object declaration"));
            return false;
        }
        if (auto_info.conflicting_specifier)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type cannot be combined with another type specifier"));
            return false;
        }
        if (auto_info.invalid_declarator || auto_info.name_index >= end)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type requires a plain identifier as declarator"));
            return false;
        }
        if (!auto_info.has_initializer || auto_info.initializer_start >= auto_info.initializer_end)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("GNU __auto_type requires an initialized data declaration"));
            return false;
        }
        u32 diagnostic_checkpoint = result->diagnostic_count;
        c_parse_bind_auto_initializer_identifiers(arena, result, preprocess, scope, auto_info.initializer_start, auto_info.initializer_end);
        CTypeId inferred = C_TYPE_ID_INVALID;
        if (!c_parse_auto_initializer_type(machine, arena, preprocess, result, scope, auto_info.initializer_start, auto_info.initializer_end, &inferred))
        {
            if (result->diagnostic_count == diagnostic_checkpoint)
            {
                c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                   S8("could not infer the type of the GNU __auto_type initializer"));
            }
            return false;
        }
        if (auto_info.qualifiers.is_restrict && inferred.value < result->type_count && result->types[inferred.value].kind != C_TYPE_POINTER)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[auto_info.auto_index]), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               S8("restrict-qualified GNU __auto_type must infer a pointer type"));
            return false;
        }
        if (auto_info.qualifiers.is_const || auto_info.qualifiers.is_volatile || auto_info.qualifiers.is_restrict || auto_info.qualifiers.is_atomic)
        {
            inferred = c_parse_add_qualified_type(result, inferred, auto_info.qualifiers);
        }
        auto_type = inferred;
    }
    if (is_thread_local && !is_static_storage && !is_extern)
    {
        u32 token_index = start;
        while (token_index < end && !(preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER &&
                                      (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]), S8("_Thread_local")) ||
                                       string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]), S8("__thread")))))
        {
            token_index += 1;
        }
        c_parse_diagnostic(result, token_index < end ? c_preprocess_token_location(&preprocess, preprocess.tokens[token_index]) : c_preprocess_token_location(&preprocess, preprocess.tokens[start]),
                           C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                           S8("block-scope thread-local declarations require static or extern"));
        return false;
    }
    u32 enum_member_start = result->enum_member_count;
    u32 declarator_start = start;
    CTypeId base = is_auto_type ? C_TYPE_ID_INVALID : c_parse_scalar_type_in_scope(machine, result, preprocess, scope, start, end, &declarator_start);
    if (is_auto_type)
    {
        declarator_start = auto_info.name_index;
    }
    if (base.value == C_ID_UNDERLYING_INVALID)
    {
        if (!is_auto_type)
        {
            return false;
        }
    }
    u32 alignment_start = 0;
    u32 alignment_count = 0;
    if (!c_parse_alignment_specifiers(machine, result, preprocess, start, declarator_start, &alignment_start, &alignment_count))
    {
        return false;
    }
    if (alignment_count && (is_typedef || is_register))
    {
        c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, preprocess.tokens[start]), C_DIAGNOSTIC_INVALID_ALIGNMENT,
                           is_typedef ? S8("alignment specifier cannot be applied to a typedef")
                                      : S8("alignment specifier cannot be applied to a register object"));
        alignment_count = 0;
    }
    CCleanupAttributeInfo declaration_cleanup = {0};
    c_parse_cleanup_attribute_scan(preprocess, start, declarator_start, true, &declaration_cleanup);
    for (u32 member_index = enum_member_start; member_index < result->enum_member_count; member_index += 1)
    {
        CEnumMember* member = &result->enum_members[member_index];
        CEntityId entity = {
            .value = result->entity_count,
        };
        BUSTER_CHECK(result->entity_count < result->entity_capacity);
        result->entities[result->entity_count++] = (CEntity){
            .name = member->name,
            .location = member->location,
            .type = base,
            .scope = scope,
            .next_in_scope = C_ENTITY_ID_INVALID,
            .declaration_index = declaration_index,
            .kind = C_ENTITY_ENUMERATOR,
            .is_definition = true,
            .constant_is_negative = member->is_negative,
            .constant_value = member->value,
        };
        c_parse_scope_add_entity(result, scope, entity);
    }
    u32 segment_start = declarator_start;
    while (segment_start < end)
    {
        u32 segment_end = segment_start;
        u32 depth = 0;
        for (; segment_end < end; segment_end += 1)
        {
            CToken token = preprocess.tokens[segment_end];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                if (depth)
                {
                    depth -= 1;
                }
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
            {
                break;
            }
        }
        u32 suffix_end = segment_end;
        u32 suffix_depth = 0;
        for (u32 suffix_index = segment_start; suffix_index < segment_end; suffix_index += 1)
        {
            CToken token = preprocess.tokens[suffix_index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                suffix_depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                if (suffix_depth)
                {
                    suffix_depth -= 1;
                }
            }
            else if (!suffix_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
            {
                suffix_end = suffix_index;
                break;
            }
        }
        // A GNU `aligned` attribute may follow the declarator rather than the
        // specifiers (`char scratch[64] __attribute__((aligned(8)));`). The
        // records it appends have to end up in one contiguous run with the
        // specifier-level ones, which the shared declaration-level scan
        // appended before this loop and before any other declarator's, so the
        // specifier records are copied in behind these when both exist. The
        // aggregate is a maximum, so the order inside the run does not matter.
        u32 segment_alignment_start = 0;
        u32 segment_alignment_count = 0;
        c_parse_layout_attributes(result, preprocess, segment_start, suffix_end, 0, &segment_alignment_start, &segment_alignment_count);
        for (u32 copy_index = 0; segment_alignment_count && copy_index < alignment_count; copy_index += 1)
        {
            if (result->alignment_count >= result->alignment_capacity)
            {
                break;
            }
            result->alignments[result->alignment_count++] = result->alignments[alignment_start + copy_index];
            segment_alignment_count += 1;
        }
        CCleanupAttributeInfo cleanup = declaration_cleanup;
        CCleanupAttributeInfo segment_cleanup = {0};
        c_parse_cleanup_attribute_scan(preprocess, segment_start, suffix_end, true, &segment_cleanup);
        if (segment_cleanup.count)
        {
            if (!cleanup.count)
            {
                cleanup = segment_cleanup;
            }
            else
            {
                cleanup.count += segment_cleanup.count;
                cleanup.malformed = true;
                cleanup.last_end = segment_cleanup.last_end;
            }
        }
        u32 index = segment_start;
        u32 derived_start = result->type_count;
        CTypeId type = is_auto_type ? auto_type : c_parse_pointer_chain(result, preprocess, base, &index, suffix_end);
        CToken name = {0};
        u32 name_index = UINT32_MAX;
        bool parenthesized = !is_auto_type && index < suffix_end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS);
        if (parenthesized)
        {
            u32 close = index + 1;
            u32 parenthesis_depth = 1;
            while (close < suffix_end && parenthesis_depth)
            {
                if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    parenthesis_depth += 1;
                }
                else if (c_token_is_punctuator(&preprocess.tokens[close], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    parenthesis_depth -= 1;
                    if (!parenthesis_depth)
                    {
                        break;
                    }
                }
                close += 1;
            }
            u32 parenthesized_name = 0;
            if (parenthesis_depth || close <= index + 1)
            {
                return false;
            }
            if (c_parse_parenthesized_declarator_name(preprocess, index, suffix_end, &parenthesized_name))
            {
                // The name may be a group deeper than this one:
                // `void (*(*x)(void *, const char *))(void)` declares a
                // pointer to a function returning a function pointer, which is
                // how SQLite's Unix VFS holds dlsym.
                name_index = parenthesized_name;
            }
            else if (preprocess.tokens[close - 1].kind == C_TOKEN_IDENTIFIER)
            {
                name_index = close - 1;
            }
            else
            {
                return false;
            }
            name = preprocess.tokens[name_index];
            type = c_parse_parenthesized_declaration_type(machine, result, preprocess, type, index, name_index, suffix_end, true);
            index = suffix_end;
        }
        else
        {
            if (index >= suffix_end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER)
            {
                return false;
            }
            name_index = index;
            name = preprocess.tokens[index++];
            index = c_parse_skip_attributes(preprocess, index, suffix_end);
            if (index < suffix_end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                type = c_parse_local_function_suffix(machine, result, preprocess, type, index, suffix_end, &index);
                if (type.value == C_ID_UNDERLYING_INVALID)
                {
                    return false;
                }
            }
            type = c_parse_array_suffixes(result, preprocess, type, &index, suffix_end);
            index = c_parse_skip_attributes(preprocess, index, suffix_end);
        }
        if (is_auto_type)
        {
            type = auto_type;
        }
        else
        {
            type = c_parse_apply_vector_attribute(result, preprocess, scope, type, segment_start, suffix_end);
        }
        // The marker sits either in the specifiers every declarator of the list
        // shares -- a block-scope
        // `typedef __attribute__((noreturn)) void (*die)(int);` writes it there
        // -- or in this declarator.  The two ranges are scanned separately
        // rather than as the span between them, which would read a preceding
        // declarator's own attribute as this one's and end control flow at a
        // call that returns.  c_ir_declaration_is_noreturn splits the file-scope
        // declaration the same way, for the same reason.
        CTypeId noreturn_function = c_parse_noreturn_candidate_function_type(result, type, is_typedef, derived_start);
        if (noreturn_function.value != C_ID_UNDERLYING_INVALID && (c_ir_noreturn_marker_in_range(preprocess, start, declarator_start) ||
                                                                   c_ir_noreturn_marker_in_range(preprocess, segment_start, suffix_end)))
        {
            c_parse_add_noreturn_function_type(result, noreturn_function);
        }
        if (is_constexpr && type.value < result->type_count)
        {
            type = c_parse_add_qualified_type(result, type,
                                              (CType){
                                                  .is_const = true,
                                              });
        }
        if (type.value == C_ID_UNDERLYING_INVALID || index != suffix_end)
        {
            return false;
        }
        if (!is_typedef)
        {
            CTypeId object_type = C_TYPE_ID_INVALID;
            if (!c_parse_clone_incomplete_array_declarator(machine, result, type, &object_type))
            {
                c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, name), C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                   S8("compiler resource limit while materializing incomplete array declaration"));
                return false;
            }
            type = object_type;
        }
        if (is_typedef && c_parse_variable_argument_list_name(c_token_spelling(preprocess.spelling_base, name)))
        {
            type = c_parse_variable_argument_list_type(result);
        }
        // A block-scope declarator with a parameter list declares a function
        // with external linkage rather than an object -- DoomGeneric's
        // i_video.c declares `extern void I_InitInput(void);` inside
        // I_InitGraphics and then calls it. The local entity below is still
        // created, so the body lowering recognizes the declaration statement,
        // but the name has to reach the file-scope function index as well or
        // the call resolves to nothing. Register it there the way a file-scope
        // declaration of the same function would, unless one already is.
        CType* declared_type = type.value < result->type_count ? &result->types[type.value] : 0;
        if (!is_typedef && declared_type && declared_type->kind == C_TYPE_FUNCTION &&
            result->declaration_count < result->declaration_capacity && result->entity_count + 1 < result->entity_capacity)
        {
            String8 function_name = c_token_spelling(preprocess.spelling_base, name);
            CEntityId file_scope = c_parse_lookup_entity(result,
                                                         (CScopeId){
                                                             .value = 0,
                                                         },
                                                         function_name);
            if (file_scope.value == C_ID_UNDERLYING_INVALID || result->entities[file_scope.value].kind != C_ENTITY_FUNCTION)
            {
                CEntityId function_entity = {
                    .value = result->entity_count,
                };
                u32 function_declaration_index = result->declaration_count;
                result->declarations[result->declaration_count++] = (CDeclaration){
                    .name = function_name,
                    .location = c_preprocess_token_location(&preprocess, name),
                    .token_start = segment_start,
                    .token_count = suffix_end - segment_start,
                    .parameter_start = declared_type->parameter_start,
                    .parameter_count = declared_type->parameter_count,
                    .type = type,
                    .base_type = C_TYPE_ID_INVALID,
                    .entity = function_entity,
                    .scope =
                        {
                            .value = 0,
                        },
                    .kind = C_DECLARATION_FUNCTION,
                    .is_variadic = declared_type->is_variadic,
                };
                result->entities[result->entity_count++] = (CEntity){
                    .name = function_name,
                    .location = c_preprocess_token_location(&preprocess, name),
                    .type = type,
                    .scope =
                        {
                            .value = 0,
                        },
                    .next_in_scope = C_ENTITY_ID_INVALID,
                    .declaration_index = function_declaration_index,
                    .declaration_token_plus_one = name_index + 1,
                    .kind = C_ENTITY_FUNCTION,
                };
                c_parse_scope_add_entity(result,
                                         (CScopeId){
                                             .value = 0,
                                         },
                                         function_entity);
            }
        }
        CEntityId duplicate = c_parse_lookup_entity(result, scope, c_token_spelling(preprocess.spelling_base, name));
        if (duplicate.value != C_ID_UNDERLYING_INVALID && result->entities[duplicate.value].scope.value == scope.value)
        {
            c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, name), C_DIAGNOSTIC_REDEFINITION, S8("redefinition of local identifier"));
            return false;
        }
        CEntityId entity = {
            .value = result->entity_count,
        };
        BUSTER_CHECK(result->entity_count < result->entity_capacity);
        result->entities[result->entity_count++] = (CEntity){
            .name = c_token_spelling(preprocess.spelling_base, name),
            .location = c_preprocess_token_location(&preprocess, name),
            .type = type,
            .scope = scope,
            .next_in_scope = C_ENTITY_ID_INVALID,
            .declaration_index = declaration_index,
            .declaration_token_plus_one = name_index + 1,
            .declaration_token_start = segment_start,
            .declaration_token_count = segment_end - segment_start,
            .alignment_start = segment_alignment_count ? segment_alignment_start : alignment_start,
            .alignment_count = segment_alignment_count ? segment_alignment_count : alignment_count,
            .kind = is_typedef ? C_ENTITY_TYPEDEF : C_ENTITY_LOCAL,
            .is_definition = true,
            .is_static_storage = is_static_storage,
            .is_thread_local = is_thread_local,
            .is_constexpr = is_constexpr,
            .is_register = is_register,
        };
        CEntity* local_entity = &result->entities[entity.value];
        if (cleanup.count)
        {
            local_entity->cleanup_attribute_checked = true;
            local_entity->cleanup_attribute_token = cleanup.first_start;
            local_entity->cleanup_attribute_end = cleanup.last_end;
            c_parse_validate_cleanup_attribute(arena, result, preprocess, entity, cleanup, is_typedef, is_register, is_extern, is_thread_local);
        }
        c_parse_scope_add_entity(result, scope, entity);
        c_parse_bind_array_bound_identifiers(arena, result, preprocess, scope, segment_start, suffix_end);
        u32 initializer_start = suffix_end < segment_end ? suffix_end + 1 : segment_end;
        if (is_constexpr)
        {
            CDeclaration local_declaration = {
                .name = c_token_spelling(preprocess.spelling_base, name),
                .location = c_preprocess_token_location(&preprocess, name),
                .token_start = segment_start,
                .token_count = segment_end - segment_start,
                .type = type,
                .scope = scope,
                .kind = C_DECLARATION_OBJECT,
                .is_definition = initializer_start < segment_end,
                .is_constexpr = true,
            };
            c_parse_validate_constexpr_declaration(machine, arena, result, preprocess, &local_declaration);
            if (initializer_start < segment_end)
            {
                c_parse_validate_constexpr_initializer(machine, arena, result, preprocess, scope, entity, initializer_start, segment_end);
            }
        }
        if (initializer_start < segment_end && type.value < result->type_count && result->types[type.value].kind == C_TYPE_ARRAY)
        {
            CType array_type = result->types[type.value];
            if (array_type.array_bound < result->array_bound_count)
            {
                u32 bound_index = array_type.array_bound;
                CArrayBound bound = result->array_bounds[bound_index];
                if (!bound.token_count)
                {
                    u64 count = 0;
                    if (c_parse_infer_initializer_array_count(machine, arena, preprocess, result, scope, array_type.element_type,
                                                              initializer_start, segment_end, &count) &&
                        bound_index < result->array_bound_count)
                    {
                        result->array_bounds[bound_index].inferred_count = count;
                        result->array_bounds[bound_index].has_inferred_count = true;
                    }
                }
            }
        }
        for (u32 use_index = initializer_start; use_index < segment_end; use_index += 1)
        {
            CToken use = preprocess.tokens[use_index];
            // A statement expression is a block, so its body binds against a
            // scope of its own rather than against this declaration's. Nothing
            // else opens that scope: the function-body walk hands this whole
            // statement over and resumes past its semicolon.
            u32 statement_body_start = 0;
            u32 statement_body_end = 0;
            u32 statement_group_end = 0;
            if (c_parse_statement_expression_at(preprocess, use_index, segment_end, &statement_body_start, &statement_body_end, &statement_group_end))
            {
                c_parse_bind_statement_expression_body(machine, arena, result, preprocess, scope, declaration_index, statement_body_start,
                                                       statement_body_end);
                use_index = statement_group_end;
                continue;
            }
            // An aggregate definition inside an initializer declares its
            // members; it does not use them. `int b = (union{float _f; int _i;}){x}._i`
            // is a compound literal, and reading `_f` as a use of an undeclared
            // name is how a libc's type punning failed here while the same
            // expression in a return statement compiled.
            u32 aggregate_end = 0;
            if (c_parse_aggregate_definition_at(preprocess, use_index, segment_end, &aggregate_end))
            {
                use_index = aggregate_end;
                continue;
            }
            if (use.kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, use), S8("__builtin_offsetof")) && use_index + 1 < segment_end &&
                c_token_is_punctuator(&preprocess.tokens[use_index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                u32 builtin_depth = 0;
                for (use_index += 1; use_index < segment_end; use_index += 1)
                {
                    if (c_token_is_punctuator(&preprocess.tokens[use_index], C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        builtin_depth += 1;
                    }
                    else if (c_token_is_punctuator(&preprocess.tokens[use_index], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                    {
                        if (builtin_depth)
                        {
                            builtin_depth -= 1;
                        }
                        if (!builtin_depth)
                        {
                            break;
                        }
                    }
                }
                continue;
            }
            bool member = use_index > initializer_start && (c_token_is_punctuator(&preprocess.tokens[use_index - 1], C_PUNCTUATOR_DOT) ||
                                                            c_token_is_punctuator(&preprocess.tokens[use_index - 1], C_PUNCTUATOR_ARROW));
            bool tag_name =
                use_index > initializer_start && preprocess.tokens[use_index - 1].kind == C_TOKEN_IDENTIFIER &&
                (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("struct")) ||
                 string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("union")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[use_index - 1]), S8("enum")));
            if (use.kind == C_TOKEN_IDENTIFIER && !c_parse_declaration_keyword_at(result, preprocess, use_index) && !member && !tag_name &&
                !(use_index && c_parse_label_address_prefix_with_typedef(result, &preprocess, scope, initializer_start, use_index - 1)) &&
                !c_parse_identifier_is_bound(result, use_index))
            {
                c_parse_bind_identifier(arena, result, preprocess, scope, use_index);
            }
        }
        segment_start = segment_end + 1;
    }
    return true;
}

BUSTER_C_INTERNAL void c_parse_bind_function_static_asserts(CTypeParseMachine* machine, Arena* scratch_arena, Arena* result_arena,
                                                              CParseResult* result, CPreprocessResult preprocess, CDeclaration* declaration);
BUSTER_C_INTERNAL void c_parse_bind_function_expression_aggregates(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                                     CDeclaration* declaration);

BUSTER_C_SHARED bool c_parse_label_address_prefix_proven(CPreprocessResult const* preprocess, u32 body_start, u32 index)
{
    if (body_start >= preprocess->token_count || index >= preprocess->token_count ||
        !c_token_is_punctuator(&preprocess->tokens[index], C_PUNCTUATOR_AMPERSAND_AMPERSAND))
    {
        return false;
    }
    if (index == body_start)
    {
        return true;
    }
    if (!index)
    {
        return false;
    }
    CToken previous = preprocess->tokens[index - 1];
    if (previous.kind == C_TOKEN_IDENTIFIER)
    {
        return string_equal(c_token_spelling(preprocess->spelling_base, previous), S8("return")) || string_equal(c_token_spelling(preprocess->spelling_base, previous), S8("case"));
    }
    if (previous.kind == C_TOKEN_PREPROCESSING_NUMBER || previous.kind == C_TOKEN_CHARACTER_LITERAL || previous.kind == C_TOKEN_STRING_LITERAL)
    {
        return false;
    }
    if (c_token_is_punctuator(&previous, C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        u32 depth = 0;
        u32 open = UINT32_MAX;
        for (u32 scan = index - 1; scan >= body_start; scan -= 1)
        {
            u8 punctuator = preprocess->tokens[scan].punctuator;
            if (c_punctuator_in_set(punctuator, C_PUNCTUATOR_SET_PARENTHESES))
            {
                if (punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS)
                {
                    depth += 1;
                }
                else
                {
                    if (!depth)
                    {
                        break;
                    }
                    depth -= 1;
                    if (!depth)
                    {
                        open = scan;
                        break;
                    }
                }
            }
            if (scan == body_start)
            {
                break;
            }
        }
        if (open != UINT32_MAX)
        {
            if (open > body_start && preprocess->tokens[open - 1].kind == C_TOKEN_IDENTIFIER &&
                (string_equal(c_token_spelling(preprocess->spelling_base, preprocess->tokens[open - 1]), S8("sizeof")) || c_parse_alignof_word(c_token_spelling(preprocess->spelling_base, preprocess->tokens[open - 1]))))
            {
                return false;
            }
            bool type_like = false;
            bool tag_name = false;
            for (u32 scan = open + 1; scan < index - 1; scan += 1)
            {
                CToken candidate = preprocess->tokens[scan];
                if (candidate.kind == C_TOKEN_IDENTIFIER)
                {
                    if (tag_name)
                    {
                        tag_name = false;
                        type_like = true;
                    }
                    else if (string_equal(c_token_spelling(preprocess->spelling_base, candidate), S8("struct")) || string_equal(c_token_spelling(preprocess->spelling_base, candidate), S8("union")) ||
                             string_equal(c_token_spelling(preprocess->spelling_base, candidate), S8("enum")))
                    {
                        tag_name = true;
                        type_like = true;
                    }
                    else if (c_parse_type_word_for_dialect_token(*preprocess, candidate))
                    {
                        type_like = true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else if (!c_token_is_punctuator(&candidate, C_PUNCTUATOR_STAR) && !c_token_is_punctuator(&candidate, C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                         !c_token_is_punctuator(&candidate, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    return false;
                }
            }
            if (type_like && !tag_name)
            {
                return true;
            }
        }
        return false;
    }
    return !c_token_is_punctuator(&previous, C_PUNCTUATOR_RIGHT_BRACKET) && !c_token_is_punctuator(&previous, C_PUNCTUATOR_PLUS_PLUS) &&
           !c_token_is_punctuator(&previous, C_PUNCTUATOR_MINUS_MINUS);
}

BUSTER_C_SHARED bool c_parse_label_address_prefix_with_typedef(CParseResult* result, CPreprocessResult const* preprocess, CScopeId scope,
                                                                    u32 body_start, u32 index)
{
    if (c_parse_label_address_prefix(preprocess, body_start, index))
    {
        return true;
    }
    if (!result || body_start >= preprocess->token_count || index >= preprocess->token_count ||
        !c_token_is_punctuator(&preprocess->tokens[index], C_PUNCTUATOR_AMPERSAND_AMPERSAND) || !index ||
        !c_token_is_punctuator(&preprocess->tokens[index - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        return false;
    }
    u32 depth = 0;
    u32 open = UINT32_MAX;
    for (u32 scan = index - 1; scan >= body_start; scan -= 1)
    {
        CToken token = preprocess->tokens[scan];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            if (!depth)
            {
                break;
            }
            depth -= 1;
            if (!depth)
            {
                open = scan;
                break;
            }
        }
        if (scan == body_start)
        {
            break;
        }
    }
    if (open == UINT32_MAX || open < body_start || open + 1 >= index - 1)
    {
        return false;
    }
    if (open > body_start && preprocess->tokens[open - 1].kind == C_TOKEN_IDENTIFIER &&
        (string_equal(c_token_spelling(preprocess->spelling_base, preprocess->tokens[open - 1]), S8("sizeof")) || c_parse_alignof_word(c_token_spelling(preprocess->spelling_base, preprocess->tokens[open - 1]))))
    {
        return false;
    }
    bool type_name = false;
    bool tag_name = false;
    for (u32 scan = open + 1; scan < index - 1; scan += 1)
    {
        CToken token = preprocess->tokens[scan];
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            if (tag_name)
            {
                tag_name = false;
                type_name = true;
            }
            else if (string_equal(c_token_spelling(preprocess->spelling_base, token), S8("struct")) || string_equal(c_token_spelling(preprocess->spelling_base, token), S8("union")) ||
                     string_equal(c_token_spelling(preprocess->spelling_base, token), S8("enum")))
            {
                tag_name = true;
                type_name = true;
            }
            else if (c_parse_type_start_token(result, *preprocess, scope, token))
            {
                type_name = true;
            }
            else
            {
                return false;
            }
        }
        else if (!c_token_is_punctuator(&token, C_PUNCTUATOR_STAR) && !c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                 !c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            return false;
        }
    }
    return type_name && !tag_name;
}

BUSTER_C_INTERNAL bool c_parse_asm_goto_label_range(CPreprocessResult preprocess, u32 start, u32 end, u32* label_start_out, u32* label_end_out)
{
    u32 open = start + 1;
    while (open < end && preprocess.tokens[open].kind == C_TOKEN_IDENTIFIER &&
           (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[open]), S8("volatile")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[open]), S8("__volatile__")) ||
            string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[open]), S8("inline")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[open]), S8("__inline__")) ||
            string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[open]), S8("goto"))))
    {
        open += 1;
    }
    if (open + 1 >= end || !c_token_is_punctuator(&preprocess.tokens[open], C_PUNCTUATOR_LEFT_PARENTHESIS) ||
        preprocess.tokens[open + 1].kind != C_TOKEN_STRING_LITERAL)
    {
        return false;
    }
    u32 close = c_parse_matching_delimiter(preprocess, open, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
    if (close >= end)
    {
        return false;
    }
    u32 template_end = open + 1;
    while (template_end < close && preprocess.tokens[template_end].kind == C_TOKEN_STRING_LITERAL)
    {
        template_end += 1;
    }
    u32 separators[4] = {0};
    u32 separator_count = 0;
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    for (u32 index = template_end; index < close; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            parentheses += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) && parentheses)
        {
            parentheses -= 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            brackets += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) && brackets)
        {
            brackets -= 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            braces += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE) && braces)
        {
            braces -= 1;
        }
        else if (!parentheses && !brackets && !braces && c_token_is_punctuator(&token, C_PUNCTUATOR_COLON))
        {
            if (separator_count == BUSTER_ARRAY_LENGTH(separators))
            {
                return false;
            }
            separators[separator_count++] = index;
        }
    }
    if (separator_count != 4)
    {
        return false;
    }
    *label_start_out = separators[3] + 1;
    *label_end_out = close;
    return true;
}

BUSTER_C_SHARED bool c_parse_asm_goto_qualifier(CPreprocessResult preprocess, u32 start, u32 end)
{
    u32 index = start + 1;
    while (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER)
    {
        String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
        if (string_equal(spelling, S8("goto")))
        {
            return true;
        }
        if (!string_equal(spelling, S8("volatile")) && !string_equal(spelling, S8("__volatile__")) && !string_equal(spelling, S8("inline")) &&
            !string_equal(spelling, S8("__inline__")))
        {
            break;
        }
        index += 1;
    }
    return false;
}

BUSTER_C_INTERNAL bool c_parse_asm_operand_name_token(CPreprocessResult preprocess, u32 open, u32 close, u32 token_index)
{
    if (open < close && close <= preprocess.token_count && token_index < close && preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER)
    {
        u32 separators[4] = {0};
        u32 separator_count = 0;
        u32 parentheses = 0;
        u32 brackets = 0;
        u32 braces = 0;
        u32 template_end = open + 1;
        while (template_end < close && preprocess.tokens[template_end].kind == C_TOKEN_STRING_LITERAL)
        {
            template_end += 1;
        }
        for (u32 index = template_end; index < close; index += 1)
        {
            u32 punctuator = preprocess.tokens[index].punctuator;
            if (punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS)
            {
                parentheses += 1;
            }
            else if (punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS && parentheses)
            {
                parentheses -= 1;
            }
            else if (punctuator == C_PUNCTUATOR_LEFT_BRACKET)
            {
                brackets += 1;
            }
            else if (punctuator == C_PUNCTUATOR_RIGHT_BRACKET && brackets)
            {
                brackets -= 1;
            }
            else if (punctuator == C_PUNCTUATOR_LEFT_BRACE)
            {
                braces += 1;
            }
            else if (punctuator == C_PUNCTUATOR_RIGHT_BRACE && braces)
            {
                braces -= 1;
            }
            else if (!parentheses && !brackets && !braces && punctuator == C_PUNCTUATOR_COLON)
            {
                if (separator_count >= BUSTER_ARRAY_LENGTH(separators))
                {
                    return false;
                }
                separators[separator_count++] = index;
            }
        }
        if (separator_count)
        {
            u32 ranges[4] = {
                separators[0] + 1,
                separator_count >= 2 ? separators[1] : close,
                separator_count >= 2 ? separators[1] + 1 : close,
                separator_count >= 3 ? separators[2] : close,
            };
            for (u32 range_index = 0; range_index < 2; range_index += 1)
            {
                u32 start = ranges[range_index * 2];
                u32 end = ranges[range_index * 2 + 1];
                if (token_index < start || token_index >= end)
                {
                    continue;
                }
                u32 segment_start = start;
                parentheses = 0;
                brackets = 0;
                braces = 0;
                for (u32 index = start; index <= token_index && index < end; index += 1)
                {
                    u32 punctuator = preprocess.tokens[index].punctuator;
                    if (punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS)
                    {
                        parentheses += 1;
                    }
                    else if (punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS && parentheses)
                    {
                        parentheses -= 1;
                    }
                    else if (punctuator == C_PUNCTUATOR_LEFT_BRACKET)
                    {
                        brackets += 1;
                    }
                    else if (punctuator == C_PUNCTUATOR_RIGHT_BRACKET && brackets)
                    {
                        brackets -= 1;
                    }
                    else if (punctuator == C_PUNCTUATOR_LEFT_BRACE)
                    {
                        braces += 1;
                    }
                    else if (punctuator == C_PUNCTUATOR_RIGHT_BRACE && braces)
                    {
                        braces -= 1;
                    }
                    else if (!parentheses && !brackets && !braces && punctuator == C_PUNCTUATOR_COMMA)
                    {
                        segment_start = index + 1;
                    }
                }
                return token_index == segment_start + 1 && segment_start + 2 < end &&
                       c_token_is_punctuator(&preprocess.tokens[segment_start], C_PUNCTUATOR_LEFT_BRACKET) &&
                       c_token_is_punctuator(&preprocess.tokens[segment_start + 2], C_PUNCTUATOR_RIGHT_BRACKET);
            }
        }
    }

    return false;
}

typedef enum CParseStatementSuffix
{
    C_PARSE_STATEMENT_SUFFIX_ELSE,
    C_PARSE_STATEMENT_SUFFIX_DO_WHILE,
} CParseStatementSuffix;

BUSTER_C_INTERNAL bool c_parse_statement_keyword_at(CPreprocessResult preprocess, u32 index, u32 end, String8 keyword)
{
    return index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER &&
           string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), keyword);
}

// One past the last token of the single statement beginning at `start`, or UINT32_MAX when that
// statement is not closed inside [start, end).
//
// The rule is the grammar's and not "the next semicolon at depth zero": a statement ends at its
// terminating `;` at delimiter depth zero, or past the `}` closing a compound statement, except
// that the constructs owning a substatement hold it open past that point. `if (c)`, `while (c)`,
// `for (...)`, `switch (c)` and `label:` prefix one more statement; `if` additionally admits a
// trailing `else` statement and `do` a trailing `while (c);`. Those two trailing forms are pushed
// onto `suffix` and discharged innermost-first, so `for (int i = 0; ...) if (a) b; else c;` spans
// the whole if-else rather than stopping at the `;` after `b`, and `do x; while (c);` covers its
// own terminator. `suffix_capacity` must be at least the token count of the range, since every
// obligation is introduced by a token of its own.
//
// Declarations, `_Generic`, statement expressions and every other construct that can hold a `;`
// inside a delimiter are covered by the depth count alone; nothing here needs to know their shape.
BUSTER_C_INTERNAL u32 c_parse_statement_end(CPreprocessResult preprocess, u32 start, u32 end, u8* suffix, u32 suffix_capacity)
{
    u32 cursor = start;
    u32 suffix_count = 0;
    while (cursor < end)
    {
        bool prefix = true;
        while (prefix && cursor < end && preprocess.tokens[cursor].kind == C_TOKEN_IDENTIFIER)
        {
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[cursor]);
            bool conditional = string_equal(spelling, S8("if"));
            if (conditional || string_equal(spelling, S8("while")) || string_equal(spelling, S8("for")) || string_equal(spelling, S8("switch")))
            {
                if (cursor + 1 >= end || !c_token_is_punctuator(&preprocess.tokens[cursor + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    return UINT32_MAX;
                }
                u32 header_close = c_parse_matching_delimiter(preprocess, cursor + 1, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                if (header_close == end)
                {
                    return UINT32_MAX;
                }
                if (conditional)
                {
                    if (suffix_count == suffix_capacity)
                    {
                        return UINT32_MAX;
                    }
                    suffix[suffix_count++] = C_PARSE_STATEMENT_SUFFIX_ELSE;
                }
                cursor = header_close + 1;
            }
            else if (string_equal(spelling, S8("do")))
            {
                if (suffix_count == suffix_capacity)
                {
                    return UINT32_MAX;
                }
                suffix[suffix_count++] = C_PARSE_STATEMENT_SUFFIX_DO_WHILE;
                cursor += 1;
            }
            else if (cursor + 1 < end && c_token_is_punctuator(&preprocess.tokens[cursor + 1], C_PUNCTUATOR_COLON))
            {
                cursor += 2;
            }
            else
            {
                prefix = false;
            }
        }
        if (cursor >= end)
        {
            return UINT32_MAX;
        }
        if (c_token_is_punctuator(&preprocess.tokens[cursor], C_PUNCTUATOR_LEFT_BRACE))
        {
            u32 close = c_parse_matching_delimiter(preprocess, cursor, end, C_PUNCTUATOR_LEFT_BRACE, C_PUNCTUATOR_RIGHT_BRACE);
            if (close == end)
            {
                return UINT32_MAX;
            }
            cursor = close + 1;
        }
        else
        {
            u32 depth = 0;
            while (cursor < end)
            {
                CToken token = preprocess.tokens[cursor];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                    c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                         c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
                {
                    if (!depth)
                    {
                        return UINT32_MAX;
                    }
                    depth -= 1;
                }
                else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON))
                {
                    break;
                }
                cursor += 1;
            }
            if (cursor == end)
            {
                return UINT32_MAX;
            }
            cursor += 1;
        }
        bool continued = false;
        while (suffix_count && !continued)
        {
            if (suffix[suffix_count - 1] == C_PARSE_STATEMENT_SUFFIX_DO_WHILE)
            {
                if (!c_parse_statement_keyword_at(preprocess, cursor, end, S8("while")) || cursor + 1 >= end ||
                    !c_token_is_punctuator(&preprocess.tokens[cursor + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    return UINT32_MAX;
                }
                u32 close = c_parse_matching_delimiter(preprocess, cursor + 1, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                if (close == end || close + 1 == end || !c_token_is_punctuator(&preprocess.tokens[close + 1], C_PUNCTUATOR_SEMICOLON))
                {
                    return UINT32_MAX;
                }
                cursor = close + 2;
                suffix_count -= 1;
                continue;
            }
            suffix_count -= 1;
            if (c_parse_statement_keyword_at(preprocess, cursor, end, S8("else")))
            {
                cursor += 1;
                continued = true;
            }
        }
        if (!continued)
        {
            return cursor;
        }
    }
    return UINT32_MAX;
}

// The block-statement walk of one brace-delimited range: `scope` is the scope
// the range sits directly in, `body_start` and `body_token_count` bound its
// tokens. It is separate from c_parse_bind_function_body because a GNU
// statement expression's body is a block too, and one written in a
// declaration's initializer is reached from c_parse_local_declarations rather
// than from the function-body walk -- which hands that whole declaration
// statement over and resumes past its semicolon.
BUSTER_C_INTERNAL void c_parse_bind_block_statements(CTypeParseMachine* machine, Arena* result_arena, CParseResult* result,
                                                       CPreprocessResult preprocess, u32 declaration_index, CScopeId scope, u32 body_start,
                                                       u32 body_token_count)
{
    CTokenShape const* token_shapes = c_preprocess_token_shapes(&preprocess);
    Arena* conflicts[] = {
        result_arena,
    };
    TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    CScopeId* scope_stack = arena_allocate(temporary.arena, CScopeId, body_token_count + 1);
    u32* scope_end_stack = arena_allocate(temporary.arena, u32, body_token_count + 1);
    u8* statement_suffix = arena_allocate(temporary.arena, u8, body_token_count + 1);
    u32 scope_count = 1;
    scope_stack[0] = scope;
    scope_end_stack[0] = UINT32_MAX;
    u32 body_end = body_start + body_token_count;
    u32 index = body_start;
    bool statement_start = true;
    u32 asm_goto_label_start = UINT32_MAX;
    u32 asm_goto_label_end = UINT32_MAX;
    u32 asm_operand_range_start = UINT32_MAX;
    u32 asm_operand_range_end = UINT32_MAX;
    while (index < body_end)
    {
        if (asm_goto_label_end != UINT32_MAX && index >= asm_goto_label_end)
        {
            asm_goto_label_start = UINT32_MAX;
            asm_goto_label_end = UINT32_MAX;
        }
        if (asm_operand_range_end != UINT32_MAX && index >= asm_operand_range_end)
        {
            asm_operand_range_start = UINT32_MAX;
            asm_operand_range_end = UINT32_MAX;
        }
        while (scope_count > 1 && scope_end_stack[scope_count - 1] == index)
        {
            scope_count -= 1;
        }
        // The identifiers inside a C23 attribute specifier are attribute
        // names, not uses of anything declared, so the binder steps over the
        // whole sequence. `statement_start` is deliberately left alone: an
        // attributed declaration or label still begins a statement. A block
        // declaration is recognized past its attributes below and consumes
        // its own range, so this only ever runs for the attribute sequences
        // that precede a statement or a label.
        u32 attribute_end = 0;
        if (c_parse_c23_attribute_at(preprocess, index, body_end, &attribute_end))
        {
            index = attribute_end;
            continue;
        }
        CToken token = preprocess.tokens[index];
        CTokenShape shape = c_preprocess_token_shape_at(token_shapes, &preprocess, index);
        CPunctuator punctuator = c_token_shape_punctuator(shape);
        if (shape == C_TOKEN_IDENTIFIER &&
            (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("asm")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__asm")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__asm__"))))
        {
            c_parse_asm_goto_label_range(preprocess, index, body_end, &asm_goto_label_start, &asm_goto_label_end);
            u32 asm_open = index + 1;
            while (asm_open < body_end && c_preprocess_token_shape_at(token_shapes, &preprocess, asm_open) == C_TOKEN_IDENTIFIER &&
                   (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[asm_open]), S8("volatile")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[asm_open]), S8("__volatile__")) ||
                    string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[asm_open]), S8("inline")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[asm_open]), S8("__inline__")) ||
                    string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[asm_open]), S8("goto"))))
            {
                asm_open += 1;
            }
            if (asm_open < body_end && c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, asm_open)) == C_PUNCTUATOR_LEFT_PARENTHESIS)
            {
                u32 asm_close = c_parse_matching_delimiter(preprocess, asm_open, body_end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                if (asm_close < body_end)
                {
                    asm_operand_range_start = asm_open;
                    asm_operand_range_end = asm_close;
                }
            }
        }
        if (shape == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__builtin_offsetof")) && index + 1 < body_end &&
            c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, index + 1)) == C_PUNCTUATOR_LEFT_PARENTHESIS)
        {
            u32 builtin_index = index + 1;
            u32 builtin_depth = 0;
            while (builtin_index < body_end)
            {
                if (c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, builtin_index)) == C_PUNCTUATOR_LEFT_PARENTHESIS)
                {
                    builtin_depth += 1;
                }
                else if (c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, builtin_index)) == C_PUNCTUATOR_RIGHT_PARENTHESIS)
                {
                    if (!builtin_depth)
                    {
                        break;
                    }
                    builtin_depth -= 1;
                    if (!builtin_depth)
                    {
                        builtin_index += 1;
                        break;
                    }
                }
                builtin_index += 1;
            }
            index = builtin_index;
            statement_start = false;
            continue;
        }
        if (punctuator == C_PUNCTUATOR_LEFT_BRACE)
        {
            CScopeId parent = scope_stack[scope_count - 1];
            CScopeId child = {
                .value = result->scope_count,
            };
            BUSTER_CHECK(result->scope_count < result->scope_capacity);
            result->scopes[result->scope_count++] = (CScope){
                .parent = parent,
                .first_entity = C_ENTITY_ID_INVALID,
                .last_entity = C_ENTITY_ID_INVALID,
                .token_start = index + 1,
                .token_end = UINT32_MAX,
            };
            scope_stack[scope_count++] = child;
            scope_end_stack[scope_count - 1] = UINT32_MAX;
            statement_start = true;
            index += 1;
            continue;
        }
        if (punctuator == C_PUNCTUATOR_RIGHT_BRACE)
        {
            if (scope_count > 1)
            {
                result->scopes[scope_stack[scope_count - 1].value].token_end = index;
                scope_count -= 1;
            }
            statement_start = true;
            index += 1;
            continue;
        }
        // `for` is reserved, so a token spelled that way followed by `(` is a for statement wherever
        // it appears; `statement_start` is deliberately not required. It is only set after `;`, `{`
        // and `}`, which misses every `for` that is itself the unbraced body of an enclosing
        // control statement — `for (...) for (int j = ...) ...` and `if (c) for (int j = ...) ...`.
        if (shape == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("for")) && index + 1 < body_end &&
            c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, index + 1)) == C_PUNCTUATOR_LEFT_PARENTHESIS)
        {
            u32 header_close = UINT32_MAX;
            u32 depth = 0;
            for (u32 scan = index + 1; scan < body_end; scan += 1)
            {
                if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    depth += 1;
                }
                else if (c_token_is_punctuator(&preprocess.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    if (!depth)
                    {
                        break;
                    }
                    depth -= 1;
                    if (!depth)
                    {
                        header_close = scan;
                        break;
                    }
                }
            }
            if (header_close != UINT32_MAX && header_close + 1 < body_end)
            {
                // The loop scope covers the whole controlled statement, compound or not, so the
                // init declaration stays visible across every form of body.
                u32 loop_end = c_parse_statement_end(preprocess, header_close + 1, body_end, statement_suffix, body_token_count + 1);
                u32 first_separator = UINT32_MAX;
                depth = 0;
                for (u32 scan = index + 2; scan < header_close; scan += 1)
                {
                    CToken scan_token = preprocess.tokens[scan];
                    if (c_token_is_punctuator(&scan_token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&scan_token, C_PUNCTUATOR_LEFT_BRACKET))
                    {
                        depth += 1;
                    }
                    else if (c_token_is_punctuator(&scan_token, C_PUNCTUATOR_RIGHT_PARENTHESIS) ||
                             c_token_is_punctuator(&scan_token, C_PUNCTUATOR_RIGHT_BRACKET))
                    {
                        if (depth)
                        {
                            depth -= 1;
                        }
                    }
                    else if (!depth && c_token_is_punctuator(&scan_token, C_PUNCTUATOR_SEMICOLON))
                    {
                        first_separator = scan;
                        break;
                    }
                }
                if (loop_end != UINT32_MAX && first_separator != UINT32_MAX)
                {
                    CScopeId parent = scope_stack[scope_count - 1];
                    CScopeId loop_scope = {
                        .value = result->scope_count,
                    };
                    BUSTER_CHECK(result->scope_count < result->scope_capacity);
                    result->scopes[result->scope_count++] = (CScope){
                        .parent = parent,
                        .first_entity = C_ENTITY_ID_INVALID,
                        .last_entity = C_ENTITY_ID_INVALID,
                        .token_start = index + 2,
                        .token_end = loop_end,
                    };
                    scope_stack[scope_count] = loop_scope;
                    scope_end_stack[scope_count] = loop_end;
                    scope_count += 1;
                    if (index + 2 < first_separator &&
                        c_parse_local_declarations(machine, result_arena, result, preprocess, loop_scope, declaration_index, index + 2,
                                                   first_separator))
                    {
                        index = first_separator + 1;
                        statement_start = false;
                        continue;
                    }
                }
            }
        }
        u32 declaration_type_start = c_parse_skip_attributes(preprocess, index, body_end);
        if (statement_start && declaration_type_start < body_end && c_preprocess_token_shape_at(token_shapes, &preprocess, declaration_type_start) == C_TOKEN_IDENTIFIER &&
            c_parse_type_start_token(result, preprocess, scope_stack[scope_count - 1], preprocess.tokens[declaration_type_start]))
        {
            if (!c_parse_type_word_for_dialect_token(preprocess, preprocess.tokens[declaration_type_start]) &&
                !c_parse_auto_type_word_token(preprocess, preprocess.tokens[declaration_type_start]))
            {
                c_parse_bind_identifier(result_arena, result, preprocess, scope_stack[scope_count - 1], declaration_type_start);
            }
            u32 end = index;
            u32 delimiter_depth = 0;
            while (end < body_end)
            {
                CToken end_token = preprocess.tokens[end];
                if (c_token_is_punctuator(&end_token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&end_token, C_PUNCTUATOR_LEFT_BRACKET) ||
                    c_token_is_punctuator(&end_token, C_PUNCTUATOR_LEFT_BRACE))
                {
                    delimiter_depth += 1;
                }
                else if (c_token_is_punctuator(&end_token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&end_token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                         c_token_is_punctuator(&end_token, C_PUNCTUATOR_RIGHT_BRACE))
                {
                    if (delimiter_depth)
                    {
                        delimiter_depth -= 1;
                    }
                }
                else if (!delimiter_depth && c_token_is_punctuator(&end_token, C_PUNCTUATOR_SEMICOLON))
                {
                    break;
                }
                end += 1;
            }
            CAutoDeclarationInfo auto_declaration_info = {0};
            bool auto_declaration = c_parse_auto_declaration_info(preprocess, index, end, &auto_declaration_info);
            if (end < body_end &&
                c_parse_local_declarations(machine, result_arena, result, preprocess, scope_stack[scope_count - 1], declaration_index, index, end))
            {
                index = end + 1;
                statement_start = true;
                continue;
            }
            if (auto_declaration && end < body_end)
            {
                index = end + 1;
                statement_start = true;
                continue;
            }
        }
        bool label = false;
        if (shape == C_TOKEN_IDENTIFIER)
        {
            label = c_ir_named_label_at(&preprocess, body_start, index, body_end);
            bool member = index > body_start && (c_token_is_punctuator(&preprocess.tokens[index - 1], C_PUNCTUATOR_DOT) ||
                                                 c_token_is_punctuator(&preprocess.tokens[index - 1], C_PUNCTUATOR_ARROW));
            bool tag_name = index > body_start && c_preprocess_token_shape_at(token_shapes, &preprocess, index - 1) == C_TOKEN_IDENTIFIER &&
                            (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index - 1]), S8("struct")) ||
                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index - 1]), S8("union")) ||
                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index - 1]), S8("enum")));
            bool goto_target = index > body_start && c_preprocess_token_shape_at(token_shapes, &preprocess, index - 1) == C_TOKEN_IDENTIFIER &&
                               string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index - 1]), S8("goto"));
            bool asm_goto_label = asm_goto_label_start != UINT32_MAX && index >= asm_goto_label_start && index < asm_goto_label_end;
            if (!member && !tag_name && !label && !goto_target && !asm_goto_label && !c_parse_declaration_keyword_at(result, preprocess, index) &&
                !(index > body_start &&
                  c_parse_label_address_prefix_with_typedef(result, &preprocess, scope_stack[scope_count - 1], body_start, index - 1)) &&
                !(asm_operand_range_start != UINT32_MAX &&
                  c_parse_asm_operand_name_token(preprocess, asm_operand_range_start, asm_operand_range_end, index)))
            {
                c_parse_bind_identifier(result_arena, result, preprocess, scope_stack[scope_count - 1], index);
            }
        }
        if (label)
        {
            statement_start = true;
            index += 2;
            continue;
        }
        statement_start = punctuator == C_PUNCTUATOR_SEMICOLON;
        index += 1;
    }
    scratch_end(temporary);
}

BUSTER_C_SHARED void c_parse_bind_function_body(CTypeParseMachine* machine, Arena* result_arena, CParseResult* result,
                                                    CPreprocessResult preprocess, u32 declaration_index)
{
    CDeclaration* declaration = &result->declarations[declaration_index];
    if (declaration->is_definition && declaration->body_token_count)
    {
        c_parse_bind_block_statements(machine, result_arena, result, preprocess, declaration_index, declaration->scope, declaration->body_start,
                                      declaration->body_token_count);
        Arena* conflicts[] = {
            result_arena,
        };
        TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
        c_parse_bind_function_static_asserts(machine, temporary.arena, result_arena, result, preprocess, declaration);
        // After the walk above, because it needs the block scopes that walk creates.
        c_parse_bind_function_expression_aggregates(machine, result, preprocess, declaration);
        scratch_end(temporary);
    }
}

BUSTER_C_INTERNAL bool c_parse_type_only_declaration(CPreprocessResult preprocess, u32 start, u32 end)
{
    // GNU attributes may sit before the aggregate keyword, between it and the
    // tag (`struct __attribute__((__may_alias__)) sockaddr_storage { ... };`,
    // which is how glibc declares it), and after the tag. Skipping them keeps
    // such a definition classified as a type-only declaration; read as an
    // object declaration instead, the attribute's name becomes the declared
    // object and the aggregate is never defined.
    u32 index = c_parse_skip_attributes(preprocess, start, end);
    while (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER &&
           (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("const")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("volatile")) ||
            string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("_Atomic")) || string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("static")) ||
            string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("extern"))))
    {
        index = c_parse_skip_attributes(preprocess, index + 1, end);
    }
    if (index >= end || preprocess.tokens[index].kind != C_TOKEN_IDENTIFIER ||
        (!string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("struct")) && !string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("union")) &&
         !string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), S8("enum"))))
    {
        return false;
    }
    index = c_parse_skip_attributes(preprocess, index + 1, end);
    if (index < end && preprocess.tokens[index].kind == C_TOKEN_IDENTIFIER)
    {
        index += 1;
    }
    index = c_parse_skip_attributes(preprocess, index, end);
    if (index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_COLON))
    {
        while (index < end && !c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
        {
            index += 1;
        }
    }
    if (index < end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
    {
        u32 depth = 1;
        index += 1;
        while (index < end && depth)
        {
            if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_RIGHT_BRACE))
            {
                depth -= 1;
            }
            index += 1;
        }
        if (depth)
        {
            return false;
        }
    }
    index = c_parse_skip_attributes(preprocess, index, end);
    return index + 1 == end && c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_SEMICOLON);
}

BUSTER_C_SHARED void c_parse_index_scope_children(CParseResult* result, Arena* arena)
{
    u32 scope_count = result->scope_count;
    u32* offsets = arena_allocate(arena, u32, (u64)scope_count + 1);
    memset(offsets, 0, sizeof(*offsets) * ((u64)scope_count + 1));
    for (u32 scope_index = 0; scope_index < scope_count; scope_index += 1)
    {
        CScopeId parent = result->scopes[scope_index].parent;
        if (parent.value < scope_count && parent.value != scope_index)
        {
            offsets[parent.value + 1] += 1;
        }
    }
    for (u32 scope_index = 0; scope_index < scope_count; scope_index += 1)
    {
        offsets[scope_index + 1] += offsets[scope_index];
    }
    u32* children = arena_allocate(arena, u32, offsets[scope_count]);
    u32* cursors = arena_allocate(arena, u32, scope_count);
    memset(cursors, 0, sizeof(*cursors) * scope_count);
    for (u32 scope_index = 0; scope_index < scope_count; scope_index += 1)
    {
        CScopeId parent = result->scopes[scope_index].parent;
        if (parent.value < scope_count && parent.value != scope_index)
        {
            children[offsets[parent.value] + cursors[parent.value]] = scope_index;
            cursors[parent.value] += 1;
        }
    }
    result->scope_children_offsets = offsets;
    result->scope_children = children;
}

BUSTER_C_SHARED CScopeId c_parse_scope_for_token(CParseResult* result, CScopeId root, u32 token_index)
{
    if (!result || root.value >= result->scope_count)
    {
        return root;
    }
    if (result->scope_children_offsets)
    {
        // Sibling scopes never overlap, so at most one child of the current
        // scope contains the token (equal-range parent/child pairs resolve to
        // the child, matching the full scan's index tie-break), and the
        // deepest containing scope under the root is the scan's answer.
        CScopeId best = root;
        bool descended = true;
        while (descended)
        {
            descended = false;
            u32 child_end = result->scope_children_offsets[best.value + 1];
            for (u32 child_index = result->scope_children_offsets[best.value]; child_index < child_end; child_index += 1)
            {
                u32 candidate = result->scope_children[child_index];
                CScope* scope = &result->scopes[candidate];
                if (scope->token_start <= token_index && token_index < scope->token_end)
                {
                    best.value = candidate;
                    descended = true;
                }
            }
        }
        return best;
    }
    CScopeId best = root;
    u32 best_start = result->scopes[root.value].token_start;
    for (u32 scope_index = 0; scope_index < result->scope_count; scope_index += 1)
    {
        CScope* candidate = &result->scopes[scope_index];
        if (candidate->token_start > token_index || candidate->token_end <= token_index)
        {
            continue;
        }
        CScopeId ancestor = {
            .value = scope_index,
        };
        bool under_root = false;
        while (ancestor.value != C_ID_UNDERLYING_INVALID && ancestor.value < result->scope_count)
        {
            if (ancestor.value == root.value)
            {
                under_root = true;
                break;
            }
            ancestor = result->scopes[ancestor.value].parent;
        }
        if (under_root && (candidate->token_start > best_start || (candidate->token_start == best_start && scope_index > best.value)))
        {
            best.value = scope_index;
            best_start = candidate->token_start;
        }
    }
    return best;
}

// An aggregate definition that appears inside a parenthesis is a type name,
// not a declaration: `(union{float _f; uint32_t _i;}){x}` is a compound
// literal, `(struct S{int x;}*)p` a cast, `sizeof(struct{int x;})` an operand.
// The declaration paths never see these, so the type they define would be
// registered nowhere and the lowering would meet an aggregate keyword it could
// not resolve -- which is how most of a libc's math library, whose type
// punning is written exactly this way, failed to compile.
//
// The `(` is what makes the shape unambiguous. A declaration's specifiers
// cannot be preceded by one, so nothing a declaration owns is registered twice
// here, and the check that no type already carries this definition keeps a
// range that is somehow reached twice from defining two types.
BUSTER_C_INTERNAL void c_parse_bind_function_expression_aggregates(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                                     CDeclaration* declaration)
{
    u32 body_end = declaration->body_start + declaration->body_token_count;
    if (body_end > preprocess.token_count)
    {
        body_end = (u32)preprocess.token_count;
    }
    for (u32 index = declaration->body_start; index + 2 < body_end; index += 1)
    {
        if (!c_token_is_punctuator(&preprocess.tokens[index], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            continue;
        }
        u32 keyword = index + 1;
        u32 close = 0;
        if (!c_parse_aggregate_definition_at(preprocess, keyword, body_end, &close))
        {
            continue;
        }
        u32 open = keyword + 1;
        if (preprocess.tokens[open].kind == C_TOKEN_IDENTIFIER)
        {
            open += 1;
        }
        bool defined = false;
        for (u32 type_index = 0; type_index < result->type_count && !defined; type_index += 1)
        {
            defined = result->types[type_index].definition_start == open + 1;
        }
        if (defined)
        {
            index = close;
            continue;
        }
        u32 declarator_start = 0;
        CScopeId scope = c_parse_scope_for_token(result, declaration->scope, keyword);
        c_parse_scalar_type_in_scope(machine, result, preprocess, scope, keyword, close + 1, &declarator_start);
        index = close;
    }
}

BUSTER_C_INTERNAL void c_parse_bind_function_static_asserts(CTypeParseMachine* machine, Arena* scratch_arena, Arena* result_arena,
                                                              CParseResult* result, CPreprocessResult preprocess, CDeclaration* declaration)
{
    // The tree walk below chases a pointer per statement of the body to find
    // the _Static_assert statements, and a body that has none is the common
    // case by a wide margin: the parser records the answer as it appends the
    // statements, and the two chasing lines were 1,17% of the compile's DRAM
    // fills in the cache-miss survey of 2026-08-22T114019Z.
    if (!declaration->syntax_body || !declaration->syntax_declaration || !declaration->syntax_declaration->body_has_static_assert)
    {
        return;
    }
    CParserStatement** stack = arena_allocate(scratch_arena, CParserStatement*, declaration->body_token_count + 1);
    u32 stack_count = 0;
    CParserStatement* statement = declaration->syntax_body;
    while (statement || stack_count)
    {
        if (!statement)
        {
            statement = stack[--stack_count];
        }
        if (statement->next)
        {
            BUSTER_CHECK(stack_count < declaration->body_token_count + 1);
            stack[stack_count++] = statement->next;
        }
        if (statement->kind == C_PARSER_STATEMENT_STATIC_ASSERT)
        {
            CScopeId scope = c_parse_scope_for_token(result, declaration->scope, statement->token_start);
            c_parse_static_assert_check(machine, result_arena, preprocess, result,
                                        (CDeclaration){
                                            .token_start = statement->token_start,
                                            .token_count = statement->token_count,
                                        },
                                        scope);
        }
        statement = statement->first_child;
    }
}

BUSTER_C_INTERNAL void c_parser_diagnostic(CParserResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)
{
    if (!result || result->diagnostic_count >= result->diagnostic_capacity)
    {
        return;
    }
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = location,
        .kind = kind,
    };
}

typedef struct CParserBlockFrame CParserBlockFrame;
struct CParserBlockFrame
{
    CParserStatement* block;
    u32 statement_start;
    u32 open_index;
    u32 parenthesis_depth;
    u32 bracket_depth;
};

BUSTER_C_INTERNAL bool c_parser_statement_is_declaration(char8 const* spelling_base, CToken token, CPreprocessDialect dialect)
{
    if (token.kind != C_TOKEN_IDENTIFIER)
    {
        return false;
    }
    String8 spelling = c_token_spelling(spelling_base, token);
    return c_parse_type_word_for_dialect(spelling, dialect) || string_equal(spelling, S8("struct")) ||
           string_equal(spelling, S8("union")) || string_equal(spelling, S8("enum")) || string_equal(spelling, S8("typedef")) ||
           string_equal(spelling, S8("static")) || string_equal(spelling, S8("extern")) || string_equal(spelling, S8("_Thread_local")) ||
           string_equal(spelling, S8("__thread")) || string_equal(spelling, S8("__attribute__")) || string_equal(spelling, S8("__declspec"));
}

BUSTER_C_INTERNAL CParserStatementKind c_parser_statement_kind(CPreprocessResult preprocess, u32 start, u32 end)
{
    if (start >= end || end > preprocess.token_count)
    {
        return C_PARSER_STATEMENT_UNKNOWN;
    }
    // A C23 attribute specifier sequence may precede any statement or block
    // declaration, so the token that decides the kind is the first one past
    // it. `[[fallthrough]];` skips to its own semicolon and is classified as
    // the null statement it is.
    for (u32 attribute_end = 0; c_parse_c23_attribute_at(preprocess, start, end, &attribute_end);)
    {
        start = attribute_end;
    }
    if (start >= end)
    {
        return C_PARSER_STATEMENT_EXPRESSION;
    }
    CToken first = preprocess.tokens[start];
    CParserStatementKind result;
    if (first.kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, first), S8("_Static_assert")))
    {
        result = C_PARSER_STATEMENT_STATIC_ASSERT;
    }
    else if (start + 1 < end && first.kind == C_TOKEN_IDENTIFIER && c_token_is_punctuator(&preprocess.tokens[start + 1], C_PUNCTUATOR_COLON))
    {
        result = C_PARSER_STATEMENT_LABEL;
    }
    else if (c_parser_statement_is_declaration(preprocess.spelling_base, first, preprocess.dialect))
    {
        result = C_PARSER_STATEMENT_DECLARATION;
    }
    else
    {
        result = C_PARSER_STATEMENT_EXPRESSION;
    }

    return result;
}

BUSTER_C_INTERNAL CParserStatement* c_parser_statement_make(Arena* arena, CPreprocessResult preprocess, u32 start, u32 end,
                                                               CParserStatementKind kind)
{
    if (!arena || start >= end || end > preprocess.token_count)
    {
        return 0;
    }
    CParserStatement* statement = arena_allocate(arena, CParserStatement, 1);
    u32 expression_end = end;
    if (expression_end > start && c_token_is_punctuator(&preprocess.tokens[expression_end - 1], C_PUNCTUATOR_SEMICOLON))
    {
        expression_end -= 1;
    }
    *statement = (CParserStatement){
        .expression =
            {
                .token_start = start,
                .token_count = expression_end - start,
            },
        .location = c_preprocess_token_location(&preprocess, preprocess.tokens[start]),
        .token_start = start,
        .token_count = end - start,
        .kind = kind,
    };
    return statement;
}

BUSTER_C_INTERNAL void c_parser_statement_append(CParserDeclaration* declaration, CParserStatement* parent, CParserStatement* statement)
{
    declaration->body_has_static_assert |= statement->kind == C_PARSER_STATEMENT_STATIC_ASSERT;
    CParserStatement** first = parent ? &parent->first_child : &declaration->first_statement;
    CParserStatement** last = parent ? &parent->last_child : &declaration->last_statement;
    if (*last)
    {
        (*last)->next = statement;
    }
    else
    {
        *first = statement;
    }
    *last = statement;
}

BUSTER_C_INTERNAL void c_parser_parse_function_body(Arena* arena, CPreprocessResult preprocess, CParserDeclaration* declaration)
{
    u32 body_start = declaration->body_start;
    u32 body_end = body_start + declaration->body_token_count;
    CParserBlockFrame* frames = arena_allocate(arena, CParserBlockFrame, declaration->body_token_count + 1);
    u32 frame_count = 1;
    frames[0] = (CParserBlockFrame){
        .statement_start = body_start,
    };
    u32 index = body_start;
    while (index < body_end)
    {
        CParserBlockFrame* frame = &frames[frame_count - 1];
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            frame->parenthesis_depth += 1;
            index += 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            frame->parenthesis_depth -= frame->parenthesis_depth != 0;
            index += 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            frame->bracket_depth += 1;
            index += 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET))
        {
            frame->bracket_depth -= frame->bracket_depth != 0;
            index += 1;
            continue;
        }
        if (!frame->parenthesis_depth && !frame->bracket_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            CParserStatement* block = c_parser_statement_make(arena, preprocess, frame->statement_start, index + 1, C_PARSER_STATEMENT_BLOCK);
            if (!block)
            {
                return;
            }
            block->expression.token_count = index - frame->statement_start;
            block->body_start = index + 1;
            c_parser_statement_append(declaration, frame->block, block);
            BUSTER_CHECK(frame_count < declaration->body_token_count + 1);
            frames[frame_count++] = (CParserBlockFrame){
                .block = block,
                .statement_start = index + 1,
                .open_index = index,
            };
            index += 1;
            continue;
        }
        if (!frame->parenthesis_depth && !frame->bracket_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            if (frame_count == 1)
            {
                index += 1;
                continue;
            }
            if (frame->statement_start < index)
            {
                CParserStatement* statement = c_parser_statement_make(arena, preprocess, frame->statement_start, index,
                                                                        c_parser_statement_kind(preprocess, frame->statement_start, index));
                if (statement)
                {
                    c_parser_statement_append(declaration, frame->block, statement);
                }
            }
            CParserStatement* block = frame->block;
            block->token_count = index + 1 - block->token_start;
            block->body_token_count = index - block->body_start;
            frame_count -= 1;
            frames[frame_count - 1].statement_start = index + 1;
            index += 1;
            continue;
        }
        if (!frame->parenthesis_depth && !frame->bracket_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON))
        {
            if (frame->statement_start < index + 1)
            {
                CParserStatement* statement = c_parser_statement_make(arena, preprocess, frame->statement_start, index + 1,
                                                                        c_parser_statement_kind(preprocess, frame->statement_start, index + 1));
                if (statement)
                {
                    c_parser_statement_append(declaration, frame->block, statement);
                }
            }
            frame->statement_start = index + 1;
        }
        index += 1;
    }
    while (frame_count > 1)
    {
        CParserBlockFrame* frame = &frames[frame_count - 1];
        if (frame->statement_start < body_end)
        {
            CParserStatement* statement = c_parser_statement_make(arena, preprocess, frame->statement_start, body_end,
                                                                    c_parser_statement_kind(preprocess, frame->statement_start, body_end));
            if (statement)
            {
                c_parser_statement_append(declaration, frame->block, statement);
            }
        }
        frame->block->token_count = body_end - frame->block->token_start;
        frame->block->body_token_count = body_end - frame->block->body_start;
        frame_count -= 1;
    }
    if (frames[0].statement_start < body_end)
    {
        CParserStatement* statement = c_parser_statement_make(arena, preprocess, frames[0].statement_start, body_end,
                                                                c_parser_statement_kind(preprocess, frames[0].statement_start, body_end));
        if (statement)
        {
            c_parser_statement_append(declaration, 0, statement);
        }
    }
}

BUSTER_C_INTERNAL void c_parser_parse_declaration_expression(CPreprocessResult preprocess, CParserDeclaration* declaration)
{
    // A declarator split out of a list owns only its own initializer; an
    // unsplit declaration owns its whole range.
    u32 start = declaration->declarator_count ? declaration->declarator_start : declaration->token_start;
    u32 end = declaration->declarator_count ? declaration->declarator_start + declaration->declarator_count
                                            : declaration->token_start + declaration->token_count;
    if (declaration->kind == C_PARSER_DECLARATION_STATIC_ASSERT && start + 1 < end &&
        c_token_is_punctuator(&preprocess.tokens[start + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        u32 depth = 1;
        u32 expression_start = start + 2;
        for (u32 index = expression_start; index < end; index += 1)
        {
            CToken token = preprocess.tokens[index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                depth -= depth != 0;
            }
            else if (depth == 1 && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
            {
                declaration->expression = (CParserExpression){
                    .token_start = expression_start,
                    .token_count = index - expression_start,
                };
                return;
            }
        }
        return;
    }
    u32 depth = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            depth -= depth != 0;
        }
        else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
        {
            u32 expression_end = end;
            if (expression_end > index + 1 && c_token_is_punctuator(&preprocess.tokens[expression_end - 1], C_PUNCTUATOR_SEMICOLON))
            {
                expression_end -= 1;
            }
            declaration->expression = (CParserExpression){
                .token_start = index + 1,
                .token_count = expression_end - (index + 1),
            };
            return;
        }
    }
}

// Answered at the closing parenthesis of a declarator's top-level group, which
// `delimiter_count` still counts: `...` is the last parameter a parameter list
// may have, so the token before that ')' is the only place an ellipsis makes
// the declarator variadic. The declarator's own parameter list is that one
// top-level group -- `f(int, ...)`, and `(*f)(int, ...)` once the
// parenthesized declarator has closed. Any other ellipsis spells someone
// else's parameter list: a parameter's own (`f(int (*cb)(int, ...))`) or a
// function type named inside an array bound (`g[sizeof(int(int, ...))]`).
// Both scans below used to take any ellipsis in the declarator's token range,
// which gave a fixed-arity function a variadic type -- calls through it then
// skipped the arity check and the definition paid a variadic prologue.
BUSTER_C_INTERNAL bool c_parser_parameter_list_is_variadic(CPreprocessResult preprocess, u32 close_index, u32 delimiter_count, bool top_level_parenthesis)
{
    return delimiter_count == 1 && top_level_parenthesis && c_token_is_punctuator(&preprocess.tokens[close_index - 1], C_PUNCTUATOR_ELLIPSIS);
}

// One declarator of a file-scope comma-separated list. `start`/`count` cover
// the segment between two top-level separators; for the first declarator that
// range still carries the declaration specifiers, which is why the name scan
// below is the same last-identifier rule the whole-declaration scan uses.
typedef struct CParserDeclarator CParserDeclarator;
struct CParserDeclarator
{
    u32 name_token;
    u32 function_name_token;
    bool seen_equal;
    bool is_variadic;
};

BUSTER_C_INTERNAL CParserDeclarator c_parser_scan_declarator(CPreprocessResult preprocess, u32 start, u32 end)
{
    CParserDeclarator declarator = {
        .name_token = C_ID_UNDERLYING_INVALID,
        .function_name_token = C_ID_UNDERLYING_INVALID,
    };
    u32 delimiter_count = 0;
    bool top_level_parenthesis = false;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        u32 asm_label_end = 0;
        if (token.kind == C_TOKEN_IDENTIFIER && c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ASM_KEYWORDS) && !delimiter_count &&
            !declarator.seen_equal && c_parse_asm_label_at(preprocess, index, end, &asm_label_end))
        {
            index = asm_label_end - 1;
            continue;
        }
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            bool keyword = c_declaration_keyword_for_dialect_token(preprocess, token);
            if (!delimiter_count && !declarator.seen_equal && !keyword)
            {
                declarator.name_token = index;
            }
            else if (declarator.name_token == C_ID_UNDERLYING_INVALID && !keyword)
            {
                declarator.name_token = index;
            }
        }
        if (!delimiter_count && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
        {
            declarator.seen_equal = true;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            if (!delimiter_count && c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) && !declarator.seen_equal &&
                declarator.function_name_token == C_ID_UNDERLYING_INVALID)
            {
                u32 parenthesized_name_token = C_ID_UNDERLYING_INVALID;
                bool parenthesized_function = c_parse_parenthesized_function_name(preprocess, index, end, &parenthesized_name_token);
                bool ordinary_function = index > start && preprocess.tokens[index - 1].kind == C_TOKEN_IDENTIFIER &&
                                         !c_declaration_keyword_for_dialect_token(preprocess, preprocess.tokens[index - 1]);
                if (parenthesized_function)
                {
                    declarator.name_token = parenthesized_name_token;
                    // `(*f)(...)` declares an object holding a function
                    // pointer; the redundant plain `(f)(...)` form and the
                    // pointer-returning `(*f(...))(...)` form are function
                    // declarations in the AST.
                    if (!c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_STAR) ||
                        c_parse_declarator_name_has_parameters(preprocess, parenthesized_name_token, end))
                    {
                        declarator.function_name_token = parenthesized_name_token;
                    }
                }
                else if (ordinary_function)
                {
                    declarator.function_name_token = index - 1;
                    declarator.name_token = declarator.function_name_token;
                }
                else if (declarator.name_token == C_ID_UNDERLYING_INVALID &&
                         c_parse_parenthesized_declarator_name(preprocess, index, end, &parenthesized_name_token))
                {
                    declarator.name_token = parenthesized_name_token;
                }
            }
            if (!delimiter_count)
            {
                top_level_parenthesis = c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            }
            delimiter_count += 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
            c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            declarator.is_variadic |= c_parser_parameter_list_is_variadic(preprocess, index, delimiter_count, top_level_parenthesis);
            delimiter_count -= delimiter_count != 0;
        }
    }
    return declarator;
}

// End of the declarator that begins at `start`: the first top-level ',' or the
// end of the declarator list. Delimiters keep an aggregate body, a parameter
// list, an array bound, and a braced initializer from being read as separators.
BUSTER_C_INTERNAL u32 c_parser_declarator_list_segment_end(CPreprocessResult preprocess, u32 start, u32 end)
{
    u32 delimiter_count = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (c_punctuator_in_set(token.punctuator, C_PUNCTUATOR_SET_DELIMITER_OPEN))
        {
            delimiter_count += 1;
        }
        else if (c_punctuator_in_set(token.punctuator, C_PUNCTUATOR_SET_DELIMITER_CLOSE))
        {
            delimiter_count -= delimiter_count != 0;
        }
        else if (!delimiter_count && token.punctuator == C_PUNCTUATOR_COMMA)
        {
            return index;
        }
    }
    return end;
}

CParserResult c_parse_ast(Arena* arena, CPreprocessResult preprocess)
{
    CParserResult result = {0};
    CTokenShape const* token_shapes = c_preprocess_token_shapes(&preprocess);
    if (arena && preprocess.tokens && preprocess.token_count && preprocess.token_count <= (UINT32_MAX - 1) / 2)
    {
        u32 token_count = (u32)preprocess.token_count;
        result.declaration_capacity = token_count + 1;
        result.diagnostic_capacity = token_count + 1;
        result.diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_capacity);
        u32* delimiter_stack = arena_allocate(arena, u32, token_count + 1);
        u32 index = 0;
        while (index < token_count && c_preprocess_token_shape_at(token_shapes, &preprocess, index) != C_TOKEN_END_OF_FILE)
        {
            u32 start = index;
            u32 delimiter_count = 0;
            u32 body_start = 0;
            u32 body_token_count = 0;
            u32 name_token = C_ID_UNDERLYING_INVALID;
            u32 function_name_token = C_ID_UNDERLYING_INVALID;
            bool is_typedef = false;
            bool is_constexpr = false;
            bool seen_equal = false;
            bool seen_declarator_comma = false;
            bool is_variadic = false;
            bool top_level_parenthesis = false;
            bool ended = false;
            while (index < token_count)
            {
                CToken token = preprocess.tokens[index];
                CTokenShape shape = c_preprocess_token_shape_at(token_shapes, &preprocess, index);
                CPunctuator punctuator = c_token_shape_punctuator(shape);
                if (shape == C_TOKEN_END_OF_FILE)
                {
                    break;
                }
                u32 asm_label_end = 0;
                if (shape == C_TOKEN_IDENTIFIER && c_token_in_well_known_set(preprocess.spelling_base, token, C_PARSE_ASM_KEYWORDS) && index > start &&
                    !delimiter_count && !seen_equal && c_parse_asm_label_at(preprocess, index, token_count, &asm_label_end))
                {
                    index = asm_label_end;
                    continue;
                }
                if (shape == C_TOKEN_IDENTIFIER)
                {
                    is_typedef |= string_equal(c_token_spelling(preprocess.spelling_base, token), S8("typedef"));
                    is_constexpr |= c_preprocess_dialect_is_c23(preprocess.dialect) && string_equal(c_token_spelling(preprocess.spelling_base, token), S8("constexpr"));
                    if (!delimiter_count && !seen_equal && !seen_declarator_comma && !c_declaration_keyword_for_dialect_token(preprocess, token))
                    {
                        name_token = index;
                    }
                    else if (name_token == C_ID_UNDERLYING_INVALID && !c_declaration_keyword_for_dialect_token(preprocess, token))
                    {
                        name_token = index;
                    }
                }
                seen_declarator_comma |= !delimiter_count && punctuator == C_PUNCTUATOR_COMMA;
                if (!delimiter_count && punctuator == C_PUNCTUATOR_ASSIGN)
                {
                    seen_equal = true;
                }
                if (c_punctuator_in_set((u8)punctuator, C_PUNCTUATOR_SET_DELIMITER_OPEN))
                {
                    if (!delimiter_count && punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS && !seen_equal &&
                        function_name_token == C_ID_UNDERLYING_INVALID)
                    {
                        u32 parenthesized_name_token = C_ID_UNDERLYING_INVALID;
                        bool parenthesized_function = c_parse_parenthesized_function_name(preprocess, index, token_count, &parenthesized_name_token);
                        bool ordinary_function = index > start && c_preprocess_token_shape_at(token_shapes, &preprocess, index - 1) == C_TOKEN_IDENTIFIER &&
                                                  !c_declaration_keyword_for_dialect_token(preprocess, preprocess.tokens[index - 1]);
                        if (parenthesized_function)
                        {
                            name_token = parenthesized_name_token;
                            if (!c_token_is_punctuator(&preprocess.tokens[index + 1], C_PUNCTUATOR_STAR) ||
                                c_parse_declarator_name_has_parameters(preprocess, parenthesized_name_token, token_count))
                            {
                                function_name_token = parenthesized_name_token;
                            }
                        }
                        else if (ordinary_function)
                        {
                            function_name_token = index - 1;
                            name_token = function_name_token;
                        }
                        else if (name_token == C_ID_UNDERLYING_INVALID &&
                                 c_parse_parenthesized_declarator_name(preprocess, index, token_count, &parenthesized_name_token))
                        {
                            name_token = parenthesized_name_token;
                        }
                    }
                    if (!delimiter_count && punctuator == C_PUNCTUATOR_LEFT_BRACE && function_name_token != C_ID_UNDERLYING_INVALID && !seen_equal)
                    {
                        body_start = index + 1;
                        u32 brace_depth = 1;
                        index += 1;
                        while (index < token_count)
                        {
                            CPunctuator body_punctuator = c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, index));
                            if (body_punctuator == C_PUNCTUATOR_LEFT_BRACE)
                            {
                                brace_depth += 1;
                            }
                            else if (body_punctuator == C_PUNCTUATOR_RIGHT_BRACE)
                            {
                                brace_depth -= 1;
                                if (!brace_depth)
                                {
                                    body_token_count = index - body_start;
                                    index += 1;
                                    ended = true;
                                    break;
                                }
                            }
                            index += 1;
                        }
                        if (!ended)
                        {
                            c_parser_diagnostic(&result, c_preprocess_token_location(&preprocess, token), C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("unterminated function body"));
                        }
                        break;
                    }
                    if (!delimiter_count)
                    {
                        top_level_parenthesis = punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS;
                    }
                    delimiter_stack[delimiter_count++] = index;
                    index += 1;
                    continue;
                }
                if (c_punctuator_in_set((u8)punctuator, C_PUNCTUATOR_SET_DELIMITER_CLOSE))
                {
                    CPunctuator expected = punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS ? C_PUNCTUATOR_LEFT_PARENTHESIS
                                           : punctuator == C_PUNCTUATOR_RIGHT_BRACKET   ? C_PUNCTUATOR_LEFT_BRACKET
                                                                                          : C_PUNCTUATOR_LEFT_BRACE;
                    if (!delimiter_count || c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, delimiter_stack[delimiter_count - 1])) != expected)
                    {
                        c_parser_diagnostic(&result, c_preprocess_token_location(&preprocess, token), C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("unmatched closing delimiter"));
                    }
                    else
                    {
                        is_variadic |= c_parser_parameter_list_is_variadic(preprocess, index, delimiter_count, top_level_parenthesis);
                        delimiter_count -= 1;
                    }
                    index += 1;
                    continue;
                }
                if (!delimiter_count && punctuator == C_PUNCTUATOR_SEMICOLON)
                {
                    index += 1;
                    ended = true;
                    break;
                }
                index += 1;
            }
            if (!ended)
            {
                CSourceLocation location = c_preprocess_token_location(&preprocess, preprocess.tokens[BUSTER_MIN(index, token_count - 1)]);
                c_parser_diagnostic(&result, location, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("expected ';' or a function body after declaration"));
                break;
            }
            CParserDeclarationKind kind = is_typedef                         ? C_PARSER_DECLARATION_TYPEDEF
                                          : function_name_token != C_ID_UNDERLYING_INVALID ? C_PARSER_DECLARATION_FUNCTION
                                          : (c_preprocess_token_shape_at(token_shapes, &preprocess, start) == C_TOKEN_IDENTIFIER &&
                                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]), S8("_Static_assert")))
                                                ? C_PARSER_DECLARATION_STATIC_ASSERT
                                          : (c_preprocess_token_shape_at(token_shapes, &preprocess, start) == C_TOKEN_IDENTIFIER &&
                                             (string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]), S8("asm")) ||
                                              string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]), S8("__asm")) ||
                                              string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[start]), S8("__asm__"))))
                                                ? C_PARSER_DECLARATION_ASSEMBLY
                                          : c_parse_type_only_declaration(preprocess, start, index) ? C_PARSER_DECLARATION_TYPE
                                          : name_token != C_ID_UNDERLYING_INVALID ? C_PARSER_DECLARATION_OBJECT
                                                                                   : C_PARSER_DECLARATION_UNKNOWN;
            // A declarator list binds one name per declarator, but the scan above
            // tracks only the first: it stops updating name_token at the first
            // top-level comma. Split the declaration so every declarator gets its
            // own segment, its own name and its own initializer, the way the
            // block-scope path in c_parse_local_declarations already does. The
            // declaration specifiers stay shared through token_start/token_count,
            // which is what the storage-class and attribute scans read.
            u32 declarator_list_end = index;
            if (declarator_list_end > start && c_token_shape_punctuator(c_preprocess_token_shape_at(token_shapes, &preprocess, declarator_list_end - 1)) == C_PUNCTUATOR_SEMICOLON)
            {
                declarator_list_end -= 1;
            }
            bool split_declarators = seen_declarator_comma && !body_start && kind != C_PARSER_DECLARATION_STATIC_ASSERT &&
                                     kind != C_PARSER_DECLARATION_ASSEMBLY && kind != C_PARSER_DECLARATION_TYPE;
            u32 segment_start = start;
            bool continuation = false;
            while (true)
            {
                u32 segment_end = split_declarators ? c_parser_declarator_list_segment_end(preprocess, segment_start, declarator_list_end)
                                                    : declarator_list_end;
                CParserDeclaration* declaration = arena_allocate(arena, CParserDeclaration, 1);
                *declaration = (CParserDeclaration){
                    .location = c_preprocess_token_location(&preprocess, preprocess.tokens[start]),
                    .token_start = start,
                    .token_count = index - start,
                    .declarator_start = split_declarators ? segment_start : 0,
                    .declarator_count = split_declarators ? segment_end - segment_start : 0,
                    .body_start = body_start,
                    .body_token_count = body_token_count,
                    .name_token = name_token,
                    .function_name_token = function_name_token,
                    .kind = kind,
                    .is_definition = body_start != 0 || (kind == C_PARSER_DECLARATION_OBJECT && seen_equal),
                    .is_typedef = is_typedef,
                    .is_constexpr = is_constexpr,
                    .is_variadic = is_variadic,
                    .seen_equal = seen_equal,
                    .is_declarator_continuation = continuation,
                };
                if (split_declarators)
                {
                    CParserDeclarator declarator = c_parser_scan_declarator(preprocess, segment_start, segment_end);
                    declaration->name_token = declarator.name_token;
                    declaration->function_name_token = declarator.function_name_token;
                    declaration->seen_equal = declarator.seen_equal;
                    declaration->is_variadic = declarator.is_variadic;
                    declaration->kind = is_typedef                                                   ? C_PARSER_DECLARATION_TYPEDEF
                                        : declarator.function_name_token != C_ID_UNDERLYING_INVALID ? C_PARSER_DECLARATION_FUNCTION
                                        : declarator.name_token != C_ID_UNDERLYING_INVALID          ? C_PARSER_DECLARATION_OBJECT
                                                                                                    : C_PARSER_DECLARATION_UNKNOWN;
                    declaration->is_definition = declaration->kind == C_PARSER_DECLARATION_OBJECT && declarator.seen_equal;
                }
                c_parser_parse_declaration_expression(preprocess, declaration);
                if (declaration->kind == C_PARSER_DECLARATION_FUNCTION && body_token_count)
                {
                    c_parser_parse_function_body(arena, preprocess, declaration);
                }
                if (result.last_declaration)
                {
                    result.last_declaration->next = declaration;
                }
                else
                {
                    result.first_declaration = declaration;
                }
                result.last_declaration = declaration;
                result.declaration_count += 1;
                if (!split_declarators || segment_end >= declarator_list_end)
                {
                    break;
                }
                // An empty trailing segment ("int a, ;") would otherwise be emitted
                // with a zero declarator count, which reads as an unsplit
                // declaration owning the whole range.
                segment_start = segment_end + 1;
                if (segment_start >= declarator_list_end)
                {
                    break;
                }
                continuation = true;
            }
        }
    }

    return result;
}
BUSTER_C_SHARED String8 c_ir_unsupported_gnu_construct(CPreprocessResult preprocess, u32 start, u32 end, u32* token_index_out);

// The census of token shapes that sizes every table c_analyze_semantics
// allocates: each counter below becomes one array's capacity, so all nine are
// upper bounds that must come out exactly as c_parse_token_census_reference
// computes them. The vector path is a throughput change and nothing else.
typedef struct CTokenCensus CTokenCensus;
struct CTokenCensus
{
    u32 identifier_count;
    u32 semicolon_count;
    u32 comma_count;
    u32 declarator_list_comma_count;
    u32 open_parenthesis_count;
    u32 open_bracket_count;
    u32 open_brace_count;
    u32 for_count;
    u32 maximum_delimiter_depth;
};

// The scalar reference, and the definition of the answer. An optimized
// non-unity build with the 512-bit vocabulary available calls it from
// neither arm below, hence BUSTER_UNUSED_DECL. c_parse_token_census
// runs this whenever the 512-bit vocabulary is unavailable, and
// c_parse_token_census_differential in the tests runs both and compares all
// nine counters.
BUSTER_C_INTERNAL BUSTER_UNUSED_DECL void c_parse_token_census_reference(CPreprocessResult preprocess, u32 token_count, CTokenCensus* census)
{
    u32 brace_depth = 0;
    u32 delimiter_depth = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            census->identifier_count += 1;
            census->for_count += c_token_is_well_known(preprocess.spelling_base, token, C_SYMBOL_WELL_KNOWN_FOR);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON))
        {
            census->semicolon_count += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
        {
            census->comma_count += 1;
            // An upper bound on the declarations a file-scope declarator list
            // adds, since each split declarator becomes its own CDeclaration.
            // Only brace depth gates it: c_parse_ast diagnoses every brace
            // imbalance and c_analyze_semantics returns before this capacity is
            // used, while a stray '(' inside a function body goes undiagnosed
            // and would hide the file-scope commas that follow it. Counting
            // parameter-list and attribute commas here is the price of a bound
            // that can only ever be too large.
            census->declarator_list_comma_count += brace_depth == 0;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            census->open_parenthesis_count += 1;
            delimiter_depth += 1;
            census->maximum_delimiter_depth = BUSTER_MAX(census->maximum_delimiter_depth, delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            census->open_bracket_count += 1;
            delimiter_depth += 1;
            census->maximum_delimiter_depth = BUSTER_MAX(census->maximum_delimiter_depth, delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            census->open_brace_count += 1;
            brace_depth += 1;
            delimiter_depth += 1;
            census->maximum_delimiter_depth = BUSTER_MAX(census->maximum_delimiter_depth, delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            brace_depth -= brace_depth != 0 && c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE);
            delimiter_depth -= delimiter_depth != 0;
        }
    }
}

#if BUSTER_SIMD_512

// The census reads its kind|punctuator sidecar as one contiguous byte stream,
// so every pure shape count is one compare, one popcount and one add over 64
// tokens. The rare `for` spelling filter still projects the symbol low byte
// from the 12-byte rows with the fixed indices below; keeping that projection
// separate avoids revisiting every identifier's spelling.
BUSTER_CT_CHECK(sizeof(CToken) == 12);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CToken, symbol) == 4);

#define C_PARSE_CENSUS_GROUP_TOKENS 16
// Symbol bytes are projected only for the rare `for` spelling filter; the
// kind|punctuator stream is already contiguous in the preprocessor sidecar.
// The tile keeps the projected symbols in L1 beside the rows being gathered,
// and the padding keeps that auxiliary stream readable in whole windows.
#define C_PARSE_CENSUS_TILE_TOKENS C_PARSE_TOKEN_TILE_TOKENS
#define C_PARSE_CENSUS_TILE_BYTES (C_PARSE_CENSUS_TILE_TOKENS + 64)

// The symbol byte of token 10 is still at 124, so its stream splits one token
// later than the first ten rows in a 128-byte pair.
#define C_PARSE_CENSUS_SYMBOL_LOW_LANES UINT64_C(0x07ff)
#define C_PARSE_CENSUS_SYMBOL_HIGH_LANES UINT64_C(0xf800)
#define C_PARSE_CENSUS_GROUP_LANES UINT64_C(0xffff)

// Source byte of the projected symbol field, per output lane: 12 * token + 4
// for the low pair, less 64 for the high pair, which starts at chunk 1.
BUSTER_C_INTERNAL const u8 c_parse_census_symbol_low[64] = {4, 16, 28, 40, 52, 64, 76, 88, 100, 112, 124};
BUSTER_C_INTERNAL const u8 c_parse_census_symbol_high[64] = {[11] = 72, 84, 96, 108, 120};

#endif

// Nine counters over the preprocessed token stream. Seven are pure counts and
// fall out of masked compares; the two that carry state across tokens --
// maximum_delimiter_depth and the count of commas at brace depth 0 -- cannot
// be counted lanewise, so they replay the reference's depth arithmetic,
// clamps included, over the set bits of the delimiter masks the counting pass
// already produced. That is 23% of tokens rather than all of them, and the
// commas inside each brace-depth-0 span arrive as a popcount of the comma
// mask rather than a visit per comma.
//
// The scalar fallback is the reference rather than this kernel run through
// simd.h's fallback: without 512-bit lanes every simd512_load here would be a
// 64-iteration byte loop, which is the case AGENTS.md means by a different
// algorithm being worth writing.
BUSTER_C_INTERNAL void c_parse_token_census(CPreprocessResult preprocess, u32 token_count, CTokenCensus* census)
{
#if BUSTER_SIMD_512
    u8 symbol_bytes[C_PARSE_CENSUS_TILE_BYTES];
    Simd512 symbol_low_indices = simd512_load(c_parse_census_symbol_low);
    Simd512 symbol_high_indices = simd512_load(c_parse_census_symbol_high);
    CTokenShape const* token_shapes = c_preprocess_token_shapes(&preprocess);
    if (token_shapes)
    {
        Simd512 identifier_shape = simd512_splat((u8)C_TOKEN_IDENTIFIER);
        Simd512 semicolon = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_SEMICOLON));
        Simd512 comma = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_COMMA));
        Simd512 open_parenthesis = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_PARENTHESIS));
        Simd512 open_bracket = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_BRACKET));
        Simd512 open_brace = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_LEFT_BRACE));
        Simd512 close_parenthesis = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_PARENTHESIS));
        Simd512 close_bracket = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_BRACKET));
        Simd512 close_brace = simd512_splat((u8)(C_TOKEN_SHAPE_PUNCTUATOR | C_PUNCTUATOR_RIGHT_BRACE));
        Simd512 for_symbol = simd512_splat((u8)C_SYMBOL_WELL_KNOWN_FOR);
        Simd512 uninterned_symbol = simd512_splat(0);
        u32 brace_depth = 0;
        u32 delimiter_depth = 0;
        u32 maximum_depth = 0;
        // Commas in stream order, and the count standing when the open
        // brace-depth-0 span began; their difference is that span's contribution.
        u64 comma_total = 0;
        u64 depth_zero_comma_base = 0;
        for (u32 tile_base = 0; tile_base < token_count; tile_base += C_PARSE_CENSUS_TILE_TOKENS)
        {
            u32 tile_tokens = BUSTER_MIN(C_PARSE_CENSUS_TILE_TOKENS, token_count - tile_base);
            u32 projected = 0;
            while (projected + C_PARSE_CENSUS_GROUP_TOKENS <= tile_tokens)
            {
                const u8* rows = (const u8*)(preprocess.tokens + tile_base + projected);
                Simd512 chunk0 = simd512_load(rows);
                Simd512 chunk1 = simd512_load(rows + 64);
                Simd512 chunk2 = simd512_load(rows + 128);
                Simd512 symbol_group = simd512_or(simd512_permute2_byte(C_PARSE_CENSUS_SYMBOL_LOW_LANES, chunk0, symbol_low_indices, chunk1),
                                                  simd512_permute2_byte(C_PARSE_CENSUS_SYMBOL_HIGH_LANES, chunk1, symbol_high_indices, chunk2));
                simd512_store_masked(symbol_bytes + projected, C_PARSE_CENSUS_GROUP_LANES, symbol_group);
                projected += C_PARSE_CENSUS_GROUP_TOKENS;
            }
            while (projected < tile_tokens)
            {
                CToken token = preprocess.tokens[tile_base + projected];
                symbol_bytes[projected] = (u8)token.symbol;
                projected += 1;
            }
            // Pad the projected symbol bytes to a whole window. The sidecar is
            // loaded with a tail mask, so its padding never needs to be written.
            u32 padded = (tile_tokens + 63) & ~UINT32_C(63);
            while (projected < padded)
            {
                symbol_bytes[projected] = 0;
                projected += 1;
            }
            for (u32 window = 0; window < padded; window += 64)
            {
                u32 window_tokens = BUSTER_MIN(64, tile_tokens - window);
                Mask64 window_mask = mask64_prefix(window_tokens);
                Simd512 shape_lanes = simd512_load_masked(token_shapes + tile_base + window, window_mask);
                Mask64 identifiers = simd512_equal_byte(shape_lanes, identifier_shape);
                Mask64 semicolons = simd512_equal_byte(shape_lanes, semicolon);
                Mask64 commas = simd512_equal_byte(shape_lanes, comma);
                Mask64 open_parentheses = simd512_equal_byte(shape_lanes, open_parenthesis);
                Mask64 open_brackets = simd512_equal_byte(shape_lanes, open_bracket);
                Mask64 open_braces = simd512_equal_byte(shape_lanes, open_brace);
                Mask64 close_braces = simd512_equal_byte(shape_lanes, close_brace);
                Mask64 opens = mask64_or(mask64_or(open_parentheses, open_brackets), open_braces);
                Mask64 closes =
                    mask64_or(mask64_or(simd512_equal_byte(shape_lanes, close_parenthesis), simd512_equal_byte(shape_lanes, close_bracket)), close_braces);
                census->identifier_count += mask64_count(identifiers);
                census->semicolon_count += mask64_count(semicolons);
                census->comma_count += mask64_count(commas);
                census->open_parenthesis_count += mask64_count(open_parentheses);
                census->open_bracket_count += mask64_count(open_brackets);
                census->open_brace_count += mask64_count(open_braces);
                // `for` is one interned id, so its low symbol byte is the filter,
                // and the zero byte admits the uninterned tokens whose answer is
                // the spelling fallback. The two together leave a few thousand
                // candidates in a million identifiers, each then answered by the
                // predicate the reference calls.
                Simd512 symbol_lanes = simd512_load(symbol_bytes + window);
                Mask64 for_candidates =
                    mask64_and(identifiers, mask64_or(simd512_equal_byte(symbol_lanes, for_symbol), simd512_equal_byte(symbol_lanes, uninterned_symbol)));
                for (Mask64 remaining = for_candidates; remaining; remaining &= remaining - 1)
                {
                    u32 lane = mask64_first_set(remaining);
                    census->for_count += c_token_is_well_known(preprocess.spelling_base, preprocess.tokens[tile_base + window + lane], C_SYMBOL_WELL_KNOWN_FOR);
                }
                u64 window_comma_base = comma_total;
                comma_total += mask64_count(commas);
                for (Mask64 remaining = mask64_or(opens, closes); remaining; remaining &= remaining - 1)
                {
                    u32 lane = mask64_first_set(remaining);
                    if ((opens >> lane) & 1)
                    {
                        delimiter_depth += 1;
                        maximum_depth = BUSTER_MAX(maximum_depth, delimiter_depth);
                        if ((open_braces >> lane) & 1)
                        {
                            if (!brace_depth)
                            {
                                census->declarator_list_comma_count +=
                                    (u32)(window_comma_base + mask64_count(mask64_and(commas, mask64_prefix(lane))) - depth_zero_comma_base);
                            }
                            brace_depth += 1;
                        }
                    }
                    else
                    {
                        if (brace_depth && ((close_braces >> lane) & 1))
                        {
                            brace_depth -= 1;
                            if (!brace_depth)
                            {
                                depth_zero_comma_base = window_comma_base + mask64_count(mask64_and(commas, mask64_prefix(lane)));
                            }
                        }
                        delimiter_depth -= delimiter_depth != 0;
                    }
                }
            }
        }
        // The span still open at end of stream. When a brace is unclosed there is
        // none, and the reference counted nothing more either.
        if (!brace_depth)
        {
            census->declarator_list_comma_count += (u32)(comma_total - depth_zero_comma_base);
        }
        census->maximum_delimiter_depth = maximum_depth;
    }
    else
    {
        c_parse_token_census_reference(preprocess, token_count, census);
    }
#if !BUSTER_OPTIMIZE
    // The differential gate. Unoptimized builds recompute the census the
    // reference way and require all nine counters to agree, which puts the
    // check on every translation unit the suite compiles rather than on a
    // hand-written list of token streams -- every fixture, every header of
    // the include closure, and the whole unity unit when the Debug tree
    // compiles it. Release pays nothing.
    CTokenCensus reference_census = {0};
    c_parse_token_census_reference(preprocess, token_count, &reference_census);
    BUSTER_CHECK(memcmp(&reference_census, census, sizeof(reference_census)) == 0);
#endif
#else
    c_parse_token_census_reference(preprocess, token_count, census);
#endif
}

// Does a same-named file-scope entity declare the same thing this
// declaration does, so that the two are one entity rather than a conflict?
// A function declared through a type name instead of a parameter-list
// declarator -- `extern __typeof(f) g;`, and the same through a typedef --
// has no function declarator, so the declaration is filed as an object one;
// musl's weak_alias() publishes every one of its public names that way, and
// without this each of them would conflict with its own prototype instead of
// redeclaring it. Only the entity-kind gate is relaxed: type compatibility
// still decides the match, so `int f(void); extern int f;` stays a conflict.
BUSTER_C_INTERNAL bool c_parse_entity_kind_redeclares(CEntityKind candidate, CEntityKind declared, bool declares_function_type)
{
    bool function_shaped = (candidate == C_ENTITY_FUNCTION || candidate == C_ENTITY_OBJECT) &&
                           (declared == C_ENTITY_FUNCTION || declared == C_ENTITY_OBJECT);
    return candidate == declared || (function_shaped && declares_function_type);
}

BUSTER_C_INTERNAL CAnalysisResult c_analyze_semantics(Arena* arena, CPreprocessResult preprocess, CParserResult syntax)
{
    CParseResult result = {
        .arena = arena,
        .symbols = preprocess.symbols,
    };
    if (syntax.diagnostic_count)
    {
        result.diagnostics = syntax.diagnostics;
        result.diagnostic_count = syntax.diagnostic_count;
        return result;
    }
    if (!arena || !preprocess.tokens || !preprocess.token_count)
    {
        return result;
    }
    if (preprocess.token_count > (UINT32_MAX - 1) / 2)
    {
        return result;
    }
    u32 token_count = (u32)preprocess.token_count;
    CTokenCensus census = {0};
    c_parse_token_census(preprocess, token_count, &census);
    u32 identifier_count = census.identifier_count;
    u32 semicolon_count = census.semicolon_count;
    u32 comma_count = census.comma_count;
    u32 declarator_list_comma_count = census.declarator_list_comma_count;
    u32 open_parenthesis_count = census.open_parenthesis_count;
    u32 open_bracket_count = census.open_bracket_count;
    u32 open_brace_count = census.open_brace_count;
    u32 for_count = census.for_count;
    u32 maximum_delimiter_depth = census.maximum_delimiter_depth;
    if (maximum_delimiter_depth > (UINT32_MAX - 64) / 8 || token_count == UINT32_MAX)
    {
        return result;
    }
    u32 type_frame_capacity = maximum_delimiter_depth * 4 + 32;
    u32 type_mutation_capacity = maximum_delimiter_depth * 8 + 64;
    u32 expression_task_capacity = token_count + 1;
    u64 promoted_member_capacity_u64 = (u64)token_count * 2 + 1;
    if (promoted_member_capacity_u64 > UINT32_MAX)
    {
        return result;
    }
    u32 promoted_member_capacity = (u32)promoted_member_capacity_u64;
    u32 incomplete_array_chain_capacity = (u32)promoted_member_capacity_u64;
    u64 machine_buffer_size = arena_minimum_position;
    if (!c_type_parse_buffer_size_add(&machine_buffer_size, type_frame_capacity, sizeof(CTypeParseFrame), BUSTER_ALIGN_OF(CTypeParseFrame)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, type_mutation_capacity, sizeof(CTypeMutation), BUSTER_ALIGN_OF(CTypeMutation)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, expression_task_capacity, sizeof(CParseExpressionTypeTask),
                                      BUSTER_ALIGN_OF(CParseExpressionTypeTask)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, promoted_member_capacity, sizeof(CParsePromotedMemberWork),
                                      BUSTER_ALIGN_OF(CParsePromotedMemberWork)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, promoted_member_capacity, sizeof(u32), BUSTER_ALIGN_OF(u32)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, incomplete_array_chain_capacity, sizeof(CTypeId), BUSTER_ALIGN_OF(CTypeId)) ||
        machine_buffer_size > UINT64_MAX - (BUSTER_KB(64) - 1))
    {
        return result;
    }
    machine_buffer_size = (machine_buffer_size + BUSTER_KB(64) - 1) & ~(BUSTER_KB(64) - 1);
    Arena* machine_buffer_arena = arena_create((ArenaCreation){
        .reserved_size = machine_buffer_size,
        .granularity = BUSTER_KB(64),
        .initial_size = BUSTER_MIN(machine_buffer_size, BUSTER_KB(256)),
    });
    if (!machine_buffer_arena)
    {
        return result;
    }
    Arena* machine_conflicts[] = {
        arena,
    };
    TemporalArena machine_temporary = scratch_begin(machine_conflicts, BUSTER_ARRAY_LENGTH(machine_conflicts));
    CTypeParseMachine machine = {
        .frames = arena_allocate(machine_buffer_arena, CTypeParseFrame, type_frame_capacity),
        .mutations = arena_allocate(machine_buffer_arena, CTypeMutation, type_mutation_capacity),
        .expression_tasks = arena_allocate(machine_buffer_arena, CParseExpressionTypeTask, expression_task_capacity),
        .incomplete_array_chain = arena_allocate(machine_buffer_arena, CTypeId, incomplete_array_chain_capacity),
        .incomplete_array_chain_capacity = incomplete_array_chain_capacity,
        .scratch_arena = machine_temporary.arena,
        .layout_cache = {.tokens = preprocess.tokens},
        .promoted_member_work = arena_allocate(machine_buffer_arena, CParsePromotedMemberWork, promoted_member_capacity),
        .promoted_member_visited = arena_allocate(machine_buffer_arena, u32, promoted_member_capacity),
        .promoted_member_capacity = promoted_member_capacity,
        .frame_capacity = type_frame_capacity,
        .mutation_capacity = type_mutation_capacity,
        .expression_task_capacity = expression_task_capacity,
    };
    result.declaration_capacity = semicolon_count + open_brace_count + declarator_list_comma_count + 1;
    result.type_capacity = token_count * 2 + 1;
    result.parameter_capacity = comma_count + open_parenthesis_count + 1;
    result.member_capacity = identifier_count + semicolon_count + 1;
    result.enum_member_capacity = identifier_count + 1;
    result.array_bound_capacity = open_bracket_count + 1;
    result.alignment_capacity = token_count + 1;
    // One entry per aggregate definition bounds the table; almost every
    // translation unit fills a handful of them or none at all.
    result.aggregate_attribute_capacity = open_brace_count + 1;
    result.entity_capacity = identifier_count + 1;
    result.scope_capacity = open_brace_count + open_parenthesis_count + for_count + 1;
    result.identifier_use_capacity = identifier_count + 1;
    result.identifier_use_by_token_capacity = token_count + 1;
    result.deferred_static_assert_capacity = token_count + 1;
    result.diagnostic_capacity = token_count + 1;
    result.declarations = arena_allocate(arena, CDeclaration, result.declaration_capacity);
    result.types = arena_allocate(arena, CType, result.type_capacity);
    result.parameters = arena_allocate(arena, CParameter, result.parameter_capacity);
    result.members = arena_allocate(arena, CMember, result.member_capacity);
    result.enum_members = arena_allocate(arena, CEnumMember, result.enum_member_capacity);
    result.array_bounds = arena_allocate(arena, CArrayBound, result.array_bound_capacity);
    result.alignments = arena_allocate(arena, CAlignmentSpecifier, result.alignment_capacity);
    result.aggregate_attributes = arena_allocate(arena, CAggregateAttributes, result.aggregate_attribute_capacity);
    result.entities = arena_allocate(arena, CEntity, result.entity_capacity);
    result.scopes = arena_allocate(arena, CScope, result.scope_capacity);
    result.deferred_static_asserts = arena_allocate(arena, CDeferredStaticAssert, result.deferred_static_assert_capacity);
    // The buckets bound distinct lookup *names*, not identifier-token
    // occurrences: sizing off entity_capacity (one slot per identifier
    // token) built three ~8 MB tables, memset per parse, for a few tens of
    // thousands of entities. The interned-name count bounds the distinct
    // keys instead; names interned on demand during the parse only deepen
    // chains, never break lookups, because every table is chained through
    // next_in_lookup/next_by_name. Hand-built inputs without a symbol table
    // keep the token-derived bound.
    u64 desired_lookup_bucket_count =
        result.symbols ? ((u64)result.symbols->count + 1) * 2 : (u64)result.entity_capacity * 2;
    result.entity_lookup_bucket_count = 1;
    while ((u64)result.entity_lookup_bucket_count < desired_lookup_bucket_count && result.entity_lookup_bucket_count <= UINT32_MAX / 2)
    {
        result.entity_lookup_bucket_count *= 2;
    }
    result.entity_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.entity_lookup_buckets, 0xff, sizeof(*result.entity_lookup_buckets) * result.entity_lookup_bucket_count);
    result.typedef_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.typedef_lookup_buckets, 0xff, sizeof(*result.typedef_lookup_buckets) * result.entity_lookup_bucket_count);
    result.name_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.name_lookup_buckets, 0xff, sizeof(*result.name_lookup_buckets) * result.entity_lookup_bucket_count);
    {
        u32 aggregate_slot_count = 16384;
        result.aggregate_lookup = arena_allocate(arena, CAggregateLookup, 1);
        *result.aggregate_lookup = (CAggregateLookup){
            .slots = arena_allocate(arena, CAggregateLookupSlot, aggregate_slot_count),
            .slot_count = aggregate_slot_count,
        };
        memset(result.aggregate_lookup->slots, 0, sizeof(*result.aggregate_lookup->slots) * aggregate_slot_count);
    }
    result.position_index = arena_allocate(arena, CTokenPositionIndex, 1);
    *result.position_index = (CTokenPositionIndex){0};
    result.identifier_uses = arena_allocate(arena, CIdentifierUse, result.identifier_use_capacity);
    result.identifier_use_by_token = arena_allocate(arena, u32, result.identifier_use_by_token_capacity);
    memset(result.identifier_use_by_token, 0xff, sizeof(*result.identifier_use_by_token) * result.identifier_use_by_token_capacity);
    result.token_classes = arena_allocate(arena, u8, result.identifier_use_by_token_capacity);
    memset(result.token_classes, 0, sizeof(*result.token_classes) * result.identifier_use_by_token_capacity);
    result.diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_capacity);
    BUSTER_CHECK(result.scope_count < result.scope_capacity);
    result.scopes[result.scope_count++] = (CScope){
        .parent = C_SCOPE_ID_INVALID,
        .first_entity = C_ENTITY_ID_INVALID,
        .last_entity = C_ENTITY_ID_INVALID,
        .token_start = 0,
        .token_end = UINT32_MAX,
    };
    CTypeId declarator_list_base = C_TYPE_ID_INVALID;
    for (CParserDeclaration* syntax_declaration = syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        u32 start = syntax_declaration->token_start;
        u32 index = start + syntax_declaration->token_count;
        u32 body_start = syntax_declaration->body_start;
        u32 body_count = syntax_declaration->body_token_count;
        bool is_typedef = syntax_declaration->is_typedef;
        bool is_constexpr = syntax_declaration->is_constexpr;
        bool variadic = syntax_declaration->is_variadic;
        String8 function_name = syntax_declaration->function_name_token < preprocess.token_count
                                    ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[syntax_declaration->function_name_token])
                                    : (String8){0};
        CSourceLocation function_location = syntax_declaration->function_name_token < preprocess.token_count
                                                ? c_preprocess_token_location(&preprocess, preprocess.tokens[syntax_declaration->function_name_token])
                                                : (CSourceLocation){0};
        String8 object_name = syntax_declaration->name_token < preprocess.token_count ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[syntax_declaration->name_token]) : (String8){0};
        CSourceLocation object_location = syntax_declaration->name_token < preprocess.token_count
                                              ? c_preprocess_token_location(&preprocess, preprocess.tokens[syntax_declaration->name_token])
                                              : (CSourceLocation){0};
        bool static_assertion = syntax_declaration->kind == C_PARSER_DECLARATION_STATIC_ASSERT;
        bool global_assembly = syntax_declaration->kind == C_PARSER_DECLARATION_ASSEMBLY;
        bool type_only = static_assertion || syntax_declaration->kind == C_PARSER_DECLARATION_TYPE;
        CDeclarationKind kind = global_assembly        ? C_DECLARATION_ASSEMBLY
                                : type_only            ? C_DECLARATION_TYPE
                                : is_typedef           ? C_DECLARATION_TYPEDEF
                                : function_name.length ? C_DECLARATION_FUNCTION
                                                       : C_DECLARATION_OBJECT;
        String8 name = kind == C_DECLARATION_FUNCTION ? function_name : object_name;
        CSourceLocation location = kind == C_DECLARATION_ASSEMBLY   ? c_preprocess_token_location(&preprocess, preprocess.tokens[start])
                                   : kind == C_DECLARATION_FUNCTION ? function_location
                                                                    : object_location;
        BUSTER_CHECK(result.declaration_count < result.declaration_capacity);
        CDeclaration* declaration = &result.declarations[result.declaration_count++];
        *declaration = (CDeclaration){
            .name = name,
            .location = location,
            .token_start = start,
            .token_count = index - start,
            .declarator_start = syntax_declaration->declarator_start,
            .declarator_count = syntax_declaration->declarator_count,
            .body_start = body_start,
            .body_token_count = body_count,
            .type = C_TYPE_ID_INVALID,
            .base_type = C_TYPE_ID_INVALID,
            .entity = C_ENTITY_ID_INVALID,
            .scope = C_SCOPE_ID_INVALID,
            .syntax_declaration = syntax_declaration,
            .syntax_body = syntax_declaration->first_statement,
            .kind = kind,
            .is_definition = syntax_declaration->is_definition,
            .is_variadic = variadic,
            .is_constexpr = is_constexpr,
            .is_declarator_continuation = syntax_declaration->is_declarator_continuation,
        };
        if (!static_assertion && !global_assembly)
        {
            // Declarators split out of one list share the specifiers the first
            // of them parsed. The syntax list keeps them consecutive, so the
            // leader's base type is still the one in hand.
            CTypeId inherited_base = declaration->is_declarator_continuation ? declarator_list_base : C_TYPE_ID_INVALID;
            c_parse_declaration_type(&machine, &result, preprocess, declaration, inherited_base);
            if (!declaration->is_declarator_continuation)
            {
                declarator_list_base = declaration->base_type;
            }
            if (declaration->kind == C_DECLARATION_OBJECT && declaration->type.value < result.type_count)
            {
                CTypeId object_type = C_TYPE_ID_INVALID;
                if (!c_parse_clone_incomplete_array_declarator(&machine, &result, declaration->type, &object_type))
                {
                    c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                       S8("compiler resource limit while materializing incomplete array declaration"));
                }
                else
                {
                    declaration->type = object_type;
                }
            }
            if (declaration->is_constexpr && declaration->kind == C_DECLARATION_OBJECT && declaration->type.value < result.type_count)
            {
                declaration->type = c_parse_add_qualified_type(&result, declaration->type,
                                                               (CType){
                                                                   .is_const = true,
                                                               });
            }
            c_parse_validate_constexpr_declaration(&machine, arena, &result, preprocess, declaration);
            if (kind == C_DECLARATION_TYPEDEF && c_parse_variable_argument_list_name(declaration->name))
            {
                declaration->type = c_parse_variable_argument_list_type(&result);
            }
            // A declarator with no parameter list still declares a function
            // when its type is one -- `extern __typeof(f) g;`, musl's
            // weak_alias spelling, and the same shape through a typedef
            // (`typedef int F(int); extern F g;`).  The kind above is decided
            // by the declarator's function-name token, which these do not
            // have, so the declaration arrives here filed as an object.  File
            // it as a function instead, taking the parameter range from the
            // type: the call-target index, the signature, and the entity kind
            // all read the declaration, and only the type knows the
            // parameters.  The same handoff is what the parenthesized
            // declarator path and the block-scope function declarator do.
            if (kind == C_DECLARATION_OBJECT && declaration->type.value < result.type_count && result.types[declaration->type.value].kind == C_TYPE_FUNCTION)
            {
                CType* declared_function = &result.types[declaration->type.value];
                kind = C_DECLARATION_FUNCTION;
                declaration->kind = kind;
                declaration->is_variadic = declared_function->is_variadic;
                // The parameter records stay on the type rather than moving
                // onto the declaration. `__typeof(f)` resolves to f's own
                // function type, so the records this declaration would claim
                // are the ones f's declarator wrote, and the parameter-scope
                // pass below rebinds every record a declaration lists to a
                // fresh entity in a fresh scope -- claiming them takes f's
                // parameter names away from f's body, which is what musl's
                // `weak_alias(__wcsxfrm_l, wcsxfrm_l)` does to the definition
                // above it. c_ir_function_signature reads the range off the
                // type when the declaration carries none.
            }
        }
        if (!static_assertion && !declaration->name.length && c_preprocess_dialect_is_c23(preprocess.dialect))
        {
            for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
            {
                String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]);
                if (!string_equal(spelling, S8("true")) && !string_equal(spelling, S8("false")) && !string_equal(spelling, S8("nullptr")) &&
                    !string_equal(spelling, S8("constexpr")) && !string_equal(spelling, S8("typeof_unqual")))
                {
                    continue;
                }
                c_parse_diagnostic(&result, c_preprocess_token_location(&preprocess, preprocess.tokens[token_index]), C_DIAGNOSTIC_EXPECTED_DECLARATION,
                                   string_format(arena, S8("C23 keyword '{S8}' cannot be used as an identifier"), spelling));
                break;
            }
        }
        if (!declaration->name.length || declaration->type.value == C_ID_UNDERLYING_INVALID || kind == C_DECLARATION_TYPE || kind == C_DECLARATION_ASSEMBLY)
        {
            continue;
        }
        CEntity* existing = 0;
        u32 existing_index = C_ID_UNDERLYING_INVALID;
        CEntity* conflicting = 0;
        bool overloadable = false;
        for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
        {
            overloadable |= preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER &&
                            c_token_is_well_known(preprocess.spelling_base, preprocess.tokens[token_index], C_SYMBOL_WELL_KNOWN_OVERLOADABLE);
        }
        CEntityKind entity_kind = kind == C_DECLARATION_FUNCTION ? C_ENTITY_FUNCTION : kind == C_DECLARATION_TYPEDEF ? C_ENTITY_TYPEDEF : C_ENTITY_OBJECT;
        bool declares_function_type = declaration->type.value < result.type_count && result.types[declaration->type.value].kind == C_TYPE_FUNCTION;
        // The name chain lists same-named entities newest first; the
        // redeclaration logic below needs ascending entity order, so gather
        // the file-scope candidates and walk them in reverse.
        u32 candidate_ids[64];
        u32 candidate_count = 0;
        bool candidates_overflowed = false;
        u32 name_bucket = (u32)c_parse_name_hash(c_parse_name_symbol(&result, declaration->name), declaration->name) & (result.entity_lookup_bucket_count - 1);
        for (CEntityId chain = result.name_lookup_buckets[name_bucket]; chain.value != C_ID_UNDERLYING_INVALID;
             chain = result.entities[chain.value].next_by_name)
        {
            CEntity* candidate = &result.entities[chain.value];
            if (candidate->scope.value != 0 || !string_equal(candidate->name, declaration->name))
            {
                continue;
            }
            if (candidate_count == 64)
            {
                candidates_overflowed = true;
                break;
            }
            candidate_ids[candidate_count++] = chain.value;
        }
        if (candidates_overflowed)
        {
            for (u32 entity_index = 0; entity_index < result.entity_count; entity_index += 1)
            {
                CEntity* candidate = &result.entities[entity_index];
                if (candidate->scope.value != 0 || !string_equal(candidate->name, declaration->name))
                {
                    continue;
                }
                if (c_parse_entity_kind_redeclares(candidate->kind, entity_kind, declares_function_type) &&
                    c_parse_types_compatible(arena, &result, preprocess, candidate->type, declaration->type))
                {
                    existing = candidate;
                    existing_index = entity_index;
                    break;
                }
                if (!conflicting)
                {
                    conflicting = candidate;
                }
            }
        }
        else
        {
            for (u32 position = candidate_count; position-- > 0;)
            {
                u32 entity_index = candidate_ids[position];
                CEntity* candidate = &result.entities[entity_index];
                if (c_parse_entity_kind_redeclares(candidate->kind, entity_kind, declares_function_type) &&
                    c_parse_types_compatible(arena, &result, preprocess, candidate->type, declaration->type))
                {
                    existing = candidate;
                    existing_index = entity_index;
                    break;
                }
                if (!conflicting)
                {
                    conflicting = candidate;
                }
            }
        }
        if (existing)
        {
            declaration->entity = (CEntityId){
                .value = existing_index,
            };
            if (existing->is_definition && declaration->is_definition)
            {
                c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_REDEFINITION, S8("redefinition"));
            }
            else
            {
                existing->is_definition |= declaration->is_definition;
            }
            // The composite of `char pad[]` and `char pad[5]` is the complete
            // array (C11 6.2.7p3): a redeclaration that completes an entity
            // first declared with an unbounded array adopts its type — later
            // declarations, sizeof, and the symbol's layout all read the
            // entity's type, not the declaration's. A defining redeclaration
            // whose own bound is inferred from its initializer counts too:
            // lowering infers the bound on the definition's type, so the
            // entity must share that type object to see the inference.
            if (entity_kind == C_ENTITY_OBJECT && existing->type.value < result.type_count && declaration->type.value < result.type_count)
            {
                CType* existing_type = &result.types[existing->type.value];
                CType* declared_type = &result.types[declaration->type.value];
                if (existing_type->kind == C_TYPE_ARRAY && declared_type->kind == C_TYPE_ARRAY &&
                    existing_type->array_bound < result.array_bound_count && declared_type->array_bound < result.array_bound_count)
                {
                    CArrayBound existing_bound = result.array_bounds[existing_type->array_bound];
                    CArrayBound declared_bound = result.array_bounds[declared_type->array_bound];
                    if (!existing_bound.token_count && !existing_bound.is_star && !existing_bound.has_inferred_count &&
                        (declared_bound.token_count || declaration->is_definition))
                    {
                        existing->type = declaration->type;
                    }
                }
            }
            continue;
        }
        if (conflicting && !(kind == C_DECLARATION_FUNCTION && conflicting->kind == C_ENTITY_FUNCTION && overloadable))
        {
            declaration->entity = (CEntityId){
                .value = (u32)(conflicting - result.entities),
            };
            c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_CONFLICTING_DECLARATION,
                               string_format(arena, S8("conflicting declaration of '{S8}' (previous type {u32}, new type {u32})"), declaration->name,
                                             conflicting->type.value, declaration->type.value));
            continue;
        }
        CEntityId entity = {
            .value = result.entity_count,
        };
        // A function filed from a type name has no function-name token; its
        // name is the object declarator's, which is the token the entity has
        // to point at.
        u32 declaration_name_token = kind == C_DECLARATION_FUNCTION && syntax_declaration->function_name_token < token_count
                                         ? syntax_declaration->function_name_token
                                         : syntax_declaration->name_token;
        bool is_thread_local = false;
        if (kind == C_DECLARATION_OBJECT)
        {
            for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
            {
                CToken token = preprocess.tokens[token_index];
                is_thread_local |= token.kind == C_TOKEN_IDENTIFIER &&
                                   (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("_Thread_local")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__thread")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("thread_local")));
            }
        }
        declaration->entity = entity;
        BUSTER_CHECK(result.entity_count < result.entity_capacity);
        result.entities[result.entity_count++] = (CEntity){
            .name = declaration->name,
            .location = declaration->location,
            .type = declaration->type,
            .scope =
                {
                    .value = 0,
                },
            .next_in_scope = C_ENTITY_ID_INVALID,
            .declaration_index = result.declaration_count - 1,
            .declaration_token_plus_one = declaration_name_token < token_count ? declaration_name_token + 1 : 0,
            .alignment_start = declaration->alignment_start,
            .alignment_count = declaration->alignment_count,
            .kind = kind == C_DECLARATION_FUNCTION  ? C_ENTITY_FUNCTION
                    : kind == C_DECLARATION_TYPEDEF ? C_ENTITY_TYPEDEF
                                                    : C_ENTITY_OBJECT,
            .is_definition = declaration->is_definition,
            .is_thread_local = is_thread_local,
            .is_constexpr = declaration->is_constexpr,
        };
        c_parse_scope_add_entity(&result,
                                 (CScopeId){
                                     .value = 0,
                                 },
                                 entity);
    }
    if (result.enum_member_count)
    {
        CTypeId enum_integer_type = c_parse_add_type(&result, (CType){
                                                                  .element_type = C_TYPE_ID_INVALID,
                                                                  .return_type = C_TYPE_ID_INVALID,
                                                                  .array_bound = C_ARRAY_BOUND_INVALID,
                                                                  .kind = C_TYPE_INT,
                                                                  .is_complete = true,
                                                              });
        for (u32 member_index = 0; member_index < result.enum_member_count; member_index += 1)
        {
            CEnumMember* member = &result.enum_members[member_index];
            if (c_parse_lookup_entity(&result,
                                      (CScopeId){
                                          .value = 0,
                                      },
                                      member->name)
                    .value != C_ID_UNDERLYING_INVALID)
            {
                c_parse_diagnostic(&result, member->location, C_DIAGNOSTIC_REDEFINITION, S8("redefinition of enumerator"));
                continue;
            }
            CEntityId entity = {
                .value = result.entity_count,
            };
            BUSTER_CHECK(result.entity_count < result.entity_capacity);
            result.entities[result.entity_count++] = (CEntity){
                .name = member->name,
                .location = member->location,
                .type = enum_integer_type,
                .scope =
                    {
                        .value = 0,
                    },
                .next_in_scope = C_ENTITY_ID_INVALID,
                .declaration_index = C_ID_UNDERLYING_INVALID,
                .kind = C_ENTITY_ENUMERATOR,
                .is_definition = true,
                .constant_is_negative = member->is_negative,
                .constant_value = member->value,
            };
            c_parse_scope_add_entity(&result,
                                     (CScopeId){
                                         .value = 0,
                                     },
                                     entity);
        }
    }
    for (u32 declaration_index = 0; declaration_index < result.declaration_count; declaration_index += 1)
    {
        CDeclaration* declaration = &result.declarations[declaration_index];
        if (!declaration->is_constexpr || declaration->kind != C_DECLARATION_OBJECT || declaration->entity.value >= result.entity_count)
        {
            continue;
        }
        u32 end = declaration->token_start + declaration->token_count;
        u32 initializer_start = end;
        u32 depth = 0;
        for (u32 token_index = declaration->token_start; token_index < end; token_index += 1)
        {
            CToken token = preprocess.tokens[token_index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                if (depth)
                {
                    depth -= 1;
                }
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
            {
                initializer_start = token_index + 1;
                break;
            }
        }
        u32 initializer_end = end;
        if (initializer_end > initializer_start && c_token_is_punctuator(&preprocess.tokens[initializer_end - 1], C_PUNCTUATOR_SEMICOLON))
        {
            initializer_end -= 1;
        }
        if (initializer_start < initializer_end)
        {
            c_parse_validate_constexpr_initializer(&machine, arena, &result, preprocess,
                                                   (CScopeId){
                                                       .value = 0,
                                                   },
                                                   declaration->entity, initializer_start, initializer_end);
        }
    }
    c_parse_infer_file_array_bounds(&machine, arena, preprocess, &result);
    for (CParserDeclaration* syntax_declaration = syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        if (syntax_declaration->kind != C_PARSER_DECLARATION_STATIC_ASSERT)
        {
            continue;
        }
        c_parse_static_assert_check(&machine, arena, preprocess, &result,
                                    (CDeclaration){
                                        .token_start = syntax_declaration->token_start,
                                        .token_count = syntax_declaration->token_count,
                                    },
                                    (CScopeId){
                                        .value = 0,
                                    });
    }
    for (u32 declaration_index = 0; declaration_index < result.declaration_count; declaration_index += 1)
    {
        CDeclaration* declaration = &result.declarations[declaration_index];
        if (declaration->kind != C_DECLARATION_FUNCTION)
        {
            continue;
        }
        CScopeId scope = {
            .value = result.scope_count,
        };
        declaration->scope = scope;
        BUSTER_CHECK(result.scope_count < result.scope_capacity);
        CScope* function_scope = &result.scopes[result.scope_count++];
        *function_scope = (CScope){
            .parent =
                {
                    .value = 0,
                },
            .first_entity = C_ENTITY_ID_INVALID,
            .last_entity = C_ENTITY_ID_INVALID,
            .token_start = declaration->body_start,
            .token_end = declaration->body_start + declaration->body_token_count,
        };
        for (u32 parameter_index = 0; parameter_index < declaration->parameter_count; parameter_index += 1)
        {
            CParameter* parameter = &result.parameters[declaration->parameter_start + parameter_index];
            // A parameter's array bounds are expressions in the declarator,
            // not part of the function body.  Bind them while the parameter
            // scope is being built, after the preceding parameters have been
            // added but before this parameter itself is visible: the scope of
            // a parameter name starts after its declarator, so `int a[a]`
            // must not accidentally resolve the inner `a` to itself.  Global
            // objects, functions, and enumerators are already in file scope;
            // c_parse_bind_auto_initializer_identifiers has the ordinary
            // expression filtering (member/tag names and offsetof) needed by
            // this token range as well.  Prototype-only declarations never
            // lower a bound, so leave their references out of the emitted
            // function reachability graph.
            for (CTypeId type_id = declaration->is_definition ? parameter->type : C_TYPE_ID_INVALID;
                 type_id.value < result.type_count && result.types[type_id.value].kind == C_TYPE_ARRAY;
                 type_id = result.types[type_id.value].element_type)
            {
                CType array_type = result.types[type_id.value];
                if (array_type.array_bound >= result.array_bound_count)
                {
                    continue;
                }
                CArrayBound bound = result.array_bounds[array_type.array_bound];
                u64 bound_end = (u64)bound.token_start + bound.token_count;
                if (bound_end > preprocess.token_count)
                {
                    bound_end = preprocess.token_count;
                }
                if (bound.token_start < bound_end)
                {
                    c_parse_bind_auto_initializer_identifiers(arena, &result, preprocess, scope, bound.token_start, (u32)bound_end);
                }
            }
            if (!parameter->name.length)
            {
                continue;
            }
            CEntityId entity = {
                .value = result.entity_count,
            };
            parameter->entity = entity;
            BUSTER_CHECK(result.entity_count < result.entity_capacity);
            result.entities[result.entity_count++] = (CEntity){
                .name = parameter->name,
                .location = parameter->location,
                .type = parameter->type,
                .scope = scope,
                .next_in_scope = C_ENTITY_ID_INVALID,
                .declaration_index = declaration_index,
                .kind = C_ENTITY_PARAMETER,
                .is_definition = true,
            };
            c_parse_scope_add_entity(&result, scope, entity);
        }
        u32 unsupported_token_index = UINT32_MAX;
        String8 unsupported_construct = c_ir_unsupported_gnu_construct(
            preprocess, declaration->body_start, declaration->body_start + declaration->body_token_count, &unsupported_token_index);
        if (unsupported_construct.length)
        {
            c_parse_diagnostic(&result, unsupported_token_index < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[unsupported_token_index]) : declaration->location,
                               C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               string_format(arena, S8("in function '{S8}': {S8}"), declaration->name, unsupported_construct));
            continue;
        }
        c_parse_bind_function_body(&machine, arena, &result, preprocess, declaration_index);
    }
    c_parse_validate_unattached_cleanup_attributes(&result, preprocess);
    scratch_end(machine_temporary);
    BUSTER_CHECK(arena_destroy(machine_buffer_arena, 1));
    return result;
}
CParseResult c_parse(Arena* arena, CPreprocessResult preprocess)
{
    CParserResult syntax = c_parse_ast(arena, preprocess);
    return c_analyze_semantics(arena, preprocess, syntax);
}

#if !BUSTER_UNITY_BUILD
CIRLowerResult c_analyze(Arena* arena, String8 source_path, CPreprocessResult preprocess, CParserResult syntax, Target target)
{
    CIRLowerResult result = {0};
    CAnalysisResult analysis = c_analyze_semantics(arena, preprocess, syntax);
    if (analysis.diagnostic_count)
    {
        result.diagnostics = analysis.diagnostics;
        result.diagnostic_count = analysis.diagnostic_count;
        return result;
    }
    return c_lower_to_ir(arena, source_path, preprocess, analysis, target);
}
#endif
