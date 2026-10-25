// Mixed-source component: see THIRD_PARTY_NOTICES.md for the portion map and LICENSES/ for the retained terms.
// Accelerated-Zig-Parser: MIT, with Apache-2.0 for its identified simdjson/simdjzon-derived portions.
// Copyright (c) 2023, 2024 Niles Salter. Local modifications Copyright (c) 2026 David Gonzalez Martin.
// Clang-following scalar portions retain LLVM's Apache-2.0 WITH LLVM-exception terms; identifier data also retains
// the Unicode notice (Copyright 1991-2026 Unicode, Inc.). Those notices do not extend the LLVM exception to the
// Apache-only portions. Local modifications follow each portion's applicable component terms.
//
// The C lexer: turns unpreprocessed C or C++ source bytes into preprocessing tokens (translation phase 3) with
// AVX-512, falling back to a scalar lexer for the rare lines the vectorized path can't handle. main.c includes this
// file directly, and every function in it is static, but for the entry points that take BUSTER_LEXER_ENTRY, which
// main.c's units compile in one unit alone.
//
// Entry points: c_tokenize (the lexer), c_tokenize_scalar and c_tokenize_vectorized (each path alone, for testing),
// and c_token_kind_name. Token i is source[starts[i] .. ends[i]] with kind kinds[i]. The output arrays need
// BUSTER_C_TOKENIZER_CAPACITY(length) entries, because the vectorized path stores whole vectors past the last token.
// Vector loads never read outside the source: lanes that cross its ends are masked loads (load_lane).
//
// Layout, in this order:
//   - API: CTokenKind (BUSTER_C_TOKEN_KINDS), CTokenizerFlags, CTokenizerError and the prototypes.
//   - Tables: c_char_properties, c_operator_classes, c_char_kinds and the punctuator channel tables, then
//     c_identifier_start_ranges and c_identifier_continue_ranges for non-ASCII identifiers.
//   - Scalar lexer: ScalarLexer, get_char_slow, get_char and peek_char (line splices and trigraphs), the lex_*
//     functions, c_tokenize_scalar_range (a line range, used by c_tokenize) and c_tokenize_scalar.
//   - Vectorized lexer: the U64x8 bitstring helpers, the vector helpers (perm2b, shift_in_previous), the source
//     checks it calls on rare paths (is_literal_suffix, contains_trigraph), load_lane, the masks, State and
//     needs_scalar, property_masks and transpose_property_masks (the GFNI property transposition), classify
//     (Phase I), scan_lines (the delimiter scan), skip_leading_comments, ChunkTokens and write_chunk (Phase III),
//     tokenize_chunks (Phase II, with the BUSTER_LEX_CHUNK_BYTES chunk loop), tokenize_c, tokenize_c_raw and
//     tokenize_cpp, tokenize_with_flags, c_tokenize_vectorized and c_tokenize.
//
// Origin: a single-file version of the C lexer from https://github.com/davidgmbb/Accelerated-Zig-Parser
// (c/c_tokenizer.h, c/c_tokenizer_tables.h, c/c_tokenizer_unicode.h, c/c_tokenizer_scalar.c and c/c_tokenizer.c at
// commit f4482e2dd53413522d75d8c254b9a824021238ce; the "=====" section markers name them), adapted to
// docs/coding-style.md: static functions, BUSTER_-prefixed macros, a single return per function and named chunk
// constants. The tables are generated upstream by c/gen_c_tokenizer_tables.py and c/gen_c_tokenizer_unicode.py; a
// regenerated table needs its macros renamed to the BUSTER_LEX_ names used here, and c_char_properties extended
// with the quote, apostrophe and backslash bits below.
// Local changes (2026-10-19): GFNI property-mask transposition, bounded leading-comment scanning, and shortcuts for
// comment-only chunks and chunks without literal/comment delimiters; then branch-free chunk state, a write-out a
// chunk behind the analysis, a shortcut for chunks without backslashes, multi-character punctuators by table lookup,
// fewer register spills, and a scan that finds the block comment a chunk starts inside of first (which also covers
// what the comment-only chunk shortcut did, so that is gone); then C++'s raw string literals checked at literal
// starts, chunks read in place from the source, the previous chunk's bitstrings carried as their last lanes, and a
// one-permute byte shift; then (2026-10-22) raw string literals in C (C_TOKENIZER_RAW_STRINGS), found at prefixed
// literals' starts by bitstrings; then (2026-10-25) the entry points' linkage from BUSTER_LEXER_ENTRY (#270).
// Measurements: docs/c-lexer.md.
//
// Upstream license (Accelerated-Zig-Parser's LICENSE file, verbatim):
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
// -------------------------------------------------------------------------
//
// The pieces of this software that are derived from simdjson and simdjzon are marked as such in the source code, and are released under the Apache license:
//
//
//                                  Apache License
//                            Version 2.0, January 2004
//                         http://www.apache.org/licenses/
//
//    TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION
//
//    1. Definitions.
//
//       "License" shall mean the terms and conditions for use, reproduction,
//       and distribution as defined by Sections 1 through 9 of this document.
//
//       "Licensor" shall mean the copyright owner or entity authorized by
//       the copyright owner that is granting the License.
//
//       "Legal Entity" shall mean the union of the acting entity and all
//       other entities that control, are controlled by, or are under common
//       control with that entity. For the purposes of this definition,
//       "control" means (i) the power, direct or indirect, to cause the
//       direction or management of such entity, whether by contract or
//       otherwise, or (ii) ownership of fifty percent (50%) or more of the
//       outstanding shares, or (iii) beneficial ownership of such entity.
//
//       "You" (or "Your") shall mean an individual or Legal Entity
//       exercising permissions granted by this License.
//
//       "Source" form shall mean the preferred form for making modifications,
//       including but not limited to software source code, documentation
//       source, and configuration files.
//
//       "Object" form shall mean any form resulting from mechanical
//       transformation or translation of a Source form, including but
//       not limited to compiled object code, generated documentation,
//       and conversions to other media types.
//
//       "Work" shall mean the work of authorship, whether in Source or
//       Object form, made available under the License, as indicated by a
//       copyright notice that is included in or attached to the work
//       (an example is provided in the Appendix below).
//
//       "Derivative Works" shall mean any work, whether in Source or Object
//       form, that is based on (or derived from) the Work and for which the
//       editorial revisions, annotations, elaborations, or other modifications
//       represent, as a whole, an original work of authorship. For the purposes
//       of this License, Derivative Works shall not include works that remain
//       separable from, or merely link (or bind by name) to the interfaces of,
//       the Work and Derivative Works thereof.
//
//       "Contribution" shall mean any work of authorship, including
//       the original version of the Work and any modifications or additions
//       to that Work or Derivative Works thereof, that is intentionally
//       submitted to Licensor for inclusion in the Work by the copyright owner
//       or by an individual or Legal Entity authorized to submit on behalf of
//       the copyright owner. For the purposes of this definition, "submitted"
//       means any form of electronic, verbal, or written communication sent
//       to the Licensor or its representatives, including but not limited to
//       communication on electronic mailing lists, source code control systems,
//       and issue tracking systems that are managed by, or on behalf of, the
//       Licensor for the purpose of discussing and improving the Work, but
//       excluding communication that is conspicuously marked or otherwise
//       designated in writing by the copyright owner as "Not a Contribution."
//
//       "Contributor" shall mean Licensor and any individual or Legal Entity
//       on behalf of whom a Contribution has been received by Licensor and
//       subsequently incorporated within the Work.
//
//    2. Grant of Copyright License. Subject to the terms and conditions of
//       this License, each Contributor hereby grants to You a perpetual,
//       worldwide, non-exclusive, no-charge, royalty-free, irrevocable
//       copyright license to reproduce, prepare Derivative Works of,
//       publicly display, publicly perform, sublicense, and distribute the
//       Work and such Derivative Works in Source or Object form.
//
//    3. Grant of Patent License. Subject to the terms and conditions of
//       this License, each Contributor hereby grants to You a perpetual,
//       worldwide, non-exclusive, no-charge, royalty-free, irrevocable
//       (except as stated in this section) patent license to make, have made,
//       use, offer to sell, sell, import, and otherwise transfer the Work,
//       where such license applies only to those patent claims licensable
//       by such Contributor that are necessarily infringed by their
//       Contribution(s) alone or by combination of their Contribution(s)
//       with the Work to which such Contribution(s) was submitted. If You
//       institute patent litigation against any entity (including a
//       cross-claim or counterclaim in a lawsuit) alleging that the Work
//       or a Contribution incorporated within the Work constitutes direct
//       or contributory patent infringement, then any patent licenses
//       granted to You under this License for that Work shall terminate
//       as of the date such litigation is filed.
//
//    4. Redistribution. You may reproduce and distribute copies of the
//       Work or Derivative Works thereof in any medium, with or without
//       modifications, and in Source or Object form, provided that You
//       meet the following conditions:
//
//       (a) You must give any other recipients of the Work or
//           Derivative Works a copy of this License; and
//
//       (b) You must cause any modified files to carry prominent notices
//           stating that You changed the files; and
//
//       (c) You must retain, in the Source form of any Derivative Works
//           that You distribute, all copyright, patent, trademark, and
//           attribution notices from the Source form of the Work,
//           excluding those notices that do not pertain to any part of
//           the Derivative Works; and
//
//       (d) If the Work includes a "NOTICE" text file as part of its
//           distribution, then any Derivative Works that You distribute must
//           include a readable copy of the attribution notices contained
//           within such NOTICE file, excluding those notices that do not
//           pertain to any part of the Derivative Works, in at least one
//           of the following places: within a NOTICE text file distributed
//           as part of the Derivative Works; within the Source form or
//           documentation, if provided along with the Derivative Works; or,
//           within a display generated by the Derivative Works, if and
//           wherever such third-party notices normally appear. The contents
//           of the NOTICE file are for informational purposes only and
//           do not modify the License. You may add Your own attribution
//           notices within Derivative Works that You distribute, alongside
//           or as an addendum to the NOTICE text from the Work, provided
//           that such additional attribution notices cannot be construed
//           as modifying the License.
//
//       You may add Your own copyright statement to Your modifications and
//       may provide additional or different license terms and conditions
//       for use, reproduction, or distribution of Your modifications, or
//       for any such Derivative Works as a whole, provided Your use,
//       reproduction, and distribution of the Work otherwise complies with
//       the conditions stated in this License.
//
//    5. Submission of Contributions. Unless You explicitly state otherwise,
//       any Contribution intentionally submitted for inclusion in the Work
//       by You to the Licensor shall be under the terms and conditions of
//       this License, without any additional terms or conditions.
//       Notwithstanding the above, nothing herein shall supersede or modify
//       the terms of any separate license agreement you may have executed
//       with Licensor regarding such Contributions.
//
//    6. Trademarks. This License does not grant permission to use the trade
//       names, trademarks, service marks, or product names of the Licensor,
//       except as required for reasonable and customary use in describing the
//       origin of the Work and reproducing the content of the NOTICE file.
//
//    7. Disclaimer of Warranty. Unless required by applicable law or
//       agreed to in writing, Licensor provides the Work (and each
//       Contributor provides its Contributions) on an "AS IS" BASIS,
//       WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//       implied, including, without limitation, any warranties or conditions
//       of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
//       PARTICULAR PURPOSE. You are solely responsible for determining the
//       appropriateness of using or redistributing the Work and assume any
//       risks associated with Your exercise of permissions under this License.
//
//    8. Limitation of Liability. In no event and under no legal theory,
//       whether in tort (including negligence), contract, or otherwise,
//       unless required by applicable law (such as deliberate and grossly
//       negligent acts) or agreed to in writing, shall any Contributor be
//       liable to You for damages, including any direct, indirect, special,
//       incidental, or consequential damages of any character arising as a
//       result of this License or out of the use or inability to use the
//       Work (including but not limited to damages for loss of goodwill,
//       work stoppage, computer failure or malfunction, or any and all
//       other commercial damages or losses), even if such Contributor
//       has been advised of the possibility of such damages.
//
//    9. Accepting Warranty or Additional Liability. While redistributing
//       the Work or Derivative Works thereof, You may choose to offer,
//       and charge a fee for, acceptance of support, warranty, indemnity,
//       or other liability obligations and/or rights consistent with this
//       License. However, in accepting such obligations, You may act only
//       on Your own behalf and on Your sole responsibility, not on behalf
//       of any other Contributor, and only if You agree to indemnify,
//       defend, and hold each Contributor harmless for any liability
//       incurred by, or claims asserted against, such Contributor by reason
//       of your accepting any such warranty or additional liability.
//
//    END OF TERMS AND CONDITIONS
//
//    APPENDIX: How to apply the Apache License to your work.
//
//       To apply the Apache License to your work, attach the following
//       boilerplate notice, with the fields enclosed by brackets "[]"
//       replaced with your own identifying information. (Don't include
//       the brackets!)  The text should be enclosed in the appropriate
//       comment syntax for the file format. We also recommend that a
//       file or class name and description of purpose be included on the
//       same "printed page" as the copyright notice for easier
//       identification within third-party archives.
//
//    Copyright [yyyy] [name of copyright owner]
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

// ===== c_tokenizer.h =====
// A compacting AVX-512 tokenizer for C and C++ source code that hasn't been preprocessed yet: it produces the
// preprocessing tokens of translation phase 3 (after line splicing and, optionally, trigraph replacement), for a
// preprocessor to run on afterwards. It is the 512-byte-chunk design of src/CompactingTokenizer.zig applied to C.
//
// Every kind of source is tokenized exactly like clang's raw lexer in C23 mode (`clang -cc1 -std=c23
// -dump-raw-tokens`), or in C++26 mode with `C_TOKENIZER_CPLUSPLUS` (`clang -cc1 -x c++ -std=c++26
// -dump-raw-tokens`): line splices anywhere, digraphs, UCNs and UTF-8 identifiers, digit separators, unterminated
// literals and stray characters, and in C++ raw string literals, user-defined literal suffixes (which are part of
// the literal's token), `<=>`, `->*`, `.*` and the `<::` rule. With `C_TOKENIZER_RAW_STRINGS`, C has raw string
// literals too (`clang -cc1 -std=gnu23 -dump-raw-tokens`, or `-std=gnu99` without `C_TOKENIZER_UTF_RAW_STRINGS`).
// Common code takes the vectorized path; the rare lines it can't handle exactly (e.g. a line splice in the middle of
// a token, a non-ASCII identifier or a raw string literal) are tokenized by a scalar lexer. With
// `C_TOKENIZER_NO_DOLLARS` the scalar lexer takes the whole source and lexes `$` as punctuation, as
// `-fno-dollars-in-identifiers` and `-x assembler-with-cpp` have it.
// the literal's token), `<=>`, `->*`, `.*` and the `<::` rule. With `C_TOKENIZER_NO_DIGRAPHS` and
// `C_TOKENIZER_NO_LINE_COMMENTS`, C is lexed without digraphs or without `//` comments, as clang's `-E` lexes
// `-std=c89` (neither) and `-std=iso9899:199409` (no `//` comments). Common code takes the vectorized path; the rare
// lines it can't handle exactly (e.g. a line splice in the middle of a token, a non-ASCII identifier or a raw string
// literal) are tokenized by a scalar lexer. With `C_TOKENIZER_NO_DOLLARS` the scalar lexer takes the whole source and
// lexes `$` as punctuation, as `-fno-dollars-in-identifiers` and `-x assembler-with-cpp` have it.
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

// The linkage of the entry points that main.c's units compile in one of them, their home, and call there (main.c): a
// unit of its own, such as a benchmark's, has them static, as every other function here.
#ifndef BUSTER_LEXER_ENTRY
#define BUSTER_LEXER_ENTRY static
#endif

