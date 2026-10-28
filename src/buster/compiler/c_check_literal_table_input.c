// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #658's bounded file-scope integer tables. Prove the complete literal/comma grammar and every warning-free
// conversion before changing reader state. One retained bytes part replaces the elements' graph and plans.
// Unsupported syntax, formats, widths, excess elements and conversions keep the ordinary source path exactly.
// The SIMD grammar reads bounded 64-token blocks; small literals decode 16 spellings and stores narrow 8 values.
// Source records and payloads belong to the input arena. Failed candidates roll back its suffix and scratch.

#define BUSTER_CHECK_LITERAL_TABLE_MINIMUM 128u
#define BUSTER_CHECK_LITERAL_TABLE_BATCH 16u

// Empty lists and a final unary minus are ineligible; one trailing comma is allowed. Every predecessor is carried
// across blocks, including a minus at lane 63. Only the outer braces have been removed from [begin,end).
static uint32_t check_literal_table_shape(const uint8_t* kinds, uint32_t begin, uint32_t end, bool scalar)
{
    (void)scalar;
    uint32_t count = 0;
    uint32_t previous = PARSE_KIND_COMMA;
    bool valid = begin < end;
    for (uint32_t first = begin; valid && first < end;)
    {
        const uint32_t left = end - first;
        const uint32_t take = BUSTER_SCALAR_PATH(scalar) ? 1 : left < 64 ? left : 64;
        if (BUSTER_SCALAR_PATH(scalar))
        {
            const uint32_t kind = kinds[first];
            valid = kind == PARSE_KIND_NUMBER ? previous == PARSE_KIND_COMMA || previous == PARSE_KIND_MINUS :
                    kind == PARSE_KIND_MINUS ? previous == PARSE_KIND_COMMA : kind == PARSE_KIND_COMMA && previous == PARSE_KIND_NUMBER;
            count += kind == PARSE_KIND_NUMBER;
            previous = kind;
        }
        else
        {
            const uint64_t lanes = take == 64 ? UINT64_MAX : (UINT64_C(1) << take) - 1;
            const __m512i block = _mm512_maskz_loadu_epi8((__mmask64)lanes, kinds + first);
            const uint64_t numbers = lanes & _mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8(PARSE_KIND_NUMBER));
            const uint64_t commas = lanes & _mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8(PARSE_KIND_COMMA));
            const uint64_t minus = lanes & _mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8(PARSE_KIND_MINUS));
            const uint64_t number_before = numbers << 1 | (previous == PARSE_KIND_NUMBER);
            const uint64_t comma_before = commas << 1 | (previous == PARSE_KIND_COMMA);
            const uint64_t minus_before = minus << 1 | (previous == PARSE_KIND_MINUS);
            valid = (numbers | commas | minus) == lanes && (numbers & ~(comma_before | minus_before)) == 0 &&
                    (minus & ~comma_before) == 0 && (commas & ~number_before) == 0;
            count += (uint32_t)__builtin_popcountll(numbers);
            previous = kinds[first + take - 1];
        }
        first += take;
    }
    return valid && previous != PARSE_KIND_MINUS ? count : 0;
}

