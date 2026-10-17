// Single-file amalgamation of the C lexer (preprocessing tokenizer) from Accelerated-Zig-Parser:
//   https://github.com/davidgmbb/Accelerated-Zig-Parser (c/c_tokenizer.h, c/c_tokenizer_tables.h,
//   c/c_tokenizer_unicode.h, c/c_tokenizer_scalar.c, c/c_tokenizer.c at commit f4482e2dd53413522d75d8c254b9a824021238ce).
// The sections below are the original files in that order, with their local #includes removed and
// c_tokenize_scalar_range made static. The tables are generated upstream by c/gen_c_tokenizer_tables.py and
// c/gen_c_tokenizer_unicode.py; regenerate there rather than editing them here.
//
// Upstream license (MIT):
//
// MIT License
//
// Copyright (c) 2023 Niles Salter
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// The odd-backslash matching in the vectorized lexer follows simdjson's algorithm (John Keiser); upstream's
// LICENSE releases pieces derived from simdjson under the Apache License, Version 2.0.

// ===== c_tokenizer.h =====
// A compacting AVX-512 tokenizer for C and C++ source code that hasn't been preprocessed yet: it produces the
// preprocessing tokens of translation phase 3 (after line splicing and, optionally, trigraph replacement), for a
// preprocessor to run on afterwards. It is the 512-byte-chunk design of src/CompactingTokenizer.zig applied to C.
//
// Every kind of source is tokenized exactly like clang's raw lexer in C23 mode (`clang -cc1 -std=c23
// -dump-raw-tokens`), or in C++26 mode with `C_TOKENIZER_CPLUSPLUS` (`clang -cc1 -x c++ -std=c++26
// -dump-raw-tokens`): line splices anywhere, digraphs, UCNs and UTF-8 identifiers, digit separators, unterminated
// literals and stray characters, and in C++ raw string literals, user-defined literal suffixes (which are part of
// the literal's token), `<=>`, `->*`, `.*` and the `<::` rule. Common code takes the vectorized path; the rare
// lines it can't handle exactly (e.g. a line splice in the middle of a token, a non-ASCII identifier or a raw string
// literal) are tokenized by a scalar lexer.
//
// Differences from the raw lexer's output, by design:
//   - Whitespace and comments aren't tokens. An unterminated block comment is a `C_TOKEN_UNKNOWN` token, as in clang.
//   - A token's start is its first character after any line splices (clang starts it at the first splice).
//   - Digraphs have the kind of the punctuator they stand for (`<:` is `C_TOKEN_L_SQUARE`), like clang.
//   - Header names aren't recognized: after `#include`, re-lex the `<...>` form from the source starting at the `<`.
//   - A named universal character name `\N{...}` is accepted in an identifier if the name is written in capital
//     letters, digits, spaces and hyphens; clang also checks that the name exists.
//
// Requires Clang and AVX-512 F/BW/VL/VBMI/VBMI2/VPOPCNTDQ plus BMI2 (e.g. -march=znver4, znver5 or tigerlake).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The kinds of preprocessing tokens. Each name is the clang token kind it matches (e.g. `C_TOKEN_STRING_LITERAL` is
// clang's `string_literal`). The prefixed character constants and string literals follow their unprefixed kind in
// the order L, u8, u, U (a raw string literal has the kind of its prefix without the R). The last three are C++'s.
#define C_TOKEN_KINDS(X) \
    X(UNKNOWN, unknown) \
    X(RAW_IDENTIFIER, raw_identifier) \
    X(NUMERIC_CONSTANT, numeric_constant) \
    X(CHAR_CONSTANT, char_constant) \
    X(WIDE_CHAR_CONSTANT, wide_char_constant) \
    X(UTF8_CHAR_CONSTANT, utf8_char_constant) \
    X(UTF16_CHAR_CONSTANT, utf16_char_constant) \
    X(UTF32_CHAR_CONSTANT, utf32_char_constant) \
    X(STRING_LITERAL, string_literal) \
    X(WIDE_STRING_LITERAL, wide_string_literal) \
    X(UTF8_STRING_LITERAL, utf8_string_literal) \
    X(UTF16_STRING_LITERAL, utf16_string_literal) \
    X(UTF32_STRING_LITERAL, utf32_string_literal) \
    X(L_SQUARE, l_square) \
    X(R_SQUARE, r_square) \
    X(L_PAREN, l_paren) \
    X(R_PAREN, r_paren) \
    X(L_BRACE, l_brace) \
    X(R_BRACE, r_brace) \
    X(PERIOD, period) \
    X(ELLIPSIS, ellipsis) \
    X(AMP, amp) \
    X(AMPAMP, ampamp) \
    X(AMPEQUAL, ampequal) \
    X(STAR, star) \
    X(STAREQUAL, starequal) \
    X(PLUS, plus) \
    X(PLUSPLUS, plusplus) \
    X(PLUSEQUAL, plusequal) \
    X(MINUS, minus) \
    X(ARROW, arrow) \
    X(MINUSMINUS, minusminus) \
    X(MINUSEQUAL, minusequal) \
    X(TILDE, tilde) \
    X(EXCLAIM, exclaim) \
    X(EXCLAIMEQUAL, exclaimequal) \
    X(SLASH, slash) \
    X(SLASHEQUAL, slashequal) \
    X(PERCENT, percent) \
    X(PERCENTEQUAL, percentequal) \
    X(LESS, less) \
    X(LESSLESS, lessless) \
    X(LESSEQUAL, lessequal) \
    X(LESSLESSEQUAL, lesslessequal) \
    X(GREATER, greater) \
    X(GREATERGREATER, greatergreater) \
    X(GREATEREQUAL, greaterequal) \
    X(GREATERGREATEREQUAL, greatergreaterequal) \
    X(CARET, caret) \
    X(CARETEQUAL, caretequal) \
    X(PIPE, pipe) \
    X(PIPEPIPE, pipepipe) \
    X(PIPEEQUAL, pipeequal) \
    X(QUESTION, question) \
    X(COLON, colon) \
    X(COLONCOLON, coloncolon) \
    X(SEMI, semi) \
    X(EQUAL, equal) \
    X(EQUALEQUAL, equalequal) \
    X(COMMA, comma) \
    X(HASH, hash) \
    X(HASHHASH, hashhash) \
    X(PERIODSTAR, periodstar) \
    X(ARROWSTAR, arrowstar) \
    X(SPACESHIP, spaceship)

typedef enum CTokenKind
{
#define C_TOKEN_ENUM(name, clang_name) C_TOKEN_##name,
    C_TOKEN_KINDS(C_TOKEN_ENUM)
#undef C_TOKEN_ENUM
    C_TOKEN_KIND_COUNT,
} CTokenKind;

// Set in a token's kind byte if it's the first token of a logical source line (comments don't count as tokens), so a
// `#` with this bit starts a preprocessing directive, which ends before the next token with this bit.
#define C_TOKEN_START_OF_LINE 0x80

typedef enum CTokenizerFlags
{
    // Replace trigraphs (`??=` is `#`, ...) in translation phase 1, as C17 and C++14 and earlier require. Off by
    // default, like C23, C++17 and clang's GNU modes.
    C_TOKENIZER_TRIGRAPHS = 1,
    // Tokenize C++ (C++26) instead of C.
    C_TOKENIZER_CPLUSPLUS = 2,
} CTokenizerFlags;

typedef enum CTokenizerError
{
    C_TOKENIZER_ERROR_FILE_TOO_BIG = -1, // Token positions are 32-bit.
    C_TOKENIZER_ERROR_NEEDS_SCALAR = -2, // Only from c_tokenize_vectorized.
} CTokenizerError;

// The number of entries `kinds`, `starts` and `ends` must each have room for when tokenizing `length` bytes.
#define C_TOKENIZER_CAPACITY(length) ((length) + 128)

// Tokenizes `length` bytes of C source. Token `i` is `source[starts[i] .. ends[i]]`, of kind `kinds[i] &
// ~C_TOKEN_START_OF_LINE` (a `CTokenKind`). Returns the number of tokens, or a negative `CTokenizerError`. `source`
// needs no padding or terminator, and `kinds`, `starts` and `ends` need `C_TOKENIZER_CAPACITY(length)` entries.
int64_t c_tokenize(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// c_tokenize without the scalar lexer: returns C_TOKENIZER_ERROR_NEEDS_SCALAR for the rare inputs that need it for a
// part (see c_tokenizer.c), with the output arrays clobbered. For testing and benchmarking.
int64_t c_tokenize_vectorized(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// The same tokenization by a straightforward scalar lexer (which `c_tokenize` uses for the rare parts of a source
// that its vectorized path doesn't handle). It writes exactly the returned number of tokens.
int64_t c_tokenize_scalar(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// The clang name of a token kind (without the start-of-line bit), e.g. "raw_identifier" or "plusequal".
const char* c_token_kind_name(uint8_t kind);

#ifdef __cplusplus
}
#endif

#if !defined(__clang__)
#error "c_lexer.c uses Clang vector extensions"
#endif
#if !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512VBMI__) && \
      defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) && defined(__BMI2__))
#error "c_lexer.c needs AVX-512 F/BW/VL/VBMI/VBMI2/VPOPCNTDQ and BMI2 (e.g. -march=znver4, znver5 or tigerlake)"
#endif

#include <immintrin.h>
#include <stdbool.h>
#include <string.h>

// ===== c_tokenizer_tables.h =====
// Generated by c/gen_c_tokenizer_tables.py: the lookup tables of the vectorized C tokenizer.

#include <stdint.h>

#define CHAR_IDENTIFIER 0x01
#define CHAR_DIGIT 0x02
#define CHAR_WHITESPACE 0x04
#define CHAR_NEWLINE 0x08
#define CHAR_SEPARATOR_FOLLOWER 0x10

#define CHANNEL_EQ 0x01
#define CHANNEL_SELF 0x02
#define CHANNEL_SHIFT 0x04
#define CHANNEL_DOTS 0x08
#define CHANNEL_GT 0x10
#define CHANNEL_LTDIG 0x20
#define CHANNEL_HASHDIG 0x40
#define CHANNEL_EXPSIGN 0x80
#define OP_HASH_MULTIPLIER 0xAE3B2443u

// Indexed by ASCII character.
static const uint8_t c_char_properties[128] __attribute__((aligned(64))) =
{
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x04, 0x04, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x11,
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t c_operator_classes[128] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x21, 0x00, 0x0A, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x05, 0x0C, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x07, 0x09, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
};
static const uint8_t c_char_kinds[128] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x22, 0x08, 0x3C, 0x01, 0x26, 0x15, 0x03, 0x0F, 0x10, 0x18, 0x1A, 0x3B, 0x1D, 0x13, 0x24,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x36, 0x38, 0x28, 0x39, 0x2C, 0x35,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0D, 0x00, 0x0E, 0x30, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x11, 0x32, 0x12, 0x21, 0x00,
};

// Indexed by the low 4 bits of an operator class.
static const uint8_t c_first_char_channels[16] __attribute__((aligned(64))) =
{
    0x00, 0x01, 0x01, 0x03, 0x03, 0x13, 0x51, 0x25, 0x05, 0x03, 0x02, 0x12, 0x08, 0x80, 0x00, 0x00,
};
static const uint8_t c_second_char_channels[16] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x02, 0x82, 0x82, 0x20, 0x04, 0x14, 0x03, 0x02, 0x62, 0x08, 0x00, 0x00, 0x00,
};
static const uint8_t c_third_char_channels[16] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
};
// In C++.
static const uint8_t c_third_char_channels_cxx[16] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
};

// Indexed by the hash of a multi-character punctuator.
static const uint8_t c_op_hash_kinds[64] __attribute__((aligned(64))) =
{
    0x3E, 0x1F, 0x33, 0x00, 0x31, 0x25, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x1B, 0x00, 0x00, 0x0D,
    0x00, 0x27, 0x2E, 0x00, 0x00, 0x2B, 0x1C, 0x00, 0x11, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x34,
    0x12, 0x2D, 0x00, 0x23, 0x00, 0x3C, 0x3A, 0x00, 0x00, 0x16, 0x00, 0x19, 0x29, 0x40, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x0E, 0x3D, 0x00, 0x00, 0x37, 0x00, 0x3F, 0x3D, 0x2A, 0x1E, 0x17, 0x00, 0x00,
};

// ===== c_tokenizer_unicode.h =====
// Generated by c/gen_c_tokenizer_unicode.py from clang version 23.1.3.
// The non-ASCII code points clang's C23 lexer accepts at the start and in the rest of an identifier, as sorted ranges.

#include <stdint.h>

