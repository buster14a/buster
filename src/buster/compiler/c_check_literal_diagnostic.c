// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The error clang gives for a numeric constant the checker refuses (#1127), and the character of its spelling it gives
// it at, as clang's NumericLiteralParser reads C's constants on x86-64: the radix's prefix and digits, a period and an
// exponent, then the suffix. c_check_literals.c decides whether a constant is valid and its value; this only words the
// refusal, once, as the driver prints it, so a constant that is valid costs nothing here. What clang accepts that the
// checker doesn't (`1.5q`, C2y's `0o17`) has no error here, and the checker's own message stays. Then the escape
// sequences clang refuses in an unevaluated string literal (check_literal_unevaluated_escapes, #147), which the reader
// tests its attributes' strings and static assertions' messages with, and the driver and the #line directive word.

#ifndef BUSTER_CHECK_LITERAL_DIAGNOSTIC_C
#define BUSTER_CHECK_LITERAL_DIAGNOSTIC_C

// Shared by the checker and the preprocessor; only a refused constant reaches this cold scan.
typedef enum CheckLiteralDiagnosticKind
{
    CHECK_LITERAL_DIAGNOSTIC_NONE,
    // "exponent has no digits"
    CHECK_LITERAL_DIAGNOSTIC_EXPONENT,
    // "invalid digit '%c' in %s constant": `character`, and `radix` (2, 8 or 10).
    CHECK_LITERAL_DIAGNOSTIC_DIGIT,
    // "invalid suffix '%.*s' on %s constant": the suffix from `offset` to the end, on a floating constant if `floating`.
    CHECK_LITERAL_DIAGNOSTIC_SUFFIX,
    // "hexadecimal floating constant requires an exponent", and "... a significand".
    CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT,
    CHECK_LITERAL_DIAGNOSTIC_HEX_SIGNIFICAND,
    // "digit separator cannot appear at %s of digit sequence": at its end if `floating` (reused as the side), else its start.
    CHECK_LITERAL_DIAGNOSTIC_SEPARATOR,
    // "integer literal is too large to be represented in any integer type", at the constant.
    CHECK_LITERAL_DIAGNOSTIC_TOO_LARGE,
    // "'size_t' suffix for literals is a C++23 feature", at the constant.
    CHECK_LITERAL_DIAGNOSTIC_SIZE_T,
} CheckLiteralDiagnosticKind;

typedef struct CheckLiteralDiagnostic CheckLiteralDiagnostic;
struct CheckLiteralDiagnostic
{
    uint32_t offset;
    uint8_t kind;
    uint8_t character;
    uint8_t radix;
    bool floating;
};

// The scan's state: the spelling, where it is, and the first error.
typedef struct CheckLiteralScan CheckLiteralScan;
struct CheckLiteralScan
{
    const uint8_t* bytes;
    uint32_t length;
    uint32_t at;
    bool separators;
    CheckLiteralDiagnostic error;
};

BUSTER_INLINE uint8_t check_literal_scan_byte(const CheckLiteralScan* scan, uint32_t at)
{
    return at < scan->length ? scan->bytes[at] : 0;
}

BUSTER_INLINE bool check_literal_is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

BUSTER_INLINE bool check_literal_is_hex_digit(uint8_t c)
{
    return check_literal_is_digit(c) || ((c | 32u) >= 'a' && (c | 32u) <= 'f');
}

// The first error only: later ones are clang's too, but its first is the one that counts.
static void check_literal_scan_error(CheckLiteralScan* scan, uint8_t kind, uint32_t offset)
{
    if (scan->error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE)
    {
        scan->error.kind = kind;
        scan->error.offset = offset;
    }
}

// Past the digits (of `radix`) and C23's digit separators from `at`.
static uint32_t check_literal_skip_digits(const CheckLiteralScan* scan, uint32_t at, uint32_t radix)
{
    uint32_t i = at;
    bool digit = true;
    while (i < scan->length && digit)
    {
        const uint8_t c = scan->bytes[i];
        digit = (scan->separators && c == '\'') || (radix == 16 ? check_literal_is_hex_digit(c) : c >= '0' && c < '0' + radix);
        i += digit;
    }
    return i;
}