// The kinds of preprocessing tokens. Each name is the clang token kind it matches (e.g. `C_TOKEN_STRING_LITERAL` is
// clang's `string_literal`). The prefixed character constants and string literals follow their unprefixed kind in
// the order L, u8, u, U (a raw string literal has the kind of its prefix without the R). The last three are C++'s.
#define BUSTER_C_TOKEN_KINDS(X) \
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
#define BUSTER_C_TOKEN_ENUM(name, clang_name) C_TOKEN_##name,
    BUSTER_C_TOKEN_KINDS(BUSTER_C_TOKEN_ENUM)
#undef BUSTER_C_TOKEN_ENUM
    C_TOKEN_KIND_COUNT,
} CTokenKind;

// Set in a token's kind byte if it's the first token of a logical source line (comments don't count as tokens), so a
// `#` with this bit starts a preprocessing directive, which ends before the next token with this bit.
#define BUSTER_C_TOKEN_START_OF_LINE 0x80

typedef enum CTokenizerFlags
{
    // Replace trigraphs (`??=` is `#`, ...) in translation phase 1, as C17 and C++14 and earlier require. Off by
    // default, like C23, C++17 and clang's GNU modes.
    C_TOKENIZER_TRIGRAPHS = 1,
    // Tokenize C++ (C++26) instead of C.
    C_TOKENIZER_CPLUSPLUS = 2,
    // End pp-numbers before an apostrophe, as C17 and C++11 and earlier do.
    C_TOKENIZER_NO_DIGIT_SEPARATORS = 4,
    // '$' is punctuation (a C_TOKEN_UNKNOWN token of its own) rather than an identifier and pp-number character, as
    // clang has it with -fno-dollars-in-identifiers and for assembler-with-cpp (its DollarIdents off). The scalar
    // lexer's alone: c_tokenize gives such a source to it, and c_tokenize_vectorized refuses it. 128 keeps it apart from
    // the flags other branches add (#278's 4, #285's 8 and 16, #292's 32 and 64).
    C_TOKENIZER_NO_DOLLARS = 128,
    // Lex raw string literals in C (C++ always has them): R"d(...)d" and LR"d(...)d", as clang's GNU modes from C99 on
    // and -fraw-string-literals have them, without C++'s user-defined suffix. Off by default, like clang's ISO modes.
    C_TOKENIZER_RAW_STRINGS = 8,
    // With C_TOKENIZER_RAW_STRINGS, their u8R, uR and UR forms too, which need C11's u8, u and U prefixes.
    C_TOKENIZER_UTF_RAW_STRINGS = 16,
    // Lex without digraphs, as clang's `-std=c89` and `-fno-digraphs` do: `<:`, `:>`, `<%`, `%>` and `%:` are two
    // punctuators each (`<%=` is `<` and `%=`, and `%:%:` four tokens).
    C_TOKENIZER_NO_DIGRAPHS = 32,
    // Lex without `//` comments, as clang's `-E` does in `-std=c89` and `-std=iso9899:199409`: `//` is two `/`, and what
    // follows them on the line is lexed (`a //* c */ b` is `a`, `/` and `b`).
    C_TOKENIZER_NO_LINE_COMMENTS = 64,
} CTokenizerFlags;

typedef enum CTokenizerError
{
    C_TOKENIZER_ERROR_FILE_TOO_BIG = -1, // Token positions are 32-bit.
    C_TOKENIZER_ERROR_NEEDS_SCALAR = -2, // Only from c_tokenize_vectorized (and always with C_TOKENIZER_NO_DOLLARS).
} CTokenizerError;

// The spare entries past the last token that the vectorized path's whole-vector stores of kinds, starts and ends may
// write. Sources are also limited to UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK bytes, so positions fit in 32 bits.
#define BUSTER_C_TOKENIZER_OUTPUT_SLACK 128
// The number of entries `kinds`, `starts` and `ends` must each have room for when tokenizing `length` bytes.
#define BUSTER_C_TOKENIZER_CAPACITY(length) ((length) + BUSTER_C_TOKENIZER_OUTPUT_SLACK)

// Tokenizes `length` bytes of C source. Token `i` is `source[starts[i] .. ends[i]]`, of kind `kinds[i] &
// ~BUSTER_C_TOKEN_START_OF_LINE` (a `CTokenKind`). Returns the number of tokens, or a negative `CTokenizerError`.
// `source` needs no padding or terminator, and `kinds`, `starts` and `ends` need `BUSTER_C_TOKENIZER_CAPACITY(length)`
// entries. The three output arrays must not overlap each other or `source` (the vectorized path writes them through
// restrict pointers).
BUSTER_LEXER_ENTRY __attribute__((unused)) int64_t c_tokenize(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// c_tokenize of the bytes from `begin` to `length` alone, as if the source ended at `length`: `begin` is 0 or the start
// of a token that starts a logical line (BUSTER_C_TOKEN_START_OF_LINE). Every token before the last one that starts a
// line is then the token c_tokenize of the whole source gives (a construct the end cuts, such as a block comment or a
// raw string literal, runs to the end, as at the source's), so a large source can be lexed a window at a time
// (pp_prepare_into). Not for a source that takes the scalar lexer whole (c_tokenize_scalar_only of the whole source):
// those go to c_tokenize. Returns the number of tokens, written from kinds[0], starts[0] and ends[0] on (positions are
// the source's), or C_TOKENIZER_ERROR_FILE_TOO_BIG; the arrays need BUSTER_C_TOKENIZER_CAPACITY(length - begin) entries.
BUSTER_LEXER_ENTRY __attribute__((unused)) int64_t c_tokenize_lines(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts, uint32_t* ends,
                                                        uint32_t flags);

// c_tokenize without the scalar lexer: returns C_TOKENIZER_ERROR_NEEDS_SCALAR for the rare inputs that need it for a
// part (see c_tokenizer.c), with the output arrays clobbered. For testing and benchmarking.
BUSTER_LEXER_ENTRY __attribute__((unused)) int64_t c_tokenize_vectorized(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// The same tokenization by a straightforward scalar lexer (which `c_tokenize` uses for the rare parts of a source
// that its vectorized path doesn't handle). It writes exactly the returned number of tokens.
BUSTER_LEXER_ENTRY __attribute__((unused)) int64_t c_tokenize_scalar(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags);

// The clang name of a token kind (without the start-of-line bit), e.g. "raw_identifier" or "plusequal".
static __attribute__((unused)) const char* c_token_kind_name(uint8_t kind);

#if !defined(__clang__)
#error "c_lexer.c uses Clang vector extensions"
#endif
#if !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512VBMI__) && \
      defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) && defined(__GFNI__) && defined(__BMI2__))
#error "c_lexer.c needs AVX-512 F/BW/VL/VBMI/VBMI2/VPOPCNTDQ, GFNI and BMI2 (Zen 4 and later, Intel since Ice Lake)"
#endif

#include <immintrin.h>
#include <stdbool.h>
#include <string.h>

#define BUSTER_INLINE static inline __attribute__((always_inline))
#define BUSTER_LIKELY(x) __builtin_expect(!!(x), 1)
#define BUSTER_UNLIKELY(x) __builtin_expect(!!(x), 0)

// ===== c_tokenizer_tables.h =====
// Generated by c/gen_c_tokenizer_tables.py: the lookup tables of the vectorized C tokenizer.

#include <stdint.h>

#define BUSTER_LEX_CHAR_IDENTIFIER 0x01
#define BUSTER_LEX_CHAR_DIGIT 0x02
#define BUSTER_LEX_CHAR_WHITESPACE 0x04
#define BUSTER_LEX_CHAR_NEWLINE 0x08
#define BUSTER_LEX_CHAR_SEPARATOR_FOLLOWER 0x10
#define BUSTER_LEX_CHAR_QUOTE 0x20
#define BUSTER_LEX_CHAR_APOSTROPHE 0x40
#define BUSTER_LEX_CHAR_BACKSLASH 0x80

#define BUSTER_LEX_CHANNEL_EQ 0x01
#define BUSTER_LEX_CHANNEL_SELF 0x02
#define BUSTER_LEX_CHANNEL_SHIFT 0x04
#define BUSTER_LEX_CHANNEL_DOTS 0x08
#define BUSTER_LEX_CHANNEL_GT 0x10
#define BUSTER_LEX_CHANNEL_LTDIG 0x20
#define BUSTER_LEX_CHANNEL_HASHDIG 0x40
#define BUSTER_LEX_CHANNEL_EXPSIGN 0x80

// Indexed by ASCII character. Bit i becomes property mask i (M_IDENTIFIER through M_BACKSLASH).
static const uint8_t c_char_properties[128] __attribute__((aligned(64))) =
{
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x04, 0x04, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x80, 0x00, 0x00, 0x11,
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// Without digit separators no byte has BUSTER_LEX_CHAR_SEPARATOR_FOLLOWER (#264).
static const uint8_t c_char_properties_no_separators[128] __attribute__((aligned(64))) =
{
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x04, 0x04, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x80, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
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
// c_first_char_channels without digraphs (C_TOKENIZER_NO_DIGRAPHS; written here, not generated): '%' (class 6) without
// BUSTER_LEX_CHANNEL_GT and BUSTER_LEX_CHANNEL_HASHDIG, '<' (class 7) without BUSTER_LEX_CHANNEL_LTDIG and ':' (class 11)
// without BUSTER_LEX_CHANNEL_GT, so that no "%>", "%:", "<:", "<%" or ":>" pairs, and so no "%:%:" either.
static const uint8_t c_first_char_channels_no_digraphs[16] __attribute__((aligned(64))) =
{
    0x00, 0x01, 0x01, 0x03, 0x03, 0x13, 0x01, 0x05, 0x05, 0x03, 0x02, 0x02, 0x08, 0x80, 0x00, 0x00,
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

// Indexed by (5 * c0 + 4 * c1) mod 128, the kind of the 2-character punctuator c0 c1. The index is distinct for each
// of the 26 pairs whose channels (c_first_char_channels and c_second_char_channels) make a 2-character punctuator,
// and for C++'s ".*"; it was found by a search over small multipliers (docs/c-lexer.md), and the boundary snippets of
// c_lexer_test.c contain every pair.
static const uint8_t c_pair_kinds[128] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x00, 0x3E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0D, 0x1F, 0x00, 0x00, 0x00, 0x23, 0x0E, 0x00, 0x29, 0x00, 0x00, 0x00,
    0x2A, 0x3C, 0x00, 0x00, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x27, 0x2D, 0x00,
    0x00, 0x12, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x31, 0x1C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x16, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x33, 0x00, 0x00, 0x25,
    0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Indexed by (5 * c0 + 4 * c1) mod 64, like c_pair_kinds, the kind of the 3- or 4-character punctuator that starts
// with c0 c1: "...", "<<=", ">>=", C++'s "->*" and "<=>", and "%:%:", whose first two characters tell them apart.
static const uint8_t c_long_kinds[64] __attribute__((aligned(64))) =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x2B, 0x00, 0x14, 0x00,
    0x40, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif
#define BUSTER_LEX_END_OF_FILE (-1)
// The code point of a named UCN (`\N{...}`) whose name looks valid. See the header.
#define BUSTER_LEX_NAMED_UCN 0xFFFFFFFFu

typedef struct ScalarLexer ScalarLexer;
struct ScalarLexer
{
    const uint8_t* source;
    uint64_t length;
    bool trigraphs;
    bool cplusplus;
    // clang's RawStringLiterals: R and LR start raw string literals (C++, and C_TOKENIZER_RAW_STRINGS), and so do u8R,
    // uR and UR where the u8, u and U prefixes have them (C++, and C_TOKENIZER_UTF_RAW_STRINGS).
    bool raw_strings;
    bool utf_raw_strings;
    // C_TOKENIZER_NO_DOLLARS: '$' takes no part in identifiers and pp-numbers (false, the default, as clang's C has it).
    bool no_dollars;
    bool no_digraphs; // C_TOKENIZER_NO_DIGRAPHS
    bool no_line_comments; // C_TOKENIZER_NO_LINE_COMMENTS
    bool no_digit_separators; // C_TOKENIZER_NO_DIGIT_SEPARATORS
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
    bool found = false;
    while (!found && low < high)
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
            found = true;
        }
    }
    return found;
}

static bool is_identifier_start_code_point(uint32_t code_point)
{
    return code_point == BUSTER_LEX_NAMED_UCN || code_point_in(c_identifier_start_ranges, BUSTER_ARRAY_LENGTH(c_identifier_start_ranges), code_point);
}

static bool is_identifier_continue_code_point(uint32_t code_point)
{
    return code_point == BUSTER_LEX_NAMED_UCN || code_point_in(c_identifier_continue_ranges, BUSTER_ARRAY_LENGTH(c_identifier_continue_ranges), code_point);
}

// clang's getEscapedNewLineSize: the size of the whitespace and newline after a backslash, if they make it a line
// splice ("\r\n" and "\n\r" count as one newline), or 0.
static uint64_t escaped_newline_size(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t size = 0;
    uint64_t result = 0;
    while (result == 0 && position + size < lexer->length && is_whitespace(lexer->source[position + size]))
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
            result = size;
        }
    }
    return result;
}

// clang's getCharAndSize: the character at `position` after trigraph replacement and line splicing, the number of
// bytes it takes up (`size`, including the line splices before it) and where the character itself starts. Call
// get_char, which handles the common case inline.
__attribute__((noinline)) static int32_t get_char_slow(const ScalarLexer* lexer, uint64_t position, uint64_t* size, uint64_t* char_start)
{
    uint64_t p = position;
    int32_t result = BUSTER_LEX_END_OF_FILE;
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
    if (result == BUSTER_LEX_END_OF_FILE)
    {
        *char_start = lexer->length;
    }
    *size = p - position;
    return result;
}

// get_char_slow, with its common case inline: a byte that can't start a trigraph or a line splice is the character
// itself. The lex_* loops get every character through here; an out-of-line call per character (with `size` and
// `char_start` going through memory) made the scalar lexer about 1.6 times slower on Zen 5 and 2 times slower on
// Zen 4, and on Zen 5 it also made its speed depend on how the called code happened to be aligned.
BUSTER_INLINE int32_t get_char(const ScalarLexer* lexer, uint64_t position, uint64_t* size, uint64_t* char_start)
{
    int32_t result;
    if (BUSTER_LIKELY(position < lexer->length && lexer->source[position] != '\\' && (lexer->source[position] != '?' || !lexer->trigraphs)))
    {
        result = lexer->source[position];
        *char_start = position;
        *size = 1;
    }
    else
    {
        result = get_char_slow(lexer, position, size, char_start);
    }
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
            if (c == BUSTER_LEX_END_OF_FILE)
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
            *code_point = valid_name ? BUSTER_LEX_NAMED_UCN : 0;
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
        if (is_ascii_identifier_continue(c) || (c == '$' && !lexer->no_dollars))
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
        while (is_ascii_identifier_continue(c) || c == '.' || (c == '$' && !lexer->no_dollars))
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
        if (c == '\'' && !lexer->no_digit_separators)
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
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(suffixes); i += 1)
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
    bool is_suffix = true;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
    {
        // A UCN or UTF-8 identifier character starts a suffix. try_consume_* advance p only when they succeed.
        is_suffix = (c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p));
    }
    else
    {
        // A suffix must start with '_', unless it's one of the standard library's (after a string literal).
        is_suffix = c == '_';
        if (!is_suffix && is_string)
        {
            char suffix[3] = { (char)c };
            uint32_t length = 1;
            uint64_t q = p + size;
            for (;;)
            {
                uint64_t next_size;
                int32_t next = peek_char(lexer, q, &next_size);
                if (!is_ascii_identifier_continue(next) && (next != '$' || lexer->no_dollars))
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
        if (is_suffix)
        {
            p += size;
        }
    }
    uint64_t result = position;
    if (is_suffix)
    {
        for (;;)
        {
            c = peek_char(lexer, p, &size);
            if (is_ascii_identifier_continue(c) || (c == '$' && !lexer->no_dollars))
            {
                p += size;
            }
            else if (!((c == '\\' && try_consume_identifier_ucn(lexer, p, &p)) || (c >= 0x80 && try_consume_identifier_utf8(lexer, p, &p))))
            {
                break;
            }
        }
        result = p;
    }
    return result;
}

// clang's LexRawStringLiteral: returns the end of the raw string literal whose `R"` ends at `position`, and turns
// `kind` into `C_TOKEN_UNKNOWN` if its delimiter is invalid or it is unterminated. Inside of it, trigraphs and line
// splices are undone, so it is read from the source bytes. In C++, a user-defined suffix is part of the literal.
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
    uint64_t result;
    if (position + delimiter_length >= n || s[position + delimiter_length] != '(')
    {
        // An unknown token, up to the next '"'.
        while (p < n && s[p] != '"')
        {
            p += 1;
        }
        *kind = C_TOKEN_UNKNOWN;
        result = p < n ? p + 1 : n;
    }
    else
    {
        p = position + delimiter_length + 1;
        bool terminated = false;
        while (!terminated && p < n)
        {
            const uint8_t c = s[p];
            p += 1;
            if (c == ')' && p + delimiter_length < n && memcmp(s + p, s + position, delimiter_length) == 0 && s[p + delimiter_length] == '"')
            {
                p += delimiter_length + 1;
                terminated = true;
            }
        }
        if (terminated)
        {
            result = lexer->cplusplus ? lex_ud_suffix(lexer, p, true) : p;
        }
        else
        {
            *kind = C_TOKEN_UNKNOWN;
            result = n;
        }
    }
    return result;
}