static const uint32_t c_identifier_start_ranges[695][2] =
{
    {0xAA, 0xAA}, {0xB5, 0xB5}, {0xBA, 0xBA}, {0xC0, 0xD6}, {0xD8, 0xF6}, {0xF8, 0x2C1},
    {0x2C6, 0x2D1}, {0x2E0, 0x2E4}, {0x2EC, 0x2EC}, {0x2EE, 0x2EE}, {0x370, 0x374}, {0x376, 0x377},
    {0x37B, 0x37D}, {0x37F, 0x37F}, {0x386, 0x386}, {0x388, 0x38A}, {0x38C, 0x38C}, {0x38E, 0x3A1},
    {0x3A3, 0x3F5}, {0x3F7, 0x481}, {0x48A, 0x52F}, {0x531, 0x556}, {0x558, 0x559}, {0x560, 0x588},
    {0x58B, 0x58C}, {0x5D0, 0x5EA}, {0x5EF, 0x5F2}, {0x620, 0x64A}, {0x66E, 0x66F}, {0x671, 0x6D3},
    {0x6D5, 0x6D5}, {0x6E5, 0x6E6}, {0x6EE, 0x6EF}, {0x6FA, 0x6FC}, {0x6FF, 0x6FF}, {0x710, 0x710},
    {0x712, 0x72F}, {0x74D, 0x7A5}, {0x7B1, 0x7B1}, {0x7CA, 0x7EA}, {0x7F4, 0x7F5}, {0x7FA, 0x7FA},
    {0x800, 0x815}, {0x81A, 0x81A}, {0x824, 0x824}, {0x828, 0x828}, {0x840, 0x858}, {0x860, 0x86A},
    {0x870, 0x887}, {0x889, 0x88F}, {0x8A0, 0x8C9}, {0x904, 0x939}, {0x93D, 0x93D}, {0x950, 0x950},
    {0x958, 0x961}, {0x971, 0x980}, {0x985, 0x98C}, {0x98F, 0x990}, {0x993, 0x9A8}, {0x9AA, 0x9B0},
    {0x9B2, 0x9B2}, {0x9B6, 0x9B9}, {0x9BD, 0x9BD}, {0x9CE, 0x9CE}, {0x9DC, 0x9DD}, {0x9DF, 0x9E1},
    {0x9F0, 0x9F1}, {0x9FC, 0x9FC}, {0xA05, 0xA0A}, {0xA0F, 0xA10}, {0xA13, 0xA28}, {0xA2A, 0xA30},
    {0xA32, 0xA33}, {0xA35, 0xA36}, {0xA38, 0xA39}, {0xA59, 0xA5C}, {0xA5E, 0xA5E}, {0xA72, 0xA74},
    {0xA85, 0xA8D}, {0xA8F, 0xA91}, {0xA93, 0xAA8}, {0xAAA, 0xAB0}, {0xAB2, 0xAB3}, {0xAB5, 0xAB9},
    {0xABD, 0xABD}, {0xAD0, 0xAD0}, {0xAE0, 0xAE1}, {0xAF9, 0xAF9}, {0xB05, 0xB0C}, {0xB0F, 0xB10},
    {0xB13, 0xB28}, {0xB2A, 0xB30}, {0xB32, 0xB33}, {0xB35, 0xB39}, {0xB3D, 0xB3D}, {0xB5C, 0xB5D},
    {0xB5F, 0xB61}, {0xB71, 0xB71}, {0xB83, 0xB83}, {0xB85, 0xB8A}, {0xB8E, 0xB90}, {0xB92, 0xB95},
    {0xB99, 0xB9A}, {0xB9C, 0xB9C}, {0xB9E, 0xB9F}, {0xBA3, 0xBA4}, {0xBA8, 0xBAA}, {0xBAE, 0xBB9},
    {0xBD0, 0xBD0}, {0xC05, 0xC0C}, {0xC0E, 0xC10}, {0xC12, 0xC28}, {0xC2A, 0xC39}, {0xC3D, 0xC3D},
    {0xC58, 0xC5A}, {0xC5C, 0xC5D}, {0xC60, 0xC61}, {0xC80, 0xC80}, {0xC85, 0xC8C}, {0xC8E, 0xC90},
    {0xC92, 0xCA8}, {0xCAA, 0xCB3}, {0xCB5, 0xCB9}, {0xCBD, 0xCBD}, {0xCDC, 0xCDE}, {0xCE0, 0xCE1},
    {0xCF1, 0xCF2}, {0xD04, 0xD0C}, {0xD0E, 0xD10}, {0xD12, 0xD3A}, {0xD3D, 0xD3D}, {0xD4E, 0xD4E},
    {0xD54, 0xD56}, {0xD5F, 0xD61}, {0xD7A, 0xD7F}, {0xD85, 0xD96}, {0xD9A, 0xDB1}, {0xDB3, 0xDBB},
    {0xDBD, 0xDBD}, {0xDC0, 0xDC6}, {0xE01, 0xE30}, {0xE32, 0xE32}, {0xE40, 0xE46}, {0xE81, 0xE82},
    {0xE84, 0xE84}, {0xE86, 0xE8A}, {0xE8C, 0xEA3}, {0xEA5, 0xEA5}, {0xEA7, 0xEB0}, {0xEB2, 0xEB2},
    {0xEBD, 0xEBD}, {0xEC0, 0xEC4}, {0xEC6, 0xEC6}, {0xEDC, 0xEDF}, {0xF00, 0xF00}, {0xF40, 0xF47},
    {0xF49, 0xF6C}, {0xF88, 0xF8C}, {0x1000, 0x102A}, {0x103F, 0x103F}, {0x1050, 0x1055}, {0x105A, 0x105D},
    {0x1061, 0x1061}, {0x1065, 0x1066}, {0x106E, 0x1070}, {0x1075, 0x1081}, {0x108E, 0x108E}, {0x10A0, 0x10C5},
    {0x10C7, 0x10C7}, {0x10CD, 0x10CD}, {0x10D0, 0x10FA}, {0x10FC, 0x1248}, {0x124A, 0x124D}, {0x1250, 0x1256},
    {0x1258, 0x1258}, {0x125A, 0x125D}, {0x1260, 0x1288}, {0x128A, 0x128D}, {0x1290, 0x12B0}, {0x12B2, 0x12B5},
    {0x12B8, 0x12BE}, {0x12C0, 0x12C0}, {0x12C2, 0x12C5}, {0x12C8, 0x12D6}, {0x12D8, 0x1310}, {0x1312, 0x1315},
    {0x1318, 0x135A}, {0x1380, 0x138F}, {0x13A0, 0x13F5}, {0x13F8, 0x13FD}, {0x1401, 0x166C}, {0x166F, 0x167F},
    {0x1681, 0x169A}, {0x16A0, 0x16EA}, {0x16EE, 0x16F8}, {0x1700, 0x1711}, {0x171F, 0x1731}, {0x1740, 0x1751},
    {0x1760, 0x176C}, {0x176E, 0x1770}, {0x1780, 0x17B3}, {0x17D7, 0x17D7}, {0x17DC, 0x17DC}, {0x1820, 0x1878},
    {0x1880, 0x18A8}, {0x18AA, 0x18AA}, {0x18B0, 0x18F5}, {0x1900, 0x191E}, {0x1950, 0x196D}, {0x1970, 0x1974},
    {0x1980, 0x19AB}, {0x19B0, 0x19C9}, {0x1A00, 0x1A16}, {0x1A20, 0x1A54}, {0x1AA7, 0x1AA7}, {0x1B05, 0x1B33},
    {0x1B45, 0x1B4C}, {0x1B83, 0x1BA0}, {0x1BAE, 0x1BAF}, {0x1BBA, 0x1BE5}, {0x1C00, 0x1C23}, {0x1C4D, 0x1C4F},
    {0x1C5A, 0x1C7D}, {0x1C80, 0x1C8A}, {0x1C90, 0x1CBA}, {0x1CBD, 0x1CBF}, {0x1CE9, 0x1CEC}, {0x1CEE, 0x1CF3},
    {0x1CF5, 0x1CF6}, {0x1CFA, 0x1CFA}, {0x1D00, 0x1DBF}, {0x1E00, 0x1F15}, {0x1F18, 0x1F1D}, {0x1F20, 0x1F45},
    {0x1F48, 0x1F4D}, {0x1F50, 0x1F57}, {0x1F59, 0x1F59}, {0x1F5B, 0x1F5B}, {0x1F5D, 0x1F5D}, {0x1F5F, 0x1F7D},
    {0x1F80, 0x1FB4}, {0x1FB6, 0x1FBC}, {0x1FBE, 0x1FBE}, {0x1FC2, 0x1FC4}, {0x1FC6, 0x1FCC}, {0x1FD0, 0x1FD3},
    {0x1FD6, 0x1FDB}, {0x1FE0, 0x1FEC}, {0x1FF2, 0x1FF4}, {0x1FF6, 0x1FFC}, {0x2071, 0x2071}, {0x207F, 0x207F},
    {0x208F, 0x209F}, {0x2102, 0x2102}, {0x2107, 0x2107}, {0x210A, 0x2113}, {0x2115, 0x2115}, {0x2118, 0x211D},
    {0x2124, 0x2124}, {0x2126, 0x2126}, {0x2128, 0x2128}, {0x212A, 0x2139}, {0x213C, 0x213F}, {0x2145, 0x2149},
    {0x214E, 0x214E}, {0x2160, 0x2188}, {0x2202, 0x2202}, {0x2207, 0x2207}, {0x221E, 0x221E}, {0x2C00, 0x2CE4},
    {0x2CEB, 0x2CEE}, {0x2CF2, 0x2CF3}, {0x2D00, 0x2D25}, {0x2D27, 0x2D27}, {0x2D2D, 0x2D2D}, {0x2D30, 0x2D67},
    {0x2D6F, 0x2D6F}, {0x2D80, 0x2D96}, {0x2DA0, 0x2DA6}, {0x2DA8, 0x2DAE}, {0x2DB0, 0x2DB6}, {0x2DB8, 0x2DBE},
    {0x2DC0, 0x2DC6}, {0x2DC8, 0x2DCE}, {0x2DD0, 0x2DD6}, {0x2DD8, 0x2DDE}, {0x3005, 0x3007}, {0x3021, 0x3029},
    {0x3031, 0x3035}, {0x3038, 0x303C}, {0x3041, 0x3096}, {0x309D, 0x309F}, {0x30A1, 0x30FA}, {0x30FC, 0x30FF},
    {0x3105, 0x312F}, {0x3131, 0x318E}, {0x31A0, 0x31BF}, {0x31F0, 0x31FF}, {0x3400, 0x4DBF}, {0x4E00, 0xA48C},
    {0xA4D0, 0xA4FD}, {0xA500, 0xA60C}, {0xA610, 0xA61F}, {0xA62A, 0xA62B}, {0xA640, 0xA66E}, {0xA67F, 0xA69D},
    {0xA6A0, 0xA6EF}, {0xA717, 0xA71F}, {0xA722, 0xA788}, {0xA78B, 0xA7DD}, {0xA7E2, 0xA7E2}, {0xA7F1, 0xA801},
    {0xA803, 0xA805}, {0xA807, 0xA80A}, {0xA80C, 0xA822}, {0xA840, 0xA873}, {0xA882, 0xA8B3}, {0xA8F2, 0xA8F7},
    {0xA8FB, 0xA8FB}, {0xA8FD, 0xA8FE}, {0xA90A, 0xA925}, {0xA930, 0xA946}, {0xA960, 0xA97C}, {0xA984, 0xA9B2},
    {0xA9CF, 0xA9CF}, {0xA9E0, 0xA9E4}, {0xA9E6, 0xA9EF}, {0xA9FA, 0xA9FE}, {0xAA00, 0xAA28}, {0xAA40, 0xAA42},
    {0xAA44, 0xAA4B}, {0xAA60, 0xAA76}, {0xAA7A, 0xAA7A}, {0xAA7E, 0xAAAF}, {0xAAB1, 0xAAB1}, {0xAAB5, 0xAAB6},
    {0xAAB9, 0xAABD}, {0xAAC0, 0xAAC0}, {0xAAC2, 0xAAC2}, {0xAADB, 0xAADD}, {0xAAE0, 0xAAEA}, {0xAAF2, 0xAAF4},
    {0xAB01, 0xAB06}, {0xAB09, 0xAB0E}, {0xAB11, 0xAB16}, {0xAB20, 0xAB26}, {0xAB28, 0xAB2E}, {0xAB30, 0xAB5A},
    {0xAB5C, 0xAB69}, {0xAB6C, 0xAB6D}, {0xAB70, 0xABE2}, {0xAC00, 0xD7A3}, {0xD7B0, 0xD7C6}, {0xD7CB, 0xD7FB},
    {0xF900, 0xFA6D}, {0xFA70, 0xFAD9}, {0xFB00, 0xFB06}, {0xFB13, 0xFB17}, {0xFB1D, 0xFB1D}, {0xFB1F, 0xFB28},
    {0xFB2A, 0xFB36}, {0xFB38, 0xFB3C}, {0xFB3E, 0xFB3E}, {0xFB40, 0xFB41}, {0xFB43, 0xFB44}, {0xFB46, 0xFBB1},
    {0xFBD3, 0xFC5D}, {0xFC64, 0xFD3D}, {0xFD50, 0xFD8F}, {0xFD92, 0xFDC7}, {0xFDF0, 0xFDF9}, {0xFE71, 0xFE71},
    {0xFE73, 0xFE73}, {0xFE77, 0xFE77}, {0xFE79, 0xFE79}, {0xFE7B, 0xFE7B}, {0xFE7D, 0xFE7D}, {0xFE7F, 0xFEFC},
    {0xFF21, 0xFF3A}, {0xFF41, 0xFF5A}, {0xFF66, 0xFF9D}, {0xFFA0, 0xFFBE}, {0xFFC2, 0xFFC7}, {0xFFCA, 0xFFCF},
    {0xFFD2, 0xFFD7}, {0xFFDA, 0xFFDC}, {0x10000, 0x1000B}, {0x1000D, 0x10026}, {0x10028, 0x1003A}, {0x1003C, 0x1003D},
    {0x1003F, 0x1004D}, {0x10050, 0x1005D}, {0x10080, 0x100FA}, {0x10140, 0x10174}, {0x10280, 0x1029C}, {0x102A0, 0x102D0},
    {0x10300, 0x1031F}, {0x1032D, 0x1034A}, {0x10350, 0x10375}, {0x10380, 0x1039D}, {0x103A0, 0x103C3}, {0x103C8, 0x103CF},
    {0x103D1, 0x103D5}, {0x10400, 0x1049D}, {0x104B0, 0x104D3}, {0x104D8, 0x104FB}, {0x10500, 0x10527}, {0x10530, 0x10563},
    {0x10570, 0x1057A}, {0x1057C, 0x1058A}, {0x1058C, 0x10592}, {0x10594, 0x10595}, {0x10597, 0x105A1}, {0x105A3, 0x105B1},
    {0x105B3, 0x105B9}, {0x105BB, 0x105BC}, {0x105C0, 0x105F3}, {0x10600, 0x10736}, {0x10740, 0x10755}, {0x10760, 0x10767},
    {0x10780, 0x10785}, {0x10787, 0x107B0}, {0x107B2, 0x107BF}, {0x10800, 0x10805}, {0x10808, 0x10808}, {0x1080A, 0x10835},
    {0x10837, 0x10838}, {0x1083C, 0x1083C}, {0x1083F, 0x10855}, {0x10860, 0x10876}, {0x10880, 0x1089E}, {0x108E0, 0x108F2},
    {0x108F4, 0x108F5}, {0x10900, 0x10915}, {0x10920, 0x10939}, {0x10940, 0x10959}, {0x10980, 0x109B7}, {0x109BE, 0x109BF},
    {0x10A00, 0x10A00}, {0x10A10, 0x10A13}, {0x10A15, 0x10A17}, {0x10A19, 0x10A35}, {0x10A60, 0x10A7C}, {0x10A80, 0x10A9C},
    {0x10AC0, 0x10AC7}, {0x10AC9, 0x10AE4}, {0x10B00, 0x10B35}, {0x10B40, 0x10B55}, {0x10B60, 0x10B72}, {0x10B80, 0x10B91},
    {0x10C00, 0x10C48}, {0x10C80, 0x10CB2}, {0x10CC0, 0x10CF2}, {0x10D00, 0x10D23}, {0x10D4A, 0x10D65}, {0x10D6F, 0x10D85},
    {0x10E80, 0x10EA9}, {0x10EB0, 0x10EB1}, {0x10EC2, 0x10EC7}, {0x10ED9, 0x10EEE}, {0x10F00, 0x10F1C}, {0x10F27, 0x10F27},
    {0x10F30, 0x10F45}, {0x10F70, 0x10F81}, {0x10FB0, 0x10FC4}, {0x10FE0, 0x10FF6}, {0x11003, 0x11037}, {0x11071, 0x11072},
    {0x11075, 0x11075}, {0x11083, 0x110AF}, {0x110D0, 0x110E8}, {0x11103, 0x11126}, {0x11144, 0x11144}, {0x11147, 0x11147},
    {0x11150, 0x11172}, {0x11176, 0x11176}, {0x11183, 0x111B2}, {0x111C1, 0x111C4}, {0x111DA, 0x111DA}, {0x111DC, 0x111DC},
    {0x11200, 0x11211}, {0x11213, 0x1122B}, {0x1123F, 0x11240}, {0x11280, 0x11286}, {0x11288, 0x11288}, {0x1128A, 0x1128D},
    {0x1128F, 0x1129D}, {0x1129F, 0x112A8}, {0x112B0, 0x112DE}, {0x11305, 0x1130C}, {0x1130F, 0x11310}, {0x11313, 0x11328},
    {0x1132A, 0x11330}, {0x11332, 0x11333}, {0x11335, 0x11339}, {0x1133D, 0x1133D}, {0x11350, 0x11350}, {0x1135D, 0x11361},
    {0x11380, 0x11389}, {0x1138B, 0x1138B}, {0x1138E, 0x1138E}, {0x11390, 0x113B5}, {0x113B7, 0x113B7}, {0x113D1, 0x113D1},
    {0x113D3, 0x113D3}, {0x11400, 0x11434}, {0x11447, 0x1144A}, {0x1145F, 0x11461}, {0x11480, 0x114AF}, {0x114C4, 0x114C5},
    {0x114C7, 0x114C7}, {0x11580, 0x115AE}, {0x115D8, 0x115DB}, {0x11600, 0x1162F}, {0x11644, 0x11644}, {0x11680, 0x116AA},
    {0x116B8, 0x116B8}, {0x11700, 0x1171A}, {0x11740, 0x11746}, {0x11800, 0x1182B}, {0x118A0, 0x118DF}, {0x118FF, 0x11906},
    {0x11909, 0x11909}, {0x1190C, 0x11913}, {0x11915, 0x11916}, {0x11918, 0x1192F}, {0x1193F, 0x1193F}, {0x11941, 0x11941},
    {0x119A0, 0x119A7}, {0x119AA, 0x119D0}, {0x119E1, 0x119E1}, {0x119E3, 0x119E3}, {0x11A00, 0x11A00}, {0x11A0B, 0x11A32},
    {0x11A3A, 0x11A3A}, {0x11A50, 0x11A50}, {0x11A5C, 0x11A89}, {0x11A9D, 0x11A9D}, {0x11AB0, 0x11AF8}, {0x11B0A, 0x11B0A},
    {0x11BC0, 0x11BE0}, {0x11C00, 0x11C08}, {0x11C0A, 0x11C2E}, {0x11C40, 0x11C40}, {0x11C72, 0x11C8F}, {0x11D00, 0x11D06},
    {0x11D08, 0x11D09}, {0x11D0B, 0x11D30}, {0x11D46, 0x11D46}, {0x11D60, 0x11D65}, {0x11D67, 0x11D68}, {0x11D6A, 0x11D89},
    {0x11D98, 0x11D98}, {0x11DB0, 0x11DDB}, {0x11DF1, 0x11DF1}, {0x11EE0, 0x11EF2}, {0x11F02, 0x11F02}, {0x11F04, 0x11F10},
    {0x11F12, 0x11F33}, {0x11FB0, 0x11FB0}, {0x12000, 0x12399}, {0x12400, 0x1246F}, {0x12475, 0x12543}, {0x12550, 0x12686},
    {0x12F90, 0x12FF0}, {0x13000, 0x1342F}, {0x13441, 0x13446}, {0x13460, 0x143FA}, {0x14400, 0x14646}, {0x16100, 0x1611D},
    {0x16800, 0x16A38}, {0x16A40, 0x16A5E}, {0x16A70, 0x16ABE}, {0x16AD0, 0x16AED}, {0x16B00, 0x16B2F}, {0x16B40, 0x16B43},
    {0x16B63, 0x16B77}, {0x16B7D, 0x16B8F}, {0x16D40, 0x16D6C}, {0x16D80, 0x16D97}, {0x16D99, 0x16D9C}, {0x16E40, 0x16E7F},
    {0x16EA0, 0x16EB8}, {0x16EBB, 0x16ED3}, {0x16F00, 0x16F4A}, {0x16F50, 0x16F50}, {0x16F93, 0x16F9F}, {0x16FE0, 0x16FE1},
    {0x16FE3, 0x16FE3}, {0x16FF2, 0x16FF6}, {0x17000, 0x18CDA}, {0x18CFF, 0x18D20}, {0x18D80, 0x18DF2}, {0x18E00, 0x19191},
    {0x191A0, 0x191D2}, {0x1AFF0, 0x1AFF3}, {0x1AFF5, 0x1AFFB}, {0x1AFFD, 0x1AFFE}, {0x1B000, 0x1B128}, {0x1B132, 0x1B132},
    {0x1B150, 0x1B152}, {0x1B155, 0x1B155}, {0x1B164, 0x1B168}, {0x1B170, 0x1B2FB}, {0x1BC00, 0x1BC6A}, {0x1BC70, 0x1BC7C},
    {0x1BC80, 0x1BC88}, {0x1BC90, 0x1BC99}, {0x1D400, 0x1D454}, {0x1D456, 0x1D49C}, {0x1D49E, 0x1D49F}, {0x1D4A2, 0x1D4A2},
    {0x1D4A5, 0x1D4A6}, {0x1D4A9, 0x1D4AC}, {0x1D4AE, 0x1D4B9}, {0x1D4BB, 0x1D4BB}, {0x1D4BD, 0x1D4C3}, {0x1D4C5, 0x1D505},
    {0x1D507, 0x1D50A}, {0x1D50D, 0x1D514}, {0x1D516, 0x1D51C}, {0x1D51E, 0x1D539}, {0x1D53B, 0x1D53E}, {0x1D540, 0x1D544},
    {0x1D546, 0x1D546}, {0x1D54A, 0x1D550}, {0x1D552, 0x1D6A6}, {0x1D6A8, 0x1D7CB}, {0x1DF00, 0x1DF81}, {0x1DF90, 0x1DF96},
    {0x1DFCD, 0x1DFFF}, {0x1E030, 0x1E06D}, {0x1E100, 0x1E12C}, {0x1E137, 0x1E13D}, {0x1E14E, 0x1E14E}, {0x1E290, 0x1E2AD},
    {0x1E2C0, 0x1E2EB}, {0x1E4D0, 0x1E4EB}, {0x1E5D0, 0x1E5ED}, {0x1E5F0, 0x1E5F0}, {0x1E6C0, 0x1E6DE}, {0x1E6E0, 0x1E6E2},
    {0x1E6E4, 0x1E6E5}, {0x1E6E7, 0x1E6ED}, {0x1E6F0, 0x1E6F4}, {0x1E6FE, 0x1E6FF}, {0x1E7E0, 0x1E7E6}, {0x1E7E8, 0x1E7EB},
    {0x1E7ED, 0x1E7EE}, {0x1E7F0, 0x1E7FE}, {0x1E800, 0x1E8C4}, {0x1E900, 0x1E943}, {0x1E94B, 0x1E94B}, {0x1EE00, 0x1EE03},
    {0x1EE05, 0x1EE1F}, {0x1EE21, 0x1EE22}, {0x1EE24, 0x1EE24}, {0x1EE27, 0x1EE27}, {0x1EE29, 0x1EE32}, {0x1EE34, 0x1EE37},
    {0x1EE39, 0x1EE39}, {0x1EE3B, 0x1EE3B}, {0x1EE42, 0x1EE42}, {0x1EE47, 0x1EE47}, {0x1EE49, 0x1EE49}, {0x1EE4B, 0x1EE4B},
    {0x1EE4D, 0x1EE4F}, {0x1EE51, 0x1EE52}, {0x1EE54, 0x1EE54}, {0x1EE57, 0x1EE57}, {0x1EE59, 0x1EE59}, {0x1EE5B, 0x1EE5B},
    {0x1EE5D, 0x1EE5D}, {0x1EE5F, 0x1EE5F}, {0x1EE61, 0x1EE62}, {0x1EE64, 0x1EE64}, {0x1EE67, 0x1EE6A}, {0x1EE6C, 0x1EE72},
    {0x1EE74, 0x1EE77}, {0x1EE79, 0x1EE7C}, {0x1EE7E, 0x1EE7E}, {0x1EE80, 0x1EE89}, {0x1EE8B, 0x1EE9B}, {0x1EEA1, 0x1EEA3},
    {0x1EEA5, 0x1EEA9}, {0x1EEAB, 0x1EEBB}, {0x20000, 0x2A6DF}, {0x2A700, 0x2B81E}, {0x2B820, 0x2CEAD}, {0x2CEB0, 0x2EBE0},
    {0x2EBF0, 0x2EE5D}, {0x2F800, 0x2FA1D}, {0x30000, 0x3134A}, {0x31350, 0x33479}, {0x3D000, 0x3FC3F},
};

