// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The os_log builtins' format strings (#148): __builtin_os_log_format_buffer_size's and __builtin_os_log_format's
// format, read as clang 23 reads it (clang/lib/AST/PrintfFormatString.cpp's ParsePrintfSpecifier with os_log's
// annotations, FormatString.cpp's amounts and length modifiers). Two passes over it: the layout's, which stops where
// clang/lib/AST/OSLog.cpp's handler stops and gives its items (each specifier's mask, field width, precision or count,
// and value, in that order, CheckOSLogItem), and the format checks', which go on past a %P without its length and record the
// data arguments any specifier takes and whether a mask's size is invalid (Sema's CheckFormatArguments, whose errors are
// os_log's). The items' sizes come from the arguments' types, which Sema knows (check_builtin_misc_os_log). Include
// after c_check_builtin_evidence.c.

// An amount (clang's OptionalAmount): none, a constant, a data argument's (positional or the next one's), or invalid.
typedef struct CheckOSLogAmount CheckOSLogAmount;
struct CheckOSLogAmount
{
    uint8_t how;
    uint32_t value;
};

#define BUSTER_CHECK_OS_LOG_AMOUNT_NONE 0u
#define BUSTER_CHECK_OS_LOG_AMOUNT_CONSTANT 1u
#define BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT 2u
#define BUSTER_CHECK_OS_LOG_AMOUNT_INVALID 3u

// The parser's place in the format, the next data argument a specifier without a position takes, whether a stop ended
// it, and the pass's outputs: the layout's `count` items, pushed on `arena` as the pass finds them (check_os_log_add;
// none for the checks' pass), whether a push failed (`full`), and the checks' arguments taken and invalid mask.
typedef struct CheckOSLogCursor CheckOSLogCursor;
struct CheckOSLogCursor
{
    const uint8_t* bytes;
    uint32_t length;
    uint32_t at;
    uint32_t next_argument;
    uint32_t arguments;
    bool stop;
    bool checking;
    bool c23;
    bool full;
    Arena* arena;
    CheckOSLogItem* items;
    uint32_t count;
    uint64_t taken[4];
    bool invalid_mask;
};