// Whether the bytes from `from` to `to` hold a digit (not only separators).
static bool check_literal_has_digits(const CheckLiteralScan* scan, uint32_t from, uint32_t to)
{
    bool result = false;
    for (uint32_t i = from; i < to && !result; i += 1)
    {
        result = scan->bytes[i] != '\'';
    }
    return result;
}

// A digit separator just before `at` (`after`, at the end of a digit sequence) or at it (at the start of one).
static void check_literal_check_separator(CheckLiteralScan* scan, uint32_t at, bool after)
{
    const uint32_t place = after ? at - 1 : at;
    const bool inside = after ? at != 0 : at != scan->length;
    if (scan->separators && inside && check_literal_scan_byte(scan, place) == '\'')
    {
        check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_SEPARATOR, place);
        scan->error.floating = after;
    }
}

// A decimal or octal constant's rest, from a digit that isn't one of its radix's: a hexadecimal digit is a wrong one,
// a period and an exponent make it floating.
static void check_literal_decimal_or_octal(CheckLiteralScan* scan, uint32_t* radix, bool* saw_period, bool* saw_exponent)
{
    const uint8_t c = check_literal_scan_byte(scan, scan->at);
    if (check_literal_is_hex_digit(c) && (c | 32u) != 'e')
    {
        check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_DIGIT, scan->at);
        scan->error.character = c;
        scan->error.radix = (uint8_t)*radix;
    }
    else
    {
        if (c == '.')
        {
            check_literal_check_separator(scan, scan->at, true);
            scan->at += 1;
            *radix = 10;
            *saw_period = true;
            check_literal_check_separator(scan, scan->at, false);
            scan->at = check_literal_skip_digits(scan, scan->at, 10);
        }
        if ((check_literal_scan_byte(scan, scan->at) | 32u) == 'e')
        {
            check_literal_check_separator(scan, scan->at, true);
            const uint32_t exponent = scan->at;
            scan->at += 1;
            *radix = 10;
            *saw_exponent = true;
            const uint8_t sign = check_literal_scan_byte(scan, scan->at);
            scan->at += sign == '+' || sign == '-';
            const uint32_t digits = check_literal_skip_digits(scan, scan->at, 10);
            if (check_literal_has_digits(scan, scan->at, digits))
            {
                check_literal_check_separator(scan, scan->at, false);
                scan->at = digits;
            }
            else
            {
                check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_EXPONENT, exponent);
            }
        }
    }
}

