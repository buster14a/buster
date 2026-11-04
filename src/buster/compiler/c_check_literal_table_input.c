// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #658's bounded file-scope integer and float/double tables. Prove the complete literal/comma grammar and every warning-free
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
    uint32_t count = 0;
    uint32_t previous = PARSE_KIND_COMMA;
    bool valid = begin < end;
    for (uint32_t first = begin; valid && first < end;)
    {
        const uint32_t left = end - first;
        const uint32_t take = scalar ? 1 : left < 64 ? left : 64;
        if (scalar)
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
    // A bit-precise constant, whose type the table makes (check_input_literal), takes the ordinary path.
    bool valid = literal.valid && literal.bit_width == 0;
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
    const __mmask16 small = valid && !reader->parser->options.scalar ? check_literal_table_small(words, lengths, decoded, lanes) : 0;
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
    for (uint32_t lane = 0; valid && reader->parser->options.scalar && lane < count; lane += 1)
    {
        memcpy(destination + (uint64_t)lane * (width / 8), values + lane, width / 8);
    }
    for (uint32_t first = 0; valid && !reader->parser->options.scalar && first < count; first += 8)
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

// Keep the ordinary literal parser's rounding and range classification. Same-format real literals need no
// expression action; other source types use the ordinary unary/initializer actions, whose result is the value the
// ordinary path stores. A floating source rounds silently there (clang warns only under -Wimplicit-float-conversion),
// so `float t[] = { 0.1, ... }` stays compact. An integer must convert exactly, since clang warns by default when
// one changes value: test it in the host's exact 64-bit-significand format, not after rounding it to double.
// Range warnings, imaginary/bit-precise constants and malformed spellings retain dense checking.
static bool check_literal_table_floating_value(CheckInputReader* reader, const uint8_t* spelling, uint32_t length, bool negative, TypeId element, uint64_t* bits)
{
    CheckLiteral literal = { 0 };
    check_literal_number(spelling, length, reader->scratch, &literal);
    reader->output->resource |= literal.resource;
    bool valid = literal.valid && !literal.resource && !literal.imaginary && literal.bit_width == 0 && literal.range == CHECK_LITERAL_RANGE_NONE;
    long double number = 0;
    if (valid && literal.real && literal.type == type_unqualified(element))
    {
        number = negative ? -literal.floating : literal.floating;
    }
    else if (valid)
    {
        CheckValue value = check_literal_value(literal);
        if (negative)
        {
            const CheckAction action = {
                .op = CHECK_OP_NEGATIVE,
                .operands = { value },
            };
            CheckActionResult result = { 0 };
            check_action_scalar(reader->types, &action, &result);
            reader->output->resource |= result.diagnostic == CHECK_DIAG_RESOURCE;
            valid = result.diagnostic == CHECK_DIAG_NONE && result.warnings == 0 && result.value.fault == CHECK_FAULT_NONE;
            value = result.value;
        }
        long double original = 0;
        const bool integer = value.constant_kind == CHECK_CONSTANT_INTEGER;
        if (valid && integer)
        {
            uint32_t width = 0;
            bool is_signed = false;
            valid = check_action_integer_type(reader->types, value.type, &width, &is_signed) && width <= 64;
            if (valid)
            {
                const unsigned __int128 magnitude = value.integer & check_action_mask(width);
                const __int128 signed_number = is_signed && (magnitude >> (width - 1)) != 0 ?
                                               (__int128)magnitude - ((__int128)1 << width) : (__int128)magnitude;
                original = (long double)signed_number;
            }
        }
        else if (valid)
        {
            valid = value.constant_kind == CHECK_CONSTANT_FLOATING;
            original = value.floating;
        }
        if (valid)
        {
            const CheckAction action = {
                .op = CHECK_OP_INITIALIZE,
                .operands = { value },
                .type = element,
            };
            CheckActionResult result = { 0 };
            check_action_scalar(reader->types, &action, &result);
            reader->output->resource |= result.diagnostic == CHECK_DIAG_RESOURCE;
            valid = result.diagnostic == CHECK_DIAG_NONE && result.warnings == 0 && result.value.fault == CHECK_FAULT_NONE &&
                    result.value.constant_kind == CHECK_CONSTANT_FLOATING && (!integer || result.value.floating == original);
            number = result.value.floating;
        }
    }
    if (valid)
    {
        *bits = 0;
        if (type_unqualified(element) == BUSTER_TYPE_FLOAT)
        {
            const float converted = (float)number;
            memcpy(bits, &converted, sizeof(converted));
        }
        else
        {
            const double converted = (double)number;
            memcpy(bits, &converted, sizeof(converted));
        }
    }
    return valid;
}

// Whether a number's spelling is an integer literal's: no '.', and no exponent ('e' in decimal, 'p' in hexadecimal).
static bool check_literal_table_integer_spelling(const uint8_t* spelling, uint32_t length)
{
    const bool hexadecimal = length > 2 && spelling[0] == '0' && (spelling[1] | 0x20) == 'x';
    const uint8_t exponent = hexadecimal ? 'p' : 'e';
    bool integer = true;
    for (uint32_t i = 0; integer && i < length; i += 1)
    {
        integer = spelling[i] != '.' && (spelling[i] | 0x20) != exponent;
    }
    return integer;
}

// Whether an integer literal's spelling may hold a value that `width`'s format (32: float, 64: double) can't: more
// digits than every value of that many is exact in, 2^24 or 2^53 being the format's least integer past which one isn't.
// Decimal: 7 or 15 digits; hexadecimal: 6 or 13; octal: 8 or 17; binary: 24 or 53. Leading zeros count, and digit
// separators don't, so the count errs only toward testing: a spelling counted short is still tested by its batch
// (check_literal_table_floating_value), only later.
static bool check_literal_table_integer_may_round(const uint8_t* spelling, uint32_t length, uint32_t width)
{
    const bool hexadecimal = length > 2 && spelling[0] == '0' && (spelling[1] | 0x20) == 'x';
    const bool binary = length > 2 && spelling[0] == '0' && (spelling[1] | 0x20) == 'b';
    const bool octal = !hexadecimal && !binary && length > 1 && spelling[0] == '0';
    const uint32_t first = hexadecimal || binary ? 2 : octal ? 1 : 0;
    const uint32_t limit = hexadecimal ? (width == 32 ? 6 : 13) : binary ? (width == 32 ? 24 : 53) : octal ? (width == 32 ? 8 : 17) : (width == 32 ? 7 : 15);
    uint32_t digits = 0;
    bool more = true;
    for (uint32_t i = first; more && i < length; i += 1)
    {
        const uint8_t c = spelling[i];
        const bool digit = (c >= '0' && c <= '9') || (hexadecimal && (c | 0x20) >= 'a' && (c | 0x20) <= 'f');
        digits += digit;
        more = digit || c == '\'';
    }
    return digits > limit;
}

// What check_literal_table_classify finds in a number's bytes: a line splice (or, with trigraphs, a '?'), which leaves
// its spelling to check_input_spelling; or an integer whose digits allow a value the format can't hold
// (check_literal_table_integer_may_round).
#define BUSTER_CHECK_LITERAL_TABLE_SPLICED 1u
#define BUSTER_CHECK_LITERAL_TABLE_ROUND 2u

// The scalar twin of check_literal_table_classify, and its oracle: `splices` says whether to look for splices, and
// `trigraphs` whether a '?' counts as one, as pp_remove_splices reads them; `round` whether to look for an integer that
// may round in `width`'s format.
static uint32_t check_literal_table_classify_scalar(const uint8_t* bytes, uint32_t length, uint32_t width, bool splices, bool trigraphs, bool round)
{
    bool spliced = false;
    for (uint32_t i = 0; splices && i < length; i += 1)
    {
        spliced = spliced || bytes[i] == '\\' || (trigraphs && bytes[i] == '?');
    }
    const bool rounds = round && !spliced && check_literal_table_integer_spelling(bytes, length) && check_literal_table_integer_may_round(bytes, length, width);
    return spliced ? BUSTER_CHECK_LITERAL_TABLE_SPLICED : rounds ? BUSTER_CHECK_LITERAL_TABLE_ROUND : 0;
}

// A number's category (BUSTER_CHECK_LITERAL_TABLE_SPLICED, _ROUND or neither) from one masked load of its bytes: masks of its
// splices, its '.' and exponent (an integer has none), and its digits up to the first character past its prefix that is
// neither a digit nor a separator. The masked-off lanes past `length` are never read. A number longer than 64 bytes takes
// the scalar twin.
static uint32_t check_literal_table_classify(const uint8_t* bytes, uint32_t length, uint32_t width, bool splices, bool trigraphs, bool round)
{
    uint32_t result = 0;
    if (length > 64)
    {
        result = check_literal_table_classify_scalar(bytes, length, width, splices, trigraphs, round);
    }
    else
    {
        const uint64_t live = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
        const __m512i text = _mm512_maskz_loadu_epi8((__mmask64)live, bytes);
        const uint64_t backslash = splices ? _mm512_cmpeq_epi8_mask(text, _mm512_set1_epi8('\\')) : 0;
        const uint64_t question = splices && trigraphs ? _mm512_cmpeq_epi8_mask(text, _mm512_set1_epi8('?')) : 0;
        const uint64_t spliced = live & (backslash | question);
        result = spliced != 0 ? BUSTER_CHECK_LITERAL_TABLE_SPLICED : 0;
        if (round && spliced == 0)
        {
            const __m512i lower = _mm512_or_si512(text, _mm512_set1_epi8(0x20));
            const bool hexadecimal = length > 2 && bytes[0] == '0' && (bytes[1] | 0x20) == 'x';
            const bool binary = length > 2 && bytes[0] == '0' && (bytes[1] | 0x20) == 'b';
            const bool octal = !hexadecimal && !binary && length > 1 && bytes[0] == '0';
            const uint32_t first = hexadecimal || binary ? 2 : octal ? 1 : 0;
            const uint32_t limit = hexadecimal ? (width == 32 ? 6 : 13) : binary ? (width == 32 ? 24 : 53) : octal ? (width == 32 ? 8 : 17) : (width == 32 ? 7 : 15);
            const uint64_t floating = live & (_mm512_cmpeq_epi8_mask(text, _mm512_set1_epi8('.')) |
                                              _mm512_cmpeq_epi8_mask(lower, _mm512_set1_epi8(hexadecimal ? 'p' : 'e')));
            const uint64_t decimal = _mm512_cmple_epu8_mask(_mm512_sub_epi8(text, _mm512_set1_epi8('0')), _mm512_set1_epi8(9));
            const uint64_t letter = hexadecimal ? _mm512_cmple_epu8_mask(_mm512_sub_epi8(lower, _mm512_set1_epi8('a')), _mm512_set1_epi8(5)) : 0;
            const uint64_t digit = live & (decimal | letter);
            const uint64_t separator = live & _mm512_cmpeq_epi8_mask(text, _mm512_set1_epi8('\''));
            const uint64_t from = live & (UINT64_MAX << first);
            const uint64_t stops = from & ~(digit | separator);
            const uint64_t before = stops != 0 ? (stops & (0 - stops)) - 1 : UINT64_MAX;
            const uint32_t digits = (uint32_t)__builtin_popcountll(digit & from & before);
            result = floating == 0 && digits > limit ? BUSTER_CHECK_LITERAL_TABLE_ROUND : 0;
        }
    }
    return result;
}

// Where a floating table's elements are read from: the source range of the last one read, kept from element to element
// (a table's numbers written in one file are in one range, which pp_find_range then searches for once rather than for
// each element); and the span of the location space from the table's first number to its last, where those are in one
// range and its bytes hold no splice (check_literal_table_span), whose numbers' bytes are their spellings without a scan
// of each. `clean_end` is 0 for no such span.
typedef struct CheckLiteralTableSource CheckLiteralTableSource;
struct CheckLiteralTableSource
{
    const uint8_t* bytes;
    uint32_t base;
    uint32_t length;
    const uint8_t* clean;
    uint32_t clean_begin;
    uint32_t clean_end;
};

// The source of the numbers of tokens [begin, end) (CheckLiteralTableSource), its clean span found by one scan of its
// bytes for the splices pp_remove_splices reads ('\\', and with trigraphs '?'), or none where it has one. A final clean
// scratch spelling needs no scan (pp_without_splices).
static CheckLiteralTableSource check_literal_table_span(CheckInputReader* reader, uint32_t begin, uint32_t end)
{
    Preprocessor* pp = reader->pp;
    const PPTokens* output = &pp->output;
    const uint8_t* kinds = reader->parser->tokens.kinds;
    const uint32_t* sources = reader->parser->tokens.sources;
    CheckLiteralTableSource result = { 0 };
    uint32_t first = begin;
    uint32_t last = end;
    while (first < end && kinds[first] != PARSE_KIND_NUMBER)
    {
        first += 1;
    }
    while (last > first && kinds[last - 1] != PARSE_KIND_NUMBER)
    {
        last -= 1;
    }
    const uint32_t location = first < last ? output->locations[sources[first]] : 0;
    const uint32_t stop = first < last ? output->values[sources[last - 1]] : 0;
    const uint32_t range = first < last ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    if (range != BUSTER_PP_NONE)
    {
        const PPRange* record = &pp->ranges[range];
        result.bytes = record->bytes;
        result.base = record->base;
        result.length = record->length;
        if (stop > location && stop - record->base <= record->length)
        {
            const uint8_t* bytes = record->bytes + (location - record->base);
            const uint32_t length = stop - location;
            const Arena* clean = &pp->scratch[1];
            const bool final = (uintptr_t)bytes >= (uintptr_t)clean->base && (uintptr_t)bytes - (uintptr_t)clean->base < clean->capacity;
            const bool trigraphs = (pp->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
            const bool spliced = !final && (memchr(bytes, '\\', length) != NULL || (trigraphs && memchr(bytes, '?', length) != NULL));
            result.clean = spliced ? NULL : bytes;
            result.clean_begin = spliced ? 0 : location;
            result.clean_end = spliced ? 0 : stop;
        }
    }
    return result;
}

// Element `token`'s spelling, as check_input_spelling makes it, and with `classify`, its category: whether it is an
// integer that may round (check_literal_table_classify). Only a number of 8 bytes or more for float, 16 for double, may
// be one (an integer of fewer has at most 7 or 15 digits), and a shorter one is passed by its token's length alone when
// `spelling` is null. A number token whose bytes are in one source range without a splice is spelled by those bytes
// (pp_spelling's pp_without_splices), which takes no scratch and no search but the range's, kept in `source`; any other
// token, and a spliced one, takes check_input_spelling, whose scratch the caller pops.
static bool check_literal_table_element(CheckInputReader* reader, CheckLiteralTableSource* source, uint32_t token, uint32_t width, bool classify,
                                        const uint8_t** spelling, uint32_t* length, uint32_t* category)
{
    Preprocessor* pp = reader->pp;
    const PPTokens* output = &pp->output;
    const uint32_t index = reader->parser->tokens.sources[token];
    const uint32_t location = output->locations[index];
    const uint32_t end = output->values[index];
    const bool number = output->kinds[index] == C_TOKEN_NUMERIC_CONSTANT && end > location;
    const uint32_t shortest = width == 32 ? 8 : 16;
    // The bytes as written are at least as long as the spelling, which drops a splice's.
    const bool short_number = number && end - location < shortest;
    const bool spanned = number && source->clean_end != 0 && location >= source->clean_begin && end <= source->clean_end;
    const bool scalar = reader->parser->options.scalar;
    bool valid = true;
    *category = 0;
    if (spanned && !(classify && short_number && spelling == NULL))
    {
        // In the clean span: its bytes are its spelling.
        const uint8_t* bytes = source->clean + (location - source->clean_begin);
        if (classify && !short_number)
        {
            *category = scalar ? check_literal_table_classify_scalar(bytes, end - location, width, false, false, true) :
                                 check_literal_table_classify(bytes, end - location, width, false, false, true);
        }
        if (spelling != NULL)
        {
            *spelling = bytes;
            *length = end - location;
        }
    }
    else if (!(classify && short_number && spelling == NULL))
    {
        if (number && !(location - source->base < source->length && end - source->base <= source->length))
        {
            const uint32_t range = pp_find_range(pp, location);
            source->bytes = range != BUSTER_PP_NONE ? pp->ranges[range].bytes : NULL;
            source->base = range != BUSTER_PP_NONE ? pp->ranges[range].base : 0;
            source->length = range != BUSTER_PP_NONE ? pp->ranges[range].length : 0;
        }
        const bool ranged = number && source->bytes != NULL && location - source->base < source->length && end - source->base <= source->length;
        const uint8_t* bytes = ranged ? source->bytes + (location - source->base) : NULL;
        // A clean scratch spelling is final, splices and all (pp_without_splices).
        const Arena* clean = &pp->scratch[1];
        const uintptr_t address = (uintptr_t)bytes;
        const bool final = ranged && address >= (uintptr_t)clean->base && address - (uintptr_t)clean->base < clean->capacity;
        const bool trigraphs = (pp->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
        uint32_t kind = BUSTER_CHECK_LITERAL_TABLE_SPLICED;
        if (ranged)
        {
            kind = scalar ? check_literal_table_classify_scalar(bytes, end - location, width, !final, trigraphs, classify) :
                            check_literal_table_classify(bytes, end - location, width, !final, trigraphs, classify);
        }
        const uint8_t* spelled = bytes;
        uint32_t spelled_length = end - location;
        if (kind == BUSTER_CHECK_LITERAL_TABLE_SPLICED)
        {
            valid = check_input_spelling(reader, token, &spelled, &spelled_length);
            kind = !valid ? 0 : scalar ? check_literal_table_classify_scalar(spelled, spelled_length, width, false, false, classify) :
                                         check_literal_table_classify(spelled, spelled_length, width, false, false, classify);
        }
        *category = kind;
        if (spelling != NULL)
        {
            *spelling = spelled;
            *length = spelled_length;
        }
    }
    return valid;
}

// Converts, into `payload`, the elements of tokens [begin, end) that may hold an integer the format can't
// (BUSTER_CHECK_LITERAL_TABLE_ROUND), and marks them in `converted`, before any other conversion: so the refusals an
// integer's precision loss makes come first. One late in a table would otherwise come after every element before it was
// converted in vain, which the ordinary path then converts again: 28% slower than main on a 200,000-element table refused
// at its last element on the 9700X (#1210). Converting every integer of four characters or more first still left that
// table of exact long integers 23% slower, and gathering every element's spelling (check_input_spelling, kept in scratch
// for the batches) 6% slower. Now an element shorter than its format's shortest such integer (8 bytes for float, 16 for
// double) is passed by its token's length alone, and a longer one is read in place (check_literal_table_element), with
// nothing kept: the batches read the bytes again. The other late refusals (a range warning, an imaginary, bit-precise or
// malformed constant) are code that warns or fails, and keep their cost.
static bool check_literal_table_floating_integers(CheckInputReader* reader, uint32_t begin, uint32_t end, TypeId element, uint32_t width,
                                                  uint8_t* payload, uint64_t* converted)
{
    const uint8_t* kinds = reader->parser->tokens.kinds;
    const uint32_t unit = width / 8;
    CheckLiteralTableSource source = check_literal_table_span(reader, begin, end);
    uint32_t index = 0;
    bool valid = true;
    for (uint32_t token = begin; valid && token < end; token += 1)
    {
        if (kinds[token] == PARSE_KIND_NUMBER)
        {
            const uint64_t mark = reader->scratch->length;
            uint32_t category = 0;
            valid = check_literal_table_element(reader, &source, token, width, true, NULL, NULL, &category);
            if (valid && category == BUSTER_CHECK_LITERAL_TABLE_ROUND)
            {
                const uint8_t* spelling = NULL;
                uint32_t length = 0;
                uint64_t bits = 0;
                valid = check_literal_table_element(reader, &source, token, width, false, &spelling, &length, &category) &&
                        check_literal_table_floating_value(reader, spelling, length, kinds[token - 1] == PARSE_KIND_MINUS, element, &bits);
                memcpy(payload + (uint64_t)index * unit, &bits, unit);
                converted[index / 64] |= (uint64_t)valid << (index % 64);
            }
            arena_pop_to(reader->scratch, mark);
            index += 1;
        }
    }
    return valid;
}

// Converts a batch's `count` elements (at most BUSTER_CHECK_LITERAL_TABLE_BATCH), their tokens and spellings given, into
// `destination`, but those `converted` marks, which are there already.
static bool check_literal_table_floating_batch(CheckInputReader* reader, const uint32_t* tokens, const uint8_t* const* spellings, const uint32_t* spelled,
                                               uint32_t converted, uint32_t count, TypeId element, uint32_t width, uint8_t* destination)
{
    uint32_t words[BUSTER_CHECK_LITERAL_TABLE_BATCH] = { 0 };
    // The decoder reads 16 lengths whatever the batch's count: a copy, zero past it.
    uint32_t lengths[BUSTER_CHECK_LITERAL_TABLE_BATCH] = { 0 };
    uint32_t decoded[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint64_t values[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    memcpy(lengths, spelled, (uint64_t)count * sizeof(uint32_t));
    // The parser's columns/options stay immutable and disjoint from output.
    const bool scalar = reader->parser->options.scalar;
    const uint8_t* kinds = reader->parser->tokens.kinds;
    const uint32_t unit = width / 8;
    bool valid = true;
    for (uint32_t lane = 0; lane < count; lane += 1)
    {
        if (lengths[lane] <= sizeof(uint32_t))
        {
            memcpy(words + lane, spellings[lane], lengths[lane]);
        }
    }
    const __mmask16 lanes = (__mmask16)((1u << count) - 1);
    const __mmask16 small = !scalar ? check_literal_table_small(words, lengths, decoded, lanes) : 0;
    for (uint32_t lane = 0; valid && lane < count; lane += 1)
    {
        const bool negative = kinds[tokens[lane] - 1] == PARSE_KIND_MINUS;
        values[lane] = 0;
        if ((converted & (1u << lane)) != 0)
        {
            memcpy(values + lane, destination + (uint64_t)lane * unit, unit);
        }
        else if ((small & (1u << lane)) != 0)
        {
            // These int literals are at most 999, exactly representable in both formats. Negate as an int:
            // -0 is positive floating zero, unlike the floating literal -0.0.
            const int32_t number = negative ? -(int32_t)decoded[lane] : (int32_t)decoded[lane];
            if (width == 32)
            {
                const float converted_value = (float)number;
                memcpy(values + lane, &converted_value, sizeof(converted_value));
            }
            else
            {
                const double converted_value = (double)number;
                memcpy(values + lane, &converted_value, sizeof(converted_value));
            }
        }
        else
        {
            const uint64_t mark = reader->scratch->length;
            valid = check_literal_table_floating_value(reader, spellings[lane], lengths[lane], negative, element, values + lane);
            arena_pop_to(reader->scratch, mark);
        }
    }
    for (uint32_t lane = 0; valid && scalar && lane < count; lane += 1)
    {
        memcpy(destination + (uint64_t)lane * unit, values + lane, unit);
    }
    for (uint32_t at = 0; valid && !scalar && at < count; at += 8)
    {
        const uint32_t left = count - at;
        const __mmask8 live = (__mmask8)((1u << (left < 8 ? left : 8)) - 1);
        const __m512i representations = _mm512_maskz_loadu_epi64(live, values + at);
        uint8_t* bytes = destination + (uint64_t)at * unit;
        if (width == 32)
        {
            _mm512_mask_cvtepi64_storeu_epi32(bytes, live, representations);
        }
        else
        {
            _mm512_mask_storeu_epi64(bytes, live, representations);
        }
    }
    return valid;
}

// A floating table's `count` elements, tokens [begin, end): the integers that may round converted first
// (check_literal_table_floating_integers), then every element a batch at a time into `payload`, each batch's spellings
// read in place again (check_literal_table_element) and its scratch popped after it. The scratch holds only the
// converted bitset, and is popped once the table is decided.
static bool check_literal_table_floating(CheckInputReader* reader, uint32_t begin, uint32_t end, uint32_t count, TypeId element, uint32_t width, uint8_t* payload)
{
    const uint64_t mark = reader->scratch->length;
    const uint32_t words = (count + 63) / 64;
    uint64_t* converted = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, words);
    reader->output->resource |= converted == NULL;
    if (converted != NULL)
    {
        memset(converted, 0, (uint64_t)words * sizeof(uint64_t));
    }
    bool valid = converted != NULL && check_literal_table_floating_integers(reader, begin, end, element, width, payload, converted);
    const uint8_t* kinds = reader->parser->tokens.kinds;
    CheckLiteralTableSource source = valid ? check_literal_table_span(reader, begin, end) : (CheckLiteralTableSource){ 0 };
    uint32_t tokens[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    const uint8_t* spellings[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint32_t lengths[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint32_t batch = 0;
    uint32_t written = 0;
    const uint64_t batch_mark = reader->scratch->length;
    for (uint32_t token = begin; valid && token < end; token += 1)
    {
        if (kinds[token] == PARSE_KIND_NUMBER)
        {
            uint32_t category = 0;
            tokens[batch] = token;
            valid = check_literal_table_element(reader, &source, token, width, false, &spellings[batch], &lengths[batch], &category);
            batch += 1;
            if (valid && (batch == BUSTER_CHECK_LITERAL_TABLE_BATCH || written + batch == count))
            {
                // A batch is 16 elements from a multiple of 16, within one word of the bitset.
                const uint32_t marks = (uint32_t)(converted[written / 64] >> (written % 64)) & 0xffffu;
                valid = check_literal_table_floating_batch(reader, tokens, spellings, lengths, marks, batch, element, width, payload + (uint64_t)written * (width / 8));
                written += batch;
                batch = 0;
                arena_pop_to(reader->scratch, batch_mark);
            }
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
    const uint32_t begin = check_sparse_get(input->initializer_begins, declaration);
    const uint32_t end = check_sparse_get(input->initializer_ends, declaration);
    TypeId type = reader->declared_types[declaration];
    const uint32_t kind = type_kind(reader->types, type);
    bool eligible = !reader->literal_tables_disabled && reader->function_body == BUSTER_CHECK_NONE && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) &&
                    (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) && begin < end && end <= parser->tokens.count &&
                    end - begin >= 2 * BUSTER_CHECK_LITERAL_TABLE_MINIMUM && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE &&
                    parser->tree.parents[begin] == end - 1 && parser->tokens.kinds[end - 1] == PARSE_KIND_R_BRACE;
    uint32_t width = 0;
    bool is_signed = false;
    const TypeId element = eligible ? type_element(reader->types, type) : 0;
    const bool floating = element != 0 && (type_unqualified(element) == BUSTER_TYPE_FLOAT || type_unqualified(element) == BUSTER_TYPE_DOUBLE);
    if (eligible)
    {
        width = floating ? (type_unqualified(element) == BUSTER_TYPE_FLOAT ? 32 : 64) : 0;
        eligible = (type_qualifiers(element) & BUSTER_TYPE_VOLATILE) == 0 &&
                   (floating || (type_kind(reader->types, element) == BUSTER_TYPE_KIND_INTEGER && type_unqualified(element) != BUSTER_TYPE_BOOL &&
                    check_action_integer_type(reader->types, element, &width, &is_signed) && (width == 8 || width == 16 || width == 32 || width == 64)));
    }
    const uint32_t count = eligible ? check_literal_table_shape(parser->tokens.kinds, begin + 1, end - 1, parser->options.scalar) : 0;
    eligible = eligible && count >= BUSTER_CHECK_LITERAL_TABLE_MINIMUM &&
               (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || count <= reader->types->types.extent[type_index(type)]);
    const uint64_t mark = reader->arena->length;
    const uint64_t bytes = (uint64_t)count * (width / 8);
    uint8_t* payload = eligible ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, bytes) : NULL;
    input->resource |= eligible && payload == NULL;
    bool accepted = eligible && payload != NULL && (!floating || check_literal_table_floating(reader, begin + 1, end - 1, count, element, width, payload));
    uint32_t tokens[BUSTER_CHECK_LITERAL_TABLE_BATCH];
    uint32_t batch = 0;
    uint32_t written = 0;
    for (uint32_t token = begin + 1; accepted && !floating && token + 1 < end; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_NUMBER)
        {
            tokens[batch] = token;
            batch += 1;
            if (batch == BUSTER_CHECK_LITERAL_TABLE_BATCH || written + batch == count)
            {
                uint8_t* destination = payload + (uint64_t)written * (width / 8);
                accepted = check_literal_table_batch(reader, tokens, batch, width, is_signed, destination);
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
        if (check_input_linkage(reader->declaration_bits, declaration) != 0)
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
