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
// universal character names, which the decoders read, after clang's own checks of them (check_literal_unevaluated_ucn).
// Every other escape sequence is "invalid escape sequence 'S' in an
// unevaluated string literal", S as written, after the diagnostics clang's ProcessCharEscape gives it first; then clang
// drops what the literal belonged to (an attribute, a static assertion, the directive). Each is at the escape's
// backslash. A literal's characters are a byte wide there, whatever its prefix, as clang's are: an octal or hexadecimal
// value past 255 is out of range. -pedantic's extension warnings (\e, \E, \(, \{, \[ and \%, the delimited and named
// escapes) are given where the caller asks for them (#1447).
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
    // A delimited escape's (\x{...}, \o{...}, \u{...} and \N{...}): "delimited escape sequence cannot be empty", "invalid
    // digit 'c' in escape sequence" (`character`), "expected '{' after '\c' escape sequence" (`character` the escape's
    // letter, o or N) and "expected '}'".
    CHECK_ESCAPE_DIAGNOSTIC_EMPTY,
    CHECK_ESCAPE_DIAGNOSTIC_DIGIT,
    CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE,
    CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE,
    // A universal character name's (check_literal_unevaluated_ucn): "\c used with no following hex digits" (`character` u
    // or U), "incomplete universal character name", "invalid universal character" (a surrogate or past U+10FFFF), and
    // before C23 "universal character name refers to a control character" and "character 'c' cannot be specified by a
    // universal character name" (`character` the basic one).
    CHECK_ESCAPE_DIAGNOSTIC_UCN_DIGITS,
    CHECK_ESCAPE_DIAGNOSTIC_UCN_INCOMPLETE,
    CHECK_ESCAPE_DIAGNOSTIC_UCN_INVALID,
    CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL,
    CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC,
    // A named universal character name's name that isn't Unicode's (#1447): "'NAME' is not a valid Unicode character
    // name" at the name, three bytes past the backslash, with clang's notes (unicode_name_diagnose).
    CHECK_ESCAPE_DIAGNOSTIC_UCN_NAME,
    // "invalid escape sequence 'S' in an unevaluated string literal", S the escape's `length` bytes.
    CHECK_ESCAPE_DIAGNOSTIC_UNEVALUATED,
    // -pedantic's warnings (#1447), given only where the caller asks for them: "use of non-standard escape character '\c'"
    // (-Wpedantic) at a GNU escape's backslash (\e, \E, \(, \{, \[ and \%), and -Wdelimited-escape-sequence-extension's
    // "delimited escape sequences are a C2y extension" and "named escape sequences are a Clang extension", which clang
    // places at the literal's token (offset 0), after a delimited or named escape that it read whole while the literal
    // had no error.
    CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD,
    CHECK_ESCAPE_DIAGNOSTIC_DELIMITED,
    CHECK_ESCAPE_DIAGNOSTIC_NAMED,
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