// A constant that starts with `0`: hexadecimal, binary, C2y's octal prefix, or octal (which a period or an exponent
// makes decimal).
static void check_literal_zero(CheckLiteralScan* scan, uint32_t* radix, bool* saw_period, bool* saw_exponent)
{
    scan->at = 1;
    const uint8_t c1 = check_literal_scan_byte(scan, 1) | 32u;
    const uint8_t c2 = check_literal_scan_byte(scan, 2);
    if (c1 == 'x' && (check_literal_is_hex_digit(c2) || c2 == '.'))
    {
        *radix = 16;
        const uint32_t digits = 2;
        scan->at = check_literal_skip_digits(scan, digits, 16);
        bool significand = check_literal_has_digits(scan, digits, scan->at);
        if (check_literal_scan_byte(scan, scan->at) == '.')
        {
            scan->at += 1;
            *saw_period = true;
            const uint32_t fraction = scan->at;
            scan->at = check_literal_skip_digits(scan, fraction, 16);
            significand |= check_literal_has_digits(scan, fraction, scan->at);
            if (significand)
            {
                check_literal_check_separator(scan, fraction, false);
            }
        }
        if (!significand)
        {
            check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_HEX_SIGNIFICAND, scan->at);
        }
        else if ((check_literal_scan_byte(scan, scan->at) | 32u) == 'p')
        {
            check_literal_check_separator(scan, scan->at, true);
            const uint32_t exponent = scan->at;
            scan->at += 1;
            *saw_exponent = true;
            const uint8_t sign = check_literal_scan_byte(scan, scan->at);
            scan->at += sign == '+' || sign == '-';
            const uint32_t end = check_literal_skip_digits(scan, scan->at, 10);
            if (!check_literal_has_digits(scan, scan->at, end))
            {
                check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_EXPONENT, exponent);
            }
            else
            {
                check_literal_check_separator(scan, scan->at, false);
                scan->at = end;
            }
        }
        else if (*saw_period)
        {
            check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT, scan->at);
        }
    }
    else if (c1 == 'b' && (c2 == '0' || c2 == '1'))
    {
        *radix = 2;
        scan->at = check_literal_skip_digits(scan, 2, 2);
        const uint8_t c = check_literal_scan_byte(scan, scan->at);
        if (scan->at != scan->length && check_literal_is_hex_digit(c))
        {
            check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_DIGIT, scan->at);
            scan->error.character = c;
            scan->error.radix = 2;
        }
    }
    else if (c1 == 'o' && c2 >= '0' && c2 <= '7')
    {
        *radix = 8;
        scan->at = check_literal_skip_digits(scan, 2, 8);
        const uint8_t c = check_literal_scan_byte(scan, scan->at);
        if (scan->at != scan->length && check_literal_is_hex_digit(c) && (c | 32u) != 'e')
        {
            check_literal_scan_error(scan, CHECK_LITERAL_DIAGNOSTIC_DIGIT, scan->at);
            scan->error.character = c;
            scan->error.radix = 8;
        }
    }
    else
    {
        *radix = 8;
        scan->at = check_literal_skip_digits(scan, 1, 8);
        if (scan->at != scan->length)
        {
            // A decimal digit past the octal ones goes on as a decimal floating constant's, if a period or an
            // exponent follows them.
            if (check_literal_is_digit(check_literal_scan_byte(scan, scan->at)))
            {
                const uint32_t end = check_literal_skip_digits(scan, scan->at, 10);
                const uint8_t after = check_literal_scan_byte(scan, end);
                scan->at = after == '.' || (after | 32u) == 'e' ? end : scan->at;
                *radix = after == '.' || (after | 32u) == 'e' ? 10 : *radix;
            }
            check_literal_decimal_or_octal(scan, radix, saw_period, saw_exponent);
        }
    }
}