typedef enum CRawStringError
{
    C_RAW_STRING_OK,
    C_RAW_STRING_DELIMITER_CHARACTER,
    C_RAW_STRING_DELIMITER_NEWLINE,
    C_RAW_STRING_DELIMITER_LENGTH,
    C_RAW_STRING_UNTERMINATED,
} CRawStringError;

// Diagnose an UNKNOWN token only, with the readable remainder of its original source. An invalid delimiter can
// look past the quote at which the lexer salvaged that token. Its prefix undergoes phases 1/2, its delimiter doesn't. The caller decides
// whether this is active C text: opaque header names/messages and skipped groups must not diagnose these bytes.
// This cold query leaves the tokenizer's output/API and its vector chunk loop unchanged.
static __attribute__((unused)) CRawStringError c_raw_string_error(const uint8_t* source, uint64_t length, uint32_t flags, uint64_t* offset)
{
    const ScalarLexer lexer = { .source = source, .length = length, .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0 };
    char prefix[4];
    uint32_t count = 0;
    uint64_t position = 0;
    bool quoted = false;
    while (!quoted && count < sizeof(prefix) && position < length)
    {
        uint64_t size;
        uint64_t at;
        const int32_t c = get_char(&lexer, position, &size, &at);
        quoted = c == '"';
        prefix[count] = (char)c;
        count += 1;
        position += size;
    }
    const bool plain = (count == 2 && memcmp(prefix, "R\"", 2) == 0) || (count == 3 && memcmp(prefix, "LR\"", 3) == 0);
    const bool utf = (count == 3 && (memcmp(prefix, "uR\"", 3) == 0 || memcmp(prefix, "UR\"", 3) == 0)) ||
                     (count == 4 && memcmp(prefix, "u8R\"", 4) == 0);
    const bool cpp = (flags & C_TOKENIZER_CPLUSPLUS) != 0;
    const bool raw = cpp || (flags & C_TOKENIZER_RAW_STRINGS) != 0;
    CRawStringError result = C_RAW_STRING_OK;
    *offset = 0;
    if (quoted && raw && (plain || (utf && (cpp || (flags & C_TOKENIZER_UTF_RAW_STRINGS) != 0))))
    {
        uint32_t delimiter = 0;
        while (delimiter < 16 && position + delimiter < length && source[position + delimiter] > ' ' && source[position + delimiter] < 0x7F &&
               source[position + delimiter] != '(' && source[position + delimiter] != ')' && source[position + delimiter] != '\\')
        {
            delimiter += 1;
        }
        const uint64_t end = position + delimiter;
        if (end < length && source[end] == '(')
        {
            result = C_RAW_STRING_UNTERMINATED;
        }
        else
        {
            *offset = end;
            result = delimiter == 16 ? C_RAW_STRING_DELIMITER_LENGTH : end < length && source[end] == '\n' ? C_RAW_STRING_DELIMITER_NEWLINE : C_RAW_STRING_DELIMITER_CHARACTER;
        }
    }
    return result;
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
    uint64_t result;
    if (quote == '\'' && c == '\'')
    {
        *kind = C_TOKEN_UNKNOWN;
        result = p;
    }
    else
    {
        bool unterminated = false;
        while (!unterminated && c != quote)
        {
            if (c == '\\')
            {
                c = peek_char(lexer, p, &size);
                p += size;
            }
            if (c == '\n' || c == '\r')
            {
                unterminated = true;
                result = p - 1;
            }
            else if (c == BUSTER_LEX_END_OF_FILE)
            {
                unterminated = true;
                result = lexer->length;
            }
            else
            {
                c = peek_char(lexer, p, &size);
                p += size;
            }
        }
        if (unterminated)
        {
            *kind = C_TOKEN_UNKNOWN;
        }
        else
        {
            result = lexer->cplusplus ? lex_ud_suffix(lexer, p, quote == '"') : p;
        }
    }
    return result;
}

// Skips the block comment whose "/*" ends at `position`. Returns false if it is unterminated.
static bool skip_block_comment(const ScalarLexer* lexer, uint64_t position, uint64_t* end)
{
    uint64_t p = position;
    int32_t previous = 0;
    bool closed = false;
    bool at_end = false;
    while (!closed && !at_end)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        if (c == BUSTER_LEX_END_OF_FILE)
        {
            at_end = true;
        }
        else
        {
            p += size;
            closed = c == '/' && previous == '*';
            previous = c;
        }
    }
    if (closed)
    {
        *end = p;
    }
    return closed;
}

// Skips the line comment whose "//" ends at `position`, up to (not including) the newline that ends it.
static uint64_t skip_line_comment(const ScalarLexer* lexer, uint64_t position)
{
    uint64_t p = position;
    for (;;)
    {
        uint64_t size;
        int32_t c = peek_char(lexer, p, &size);
        if (c == BUSTER_LEX_END_OF_FILE || c == '\n' || c == '\r')
        {
            return p;
        }
        p += size;
    }
}

static void emit(TokenOutput* output, uint8_t kind, uint64_t start, uint64_t end)
{
    output->kinds[output->count] = kind | (output->start_of_line ? BUSTER_C_TOKEN_START_OF_LINE : 0);
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
BUSTER_LEXER_ENTRY uint64_t c_tokenize_scalar_range(const uint8_t* source, uint64_t length, uint64_t position, bool start_of_line, uint64_t stop_at,
                                 uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint64_t count, uint32_t flags, uint64_t* resume)
{
    ScalarLexer lexer_value = {
        .source = source,
        .length = length,
        .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = (flags & C_TOKENIZER_CPLUSPLUS) != 0,
        .raw_strings = (flags & (C_TOKENIZER_CPLUSPLUS | C_TOKENIZER_RAW_STRINGS)) != 0,
        .utf_raw_strings = (flags & C_TOKENIZER_CPLUSPLUS) != 0 || (flags & (C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS)) ==
                                                                     (C_TOKENIZER_RAW_STRINGS | C_TOKENIZER_UTF_RAW_STRINGS),
        .no_dollars = (flags & C_TOKENIZER_NO_DOLLARS) != 0,
        .no_digraphs = (flags & C_TOKENIZER_NO_DIGRAPHS) != 0,
        .no_line_comments = (flags & C_TOKENIZER_NO_LINE_COMMENTS) != 0,
        .no_digit_separators = (flags & C_TOKENIZER_NO_DIGIT_SEPARATORS) != 0,
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
        if (c == BUSTER_LEX_END_OF_FILE)
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
                if (c2 == '/' && !lexer->no_line_comments)
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
                // Prefixed character constants and string literals: L, u8, u and U, in the order of the kinds. With raw
                // strings, R (alone or after L, and after u8, u or U where those have raw strings) starts a raw string
                // literal.
                uint8_t prefix = c == 'L' ? 1 : c == 'u' ? 3 : c == 'U' ? 4 : 0;
                c2 = peek_char(lexer, p, &size2);
                uint64_t quote_end = p + size2;
                if (c == 'u' && c2 == '8')
                {
                    uint64_t size3;
                    int32_t c3 = peek_char(lexer, quote_end, &size3);
                    if (c3 == '"' || c3 == '\'' || (c3 == 'R' && lexer->utf_raw_strings))
                    {
                        prefix = 2;
                        c2 = c3;
                        quote_end += size3;
                    }
                }
                bool raw = false;
                if ((c == 'R' || c == 'L' ? lexer->raw_strings : lexer->utf_raw_strings) && (c == 'R' || c2 == 'R'))
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
                else if (c2 == '=' || ((c2 == ':' || c2 == '%') && !lexer->no_digraphs))
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
                if (c2 == '=' || (c2 == '>' && !lexer->no_digraphs))
                {
                    kind = c2 == '=' ? C_TOKEN_PERCENTEQUAL : C_TOKEN_R_BRACE;
                    p += size2;
                }
                else if (c2 == ':' && !lexer->no_digraphs)
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
                if ((c2 == '>' && !lexer->no_digraphs) || c2 == ':')
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
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c == '$' && !lexer->no_dollars))
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

BUSTER_LEXER_ENTRY int64_t c_tokenize_scalar(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    int64_t result;
    if (length > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        result = C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    else
    {
        const uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
        uint64_t resume;
        result = (int64_t)c_tokenize_scalar_range(source, length, begin, true, UINT64_MAX, kinds, starts, ends, 0, flags, &resume);
    }
    return result;
}

// ===== c_tokenizer.c =====
// The vectorized C tokenizer. See c_tokenizer.h, and src/CompactingTokenizer.zig for the 512-byte-chunk design it
// shares with the Zig tokenizer.
//
// Each iteration tokenizes a 512-byte chunk as eight 64-byte vectors, with a 512-bit bitstring per character class
// (`U64x8`, lane 0 holding the lowest bits). Phase I classifies each vector, Phase II finds where every token starts
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
// comment, block comments whose lines need more than a few rescans to settle, raw string literals, and trigraphs
// (when enabled, for the whole source).
//
// C++ (C_TOKENIZER_CPLUSPLUS) is a specialization of the same chunk loop: it adds "<=>", "->*" and ".*", the "<::"
// rule, user-defined literal suffixes and raw string literals. So is C with raw string literals
// (C_TOKENIZER_RAW_STRINGS), whose check is C++'s: it finds every u8R, uR and UR literal too, which before C11
// (without C_TOKENIZER_UTF_RAW_STRINGS) the scalar lexer then lexes as an identifier and a string literal.
// rule and user-defined literal suffixes. So is C without digraphs (C_TOKENIZER_NO_DIGRAPHS), by its table of
// first-character channels alone (c_first_char_channels_no_digraphs), in which no digraph pairs: the loops are the same
// code. And C without `//` comments (C_TOKENIZER_NO_LINE_COMMENTS, c89 and C94 only), whose line-comment marks are empty.
// C++ has both: with either flag, it takes the scalar lexer.

// The channels of 2-character punctuators, and those that need the same character twice.
#define BUSTER_LEX_OP2_CHANNELS (BUSTER_LEX_CHANNEL_EQ | BUSTER_LEX_CHANNEL_SELF | BUSTER_LEX_CHANNEL_SHIFT | BUSTER_LEX_CHANNEL_GT | BUSTER_LEX_CHANNEL_LTDIG | BUSTER_LEX_CHANNEL_HASHDIG)
#define BUSTER_LEX_SAME_CHARACTER_CHANNELS (BUSTER_LEX_CHANNEL_SELF | BUSTER_LEX_CHANNEL_SHIFT | BUSTER_LEX_CHANNEL_DOTS)

// A chunk is BUSTER_LEX_VECTOR_COUNT vectors of BUSTER_LEX_VECTOR_BYTES bytes. A U64x8 bitstring has one bit per byte
// of a chunk, so its lane j covers vector j.
#define BUSTER_LEX_VECTOR_BYTES 64
#define BUSTER_LEX_VECTOR_COUNT 8
#define BUSTER_LEX_CHUNK_BYTES (BUSTER_LEX_VECTOR_COUNT * BUSTER_LEX_VECTOR_BYTES)
// Phase I classifies a chunk's vectors and the next chunk's first one, which Phase II looks ahead into.
#define BUSTER_LEX_LOOKAHEAD_VECTORS (BUSTER_LEX_VECTOR_COUNT + 1)
// The stride of a mask's lanes in the Phase I buffers: the lookahead lanes, padded so that each mask's first
// BUSTER_LEX_VECTOR_COUNT lanes load as an aligned U64x8.
#define BUSTER_LEX_MASK_LANES (2 * BUSTER_LEX_VECTOR_COUNT)
// All eight bits of c_char_properties are transposed together; the remaining masks are stored separately.
#define BUSTER_LEX_PROPERTY_MASK_COUNT 8

typedef uint64_t U64x8 __attribute__((ext_vector_type(BUSTER_LEX_VECTOR_COUNT)));
typedef uint8_t U8x64 __attribute__((ext_vector_type(BUSTER_LEX_VECTOR_BYTES)));
typedef uint8_t U8x16 __attribute__((ext_vector_type(16)));

static const char* const c_token_kind_names[C_TOKEN_KIND_COUNT] =
{
#define BUSTER_C_TOKEN_NAME(name, clang_name) #clang_name,
    BUSTER_C_TOKEN_KINDS(BUSTER_C_TOKEN_NAME)
#undef BUSTER_C_TOKEN_NAME
};

static const char* c_token_kind_name(uint8_t kind)
{
    uint8_t k = kind & (uint8_t)~BUSTER_C_TOKEN_START_OF_LINE;
    return k < C_TOKEN_KIND_COUNT ? c_token_kind_names[k] : "<invalid kind>";
}

static const uint8_t iota_bytes[BUSTER_LEX_VECTOR_BYTES] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))) =
{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

// ---------------------------------------------------------------------------------------------------------------------
// 512-bit bitstrings

// `a << i` and `a >> i` within the chunk, for a constant 0 < i < 64.
BUSTER_INLINE U64x8 shl(U64x8 a, int i)
{
    return (__builtin_shufflevector(a, (U64x8){0}, 8, 0, 1, 2, 3, 4, 5, 6) >> (64 - i)) | (a << i);
}

BUSTER_INLINE U64x8 shr(U64x8 a, int i)
{
    return (__builtin_shufflevector(a, (U64x8){0}, 1, 2, 3, 4, 5, 6, 7, 8) << (64 - i)) | (a >> i);
}

// `a << i`, shifting in the top `i` bits of `previous`, the last lane of the previous chunk's bitstring.
BUSTER_INLINE U64x8 shl_carry(U64x8 a, uint64_t previous, int i)
{
    return (__builtin_shufflevector(a, (U64x8){previous}, 8, 0, 1, 2, 3, 4, 5, 6) >> (64 - i)) | (a << i);
}

// `a >> i`, shifting in the low `i` bits of `next`, the bitstring of the next chunk's first 64 bytes.
BUSTER_INLINE U64x8 shr_next(U64x8 a, uint64_t next, int i)
{
    return (__builtin_shufflevector(a, (U64x8){next}, 1, 2, 3, 4, 5, 6, 7, 8) << (64 - i)) | (a >> i);
}

// 512-bit subtraction: a lane borrows when the lane below it borrowed out (Kogge-Stone, propagated in a mask register).
BUSTER_INLINE U64x8 sub(U64x8 a, U64x8 b)
{
    const uint8_t carries = _mm512_cmplt_epu64_mask((__m512i)a, (__m512i)b);
    const uint8_t equal = _mm512_cmpeq_epi64_mask((__m512i)a, (__m512i)b);
    const uint8_t borrows = equal ^ (uint8_t)((carries << 1) + equal);
    const U64x8 difference = a - b;
    return (U64x8)_mm512_mask_add_epi64((__m512i)difference, borrows, (__m512i)difference, _mm512_set1_epi64(-1));
}

// 512-bit addition with a carry in and out.
BUSTER_INLINE U64x8 add_carry(U64x8 a, U64x8 b, uint32_t carry_in, uint32_t* carry_out)
{
    const U64x8 sum = a + b;
    const uint32_t generates = _mm512_cmplt_epu64_mask((__m512i)sum, (__m512i)a);
    const uint32_t propagates = _mm512_cmpeq_epi64_mask((__m512i)sum, _mm512_set1_epi64(-1));
    const uint32_t carries = ((generates << 1) | carry_in) + propagates;
    *carry_out = (carries >> 8) & 1;
    return (U64x8)_mm512_mask_sub_epi64((__m512i)sum, (uint8_t)(carries ^ propagates), (__m512i)sum, _mm512_set1_epi64(-1));
}

BUSTER_INLINE bool is_zero(U64x8 x)
{
    return _mm512_test_epi64_mask((__m512i)x, (__m512i)x) == 0;
}

BUSTER_INLINE uint64_t bit_511(U64x8 x)
{
    return x[7] >> 63;
}

// Bit 0 if `condition` (0 or 1), without a branch.
BUSTER_INLINE U64x8 bit_0_if(uint64_t condition)
{
    return (U64x8){condition, 0, 0, 0, 0, 0, 0, 0};
}

// The bits below `limit` (at most 512).
BUSTER_INLINE U64x8 bits_below(uint32_t limit)
{
    // The first bit of each lane: multiples of BUSTER_LEX_VECTOR_BYTES.
    const __m512i lane_starts = _mm512_setr_epi64(0, 64, 128, 192, 256, 320, 384, 448);
    const __m512i shifts = _mm512_max_epi64(_mm512_sub_epi64(_mm512_set1_epi64(limit), lane_starts), _mm512_setzero_si512());
    return (U64x8)_mm512_andnot_si512(_mm512_sllv_epi64(_mm512_set1_epi64(-1), shifts), _mm512_set1_epi64(-1));
}

// The position of the highest set bit below `limit` (at most 512), or -1 if there is none.
BUSTER_INLINE int32_t highest_bit_below(U64x8 x, uint32_t limit)
{
    int32_t result = -1;
    for (int32_t lane = 7; lane >= 0; lane -= 1)
    {
        const uint32_t lane_start = (uint32_t)lane * BUSTER_LEX_VECTOR_BYTES;
        if (lane_start >= limit)
        {
            continue;
        }
        uint64_t bits = x[lane];
        if (limit - lane_start < BUSTER_LEX_VECTOR_BYTES)
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

BUSTER_INLINE __m512i splat8(uint8_t x)
{
    return _mm512_set1_epi8((char)x);
}

// Looks up 64 bytes in a 16-byte table (repeated across the vector). Indices >= 0x80 give 0.
BUSTER_INLINE __m512i vpshufb(const uint8_t table[16], __m512i indices)
{
    return _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_load_si128((const __m128i*)table)), indices);
}

// Looks up 64 bytes in a 128-byte table, using the low 7 bits of each index.
BUSTER_INLINE __m512i perm2b(const uint8_t table[128], __m512i indices)
{
    return _mm512_permutex2var_epi8(_mm512_load_si512(table), indices, _mm512_load_si512(table + 64));
}

// For a permute that shifts a vector up by one byte, shifting in the last byte of another: byte i of the result is
// byte i - 1 of the first vector (index i - 1), and byte 0 is byte 63 of the second (index 127).
static const uint8_t shift_in_indices[BUSTER_LEX_VECTOR_BYTES] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))) =
{
    127, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
};