// A universal character name at the backslash at `escape` (before `end`), \u, \U or \N, as clang's ProcessUCNEscape
// checks it in any string literal (an unevaluated one's too): a numeric one's digits (four of \u, eight of \U, or \u{...}'s
// delimited ones), a named one's braces and name (Unicode's exactly, #1447: unicode_name_strict; it ends at a line break
// too), and then its value. Its diagnostics appended at the backslash, `*failed` set by any. Returns the offset past
// what clang reads of it.
static uint32_t check_literal_unevaluated_ucn(const uint8_t* bytes, uint32_t escape, uint32_t end, bool c23, bool pedantic, bool evaluated, CheckEscapeDiagnostic* output,
                                              uint32_t* count, bool* failed)
{
    const uint8_t letter = bytes[escape + 1];
    // (-pedantic's extension warning, after an escape read whole while the literal had no error: a named one of no error of
    // its own, a delimited one whose value alone may be refused.)
    const bool before = *failed;
    bool extension = false;
    uint32_t at = escape + 2;
    uint8_t kind = CHECK_ESCAPE_DIAGNOSTIC_NONE;
    uint8_t character = 0;
    bool invalid = false;
    if (letter == 'N')
    {
        const bool braced = at < end && bytes[at] == '{';
        uint32_t close = braced ? at + 1 : at;
        while (braced && close < end && bytes[close] != '}' && bytes[close] != '\n' && bytes[close] != '\r')
        {
            close += 1;
        }
        uint32_t value = 0;
        const bool shaped = braced && close != end && close != at + 1;
        const bool named = shaped && unicode_name_strict(bytes + at + 1, close - at - 1, &value);
        const bool basic = value < 0xA0 && value != 0x24 && value != 0x40 && value != 0x60;
        kind = !braced ? CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE : close == end ? CHECK_ESCAPE_DIAGNOSTIC_UCN_INCOMPLETE :
               close == at + 1 ? CHECK_ESCAPE_DIAGNOSTIC_EMPTY : !named ? CHECK_ESCAPE_DIAGNOSTIC_UCN_NAME :
               !c23 && basic ? (value >= 0x20 && value < 0x7F ? CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC : CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL) : CHECK_ESCAPE_DIAGNOSTIC_NONE;
        character = kind == CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC ? (uint8_t)value : letter;
        at = braced && close < end ? close + 1 : braced ? close : at;
        extension = kind == CHECK_ESCAPE_DIAGNOSTIC_NONE;
    }
    else
    {
        const bool delimited = letter == 'u' && at < end && bytes[at] == '{';
        at += delimited ? 1 : 0;
        const bool digits_follow = delimited || (at < end && check_literal_is_hex_digit(bytes[at]));
        const uint32_t width = letter == 'u' ? 4 : 8;
        uint32_t value = 0;
        uint32_t digits = 0;
        bool overflow = false;
        bool closed = false;
        bool reading = digits_follow;
        while (reading && at < end && (delimited || digits != width))
        {
            const uint8_t digit = bytes[at];
            const bool hex = check_literal_is_hex_digit(digit);
            closed = delimited && digit == '}';
            if (!closed && !hex && delimited)
            {
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DIGIT, escape, 0, digit);
            }
            invalid |= !closed && !hex;
            overflow |= hex && (value & 0xF0000000u) != 0;
            value = hex && (value & 0xF0000000u) == 0 ? value << 4 | (check_literal_is_digit(digit) ? (uint32_t)(digit - '0') : (uint32_t)((digit | 32u) - 'a' + 10)) : value;
            // (A delimited one's bad digits count, so `\u{g}` isn't empty; a plain one's terminator isn't a digit: `\u123g` is
            // incomplete.)
            digits += (delimited && !closed && !hex) || (hex && !overflow) ? 1 : 0;
            // A plain name's first other byte ends it; a delimited one reads on to its brace.
            reading = !closed && (hex || delimited);
            at += reading || closed ? 1 : 0;
        }
        const bool basic = value < 0xA0 && value != 0x24 && value != 0x40 && value != 0x60;
        kind = !digits_follow ? CHECK_ESCAPE_DIAGNOSTIC_UCN_DIGITS : overflow ? CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE :
               delimited && !closed ? CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE :
               digits == 0 ? CHECK_ESCAPE_DIAGNOSTIC_EMPTY : !delimited && digits != width ? CHECK_ESCAPE_DIAGNOSTIC_UCN_INCOMPLETE :
               invalid ? CHECK_ESCAPE_DIAGNOSTIC_NONE : (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF ? CHECK_ESCAPE_DIAGNOSTIC_UCN_INVALID :
               !c23 && basic ? (value >= 0x20 && value < 0x7F ? CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC : CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL) : CHECK_ESCAPE_DIAGNOSTIC_NONE;
        character = kind == CHECK_ESCAPE_DIAGNOSTIC_UCN_DIGITS ? letter : (uint8_t)value;
        extension = delimited && !invalid && (kind == CHECK_ESCAPE_DIAGNOSTIC_NONE || kind == CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC || kind == CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL);
    }
    if (kind != CHECK_ESCAPE_DIAGNOSTIC_NONE)
    {
        check_literal_escape_append(output, count, kind, escape, 0, character);
    }
    // (An evaluated literal's (#1447): after one of no error of its own, whatever came before, as clang's ProcessUCNEscape.)
    const bool warned = evaluated ? extension && kind == CHECK_ESCAPE_DIAGNOSTIC_NONE : extension && !before;
    if (pedantic && warned)
    {
        check_literal_escape_append(output, count, letter == 'N' ? CHECK_ESCAPE_DIAGNOSTIC_NAMED : CHECK_ESCAPE_DIAGNOSTIC_DELIMITED, 0, 0, 0);
    }
    *failed |= invalid || kind != CHECK_ESCAPE_DIAGNOSTIC_NONE;
    return at;
}