// clang's first error for the numeric constant `bytes` (its spelling after translation phases 1 and 2), into
// `diagnostic`, with C23's digit separators where `c23`. False if clang gives none.
static __attribute__((noinline, cold)) bool check_literal_diagnose(const uint8_t* bytes, uint32_t length, bool c23, CheckLiteralDiagnostic* diagnostic)
{
    CheckLiteralScan scan = { .bytes = bytes, .length = length, .separators = c23 };
    uint32_t radix = 10;
    bool saw_period = false;
    bool saw_exponent = false;
    if (length != 0 && bytes[0] == '0')
    {
        check_literal_zero(&scan, &radix, &saw_period, &saw_exponent);
    }
    else
    {
        scan.at = check_literal_skip_digits(&scan, 0, 10);
        if (scan.at != length)
        {
            check_literal_decimal_or_octal(&scan, &radix, &saw_period, &saw_exponent);
        }
    }
    const uint32_t digits_end = scan.at;
    // The suffix: its letters each once, in an order C allows; the first that isn't ends it, and what is left is invalid.
    const bool floating = saw_period || saw_exponent;
    bool size = false;
    bool is_unsigned = false;
    bool imaginary = false;
    bool size_t_suffix = false;
    bool bit_int = false;
    bool reading = scan.error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE;
    if (reading)
    {
        check_literal_check_separator(&scan, scan.at, true);
    }
    while (reading && scan.at < length)
    {
        const uint8_t c = bytes[scan.at];
        const uint8_t lower = c | 32u;
        uint32_t step = 0;
        if (lower == 'f' && floating && !size)
        {
            // f16, _Float16's, which x86-64 has.
            step = scan.at + 2 < length && bytes[scan.at + 1] == '1' && bytes[scan.at + 2] == '6' ? 3 : 1;
            size = true;
        }
        else if (lower == 'q' && floating && !size)
        {
            step = 1;
            size = true;
        }
        else if (lower == 'u' && !floating && !is_unsigned)
        {
            step = 1;
            is_unsigned = true;
        }
        else if (lower == 'l' && !size && !(floating && scan.at + 1 < length && bytes[scan.at + 1] == c))
        {
            step = scan.at + 1 < length && bytes[scan.at + 1] == c ? 2 : 1;
            size = true;
        }
        else if (lower == 'z' && !floating && !size)
        {
            step = 1;
            size = true;
            size_t_suffix = true;
        }
        else if ((lower == 'i' || lower == 'j') && !imaginary)
        {
            step = 1;
            imaginary = true;
        }
        else if (lower == 'w' && !floating && !size && scan.at + 1 < length && ((c == 'w' && bytes[scan.at + 1] == 'b') || (c == 'W' && bytes[scan.at + 1] == 'B')))
        {
            step = 2;
            size = true;
            bit_int = true;
        }
        reading = step != 0;
        scan.at += step;
    }
    if (scan.error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE && scan.at != length)
    {
        check_literal_scan_error(&scan, CHECK_LITERAL_DIAGNOSTIC_SUFFIX, digits_end);
        scan.error.floating = floating;
    }
    else if (scan.error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE && size_t_suffix)
    {
        check_literal_scan_error(&scan, CHECK_LITERAL_DIAGNOSTIC_SIZE_T, 0);
    }
    else if (scan.error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE && !floating && !bit_int)
    {
        // Past 64 bits: no integer type holds it (clang's literals are no wider than unsigned long long).
        const uint32_t first = radix == 16 || radix == 2 ? 2 : (radix == 8 && length > 1 && (bytes[1] | 32u) == 'o') ? 2 : 0;
        uint64_t value = 0;
        bool fits = true;
        for (uint32_t i = first; i < digits_end; i += 1)
        {
            const uint8_t c = bytes[i];
            const uint32_t digit = c == '\'' ? radix : check_literal_is_digit(c) ? (uint32_t)(c - '0') : (uint32_t)((c | 32u) - 'a' + 10);
            fits &= digit >= radix || value <= (UINT64_MAX - digit) / radix;
            value = digit < radix ? value * radix + digit : value;
        }
        if (!fits)
        {
            check_literal_scan_error(&scan, CHECK_LITERAL_DIAGNOSTIC_TOO_LARGE, 0);
        }
    }
    *diagnostic = scan.error;
    if (scan.error.kind == CHECK_LITERAL_DIAGNOSTIC_NONE)
    {
        // A well-formed floating constant is still forbidden in #if; keep the separator error
        // above free to use this field for its start/end distinction.
        diagnostic->floating = floating;
    }
    return scan.error.kind != CHECK_LITERAL_DIAGNOSTIC_NONE;
}