static const uint32_t c_identifier_continue_ranges[812][2] =
{
    {0xAA, 0xAA}, {0xB2, 0xB3}, {0xB5, 0xB5}, {0xB7, 0xB7}, {0xB9, 0xBA}, {0xC0, 0xD6},
    {0xD8, 0xF6}, {0xF8, 0x2C1}, {0x2C6, 0x2D1}, {0x2E0, 0x2E4}, {0x2EC, 0x2EC}, {0x2EE, 0x2EE},
    {0x300, 0x374}, {0x376, 0x377}, {0x37B, 0x37D}, {0x37F, 0x37F}, {0x386, 0x38A}, {0x38C, 0x38C},
    {0x38E, 0x3A1}, {0x3A3, 0x3F5}, {0x3F7, 0x481}, {0x483, 0x487}, {0x48A, 0x52F}, {0x531, 0x556},
    {0x558, 0x559}, {0x560, 0x588}, {0x58B, 0x58C}, {0x591, 0x5BD}, {0x5BF, 0x5BF}, {0x5C1, 0x5C2},
    {0x5C4, 0x5C5}, {0x5C7, 0x5C9}, {0x5D0, 0x5EA}, {0x5EF, 0x5F2}, {0x610, 0x61A}, {0x620, 0x669},
    {0x66E, 0x6D3}, {0x6D5, 0x6DC}, {0x6DF, 0x6E8}, {0x6EA, 0x6FC}, {0x6FF, 0x6FF}, {0x710, 0x74A},
    {0x74D, 0x7B1}, {0x7C0, 0x7F5}, {0x7FA, 0x7FA}, {0x7FD, 0x7FD}, {0x800, 0x82D}, {0x840, 0x85B},
    {0x860, 0x86A}, {0x870, 0x887}, {0x889, 0x88F}, {0x897, 0x8E1}, {0x8E3, 0x963}, {0x966, 0x96F},
    {0x971, 0x983}, {0x985, 0x98C}, {0x98F, 0x990}, {0x993, 0x9A8}, {0x9AA, 0x9B0}, {0x9B2, 0x9B2},
    {0x9B6, 0x9B9}, {0x9BC, 0x9C4}, {0x9C7, 0x9C8}, {0x9CB, 0x9CE}, {0x9D7, 0x9D7}, {0x9DC, 0x9DD},
    {0x9DF, 0x9E3}, {0x9E6, 0x9F1}, {0x9FC, 0x9FC}, {0x9FE, 0x9FE}, {0xA01, 0xA03}, {0xA05, 0xA0A},
    {0xA0F, 0xA10}, {0xA13, 0xA28}, {0xA2A, 0xA30}, {0xA32, 0xA33}, {0xA35, 0xA36}, {0xA38, 0xA39},
    {0xA3C, 0xA3C}, {0xA3E, 0xA42}, {0xA47, 0xA48}, {0xA4B, 0xA4D}, {0xA51, 0xA51}, {0xA59, 0xA5C},
    {0xA5E, 0xA5E}, {0xA66, 0xA75}, {0xA81, 0xA83}, {0xA85, 0xA8D}, {0xA8F, 0xA91}, {0xA93, 0xAA8},
    {0xAAA, 0xAB0}, {0xAB2, 0xAB3}, {0xAB5, 0xAB9}, {0xABC, 0xAC5}, {0xAC7, 0xAC9}, {0xACB, 0xACD},
    {0xAD0, 0xAD0}, {0xAE0, 0xAE3}, {0xAE6, 0xAEF}, {0xAF9, 0xAFF}, {0xB01, 0xB03}, {0xB05, 0xB0C},
    {0xB0F, 0xB10}, {0xB13, 0xB28}, {0xB2A, 0xB30}, {0xB32, 0xB33}, {0xB35, 0xB39}, {0xB3C, 0xB44},
    {0xB47, 0xB48}, {0xB4B, 0xB4D}, {0xB53, 0xB57}, {0xB5C, 0xB5D}, {0xB5F, 0xB63}, {0xB66, 0xB6F},
    {0xB71, 0xB71}, {0xB82, 0xB83}, {0xB85, 0xB8A}, {0xB8E, 0xB90}, {0xB92, 0xB95}, {0xB99, 0xB9A},
    {0xB9C, 0xB9C}, {0xB9E, 0xB9F}, {0xBA3, 0xBA4}, {0xBA8, 0xBAA}, {0xBAE, 0xBB9}, {0xBBE, 0xBC2},
    {0xBC6, 0xBC8}, {0xBCA, 0xBCD}, {0xBD0, 0xBD0}, {0xBD7, 0xBD7}, {0xBE6, 0xBEF}, {0xC00, 0xC0C},
    {0xC0E, 0xC10}, {0xC12, 0xC28}, {0xC2A, 0xC39}, {0xC3C, 0xC44}, {0xC46, 0xC48}, {0xC4A, 0xC4D},
    {0xC55, 0xC56}, {0xC58, 0xC5A}, {0xC5C, 0xC5D}, {0xC60, 0xC63}, {0xC66, 0xC6F}, {0xC80, 0xC83},
    {0xC85, 0xC8C}, {0xC8E, 0xC90}, {0xC92, 0xCA8}, {0xCAA, 0xCB3}, {0xCB5, 0xCB9}, {0xCBC, 0xCC4},
    {0xCC6, 0xCC8}, {0xCCA, 0xCCD}, {0xCD5, 0xCD6}, {0xCDC, 0xCDE}, {0xCE0, 0xCE3}, {0xCE6, 0xCEF},
    {0xCF1, 0xCF3}, {0xD00, 0xD0C}, {0xD0E, 0xD10}, {0xD12, 0xD44}, {0xD46, 0xD48}, {0xD4A, 0xD4E},
    {0xD54, 0xD57}, {0xD5F, 0xD63}, {0xD66, 0xD6F}, {0xD7A, 0xD7F}, {0xD81, 0xD83}, {0xD85, 0xD96},
    {0xD9A, 0xDB1}, {0xDB3, 0xDBB}, {0xDBD, 0xDBD}, {0xDC0, 0xDC6}, {0xDCA, 0xDCA}, {0xDCF, 0xDD4},
    {0xDD6, 0xDD6}, {0xDD8, 0xDDF}, {0xDE6, 0xDEF}, {0xDF2, 0xDF3}, {0xE01, 0xE3A}, {0xE40, 0xE4E},
    {0xE50, 0xE59}, {0xE81, 0xE82}, {0xE84, 0xE84}, {0xE86, 0xE8A}, {0xE8C, 0xEA3}, {0xEA5, 0xEA5},
    {0xEA7, 0xEBD}, {0xEC0, 0xEC4}, {0xEC6, 0xEC6}, {0xEC8, 0xECE}, {0xED0, 0xED9}, {0xEDC, 0xEDF},
    {0xF00, 0xF00}, {0xF18, 0xF19}, {0xF20, 0xF29}, {0xF35, 0xF35}, {0xF37, 0xF37}, {0xF39, 0xF39},
    {0xF3E, 0xF47}, {0xF49, 0xF6C}, {0xF71, 0xF84}, {0xF86, 0xF97}, {0xF99, 0xFBC}, {0xFC6, 0xFC6},
    {0x1000, 0x1049}, {0x1050, 0x109D}, {0x10A0, 0x10C5}, {0x10C7, 0x10C7}, {0x10CD, 0x10CD}, {0x10D0, 0x10FA},
    {0x10FC, 0x1248}, {0x124A, 0x124D}, {0x1250, 0x1256}, {0x1258, 0x1258}, {0x125A, 0x125D}, {0x1260, 0x1288},
    {0x128A, 0x128D}, {0x1290, 0x12B0}, {0x12B2, 0x12B5}, {0x12B8, 0x12BE}, {0x12C0, 0x12C0}, {0x12C2, 0x12C5},
    {0x12C8, 0x12D6}, {0x12D8, 0x1310}, {0x1312, 0x1315}, {0x1318, 0x135A}, {0x135D, 0x135F}, {0x1369, 0x1371},
    {0x1380, 0x138F}, {0x13A0, 0x13F5}, {0x13F8, 0x13FD}, {0x1401, 0x166C}, {0x166F, 0x167F}, {0x1681, 0x169A},
    {0x16A0, 0x16EA}, {0x16EE, 0x16F8}, {0x1700, 0x1715}, {0x171F, 0x1734}, {0x1740, 0x1753}, {0x1760, 0x176C},
    {0x176E, 0x1770}, {0x1772, 0x1773}, {0x1780, 0x17D3}, {0x17D7, 0x17D7}, {0x17DC, 0x17DD}, {0x17E0, 0x17E9},
    {0x180B, 0x180D}, {0x180F, 0x1819}, {0x1820, 0x1878}, {0x1880, 0x18AA}, {0x18B0, 0x18F5}, {0x1900, 0x191E},
    {0x1920, 0x192B}, {0x1930, 0x193B}, {0x1946, 0x196D}, {0x1970, 0x1974}, {0x1980, 0x19AB}, {0x19B0, 0x19C9},
    {0x19D0, 0x19DA}, {0x1A00, 0x1A1B}, {0x1A20, 0x1A5E}, {0x1A60, 0x1A7C}, {0x1A7F, 0x1A89}, {0x1A90, 0x1A99},
    {0x1AA7, 0x1AA7}, {0x1AB0, 0x1ABD}, {0x1ABF, 0x1AF0}, {0x1B00, 0x1B4C}, {0x1B50, 0x1B59}, {0x1B6B, 0x1B73},
    {0x1B80, 0x1BF3}, {0x1C00, 0x1C37}, {0x1C40, 0x1C49}, {0x1C4D, 0x1C7D}, {0x1C80, 0x1C8A}, {0x1C90, 0x1CBA},
    {0x1CBD, 0x1CBF}, {0x1CD0, 0x1CD2}, {0x1CD4, 0x1CFA}, {0x1D00, 0x1F15}, {0x1F18, 0x1F1D}, {0x1F20, 0x1F45},
    {0x1F48, 0x1F4D}, {0x1F50, 0x1F57}, {0x1F59, 0x1F59}, {0x1F5B, 0x1F5B}, {0x1F5D, 0x1F5D}, {0x1F5F, 0x1F7D},
    {0x1F80, 0x1FB4}, {0x1FB6, 0x1FBC}, {0x1FBE, 0x1FBE}, {0x1FC2, 0x1FC4}, {0x1FC6, 0x1FCC}, {0x1FD0, 0x1FD3},
    {0x1FD6, 0x1FDB}, {0x1FE0, 0x1FEC}, {0x1FF2, 0x1FF4}, {0x1FF6, 0x1FFC}, {0x200C, 0x200D}, {0x203F, 0x2040},
    {0x2054, 0x2054}, {0x2070, 0x2071}, {0x2074, 0x209F}, {0x20D0, 0x20DC}, {0x20E1, 0x20E1}, {0x20E5, 0x20F0},
    {0x2102, 0x2102}, {0x2107, 0x2107}, {0x210A, 0x2113}, {0x2115, 0x2115}, {0x2118, 0x211D}, {0x2124, 0x2124},
    {0x2126, 0x2126}, {0x2128, 0x2128}, {0x212A, 0x2139}, {0x213C, 0x213F}, {0x2145, 0x2149}, {0x214E, 0x214E},
    {0x2160, 0x2188}, {0x2202, 0x2202}, {0x2207, 0x2207}, {0x221E, 0x221E}, {0x2C00, 0x2CE4}, {0x2CEB, 0x2CF3},
    {0x2D00, 0x2D25}, {0x2D27, 0x2D27}, {0x2D2D, 0x2D2D}, {0x2D30, 0x2D67}, {0x2D6F, 0x2D6F}, {0x2D7F, 0x2D96},
    {0x2DA0, 0x2DA6}, {0x2DA8, 0x2DAE}, {0x2DB0, 0x2DB6}, {0x2DB8, 0x2DBE}, {0x2DC0, 0x2DC6}, {0x2DC8, 0x2DCE},
    {0x2DD0, 0x2DD6}, {0x2DD8, 0x2DDE}, {0x2DE0, 0x2DFF}, {0x3005, 0x3007}, {0x3021, 0x302F}, {0x3031, 0x3035},
    {0x3038, 0x303C}, {0x3041, 0x3096}, {0x3099, 0x309A}, {0x309D, 0x309F}, {0x30A1, 0x30FF}, {0x3105, 0x312F},
    {0x3131, 0x318E}, {0x31A0, 0x31BF}, {0x31F0, 0x31FF}, {0x3400, 0x4DBF}, {0x4E00, 0xA48C}, {0xA4D0, 0xA4FD},
    {0xA500, 0xA60C}, {0xA610, 0xA62B}, {0xA640, 0xA66F}, {0xA674, 0xA67D}, {0xA67F, 0xA6F1}, {0xA717, 0xA71F},
    {0xA722, 0xA788}, {0xA78B, 0xA7DD}, {0xA7E2, 0xA7E2}, {0xA7F1, 0xA827}, {0xA82C, 0xA82C}, {0xA840, 0xA873},
    {0xA880, 0xA8C5}, {0xA8D0, 0xA8D9}, {0xA8E0, 0xA8F7}, {0xA8FB, 0xA8FB}, {0xA8FD, 0xA92D}, {0xA930, 0xA953},
    {0xA960, 0xA97C}, {0xA980, 0xA9C0}, {0xA9CF, 0xA9D9}, {0xA9E0, 0xA9FE}, {0xAA00, 0xAA36}, {0xAA40, 0xAA4D},
    {0xAA50, 0xAA59}, {0xAA60, 0xAA76}, {0xAA7A, 0xAAC2}, {0xAADB, 0xAADD}, {0xAAE0, 0xAAEF}, {0xAAF2, 0xAAF6},
    {0xAB01, 0xAB06}, {0xAB09, 0xAB0E}, {0xAB11, 0xAB16}, {0xAB20, 0xAB26}, {0xAB28, 0xAB2E}, {0xAB30, 0xAB5A},
    {0xAB5C, 0xAB69}, {0xAB6C, 0xAB6D}, {0xAB70, 0xABEA}, {0xABEC, 0xABED}, {0xABF0, 0xABF9}, {0xAC00, 0xD7A3},
    {0xD7B0, 0xD7C6}, {0xD7CB, 0xD7FB}, {0xF900, 0xFA6D}, {0xFA70, 0xFAD9}, {0xFB00, 0xFB06}, {0xFB13, 0xFB17},
    {0xFB1D, 0xFB28}, {0xFB2A, 0xFB36}, {0xFB38, 0xFB3C}, {0xFB3E, 0xFB3E}, {0xFB40, 0xFB41}, {0xFB43, 0xFB44},
    {0xFB46, 0xFBB1}, {0xFBD3, 0xFC5D}, {0xFC64, 0xFD3D}, {0xFD50, 0xFD8F}, {0xFD92, 0xFDC7}, {0xFDF0, 0xFDF9},
    {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFE33, 0xFE34}, {0xFE4D, 0xFE4F}, {0xFE71, 0xFE71}, {0xFE73, 0xFE73},
    {0xFE77, 0xFE77}, {0xFE79, 0xFE79}, {0xFE7B, 0xFE7B}, {0xFE7D, 0xFE7D}, {0xFE7F, 0xFEFC}, {0xFF10, 0xFF19},
    {0xFF21, 0xFF3A}, {0xFF3F, 0xFF3F}, {0xFF41, 0xFF5A}, {0xFF65, 0xFFBE}, {0xFFC2, 0xFFC7}, {0xFFCA, 0xFFCF},
    {0xFFD2, 0xFFD7}, {0xFFDA, 0xFFDC}, {0x10000, 0x1000B}, {0x1000D, 0x10026}, {0x10028, 0x1003A}, {0x1003C, 0x1003D},
    {0x1003F, 0x1004D}, {0x10050, 0x1005D}, {0x10080, 0x100FA}, {0x10140, 0x10174}, {0x101FD, 0x101FD}, {0x10280, 0x1029C},
    {0x102A0, 0x102D0}, {0x102E0, 0x102E0}, {0x10300, 0x1031F}, {0x1032D, 0x1034A}, {0x10350, 0x1037A}, {0x10380, 0x1039D},
    {0x103A0, 0x103C3}, {0x103C8, 0x103CF}, {0x103D1, 0x103D5}, {0x10400, 0x1049D}, {0x104A0, 0x104A9}, {0x104B0, 0x104D3},
    {0x104D8, 0x104FB}, {0x10500, 0x10527}, {0x10530, 0x10563}, {0x10570, 0x1057A}, {0x1057C, 0x1058A}, {0x1058C, 0x10592},
    {0x10594, 0x10595}, {0x10597, 0x105A1}, {0x105A3, 0x105B1}, {0x105B3, 0x105B9}, {0x105BB, 0x105BC}, {0x105C0, 0x105F3},
    {0x10600, 0x10736}, {0x10740, 0x10755}, {0x10760, 0x10767}, {0x10780, 0x10785}, {0x10787, 0x107B0}, {0x107B2, 0x107BF},
    {0x10800, 0x10805}, {0x10808, 0x10808}, {0x1080A, 0x10835}, {0x10837, 0x10838}, {0x1083C, 0x1083C}, {0x1083F, 0x10855},
    {0x10860, 0x10876}, {0x10880, 0x1089E}, {0x108E0, 0x108F2}, {0x108F4, 0x108F5}, {0x10900, 0x10915}, {0x10920, 0x10939},
    {0x10940, 0x10959}, {0x10980, 0x109B7}, {0x109BE, 0x109BF}, {0x10A00, 0x10A03}, {0x10A05, 0x10A06}, {0x10A0C, 0x10A13},
    {0x10A15, 0x10A17}, {0x10A19, 0x10A35}, {0x10A38, 0x10A3A}, {0x10A3F, 0x10A3F}, {0x10A60, 0x10A7C}, {0x10A80, 0x10A9C},
    {0x10AC0, 0x10AC7}, {0x10AC9, 0x10AE6}, {0x10B00, 0x10B35}, {0x10B40, 0x10B55}, {0x10B60, 0x10B72}, {0x10B80, 0x10B91},
    {0x10C00, 0x10C48}, {0x10C80, 0x10CB2}, {0x10CC0, 0x10CF2}, {0x10D00, 0x10D27}, {0x10D30, 0x10D39}, {0x10D40, 0x10D65},
    {0x10D69, 0x10D6D}, {0x10D6F, 0x10D85}, {0x10E80, 0x10EA9}, {0x10EAB, 0x10EAC}, {0x10EB0, 0x10EB1}, {0x10EC2, 0x10EC7},
    {0x10ECB, 0x10ECF}, {0x10ED9, 0x10EEE}, {0x10EF0, 0x10F1C}, {0x10F27, 0x10F27}, {0x10F30, 0x10F50}, {0x10F70, 0x10F85},
    {0x10FB0, 0x10FC4}, {0x10FE0, 0x10FF6}, {0x11000, 0x11046}, {0x11066, 0x11075}, {0x1107F, 0x110BA}, {0x110C2, 0x110C2},
    {0x110D0, 0x110E8}, {0x110F0, 0x110F9}, {0x11100, 0x11134}, {0x11136, 0x1113F}, {0x11144, 0x11147}, {0x11150, 0x11173},
    {0x11176, 0x11176}, {0x11180, 0x111C4}, {0x111C9, 0x111CC}, {0x111CE, 0x111DA}, {0x111DC, 0x111DC}, {0x11200, 0x11211},
    {0x11213, 0x11237}, {0x1123E, 0x11241}, {0x11280, 0x11286}, {0x11288, 0x11288}, {0x1128A, 0x1128D}, {0x1128F, 0x1129D},
    {0x1129F, 0x112A8}, {0x112B0, 0x112EA}, {0x112F0, 0x112F9}, {0x11300, 0x11303}, {0x11305, 0x1130C}, {0x1130F, 0x11310},
    {0x11313, 0x11328}, {0x1132A, 0x11330}, {0x11332, 0x11333}, {0x11335, 0x11339}, {0x1133B, 0x11344}, {0x11347, 0x11348},
    {0x1134B, 0x1134D}, {0x11350, 0x11350}, {0x11357, 0x11357}, {0x1135D, 0x11363}, {0x11366, 0x1136C}, {0x11370, 0x11374},
    {0x11380, 0x11389}, {0x1138B, 0x1138B}, {0x1138E, 0x1138E}, {0x11390, 0x113B5}, {0x113B7, 0x113C0}, {0x113C2, 0x113C2},
    {0x113C5, 0x113C5}, {0x113C7, 0x113CA}, {0x113CC, 0x113D3}, {0x113E1, 0x113E2}, {0x11400, 0x1144A}, {0x11450, 0x11459},
    {0x1145E, 0x11461}, {0x11480, 0x114C5}, {0x114C7, 0x114C7}, {0x114D0, 0x114D9}, {0x11580, 0x115B5}, {0x115B8, 0x115C0},
    {0x115D8, 0x115DD}, {0x11600, 0x11640}, {0x11644, 0x11644}, {0x11650, 0x11659}, {0x11680, 0x116B8}, {0x116C0, 0x116C9},
    {0x116D0, 0x116E3}, {0x11700, 0x1171A}, {0x1171D, 0x1172B}, {0x11730, 0x11739}, {0x11740, 0x11746}, {0x11800, 0x1183A},
    {0x118A0, 0x118E9}, {0x118FF, 0x11906}, {0x11909, 0x11909}, {0x1190C, 0x11913}, {0x11915, 0x11916}, {0x11918, 0x11935},
    {0x11937, 0x11938}, {0x1193B, 0x11943}, {0x11950, 0x11959}, {0x119A0, 0x119A7}, {0x119AA, 0x119D7}, {0x119DA, 0x119E1},
    {0x119E3, 0x119E4}, {0x11A00, 0x11A3E}, {0x11A47, 0x11A47}, {0x11A50, 0x11A99}, {0x11A9D, 0x11A9D}, {0x11AB0, 0x11AF8},
    {0x11B0A, 0x11B0A}, {0x11B60, 0x11B67}, {0x11BC0, 0x11BE0}, {0x11BF0, 0x11BF9}, {0x11C00, 0x11C08}, {0x11C0A, 0x11C36},
    {0x11C38, 0x11C40}, {0x11C50, 0x11C59}, {0x11C72, 0x11C8F}, {0x11C92, 0x11CA7}, {0x11CA9, 0x11CB6}, {0x11D00, 0x11D06},
    {0x11D08, 0x11D09}, {0x11D0B, 0x11D36}, {0x11D3A, 0x11D3A}, {0x11D3C, 0x11D3D}, {0x11D3F, 0x11D47}, {0x11D50, 0x11D59},
    {0x11D60, 0x11D65}, {0x11D67, 0x11D68}, {0x11D6A, 0x11D8E}, {0x11D90, 0x11D91}, {0x11D93, 0x11D98}, {0x11DA0, 0x11DA9},
    {0x11DB0, 0x11DDB}, {0x11DE0, 0x11DE9}, {0x11DF0, 0x11DF1}, {0x11EE0, 0x11EF6}, {0x11F00, 0x11F10}, {0x11F12, 0x11F3A},
    {0x11F3E, 0x11F42}, {0x11F50, 0x11F5A}, {0x11FB0, 0x11FB0}, {0x12000, 0x12399}, {0x12400, 0x1246F}, {0x12475, 0x12543},
    {0x12550, 0x12686}, {0x12F90, 0x12FF0}, {0x13000, 0x1342F}, {0x13440, 0x13455}, {0x13460, 0x143FA}, {0x14400, 0x14646},
    {0x16100, 0x16139}, {0x16800, 0x16A38}, {0x16A40, 0x16A5E}, {0x16A60, 0x16A69}, {0x16A70, 0x16ABE}, {0x16AC0, 0x16AC9},
    {0x16AD0, 0x16AED}, {0x16AF0, 0x16AF4}, {0x16B00, 0x16B36}, {0x16B40, 0x16B43}, {0x16B50, 0x16B59}, {0x16B63, 0x16B77},
    {0x16B7D, 0x16B8F}, {0x16D40, 0x16D6C}, {0x16D70, 0x16D79}, {0x16D80, 0x16D9D}, {0x16DA0, 0x16DA9}, {0x16E40, 0x16E7F},
    {0x16EA0, 0x16EB8}, {0x16EBB, 0x16ED3}, {0x16F00, 0x16F4A}, {0x16F4F, 0x16F87}, {0x16F8F, 0x16F9F}, {0x16FE0, 0x16FE1},
    {0x16FE3, 0x16FE4}, {0x16FF0, 0x16FF6}, {0x17000, 0x18CDA}, {0x18CFF, 0x18D20}, {0x18D80, 0x18DF2}, {0x18E00, 0x19191},
    {0x191A0, 0x191D2}, {0x1AFF0, 0x1AFF3}, {0x1AFF5, 0x1AFFB}, {0x1AFFD, 0x1AFFE}, {0x1B000, 0x1B128}, {0x1B132, 0x1B132},
    {0x1B150, 0x1B152}, {0x1B155, 0x1B155}, {0x1B164, 0x1B168}, {0x1B170, 0x1B2FB}, {0x1BC00, 0x1BC6A}, {0x1BC70, 0x1BC7C},
    {0x1BC80, 0x1BC88}, {0x1BC90, 0x1BC99}, {0x1BC9D, 0x1BC9E}, {0x1CCF0, 0x1CCF9}, {0x1CF00, 0x1CF2D}, {0x1CF30, 0x1CF46},
    {0x1D127, 0x1D128}, {0x1D165, 0x1D169}, {0x1D16D, 0x1D172}, {0x1D17B, 0x1D182}, {0x1D185, 0x1D18B}, {0x1D1AA, 0x1D1AD},
    {0x1D242, 0x1D244}, {0x1D250, 0x1D252}, {0x1D25B, 0x1D25C}, {0x1D25F, 0x1D25F}, {0x1D280, 0x1D281}, {0x1D400, 0x1D454},
    {0x1D456, 0x1D49C}, {0x1D49E, 0x1D49F}, {0x1D4A2, 0x1D4A2}, {0x1D4A5, 0x1D4A6}, {0x1D4A9, 0x1D4AC}, {0x1D4AE, 0x1D4B9},
    {0x1D4BB, 0x1D4BB}, {0x1D4BD, 0x1D4C3}, {0x1D4C5, 0x1D505}, {0x1D507, 0x1D50A}, {0x1D50D, 0x1D514}, {0x1D516, 0x1D51C},
    {0x1D51E, 0x1D539}, {0x1D53B, 0x1D53E}, {0x1D540, 0x1D544}, {0x1D546, 0x1D546}, {0x1D54A, 0x1D550}, {0x1D552, 0x1D6A6},
    {0x1D6A8, 0x1D7CB}, {0x1D7CE, 0x1D7FF}, {0x1DA00, 0x1DA36}, {0x1DA3B, 0x1DA6C}, {0x1DA75, 0x1DA75}, {0x1DA84, 0x1DA84},
    {0x1DA9B, 0x1DA9F}, {0x1DAA1, 0x1DAAF}, {0x1DF00, 0x1DF81}, {0x1DF90, 0x1DF96}, {0x1DFCD, 0x1E006}, {0x1E008, 0x1E018},
    {0x1E01B, 0x1E021}, {0x1E023, 0x1E024}, {0x1E026, 0x1E02A}, {0x1E030, 0x1E06D}, {0x1E08F, 0x1E08F}, {0x1E100, 0x1E12C},
    {0x1E130, 0x1E13D}, {0x1E140, 0x1E149}, {0x1E14E, 0x1E14E}, {0x1E290, 0x1E2AE}, {0x1E2C0, 0x1E2F9}, {0x1E4D0, 0x1E4F9},
    {0x1E5D0, 0x1E5FA}, {0x1E6C0, 0x1E6DE}, {0x1E6E0, 0x1E6F5}, {0x1E6FE, 0x1E6FF}, {0x1E7E0, 0x1E7E6}, {0x1E7E8, 0x1E7EB},
    {0x1E7ED, 0x1E7EE}, {0x1E7F0, 0x1E7FE}, {0x1E800, 0x1E8C4}, {0x1E8D0, 0x1E8D6}, {0x1E900, 0x1E94B}, {0x1E950, 0x1E959},
    {0x1EE00, 0x1EE03}, {0x1EE05, 0x1EE1F}, {0x1EE21, 0x1EE22}, {0x1EE24, 0x1EE24}, {0x1EE27, 0x1EE27}, {0x1EE29, 0x1EE32},
    {0x1EE34, 0x1EE37}, {0x1EE39, 0x1EE39}, {0x1EE3B, 0x1EE3B}, {0x1EE42, 0x1EE42}, {0x1EE47, 0x1EE47}, {0x1EE49, 0x1EE49},
    {0x1EE4B, 0x1EE4B}, {0x1EE4D, 0x1EE4F}, {0x1EE51, 0x1EE52}, {0x1EE54, 0x1EE54}, {0x1EE57, 0x1EE57}, {0x1EE59, 0x1EE59},
    {0x1EE5B, 0x1EE5B}, {0x1EE5D, 0x1EE5D}, {0x1EE5F, 0x1EE5F}, {0x1EE61, 0x1EE62}, {0x1EE64, 0x1EE64}, {0x1EE67, 0x1EE6A},
    {0x1EE6C, 0x1EE72}, {0x1EE74, 0x1EE77}, {0x1EE79, 0x1EE7C}, {0x1EE7E, 0x1EE7E}, {0x1EE80, 0x1EE89}, {0x1EE8B, 0x1EE9B},
    {0x1EEA1, 0x1EEA3}, {0x1EEA5, 0x1EEA9}, {0x1EEAB, 0x1EEBB}, {0x1FBF0, 0x1FBF9}, {0x20000, 0x2A6DF}, {0x2A700, 0x2B81E},
    {0x2B820, 0x2CEAD}, {0x2CEB0, 0x2EBE0}, {0x2EBF0, 0x2EE5D}, {0x2F800, 0x2FA1D}, {0x30000, 0x3134A}, {0x31350, 0x33479},
    {0x3D000, 0x3FC3F}, {0xE0100, 0xE01EF},
};