// `current` shifted up by one byte, shifting in the last byte of `previous`: one vpermt2b (an FP1/FP2 uop), where
// Clang would otherwise emit a valignq and a vpalignr (two). The index is opaque to Clang so that it keeps the permute.
BUSTER_INLINE __m512i shift_in_previous(__m512i current, __m512i previous)
{
    __m512i indices = _mm512_load_si512(shift_in_indices);
    __asm__("" : "+v"(indices));
    return _mm512_permutex2var_epi8(current, indices, previous);
}

// Could the characters before and after a line splice be part of the same token (outside of literals and comments)?
// `earlier` is the character before `before`. It answers as with digraphs and "//" comments, which errs towards a join
// without them: a splice in a digraph's characters or in "//" then sends its chunk to the scalar lexer, as rarely.
BUSTER_INLINE bool splice_joins(uint8_t earlier, uint8_t before, uint8_t after, bool cplusplus)
{
    const bool before_identifier = before >= 0x80 || (c_char_properties[before] & BUSTER_LEX_CHAR_IDENTIFIER);
    const bool after_identifier = after >= 0x80 || (c_char_properties[after] & BUSTER_LEX_CHAR_IDENTIFIER);
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
            channels &= (uint8_t)~BUSTER_LEX_SAME_CHARACTER_CHANNELS;
        }
        result = (before < 0x80 && channels != 0) || (before == '/' && (after == '/' || after == '*')) || (before == ':' && after == '%');
    }
    return result;
}

// The byte at `position` of the source, or a newline outside of [`begin`, `length`).
BUSTER_INLINE uint8_t source_byte(const uint8_t* source, uint64_t begin, uint64_t length, int64_t position)
{
    return position >= (int64_t)begin && position < (int64_t)length ? source[position] : '\n';
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
        while (suffix_length < 4 && (c_char_properties[source_byte(source, begin, length, position + suffix_length) & 0x7F] & BUSTER_LEX_CHAR_IDENTIFIER) &&
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
    // The third characters of the trigraphs.
    static const char trigraph_finals[] = "=/'()!<>-";
    const uint8_t* end = source + length;
    bool found = false;
    for (const uint8_t* p = source; !found && p + 2 < end;)
    {
        const uint8_t* question = memchr(p, '?', (uint64_t)(end - p - 2));
        if (question == 0)
        {
            break;
        }
        found = question[1] == '?' && memchr(trigraph_finals, question[2], sizeof(trigraph_finals) - 1) != 0;
        p = question + 1;
    }
    return found;
}

// Loads the 64 bytes at `lane` (relative to the 64-byte-aligned `base`), reading the bytes outside of
// [`begin`, `length`) of the source (which starts at `base + base_offset`) as newlines.
BUSTER_INLINE __m512i load_lane(const uint8_t* base, int64_t base_offset, uint64_t begin, uint64_t length, int64_t lane)
{
    const int64_t first = -base_offset - lane + (int64_t)begin; // The first valid byte of the lane.
    const int64_t end = -base_offset - lane + (int64_t)length; // Past the last valid byte.
    __m512i result;
    if (BUSTER_LIKELY(first <= 0 && end >= BUSTER_LEX_VECTOR_BYTES))
    {
        result = _mm512_load_si512(base + lane);
    }
    else
    {
        const uint64_t below_end = end <= 0 ? 0 : end >= BUSTER_LEX_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)end);
        const uint64_t from_first = first <= 0 ? ~0ull : first >= BUSTER_LEX_VECTOR_BYTES ? 0 : ~_bzhi_u64(~0ull, (uint32_t)first);
        result = _mm512_mask_loadu_epi8(splat8('\n'), below_end & from_first, base + lane);
    }
    return result;
}

// The bits of a lane at or past the end of the source.
BUSTER_INLINE uint64_t lane_padding(int64_t base_offset, uint64_t length, int64_t lane)
{
    const int64_t end = -base_offset - lane + (int64_t)length;
    return end <= 0 ? ~0ull : end >= BUSTER_LEX_VECTOR_BYTES ? 0 : ~_bzhi_u64(~0ull, (uint32_t)end);
}

// ---------------------------------------------------------------------------------------------------------------------

// The first eight masks follow the bit order of c_char_properties. Phase I packs them per source lane; Phase II
// transposes them into bitstrings. The remaining masks are stored as `o[mask][lane]`, with lane 8 for lookahead.
enum
{
    M_IDENTIFIER, // [A-Za-z0-9_$]
    M_DIGIT,
    M_WHITESPACE, // ' ' \t \v \f \0
    M_NEWLINE, // \n \r, and the bytes past the end of the source
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

_Static_assert(M_SLASH == BUSTER_LEX_PROPERTY_MASK_COUNT, "the property masks must precede the other masks");
_Static_assert((1u << M_IDENTIFIER) == BUSTER_LEX_CHAR_IDENTIFIER && (1u << M_DIGIT) == BUSTER_LEX_CHAR_DIGIT &&
               (1u << M_WHITESPACE) == BUSTER_LEX_CHAR_WHITESPACE && (1u << M_NEWLINE) == BUSTER_LEX_CHAR_NEWLINE &&
               (1u << M_SEPARATOR_FOLLOWER) == BUSTER_LEX_CHAR_SEPARATOR_FOLLOWER && (1u << M_QUOTE) == BUSTER_LEX_CHAR_QUOTE &&
               (1u << M_APOSTROPHE) == BUSTER_LEX_CHAR_APOSTROPHE && (1u << M_BACKSLASH) == BUSTER_LEX_CHAR_BACKSLASH,
               "property table bits and transposed masks must agree");

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
    U64x8 line_breaks;
    U64x8 token_starts;
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
        const uint32_t line_start = (uint32_t)highest_bit_below(last_line->line_breaks, BUSTER_LEX_CHUNK_BYTES) + 1;
        *restart = (uint64_t)(last_line->chunk_position + line_start);
        *written = last_line->first_token + (uint64_t)_mm512_reduce_add_epi64(_mm512_popcnt_epi64((__m512i)(last_line->token_starts & bits_below(line_start))));
    }
    *scalar_until = (uint64_t)(chunk_position + BUSTER_LEX_CHUNK_BYTES);
    return C_TOKENIZER_ERROR_NEEDS_SCALAR;
}

#define BUSTER_LEX_NEEDS_SCALAR needs_scalar(&last_line, begin, chunk_position, written, restart, scalar_until)

// Transpose eight property bits per character into eight 64-bit masks with GFNI.
BUSTER_INLINE __m512i property_masks(__m512i properties)
{
    // GFNI reads the eight matrix rows in reverse byte order. After reversing each qword, multiplying by the
    // one-hot basis packs each property of eight consecutive characters into one byte. The final permutation
    // collects the eight bytes for each property into one qword, in source order.
    const __m512i reverse = _mm512_broadcast_i32x4(_mm_set_epi64x(0x08090A0B0C0D0E0F, 0x0001020304050607));
    const __m512i basis = _mm512_set1_epi64((int64_t)0x8040201008040201ull);
    const __m512i indices = _mm512_setr_epi64(
        0x3830282018100800, 0x3931292119110901, 0x3A322A221A120A02, 0x3B332B231B130B03,
        0x3C342C241C140C04, 0x3D352D251D150D05, 0x3E362E261E160E06, 0x3F372F271F170F07);
    const __m512i reversed = _mm512_shuffle_epi8(properties, reverse);
    return _mm512_permutexvar_epi8(indices, _mm512_gf2p8affine_epi64_epi8(basis, reversed, 0));
}

// Transpose the 8x8 qword matrix into the bitstrings consumed by Phase II: interleave qwords, pairs, then quartets.
// Keep this at the consumer, so Clang can retain the resulting bitstrings in registers.
BUSTER_INLINE void transpose_property_masks(const __m512i rows[BUSTER_LEX_LOOKAHEAD_VECTORS], uint64_t o[][BUSTER_LEX_MASK_LANES])
{
    const U64x8 t0_0 = __builtin_shufflevector((U64x8)rows[0], (U64x8)rows[1], 0, 8, 2, 10, 4, 12, 6, 14);
    const U64x8 t0_1 = __builtin_shufflevector((U64x8)rows[0], (U64x8)rows[1], 1, 9, 3, 11, 5, 13, 7, 15);
    const U64x8 t0_2 = __builtin_shufflevector((U64x8)rows[2], (U64x8)rows[3], 0, 8, 2, 10, 4, 12, 6, 14);
    const U64x8 t0_3 = __builtin_shufflevector((U64x8)rows[2], (U64x8)rows[3], 1, 9, 3, 11, 5, 13, 7, 15);
    const U64x8 t0_4 = __builtin_shufflevector((U64x8)rows[4], (U64x8)rows[5], 0, 8, 2, 10, 4, 12, 6, 14);
    const U64x8 t0_5 = __builtin_shufflevector((U64x8)rows[4], (U64x8)rows[5], 1, 9, 3, 11, 5, 13, 7, 15);
    const U64x8 t0_6 = __builtin_shufflevector((U64x8)rows[6], (U64x8)rows[7], 0, 8, 2, 10, 4, 12, 6, 14);
    const U64x8 t0_7 = __builtin_shufflevector((U64x8)rows[6], (U64x8)rows[7], 1, 9, 3, 11, 5, 13, 7, 15);
    const U64x8 t1_0 = __builtin_shufflevector(t0_0, t0_2, 0, 1, 8, 9, 4, 5, 12, 13);
    const U64x8 t1_2 = __builtin_shufflevector(t0_0, t0_2, 2, 3, 10, 11, 6, 7, 14, 15);
    const U64x8 t1_1 = __builtin_shufflevector(t0_1, t0_3, 0, 1, 8, 9, 4, 5, 12, 13);
    const U64x8 t1_3 = __builtin_shufflevector(t0_1, t0_3, 2, 3, 10, 11, 6, 7, 14, 15);
    const U64x8 t1_4 = __builtin_shufflevector(t0_4, t0_6, 0, 1, 8, 9, 4, 5, 12, 13);
    const U64x8 t1_6 = __builtin_shufflevector(t0_4, t0_6, 2, 3, 10, 11, 6, 7, 14, 15);
    const U64x8 t1_5 = __builtin_shufflevector(t0_5, t0_7, 0, 1, 8, 9, 4, 5, 12, 13);
    const U64x8 t1_7 = __builtin_shufflevector(t0_5, t0_7, 2, 3, 10, 11, 6, 7, 14, 15);
    const U64x8 t2_0 = __builtin_shufflevector(t1_0, t1_4, 0, 1, 2, 3, 8, 9, 10, 11);
    const U64x8 t2_4 = __builtin_shufflevector(t1_0, t1_4, 4, 5, 6, 7, 12, 13, 14, 15);
    const U64x8 t2_1 = __builtin_shufflevector(t1_1, t1_5, 0, 1, 2, 3, 8, 9, 10, 11);
    const U64x8 t2_5 = __builtin_shufflevector(t1_1, t1_5, 4, 5, 6, 7, 12, 13, 14, 15);
    const U64x8 t2_2 = __builtin_shufflevector(t1_2, t1_6, 0, 1, 2, 3, 8, 9, 10, 11);
    const U64x8 t2_6 = __builtin_shufflevector(t1_2, t1_6, 4, 5, 6, 7, 12, 13, 14, 15);
    const U64x8 t2_3 = __builtin_shufflevector(t1_3, t1_7, 0, 1, 2, 3, 8, 9, 10, 11);
    const U64x8 t2_7 = __builtin_shufflevector(t1_3, t1_7, 4, 5, 6, 7, 12, 13, 14, 15);
    *(U64x8*)o[0] = t2_0;
    *(U64x8*)o[1] = t2_1;
    *(U64x8*)o[2] = t2_2;
    *(U64x8*)o[3] = t2_3;
    *(U64x8*)o[4] = t2_4;
    *(U64x8*)o[5] = t2_5;
    *(U64x8*)o[6] = t2_6;
    *(U64x8*)o[7] = t2_7;
}