// The diagnostics of an unevaluated string literal's escape sequences (#147): clang's StringLiteralParser with
// StringLiteralEvalMethod::Unevaluated reads an attribute's string argument, a static assertion's message and a #line
// directive's file name so, and takes only the simple escapes there (\\ \' \" \? \a \b \f \n \r \t \v) and the
// universal character names, which the decoders read. Every other escape sequence is "invalid escape sequence 'S' in an
// unevaluated string literal", S as written, after the diagnostics clang's ProcessCharEscape gives it first; then clang
// drops what the literal belonged to (an attribute, a static assertion, the directive). Each is at the escape's
// backslash. A literal's characters are a byte wide there, whatever its prefix, as clang's are: an octal or hexadecimal
// value past 255 is out of range. -pedantic's extension warnings (\e, \E, \(, \{, \[ and \%, the delimited escapes) are
// left out: they are off by default.
typedef enum CheckEscapeDiagnosticKind
{
    CHECK_ESCAPE_DIAGNOSTIC_NONE,
    // "unknown escape sequence '\c'", a warning (-Wunknown-escape-sequence), `character` the escaped byte: clang prints
    // one that isn't printable as its value, sign-extended from char, in hexadecimal ('\xFFFFFFC3').
    CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN,
    // "\x used with no following hex digits", after which clang reads on without the unevaluated error.
    CHECK_ESCAPE_DIAGNOSTIC_HEX_DIGITS,
    // "hex escape sequence out of range" and "octal escape sequence out of range".
    CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE,
    CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE,
    // A delimited escape's (\x{...} and \o{...}): "delimited escape sequence cannot be empty", "invalid digit 'c' in escape
    // sequence" (`character`), "expected '{' after '\o' escape sequence" and "expected '}'".
    CHECK_ESCAPE_DIAGNOSTIC_EMPTY,
    CHECK_ESCAPE_DIAGNOSTIC_DIGIT,
    CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE,
    CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE,
    // "invalid escape sequence 'S' in an unevaluated string literal", S the escape's `length` bytes.
    CHECK_ESCAPE_DIAGNOSTIC_UNEVALUATED,
} CheckEscapeDiagnosticKind;

typedef struct CheckEscapeDiagnostic CheckEscapeDiagnostic;
struct CheckEscapeDiagnostic
{
    // The escape's backslash, from the spelling's start.
    uint32_t offset;
    uint32_t length;
    uint8_t kind;
    uint8_t character;
};

// The most diagnostics check_literal_unevaluated_escapes gives a spelling of `length` bytes: an escape of k bytes (two at
// least) gives at most k - 3 invalid digits and four others.
#define BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length) (2 * (uint64_t)(length) + 4)

// Appends a diagnostic of the escape at `offset`, where `output` (room for BUSTER_CHECK_ESCAPE_DIAGNOSTICS) isn't null.
static void check_literal_escape_append(CheckEscapeDiagnostic* output, uint32_t* count, uint8_t kind, uint32_t offset, uint32_t length, uint8_t character)
{
    if (output != NULL)
    {
        output[*count] = (CheckEscapeDiagnostic){ .offset = offset, .length = length, .kind = kind, .character = character };
    }
    *count += 1;
}