// ===== c_tokenizer_scalar.c =====
// The scalar C lexer: the reference that c_tokenize's vectorized path is checked against, and what it falls back on.
// It follows clang's raw lexer (clang/lib/Lex/Lexer.cpp) in C23 mode, and the functions are named after clang's.

#include <stdbool.h>
#include <string.h>

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define END_OF_FILE (-1)
// The code point of a named UCN (`\N{...}`) whose name looks valid. See the header.
#define NAMED_UCN 0xFFFFFFFFu

typedef struct ScalarLexer ScalarLexer;
struct ScalarLexer
{
    const uint8_t* source;
    uint64_t length;
    bool trigraphs;
    bool cplusplus;
};

typedef struct TokenOutput TokenOutput;
struct TokenOutput
{
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
    uint64_t count;
    bool start_of_line;
};

static bool is_whitespace(int32_t c)
{
    return c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\n' || c == '\r';
}

static bool is_digit(int32_t c)
{
    return c >= '0' && c <= '9';
}

static bool is_ascii_identifier_continue(int32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '_';
}

static int32_t hex_digit_value(int32_t c)
{
    int32_t result = -1;
    if (is_digit(c))
    {
        result = c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        result = c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'F')
    {
        result = c - 'A' + 10;
    }
    return result;
}

static int32_t decode_trigraph(uint8_t c)
{
    int32_t result = 0;
    switch (c)
    {
        case '=': result = '#'; break;
        case '/': result = '\\'; break;
        case '\'': result = '^'; break;
        case '(': result = '['; break;
        case ')': result = ']'; break;
        case '!': result = '|'; break;
        case '<': result = '{'; break;
        case '>': result = '}'; break;
        case '-': result = '~'; break;
    }
    return result;
}

static bool code_point_in(const uint32_t (*ranges)[2], uint64_t count, uint32_t code_point)
{
    uint64_t low = 0;
    uint64_t high = count;
    while (low < high)
    {
        uint64_t middle = low + (high - low) / 2;
        if (code_point < ranges[middle][0])
        {
            high = middle;
        }
        else if (code_point > ranges[middle][1])
        {
            low = middle + 1;
        }
        else
        {
            return true;
        }
    }
    return false;
}

static bool is_identifier_start_code_point(uint32_t code_point)
{
    return code_point == NAMED_UCN || code_point_in(c_identifier_start_ranges, ARRAY_LENGTH(c_identifier_start_ranges), code_point);
}

static bool is_identifier_continue_code_point(uint32_t code_point)
{
    return code_point == NAMED_UCN || code_point_in(c_identifier_continue_ranges, ARRAY_LENGTH(c_identifier_continue_ranges), code_point);
}

// clang's getEscapedNewLineSize: the size of the whitespace and newline after a backslash, if they make it a line
// splice ("\r\n" and "\n\r" count as one newline), or 0.
static uint64_t escaped_newline_size(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t size = 0;
    while (position + size < lexer->length && is_whitespace(lexer->source[position + size]))
    {
        size += 1;
        uint8_t c = lexer->source[position + size - 1];
        if (c == '\n' || c == '\r')
        {
            if (position + size < lexer->length)
            {
                uint8_t next = lexer->source[position + size];
                if ((next == '\n' || next == '\r') && next != c)
                {
                    size += 1;
                }
            }
            return size;
        }
    }
    return 0;
}

// clang's getCharAndSize: the character at `position` after trigraph replacement and line splicing, the number of
// bytes it takes up (`size`, including the line splices before it) and where the character itself starts.
static int32_t get_char(const ScalarLexer* lexer, uint64_t position, uint64_t* size, uint64_t* char_start)
{
    uint64_t p = position;
    int32_t result = END_OF_FILE;
    while (p < lexer->length)
    {
        int32_t c = lexer->source[p];
        uint64_t char_size = 1;
        if (lexer->trigraphs && c == '?' && p + 2 < lexer->length && lexer->source[p + 1] == '?')
        {
            int32_t trigraph = decode_trigraph(lexer->source[p + 2]);
            if (trigraph != 0)
            {
                c = trigraph;
                char_size = 3;
            }
        }
        if (c == '\\')
        {
            uint64_t newline_size = escaped_newline_size(lexer, p + char_size);
            if (newline_size != 0)
            {
                p += char_size + newline_size;
                continue;
            }
        }
        *char_start = p;
        result = c;
        p += char_size;
        break;
    }
    if (result == END_OF_FILE)
    {
        *char_start = lexer->length;
    }
    *size = p - position;
    return result;
}

static int32_t peek_char(const ScalarLexer* lexer, uint64_t position, uint64_t* size)
{
    uint64_t char_start;
    return get_char(lexer, position, size, &char_start);
}

// llvm::convertUTF8Sequence with strictConversion: the length of the UTF-8 sequence at `position`, or 0 if it isn't
// valid (overlong, a surrogate, past U+10FFFF or cut short by the end of the file).
static uint64_t decode_utf8(const ScalarLexer* lexer, uint64_t position, uint32_t* code_point)
{
    const uint8_t* s = lexer->source + position;
    uint64_t available = lexer->length - position;
    uint8_t lead = s[0];
    uint64_t length = 0;
    uint32_t value = 0;
    uint8_t second_min = 0x80;
    uint8_t second_max = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
        value = lead & 0x1F;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        value = lead & 0x0F;
        second_min = lead == 0xE0 ? 0xA0 : 0x80;
        second_max = lead == 0xED ? 0x9F : 0xBF;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        value = lead & 0x07;
        second_min = lead == 0xF0 ? 0x90 : 0x80;
        second_max = lead == 0xF4 ? 0x8F : 0xBF;
    }

    uint64_t result = 0;
    if (length != 0 && available >= length && s[1] >= second_min && s[1] <= second_max)
    {
        result = length;
        for (uint64_t i = 1; i < length; i += 1)
        {
            if ((s[i] & 0xC0) != 0x80)
            {
                result = 0;
            }
            value = (value << 6) | (s[i] & 0x3F);
        }
        *code_point = value;
    }
    return result;
}

// clang's tryReadUCN: reads the universal character name whose backslash `get_char` returns at `position`. Returns
// false if there is none. Otherwise `end` is past it and `code_point` is its code point, or 0 if C doesn't allow it
// outside of literals (clang still treats the whole UCN as one character then).
static bool read_ucn(const ScalarLexer* lexer, uint64_t position, uint64_t* end, uint32_t* code_point)
{
    uint64_t size;
    peek_char(lexer, position, &size);
    uint64_t p = position + size;
    int32_t kind = peek_char(lexer, p, &size);
    p += size;

    bool result = false;
    if (kind == 'u' || kind == 'U')
    {
        uint32_t digits_wanted = kind == 'u' ? 4 : 8;
        bool delimited = false;
        bool valid = true;
        uint32_t count = 0;
        uint32_t value = 0;
        while (count != digits_wanted || delimited)
        {
            int32_t c = peek_char(lexer, p, &size);
            if (!delimited && count == 0 && c == '{')
            {
                delimited = true;
                p += size;
                continue;
            }
            if (delimited && c == '}')
            {
                p += size;
                break;
            }
            int32_t digit = hex_digit_value(c);
            if (digit < 0)
            {
                valid = !delimited;
                break;
            }
            if (value & 0xF0000000u)
            {
                valid = false;
                break;
            }
            value = (value << 4) | (uint32_t)digit;
            p += size;
            count += 1;
        }
        if (valid && count != 0 && !(delimited && kind == 'U') && (delimited || count == digits_wanted))
        {
            result = true;
            *end = p;
            *code_point = (value < 0xA0 || (value >= 0xD800 && value <= 0xDFFF)) ? 0 : value;
        }
    }
    else if (kind == 'N' && peek_char(lexer, p, &size) == '{')
    {
        p += size;
        uint64_t name_length = 0;
        bool found_end = false;
        bool valid_name = true;
        for (;;)
        {
            int32_t c = peek_char(lexer, p, &size);
            if (c == END_OF_FILE)
            {
                break;
            }
            p += size;
            if (c == '}')
            {
                found_end = true;
                break;
            }
            if (c == '\n' || c == '\r')
            {
                break;
            }
            name_length += 1;
            if (c == 0)
            {
                break;
            }
            valid_name &= (c >= 'A' && c <= 'Z') || is_digit(c) || c == ' ' || c == '-';
        }
        if (found_end && name_length != 0)
        {
            result = true;
            *end = p;
            *code_point = valid_name ? NAMED_UCN : 0;
        }
    }
    return result;
}

// clang's tryConsumeIdentifierUCN: `get_char` returns a backslash at `position`.
static bool try_consume_identifier_ucn(const ScalarLexer* lexer, uint64_t position, uint64_t* end)
{
    uint64_t ucn_end;
    uint32_t code_point;
    bool result = read_ucn(lexer, position, &ucn_end, &code_point) && code_point != 0 && is_identifier_continue_code_point(code_point);
    if (result)
    {
        *end = ucn_end;
    }
    return result;
}

// clang's tryConsumeIdentifierUTF8Char: `get_char` returns a non-ASCII byte at `position`.
static bool try_consume_identifier_utf8(const ScalarLexer* lexer, uint64_t position, uint64_t* end)
{
    uint64_t size;
    uint64_t char_start;
    get_char(lexer, position, &size, &char_start);
    uint32_t code_point;
    uint64_t length = decode_utf8(lexer, char_start, &code_point);
    bool result = length != 0 && is_identifier_continue_code_point(code_point);
    if (result)
    {
        *end = char_start + length;
    }
    return result;
}

// clang's LexIdentifierContinue: returns the end of the identifier whose first character ends at `position`.
static uint64_t lex_identifier_continue(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t p = position;
    for (;;)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        if (is_ascii_identifier_continue(c) || c == '$')
        {
            p += size;
        }
        else if (!((c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p))))
        {
            break;
        }
    }
    return p;
}

// clang's LexNumericConstant: returns the end of the pp-number whose first character (and the digit after a leading
// '.') end at `position`.
static uint64_t lex_numeric_constant(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t p = position;
    for (;;)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        int32_t previous = 0;
        while (is_ascii_identifier_continue(c) || c == '.' || c == '$')
        {
            p += size;
            previous = c;
            c = peek_char(lexer, p, &size);
        }

        if ((c == '-' || c == '+') && (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P'))
        {
            p += size;
            continue;
        }
        if (c == '\'')
        {
            uint64_t next_size;
            if (is_ascii_identifier_continue(peek_char(lexer, p + size, &next_size)))
            {
                p += size + next_size;
                continue;
            }
        }
        if ((c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p)))
        {
            continue;
        }
        break;
    }
    return p;
}

// The suffixes of the standard library's string literal operators, which (unlike others) don't start with '_'.
static bool is_standard_suffix(const char* suffix, uint32_t length)
{
    static const char* const suffixes[] = { "d", "h", "i", "if", "il", "min", "ms", "ns", "s", "sv", "us", "y" };
    bool result = false;
    for (uint32_t i = 0; i < ARRAY_LENGTH(suffixes); i += 1)
    {
        result |= strlen(suffixes[i]) == length && memcmp(suffixes[i], suffix, length) == 0;
    }
    return result;
}

// clang's LexUDSuffix (C++): the end of the user-defined literal suffix after the literal that ends at `position`
// (`position` itself if it has none).
static uint64_t lex_ud_suffix(const ScalarLexer* lexer, uint64_t position, bool is_string)
{
    uint64_t p = position;
    uint64_t size;
    int32_t c = peek_char(lexer, p, &size);
    bool consumed = false;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
    {
        if (!((c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p))))
        {
            return position;
        }
        consumed = true;
    }
    if (!consumed)
    {
        // A suffix must start with '_', unless it's one of the standard library's (after a string literal).
        bool is_suffix = c == '_';
        if (!is_suffix && is_string)
        {
            char suffix[3] = { (char)c };
            uint32_t length = 1;
            uint64_t q = p + size;
            for (;;)
            {
                uint64_t next_size;
                int32_t next = peek_char(lexer, q, &next_size);
                if (!is_ascii_identifier_continue(next) && next != '$')
                {
                    is_suffix = is_standard_suffix(suffix, length);
                    break;
                }
                if (length == 3)
                {
                    break;
                }
                suffix[length] = (char)next;
                length += 1;
                q += next_size;
            }
        }
        if (!is_suffix)
        {
            return position;
        }
        p += size;
    }
    for (;;)
    {
        c = peek_char(lexer, p, &size);
        if (is_ascii_identifier_continue(c) || c == '$')
        {
            p += size;
        }
        else if (!((c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p))))
        {
            break;
        }
    }
    return p;
}

// clang's LexRawStringLiteral (C++): returns the end of the raw string literal whose `R"` ends at `position`, and
// turns `kind` into `C_TOKEN_UNKNOWN` if its delimiter is invalid or it is unterminated. Inside of it, trigraphs and
// line splices are undone, so it is read from the source bytes.
static uint64_t lex_raw_string(const ScalarLexer* lexer, uint64_t position, uint8_t* kind)
{
    const uint8_t* s = lexer->source;
    const uint64_t n = lexer->length;
    // The delimiter: up to 16 printable characters other than space, '(', ')' and '\\', then '('.
    uint64_t delimiter_length = 0;
    while (delimiter_length != 16 && position + delimiter_length < n && s[position + delimiter_length] > ' ' && s[position + delimiter_length] < 0x7F &&
           s[position + delimiter_length] != '(' && s[position + delimiter_length] != ')' && s[position + delimiter_length] != '\\')
    {
        delimiter_length += 1;
    }
    uint64_t p = position;
    if (position + delimiter_length >= n || s[position + delimiter_length] != '(')
    {
        // An unknown token, up to the next '"'.
        while (p < n && s[p] != '"')
        {
            p += 1;
        }
        *kind = C_TOKEN_UNKNOWN;
        return p < n ? p + 1 : n;
    }
    p = position + delimiter_length + 1;
    for (;;)
    {
        if (p >= n)
        {
            *kind = C_TOKEN_UNKNOWN;
            return n;
        }
        const uint8_t c = s[p];
        p += 1;
        if (c == ')' && p + delimiter_length < n && memcmp(s + p, s + position, delimiter_length) == 0 && s[p + delimiter_length] == '"')
        {
            p += delimiter_length + 1;
            break;
        }
    }
    return lex_ud_suffix(lexer, p, true);
}