// One escape sequence, at the backslash at `escape` (before `end`, the closing quote), as clang's ProcessCharEscape reads
// it in an unevaluated string literal: its diagnostics appended, in clang's order. Returns the offset past it. A
// universal character name is check_literal_unevaluated_ucn's (`c23`: whether the unit's standard is C23's).
// `*failed` is clang's StringLiteralParser.hadError, the whole literal's, across its tokens: an earlier error (the
// unevaluated one, a delimited escape's, an out of range hexadecimal or delimited octal one) silences the range errors
// that test it (a plain octal escape's doesn't), and this escape's errors set it.
static uint32_t check_literal_unevaluated_escape(const uint8_t* bytes, uint32_t escape, uint32_t end, bool c23, bool pedantic, CheckEscapeDiagnostic* output,
                                                 uint32_t* count, bool* had_error)
{
    const uint8_t c = escape + 1 < end ? bytes[escape + 1] : '\\';
    uint32_t at = escape + 2 <= end ? escape + 2 : end;
    // Whether clang takes the escape there, and whether it gives the escape the unevaluated error.
    bool valid = c == '\\' || c == '\'' || c == '"' || c == '?' || c == 'a' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't' || c == 'v' ||
                 c == 'u' || c == 'U' || c == 'N';
    bool delimited = false;
    bool closed = false;
    bool failed = *had_error;
    if (c == 'u' || c == 'U' || c == 'N')
    {
        at = check_literal_unevaluated_ucn(bytes, escape, end, c23, pedantic, false, output, count, &failed);
    }
    else if (c == 'x')
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
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE, escape, 0, 'o');
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
    else if (!valid && pedantic)
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD, escape, 0, c);
    }
    if (delimited && !closed)
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE, escape, 0, 0);
    }
    else if (delimited && pedantic && !failed)
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DELIMITED, 0, 0, 0);
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
// file name: short and few, a cold path of no vector twin. `c23`: whether the unit's standard is C23's, which takes a universal
// character name of a control or basic character in a literal. `pedantic`: whether to give -pedantic's warnings too
// (#1447, CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD and after it), which a literal clang takes may have: a caller that asks
// whether clang refuses the literal passes false.
static uint32_t check_literal_unevaluated_escapes(const uint8_t* bytes, uint32_t length, bool c23, bool pedantic, CheckEscapeDiagnostic* output, bool* had_error)
{
    const uint8_t* quote = length != 0 ? memchr(bytes, '"', length) : NULL;
    const uint32_t begin = quote != NULL ? (uint32_t)(quote - bytes) + 1 : length;
    const uint32_t end = length > begin && bytes[length - 1] == '"' ? length - 1 : length;
    uint32_t count = 0;
    uint32_t at = begin;
    while (at < end)
    {
        const uint8_t* backslash = memchr(bytes + at, '\\', end - at);
        at = backslash != NULL ? check_literal_unevaluated_escape(bytes, (uint32_t)(backslash - bytes), end, c23, pedantic, output, &count, had_error) : end;
    }
    return count;
}