// Common bin2c spellings: one to three decimal digits (without an octal prefix), or exactly two hexadecimal digits.
// Each word contains only bytes copied within its spelling, with zero padding. Other integer forms use the existing
// literal reader; floating, malformed and imaginary forms make the whole table fall back without dropping diagnostics.
static __mmask16 check_literal_table_small(const uint32_t* words, const uint32_t* lengths, uint32_t* decoded, __mmask16 lanes)
{
    const __m512i text = _mm512_loadu_si512(words);
    const __m512i length = _mm512_loadu_si512(lengths);
    const __m512i byte = _mm512_set1_epi32(255);
    const __m512i a = _mm512_and_si512(text, byte);
    const __m512i b = _mm512_and_si512(_mm512_srli_epi32(text, 8), byte);
    const __m512i c = _mm512_and_si512(_mm512_srli_epi32(text, 16), byte);
    const __m512i d = _mm512_srli_epi32(text, 24);
    const __m512i zero = _mm512_set1_epi32('0');
    const __m512i nine = _mm512_set1_epi32(9);
    const __m512i da = _mm512_sub_epi32(a, zero);
    const __m512i db = _mm512_sub_epi32(b, zero);
    const __m512i dc = _mm512_sub_epi32(c, zero);
    const __mmask16 one = _mm512_cmpeq_epi32_mask(length, _mm512_set1_epi32(1));
    const __mmask16 two = _mm512_cmpeq_epi32_mask(length, _mm512_set1_epi32(2));
    const __mmask16 three = _mm512_cmpeq_epi32_mask(length, _mm512_set1_epi32(3));
    const __mmask16 decimal = (one | two | three) & _mm512_cmple_epu32_mask(da, nine) &
                             (one | (_mm512_cmpneq_epi32_mask(da, _mm512_setzero_si512()) & _mm512_cmple_epu32_mask(db, nine))) &
                             (~three | _mm512_cmple_epu32_mask(dc, nine));
    __m512i value = _mm512_mask_add_epi32(da, two | three, _mm512_mullo_epi32(da, _mm512_set1_epi32(10)), db);
    value = _mm512_mask_add_epi32(value, three, _mm512_mullo_epi32(value, _mm512_set1_epi32(10)), dc);
    const __m512i hc = _mm512_or_si512(c, _mm512_set1_epi32(32));
    const __m512i hd = _mm512_or_si512(d, _mm512_set1_epi32(32));
    const __m512i cc = _mm512_sub_epi32(hc, _mm512_set1_epi32('a'));
    const __m512i cd = _mm512_sub_epi32(hd, _mm512_set1_epi32('a'));
    const __m512i dd = _mm512_sub_epi32(d, zero);
    const __mmask16 c_letter = _mm512_cmple_epu32_mask(cc, _mm512_set1_epi32(5));
    const __mmask16 d_letter = _mm512_cmple_epu32_mask(cd, _mm512_set1_epi32(5));
    const __mmask16 hex = _mm512_cmpeq_epi32_mask(length, _mm512_set1_epi32(4)) &
                         _mm512_cmpeq_epi32_mask(_mm512_and_si512(_mm512_or_si512(text, _mm512_set1_epi32(0x2000)), _mm512_set1_epi32(0xffff)), _mm512_set1_epi32(0x7830)) &
                         (c_letter | _mm512_cmple_epu32_mask(dc, nine)) & (d_letter | _mm512_cmple_epu32_mask(dd, nine));
    const __m512i high = _mm512_mask_add_epi32(dc, c_letter, cc, _mm512_set1_epi32(10));
    const __m512i low = _mm512_mask_add_epi32(dd, d_letter, cd, _mm512_set1_epi32(10));
    value = _mm512_mask_or_epi32(value, hex, _mm512_slli_epi32(high, 4), low);
    _mm512_storeu_si512(decoded, value);
    return lanes & (decimal | hex);
}