// clang's LexCharConstant and LexStringLiteral: returns the end of the literal whose opening quote ends at
// `position`, and turns `kind` into `C_TOKEN_UNKNOWN` if the literal is unterminated (or an empty character constant).
// In C++, a user-defined suffix is part of the literal.
static uint64_t lex_char_or_string(const ScalarLexer* lexer, uint64_t position, int32_t quote, uint8_t* kind)
{
    uint64_t p = position;
    uint64_t size;
    int32_t c = peek_char(lexer, p, &size);
    p += size;
    if (quote == '\'' && c == '\'')
    {
        *kind = C_TOKEN_UNKNOWN;
        return p;
    }
    while (c != quote)
    {
        if (c == '\\')
        {
            c = peek_char(lexer, p, &size);
            p += size;
        }
        if (c == '\n' || c == '\r')
        {
            *kind = C_TOKEN_UNKNOWN;
            return p - 1;
        }
        if (c == END_OF_FILE)
        {
            *kind = C_TOKEN_UNKNOWN;
            return lexer->length;
        }
        c = peek_char(lexer, p, &size);
        p += size;
    }
    return lexer->cplusplus ? lex_ud_suffix(lexer, p, quote == '"') : p;
}

// Skips the block comment whose "/*" ends at `position`. Returns false if it is unterminated.
static bool skip_block_comment(const ScalarLexer* lexer, uint64_t position, uint64_t* end)
{
    uint64_t p = position;
    int32_t previous = 0;
    for (;;)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        if (c == END_OF_FILE)
        {
            return false;
        }
        p += size;
        if (c == '/' && previous == '*')
        {
            *end = p;
            return true;
        }
        previous = c;
    }
}

// Skips the line comment whose "//" ends at `position`, up to (not including) the newline that ends it.
static uint64_t skip_line_comment(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t p = position;
    for (;;)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        if (c == END_OF_FILE || c == '\n' || c == '\r')
        {
            return p;
        }
        p += size;
    }
}

static void emit(TokenOutput* output, uint8_t kind, uint64_t start, uint64_t end)
{
    output->kinds[output->count] = kind | (output->start_of_line ? C_TOKEN_START_OF_LINE : 0);
    output->starts[output->count] = (uint32_t)start;
    output->ends[output->count] = (uint32_t)end;
    output->count += 1;
    output->start_of_line = false;
}

// After a one-character punctuator `one` at `position`: if `get_char` returns `second` next, the token is `two`.
static uint8_t punctuator2(const ScalarLexer* lexer, uint64_t* position, int32_t second, uint8_t two, uint8_t one)
{
    uint64_t size;
    uint8_t result = one;
    if (peek_char(lexer, *position, &size) == second)
    {
        *position += size;
        result = two;
    }
    return result;
}

// Lexes from `position`, where a token may start (`start_of_line` if no token has started its logical line yet),
// writing the tokens to `kinds`, `starts` and `ends` from `count` on. Stops at the end of the source, or after the
// first newline at or past `stop_at` that is outside of tokens and comments: `resume` is then the position after it
// (else the length). Returns the new count. c_tokenize uses this for the parts its vectorized path can't do.
static uint64_t c_tokenize_scalar_range(const uint8_t* source, uint64_t length, uint64_t position, bool start_of_line, uint64_t stop_at,
                                 uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint64_t count, uint32_t flags, uint64_t* resume)
{
    ScalarLexer lexer_value = {
        .source = source,
        .length = length,
        .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = (flags & C_TOKENIZER_CPLUSPLUS) != 0,
    };
    const ScalarLexer* lexer = &lexer_value;
    TokenOutput output = { .kinds = kinds, .starts = starts, .ends = ends, .count = count, .start_of_line = start_of_line };
    uint64_t p = position;
    *resume = length;

    for (;;)
    {
        uint64_t size;
        uint64_t start;
        int32_t c = get_char(lexer, p, &size, &start);
        if (c == END_OF_FILE)
        {
            break;
        }
        p += size;

        uint64_t size2;
        int32_t c2;
        uint8_t kind = C_TOKEN_UNKNOWN;
        switch (c)
        {
            case ' ': case '\t': case '\f': case '\v': case 0:
                continue;
            case '\n': case '\r':
                output.start_of_line = true;
                if (start >= stop_at)
                {
                    *resume = p;
                    return output.count;
                }
                continue;
            case '/':
                c2 = peek_char(lexer, p, &size2);
                if (c2 == '/')
                {
                    p = skip_line_comment(lexer, p + size2);
                    continue;
                }
                if (c2 == '*')
                {
                    if (skip_block_comment(lexer, p + size2, &p))
                    {
                        continue;
                    }
                    p = length;
                    kind = C_TOKEN_UNKNOWN;
                }
                else
                {
                    kind = punctuator2(lexer, &p, '=', C_TOKEN_SLASHEQUAL, C_TOKEN_SLASH);
                }
                break;
            case '"':
                kind = C_TOKEN_STRING_LITERAL;
                p = lex_char_or_string(lexer, p, '"', &kind);
                break;
            case '\'':
                kind = C_TOKEN_CHAR_CONSTANT;
                p = lex_char_or_string(lexer, p, '\'', &kind);
                break;
            case 'L': case 'u': case 'U': case 'R':
            {
                // Prefixed character constants and string literals: L, u8, u and U, in the order of the kinds. In
                // C++, R (after one of those or alone) starts a raw string literal.
                uint8_t prefix = c == 'L' ? 1 : c == 'u' ? 3 : c == 'U' ? 4 : 0;
                c2 = peek_char(lexer, p, &size2);
                uint64_t quote_end = p + size2;
                if (c == 'u' && c2 == '8')
                {
                    uint64_t size3;
                    int32_t c3 = peek_char(lexer, quote_end, &size3);
                    if (c3 == '"' || c3 == '\'' || (c3 == 'R' && lexer->cplusplus))
                    {
                        prefix = 2;
                        c2 = c3;
                        quote_end += size3;
                    }
                }
                bool raw = false;
                if (lexer->cplusplus && (c == 'R' || c2 == 'R'))
                {
                    uint64_t size3;
                    if (c == 'R')
                    {
                        raw = c2 == '"';
                    }
                    else if (peek_char(lexer, quote_end, &size3) == '"')
                    {
                        raw = true;
                        c2 = '"';
                        quote_end += size3;
                    }
                }
                if (raw)
                {
                    kind = (uint8_t)(C_TOKEN_STRING_LITERAL + prefix);
                    p = lex_raw_string(lexer, quote_end, &kind);
                }
                else if (c != 'R' && (c2 == '"' || c2 == '\''))
                {
                    kind = (uint8_t)((c2 == '"' ? C_TOKEN_STRING_LITERAL : C_TOKEN_CHAR_CONSTANT) + prefix);
                    p = lex_char_or_string(lexer, quote_end, c2, &kind);
                }
                else
                {
                    kind = C_TOKEN_RAW_IDENTIFIER;
                    p = lex_identifier_continue(lexer, p);
                }
                break;
            }
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                kind = C_TOKEN_NUMERIC_CONSTANT;
                p = lex_numeric_constant(lexer, p);
                break;
            case '.':
                c2 = peek_char(lexer, p, &size2);
                if (is_digit(c2))
                {
                    kind = C_TOKEN_NUMERIC_CONSTANT;
                    p = lex_numeric_constant(lexer, p + size2);
                }
                else if (c2 == '*' && lexer->cplusplus)
                {
                    kind = C_TOKEN_PERIODSTAR;
                    p += size2;
                }
                else
                {
                    uint64_t size3;
                    kind = C_TOKEN_PERIOD;
                    if (c2 == '.' && peek_char(lexer, p + size2, &size3) == '.')
                    {
                        kind = C_TOKEN_ELLIPSIS;
                        p += size2 + size3;
                    }
                }
                break;
            case '[': kind = C_TOKEN_L_SQUARE; break;
            case ']': kind = C_TOKEN_R_SQUARE; break;
            case '(': kind = C_TOKEN_L_PAREN; break;
            case ')': kind = C_TOKEN_R_PAREN; break;
            case '{': kind = C_TOKEN_L_BRACE; break;
            case '}': kind = C_TOKEN_R_BRACE; break;
            case '~': kind = C_TOKEN_TILDE; break;
            case '?': kind = C_TOKEN_QUESTION; break;
            case ';': kind = C_TOKEN_SEMI; break;
            case ',': kind = C_TOKEN_COMMA; break;
            case '-':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_MINUS;
                if (c2 == '-' || c2 == '>' || c2 == '=')
                {
                    kind = c2 == '-' ? C_TOKEN_MINUSMINUS : c2 == '>' ? C_TOKEN_ARROW : C_TOKEN_MINUSEQUAL;
                    p += size2;
                    uint64_t size3;
                    if (c2 == '>' && lexer->cplusplus && peek_char(lexer, p, &size3) == '*')
                    {
                        kind = C_TOKEN_ARROWSTAR;
                        p += size3;
                    }
                }
                break;
            case '+':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_PLUS;
                if (c2 == '+' || c2 == '=')
                {
                    kind = c2 == '+' ? C_TOKEN_PLUSPLUS : C_TOKEN_PLUSEQUAL;
                    p += size2;
                }
                break;
            case '&':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_AMP;
                if (c2 == '&' || c2 == '=')
                {
                    kind = c2 == '&' ? C_TOKEN_AMPAMP : C_TOKEN_AMPEQUAL;
                    p += size2;
                }
                break;
            case '|':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_PIPE;
                if (c2 == '|' || c2 == '=')
                {
                    kind = c2 == '|' ? C_TOKEN_PIPEPIPE : C_TOKEN_PIPEEQUAL;
                    p += size2;
                }
                break;
            case '*': kind = punctuator2(lexer, &p, '=', C_TOKEN_STAREQUAL, C_TOKEN_STAR); break;
            case '^': kind = punctuator2(lexer, &p, '=', C_TOKEN_CARETEQUAL, C_TOKEN_CARET); break;
            case '!': kind = punctuator2(lexer, &p, '=', C_TOKEN_EXCLAIMEQUAL, C_TOKEN_EXCLAIM); break;
            case '=': kind = punctuator2(lexer, &p, '=', C_TOKEN_EQUALEQUAL, C_TOKEN_EQUAL); break;
            case '#': kind = punctuator2(lexer, &p, '#', C_TOKEN_HASHHASH, C_TOKEN_HASH); break;
            case '<':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_LESS;
                if (c2 == '<')
                {
                    p += size2;
                    kind = punctuator2(lexer, &p, '=', C_TOKEN_LESSLESSEQUAL, C_TOKEN_LESSLESS);
                }
                else if (c2 == '=' || c2 == ':' || c2 == '%')
                {
                    uint64_t size3;
                    uint64_t size4;
                    const int32_t c3 = peek_char(lexer, p + size2, &size3);
                    // In C++, "<::" is '<' and "::" unless a ':' or '>' follows.
                    if (c2 == ':' && lexer->cplusplus && c3 == ':' && peek_char(lexer, p + size2 + size3, &size4) != ':' &&
                        peek_char(lexer, p + size2 + size3, &size4) != '>')
                    {
                        break;
                    }
                    kind = c2 == '=' ? C_TOKEN_LESSEQUAL : c2 == ':' ? C_TOKEN_L_SQUARE : C_TOKEN_L_BRACE;
                    p += size2;
                    if (c2 == '=' && lexer->cplusplus && c3 == '>')
                    {
                        kind = C_TOKEN_SPACESHIP;
                        p += size3;
                    }
                }
                break;
            case '>':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_GREATER;
                if (c2 == '>')
                {
                    p += size2;
                    kind = punctuator2(lexer, &p, '=', C_TOKEN_GREATERGREATEREQUAL, C_TOKEN_GREATERGREATER);
                }
                else if (c2 == '=')
                {
                    kind = C_TOKEN_GREATEREQUAL;
                    p += size2;
                }
                break;
            case '%':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_PERCENT;
                if (c2 == '=' || c2 == '>')
                {
                    kind = c2 == '=' ? C_TOKEN_PERCENTEQUAL : C_TOKEN_R_BRACE;
                    p += size2;
                }
                else if (c2 == ':')
                {
                    // "%:" is '#', and "%:%:" is "##".
                    uint64_t size3;
                    uint64_t size4;
                    p += size2;
                    kind = C_TOKEN_HASH;
                    if (peek_char(lexer, p, &size3) == '%' && peek_char(lexer, p + size3, &size4) == ':')
                    {
                        kind = C_TOKEN_HASHHASH;
                        p += size3 + size4;
                    }
                }
                break;
            case ':':
                c2 = peek_char(lexer, p, &size2);
                kind = C_TOKEN_COLON;
                if (c2 == '>' || c2 == ':')
                {
                    kind = c2 == '>' ? C_TOKEN_R_SQUARE : C_TOKEN_COLONCOLON;
                    p += size2;
                }
                break;
            case '\\':
            {
                // A UCN that starts an identifier, or else an unknown token (the whole UCN if there is one).
                uint64_t end;
                uint32_t code_point;
                kind = C_TOKEN_UNKNOWN;
                if (read_ucn(lexer, start, &end, &code_point))
                {
                    p = end;
                    if (code_point != 0 && is_identifier_start_code_point(code_point))
                    {
                        kind = C_TOKEN_RAW_IDENTIFIER;
                        p = lex_identifier_continue(lexer, p);
                    }
                }
                break;
            }
            default:
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')
                {
                    kind = C_TOKEN_RAW_IDENTIFIER;
                    p = lex_identifier_continue(lexer, p);
                }
                else if (c >= 0x80)
                {
                    // A UTF-8 identifier, or else an unknown token: the whole character if it is valid UTF-8.
                    uint32_t code_point;
                    uint64_t utf8_length = decode_utf8(lexer, start, &code_point);
                    kind = C_TOKEN_UNKNOWN;
                    if (utf8_length != 0)
                    {
                        p = start + utf8_length;
                        if (is_identifier_start_code_point(code_point))
                        {
                            kind = C_TOKEN_RAW_IDENTIFIER;
                            p = lex_identifier_continue(lexer, p);
                        }
                    }
                }
                break;
        }
        emit(&output, kind, start, p);
    }

    return output.count;
}