// Whether the escape at the backslash at `escape` (before `end`) is one clang reads as unknown, whose character is its
// value with -Wunknown-escape-sequence's warning (#1447): not a simple, numeric or universal character name's, nor GNU's
// \e and \E, nor \(, \{, \[ and \%, which -pedantic alone words.
static bool check_literal_unknown_escape(const uint8_t* bytes, uint32_t escape, uint32_t end)
{
    const uint8_t c = escape + 1 < end ? bytes[escape + 1] : 0;
    const bool known = (c != 0 && strchr("abefnrtv\\'\"?xuUoNE({[%", c) != NULL) || (c >= '0' && c <= '7');
    return escape + 1 < end && !known && c >= 0x20 && c < 0x7F;
}

// One escape sequence of an evaluated literal (#1447), at the backslash at `escape` (before `end`), as clang's
// ProcessCharEscape reads it there: its diagnostics appended in clang's order and the offset past it returned. An unknown
// escape is -Wunknown-escape-sequence's warning (but GNU's \e and \E, and \(, \{, \[ and \%, which -pedantic alone
// words); a numeric one past `maximum`, the literal's code unit's most (a byte's, a char16_t's, or none for 32 bits),
// is out of range (a hexadecimal or a delimited one's only while `*had_error`, the literal's error state, is clear); a
// delimited one's empty braces, bad digits and missing brace, and a universal character name's errors
// (check_literal_unevaluated_ucn) are the unevaluated literals' errors too.
static uint32_t check_literal_evaluated_escape(const uint8_t* bytes, uint32_t escape, uint32_t end, bool c23, bool pedantic, uint32_t maximum, CheckEscapeDiagnostic* output,
                                               uint32_t* count, bool* had_error)
{
    const uint8_t c = escape + 1 < end ? bytes[escape + 1] : '\\';
    uint32_t at = escape + 2 <= end ? escape + 2 : end;
    bool failed = *had_error;
    if (c == 'u' || c == 'U' || c == 'N')
    {
        at = check_literal_unevaluated_ucn(bytes, escape, end, c23, pedantic, true, output, count, &failed);
    }
    else if (c == 'x' || c == 'o')
    {
        const bool delimited = at < end && bytes[at] == '{';
        const uint32_t radix = c == 'x' ? 16 : 8;
        if (c == 'o' && !delimited)
        {
            failed = true;
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE, escape, 0, 'o');
        }
        else if (c == 'x' && !delimited && (at == end || !check_literal_is_hex_digit(bytes[at])))
        {
            // An error that leaves the escapes after it their range errors (clang's literal parser returns from it
            // without HadError).
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_HEX_DIGITS, escape, 0, 0);
        }
        else
        {
            uint64_t value = 0;
            uint32_t digits = 0;
            bool overflow = false;
            bool closed = false;
            bool bad = false;
            bool reading = true;
            at += delimited ? 1 : 0;
            while (reading && at < end)
            {
                const uint8_t digit = bytes[at];
                const uint32_t number = check_literal_is_digit(digit) ? (uint32_t)(digit - '0') : check_literal_is_hex_digit(digit) ? (uint32_t)((digit | 32u) - 'a' + 10) : 99;
                closed = delimited && digit == '}';
                if (closed)
                {
                    reading = false;
                    at += 1;
                }
                else if (number >= radix)
                {
                    // A plain hexadecimal escape ends at its first other byte; a delimited one reads on to its brace.
                    reading = delimited;
                    bad |= delimited;
                    if (delimited)
                    {
                        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DIGIT, escape, 0, digit);
                        at += 1;
                    }
                }
                else
                {
                    overflow |= value > (UINT32_MAX >> (radix == 16 ? 4 : 3));
                    value = (value * radix + number) & UINT32_MAX;
                    digits += 1;
                    at += 1;
                }
            }
            if (delimited && closed && digits == 0 && !bad)
            {
                failed = true;
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_EMPTY, escape, 0, 0);
            }
            else if (delimited && !closed)
            {
                failed = true;
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE, escape, 0, 0);
            }
            else if (!failed && !bad && (overflow || value > maximum))
            {
                failed = true;
                check_literal_escape_append(output, count, c == 'x' ? CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE : CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE, escape, 0, 0);
            }
            failed |= bad;
            if (delimited && pedantic && !failed)
            {
                // -pedantic's, at the literal, of one read whole while the literal has no error (#1447).
                check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_DELIMITED, 0, 0, 0);
            }
        }
    }
    else if (c >= '0' && c <= '7')
    {
        // At most three digits; one past the code unit is an error whatever came before (clang's "Reject '\777'").
        uint32_t value = 0;
        uint32_t digits = 0;
        at = escape + 1;
        while (at < end && digits < 3 && bytes[at] >= '0' && bytes[at] <= '7')
        {
            value = value << 3 | (uint32_t)(bytes[at] - '0');
            digits += 1;
            at += 1;
        }
        if (value > maximum)
        {
            failed = true;
            check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE, escape, 0, 0);
        }
    }
    else if (check_literal_unknown_escape(bytes, escape, end))
    {
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN, escape, 0, c);
    }
    else if (pedantic && (c == 'e' || c == 'E' || c == '(' || c == '{' || c == '[' || c == '%'))
    {
        // GNU's, -pedantic's "use of non-standard escape character" at its backslash (#1447).
        check_literal_escape_append(output, count, CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD, escape, 0, c);
    }
    *had_error = failed;
    return at;
}