static bool check_os_log_space(uint8_t c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// ParseAmount: decimal digits, a constant, or none (the cursor kept).
static CheckOSLogAmount check_os_log_digits(CheckOSLogCursor* cursor)
{
    CheckOSLogAmount result = { .how = BUSTER_CHECK_OS_LOG_AMOUNT_NONE };
    uint32_t at = cursor->at;
    uint32_t value = 0;
    while (at < cursor->length && cursor->bytes[at] >= '0' && cursor->bytes[at] <= '9')
    {
        value = value * 10 + (uint32_t)(cursor->bytes[at] - '0');
        at += 1;
    }
    if (at != cursor->at)
    {
        result = (CheckOSLogAmount){ .how = BUSTER_CHECK_OS_LOG_AMOUNT_CONSTANT, .value = value };
        cursor->at = at;
    }
    return result;
}

// A field width's or a precision's amount (ParseNonPositionAmount, or ParsePositionAmount for a specifier with a
// position): `*` takes the next data argument, or `*N$` the N-th; digits are a constant. An invalid one stops the parse.
static CheckOSLogAmount check_os_log_amount(CheckOSLogCursor* cursor, bool positional)
{
    CheckOSLogAmount result = { .how = BUSTER_CHECK_OS_LOG_AMOUNT_NONE };
    if (cursor->at < cursor->length && cursor->bytes[cursor->at] == '*' && !positional)
    {
        cursor->at += 1;
        result = (CheckOSLogAmount){ .how = BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT, .value = cursor->next_argument++ };
    }
    else if (cursor->at < cursor->length && cursor->bytes[cursor->at] == '*')
    {
        CheckOSLogCursor digits = *cursor;
        digits.at += 1;
        const CheckOSLogAmount position = check_os_log_digits(&digits);
        const bool dollar = digits.at < digits.length && digits.bytes[digits.at] == '$';
        cursor->stop = position.how != BUSTER_CHECK_OS_LOG_AMOUNT_CONSTANT || !dollar || position.value == 0;
        result.how = cursor->stop ? BUSTER_CHECK_OS_LOG_AMOUNT_INVALID : BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT;
        result.value = cursor->stop ? 0 : position.value - 1;
        cursor->at = cursor->stop ? cursor->at : digits.at + 1;
    }
    else
    {
        result = check_os_log_digits(cursor);
    }
    return result;
}

// An annotation's segment, matched as clang's regular expression
// `^[[:space:]]*(private|public|sensitive|mask\.[^[:space:],}]*)[[:space:]]*(,|})` does: its word's place in `word`
// and `word_length`, the cursor past its `,` or `}`. False where it doesn't match (the cursor kept).
static bool check_os_log_annotation_word(CheckOSLogCursor* cursor, uint32_t* word, uint32_t* word_length)
{
    const uint8_t* bytes = cursor->bytes;
    uint32_t at = cursor->at;
    while (at < cursor->length && check_os_log_space(bytes[at]))
    {
        at += 1;
    }
    const uint32_t start = at;
    const uint32_t left = cursor->length - at;
    uint32_t end = at;
    if (left >= 7 && memcmp(bytes + at, "private", 7) == 0)
    {
        end = at + 7;
    }
    else if (left >= 6 && memcmp(bytes + at, "public", 6) == 0)
    {
        end = at + 6;
    }
    else if (left >= 9 && memcmp(bytes + at, "sensitive", 9) == 0)
    {
        end = at + 9;
    }
    else if (left >= 5 && memcmp(bytes + at, "mask.", 5) == 0)
    {
        end = at + 5;
        while (end < cursor->length && bytes[end] != ',' && bytes[end] != '}' && !check_os_log_space(bytes[end]))
        {
            end += 1;
        }
    }
    at = end;
    while (end != start && at < cursor->length && check_os_log_space(bytes[at]))
    {
        at += 1;
    }
    const bool matched = end != start && at < cursor->length && (bytes[at] == ',' || bytes[at] == '}');
    if (matched)
    {
        *word = start;
        *word_length = end - start;
        cursor->at = at + 1;
    }
    return matched;
}

// The kind of the conversion character `c` (ParsePrintfSpecifier's switch, on x86-64 Linux without OpenCL or fixed-point
// types): 0 for one that takes no data (%%), 1 for os_log's errno (%m), 2 for a data argument's, 3 for an invalid one.
// `item_kind` gets its os_log kind.
static uint32_t check_os_log_conversion(uint8_t c, uint8_t* item_kind)
{
    uint32_t result = 3;
    *item_kind = BUSTER_CHECK_OS_LOG_SCALAR;
    switch (c)
    {
        case '%': result = 0; break;
        case 'm': result = 1; *item_kind = BUSTER_CHECK_OS_LOG_ERRNO; break;
        case 's': result = 2; *item_kind = BUSTER_CHECK_OS_LOG_STRING; break;
        case 'S': result = 2; *item_kind = BUSTER_CHECK_OS_LOG_WIDE_STRING; break;
        case 'P': result = 2; *item_kind = BUSTER_CHECK_OS_LOG_POINTER; break;
        case '@': result = 2; *item_kind = BUSTER_CHECK_OS_LOG_OBJC; break;
        case 'A': case 'E': case 'F': case 'G': case 'X': case 'a': case 'c': case 'd': case 'e': case 'f': case 'g':
        case 'i': case 'n': case 'o': case 'p': case 'u': case 'x': case 'b': case 'B': case 'C': result = 2; break;
        default: break;
    }
    return result;
}

// ParseLengthModifier: the cursor past a length modifier, if any (C23's H, D, DD, wN and wfN in C23 alone); false where
// there is none.
static bool check_os_log_length(CheckOSLogCursor* cursor)
{
    const uint8_t* bytes = cursor->bytes;
    const uint32_t at = cursor->at;
    const uint8_t c = bytes[at];
    const uint8_t n = at + 1 < cursor->length ? bytes[at + 1] : 0;
    const uint8_t m = at + 2 < cursor->length ? bytes[at + 2] : 0;
    uint32_t length = 0;
    switch (c)
    {
        case 'h': length = n == 'h' ? 2 : 1; break;
        case 'l': length = n == 'l' ? 2 : 1; break;
        case 'j': case 'z': case 't': case 'L': case 'q': length = 1; break;
        case 'I': length = at + 2 < cursor->length && ((n == '6' && m == '4') || (n == '3' && m == '2')) ? 3 : 1; break;
        case 'H': length = cursor->c23 ? 1 : 0; break;
        case 'D': length = cursor->c23 ? (n == 'D' ? 2 : 1) : 0; break;
        case 'w':
        {
            // wN or wfN: a width of digits that doesn't start with 0, or else the plain `w` (AsWide).
            const uint32_t digits = at + 1 + (n == 'f' ? 1u : 0u);
            uint32_t end = digits;
            while (cursor->c23 && end < cursor->length && bytes[end] >= '0' && bytes[end] <= '9')
            {
                end += 1;
            }
            length = cursor->c23 && end != digits && bytes[digits] != '0' ? end - at : 1;
            break;
        }
        default: break;
    }
    cursor->at += length;
    return length != 0;
}

// The layout's next item, pushed where its items so far end: nothing else pushes on the arena during the pass, so they
// stay one array, exactly as long as the layout (#1329: no room reserved per `%`, which a %%, a byte past the format's
// NUL or a specifier of fewer items would leave unused). A push that fails stops the pass, its layout unknown (`full`).
static void check_os_log_add(CheckOSLogCursor* cursor, CheckOSLogItem item)
{
    CheckOSLogItem* slot = cursor->full ? NULL : BUSTER_ARENA_PUSH_ARRAY(cursor->arena, CheckOSLogItem, 1);
    cursor->items = cursor->count == 0 ? slot : cursor->items;
    assert(slot == NULL || slot == cursor->items + cursor->count);
    cursor->full = slot == NULL;
    cursor->stop |= cursor->full;
    if (!cursor->full)
    {
        *slot = item;
        cursor->count += 1;
    }
}

static void check_os_log_take(CheckOSLogCursor* cursor, uint32_t argument)
{
    cursor->taken[(argument / 64) & 3] |= argument < 256 ? UINT64_C(1) << (argument % 64) : 0;
}

// One specifier from the cursor's `%` (ParsePrintfSpecifier, then OSLogFormatStringHandler::HandlePrintfSpecifier and
// computeLayout's items for it, or the format checks'). A value without its data argument, or one of an amount, stops
// either pass; a %P without its length stops the layout's alone, past its value's item; a fail-stop stops both.
static void check_os_log_specifier(CheckOSLogCursor* cursor)
{
    const uint8_t* bytes = cursor->bytes;
    cursor->at += 1;
    cursor->stop = cursor->at >= cursor->length;
    bool positional = false;
    uint32_t position = 0;
    if (!cursor->stop)
    {
        // ParseArgPosition: N$ (from 1).
        CheckOSLogCursor digits = *cursor;
        const CheckOSLogAmount amount = check_os_log_digits(&digits);
        cursor->stop = digits.at >= cursor->length;
        positional = !cursor->stop && amount.how == BUSTER_CHECK_OS_LOG_AMOUNT_CONSTANT && bytes[digits.at] == '$';
        cursor->stop |= positional && amount.value == 0;
        position = positional ? amount.value - 1 : 0;
        cursor->at = positional ? digits.at + 1 : cursor->at;
        cursor->stop |= cursor->at >= cursor->length;
    }
    uint8_t flags = 0;
    uint32_t mask_start = 0;
    uint32_t mask_length = 0;
    bool masked = false;
    if (!cursor->stop && bytes[cursor->at] == '{')
    {
        // os_log's annotations, comma-separated, each privacy one taken where it is at least as strict as those before.
        cursor->at += 1;
        bool more = true;
        while (more && !cursor->stop)
        {
            uint32_t word = 0;
            uint32_t word_length = 0;
            if (check_os_log_annotation_word(cursor, &word, &word_length))
            {
                if (word_length >= 5 && memcmp(bytes + word, "mask.", 5) == 0)
                {
                    masked = true;
                    mask_start = word + 5;
                    mask_length = word_length - 5;
                    cursor->invalid_mask |= mask_length == 0 || mask_length > 8;
                }
                else if (word_length == 9)
                {
                    flags = BUSTER_CHECK_OS_LOG_SENSITIVE;
                }
                else if (word_length == 7 && flags != BUSTER_CHECK_OS_LOG_SENSITIVE)
                {
                    flags = BUSTER_CHECK_OS_LOG_PRIVATE;
                }
                else if (word_length == 6 && flags == 0)
                {
                    flags = BUSTER_CHECK_OS_LOG_PUBLIC;
                }
            }
            else
            {
                uint32_t at = cursor->at;
                while (at < cursor->length && bytes[at] != ',' && bytes[at] != '}')
                {
                    at += 1;
                }
                cursor->stop = at >= cursor->length;
                cursor->at = at + 1;
            }
            more = !cursor->stop && bytes[cursor->at - 1] == ',';
        }
    }
    while (!cursor->stop && cursor->at < cursor->length &&
           (bytes[cursor->at] == '\'' || bytes[cursor->at] == '-' || bytes[cursor->at] == '+' || bytes[cursor->at] == ' ' ||
            bytes[cursor->at] == '#' || bytes[cursor->at] == '0'))
    {
        cursor->at += 1;
    }
    cursor->stop |= cursor->at >= cursor->length;
    const CheckOSLogAmount width = cursor->stop ? (CheckOSLogAmount){ 0 } : check_os_log_amount(cursor, positional);
    cursor->stop |= cursor->at >= cursor->length;
    CheckOSLogAmount precision = { .how = BUSTER_CHECK_OS_LOG_AMOUNT_NONE };
    if (!cursor->stop && bytes[cursor->at] == '.')
    {
        cursor->at += 1;
        cursor->stop = cursor->at >= cursor->length;
        precision = cursor->stop ? precision : check_os_log_amount(cursor, positional);
        cursor->stop |= cursor->at >= cursor->length;
    }
    if (!cursor->stop)
    {
        cursor->stop = check_os_log_length(cursor) && cursor->at >= cursor->length;
    }
    // Objective-C's modifier flags, `[tt]` alone, before %@ alone; anything else there stops the parse.
    bool objc_flags = false;
    if (!cursor->stop && bytes[cursor->at] == '[')
    {
        uint32_t at = cursor->at + 1;
        while (at < cursor->length && bytes[at] != ']')
        {
            at += 1;
        }
        cursor->stop = at >= cursor->length || at - cursor->at - 1 != 2 || memcmp(bytes + cursor->at + 1, "tt", 2) != 0;
        objc_flags = true;
        cursor->at = at + 1;
        cursor->stop |= cursor->at >= cursor->length;
    }
    cursor->stop |= !cursor->stop && bytes[cursor->at] == 0;
    uint8_t kind = BUSTER_CHECK_OS_LOG_SCALAR;
    const uint32_t conversion = cursor->stop ? 0 : check_os_log_conversion(bytes[cursor->at], &kind);
    cursor->at += cursor->stop ? 0 : 1;
    cursor->stop |= objc_flags && conversion != 3 && kind != BUSTER_CHECK_OS_LOG_OBJC;
    const bool data = !cursor->stop && conversion == 2;
    const uint32_t argument = data && !positional ? cursor->next_argument++ : position;
    if (!cursor->stop && (data || conversion == 1))
    {
        const bool string = kind == BUSTER_CHECK_OS_LOG_STRING || kind == BUSTER_CHECK_OS_LOG_WIDE_STRING;
        const bool pointer = kind == BUSTER_CHECK_OS_LOG_POINTER;
        const bool counted = string || pointer;
        const bool missing = data && argument >= cursor->arguments;
        const bool unlengthened = pointer && precision.how == BUSTER_CHECK_OS_LOG_AMOUNT_NONE;
        const bool amounts = (width.how != BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT || width.value < cursor->arguments) &&
                             (precision.how != BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT || precision.value < cursor->arguments);
        cursor->stop = missing || !amounts || (unlengthened && !cursor->checking);
        if (cursor->checking && !missing && amounts)
        {
            // The format checks' arguments taken: the value's and the amounts'.
            check_os_log_take(cursor, data ? argument : UINT32_MAX);
            check_os_log_take(cursor, width.how == BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT ? width.value : UINT32_MAX);
            check_os_log_take(cursor, precision.how == BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT ? precision.value : UINT32_MAX);
        }
        else if (!missing && unlengthened)
        {
            // The handler returns before it reads the width, the privacy and the mask: the value's item alone.
            check_os_log_add(cursor, (CheckOSLogItem){ .kind = kind, .source = BUSTER_CHECK_OS_LOG_ARGUMENT, .argument = argument });
        }
        else if (!missing && amounts)
        {
            if (masked)
            {
                uint64_t value = 0;
                for (uint32_t i = 0; i < mask_length && i < 8; i += 1)
                {
                    value |= (uint64_t)bytes[mask_start + i] << (8 * i);
                }
                check_os_log_add(cursor, (CheckOSLogItem){ .kind = BUSTER_CHECK_OS_LOG_MASK, .source = BUSTER_CHECK_OS_LOG_CONSTANT, .constant = value });
            }
            if (width.how == BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT)
            {
                check_os_log_add(cursor, (CheckOSLogItem){ .kind = BUSTER_CHECK_OS_LOG_SCALAR, .source = BUSTER_CHECK_OS_LOG_ARGUMENT, .argument = width.value });
            }
            if (!counted && precision.how == BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT)
            {
                check_os_log_add(cursor, (CheckOSLogItem){ .kind = BUSTER_CHECK_OS_LOG_SCALAR, .source = BUSTER_CHECK_OS_LOG_ARGUMENT, .argument = precision.value });
            }
            if (counted && precision.how == BUSTER_CHECK_OS_LOG_AMOUNT_ARGUMENT)
            {
                check_os_log_add(cursor, (CheckOSLogItem){ .kind = BUSTER_CHECK_OS_LOG_COUNT, .source = BUSTER_CHECK_OS_LOG_ARGUMENT, .argument = precision.value });
            }
            if (counted && precision.how == BUSTER_CHECK_OS_LOG_AMOUNT_CONSTANT)
            {
                check_os_log_add(cursor, (CheckOSLogItem){ .kind = BUSTER_CHECK_OS_LOG_COUNT, .flags = flags, .source = BUSTER_CHECK_OS_LOG_CONSTANT,
                                                           .constant = precision.value });
            }
            check_os_log_add(cursor, (CheckOSLogItem){ .kind = kind, .flags = flags,
                                                       .source = conversion == 1 ? BUSTER_CHECK_OS_LOG_NONE : BUSTER_CHECK_OS_LOG_ARGUMENT,
                                                       .argument = argument });
        }
    }
}

// A pass over a format's `length` bytes (ParsePrintfString, to its first stop: a NUL, an incomplete specifier, an
// invalid position, a value or an amount without its argument).
static void check_os_log_parse(CheckOSLogCursor* cursor)
{
    while (!cursor->stop && cursor->at < cursor->length)
    {
        while (cursor->at < cursor->length && cursor->bytes[cursor->at] != '%' && cursor->bytes[cursor->at] != 0)
        {
            cursor->at += 1;
        }
        cursor->stop = cursor->at < cursor->length && cursor->bytes[cursor->at] == 0;
        if (!cursor->stop && cursor->at < cursor->length)
        {
            check_os_log_specifier(cursor);
        }
    }
}

// A format's layout and its checks' facts: its layout's items, `count` of them, and whether it is known; whether the
// arena couldn't hold them (`resource`); and the checks' invalid mask and arguments taken (the data arguments, as bits).
typedef struct CheckOSLogFormat CheckOSLogFormat;
struct CheckOSLogFormat
{
    CheckOSLogItem* items;
    uint32_t count;
    bool known;
    bool resource;
    bool invalid_mask;
    uint64_t taken[4];
};

// The two passes over a format's `length` bytes, of `arguments` data arguments (C23's length modifiers where `c23`).
// The layout's items are pushed on `arena`, 16 bytes for each and none past them (#1329): a format without items takes
// no room, whatever its percent signs. Where the arena can't hold them, the layout is unknown and the arena as it was.
static CheckOSLogFormat check_os_log_format(Arena* arena, const uint8_t* bytes, uint32_t length, uint32_t arguments, bool c23)
{
    const uint64_t start = arena->length;
    CheckOSLogCursor layout = {
        .bytes = bytes,
        .length = length,
        .arguments = arguments,
        .c23 = c23,
        .arena = arena,
    };
    check_os_log_parse(&layout);
    CheckOSLogCursor checks = {
        .bytes = bytes,
        .length = length,
        .arguments = arguments,
        .c23 = c23,
        .checking = true,
    };
    check_os_log_parse(&checks);
    if (layout.full)
    {
        arena_pop_to(arena, start);
    }
    CheckOSLogFormat result = {
        .items = layout.full ? NULL : layout.items,
        .count = layout.full ? 0 : layout.count,
        .known = !layout.full,
        .resource = layout.full,
        .invalid_mask = checks.invalid_mask,
    };
    memcpy(result.taken, checks.taken, sizeof(checks.taken));
    return result;
}

// The os_log builtins' evidence (#148): the format, the call's argument from `first` (its arguments' first token) past
// the buffer for __builtin_os_log_format (`buffer`), up to the call's `)` `closing`, of `count` arguments. It must be an
// ordinary or UTF-8 string literal past its parentheses and casts (clang's CheckOSLogFormatStringArg); its layout's
// items (CheckBuiltinCall.os_log_items) and its checks' facts are the call's.
static __attribute__((noinline)) void check_input_os_log_evidence(CheckInputReader* reader, uint32_t first, uint32_t closing, bool buffer, uint32_t count,
                                                                  CheckBuiltinCall* evidence)
{
    const Parser* parser = reader->parser;
    uint32_t begin = first;
    if (buffer && begin < closing)
    {
        begin = check_constant_punctuation(parser, begin, closing, PARSE_KIND_COMMA) + 1;
    }
    uint32_t end = begin < closing ? check_constant_punctuation(parser, begin, closing, PARSE_KIND_COMMA) : begin;
    bool stripping = begin < end;
    while (stripping)
    {
        const uint8_t role = parser->tree.roles[begin];
        const uint32_t close = parser->tree.parents[begin];
        if (parser->tokens.kinds[begin] == PARSE_KIND_L_PAREN && role == PARSE_ROLE_OPEN_GROUP && close == end - 1)
        {
            begin += 1;
            end -= 1;
        }
        else if (role == PARSE_ROLE_OPEN_CAST && close > begin && close + 1 < end)
        {
            begin = close + 1;
        }
        else
        {
            stripping = false;
        }
    }
    uint32_t after = begin;
    while (after < end && parser->tokens.kinds[after] == PARSE_KIND_STRING)
    {
        after += 1;
    }
    const CheckInitString* string = begin < end && after == end ? check_input_string(reader, begin) : NULL;
    const TypeId element = string != NULL ? type_element(reader->types, string->type) : 0;
    const bool literal = string != NULL && string->byte_count != 0 && string->byte_count - 1 <= UINT32_MAX &&
                         (element == BUSTER_TYPE_CHAR || (element == BUSTER_TYPE_UCHAR && string->utf8));
    const uint32_t arguments = count > (buffer ? 2u : 1u) ? count - (buffer ? 2u : 1u) : 0;
    if (literal)
    {
        const uint32_t length = (uint32_t)string->byte_count - 1;
        const CheckOSLogFormat format = check_os_log_format(reader->arena, string->bytes, length, arguments, parser->language.standard >= 2023);
        reader->output->resource |= format.resource;
        evidence->os_log_known = format.known;
        evidence->os_log_items = format.items;
        evidence->os_log_count = format.count;
        evidence->os_log_invalid_mask = format.invalid_mask;
        memcpy(evidence->os_log_taken, format.taken, sizeof(format.taken));
    }
}