// Phase I for one 64-byte vector: eight packed property masks and the remaining masks in `o[...][lane]`. Without
// `digraphs`, no digraph pairs (c_first_char_channels_no_digraphs).
BUSTER_INLINE void classify(__m512i raw, uint64_t o[][BUSTER_LEX_MASK_LANES], unsigned lane, __m512i* properties_out, __m512i* previous_classes, __m512i* previous_pairs, const bool cplusplus,
                            const bool digraphs, const bool separators)
{
    const uint64_t nonascii = _mm512_movepi8_mask(raw);
    __m512i properties = perm2b(separators ? c_char_properties : c_char_properties_no_separators, raw);
    __m512i classes = perm2b(c_operator_classes, raw);
    if (BUSTER_UNLIKELY(nonascii != 0))
    {
        properties = _mm512_maskz_mov_epi8(~nonascii, properties);
        classes = _mm512_maskz_mov_epi8(~nonascii, classes);
    }

    // See c/gen_c_tokenizer_tables.py for the channels.
    const __m512i previous = shift_in_previous(classes, *previous_classes);
    const __m512i allowed = _mm512_mask_blend_epi8(_mm512_cmpeq_epi8_mask(previous, classes), splat8((uint8_t)~BUSTER_LEX_SAME_CHARACTER_CHANNELS), splat8(0xFF));
    const __m512i pairs = vpshufb(c_second_char_channels, classes) & vpshufb(digraphs ? c_first_char_channels : c_first_char_channels_no_digraphs, previous) & allowed;
    const __m512i triples = vpshufb(cplusplus ? c_third_char_channels_cxx : c_third_char_channels, classes) & shift_in_previous(pairs, *previous_pairs);

    *properties_out = property_masks(properties);
    o[M_SLASH][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('/'));
    o[M_STAR][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('*'));
    o[M_DOT][lane] = _mm512_cmpeq_epi8_mask(raw, splat8('.'));
    o[M_EXPSIGN][lane] = _mm512_test_epi8_mask(pairs, splat8(BUSTER_LEX_CHANNEL_EXPSIGN));
    o[M_HASHDIG][lane] = _mm512_test_epi8_mask(pairs, splat8(BUSTER_LEX_CHANNEL_HASHDIG));
    o[M_OP2][lane] = _mm512_test_epi8_mask(pairs, splat8(BUSTER_LEX_OP2_CHANNELS));
    o[M_OP3][lane] = _mm512_test_epi8_mask(triples, splat8(BUSTER_LEX_CHANNEL_SHIFT | BUSTER_LEX_CHANNEL_DOTS));
    if (cplusplus)
    {
        o[M_OP3_CXX][lane] = _mm512_test_epi8_mask(triples, splat8(BUSTER_LEX_CHANNEL_EQ | BUSTER_LEX_CHANNEL_GT));
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
// lines it covers is forgotten, and the line it ends on is scanned again from its end. (A block comment that the
// chunk starts inside of is found before the scan, which starts after it; `state` is never STATE_BLOCK_COMMENT.)
typedef struct Scan Scan;
struct Scan
{
    U64x8 literal_starts;
    U64x8 literal_closers;
    U64x8 literal_unterminated; // The newline that ends an unterminated literal.
    U64x8 line_comment_starts;
    U64x8 block_region;
    U64x8 block_ends;
    bool spans_lines; // A block comment spans lines, so some line may have been scanned from the wrong state.
};

BUSTER_INLINE Scan scan_lines(U64x8 cursors, State state, U64x8 line_break, U64x8 bounds, U64x8 quote_bounds, U64x8 apostrophe_closers,
                              U64x8 apostrophe_bounds, U64x8 line_comment_marks, U64x8 block_comment_marks, U64x8 block_comment_closers)
{
    const U64x8 zero = {0};
    const U64x8 bit_0 = {1, 0, 0, 0, 0, 0, 0, 0};
    Scan result = { .literal_starts = zero, .literal_closers = zero, .literal_unterminated = zero, .line_comment_starts = zero,
                    .block_region = zero, .block_ends = zero, .spans_lines = false };
    U64x8 pseudo_quote = bit_0_if(state == STATE_STRING);
    U64x8 pseudo_apostrophe = bit_0_if(state == STATE_CHAR);
    const U64x8 every_line_start = shl(line_break, 1) | bit_0;
    U64x8 starts = bounds & ~sub(bounds | line_break, cursors);
    for (;;)
    {
        const U64x8 quote_starts = starts & quote_bounds;
        const U64x8 apostrophe_starts = starts & apostrophe_bounds;
        const U64x8 block_opens = starts & block_comment_marks;
        result.line_comment_starts |= starts & line_comment_marks;

        // A literal ends at its closing quote, or (unterminated) at the end of its line.
        U64x8 next_cursors = zero;
        const U64x8 quote_lines = quote_starts | pseudo_quote;
        const U64x8 apostrophe_lines = apostrophe_starts | pseudo_apostrophe;
        if (!is_zero(quote_lines | apostrophe_lines))
        {
            const U64x8 closers = line_break | (quote_bounds & sub(line_break, quote_lines)) | (apostrophe_closers & sub(line_break, apostrophe_lines));
            const U64x8 ends = closers & ~sub(closers, shl(quote_starts | apostrophe_starts, 1) | pseudo_quote | pseudo_apostrophe);
            result.literal_starts |= quote_starts | apostrophe_starts;
            result.literal_closers |= ends & ~line_break;
            result.literal_unterminated |= ends & line_break;
            next_cursors = shl(ends & ~line_break, 1);
        }

        // A block comment ends at the first "*/" whose '*' isn't its own.
        if (!is_zero(block_opens))
        {
            // Several searches can end at the same "*/" (one from each line of a comment that seems to start a
            // comment); one that starts on it must not take part in the subtraction, or the others would skip it.
            // (The search of a "/*" in the last 3 bits starts in the next chunk.)
            const U64x8 searches = shl(block_opens, 3);
            const U64x8 ends = (block_comment_closers & ~sub(block_comment_closers, searches & ~block_comment_closers)) | (searches & block_comment_closers);
            const U64x8 region = sub(ends, block_opens) | block_opens | ends;
            if (!is_zero(region & line_break))
            {
                // The rest of each line a comment ends on, with the newline that ends it. (Two comments can end on
                // the same line, so the searches that start on a newline are kept out of the subtraction, and the
                // subtraction's holes at the other searches are filled in.)
                const U64x8 after_ends = shl(ends, 1);
                const U64x8 rest_of_line = (sub(line_break, after_ends & ~line_break) ^ line_break) | after_ends;
                const U64x8 keep = ~(region | rest_of_line);
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

        pseudo_quote = zero;
        pseudo_apostrophe = zero;
        // Another round only if a cursor finds another delimiter on its line: one round does for 98% of chunks, and
        // the test is then well predicted (a test of the cursors alone would take a second round that finds nothing).
        starts = bounds & ~sub(bounds | line_break, next_cursors);
        if (is_zero(starts))
        {
            break;
        }
    }
    return result;
}

// Leading comments cannot produce tokens or change the first token's start-of-line flag: `begin` is the start of the
// source (after a byte order mark) or of a logical line, where c_tokenize resumes after the scalar lexer. Stop at a
// backslash: translation-phase splices need the general lexer. (With C_TOKENIZER_TRIGRAPHS, a source with a trigraph
// never gets here, so "??/" can't splice a comment either.) Only commit complete comments, keeping malformed comments
// and whitespace-only prefixes unchanged. Without `line_comments` (C_TOKENIZER_NO_LINE_COMMENTS), "//" starts none.
static uint64_t skip_leading_comments(const uint8_t* source, uint64_t length, uint64_t begin, const bool line_comments)
{
    uint64_t result = begin;
    uint64_t position = begin;
    while (position < length)
    {
        while (position < length && (is_whitespace(source[position]) || source[position] == 0))
        {
            position += 1;
        }
        if (position + 1 >= length || source[position] != '/' || ((source[position + 1] != '/' || !line_comments) && source[position + 1] != '*'))
        {
            break;
        }
        const bool line_comment = source[position + 1] == '/';
        uint64_t cursor = position + 2;
        uint64_t previous_star = 0;
        bool complete = false;
        while (cursor < length)
        {
            const uint64_t remaining = length - cursor;
            const uint64_t valid = remaining >= BUSTER_LEX_VECTOR_BYTES ? ~0ull : _bzhi_u64(~0ull, (uint32_t)remaining);
            const __m512i raw = _mm512_maskz_loadu_epi8(valid, source + cursor);
            const uint64_t backslashes = _mm512_cmpeq_epi8_mask(raw, splat8('\\'));
            uint64_t closers;
            if (line_comment)
            {
                closers = _mm512_cmpeq_epi8_mask(raw, splat8('\n')) | _mm512_cmpeq_epi8_mask(raw, splat8('\r'));
            }
            else
            {
                const uint64_t stars = _mm512_cmpeq_epi8_mask(raw, splat8('*'));
                const uint64_t slashes = _mm512_cmpeq_epi8_mask(raw, splat8('/'));
                closers = slashes & ((stars << 1) | previous_star);
                previous_star = stars >> 63;
            }
            if ((closers | backslashes) != 0)
            {
                const uint32_t first = (uint32_t)__builtin_ctzll(closers | backslashes);
                if (((backslashes >> first) & 1) == 0)
                {
                    result = cursor + first + 1;
                    complete = true;
                }
                break;
            }
            cursor += remaining < BUSTER_LEX_VECTOR_BYTES ? remaining : BUSTER_LEX_VECTOR_BYTES;
        }
        if (!complete)
        {
            break;
        }
        position = result;
    }
    return result;
}

// What Phase II finds in a chunk, which Phase III writes out a chunk later: before the next chunk's analysis rather
// than after this one's. The analysis is mostly long dependency chains and the write-out a lot of independent work,
// so the two can then overlap (Phase III would otherwise wait for its own chunk's analysis). Each array has a lane
// per 64-byte vector of the chunk, and is aligned to load as a U64x8.
typedef struct ChunkTokens ChunkTokens;
struct ChunkTokens
{
    uint64_t starts[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    uint64_t ends[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    uint64_t start_of_line[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    uint64_t multi[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))); // Multi-character punctuators,
    uint64_t long_ones[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))); // and those of 3 or 4 characters.
    uint64_t dot_numbers[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))); // pp-numbers starting with '.'.
    uint64_t prefixes[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))); // Prefixed literals,
    uint64_t unknowns[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES))); // and unknown ones.
    uint64_t start_counts[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    uint64_t end_counts[BUSTER_LEX_VECTOR_COUNT] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    const __m512i* raw; // The chunk's vectors, and the next chunk's first one.
    int64_t chunk_position; // The source position of bit 0.
    uint32_t special_lanes; // Bit j: lane j has a dot-number, or a prefixed or unknown literal.
};

// Where the next token's kind, start and end go.
typedef struct OutputCursors OutputCursors;
struct OutputCursors
{
    uint8_t* kind;
    uint32_t* start;
    uint32_t* end;
};

// III. Write-out: compresses the kinds, starts and ends of a chunk's tokens into the output, a 64-byte vector at a
// time.
BUSTER_INLINE OutputCursors write_chunk(const ChunkTokens* t, const uint8_t* source, uint64_t begin, uint64_t length, OutputCursors cursors)
{
    uint8_t* cur_kind = cursors.kind;
    uint32_t* cur_start = cursors.start;
    uint32_t* cur_end = cursors.end;
    const __m512i* raw = t->raw;
    // A token's position is its byte index plus its vector's source position (and 1 more for the exclusive end). The
    // vector's position advances by an add: a broadcast from a general register is 2 uops with an 8-cycle latency on
    // Zen 5.
    __m512i start_offset = _mm512_set1_epi32((int)(uint32_t)t->chunk_position);
    for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
    {
        // From memory straight to mask registers: 1 uop each, where a general register would take 2.
        const __mmask64 starts_mask = _load_mask64((__mmask64*)&t->starts[j]);
        const __mmask64 ends_mask = _load_mask64((__mmask64*)&t->ends[j]);

        // The kind of each character's token if it starts one, then the exceptions.
        __m512i kinds = perm2b(c_char_kinds, raw[j]);
        // Multi-character punctuators (in 14% of the vectors of C sources, too many to branch on): a 2-character one
        // by its characters' entry of c_pair_kinds. The vectors are consecutive in memory, so an unaligned load gives
        // each character's next one without a shuffle.
        const __m512i next_characters = _mm512_loadu_si512((const uint8_t*)&raw[j] + 1);
        const __m512i sum = _mm512_add_epi8(raw[j], next_characters);
        const __m512i twice = _mm512_add_epi8(sum, sum);
        const __m512i pair_index = _mm512_add_epi8(_mm512_add_epi8(twice, twice), raw[j]); // 5 * c0 + 4 * c1
        kinds = _mm512_mask_mov_epi8(kinds, _load_mask64((__mmask64*)&t->multi[j]), perm2b(c_pair_kinds, pair_index));
        // Those of 3 or 4 characters (rare) by c_long_kinds.
        if (BUSTER_UNLIKELY(t->long_ones[j] != 0))
        {
            kinds = _mm512_mask_mov_epi8(kinds, t->long_ones[j], _mm512_permutexvar_epi8(pair_index, _mm512_load_si512(c_long_kinds)));
        }
        const bool special = (t->special_lanes >> j) & 1;
        if (BUSTER_UNLIKELY(special))
        {
            kinds = _mm512_mask_mov_epi8(kinds, t->dot_numbers[j], splat8(C_TOKEN_NUMERIC_CONSTANT));
        }
        kinds = _mm512_mask_mov_epi8(kinds, _load_mask64((__mmask64*)&t->start_of_line[j]), _mm512_or_si512(kinds, splat8(BUSTER_C_TOKEN_START_OF_LINE)));
        _mm512_storeu_si512(cur_kind, _mm512_maskz_compress_epi8(starts_mask, kinds));

        // Prefixed and unknown literals (rare).
        for (uint64_t bits = special ? t->prefixes[j] | t->unknowns[j] : 0; BUSTER_UNLIKELY(bits != 0); bits &= bits - 1)
        {
            const int64_t lane_position = t->chunk_position + BUSTER_LEX_VECTOR_BYTES * j;
            const uint32_t bit = (uint32_t)__builtin_ctzll(bits);
            const uint64_t index = (uint64_t)__builtin_popcountll(t->starts[j] & _bzhi_u64(~0ull, bit));
            const uint8_t kind = (t->unknowns[j] >> bit) & 1 ? C_TOKEN_UNKNOWN : prefixed_literal_kind(source, begin, length, lane_position + bit);
            cur_kind[index] = kind | (cur_kind[index] & BUSTER_C_TOKEN_START_OF_LINE);
        }

        // The starts and ends: always the first 24 of each, and the rest when there are more. A 64-byte vector has 9
        // tokens on average, more than 16 in 9% of vectors and more than 24 in 0.6%, and a branch at 16 mispredicts
        // more than the 8 extra positions cost.
        const uint64_t num_starts = t->start_counts[j];
        const uint64_t num_ends = t->end_counts[j];
        const __m512i start_indices = _mm512_maskz_compress_epi8(starts_mask, _mm512_load_si512(iota_bytes));
        const __m512i end_indices = _mm512_maskz_compress_epi8(ends_mask, _mm512_load_si512(iota_bytes));
        const __m512i end_offset = _mm512_add_epi32(start_offset, _mm512_set1_epi32(1)); // Exclusive.
        _mm512_storeu_si512(cur_start, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_castsi512_si128(start_indices)), start_offset));
        _mm512_storeu_si512(cur_end, _mm512_add_epi32(_mm512_cvtepu8_epi32(_mm512_castsi512_si128(end_indices)), end_offset));
        _mm256_storeu_si256((__m256i*)(cur_start + 16),
                            _mm256_add_epi32(_mm256_cvtepu8_epi32(_mm512_extracti32x4_epi32(start_indices, 1)), _mm512_castsi512_si256(start_offset)));
        _mm256_storeu_si256((__m256i*)(cur_end + 16),
                            _mm256_add_epi32(_mm256_cvtepu8_epi32(_mm512_extracti32x4_epi32(end_indices, 1)), _mm512_castsi512_si256(end_offset)));
        if (BUSTER_UNLIKELY((num_starts > 24) | (num_ends > 24)))
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
        start_offset = _mm512_add_epi32(start_offset, _mm512_set1_epi32(BUSTER_LEX_VECTOR_BYTES));
    }
    return (OutputCursors){ .kind = cur_kind, .start = cur_start, .end = cur_end };
}