static bool check_literal_table_value(CheckInputReader* reader, const uint8_t* spelling, uint32_t length, bool negative, uint32_t width, bool is_signed, uint64_t* bits)
{
    CheckLiteral literal = { 0 };
    check_literal_integer(spelling, length, &literal);
    bool valid = literal.valid;
    CheckValue value = { .type = literal.type, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_ICE,
                         .constant_kind = CHECK_CONSTANT_INTEGER, .integer = literal.integer };
    if (valid && negative)
    {
        const CheckAction action = { .op = CHECK_OP_NEGATIVE, .operands = { value } };
        CheckActionResult result;
        check_action_scalar(reader->types, &action, &result);
        valid = result.diagnostic == CHECK_DIAG_NONE && result.warnings == 0 && result.value.fault == CHECK_FAULT_NONE;
        value = result.value;
    }
    uint32_t source_width = 0;
    bool source_signed = false;
    valid = valid && check_action_integer_type(reader->types, value.type, &source_width, &source_signed) && source_width <= 64;
    if (valid)
    {
        const unsigned __int128 number = value.integer & check_action_mask(source_width);
        const __int128 signed_number = source_signed && (number >> (source_width - 1)) != 0 ? (__int128)number - ((__int128)1 << source_width) : (__int128)number;
        const __int128 low = is_signed ? -((__int128)1 << (width - 1)) : 0;
        const __int128 high = is_signed ? ((__int128)1 << (width - 1)) - 1 : (__int128)check_action_mask(width);
        valid = signed_number >= low && signed_number <= high;
        *bits = (uint64_t)number;
        // Sign-extend the source before widening its bytes (e.g. int -1 to long).
        *bits = signed_number < 0 ? (uint64_t)signed_number : *bits;
    }
    return valid;
}