int64_t c_tokenize_scalar(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    if (length > UINT32_MAX - 128)
    {
        return C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    const uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
    uint64_t resume;
    return (int64_t)c_tokenize_scalar_range(source, length, begin, true, UINT64_MAX, kinds, starts, ends, 0, flags, &resume);
}

// ===== c_tokenizer.c =====
// The vectorized C tokenizer. See c_tokenizer.h, and src/CompactingTokenizer.zig for the 512-byte-chunk design it
// shares with the Zig tokenizer.
//
// Each iteration tokenizes a 512-byte chunk as eight 64-byte vectors, with a 512-bit bitstring per character class
// (`u64x8`, lane 0 holding the lowest bits). Phase I classifies each vector, Phase II finds where every token starts
// and ends with bitstring arithmetic, and Phase III compresses the tokens' kinds, starts and ends into the output.
// Phase I runs a chunk ahead, and also classifies the first vector after the chunk, so that Phase II can look ahead:
// a token that crosses into the next chunk gets its start and kind from this chunk, and its end from the next one.
//
// A few rare inputs aren't modeled by the bitstrings, and make the vectorized path hand the chunk they're in to the
// scalar lexer (`c_tokenize_scalar_range`), from the start of the line before it to the first line that starts after
// it, where the vectorized path resumes: a line splice between characters that could make one token (outside of
// literals and line comments), a splice after a backslash or apostrophe or with whitespace before its newline, a
// non-ASCII byte or UCN outside of literals and comments (a UTF-8 identifier or a stray character), a stray
// backslash before a quote or backslash, the overlapping comment delimiters "/*/" and "*/*", an unterminated block
// comment, block comments whose lines need more than a few rescans to settle, C++'s raw string literals, and
// trigraphs (when enabled, for the whole source).
//
// C++ (C_TOKENIZER_CPLUSPLUS) is a specialization of the same chunk loop: it adds "<=>", "->*" and ".*", the "<::"
// rule and user-defined literal suffixes.



// The channels of 2-character punctuators, and those that need the same character twice.
#define OP2_CHANNELS (CHANNEL_EQ | CHANNEL_SELF | CHANNEL_SHIFT | CHANNEL_GT | CHANNEL_LTDIG | CHANNEL_HASHDIG)
#define SAME_CHARACTER_CHANNELS (CHANNEL_SELF | CHANNEL_SHIFT | CHANNEL_DOTS)

#define INLINE static inline __attribute__((always_inline))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

typedef uint64_t u64x8 __attribute__((ext_vector_type(8)));
typedef uint8_t u8x64 __attribute__((ext_vector_type(64)));
typedef uint8_t u8x16 __attribute__((ext_vector_type(16)));

static const char* const c_token_kind_names[C_TOKEN_KIND_COUNT] =
{
#define C_TOKEN_NAME(name, clang_name) #clang_name,
    C_TOKEN_KINDS(C_TOKEN_NAME)
#undef C_TOKEN_NAME
};

const char* c_token_kind_name(uint8_t kind)
{
    uint8_t k = kind & (uint8_t)~C_TOKEN_START_OF_LINE;
    return k < C_TOKEN_KIND_COUNT ? c_token_kind_names[k] : "<invalid kind>";
}

static const uint8_t iota_bytes[64] __attribute__((aligned(64))) =
{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

// ---------------------------------------------------------------------------------------------------------------------
// 512-bit bitstrings

// `a << i` and `a >> i` within the chunk, for a constant 0 < i < 64.
INLINE u64x8 shl(u64x8 a, int i)
{
    return (__builtin_shufflevector(a, (u64x8){0}, 8, 0, 1, 2, 3, 4, 5, 6) >> (64 - i)) | (a << i);
}

INLINE u64x8 shr(u64x8 a, int i)
{
    return (__builtin_shufflevector(a, (u64x8){0}, 1, 2, 3, 4, 5, 6, 7, 8) << (64 - i)) | (a >> i);
}

// `a << i`, shifting in the top `i` bits of the previous chunk's bitstring `previous`.
INLINE u64x8 shl_carry(u64x8 a, u64x8 previous, int i)
{
    return (__builtin_shufflevector(a, previous, 15, 0, 1, 2, 3, 4, 5, 6) >> (64 - i)) | (a << i);
}

// `a >> i`, shifting in the low `i` bits of `next`, the bitstring of the next chunk's first 64 bytes.
INLINE u64x8 shr_next(u64x8 a, uint64_t next, int i)
{
    return (__builtin_shufflevector(a, (u64x8){next}, 1, 2, 3, 4, 5, 6, 7, 8) << (64 - i)) | (a >> i);
}

// 512-bit subtraction: a lane borrows when the lane below it borrowed out (Kogge-Stone, propagated in a mask register).
INLINE u64x8 sub(u64x8 a, u64x8 b)
{
    const uint8_t carries = _mm512_cmplt_epu64_mask((__m512i)a, (__m512i)b);
    const uint8_t equal = _mm512_cmpeq_epi64_mask((__m512i)a, (__m512i)b);
    const uint8_t borrows = equal ^ (uint8_t)((carries << 1) + equal);
    const u64x8 difference = a - b;
    return (u64x8)_mm512_mask_add_epi64((__m512i)difference, borrows, (__m512i)difference, _mm512_set1_epi64(-1));
}

// 512-bit addition with a carry in and out.
INLINE u64x8 add_carry(u64x8 a, u64x8 b, uint32_t carry_in, uint32_t* carry_out)
{
    const u64x8 sum = a + b;
    const uint32_t generates = _mm512_cmplt_epu64_mask((__m512i)sum, (__m512i)a);
    const uint32_t propagates = _mm512_cmpeq_epi64_mask((__m512i)sum, _mm512_set1_epi64(-1));
    const uint32_t carries = ((generates << 1) | carry_in) + propagates;
    *carry_out = (carries >> 8) & 1;
    return (u64x8)_mm512_mask_sub_epi64((__m512i)sum, (uint8_t)(carries ^ propagates), (__m512i)sum, _mm512_set1_epi64(-1));
}

INLINE bool is_zero(u64x8 x)
{
    return _mm512_test_epi64_mask((__m512i)x, (__m512i)x) == 0;
}

INLINE uint64_t bit_511(u64x8 x)
{
    return x[7] >> 63;
}

// The bits below `limit` (at most 512).
INLINE u64x8 bits_below(uint32_t limit)
{
    const __m512i lane_starts = _mm512_setr_epi64(0, 64, 128, 192, 256, 320, 384, 448);
    const __m512i shifts = _mm512_max_epi64(_mm512_sub_epi64(_mm512_set1_epi64(limit), lane_starts), _mm512_setzero_si512());
    return (u64x8)_mm512_andnot_si512(_mm512_sllv_epi64(_mm512_set1_epi64(-1), shifts), _mm512_set1_epi64(-1));
}

// The position of the highest set bit below `limit` (at most 512), or -1 if there is none.
static int32_t highest_bit_below(u64x8 x, uint32_t limit)
{
    int32_t result = -1;
    for (int32_t lane = 7; lane >= 0; lane -= 1)
    {
        const uint32_t lane_start = (uint32_t)lane * 64;
        if (lane_start >= limit)
        {
            continue;
        }
        uint64_t bits = x[lane];
        if (limit - lane_start < 64)
        {
            bits &= _bzhi_u64(~0ull, limit - lane_start);
        }
        if (bits != 0)
        {
            result = (int32_t)lane_start + 63 - (int32_t)__builtin_clzll(bits);
            break;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Vectors

INLINE __m512i splat8(uint8_t x)
{
    return _mm512_set1_epi8((char)x);
}

// Looks up 64 bytes in a 16-byte table (repeated across the vector). Indices >= 0x80 give 0.
INLINE __m512i vpshufb(const uint8_t table[16], __m512i indices)
{
    return _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_load_si128((const __m128i*)table)), indices);
}

// Looks up 64 bytes in a 128-byte table, using the low 7 bits of each index.
INLINE __m512i perm2b(const uint8_t table[128], __m512i indices)
{
    return _mm512_permutex2var_epi8(_mm512_load_si512(table), indices, _mm512_load_si512(table + 64));
}

// The indices of the set bits of `mask`, in order.
INLINE __m512i bits_to_indices(uint64_t mask)
{
    return _mm512_maskz_compress_epi8(mask, _mm512_load_si512(iota_bytes));
}

// `current` shifted up by one byte, shifting in the last byte of `previous`.
INLINE __m512i shift_in_previous(__m512i current, __m512i previous)
{
    return (__m512i)__builtin_shufflevector((u8x64)current, (u8x64)previous, 127, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                            14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                                            35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
                                            56, 57, 58, 59, 60, 61, 62);
}

// The kinds of up to 16 multi-character punctuators that start at `start_indices` of `current` (continuing into
// `next`), by a perfect hash of their first three characters (two for 2-character punctuators). `long_ones` marks the
// punctuators of three or more characters.
INLINE __m128i punctuator_kinds(__m512i current, __m512i next, __m128i start_indices, uint16_t long_ones)
{
    // Per punctuator, the offsets of its bytes from its start; 0x80 zeroes a byte.
    const __m512i offsets = _mm512_mask_blend_epi32(long_ones, _mm512_set1_epi32((int)0x80800100), _mm512_set1_epi32((int)0x80020100));
    const u8x16 s = (u8x16)start_indices;
    const u8x64 repeated = __builtin_shufflevector(s, s, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6,
                                                   6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12,
                                                   12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15);
    const __m512i byte_indices = _mm512_add_epi8((__m512i)repeated, offsets);
    const __m512i bytes = _mm512_maskz_permutex2var_epi8(_mm512_cmplt_epu8_mask(offsets, splat8(0x80)), current, byte_indices, next);
    const __m512i hashes = _mm512_srli_epi32(_mm512_mullo_epi32(bytes, _mm512_set1_epi32((int)OP_HASH_MULTIPLIER)), 26);
    return _mm512_cvtepi32_epi8(_mm512_permutexvar_epi8(hashes, _mm512_load_si512(c_op_hash_kinds)));
}

// Could the characters before and after a line splice be part of the same token (outside of literals and comments)?
// `earlier` is the character before `before`.
static bool splice_joins(uint8_t earlier, uint8_t before, uint8_t after, bool cplusplus)
{
    const bool before_identifier = before >= 0x80 || (c_char_properties[before] & CHAR_IDENTIFIER);
    const bool after_identifier = after >= 0x80 || (c_char_properties[after] & CHAR_IDENTIFIER);
    bool result;
    if (after == '\\')
    {
        result = true; // Another splice (or a UCN), so `before` may join what comes after it.
    }
    else if (before_identifier)
    {
        // An identifier or pp-number that goes on (with a '.', a digit separator, an exponent's sign or a UCN), or
        // a prefix and its literal.
        const bool exponent = before == 'e' || before == 'E' || before == 'p' || before == 'P';
        result = after_identifier || after == '.' || after == '\'' || after == '"' || after == '\\' || (exponent && (after == '+' || after == '-'));
    }
    else if ((before == '+' || before == '-') && (earlier == 'e' || earlier == 'E' || earlier == 'p' || earlier == 'P') &&
             (after_identifier || after == '.' || after == '\'' || after == '\\'))
    {
        result = true; // The sign of an exponent, in a pp-number that goes on.
    }
    else if (cplusplus && (before == '"' || (before == '.' && after == '*') || (before == '>' && earlier == '-' && after == '*') ||
                           (before == '=' && earlier == '<' && after == '>')))
    {
        result = before != '"' || after_identifier; // ".*", "->*" and "<=>", or a literal's user-defined suffix.
    }
    else if (before == '.')
    {
        result = after == '.' || after_identifier || after == '\''; // An ellipsis, or a pp-number.
    }
    else
    {
        // A punctuator of two or more characters (with ".." and ":%" for "..." and "%:%:"), or a comment's start.
        const uint8_t first = c_operator_classes[before & 0x7F];
        const uint8_t second = after < 0x80 ? c_operator_classes[after] : 0;
        uint8_t channels = c_first_char_channels[first & 15] & c_second_char_channels[second & 15];
        if (first != second)
        {
            channels &= (uint8_t)~SAME_CHARACTER_CHANNELS;
        }
        result = (before < 0x80 && channels != 0) || (before == '/' && (after == '/' || after == '*')) || (before == ':' && after == '%');
    }
    return result;
}

// The byte at `position` of the source, or a newline outside of [`begin`, `length`).
INLINE uint8_t source_byte(const uint8_t* source, uint64_t begin, uint64_t length, int64_t position)
{
    return position >= (int64_t)begin && position < (int64_t)length ? source[position] : '\n';
}

INLINE bool is_identifier_byte(uint8_t c)
{
    return c >= 0x80 || (c_char_properties[c] & CHAR_IDENTIFIER);
}

// In C++, does the quote at `position` start a raw string literal (it ends an identifier R, LR, u8R, uR or UR)?
static bool is_raw_string_quote(const uint8_t* source, uint64_t begin, uint64_t length, int64_t position)
{
    bool result = false;
    if (source_byte(source, begin, length, position - 1) == 'R')
    {
        const uint8_t before = source_byte(source, begin, length, position - 2);
        const uint8_t earlier = source_byte(source, begin, length, position - 3);
        result = !is_identifier_byte(before) || ((before == 'L' || before == 'u' || before == 'U') && !is_identifier_byte(earlier)) ||
                 (before == '8' && earlier == 'u' && !is_identifier_byte(source_byte(source, begin, length, position - 4)));
    }
    return result;
}

// In C++, is the identifier at `position` (after a literal's closing quote) a user-defined suffix of the literal? It
// is if it starts with '_', or (after a string literal) it's one of the standard library's suffixes.
static bool is_literal_suffix(const uint8_t* source, uint64_t begin, uint64_t length, int64_t position, bool after_string)
{
    static const char* const suffixes[] = { "d", "h", "i", "if", "il", "min", "ms", "ns", "s", "sv", "us", "y" };
    bool result = source_byte(source, begin, length, position) == '_';
    if (!result && after_string)
    {
        char suffix[4];
        uint32_t suffix_length = 0;
        while (suffix_length < 4 && (c_char_properties[source_byte(source, begin, length, position + suffix_length) & 0x7F] & CHAR_IDENTIFIER) &&
               source_byte(source, begin, length, position + suffix_length) < 0x80)
        {
            suffix[suffix_length] = (char)source_byte(source, begin, length, position + suffix_length);
            suffix_length += 1;
        }
        for (uint32_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i += 1)
        {
            result |= strlen(suffixes[i]) == suffix_length && memcmp(suffixes[i], suffix, suffix_length) == 0;
        }
    }
    return result;
}

// Is there a trigraph ("??" and one of =/'()!<>-) in the source?
static bool contains_trigraph(const uint8_t* source, uint64_t length)
{
    const uint8_t* end = source + length;
    for (const uint8_t* p = source; p + 2 < end;)
    {
        const uint8_t* question = memchr(p, '?', (uint64_t)(end - p - 2));
        if (question == 0)
        {
            break;
        }
        if (question[1] == '?' && memchr("=/'()!<>-", question[2], 9) != 0)
        {
            return true;
        }
        p = question + 1;
    }
    return false;
}

// Loads the 64 bytes at `lane` (relative to the 64-byte-aligned `base`), reading the bytes outside of
// [`begin`, `length`) of the source (which starts at `base + base_offset`) as newlines.
INLINE __m512i load_lane(const uint8_t* base, int64_t base_offset, uint64_t begin, uint64_t length, int64_t lane)
{
    const int64_t first = -base_offset - lane + (int64_t)begin; // The first valid byte of the lane.
    const int64_t end = -base_offset - lane + (int64_t)length; // Past the last valid byte.
    __m512i result;
    if (LIKELY(first <= 0 && end >= 64))
    {
        result = _mm512_load_si512(base + lane);
    }
    else
    {
        const uint64_t below_end = end <= 0 ? 0 : end >= 64 ? ~0ull : _bzhi_u64(~0ull, (uint32_t)end);
        const uint64_t from_first = first <= 0 ? ~0ull : first >= 64 ? 0 : ~_bzhi_u64(~0ull, (uint32_t)first);
        result = _mm512_mask_loadu_epi8(splat8('\n'), below_end & from_first, base + lane);
    }
    return result;
}

// The bits of a lane at or past the end of the source.
INLINE uint64_t lane_padding(int64_t base_offset, uint64_t length, int64_t lane)
{
    const int64_t end = -base_offset - lane + (int64_t)length;
    return end <= 0 ? ~0ull : end >= 64 ? 0 : ~_bzhi_u64(~0ull, (uint32_t)end);
}

// ---------------------------------------------------------------------------------------------------------------------

// The character-class bitstrings that Phase I makes, as `o[mask][lane]`. Lane 8 is the next chunk's first lane.
enum
{
    M_NEWLINE, // \n \r, and the bytes past the end of the source
    M_WHITESPACE, // ' ' \t \v \f \0
    M_IDENTIFIER, // [A-Za-z0-9_$]
    M_DIGIT,
    M_SEPARATOR_FOLLOWER, // [A-Za-z0-9_]: what may follow a digit separator
    M_QUOTE,
    M_APOSTROPHE,
    M_BACKSLASH,
    M_SLASH,
    M_STAR,
    M_DOT,
    M_EXPSIGN, // a + or - after e, E, p or P
    M_HASHDIG, // the ':' of "%:"
    M_OP2, // the last character of a pair of characters that make a punctuator
    M_OP3, // the last character of "...", "<<=" or ">>="
    M_OP3_CXX, // in C++, the last character of what may be "<=>" or "->*" (if it starts with '<' or '-')
    M_NONASCII,
    NUM_MASKS,
};


// What the chunk starts inside of.
typedef enum State
{
    STATE_OUTSIDE,
    STATE_STRING,
    STATE_CHAR,
    STATE_LINE_COMMENT,
    STATE_BLOCK_COMMENT,
} State;

// The last chunk with a line start (a newline outside of block comments), from which the scalar lexer restarts if a
// later chunk needs it: the tokens on the line before such a chunk may be wrong (a splice can join one with what
// follows).
typedef struct LastLine LastLine;
struct LastLine
{
    u64x8 line_breaks;
    u64x8 token_starts;
    int64_t chunk_position;
    uint64_t first_token; // How many tokens start before the chunk.
    bool valid; // Else the scalar lexer restarts at the beginning.
};

// For a chunk that needs the scalar lexer: it takes over from `restart` (where `written` tokens have been written)
// until `scalar_until` (the end of the chunk).
__attribute__((cold, noinline)) static int64_t needs_scalar(const LastLine* last_line, uint64_t begin, int64_t chunk_position, uint64_t* written, uint64_t* restart,
                            uint64_t* scalar_until)
{
    *restart = begin;
    *written = 0;
    if (last_line->valid)
    {
        const uint32_t line_start = (uint32_t)highest_bit_below(last_line->line_breaks, 512) + 1;
        *restart = (uint64_t)(last_line->chunk_position + line_start);
        *written = last_line->first_token + (uint64_t)_mm512_reduce_add_epi64(_mm512_popcnt_epi64((__m512i)(last_line->token_starts & bits_below(line_start))));
    }
    *scalar_until = (uint64_t)(chunk_position + 512);
    return C_TOKENIZER_ERROR_NEEDS_SCALAR;
}

#define NEEDS_SCALAR needs_scalar(&last_line, begin, chunk_position, written, restart, scalar_until)

// Phase I for one 64-byte vector: its character-class bitstrings, stored to `o[...][lane]`.
INLINE void classify(__m512i raw, uint64_t o[][16], unsigned lane, __m512i* previous_classes, __m512i* previous_pairs, const bool cplusplus)
{
    const uint64_t nonascii = _mm512_movepi8_mask(raw);
    __m512i properties = perm2b(c_char_properties, raw);
    __m512i classes = perm2b(c_operator_classes, raw);
    if (UNLIKELY(nonascii != 0))
    {
        properties = _mm512_maskz_mov_epi8(~nonascii, properties);
        classes = _mm512_maskz_mov_epi8(~nonascii, classes);
    }

    // See c/gen_c_tokenizer_tables.py for the channels.
    const __m512i previous = shift_in_previous(classes, *previous_classes);
    const __m512i allowed = _mm512_mask_blend_epi8(_mm512_cmpeq_epi8_mask(previous, classes), splat8((uint8_t)~SAME_CHARACTER_CHANNELS), splat8(0xFF));
    const __m512i pairs = vpshufb(c_second_char_channels, classes) & vpshufb(c_first_char_channels, previous) & allowed;
    const __m512i triples = vpshufb(cplusplus ? c_third_char_channels_cxx : c_third_char_channels, classes) & shift_in_previous(pairs, *previous_pairs);

    o[M_NEWLINE][lane] = _mm512_test_epi8_mask(properties, splat8(CHAR_NEWLINE));
    o[M_WHITESPACE][lane] = _mm512_test_epi8_mask(properties, splat8(CHAR_WHITESPACE));
    o[M_IDENTIFIER][lane] = _mm512_test_epi8_mask(properties, splat8(CHAR_IDENTIFIER));
    o[M_DIGIT][lane] = _mm512_test_epi8_mask(properties, splat8(CHAR_DIGIT));
    o[M_SEPARATOR_FOLLOWER][lane] = _mm512_test_epi8_mask(properties, splat8(CHAR_SEPARATOR_FOLLOWER));
    o[M_QUOTE][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('"'));
    o[M_APOSTROPHE][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('\''));
    o[M_BACKSLASH][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('\\'));
    o[M_SLASH][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('/'));
    o[M_STAR][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('*'));
    o[M_DOT][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('.'));
    o[M_EXPSIGN][lane] = _mm512_test_epi8_mask(pairs, splat8(CHANNEL_EXPSIGN));
    o[M_HASHDIG][lane] = _mm512_test_epi8_mask(pairs, splat8(CHANNEL_HASHDIG));
    o[M_OP2][lane] = _mm512_test_epi8_mask(pairs, splat8(OP2_CHANNELS));
    o[M_OP3][lane] = _mm512_test_epi8_mask(triples, splat8(CHANNEL_SHIFT | CHANNEL_DOTS));
    if (cplusplus)
    {
        o[M_OP3_CXX][lane] = _mm512_test_epi8_mask(triples, splat8(CHANNEL_EQ | CHANNEL_GT));
    }
    o[M_NONASCII][lane] = nonascii;

    *previous_classes = classes;
    *previous_pairs = pairs;
}

// The kind of the prefixed literal whose prefix starts at `position`: L, u8, u or U and then a quote.
static uint8_t prefixed_literal_kind(const uint8_t* source, uint64_t begin, uint64_t length, int64_t position)
{
    const uint8_t letter = source_byte(source, begin, length, position);
    int64_t opener = position + 1;
    uint8_t prefix = letter == 'L' ? 1 : letter == 'U' ? 4 : 3;
    if (letter == 'u' && source_byte(source, begin, length, position + 1) == '8')
    {
        prefix = 2;
        opener = position + 2;
    }
    const uint8_t base = source_byte(source, begin, length, opener) == '"' ? C_TOKEN_STRING_LITERAL : C_TOKEN_CHAR_CONSTANT;
    return (uint8_t)(base + prefix);
}

// The literals and comments of a chunk, found line by line in parallel: each cursor seeks the next delimiter on its
// line, then the end of what that starts. A block comment can end on a later line; then whatever was found on the
// lines it covers is forgotten, and the line it ends on is scanned again from its end.
typedef struct Scan Scan;
struct Scan
{
    u64x8 literal_starts;
    u64x8 literal_closers;
    u64x8 literal_unterminated; // The newline that ends an unterminated literal.
    u64x8 line_comment_starts;
    u64x8 block_region;
    u64x8 block_ends;
    bool spans_lines; // A block comment spans lines, so some line may have been scanned from the wrong state.
};

INLINE Scan scan_lines(u64x8 cursors, State state, uint32_t block_comment_skip, u64x8 line_break, u64x8 bounds, u64x8 quote_bounds,
                       u64x8 apostrophe_closers, u64x8 apostrophe_bounds, u64x8 line_comment_marks, u64x8 block_comment_marks,
                       u64x8 block_comment_closers)
{
    const u64x8 zero = {0};
    const u64x8 bit_0 = {1, 0, 0, 0, 0, 0, 0, 0};
    Scan result = { .literal_starts = zero, .literal_closers = zero, .literal_unterminated = zero, .line_comment_starts = zero,
                    .block_region = zero, .block_ends = zero, .spans_lines = false };
    u64x8 pseudo_quote = state == STATE_STRING ? bit_0 : zero;
    u64x8 pseudo_apostrophe = state == STATE_CHAR ? bit_0 : zero;
    u64x8 pseudo_block_cursor = zero;
    u64x8 pseudo_block_start = zero;
    if (state == STATE_BLOCK_COMMENT)
    {
        pseudo_block_cursor[0] = 1ull << block_comment_skip;
        pseudo_block_start = bit_0;
    }
    const u64x8 every_line_start = shl(line_break, 1) | bit_0;
    for (;;)
    {
        const u64x8 starts = bounds & ~sub(bounds | line_break, cursors);
        const u64x8 quote_starts = starts & quote_bounds;
        const u64x8 apostrophe_starts = starts & apostrophe_bounds;
        const u64x8 block_opens = starts & block_comment_marks;
        result.line_comment_starts |= starts & line_comment_marks;

        // A literal ends at its closing quote, or (unterminated) at the end of its line.
        u64x8 next_cursors = zero;
        const u64x8 quote_lines = quote_starts | pseudo_quote;
        const u64x8 apostrophe_lines = apostrophe_starts | pseudo_apostrophe;
        if (!is_zero(quote_lines | apostrophe_lines))
        {
            const u64x8 closers = line_break | (quote_bounds & sub(line_break, quote_lines)) | (apostrophe_closers & sub(line_break, apostrophe_lines));
            const u64x8 ends = closers & ~sub(closers, shl(quote_starts | apostrophe_starts, 1) | pseudo_quote | pseudo_apostrophe);
            result.literal_starts |= quote_starts | apostrophe_starts;
            result.literal_closers |= ends & ~line_break;
            result.literal_unterminated |= ends & line_break;
            next_cursors = shl(ends & ~line_break, 1);
        }

        // A block comment ends at the first "*/" whose '*' isn't its own.
        if (!is_zero(block_opens | pseudo_block_start))
        {
            // Several searches can end at the same "*/" (one from each line of a comment that seems to start a
            // comment); one that starts on it must not take part in the subtraction, or the others would skip it.
            // (The search of a "/*" in the last 3 bits starts in the next chunk.)
            const u64x8 searches = shl(block_opens, 3) | pseudo_block_cursor;
            const u64x8 ends = (block_comment_closers & ~sub(block_comment_closers, searches & ~block_comment_closers)) | (searches & block_comment_closers);
            const u64x8 region = sub(ends, block_opens | pseudo_block_start) | block_opens | pseudo_block_start | ends;
            if (!is_zero(region & line_break))
            {
                // The rest of each line a comment ends on, with the newline that ends it. (Two comments can end on
                // the same line, so the searches that start on a newline are kept out of the subtraction, and the
                // subtraction's holes at the other searches are filled in.)
                const u64x8 after_ends = shl(ends, 1);
                const u64x8 rest_of_line = (sub(line_break, after_ends & ~line_break) ^ line_break) | after_ends;
                const u64x8 keep = ~(region | rest_of_line);
                result.literal_starts &= keep;
                result.literal_closers &= keep;
                result.literal_unterminated &= keep;
                result.line_comment_starts &= keep;
                result.block_region &= keep;
                next_cursors &= keep;
                result.spans_lines = true;
            }
            result.block_region |= region;
            result.block_ends |= ends;
            next_cursors |= shl(ends, 1);
            // One cursor per line: a later one on the same line is redone by the first.
            next_cursors &= ~sub(next_cursors | line_break, every_line_start);
        }

        cursors = next_cursors;
        pseudo_quote = zero;
        pseudo_apostrophe = zero;
        pseudo_block_cursor = zero;
        pseudo_block_start = zero;
        if (is_zero(cursors))
        {
            break;
        }
    }
    return result;
}

// Tokenizes the source from `begin` (the start of the source, or of a line) to its end, or until a chunk that needs
// the scalar lexer (see NEEDS_SCALAR).
INLINE int64_t tokenize_chunks(const bool cplusplus, const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* restrict kinds_out,
                               uint32_t* restrict starts_out, uint32_t* restrict ends_out, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    const u64x8 zero = {0};
    const u64x8 bit_0 = {1, 0, 0, 0, 0, 0, 0, 0};
    const u64x8 most_significant = {0, 0, 0, 0, 0, 0, 0, 1ull << 63};
    const u64x8 odd_bits = 0xAAAAAAAAAAAAAAAAull;

    // The source is read in 64-byte-aligned lanes; the bytes before `begin` and after the source read as newlines.
    // Positions are relative to the source, so the first chunk can start at a negative one.
    const uint8_t* const base = (const uint8_t*)((uintptr_t)source & ~(uintptr_t)63);
    const int64_t base_offset = (int64_t)(base - source);
    const int64_t first_chunk = ((int64_t)begin - base_offset) & ~(int64_t)63;

    // What the previous chunk passes on. Bitstrings only matter for their top bits.
    u64x8 previous_identifier = zero;
    u64x8 previous_apostrophe = zero;
    u64x8 previous_star = zero;
    u64x8 previous_number_token = zero;
    u64x8 previous_identifier_token = zero;
    u64x8 previous_unknown_openers = zero;
    u64x8 previous_splice_backslash = zero;
    u64x8 previous_splice_cr = zero;
    u64x8 next_is_escaped = zero; // At bit 0.
    uint32_t number_carry = 0; // A pp-number continues into the chunk.
    State state = STATE_OUTSIDE;
    uint32_t block_comment_skip = 0; // A "*/" in bits below this can't close the block comment the chunk starts in.
    bool pending_start_of_line = true;
    uint64_t covered = 0; // The bits at the start of the chunk that a punctuator from the previous chunk covers,
    uint64_t covered_end = 0; // and the last of them.
    uint64_t suppressed_starts = 0; // The quote of a prefixed literal that starts in the previous chunk,
    uint64_t suppressed_ends = 0; // and the '8' of its "u8" prefix.

    LastLine last_line = { .valid = false };

    uint8_t* cur_kind = kinds_out;
    uint32_t* cur_start = starts_out;
    uint32_t* cur_end = ends_out;

    // Phase I runs a chunk ahead: the masks it stores 8 bytes at a time are loaded as 64-byte bitstrings, which the
    // store buffer can't forward, so they are only loaded an iteration after they're stored. Each buffer holds a
    // chunk's 64-byte vectors and their masks (`masks[...][mask][lane]`), with the next chunk's first lane as lane 8.
    uint64_t masks[2][NUM_MASKS][16] __attribute__((aligned(64)));
    __m512i raws[2][9];
    __m512i previous_classes = _mm512_setzero_si512();
    __m512i previous_pairs = _mm512_setzero_si512();
    for (unsigned j = 0; j < 9; j += 1)
    {
        raws[0][j] = load_lane(base, base_offset, begin, length, first_chunk + 64 * j);
        classify(raws[0][j], masks[0], j, &previous_classes, &previous_pairs, cplusplus);
    }

    for (int64_t chunk = first_chunk, current = 0;; chunk += 512, current ^= 1)
    {
        uint64_t (*o)[16] = masks[current];
        const __m512i* raw = raws[current];
        const int64_t chunk_position = base_offset + chunk; // The source position of bit 0.
        const bool last_chunk = chunk_position + 512 >= (int64_t)length;

        // I. Classification of the next chunk (its lane 0 is this chunk's lane 8).
        if (LIKELY(!last_chunk))
        {
            uint64_t (*next_o)[16] = masks[current ^ 1];
            __m512i* next_raw = raws[current ^ 1];
            next_raw[0] = raw[8];
            for (unsigned m = 0; m < NUM_MASKS; m += 1)
            {
                next_o[m][0] = o[m][8];
            }
            for (unsigned j = 1; j < 9; j += 1)
            {
                next_raw[j] = load_lane(base, base_offset, begin, length, chunk + 512 + 64 * j);
                classify(next_raw[j], next_o, j, &previous_classes, &previous_pairs, cplusplus);
            }
        }
        u64x8 padding = zero;
        uint64_t next_padding = 0;
        if (UNLIKELY(chunk_position + 576 > (int64_t)length))
        {
            for (unsigned j = 0; j < 8; j += 1)
            {
                padding[j] = lane_padding(base_offset, length, chunk + 64 * j);
            }
            next_padding = lane_padding(base_offset, length, chunk + 512);
        }

#define MASK(m) (*(const u64x8*)o[m])
#define NEXT(m) (o[m][8])
        const u64x8 newline = MASK(M_NEWLINE);
        const u64x8 whitespace = MASK(M_WHITESPACE);
        const u64x8 identifier = MASK(M_IDENTIFIER);
        const u64x8 digit = MASK(M_DIGIT);
        const u64x8 separator_follower = MASK(M_SEPARATOR_FOLLOWER);
        const u64x8 quote = MASK(M_QUOTE);
        const u64x8 apostrophe = MASK(M_APOSTROPHE);
        const u64x8 backslash = MASK(M_BACKSLASH);
        const u64x8 slash = MASK(M_SLASH);
        const u64x8 star = MASK(M_STAR);
        const u64x8 dot = MASK(M_DOT);
        const u64x8 exponent_sign = MASK(M_EXPSIGN);
        const u64x8 hash_digraph = MASK(M_HASHDIG);
        const u64x8 pair_ends = MASK(M_OP2);
        const u64x8 triple_ends = MASK(M_OP3);
        const u64x8 nonascii = MASK(M_NONASCII);
        const u64x8 cxx_triple_ends = cplusplus ? MASK(M_OP3_CXX) : zero;

        // II. Analysis

        // Line splices: a backslash right before a newline (not the end of the source). They don't break lines and
        // read as whitespace, which is right unless the characters around one could be part of the same token.
        const u64x8 splice_backslash = backslash & shr_next(newline & ~padding, NEXT(M_NEWLINE) & ~next_padding, 1);
        u64x8 splice = zero;
        u64x8 joining_splices = zero; // Splices between characters that would make one token outside of literals and comments,
        u64x8 closing_splices = zero; // and between the '*' and '/' that would end a block comment.
        if (UNLIKELY(!is_zero(splice_backslash | previous_splice_backslash | previous_splice_cr)))
        {
            u64x8 cr;
            for (unsigned j = 0; j < 8; j += 1)
            {
                cr[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('\r'));
            }
            const uint64_t next_cr = _mm512_cmpeq_epi8_mask(raw[8], splat8('\r'));
            const u64x8 line_feed = newline & ~padding & ~cr;
            const u64x8 first_newline = shl_carry(splice_backslash, previous_splice_backslash, 1);
            const u64x8 second_newline = shl_carry(first_newline & cr, previous_splice_cr, 1) & line_feed;
            // In a splice, "\n\r" is one newline.
            if (!is_zero(first_newline & line_feed & shr_next(cr, next_cr, 1)))
            {
                return NEEDS_SCALAR;
            }
            splice = splice_backslash | first_newline | second_newline;
            for (unsigned j = 0; j < 8; j += 1)
            {
                for (uint64_t bits = splice_backslash[j]; bits != 0; bits &= bits - 1)
                {
                    const int64_t position = chunk_position + 64 * j + __builtin_ctzll(bits);
                    const uint8_t before = source_byte(source, begin, length, position - 1);
                    const bool crlf = source_byte(source, begin, length, position + 1) == '\r' && source_byte(source, begin, length, position + 2) == '\n';
                    const uint8_t after = source_byte(source, begin, length, position + (crlf ? 3 : 2));
                    // A splice inside an escape sequence, or after an apostrophe (which can make it a digit
                    // separator, or an empty character constant).
                    if (before == '\\' || before == '\'')
                    {
                        return NEEDS_SCALAR;
                    }
                    if (splice_joins(source_byte(source, begin, length, position - 2), before, after, cplusplus))
                    {
                        joining_splices[j] |= bits & -bits;
                    }
                    if (before == '*' && (after == '/' || after == '\\'))
                    {
                        closing_splices[j] |= bits & -bits;
                    }
                }
            }
            previous_splice_backslash = splice_backslash & most_significant;
            previous_splice_cr = first_newline & cr & most_significant;
        }

        // A backslash, whitespace and a newline is a line splice too (that's rare).
        const u64x8 backslash_whitespace = backslash & shr_next(whitespace, NEXT(M_WHITESPACE), 1);
        if (UNLIKELY(!is_zero(backslash_whitespace)))
        {
            for (unsigned j = 0; j < 8; j += 1)
            {
                for (uint64_t bits = backslash_whitespace[j]; bits != 0; bits &= bits - 1)
                {
                    int64_t p = chunk_position + 64 * j + __builtin_ctzll(bits) + 1;
                    while (p < (int64_t)length && (source[p] == ' ' || source[p] == '\t' || source[p] == '\v' || source[p] == '\f'))
                    {
                        p += 1;
                    }
                    if (p < (int64_t)length && (source[p] == '\n' || source[p] == '\r'))
                    {
                        return NEEDS_SCALAR;
                    }
                }
            }
        }

        const u64x8 line_break = newline & ~splice;

        // Matches the character after any odd number of backslashes (simdjson's algorithm by John Keiser).
        u64x8 escaped;
        {
            const u64x8 escape_backslash = backslash & ~splice_backslash;
            const u64x8 potential_escape = escape_backslash & ~next_is_escaped;
            const u64x8 maybe_escaped = shl(potential_escape, 1);
            const u64x8 even_series_codes_and_odd_bits = sub(maybe_escaped | odd_bits, potential_escape);
            const u64x8 escape_and_terminal_code = even_series_codes_and_odd_bits ^ odd_bits;
            escaped = escape_and_terminal_code ^ (escape_backslash | next_is_escaped);
            next_is_escaped = (u64x8){bit_511(escape_and_terminal_code & escape_backslash), 0, 0, 0, 0, 0, 0, 0};
        }

        // pp-numbers: a digit (or a '.' and a digit) that doesn't continue an identifier, and everything that
        // continues it: identifier characters, '.', a sign after e/E/p/P and a digit separator.
        const u64x8 apostrophe_separator = apostrophe & shr_next(separator_follower, NEXT(M_SEPARATOR_FOLLOWER), 1);
        const u64x8 number_sign = exponent_sign & ~shl_carry(apostrophe, previous_apostrophe, 2); // Not "1'e+".
        const u64x8 number_continue = identifier | dot | number_sign | apostrophe_separator;
        const u64x8 number_head = (digit & ~shl_carry(identifier, previous_identifier, 1)) | (dot & shr_next(digit, NEXT(M_DIGIT), 1));
        uint32_t number_carry_out;
        const u64x8 number_sum = add_carry(number_continue, number_head, number_carry, &number_carry_out);
        const u64x8 number_region = ((number_sum ^ number_continue) | number_head) & number_continue;

        // Delimiters of literals and comments. A digit separator isn't a quote. Comment delimiters are marked at
        // their '/': the first character of "//" and "/*", and the last of "*/".
        const u64x8 quote_bounds = quote & ~escaped;
        const u64x8 apostrophe_closers = apostrophe & ~escaped;
        const u64x8 apostrophe_bounds = apostrophe_closers & ~number_region;
        const u64x8 line_comment_marks = slash & shr_next(slash, NEXT(M_SLASH), 1);
        const u64x8 block_comment_marks = slash & shr_next(star, NEXT(M_STAR), 1);
        const u64x8 block_comment_closers = slash & shl_carry(star, previous_star, 1);
        const uint64_t next_closers = NEXT(M_SLASH) & ((NEXT(M_STAR) << 1) | bit_511(star));
        if (UNLIKELY(!is_zero((block_comment_marks & block_comment_closers) | (block_comment_marks & shr_next(block_comment_closers, next_closers, 2)))))
        {
            return NEEDS_SCALAR; // "*/*" or "/*/"
        }

        // C++'s raw string literals need the scalar lexer.
        if (cplusplus && UNLIKELY(!is_zero(quote & shl_carry(identifier, previous_identifier, 1)) || (bit_511(identifier) && (NEXT(M_QUOTE) & 1))))
        {
            const u64x8 after_identifier = quote & shl_carry(identifier, previous_identifier, 1);
            for (uint32_t lane = 0; lane < 9; lane += 1)
            {
                for (uint64_t bits = lane < 8 ? after_identifier[lane] : NEXT(M_QUOTE) & bit_511(identifier); bits != 0; bits &= bits - 1)
                {
                    if (is_raw_string_quote(source, begin, length, chunk_position + 64 * lane + __builtin_ctzll(bits)))
                    {
                        return NEEDS_SCALAR;
                    }
                }
            }
        }

        // Find the literals and comments, line by line in parallel (see `scan_lines`). The scan starts each line
        // outside of comments; if a block comment spans lines, that can be wrong, so the chunk is scanned again with
        // each line's state from the previous scan until the two agree (which is then the sequential result).
        const u64x8 bounds = quote_bounds | apostrophe_bounds | line_comment_marks | block_comment_marks;
        const u64x8 scan_cursors = shl(line_break, 1) | (state == STATE_OUTSIDE ? bit_0 : zero);
        Scan scan = scan_lines(scan_cursors, state, block_comment_skip, line_break, bounds, quote_bounds, apostrophe_closers, apostrophe_bounds,
                               line_comment_marks, block_comment_marks, block_comment_closers);
        if (UNLIKELY(scan.spans_lines))
        {
            for (unsigned round = 0;; round += 1)
            {
                if (round == 4)
                {
                    return NEEDS_SCALAR;
                }
                // A line that starts inside a block comment is scanned from the end of the comment, if it's on the line.
                const u64x8 inside = shl(line_break & scan.block_region, 1);
                const u64x8 seek = block_comment_closers | line_break;
                const u64x8 first_closers = block_comment_closers & ~sub(seek, inside);
                const Scan rescan = scan_lines((scan_cursors & ~inside) | shl(first_closers, 1), state, block_comment_skip, line_break, bounds, quote_bounds,
                                               apostrophe_closers, apostrophe_bounds, line_comment_marks, block_comment_marks, block_comment_closers);
                const bool same = is_zero((rescan.block_region ^ scan.block_region) & line_break) &&
                                  (bit_511(rescan.block_region) & ~bit_511(rescan.block_ends)) == (bit_511(scan.block_region) & ~bit_511(scan.block_ends));
                scan = rescan;
                if (same)
                {
                    break;
                }
            }
        }
        u64x8 literal_starts = scan.literal_starts;
        const u64x8 literal_closers = scan.literal_closers;
        u64x8 literal_unterminated = scan.literal_unterminated;
        const u64x8 line_comment_starts = scan.line_comment_starts;
        const u64x8 block_region = scan.block_region;
        const u64x8 block_ends = scan.block_ends;

        const u64x8 carried_literal = state == STATE_STRING || state == STATE_CHAR ? bit_0 : zero;
        const u64x8 line_comment_region = sub(line_break, line_comment_starts | (state == STATE_LINE_COMMENT ? bit_0 : zero)) & ~line_break;
        const u64x8 literal_region = sub(literal_closers | literal_unterminated, literal_starts | carried_literal) | literal_closers;
        const u64x8 outside = ~(literal_region | line_comment_region | block_region);

        // What the next chunk starts inside of. A literal open at the end of the chunk is unterminated if the next
        // chunk starts with a newline (the end of the source counts): then it ends here.
        State next_state = STATE_OUTSIDE;
        uint32_t next_block_comment_skip = 0;
        bool unterminated_at_end = false;
        if (bit_511(block_region) && !bit_511(block_ends))
        {
            if (last_chunk)
            {
                return NEEDS_SCALAR; // An unterminated block comment.
            }
            next_state = STATE_BLOCK_COMMENT;
            // A "*/" can't use the '*' of a "/*" that starts at bit 509, 510 or 511.
            const uint64_t opener_bits = (block_comment_marks[7] & block_region[7]) >> 61;
            next_block_comment_skip = opener_bits == 0 ? 0 : 63 - (uint32_t)__builtin_clzll(opener_bits);
        }
        else if (bit_511(literal_region) && !bit_511(literal_closers))
        {
            const int32_t last_start = highest_bit_below(literal_starts, 512);
            const bool is_char = last_start >= 0 ? ((apostrophe[last_start >> 6] >> (last_start & 63)) & 1) != 0 : state == STATE_CHAR;
            next_state = is_char ? STATE_CHAR : STATE_STRING;
            // (The newline that starts the next chunk is part of a splice if this chunk ends with its backslash, or
            // with its "\\\r" and the newline is the '\n' after that.)
            const bool next_splice_newline = bit_511(splice_backslash) ||
                                             (bit_511(previous_splice_cr) && (_mm512_cmpeq_epi8_mask(raw[8], splat8('\n')) & ~next_padding & 1));
            if ((NEXT(M_NEWLINE) & 1) && !next_splice_newline)
            {
                next_state = STATE_OUTSIDE;
                unterminated_at_end = true;
            }
        }
        else if (bit_511(line_comment_region))
        {
            next_state = STATE_LINE_COMMENT;
        }

        // Inputs the bitstrings don't model.
        if (UNLIKELY(!is_zero(((nonascii | joining_splices) & outside) | (closing_splices & block_region))))
        {
            return NEEDS_SCALAR;
        }
        // A stray backslash is a one-character unknown token, unless it starts a UCN (\\u, \\U or \\N) or comes before
        // a quote or another backslash (which the escape bitstring would have it escape).
        const u64x8 stray_backslash = backslash & ~splice_backslash & outside;
        if (UNLIKELY(!is_zero(stray_backslash)))
        {
            u64x8 risky;
            for (unsigned j = 0; j < 8; j += 1)
            {
                risky[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('u')) | _mm512_cmpeq_epi8_mask(raw[j], splat8('U')) |
                           _mm512_cmpeq_epi8_mask(raw[j], splat8('N')) | quote[j] | apostrophe[j] | backslash[j];
            }
            const uint64_t next_risky = _mm512_cmpeq_epi8_mask(raw[8], splat8('u')) | _mm512_cmpeq_epi8_mask(raw[8], splat8('U')) |
                                        _mm512_cmpeq_epi8_mask(raw[8], splat8('N')) | NEXT(M_QUOTE) | NEXT(M_APOSTROPHE) | NEXT(M_BACKSLASH);
            if (!is_zero(stray_backslash & shr_next(risky, next_risky, 1)))
            {
                return NEEDS_SCALAR;
            }
        }

        // Literals (by their opening quote) that are unknown tokens: unterminated ones, found by mapping the newline
        // that ends each one back to its quote (if it's in this chunk), and empty character constants.
        u64x8 unknown_openers = literal_starts & apostrophe & shr_next(literal_closers, NEXT(M_APOSTROPHE) & 1, 1);
        if (UNLIKELY(!is_zero(literal_unterminated) || unterminated_at_end))
        {
            bool carried_literal_unknown = false;
            for (uint32_t lane = 0; lane < 9; lane += 1)
            {
                uint64_t bits = lane < 8 ? literal_unterminated[lane] : (uint64_t)unterminated_at_end;
                for (; bits != 0; bits &= bits - 1)
                {
                    const int32_t opener = highest_bit_below(literal_starts, lane * 64 + (uint32_t)__builtin_ctzll(bits));
                    // (The quote of a prefixed literal that starts in the previous chunk belongs to that token.)
                    if (opener >= 0 && !(opener < 64 && ((suppressed_starts >> opener) & 1)))
                    {
                        unknown_openers[opener >> 6] |= 1ull << (opener & 63);
                    }
                    else
                    {
                        carried_literal_unknown = true;
                    }
                }
            }
            // The literal the chunk starts inside of is the first token whose end isn't written yet.
            if (carried_literal_unknown)
            {
                const uint64_t index = (uint64_t)(cur_end - ends_out);
                kinds_out[index] = C_TOKEN_UNKNOWN | (kinds_out[index] & C_TOKEN_START_OF_LINE);
            }
        }
        const u64x8 literal_token_ends = literal_closers | shr(literal_unterminated, 1) | (unterminated_at_end ? most_significant : zero);

        // Multi-character punctuators: candidates from Phase I that start outside of literals, comments and
        // pp-numbers. An ellipsis only needs its first '.' outside of a pp-number: "...5" is "..." and "5", even
        // though ".5" looks like a pp-number.
        const u64x8 number_token_with_dots = number_region & outside;
        const u64x8 covered_bits = {covered, 0, 0, 0, 0, 0, 0, 0};
        const u64x8 candidate = outside & ~number_token_with_dots & ~covered_bits;
        const u64x8 hash_digraphs = shr_next(hash_digraph, NEXT(M_HASHDIG), 1); // The '%' of each "%:".
        u64x8 quads = hash_digraphs & shr_next(hash_digraphs, NEXT(M_HASHDIG) >> 1, 2) & candidate;
        u64x8 triples = shr_next(triple_ends, NEXT(M_OP3), 2) & candidate & ~quads;
        u64x8 pairs = shr_next(pair_ends, NEXT(M_OP2), 1) & candidate & ~triples & ~quads;
        if (cplusplus)
        {
            // "<=>" and "->*": the triple candidates that start with '<' and '-' (rare, so checked one at a time).
            const u64x8 cxx_triple_starts = shr_next(cxx_triple_ends, NEXT(M_OP3_CXX), 2) & candidate & ~quads;
            if (UNLIKELY(!is_zero(cxx_triple_starts)))
            {
                for (uint32_t lane = 0; lane < 8; lane += 1)
                {
                    for (uint64_t bits = cxx_triple_starts[lane]; bits != 0; bits &= bits - 1)
                    {
                        const int64_t position = chunk_position + 64 * lane + __builtin_ctzll(bits);
                        const uint8_t first = source_byte(source, begin, length, position);
                        if (first == (source_byte(source, begin, length, position + 2) == '>' ? '<' : '-'))
                        {
                            triples[lane] |= bits & -bits;
                        }
                    }
                }
                pairs &= ~triples;
            }
            // ".*"
            pairs |= dot & shr_next(star, NEXT(M_STAR), 1) & candidate & ~triples & ~quads;
        }
        u64x8 covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        if (cplusplus && UNLIKELY(!is_zero((pairs | triples | quads) & covers) || bit_511(pairs)))
        {
            // "<::" is '<' and "::" unless a ':' or '>' follows: then its "<:" isn't a candidate.
            u64x8 less;
            u64x8 colon;
            u64x8 colon_or_greater;
            for (unsigned j = 0; j < 8; j += 1)
            {
                less[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('<'));
                colon[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8(':'));
                colon_or_greater[j] = colon[j] | _mm512_cmpeq_epi8_mask(raw[j], splat8('>'));
            }
            const uint64_t next_colon = _mm512_cmpeq_epi8_mask(raw[8], splat8(':'));
            const uint64_t next_colon_or_greater = next_colon | _mm512_cmpeq_epi8_mask(raw[8], splat8('>'));
            pairs &= ~(less & shr_next(colon, next_colon, 1) & shr_next(colon, next_colon, 2) & ~shr_next(colon_or_greater, next_colon_or_greater, 3));
            covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        }
        if (UNLIKELY(!is_zero((pairs | triples | quads) & covers)))
        {
            // Overlapping candidates, like "+++": take the longest from the left, then the next one that is left.
            u64x8 open_pairs = pairs;
            u64x8 open_triples = triples;
            u64x8 open_quads = quads;
            pairs = zero;
            triples = zero;
            quads = zero;
            do
            {
                const u64x8 open_covers = shl(open_pairs, 1) | shl(open_triples, 1) | shl(open_triples, 2) | shl(open_quads, 1) |
                                          shl(open_quads, 2) | shl(open_quads, 3);
                const u64x8 take_pairs = open_pairs & ~open_covers;
                const u64x8 take_triples = open_triples & ~open_covers;
                const u64x8 take_quads = open_quads & ~open_covers;
                pairs |= take_pairs;
                triples |= take_triples;
                quads |= take_quads;
                const u64x8 taken = take_pairs | take_triples | take_quads | shl(take_pairs, 1) | shl(take_triples, 1) | shl(take_triples, 2) |
                                    shl(take_quads, 1) | shl(take_quads, 2) | shl(take_quads, 3);
                open_pairs &= ~taken;
                open_triples &= ~taken;
                open_quads &= ~taken;
            } while (!is_zero(open_pairs | open_triples | open_quads));
            covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        }
        const u64x8 multi_starts = pairs | triples | quads;
        const u64x8 multi_ends = shl(pairs, 1) | shl(triples, 2) | shl(quads, 3);
        // A punctuator that crosses into the next chunk covers its first bits.
        uint64_t next_covered = 0;
        uint64_t next_covered_end = 0;
        {
            uint32_t overhang = 0; // How many characters of a punctuator starting at bit 509, 510 or 511 are past bit 511.
            if (bit_511(pairs))
            {
                overhang = 1;
            }
            else if ((triples[7] >> 62) != 0)
            {
                overhang = (uint32_t)(triples[7] >> 62) == 1 ? 1 : 2;
            }
            else if ((quads[7] >> 61) != 0)
            {
                overhang = 64 - (uint32_t)__builtin_clzll(quads[7] >> 61);
            }
            if (overhang != 0)
            {
                next_covered = _bzhi_u64(~0ull, overhang);
                next_covered_end = 1ull << (overhang - 1);
            }
        }

        // pp-numbers and identifiers (an identifier character in a pp-number is part of it).
        const u64x8 ellipses = triples & dot;
        const u64x8 number_token = number_token_with_dots & ~(ellipses | shl(ellipses, 1) | shl(ellipses, 2)) & ~covered_bits;
        const u64x8 number_starts = number_token & ~shl_carry(number_token, previous_number_token, 1);
        if (UNLIKELY(!is_zero(number_starts & ~(digit | dot))))
        {
            return NEEDS_SCALAR; // A digit separator that is really a quote, as in 'a'b'c'.
        }
        const uint64_t next_number_continues = (NEXT(M_IDENTIFIER) | NEXT(M_DOT) | (NEXT(M_EXPSIGN) & ~((apostrophe[7] >> 62) & 1)) |
                                                (NEXT(M_APOSTROPHE) & (NEXT(M_SEPARATOR_FOLLOWER) >> 1))) & 1;
        const u64x8 number_ends = number_token & ~shr_next(number_token, next_number_continues, 1);
        const u64x8 identifier_token = identifier & outside & ~number_token;
        const u64x8 identifier_starts = identifier_token & ~shl_carry(identifier_token, previous_identifier_token, 1);
        u64x8 identifier_ends = identifier_token & ~shr_next(identifier_token, NEXT(M_IDENTIFIER) & 1, 1);
        identifier_ends[0] &= ~suppressed_ends;

        // Every other character outside of literals and comments is a one-character token.
        const u64x8 other = ~(identifier | whitespace | newline | quote | apostrophe | backslash | nonascii);
        const u64x8 singles = (other | stray_backslash) & outside & ~number_token & ~multi_starts & ~covers & ~covered_bits;

        // Literals with a prefix (L, u8, u or U) start at the prefix, which would otherwise be an identifier.
        u64x8 literal_starts_token = literal_starts;
        literal_starts_token[0] &= ~suppressed_starts;
        u64x8 prefix_starts = zero;
        u64x8 unknown_starts = zero;
        uint64_t next_suppressed_starts = 0;
        uint64_t next_suppressed_ends = 0;
        const uint64_t next_quotes = (NEXT(M_QUOTE) | NEXT(M_APOSTROPHE)) & 3;
        if (UNLIKELY(!is_zero(literal_starts & shl_carry(identifier_token, previous_identifier_token, 1)) || (bit_511(identifier_token) && next_quotes != 0)))
        {
            u64x8 letter;
            u64x8 u;
            u64x8 eight;
            for (unsigned j = 0; j < 8; j += 1)
            {
                letter[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('L')) | _mm512_cmpeq_epi8_mask(raw[j], splat8('U'));
                u[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('u'));
                eight[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('8'));
            }
            letter |= u;
            const uint64_t next_eight = _mm512_cmpeq_epi8_mask(raw[8], splat8('8'));
            const u64x8 one_letter = identifier_starts & letter & shr_next(literal_starts, next_quotes, 1);
            const u64x8 u8 = identifier_starts & u & shr_next(eight, next_eight, 1) & shr_next(literal_starts, next_quotes, 2);
            prefix_starts = one_letter | u8;
            identifier_ends &= ~(one_letter | shl(u8, 1));
            literal_starts_token &= ~(shl(one_letter, 1) | shl(u8, 2));
            // A prefixed literal is unknown if its quote starts one (an empty character constant in the next chunk
            // is "''" there).
            const uint64_t next_unknown = NEXT(M_APOSTROPHE) & (NEXT(M_APOSTROPHE) >> 1);
            unknown_starts = (one_letter & shr_next(unknown_openers, next_unknown, 1)) | (u8 & shr_next(unknown_openers, next_unknown, 2));
            next_suppressed_starts = bit_511(one_letter) | ((u8[7] >> 62) & 1) | (bit_511(u8) << 1);
            next_suppressed_ends = bit_511(u8);
        }
        unknown_starts |= unknown_openers & literal_starts_token;

        u64x8 all_ends = number_ends | identifier_ends | multi_ends | singles | literal_token_ends;
        all_ends[0] |= covered_end;
        u64x8 all_starts = number_starts | identifier_starts | multi_starts | singles | literal_starts_token;
        // C++'s user-defined literal suffixes: an identifier right after a literal's closing quote is part of the
        // literal if it starts with '_' or (after a string literal) is a standard library suffix.
        if (cplusplus)
        {
            const uint64_t next_identifier_start = NEXT(M_IDENTIFIER) & ~NEXT(M_DIGIT) & 1;
            const u64x8 suffixed = literal_closers & ~shl_carry(unknown_openers, previous_unknown_openers, 1) & shr_next(identifier_starts, next_identifier_start, 1);
            if (UNLIKELY(!is_zero(suffixed)))
            {
                for (uint32_t lane = 0; lane < 8; lane += 1)
                {
                    for (uint64_t bits = suffixed[lane]; bits != 0; bits &= bits - 1)
                    {
                        const int64_t position = chunk_position + 64 * lane + __builtin_ctzll(bits);
                        if (is_literal_suffix(source, begin, length, position + 1, source_byte(source, begin, length, position) == '"'))
                        {
                            // The literal ends with the suffix, whose start (maybe in the next chunk) isn't a token's.
                            const uint32_t suffix = lane * 64 + (uint32_t)__builtin_ctzll(bits) + 1;
                            all_ends[lane] &= ~(bits & -bits);
                            if (suffix < 512)
                            {
                                all_starts[suffix >> 6] &= ~(1ull << (suffix & 63));
                            }
                            else
                            {
                                next_suppressed_starts |= 1;
                            }
                        }
                    }
                }
            }
        }
        all_starts[0] &= ~suppressed_starts;

        // The first token after each newline that starts a logical line (not one in a block comment).
        const u64x8 line_starts = line_break & ~block_region;
        const u64x8 seek_in = all_starts | line_starts;
        const u64x8 seek = sub(seek_in, shl(line_starts, 1) | (pending_start_of_line ? bit_0 : zero));
        const u64x8 start_of_line = all_starts & ~seek;
        pending_start_of_line = bit_511(line_starts) || bit_511(seek & ~seek_in);

        if (!is_zero(line_starts))
        {
            last_line = (LastLine){ .line_breaks = line_starts, .token_starts = all_starts, .chunk_position = chunk_position,
                                    .first_token = (uint64_t)(cur_kind - kinds_out), .valid = true };
        }

        // III. Write-out
        uint64_t starts_lanes[8];
        uint64_t ends_lanes[8];
        uint64_t start_of_line_lanes[8];
        uint64_t multi_lanes[8];
        uint64_t long_lanes[8];
        uint64_t dot_number_lanes[8];
        uint64_t prefix_lanes[8];
        uint64_t unknown_lanes[8];
        const u64x8 long_ones = triples | quads;
        const u64x8 dot_numbers = number_starts & dot;
        memcpy(starts_lanes, &all_starts, 64);
        memcpy(ends_lanes, &all_ends, 64);
        memcpy(start_of_line_lanes, &start_of_line, 64);
        memcpy(multi_lanes, &multi_starts, 64);
        memcpy(long_lanes, &long_ones, 64);
        memcpy(dot_number_lanes, &dot_numbers, 64);
        memcpy(prefix_lanes, &prefix_starts, 64);
        memcpy(unknown_lanes, &unknown_starts, 64);

        for (unsigned j = 0; j < 8; j += 1)
        {
            // The kind of each character's token if it starts one, then the exceptions.
            __m512i kinds = perm2b(c_char_kinds, raw[j]);
            kinds = _mm512_mask_mov_epi8(kinds, dot_number_lanes[j], splat8(C_TOKEN_NUMERIC_CONSTANT));
            const uint64_t multi = multi_lanes[j];
            if (multi != 0)
            {
                const __m512i indices = bits_to_indices(multi);
                const uint64_t long_multi = _pext_u64(long_lanes[j], multi);
                const __m128i low = punctuator_kinds(raw[j], raw[j + 1], _mm512_castsi512_si128(indices), (uint16_t)long_multi);
                __m128i high = _mm_setzero_si128();
                // More than 16 multi-character punctuators in 64 bytes is rare (at most 32 fit).
                if (UNLIKELY(__builtin_popcountll(multi) > 16))
                {
                    high = punctuator_kinds(raw[j], raw[j + 1], _mm512_extracti32x4_epi32(indices, 1), (uint16_t)(long_multi >> 16));
                }
                kinds = _mm512_mask_expand_epi8(kinds, multi, _mm512_castsi256_si512(_mm256_inserti128_si256(_mm256_castsi128_si256(low), high, 1)));
            }
            kinds = _mm512_mask_mov_epi8(kinds, start_of_line_lanes[j], _mm512_or_si512(kinds, splat8(C_TOKEN_START_OF_LINE)));
            _mm512_storeu_si512(cur_kind, _mm512_maskz_compress_epi8(starts_lanes[j], kinds));

            const int64_t lane_position = chunk_position + 64 * j;
            // Prefixed and unknown literals (rare).
            for (uint64_t bits = prefix_lanes[j] | unknown_lanes[j]; UNLIKELY(bits != 0); bits &= bits - 1)
            {
                const uint32_t bit = (uint32_t)__builtin_ctzll(bits);
                const uint64_t index = (uint64_t)__builtin_popcountll(starts_lanes[j] & _bzhi_u64(~0ull, bit));
                const uint8_t kind = (unknown_lanes[j] >> bit) & 1 ? C_TOKEN_UNKNOWN : prefixed_literal_kind(source, begin, length, lane_position + bit);
                cur_kind[index] = kind | (cur_kind[index] & C_TOKEN_START_OF_LINE);
            }

            // The starts and ends, 16 at a time: a 64-byte lane has 9 tokens on average, so the first 16 usually do.
            const uint64_t num_starts = (uint64_t)__builtin_popcountll(starts_lanes[j]);
            const uint64_t num_ends = (uint64_t)__builtin_popcountll(ends_lanes[j]);
            const __m512i start_indices = bits_to_indices(starts_lanes[j]);
            const __m512i end_indices = bits_to_indices(ends_lanes[j]);
            const __m512i start_offset = _mm512_set1_epi32((int)(uint32_t)lane_position);
            const __m512i end_offset = _mm512_set1_epi32((int)(uint32_t)(lane_position + 1)); // Exclusive.
            _mm512_storeu_si512(cur_start, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_castsi512_si128(start_indices)), start_offset));
            _mm512_storeu_si512(cur_end, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_castsi512_si128(end_indices)), end_offset));
            if (UNLIKELY((num_starts | num_ends) > 16))
            {
                _mm512_storeu_si512(cur_start + 16, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(start_indices, 1)), start_offset));
                _mm512_storeu_si512(cur_start + 32, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(start_indices, 2)), start_offset));
                _mm512_storeu_si512(cur_start + 48, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(start_indices, 3)), start_offset));
                _mm512_storeu_si512(cur_end + 16, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(end_indices, 1)), end_offset));
                _mm512_storeu_si512(cur_end + 32, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(end_indices, 2)), end_offset));
                _mm512_storeu_si512(cur_end + 48, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(end_indices, 3)), end_offset));
            }

            cur_kind += num_starts;
            cur_start += num_starts;
            cur_end += num_ends;
        }

        previous_identifier = identifier;
        previous_apostrophe = apostrophe;
        previous_star = star;
        previous_number_token = number_token;
        previous_identifier_token = identifier_token;
        previous_unknown_openers = unknown_openers;
        number_carry = number_carry_out;
        state = next_state;
        block_comment_skip = next_block_comment_skip;
        covered = next_covered;
        covered_end = next_covered_end;
        suppressed_starts = next_suppressed_starts;
        suppressed_ends = next_suppressed_ends;