// One escape sequence, at the backslash at `escape` (before `end`, the closing quote), as clang's ProcessCharEscape reads
// it in an unevaluated string literal: its diagnostics appended, in clang's order. Returns the offset past it. A
// universal character name is past its letter alone: its digits hold no backslash, and the decoders check them.
// `*failed` is clang's StringLiteralParser.hadError, the whole literal's, across its tokens: an earlier error (the
// unevaluated one, a delimited escape's, an out of range hexadecimal or delimited octal one) silences the range errors
// that test it (a plain octal escape's doesn't), and this escape's errors set it.
static uint32_t check_literal_unevaluated_escape(const uint8_t* bytes, uint32_t escape, uint32_t end, CheckEscapeDiagnostic* output, uint32_t* count, bool* had_error)
{
    const uint8_t c = escape + 1 < end ? bytes[escape + 1] : '\\';
    uint32_t at = escape + 2 <= end ? escape + 2 : end;
    // Whether clang takes the escape there, and whether it gives the escape the unevaluated error.
    bool valid = c == '\\' || c == '\'' || c == '"' || c == '?' || c == 'a' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't' || c == 'v' ||
                 c == 'u' || c == 'U' || c == 'N';
    bool delimited = false;
    bool closed = false;
    bool failed = *had_error;
    if (c == 'x')
    {
        uint32_t value = 0;
        bool overflow = false;
        if (at < end && bytes[at] == '{')
        {
            delimited = true;
            at += 1;
            if (at < end && bytes[at] == '}')
            {
                failed = true;
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_EMPTY, escape, 0, 0);
            }
        }
        else if (at == end || !check_literal_is_hex_digit(bytes[at]))
        {
            // clang returns here: the escape is no unevaluated error.
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_HEX_DIGITS, escape, 0, 0);
            valid = true;
        }
        bool reading = !valid;
        while (reading && at < end)
        {
            const uint8_t digit = bytes[at];
            if (delimited && digit == '}')
            {
                closed = true;
                reading = false;
            }
            else if (!check_literal_is_hex_digit(digit))
            {
                reading = delimited;
                failed |= delimited;
                if (delimited)
                {
                    check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DIGIT, escape, 0, digit);
                }
            }
            else
            {
                overflow |= (value & 0xF0000000u) != 0;
                value = value << 4 | (check_literal_is_digit(digit) ? (uint32_t)(digit - '0') : (uint32_t)((digit | 32u) - 'a' + 10));
            }
            at += reading || closed;
        }
        if (!valid && !failed && (overflow || value > 0xFFu))
        {
            failed = true;
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE, escape, 0, 0);
        }
    }
    else if (c >= '0' && c <= '7')
    {
        // At most three digits; clang's octal escape past a byte is no error that drops the literal by itself.
        uint32_t value = 0;
        uint32_t digits = 0;
        at = escape + 1;
        while (at < end && digits < 3 && bytes[at] >= '0' && bytes[at] <= '7')
        {
            value = value << 3 | (uint32_t)(bytes[at] - '0');
            digits += 1;
            at += 1;
        }
        if (value > 0xFFu)
        {
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE, escape, 0, 0);
        }
    }
    else if (c == 'o' && (at == end || bytes[at] != '{'))
    {
        failed = true;
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE, escape, 0, 0);
    }
    else if (c == 'o')
    {
        uint32_t value = 0;
        bool overflow = false;
        delimited = true;
        at += 1;
        if (at < end && bytes[at] == '}')
        {
            failed = true;
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_EMPTY, escape, 0, 0);
        }
        while (!closed && at < end)
        {
            const uint8_t digit = bytes[at];
            closed = digit == '}';
            if (!closed && (digit < '0' || digit > '7'))
            {
                failed = true;
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DIGIT, escape, 0, digit);
            }
            else if (!closed)
            {
                overflow |= (value & 0xE0000000u) != 0;
                value = value << 3 | (uint32_t)(digit - '0');
            }
            at += 1;
        }
        if (!failed && (overflow || value > 0xFFu))
        {
            failed = true;
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE, escape, 0, 0);
        }
    }
    else if (!valid && c != 'e' && c != 'E' && c != '(' && c != '{' && c != '[' && c != '%')
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN, escape, 0, c);
    }
    if (delimited && !closed)
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE, escape, 0, 0);
    }
    if (!valid)
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_UNEVALUATED, escape, at - escape, 0);
    }
    *had_error = failed || !valid;
    return at;
}

// The diagnostics of the escape sequences of the string literal `bytes` (its spelling after translation phases 1 and 2,
// with its prefix and quotes), into `output` (room for BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length) of them, or null to count
// them alone), as an unevaluated string literal's: their count, 0 where clang takes the literal there. `*had_error` is
// the literal's error state (check_literal_unevaluated_escape), false before its first token, carried from token to
// token. Its backslashes are found with memchr (libc's vector search); only an escape sequence's own bytes are read one
// at a time. Its callers' strings are an attribute's arguments', a static assertion's message and a #line directive's
// file name: short and few, a cold path of no vector twin.
static uint32_t check_literal_unevaluated_escapes(const uint8_t* bytes, uint32_t length, CheckEscapeDiagnostic* output, bool* had_error)
{
    const uint8_t* quote = length != 0 ? memchr(bytes, '"', length) : NULL;
    const uint32_t begin = quote != NULL ? (uint32_t)(quote - bytes) + 1 : length;
    const uint32_t end = length > begin && bytes[length - 1] == '"' ? length - 1 : length;
    uint32_t count = 0;
    uint32_t at = begin;
    while (at < end)
    {
        const uint8_t* backslash = memchr(bytes + at, '\\', end - at);
        at = backslash != NULL ? check_literal_unevaluated_escape(bytes, (uint32_t)(backslash - bytes), end, output, &count, had_error) : end;
    }
    return count;
}

#endif