// The diagnostics of the escape sequences of the evaluated literal `bytes` (#1447: a string literal's or a character
// constant's spelling, with its prefix and quotes), into `output` (room for BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length), or
// null to count them alone), their count returned: check_literal_evaluated_escape's of each escape, the code unit's
// most from the prefix (none, u8: a byte; u: 16 bits; U and L: 32, of no range error). A raw string has no escapes.
// `*had_error` is carried from token to token, as the unevaluated literals' is. `pedantic`: whether to give -pedantic's
// warnings too (CHECK_ESCAPE_DIAGNOSTIC_NONSTANDARD, _DELIMITED and _NAMED). The cold path of a literal the checker
// refused, one its extended decoding read (check_literal_strings_extended), or one of GNU's \e that -pedantic may word,
// of no vector twin.
static __attribute__((unused)) uint32_t check_literal_evaluated_escapes(const uint8_t* bytes, uint32_t length, bool c23, bool pedantic, CheckEscapeDiagnostic* output,
                                                                       bool* had_error)
{
    // (Its prefix, L, u, U, u8 and R, holds no quote.)
    uint32_t opening = 0;
    while (opening < length && bytes[opening] != '"' && bytes[opening] != '\'')
    {
        opening += 1;
    }
    const uint8_t quote_kind = opening < length ? bytes[opening] : '"';
    const bool raw = opening != 0 && opening < length && bytes[opening - 1] == 'R';
    const uint32_t maximum = opening != 0 && (bytes[0] == 'L' || bytes[0] == 'U') ? UINT32_MAX : opening != 0 && bytes[0] == 'u' && (opening < 2 || bytes[1] != '8') ? 0xFFFFu : 0xFFu;
    const uint32_t begin = raw ? length : opening + 1;
    const uint32_t end = length > begin && bytes[length - 1] == quote_kind ? length - 1 : length;
    uint32_t count = 0;
    uint32_t at = begin;
    while (at < end)
    {
        const uint8_t* backslash = memchr(bytes + at, '\\', end - at);
        at = backslash != NULL ? check_literal_evaluated_escape(bytes, (uint32_t)(backslash - bytes), end, c23, pedantic, maximum, output, &count, had_error) : end;
    }
    return count;
}

#endif