#undef MASK
#undef NEXT

        if (last_chunk)
        {
            break;
        }
    }

    return cur_kind - kinds_out;
}

// The chunk loop, specialized for each language.
__attribute__((noinline)) static int64_t tokenize_c(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                    uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_cpp(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                      uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(true, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

int64_t c_tokenize_vectorized(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    int64_t result;
    if (length > UINT32_MAX - 128)
    {
        result = C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    else if (length == 0)
    {
        result = 0;
    }
    else if ((flags & C_TOKENIZER_TRIGRAPHS) && contains_trigraph(source, length))
    {
        result = C_TOKENIZER_ERROR_NEEDS_SCALAR;
    }
    else
    {
        const uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
        uint64_t written;
        uint64_t restart;
        uint64_t scalar_until;
        result = (flags & C_TOKENIZER_CPLUSPLUS) ? tokenize_cpp(source, length, begin, kinds, starts, ends, &written, &restart, &scalar_until)
                                                 : tokenize_c(source, length, begin, kinds, starts, ends, &written, &restart, &scalar_until);
    }
    return result;
}

int64_t c_tokenize(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    if (length > UINT32_MAX - 128)
    {
        return C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    if (length == 0)
    {
        return 0;
    }
    if ((flags & C_TOKENIZER_TRIGRAPHS) && contains_trigraph(source, length))
    {
        return c_tokenize_scalar(source, length, kinds, starts, ends, flags);
    }

    // The vectorized path, until a chunk it can't tokenize. Then the scalar lexer goes back to the start of the line
    // before that chunk and lexes up to the first line that starts after it, and the vectorized path resumes there.
    uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
    uint64_t count = 0;
    for (;;)
    {
        uint64_t written;
        uint64_t restart;
        uint64_t scalar_until;
        const int64_t result =
            (flags & C_TOKENIZER_CPLUSPLUS)
                ? tokenize_cpp(source, length, begin, kinds + count, starts + count, ends + count, &written, &restart, &scalar_until)
                : tokenize_c(source, length, begin, kinds + count, starts + count, ends + count, &written, &restart, &scalar_until);
        if (result >= 0)
        {
            return (int64_t)count + result;
        }
        count = c_tokenize_scalar_range(source, length, restart, true, scalar_until, kinds, starts, ends, count + written, flags, &begin);
        if (begin >= length)
        {
            return (int64_t)count;
        }
    }
}