// The write-out of a source's last chunk, out of line (the chunk loop has its own copy).
__attribute__((noinline)) static OutputCursors write_chunk_outlined(const ChunkTokens* t, const uint8_t* source, uint64_t begin, uint64_t length, OutputCursors cursors)
{
    return write_chunk(t, source, begin, length, cursors);
}

// Tokenizes the source from `begin` (the start of the source, or of a line) to its end, or until a chunk that needs
// the scalar lexer (see BUSTER_LEX_NEEDS_SCALAR). Without `digraphs`, Phase I classifies with
// c_first_char_channels_no_digraphs, and every other step is as with them, so the two loops are the same code; without
// `line_comments`, "//" marks no comment.
BUSTER_INLINE int64_t tokenize_chunks(const bool cplusplus, const bool digraphs, const bool line_comments, const bool separators, const bool raw_strings, const uint8_t* source, uint64_t length, uint64_t begin,
                               uint8_t* restrict kinds_out, uint32_t* restrict starts_out, uint32_t* restrict ends_out, uint64_t* written, uint64_t* restart,
                               uint64_t* scalar_until)
{
    const U64x8 zero = {0};
    const U64x8 most_significant = {0, 0, 0, 0, 0, 0, 0, 1ull << 63};
    const U64x8 odd_bits = 0xAAAAAAAAAAAAAAAAull;

    begin = skip_leading_comments(source, length, begin, line_comments);

    // The source is read in 64-byte-aligned lanes; the bytes before `begin` and after the source read as newlines.
    // Positions are relative to the source, so the first chunk can start at a negative one.
    const uint8_t* const base = (const uint8_t*)((uintptr_t)source & ~(uintptr_t)63);
    const int64_t base_offset = (int64_t)(base - source);
    const int64_t first_chunk = ((int64_t)begin - base_offset) & ~(int64_t)63;

    // What the previous chunk passes on: the last lanes of its bitstrings (only their top bits matter), in general
    // registers. (Bitstrings carried whole would hold vector registers through the iteration, or be spilled.)
    uint64_t previous_identifier = 0;
    uint64_t previous_apostrophe = 0;
    uint64_t previous_star = 0;
    uint64_t previous_number_token = 0;
    uint64_t previous_identifier_token = 0;
    uint64_t previous_unknown_openers = 0;
    uint64_t previous_splice_backslash = 0; // At bit 63.
    uint64_t previous_splice_cr = 0; // At bit 63.
    uint64_t next_is_escaped = 0; // At bit 0.
    uint32_t number_carry = 0; // A pp-number continues into the chunk.
    State state = STATE_OUTSIDE;
    uint32_t block_comment_skip = 0; // A "*/" in bits below this can't close the block comment the chunk starts in.
    uint64_t pending_start_of_line = 1;
    uint64_t covered = 0; // The bits at the start of the chunk that a punctuator from the previous chunk covers,
    uint64_t covered_end = 0; // and the last of them.
    uint64_t suppressed_starts = 0; // The quote of a prefixed literal that starts in the previous chunk,
    uint64_t suppressed_ends = 0; // and the '8' of its "u8" prefix.

    LastLine last_line = { .valid = false };

    uint8_t* cur_kind = kinds_out;
    uint32_t* cur_start = starts_out;
    uint32_t* cur_end = ends_out;

    // Phase I runs a chunk ahead: its scalar mask stores cannot forward to the 64-byte Phase II loads. The eight
    // property masks instead share a vector store per source lane, then transpose at the consumer. Their rows in
    // masks are the transpose destination; the other rows retain the scalar masks. Lane 8 supplies lookahead.
    uint64_t masks[2][NUM_MASKS][BUSTER_LEX_MASK_LANES] __attribute__((aligned(BUSTER_LEX_VECTOR_BYTES)));
    // Three buffers of vectors: for the chunk being analyzed, the next one (classified), and the previous one (written
    // out).
    __m512i raws[3][BUSTER_LEX_LOOKAHEAD_VECTORS];
    ChunkTokens deferred;
    bool pending = false; // `deferred` has the previous chunk's tokens.
    __m512i properties[2][BUSTER_LEX_LOOKAHEAD_VECTORS];
    __m512i previous_classes = _mm512_setzero_si512();
    __m512i previous_pairs = _mm512_setzero_si512();
    for (unsigned j = 0; j < BUSTER_LEX_LOOKAHEAD_VECTORS; j += 1)
    {
        raws[0][j] = load_lane(base, base_offset, begin, length, first_chunk + BUSTER_LEX_VECTOR_BYTES * j);
        classify(raws[0][j], masks[0], j, &properties[0][j], &previous_classes, &previous_pairs, cplusplus, digraphs, separators);
    }
    // The vectors of the chunk being analyzed: in the source itself if they (and the next chunk's first one) are all
    // inside it, else in `raws`, with what is outside of it read as newlines.
    const __m512i* chunk_vectors = raws[0];

    for (int64_t chunk = first_chunk, current = 0, raw_current = 0;; chunk += BUSTER_LEX_CHUNK_BYTES, current ^= 1, raw_current = raw_current == 2 ? 0 : raw_current + 1)
    {
        uint64_t (*o)[BUSTER_LEX_MASK_LANES] = masks[current];
        const __m512i* raw = chunk_vectors;
        const int64_t chunk_position = base_offset + chunk; // The source position of bit 0.
        const bool last_chunk = chunk_position + BUSTER_LEX_CHUNK_BYTES >= (int64_t)length;

        // I. Classification of the next chunk (its lane 0 is this chunk's lane 8).
        if (BUSTER_LIKELY(!last_chunk))
        {
            uint64_t (*next_o)[BUSTER_LEX_MASK_LANES] = masks[current ^ 1];
            properties[current ^ 1][0] = properties[current][BUSTER_LEX_VECTOR_COUNT];
            for (unsigned m = BUSTER_LEX_PROPERTY_MASK_COUNT; m < NUM_MASKS; m += 1)
            {
                next_o[m][0] = o[m][BUSTER_LEX_VECTOR_COUNT];
            }
            const int64_t next_position = base_offset + chunk + BUSTER_LEX_CHUNK_BYTES; // The source position of the next chunk.
            if (BUSTER_LIKELY((next_position >= (int64_t)begin) & (next_position + BUSTER_LEX_CHUNK_BYTES + BUSTER_LEX_VECTOR_BYTES <= (int64_t)length)))
            {
                const __m512i* next_raw = (const __m512i*)(base + chunk + BUSTER_LEX_CHUNK_BYTES);
                for (unsigned j = 1; j < BUSTER_LEX_LOOKAHEAD_VECTORS; j += 1)
                {
                    classify(next_raw[j], next_o, j, &properties[current ^ 1][j], &previous_classes, &previous_pairs, cplusplus, digraphs, separators);
                }
                chunk_vectors = next_raw;
            }
            else
            {
                __m512i* next_raw = raws[raw_current == 2 ? 0 : raw_current + 1];
                next_raw[0] = raw[BUSTER_LEX_VECTOR_COUNT];
                for (unsigned j = 1; j < BUSTER_LEX_LOOKAHEAD_VECTORS; j += 1)
                {
                    next_raw[j] = load_lane(base, base_offset, begin, length, chunk + BUSTER_LEX_CHUNK_BYTES + BUSTER_LEX_VECTOR_BYTES * j);
                    classify(next_raw[j], next_o, j, &properties[current ^ 1][j], &previous_classes, &previous_pairs, cplusplus, digraphs, separators);
                }
                chunk_vectors = next_raw;
            }
        }
        // III. Write-out of the previous chunk.
        if (pending)
        {
            const OutputCursors cursors = write_chunk(&deferred, source, begin, length, (OutputCursors){ cur_kind, cur_start, cur_end });
            cur_kind = cursors.kind;
            cur_start = cursors.start;
            cur_end = cursors.end;
            pending = false;
        }

        transpose_property_masks(properties[current], o);
        U64x8 padding = zero;
        uint64_t next_padding = 0;
        if (BUSTER_UNLIKELY(chunk_position + 576 > (int64_t)length))
        {
            for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
            {
                padding[j] = lane_padding(base_offset, length, chunk + BUSTER_LEX_VECTOR_BYTES * j);
            }
            next_padding = lane_padding(base_offset, length, chunk + BUSTER_LEX_CHUNK_BYTES);
        }

#define BUSTER_LEX_MASK(m) (*(const U64x8*)o[m])
#define BUSTER_LEX_NEXT(m) ((m) < BUSTER_LEX_PROPERTY_MASK_COUNT ? ((U64x8)properties[current][BUSTER_LEX_VECTOR_COUNT])[m] : o[m][BUSTER_LEX_VECTOR_COUNT])
        U64x8 newline = BUSTER_LEX_MASK(M_NEWLINE);
        U64x8 whitespace = BUSTER_LEX_MASK(M_WHITESPACE);
        U64x8 identifier = BUSTER_LEX_MASK(M_IDENTIFIER);
        U64x8 digit = BUSTER_LEX_MASK(M_DIGIT);
        const U64x8 separator_follower = BUSTER_LEX_MASK(M_SEPARATOR_FOLLOWER);
        U64x8 quote = BUSTER_LEX_MASK(M_QUOTE);
        U64x8 apostrophe = BUSTER_LEX_MASK(M_APOSTROPHE);
        U64x8 backslash = BUSTER_LEX_MASK(M_BACKSLASH);
        const U64x8 slash = BUSTER_LEX_MASK(M_SLASH);
        U64x8 star = BUSTER_LEX_MASK(M_STAR);
        U64x8 dot = BUSTER_LEX_MASK(M_DOT);
        const U64x8 exponent_sign = BUSTER_LEX_MASK(M_EXPSIGN);
        U64x8 nonascii = BUSTER_LEX_MASK(M_NONASCII);

        // II. Analysis

        // A chunk without backslashes (97% of the chunks of C sources) has no line splices or escapes.
        U64x8 splice_backslash = zero;
        U64x8 splice = zero;
        U64x8 joining_splices = zero; // Splices between characters that would make one token outside of literals and comments,
        U64x8 closing_splices = zero; // and between the '*' and '/' that would end a block comment.
        U64x8 escaped = zero;
        if (BUSTER_UNLIKELY(!is_zero(backslash) | ((previous_splice_backslash | previous_splice_cr | next_is_escaped) != 0)))
        {
            // Line splices: a backslash right before a newline (not the end of the source). They don't break lines and
            // read as whitespace, which is right unless the characters around one could be part of the same token.
            splice_backslash = backslash & shr_next(newline & ~padding, BUSTER_LEX_NEXT(M_NEWLINE) & ~next_padding, 1);
            if (BUSTER_UNLIKELY(!is_zero(splice_backslash) | ((previous_splice_backslash | previous_splice_cr) != 0)))
            {
                U64x8 cr;
                for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
                {
                    cr[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('\r'));
                }
                const uint64_t next_cr = _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('\r'));
                const U64x8 line_feed = newline & ~padding & ~cr;
                const U64x8 first_newline = shl_carry(splice_backslash, previous_splice_backslash, 1);
                const U64x8 second_newline = shl_carry(first_newline & cr, previous_splice_cr, 1) & line_feed;
                // In a splice, "\n\r" is one newline.
                if (!is_zero(first_newline & line_feed & shr_next(cr, next_cr, 1)))
                {
                    return BUSTER_LEX_NEEDS_SCALAR;
                }
                splice = splice_backslash | first_newline | second_newline;
                // A splice after whitespace (as in most multi-line macros) can't join tokens, close a comment or need
                // the scalar lexer: only the others are looked at one by one.
                const U64x8 checked_splices = splice_backslash & ~shl(whitespace, 1);
                for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
                {
                    for (uint64_t bits = checked_splices[j]; bits != 0; bits &= bits - 1)
                    {
                        const int64_t position = chunk_position + BUSTER_LEX_VECTOR_BYTES * j + __builtin_ctzll(bits);
                        const uint8_t before = source_byte(source, begin, length, position - 1);
                        const bool crlf = source_byte(source, begin, length, position + 1) == '\r' && source_byte(source, begin, length, position + 2) == '\n';
                        const uint8_t after = source_byte(source, begin, length, position + (crlf ? 3 : 2));
                        // A splice inside an escape sequence, or after an apostrophe (which can make it a digit
                        // separator, or an empty character constant).
                        if (before == '\\' || before == '\'')
                        {
                            return BUSTER_LEX_NEEDS_SCALAR;
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
                previous_splice_backslash = splice_backslash[BUSTER_LEX_VECTOR_COUNT - 1] & (1ull << 63);
                previous_splice_cr = first_newline[BUSTER_LEX_VECTOR_COUNT - 1] & cr[BUSTER_LEX_VECTOR_COUNT - 1] & (1ull << 63);
            }

            // A backslash, whitespace and a newline is a line splice too (that's rare).
            const U64x8 backslash_whitespace = backslash & shr_next(whitespace, BUSTER_LEX_NEXT(M_WHITESPACE), 1);
            if (BUSTER_UNLIKELY(!is_zero(backslash_whitespace)))
            {
                for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
                {
                    for (uint64_t bits = backslash_whitespace[j]; bits != 0; bits &= bits - 1)
                    {
                        int64_t p = chunk_position + BUSTER_LEX_VECTOR_BYTES * j + __builtin_ctzll(bits) + 1;
                        while (p < (int64_t)length && (source[p] == ' ' || source[p] == '\t' || source[p] == '\v' || source[p] == '\f'))
                        {
                            p += 1;
                        }
                        if (p < (int64_t)length && (source[p] == '\n' || source[p] == '\r'))
                        {
                            return BUSTER_LEX_NEEDS_SCALAR;
                        }
                    }
                }
            }

            // Matches the character after any odd number of backslashes (simdjson's algorithm by John Keiser).
            {
                const U64x8 escape_backslash = backslash & ~splice_backslash;
                const U64x8 potential_escape = escape_backslash & ~bit_0_if(next_is_escaped);
                const U64x8 maybe_escaped = shl(potential_escape, 1);
                const U64x8 even_series_codes_and_odd_bits = sub(maybe_escaped | odd_bits, potential_escape);
                const U64x8 escape_and_terminal_code = even_series_codes_and_odd_bits ^ odd_bits;
                escaped = escape_and_terminal_code ^ (escape_backslash | bit_0_if(next_is_escaped));
                next_is_escaped = bit_511(escape_and_terminal_code & escape_backslash);
            }
        }

        const U64x8 line_break = newline & ~splice;

        // pp-numbers: a digit (or a '.' and a digit) that doesn't continue an identifier, and everything that
        // continues it: identifier characters, '.', a sign after e/E/p/P and a digit separator.
        const U64x8 apostrophe_separator = apostrophe & shr_next(separator_follower, BUSTER_LEX_NEXT(M_SEPARATOR_FOLLOWER), 1);
        const U64x8 number_sign = exponent_sign & ~shl_carry(apostrophe, previous_apostrophe, 2); // Not "1'e+".
        const U64x8 number_continue = identifier | dot | number_sign | apostrophe_separator;
        const U64x8 number_head = (digit & ~shl_carry(identifier, previous_identifier, 1)) | (dot & shr_next(digit, BUSTER_LEX_NEXT(M_DIGIT), 1));
        uint32_t number_carry_out;
        const U64x8 number_sum = add_carry(number_continue, number_head, number_carry, &number_carry_out);
        const U64x8 number_region = ((number_sum ^ number_continue) | number_head) & number_continue;

        // Delimiters of literals and comments. A digit separator isn't a quote. Comment delimiters are marked at
        // their '/': the first character of "//" (with line comments) and "/*", and the last of "*/".
        const U64x8 quote_bounds = quote & ~escaped;
        const U64x8 apostrophe_closers = apostrophe & ~escaped;
        const U64x8 apostrophe_bounds = apostrophe_closers & ~number_region;
        const U64x8 line_comment_marks = line_comments ? slash & shr_next(slash, BUSTER_LEX_NEXT(M_SLASH), 1) : zero;
        const U64x8 block_comment_marks = slash & shr_next(star, BUSTER_LEX_NEXT(M_STAR), 1);
        const U64x8 block_comment_closers = slash & shl_carry(star, previous_star, 1);
        const uint64_t next_closers = BUSTER_LEX_NEXT(M_SLASH) & ((BUSTER_LEX_NEXT(M_STAR) << 1) | bit_511(star));
        if (BUSTER_UNLIKELY(!is_zero((block_comment_marks & block_comment_closers) | (block_comment_marks & shr_next(block_comment_closers, next_closers, 2)))))
        {
            return BUSTER_LEX_NEEDS_SCALAR; // "*/*" or "/*/"
        }

        // The block comment the chunk starts inside of (if it does) goes up to its first "*/" at or after bit
        // block_comment_skip, or to the end of the chunk. It's found first, so that the scan below starts after it.
        const U64x8 carried_start = bit_0_if(state == STATE_BLOCK_COMMENT);
        const U64x8 carried_cursor = carried_start << block_comment_skip;
        const U64x8 carried_end = (block_comment_closers & ~sub(block_comment_closers, carried_cursor & ~block_comment_closers)) |
                                  (carried_cursor & block_comment_closers);
        const U64x8 carried_region = sub(carried_end, carried_start) | carried_start | carried_end;

        // Find the literals and comments, line by line in parallel (see `scan_lines`), after that comment. The scan
        // starts each line outside of comments; if a block comment spans lines, that can be wrong, so the chunk is
        // scanned again with each line's state from the previous scan until the two agree (which is then the
        // sequential result).
        const U64x8 bounds = quote_bounds | apostrophe_bounds | line_comment_marks | block_comment_marks;
        const State scan_state = state == STATE_BLOCK_COMMENT ? STATE_OUTSIDE : state;
        const U64x8 scan_cursors = ((shl(line_break, 1) | bit_0_if(state == STATE_OUTSIDE)) & ~carried_region) | shl(carried_end, 1);
        U64x8 line_comment_region = zero;
        U64x8 literal_region = zero;
        Scan scan = {0};
        if (!is_zero((bounds & ~carried_region) | bit_0_if(scan_state != STATE_OUTSIDE)))
        {
            scan = scan_lines(scan_cursors, scan_state, line_break, bounds, quote_bounds, apostrophe_closers, apostrophe_bounds, line_comment_marks,
                              block_comment_marks, block_comment_closers);
            if (BUSTER_UNLIKELY(scan.spans_lines))
            {
                for (unsigned round = 0;; round += 1)
                {
                    if (round == 4)
                    {
                        return BUSTER_LEX_NEEDS_SCALAR;
                    }
                    // A line that starts inside a block comment is scanned from the end of the comment, if it's on the line.
                    const U64x8 inside = shl(line_break & scan.block_region, 1);
                    const U64x8 seek = block_comment_closers | line_break;
                    const U64x8 first_closers = block_comment_closers & ~sub(seek, inside);
                    const Scan rescan = scan_lines((scan_cursors & ~inside) | shl(first_closers, 1), scan_state, line_break, bounds, quote_bounds,
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
            const U64x8 carried_literal = bit_0_if((state == STATE_STRING) | (state == STATE_CHAR));
            line_comment_region = sub(line_break, scan.line_comment_starts | bit_0_if(state == STATE_LINE_COMMENT)) & ~line_break;
            literal_region = sub(scan.literal_closers | scan.literal_unterminated, scan.literal_starts | carried_literal) | scan.literal_closers;
        }
        U64x8 literal_starts = scan.literal_starts;
        const U64x8 literal_closers = scan.literal_closers;
        U64x8 literal_unterminated = scan.literal_unterminated;
        const U64x8 block_region = scan.block_region | carried_region;
        const U64x8 block_ends = scan.block_ends | carried_end;

        const U64x8 outside = ~(literal_region | line_comment_region | block_region);

        // What the next chunk starts inside of. A literal open at the end of the chunk is unterminated if the next
        // chunk starts with a newline (the end of the source counts): then it ends here.
        // (Without branches in the common cases: which of them it is depends on the source.)
        const uint64_t block_at_end = bit_511(block_region) & ~bit_511(block_ends);
        const uint64_t literal_at_end = bit_511(literal_region) & ~bit_511(literal_closers) & ~block_at_end;
        if (BUSTER_UNLIKELY(block_at_end & (uint64_t)last_chunk))
        {
            return BUSTER_LEX_NEEDS_SCALAR; // An unterminated block comment.
        }
        State next_state = block_at_end ? STATE_BLOCK_COMMENT : bit_511(line_comment_region) ? STATE_LINE_COMMENT : STATE_OUTSIDE;
        // A "*/" can't use the '*' of a "/*" that starts at bit 509, 510 or 511: the skip is the highest of those
        // 3 bits that is set (0 if none).
        const uint32_t opener_bits = (uint32_t)((block_comment_marks[7] & block_region[7]) >> 61);
        const uint32_t next_block_comment_skip = (uint32_t)-block_at_end & ((uint32_t)(opener_bits >= 2) + (uint32_t)(opener_bits >= 4));
        bool unterminated_at_end = false;
        if (BUSTER_UNLIKELY(literal_at_end))
        {
            const int32_t last_start = highest_bit_below(literal_starts, BUSTER_LEX_CHUNK_BYTES);
            const bool is_char = last_start >= 0 ? ((apostrophe[last_start >> 6] >> (last_start & 63)) & 1) != 0 : state == STATE_CHAR;
            next_state = is_char ? STATE_CHAR : STATE_STRING;
            // (The newline that starts the next chunk is part of a splice if this chunk ends with its backslash, or
            // with its "\\\r" and the newline is the '\n' after that.)
            const bool next_splice_newline = bit_511(splice_backslash) ||
                                             ((previous_splice_cr >> 63) && (_mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('\n')) & ~next_padding & 1));
            if ((BUSTER_LEX_NEXT(M_NEWLINE) & 1) && !next_splice_newline)
            {
                next_state = STATE_OUTSIDE;
                unterminated_at_end = true;
            }
        }

        // From here on, the masks are loaded again rather than kept in registers since the start of the analysis: there
        // aren't enough registers, and Clang would store them to the stack to load them back. (The barrier keeps it
        // from reusing the first loads.)
        __asm__ volatile("" ::: "memory");
        newline = BUSTER_LEX_MASK(M_NEWLINE);
        whitespace = BUSTER_LEX_MASK(M_WHITESPACE);
        identifier = BUSTER_LEX_MASK(M_IDENTIFIER);
        digit = BUSTER_LEX_MASK(M_DIGIT);
        quote = BUSTER_LEX_MASK(M_QUOTE);
        apostrophe = BUSTER_LEX_MASK(M_APOSTROPHE);
        backslash = BUSTER_LEX_MASK(M_BACKSLASH);
        star = BUSTER_LEX_MASK(M_STAR);
        dot = BUSTER_LEX_MASK(M_DOT);
        nonascii = BUSTER_LEX_MASK(M_NONASCII);
        const U64x8 hash_digraph = BUSTER_LEX_MASK(M_HASHDIG);
        const U64x8 pair_ends = BUSTER_LEX_MASK(M_OP2);
        const U64x8 triple_ends = BUSTER_LEX_MASK(M_OP3);
        const U64x8 cxx_triple_ends = cplusplus ? BUSTER_LEX_MASK(M_OP3_CXX) : zero;

        // Inputs the bitstrings don't model.
        if (BUSTER_UNLIKELY(!is_zero(((nonascii | joining_splices) & outside) | (closing_splices & block_region))))
        {
            return BUSTER_LEX_NEEDS_SCALAR;
        }
        // A stray backslash is a one-character unknown token, unless it starts a UCN (\\u, \\U or \\N) or comes before
        // a quote or another backslash (which the escape bitstring would have it escape).
        const U64x8 stray_backslash = backslash & ~splice_backslash & outside;
        if (BUSTER_UNLIKELY(!is_zero(stray_backslash)))
        {
            U64x8 risky;
            for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
            {
                risky[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('u')) | _mm512_cmpeq_epi8_mask(raw[j], splat8('U')) |
                           _mm512_cmpeq_epi8_mask(raw[j], splat8('N')) | quote[j] | apostrophe[j] | backslash[j];
            }
            const uint64_t next_risky = _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('u')) | _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('U')) |
                                        _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('N')) | BUSTER_LEX_NEXT(M_QUOTE) | BUSTER_LEX_NEXT(M_APOSTROPHE) | BUSTER_LEX_NEXT(M_BACKSLASH);
            if (!is_zero(stray_backslash & shr_next(risky, next_risky, 1)))
            {
                return BUSTER_LEX_NEEDS_SCALAR;
            }
        }

        // Literals (by their opening quote) that are unknown tokens: unterminated ones, found by mapping the newline
        // that ends each one back to its quote (if it's in this chunk), and empty character constants.
        U64x8 unknown_openers = literal_starts & apostrophe & shr_next(literal_closers, BUSTER_LEX_NEXT(M_APOSTROPHE) & 1, 1);
        if (BUSTER_UNLIKELY(!is_zero(literal_unterminated) | unterminated_at_end))
        {
            bool carried_literal_unknown = false;
            for (uint32_t lane = 0; lane < BUSTER_LEX_LOOKAHEAD_VECTORS; lane += 1)
            {
                uint64_t bits = lane < BUSTER_LEX_VECTOR_COUNT ? literal_unterminated[lane] : (uint64_t)unterminated_at_end;
                for (; bits != 0; bits &= bits - 1)
                {
                    const int32_t opener = highest_bit_below(literal_starts, lane * BUSTER_LEX_VECTOR_BYTES + (uint32_t)__builtin_ctzll(bits));
                    // (The quote of a prefixed literal that starts in the previous chunk belongs to that token.)
                    if (opener >= 0 && !(opener < BUSTER_LEX_VECTOR_BYTES && ((suppressed_starts >> opener) & 1)))
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
                kinds_out[index] = C_TOKEN_UNKNOWN | (kinds_out[index] & BUSTER_C_TOKEN_START_OF_LINE);
            }
        }
        const U64x8 literal_token_ends = literal_closers | shr(literal_unterminated, 1) | (unterminated_at_end ? most_significant : zero);

        // Multi-character punctuators: candidates from Phase I that start outside of literals, comments and
        // pp-numbers. An ellipsis only needs its first '.' outside of a pp-number: "...5" is "..." and "5", even
        // though ".5" looks like a pp-number.
        const U64x8 number_token_with_dots = number_region & outside;
        const U64x8 covered_bits = {covered, 0, 0, 0, 0, 0, 0, 0};
        const U64x8 candidate = outside & ~number_token_with_dots & ~covered_bits;
        const U64x8 hash_digraphs = shr_next(hash_digraph, BUSTER_LEX_NEXT(M_HASHDIG), 1); // The '%' of each "%:".
        U64x8 quads = hash_digraphs & shr_next(hash_digraphs, BUSTER_LEX_NEXT(M_HASHDIG) >> 1, 2) & candidate;
        U64x8 triples = shr_next(triple_ends, BUSTER_LEX_NEXT(M_OP3), 2) & candidate & ~quads;
        U64x8 pairs = shr_next(pair_ends, BUSTER_LEX_NEXT(M_OP2), 1) & candidate & ~triples & ~quads;
        if (cplusplus)
        {
            // "<=>" and "->*": the triple candidates that start with '<' and '-' (rare, so checked one at a time).
            const U64x8 cxx_triple_starts = shr_next(cxx_triple_ends, BUSTER_LEX_NEXT(M_OP3_CXX), 2) & candidate & ~quads;
            if (BUSTER_UNLIKELY(!is_zero(cxx_triple_starts)))
            {
                for (uint32_t lane = 0; lane < BUSTER_LEX_VECTOR_COUNT; lane += 1)
                {
                    for (uint64_t bits = cxx_triple_starts[lane]; bits != 0; bits &= bits - 1)
                    {
                        const int64_t position = chunk_position + BUSTER_LEX_VECTOR_BYTES * lane + __builtin_ctzll(bits);
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
            pairs |= dot & shr_next(star, BUSTER_LEX_NEXT(M_STAR), 1) & candidate & ~triples & ~quads;
        }
        U64x8 covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        if (cplusplus && BUSTER_UNLIKELY(!is_zero((pairs | triples | quads) & covers) | (bit_511(pairs) != 0)))
        {
            // "<::" is '<' and "::" unless a ':' or '>' follows: then its "<:" isn't a candidate.
            U64x8 less;
            U64x8 colon;
            U64x8 colon_or_greater;
            for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
            {
                less[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('<'));
                colon[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8(':'));
                colon_or_greater[j] = colon[j] | _mm512_cmpeq_epi8_mask(raw[j], splat8('>'));
            }
            const uint64_t next_colon = _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8(':'));
            const uint64_t next_colon_or_greater = next_colon | _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('>'));
            pairs &= ~(less & shr_next(colon, next_colon, 1) & shr_next(colon, next_colon, 2) & ~shr_next(colon_or_greater, next_colon_or_greater, 3));
            covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        }
        if (BUSTER_UNLIKELY(!is_zero((pairs | triples | quads) & covers)))
        {
            // Overlapping candidates, like "+++": take the longest from the left, then the next one that is left.
            U64x8 open_pairs = pairs;
            U64x8 open_triples = triples;
            U64x8 open_quads = quads;
            pairs = zero;
            triples = zero;
            quads = zero;
            do
            {
                const U64x8 open_covers = shl(open_pairs, 1) | shl(open_triples, 1) | shl(open_triples, 2) | shl(open_quads, 1) |
                                          shl(open_quads, 2) | shl(open_quads, 3);
                const U64x8 take_pairs = open_pairs & ~open_covers;
                const U64x8 take_triples = open_triples & ~open_covers;
                const U64x8 take_quads = open_quads & ~open_covers;
                pairs |= take_pairs;
                triples |= take_triples;
                quads |= take_quads;
                const U64x8 taken = take_pairs | take_triples | take_quads | shl(take_pairs, 1) | shl(take_triples, 1) | shl(take_triples, 2) |
                                    shl(take_quads, 1) | shl(take_quads, 2) | shl(take_quads, 3);
                open_pairs &= ~taken;
                open_triples &= ~taken;
                open_quads &= ~taken;
            } while (!is_zero(open_pairs | open_triples | open_quads));
            covers = shl(pairs, 1) | shl(triples, 1) | shl(triples, 2) | shl(quads, 1) | shl(quads, 2) | shl(quads, 3);
        }
        const U64x8 multi_starts = pairs | triples | quads;
        const U64x8 multi_ends = shl(pairs, 1) | shl(triples, 2) | shl(quads, 3);
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
        const U64x8 ellipses = triples & dot;
        const U64x8 number_token = number_token_with_dots & ~(ellipses | shl(ellipses, 1) | shl(ellipses, 2)) & ~covered_bits;
        const U64x8 number_starts = number_token & ~shl_carry(number_token, previous_number_token, 1);
        if (BUSTER_UNLIKELY(!is_zero(number_starts & ~(digit | dot))))
        {
            return BUSTER_LEX_NEEDS_SCALAR; // A digit separator that is really a quote, as in 'a'b'c'.
        }
        const uint64_t next_number_continues = (BUSTER_LEX_NEXT(M_IDENTIFIER) | BUSTER_LEX_NEXT(M_DOT) | (BUSTER_LEX_NEXT(M_EXPSIGN) & ~((apostrophe[7] >> 62) & 1)) |
                                                (BUSTER_LEX_NEXT(M_APOSTROPHE) & (BUSTER_LEX_NEXT(M_SEPARATOR_FOLLOWER) >> 1))) & 1;
        const U64x8 number_ends = number_token & ~shr_next(number_token, next_number_continues, 1);
        const U64x8 identifier_token = identifier & outside & ~number_token;
        const U64x8 identifier_starts = identifier_token & ~shl_carry(identifier_token, previous_identifier_token, 1);
        U64x8 identifier_ends = identifier_token & ~shr_next(identifier_token, BUSTER_LEX_NEXT(M_IDENTIFIER) & 1, 1);
        identifier_ends[0] &= ~suppressed_ends;

        // Every other character outside of literals and comments is a one-character token.
        const U64x8 other = ~(identifier | whitespace | newline | quote | apostrophe | backslash | nonascii);
        const U64x8 singles = (other | stray_backslash) & outside & ~number_token & ~multi_starts & ~covers & ~covered_bits;

        // Literals with a prefix (L, u8, u or U) start at the prefix, which would otherwise be an identifier.
        U64x8 literal_starts_token = literal_starts;
        literal_starts_token[0] &= ~suppressed_starts;
        U64x8 prefix_starts = zero;
        U64x8 unknown_starts = zero;
        uint64_t next_suppressed_starts = 0;
        uint64_t next_suppressed_ends = 0;
        // The quotes in the next chunk's first bytes that an identifier ending this chunk can be the prefix of: an L,
        // u8, u or U literal's, and a u8R, uR or UR raw string literal's (three bytes on).
        const uint64_t next_quotes = (BUSTER_LEX_NEXT(M_QUOTE) | BUSTER_LEX_NEXT(M_APOSTROPHE)) & (raw_strings ? 7 : 3);
        if (BUSTER_UNLIKELY(!is_zero(literal_starts & shl_carry(identifier_token, previous_identifier_token, 1)) | ((bit_511(identifier_token) & (uint64_t)(next_quotes != 0)) != 0)))
        {
            U64x8 letter;
            U64x8 u;
            U64x8 eight;
            for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
            {
                letter[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('L')) | _mm512_cmpeq_epi8_mask(raw[j], splat8('U'));
                u[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('u'));
                eight[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('8'));
            }
            letter |= u;
            const uint64_t next_eight = _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('8'));
            // Raw string literals need the scalar lexer. One starts at an identifier R, LR, u8R, uR or UR right before
            // a quote: an identifier token's start is outside of literals and comments, and so is the quote. (Before
            // C11, without C_TOKENIZER_UTF_RAW_STRINGS, the scalar lexer then finds that u8R, uR and UR start none.)
            // The scan before is right up to the first raw string literal, whose quote it takes for a string's.
            if (raw_strings)
            {
                U64x8 r;
                for (unsigned j = 0; j < BUSTER_LEX_VECTOR_COUNT; j += 1)
                {
                    r[j] = _mm512_cmpeq_epi8_mask(raw[j], splat8('R'));
                }
                const uint64_t next_r = _mm512_cmpeq_epi8_mask(raw[BUSTER_LEX_VECTOR_COUNT], splat8('R'));
                const uint64_t next_quote = BUSTER_LEX_NEXT(M_QUOTE);
                const U64x8 raw_starts = identifier_starts & ((r & shr_next(quote, next_quote, 1)) |
                                                              (letter & shr_next(r, next_r, 1) & shr_next(quote, next_quote, 2)) |
                                                              (u & shr_next(eight, next_eight, 1) & shr_next(r, next_r, 2) & shr_next(quote, next_quote, 3)));
                if (!is_zero(raw_starts))
                {
                    return BUSTER_LEX_NEEDS_SCALAR;
                }
            }
            const U64x8 one_letter = identifier_starts & letter & shr_next(literal_starts, next_quotes, 1);
            const U64x8 u8 = identifier_starts & u & shr_next(eight, next_eight, 1) & shr_next(literal_starts, next_quotes, 2);
            prefix_starts = one_letter | u8;
            identifier_ends &= ~(one_letter | shl(u8, 1));
            literal_starts_token &= ~(shl(one_letter, 1) | shl(u8, 2));
            // A prefixed literal is unknown if its quote starts one (an empty character constant in the next chunk
            // is "''" there).
            const uint64_t next_unknown = BUSTER_LEX_NEXT(M_APOSTROPHE) & (BUSTER_LEX_NEXT(M_APOSTROPHE) >> 1);
            unknown_starts = (one_letter & shr_next(unknown_openers, next_unknown, 1)) | (u8 & shr_next(unknown_openers, next_unknown, 2));
            next_suppressed_starts = bit_511(one_letter) | ((u8[7] >> 62) & 1) | (bit_511(u8) << 1);
            next_suppressed_ends = bit_511(u8);
        }
        unknown_starts |= unknown_openers & literal_starts_token;

        U64x8 all_ends = number_ends | identifier_ends | multi_ends | singles | literal_token_ends;
        all_ends[0] |= covered_end;
        U64x8 all_starts = number_starts | identifier_starts | multi_starts | singles | literal_starts_token;
        // C++'s user-defined literal suffixes: an identifier right after a literal's closing quote is part of the
        // literal if it starts with '_' or (after a string literal) is a standard library suffix.
        if (cplusplus)
        {
            const uint64_t next_identifier_start = BUSTER_LEX_NEXT(M_IDENTIFIER) & ~BUSTER_LEX_NEXT(M_DIGIT) & 1;
            const U64x8 suffixed = literal_closers & ~shl_carry(unknown_openers, previous_unknown_openers, 1) & shr_next(identifier_starts, next_identifier_start, 1);
            if (BUSTER_UNLIKELY(!is_zero(suffixed)))
            {
                for (uint32_t lane = 0; lane < BUSTER_LEX_VECTOR_COUNT; lane += 1)
                {
                    for (uint64_t bits = suffixed[lane]; bits != 0; bits &= bits - 1)
                    {
                        const int64_t position = chunk_position + BUSTER_LEX_VECTOR_BYTES * lane + __builtin_ctzll(bits);
                        if (is_literal_suffix(source, begin, length, position + 1, source_byte(source, begin, length, position) == '"'))
                        {
                            // The literal ends with the suffix, whose start (maybe in the next chunk) isn't a token's.
                            const uint32_t suffix = lane * BUSTER_LEX_VECTOR_BYTES + (uint32_t)__builtin_ctzll(bits) + 1;
                            all_ends[lane] &= ~(bits & -bits);
                            if (suffix < BUSTER_LEX_CHUNK_BYTES)
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
        const U64x8 line_starts = line_break & ~block_region;
        const U64x8 seek_in = all_starts | line_starts;
        const U64x8 seek = sub(seek_in, shl(line_starts, 1) | bit_0_if(pending_start_of_line));
        const U64x8 start_of_line = all_starts & ~seek;
        pending_start_of_line = bit_511(line_starts) | bit_511(seek & ~seek_in);

        if (!is_zero(line_starts))
        {
            last_line = (LastLine){ .line_breaks = line_starts, .token_starts = all_starts, .chunk_position = chunk_position,
                                    .first_token = (uint64_t)(cur_kind - kinds_out), .valid = true };
        }

        // This chunk's tokens, for the write-out (III) at the start of the next iteration.
        const U64x8 long_ones = triples | quads;
        const U64x8 dot_numbers = number_starts & dot;
        const U64x8 start_counts = (U64x8)_mm512_popcnt_epi64((__m512i)all_starts);
        const U64x8 end_counts = (U64x8)_mm512_popcnt_epi64((__m512i)all_ends);
        memcpy(deferred.starts, &all_starts, sizeof(deferred.starts));
        memcpy(deferred.ends, &all_ends, sizeof(deferred.ends));
        memcpy(deferred.start_of_line, &start_of_line, sizeof(deferred.start_of_line));
        memcpy(deferred.multi, &multi_starts, sizeof(deferred.multi));
        memcpy(deferred.long_ones, &long_ones, sizeof(deferred.long_ones));
        memcpy(deferred.dot_numbers, &dot_numbers, sizeof(deferred.dot_numbers));
        memcpy(deferred.prefixes, &prefix_starts, sizeof(deferred.prefixes));
        memcpy(deferred.unknowns, &unknown_starts, sizeof(deferred.unknowns));
        memcpy(deferred.start_counts, &start_counts, sizeof(deferred.start_counts));
        memcpy(deferred.end_counts, &end_counts, sizeof(deferred.end_counts));
        deferred.raw = raw;
        deferred.chunk_position = chunk_position;
        const U64x8 special = dot_numbers | prefix_starts | unknown_starts;
        deferred.special_lanes = _mm512_test_epi64_mask((__m512i)special, (__m512i)special);
        pending = true;

        previous_identifier = identifier[BUSTER_LEX_VECTOR_COUNT - 1];
        previous_apostrophe = apostrophe[BUSTER_LEX_VECTOR_COUNT - 1];
        previous_star = star[BUSTER_LEX_VECTOR_COUNT - 1];
        previous_number_token = number_token[BUSTER_LEX_VECTOR_COUNT - 1];
        previous_identifier_token = identifier_token[BUSTER_LEX_VECTOR_COUNT - 1];
        previous_unknown_openers = unknown_openers[BUSTER_LEX_VECTOR_COUNT - 1];
        number_carry = number_carry_out;
        state = next_state;
        block_comment_skip = next_block_comment_skip;
        covered = next_covered;
        covered_end = next_covered_end;
        suppressed_starts = next_suppressed_starts;
        suppressed_ends = next_suppressed_ends;
#undef BUSTER_LEX_MASK
#undef BUSTER_LEX_NEXT

        if (last_chunk)
        {
            cur_kind = write_chunk_outlined(&deferred, source, begin, length, (OutputCursors){ cur_kind, cur_start, cur_end }).kind;
            break;
        }
    }

    return cur_kind - kinds_out;
}

// The chunk loop, specialized for each language, and C's for its modes without digraphs and without "//" comments.
__attribute__((noinline)) static int64_t tokenize_c(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                    uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, true, true, true, false, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_no_digraphs(const bool separators, const bool raw_strings, const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                                uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, false, true, separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_no_line_comments(const bool separators, const bool raw_strings, const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                                     uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, true, false, separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_no_digraphs_no_line_comments(const bool separators, const bool raw_strings, const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds,
                                                                                 uint32_t* starts, uint32_t* ends, uint64_t* written, uint64_t* restart,
                                                                                 uint64_t* scalar_until)
{
    return tokenize_chunks(false, false, false, separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_cpp(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                      uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(true, true, true, true, true, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_no_separators(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                                  uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, true, true, false, false, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_cpp_no_separators(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                                    uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(true, true, true, false, true, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_raw(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                        uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, true, true, true, true, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

__attribute__((noinline)) static int64_t tokenize_c_raw_no_separators(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts,
                                                                      uint32_t* ends, uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    return tokenize_chunks(false, true, true, false, true, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
}

// The chunk loop that `flags` ask for (C_TOKENIZER_CPLUSPLUS, C_TOKENIZER_NO_DIGRAPHS and C_TOKENIZER_NO_LINE_COMMENTS;
// C++ only with digraphs and "//" comments: see c_tokenize_scalar_only).
static int64_t tokenize_with_flags(uint32_t flags, const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts, uint32_t* ends,
                                   uint64_t* written, uint64_t* restart, uint64_t* scalar_until)
{
    const bool digraphs = (flags & C_TOKENIZER_NO_DIGRAPHS) == 0;
    const bool line_comments = (flags & C_TOKENIZER_NO_LINE_COMMENTS) == 0;
    const bool separators = (flags & C_TOKENIZER_NO_DIGIT_SEPARATORS) == 0;
    const bool raw_strings = (flags & C_TOKENIZER_RAW_STRINGS) != 0;
    int64_t result;
    if (flags & C_TOKENIZER_CPLUSPLUS)
    {
        result = separators ? tokenize_cpp(source, length, begin, kinds, starts, ends, written, restart, scalar_until)
                            : tokenize_cpp_no_separators(source, length, begin, kinds, starts, ends, written, restart, scalar_until);
    }
    else if (line_comments && digraphs && raw_strings)
    {
        result = separators ? tokenize_c_raw(source, length, begin, kinds, starts, ends, written, restart, scalar_until)
                            : tokenize_c_raw_no_separators(source, length, begin, kinds, starts, ends, written, restart, scalar_until);
    }
    else if (line_comments)
    {
        result = !digraphs ? tokenize_c_no_digraphs(separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until)
                           : separators ? tokenize_c(source, length, begin, kinds, starts, ends, written, restart, scalar_until)
                                        : tokenize_c_no_separators(source, length, begin, kinds, starts, ends, written, restart, scalar_until);
    }
    else
    {
        result = digraphs ? tokenize_c_no_line_comments(separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until)
                          : tokenize_c_no_digraphs_no_line_comments(separators, raw_strings, source, length, begin, kinds, starts, ends, written, restart, scalar_until);
    }
    return result;
}

// Does a source take the scalar lexer whole with `flags`? With trigraphs, if it has one; with C_TOKENIZER_NO_DOLLARS,
// which only the scalar lexer has; and in C++ without digraphs or "//" comments, which no chunk loop lexes (C++ has
// both, and buster takes no option that removes them).
static bool c_tokenize_scalar_only(const uint8_t* source, uint64_t length, uint32_t flags)
{
    const bool cplusplus_mode = (flags & C_TOKENIZER_CPLUSPLUS) != 0 && (flags & (C_TOKENIZER_NO_DIGRAPHS | C_TOKENIZER_NO_LINE_COMMENTS)) != 0;
    return cplusplus_mode || (flags & C_TOKENIZER_NO_DOLLARS) != 0 || ((flags & C_TOKENIZER_TRIGRAPHS) && contains_trigraph(source, length));
}

BUSTER_LEXER_ENTRY int64_t c_tokenize_vectorized(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    int64_t result;
    if (length > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        result = C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    else if (length == 0)
    {
        result = 0;
    }
    else if (c_tokenize_scalar_only(source, length, flags))
    {
        result = C_TOKENIZER_ERROR_NEEDS_SCALAR;
    }
    else
    {
        const uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
        uint64_t written;
        uint64_t restart;
        uint64_t scalar_until;
        result = tokenize_with_flags(flags, source, length, begin, kinds, starts, ends, &written, &restart, &scalar_until);
    }
    return result;
}

// The vectorized path from `begin` (the source's start, past a byte order mark, or the start of a line), until a chunk
// it can't tokenize. Then the scalar lexer goes back to the start of the line before that chunk and lexes up to the
// first line that starts after it, and the vectorized path resumes there.
static int64_t c_tokenize_from(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    uint64_t count = 0;
    int64_t result = -1;
    while (result < 0)
    {
        uint64_t written;
        uint64_t restart;
        uint64_t scalar_until;
        const int64_t vectorized = tokenize_with_flags(flags, source, length, begin, kinds + count, starts + count, ends + count, &written, &restart, &scalar_until);
        if (vectorized >= 0)
        {
            result = (int64_t)count + vectorized;
        }
        else
        {
            count = c_tokenize_scalar_range(source, length, restart, true, scalar_until, kinds, starts, ends, count + written, flags, &begin);
            if (begin >= length)
            {
                result = (int64_t)count;
            }
        }
    }
    return result;
}

BUSTER_LEXER_ENTRY int64_t c_tokenize(const uint8_t* source, uint64_t length, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    int64_t result;
    if (length > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        result = C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    else if (length == 0)
    {
        result = 0;
    }
    else if (c_tokenize_scalar_only(source, length, flags))
    {
        result = c_tokenize_scalar(source, length, kinds, starts, ends, flags);
    }
    else
    {
        const uint64_t begin = length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF ? 3 : 0;
        result = c_tokenize_from(source, length, begin, kinds, starts, ends, flags);
    }
    return result;
}

BUSTER_LEXER_ENTRY int64_t c_tokenize_lines(const uint8_t* source, uint64_t length, uint64_t begin, uint8_t* kinds, uint32_t* starts, uint32_t* ends, uint32_t flags)
{
    int64_t result;
    if (length > UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK)
    {
        result = C_TOKENIZER_ERROR_FILE_TOO_BIG;
    }
    else if (begin >= length)
    {
        result = 0;
    }
    else
    {
        const bool mark = begin == 0 && length >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF;
        result = c_tokenize_from(source, length, mark ? 3 : begin, kinds, starts, ends, flags);
    }
    return result;
}