static bool check_literal_table_batch(CheckInputReader* reader, const uint32_t* tokens, uint32_t count, uint32_t width, bool is_signed, uint8_t* destination)
{
    uint32_t words[BUSTER_CHECK_LITERAL_TABLE_BATCH] = { 0 };
    uint32_t lengths[BUSTER_CHECK_LITERAL_TABLE_BATCH] = { 0 };
    const uint8_t* spellings[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint32_t decoded[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint64_t values[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    const uint64_t mark = reader->scratch->length;
    bool valid = true;
    for (uint32_t lane = 0; valid && lane < count; lane += 1)
    {
        valid = check_input_spelling(reader, tokens[lane], &spellings[lane], &lengths[lane]);
        if (valid && lengths[lane] <= sizeof(uint32_t))
        {
            memcpy(words + lane, spellings[lane], lengths[lane]);
        }
    }
    const __mmask16 lanes = (__mmask16)((1u << count) - 1);
    const __mmask16 small = valid && BUSTER_VECTOR_PATH(!reader->parser->options.scalar) ? check_literal_table_small(words, lengths, decoded, lanes) : 0;
    for (uint32_t lane = 0; valid && lane < count; lane += 1)
    {
        const bool negative = reader->parser->tokens.kinds[tokens[lane] - 1] == PARSE_KIND_MINUS;
        if ((small & (1u << lane)) != 0 && !negative)
        {
            const uint64_t maximum = is_signed ? (UINT64_C(1) << (width - 1)) - 1 : width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            values[lane] = decoded[lane];
            valid = values[lane] <= maximum;
        }
        else
        {
            valid = check_literal_table_value(reader, spellings[lane], lengths[lane], negative, width, is_signed, values + lane);
        }
    }
    for (uint32_t lane = 0; valid && BUSTER_SCALAR_PATH(reader->parser->options.scalar) && lane < count; lane += 1)
    {
        memcpy(destination + (uint64_t)lane * (width / 8), values + lane, width / 8);
    }
    for (uint32_t first = 0; valid && BUSTER_VECTOR_PATH(!reader->parser->options.scalar) && first < count; first += 8)
    {
        const uint32_t left = count - first;
        const __mmask8 live = (__mmask8)((1u << (left < 8 ? left : 8)) - 1);
        const __m512i numbers = _mm512_maskz_loadu_epi64(live, values + first);
        uint8_t* bytes = destination + (uint64_t)first * (width / 8);
        if (width == 8)
        {
            _mm512_mask_cvtepi64_storeu_epi8(bytes, live, numbers);
        }
        else if (width == 16)
        {
            _mm512_mask_cvtepi64_storeu_epi16(bytes, live, numbers);
        }
        else if (width == 32)
        {
            _mm512_mask_cvtepi64_storeu_epi32(bytes, live, numbers);
        }
        else
        {
            _mm512_mask_storeu_epi64(bytes, live, numbers);
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_input_literal_table_prepare(CheckInputReader* reader, uint32_t assignment)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t declaration = reader->initializer_declarations[assignment];
    const uint32_t begin = input->initializer_begins[declaration];
    const uint32_t end = input->initializer_ends[declaration];
    TypeId type = reader->declared_types[declaration];
    const uint32_t kind = type_kind(reader->types, type);
    bool eligible = !reader->literal_tables_disabled && reader->function_body == BUSTER_CHECK_NONE && !input->compound_objects[declaration] &&
                    (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) && begin < end && end <= parser->tokens.count &&
                    end - begin >= 2 * BUSTER_CHECK_LITERAL_TABLE_MINIMUM && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE &&
                    parser->tree.parents[begin] == end - 1 && parser->tokens.kinds[end - 1] == PARSE_KIND_R_BRACE;
    uint32_t width = 0;
    bool is_signed = false;
    if (eligible)
    {
        const TypeId element = type_element(reader->types, type);
        eligible = (type_qualifiers(element) & BUSTER_TYPE_VOLATILE) == 0 && type_kind(reader->types, element) == BUSTER_TYPE_KIND_INTEGER &&
                   type_unqualified(element) != BUSTER_TYPE_BOOL && check_action_integer_type(reader->types, element, &width, &is_signed) &&
                   (width == 8 || width == 16 || width == 32 || width == 64);
    }
    const uint32_t count = eligible ? check_literal_table_shape(parser->tokens.kinds, begin + 1, end - 1, parser->options.scalar) : 0;
    eligible = eligible && count >= BUSTER_CHECK_LITERAL_TABLE_MINIMUM &&
               (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || count <= reader->types->types.extent[type_index(type)]);
    const uint64_t mark = reader->arena->length;
    const uint64_t bytes = (uint64_t)count * (width / 8);
    uint8_t* payload = eligible ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, bytes) : NULL;
    input->resource |= eligible && payload == NULL;
    bool accepted = eligible && payload != NULL;
    uint32_t tokens[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint32_t batch = 0;
    uint32_t written = 0;
    for (uint32_t token = begin + 1; accepted && token + 1 < end; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_NUMBER)
        {
            tokens[batch] = token;
            batch += 1;
            if (batch == BUSTER_CHECK_LITERAL_TABLE_BATCH || written + batch == count)
            {
                accepted = check_literal_table_batch(reader, tokens, batch, width, is_signed, payload + (uint64_t)written * (width / 8));
                written += batch;
                batch = 0;
            }
        }
    }
    CheckInputLiteralTable* table = accepted ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputLiteralTable, 1) : NULL;
    input->resource |= accepted && table == NULL;
    accepted = accepted && table != NULL;
    if (accepted && kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
    {
        type = check_input_type_result(reader, type_array(reader->types, type_element(reader->types, type), count));
        accepted = type != 0;
    }
    if (accepted)
    {
        *table = (CheckInputLiteralTable){ .declaration = declaration };
        table->part = (CheckInitPart){ .kind = CHECK_INIT_BYTES, .type = type, .node = BUSTER_CHECK_NONE, .token = begin,
                                       .bytes = payload, .byte_count = bytes };
        table->plan = (CheckInitializer){ .type = type, .object_size = type_size(reader->types, type), .parts = &table->part,
                                        .count = 1, .status = CHECK_INIT_OK, .failed_token = BUSTER_CHECK_NONE, .warning_token = BUSTER_CHECK_NONE };
        if (input->literal_tables_last != NULL)
        {
            input->literal_tables_last->next = table;
        }
        else
        {
            input->literal_tables = table;
        }
        input->literal_tables_last = table;
        input->literal_table_count += 1;
        reader->declared_types[declaration] = type;
        reader->query_types[assignment] = type;
        if (reader->linkage[declaration] != 0)
        {
            reader->linked_types[parser->tokens.names[declaration]] = type;
        }
        memset(reader->ignored + begin, 1, end - begin);
        reader->ignored[assignment] = true;
    }
    else
    {
        arena_pop_to(reader->arena, mark);
    }
    return accepted;
}
