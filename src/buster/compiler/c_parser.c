// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C parser (#127): one translation unit's token stream, as the preprocessor leaves it (c_preprocessor.c), to a
// tree, with diagnostics for what C's grammar rejects. docs/c-parser.md has the design and its 9700X measurements; the
// spike that chose it is #186.
//
// The tree. Every token is a node, punctuation included: a parent per token (BUSTER_PARSE_ROOT for the node of a
// top-level declaration), a role (ParseRole: what the token is to the grammar, which gives it a level and a parent
// rule) and a node depth. C's grammar is written as operator precedence, so that nearest-smaller-value searches over
// (node depth, level) keys build the tree (Bar-On and Vishkin, 1985): see "The tree" below. Every subtree is a run of
// tokens, so the postorder follows from the parents without a walk (parse_postorder_sorted).
//
// Two paths build the same tree:
//   - The vector path (parse_vector): passes over the whole token array, each a kernel in AVX-512 with a scalar twin
//     whose output is the same: K1 brackets, K3 type names, K2 roles, K1's second part (? : and case :), K4 parents
//     and K5 statements, then the validation pass, which flags every token whose neighbors C's grammar doesn't allow
//     there. Top-level declarations with a flagged token go to the scalar parser.
//   - The scalar parser (parse_scalar): an explicit-state parser, with frames for the open groups, the statement
//     stack and the precedence stacks, and no recursion. It is the fallback, the oracle every kernel is tested
//     against, and the home of diagnostics and error recovery: it rejects what C's grammar rejects, at the token
//     clang would name, and after an error skips to the end of the top-level declaration.
//
// The input (parse_tokens_from_output): the preprocessor's output, with keywords resolved by language mode, as the
// parser's kinds (ParseKind: keywords merged by what they mean to the grammar, literals merged), pragma lines taken
// out as events at their position, and `::` split into two colons.
//
// main.c includes this file after c_preprocessor.c; every function is static. Layout, in this order:
//   - Token kinds: ParseKind (BUSTER_PARSE_KINDS), parse_keyword_kinds and parse_kind_table.
//   - The input: ParseTokens and parse_tokens_from_output (with its scalar twin).
//   - The tree: levels, roles, thresholds and rules (BUSTER_PARSE_ROLES), and parse_select.
//   - The scalar parser.
//   - The kernels' scalar twins, the kernels in AVX-512, and the validation pass.
//   - The vector path (parse_vector) and the fallback.
//   - The postorder.
//   - The entry point: Parser, parse_initialize, parse_translation_unit and parse_release, and the driver's command.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// Token kinds

// The kinds the parser reads. The lexer's punctuators keep their own kinds; literals are merged by what the grammar
// needs (any number, any character constant, any string literal), and keywords by what they mean to the parser, since
// `int` and `long` parse alike. A keyword is one only in the language modes that have it (the preprocessor resolves
// them by mode), so `bool` is an identifier in gnu17 and a type keyword in c23.
#define BUSTER_PARSE_KINDS(X) \
    X(END) \
    X(UNKNOWN) \
    X(IDENTIFIER) \
    X(NUMBER) \
    X(CHARACTER) \
    X(STRING) \
    X(L_SQUARE) \
    X(R_SQUARE) \
    X(L_PAREN) \
    X(R_PAREN) \
    X(L_BRACE) \
    X(R_BRACE) \
    X(PERIOD) \
    X(ELLIPSIS) \
    X(AMP) \
    X(AMPAMP) \
    X(AMPEQUAL) \
    X(STAR) \
    X(STAREQUAL) \
    X(PLUS) \
    X(PLUSPLUS) \
    X(PLUSEQUAL) \
    X(MINUS) \
    X(ARROW) \
    X(MINUSMINUS) \
    X(MINUSEQUAL) \
    X(TILDE) \
    X(EXCLAIM) \
    X(EXCLAIMEQUAL) \
    X(SLASH) \
    X(SLASHEQUAL) \
    X(PERCENT) \
    X(PERCENTEQUAL) \
    X(LESS) \
    X(LESSLESS) \
    X(LESSEQUAL) \
    X(LESSLESSEQUAL) \
    X(GREATER) \
    X(GREATERGREATER) \
    X(GREATEREQUAL) \
    X(GREATERGREATEREQUAL) \
    X(CARET) \
    X(CARETEQUAL) \
    X(PIPE) \
    X(PIPEPIPE) \
    X(PIPEEQUAL) \
    X(QUESTION) \
    X(COLON) \
    X(SEMI) \
    X(EQUAL) \
    X(EQUALEQUAL) \
    X(COMMA) \
    X(TYPE) \
    X(STRUCT) \
    X(ENUM) \
    X(QUALIFIER) \
    X(ATOMIC) \
    X(CALLING_CONVENTION) \
    X(STORAGE) \
    X(TYPEDEF) \
    X(ATTRIBUTE) \
    X(ASM) \
    X(ALIGNAS) \
    X(TYPEOF) \
    X(SIZEOF) \
    X(TYPE_ARGUMENT) \
    X(GENERIC) \
    X(STATIC_ASSERT) \
    X(EXTENSION) \
    X(REAL_IMAG) \
    X(LABEL) \
    X(IF) \
    X(ELSE) \
    X(SWITCH) \
    X(CASE) \
    X(DEFAULT) \
    X(WHILE) \
    X(DO) \
    X(FOR) \
    X(GOTO) \
    X(CONTINUE) \
    X(BREAK) \
    X(RETURN)

typedef enum ParseKind
{
#define BUSTER_PARSE_KIND_ENUM(name) PARSE_KIND_##name,
    BUSTER_PARSE_KINDS(BUSTER_PARSE_KIND_ENUM)
#undef BUSTER_PARSE_KIND_ENUM
    PARSE_KIND_COUNT,
} ParseKind;

// The kernels read virtual kinds: the parser kinds, and PARSE_VKIND_TYPE_NAME for an identifier that a typedef
// declaration of the translation unit declares (K3, parse_typedefs).
#define PARSE_VKIND_TYPE_NAME PARSE_KIND_COUNT
#define BUSTER_PARSE_VKINDS (PARSE_KIND_COUNT + 1)

// Virtual kinds index 128-entry tables, two zmm registers for vpermi2b.
_Static_assert(BUSTER_PARSE_VKINDS <= 128, "parser kinds must fit 128-entry tables");

static const char* const parse_kind_names[PARSE_KIND_COUNT] =
{
#define BUSTER_PARSE_KIND_NAME(name) #name,
    BUSTER_PARSE_KINDS(BUSTER_PARSE_KIND_NAME)
#undef BUSTER_PARSE_KIND_NAME
};

// What each keyword means to the parser, by its C_NAME_ id (c_intern.c lists clang 23's keywords for C). The
// predefined identifiers (`__func__`) and C23's constants (`true`, `nullptr`) are operands; the builtins clang parses
// with a group of arguments that may hold type names (`__builtin_offsetof`, `__builtin_va_arg`) or that it reads as a
// call (`__builtin_LINE`) take TYPE_ARGUMENT; `_BitInt` and `_ExtInt` are type specifiers with a group, as `typeof`
// is. What clang only has for other targets or languages (ARM's keyword attributes, `__ptrauth`) is UNKNOWN, which no
// rule takes: an error, as clang gives one for x86-64 C.
#define BUSTER_PARSE_KEYWORD_KINDS(X) \
    X(AUTO, STORAGE) \
    X(BREAK, BREAK) \
    X(CASE, CASE) \
    X(CHAR, TYPE) \
    X(CONST, QUALIFIER) \
    X(CONTINUE, CONTINUE) \
    X(DEFAULT, DEFAULT) \
    X(DO, DO) \
    X(DOUBLE, TYPE) \
    X(ELSE, ELSE) \
    X(ENUM, ENUM) \
    X(EXTERN, STORAGE) \
    X(FLOAT, TYPE) \
    X(FOR, FOR) \
    X(GOTO, GOTO) \
    X(IF, IF) \
    X(INT, TYPE) \
    X(LONG, TYPE) \
    X(REGISTER, STORAGE) \
    X(RETURN, RETURN) \
    X(SHORT, TYPE) \
    X(SIGNED, TYPE) \
    X(SIZEOF, SIZEOF) \
    X(STATIC, STORAGE) \
    X(STRUCT, STRUCT) \
    X(SWITCH, SWITCH) \
    X(TYPEDEF, TYPEDEF) \
    X(UNION, STRUCT) \
    X(UNSIGNED, TYPE) \
    X(VOID, TYPE) \
    X(VOLATILE, QUALIFIER) \
    X(WHILE, WHILE) \
    X(INLINE, STORAGE) \
    X(RESTRICT, QUALIFIER) \
    X(ALIGNAS_C11, ALIGNAS) \
    X(ALIGNOF_C11, SIZEOF) \
    X(ATOMIC, ATOMIC) \
    X(BOOL_C99, TYPE) \
    X(COMPLEX_C99, TYPE) \
    X(GENERIC, GENERIC) \
    X(IMAGINARY, TYPE) \
    X(NORETURN, STORAGE) \
    X(STATIC_ASSERT_C11, STATIC_ASSERT) \
    X(THREAD_LOCAL_C11, STORAGE) \
    X(BITINT, TYPEOF) \
    X(DECIMAL32, TYPE) \
    X(DECIMAL64, TYPE) \
    X(DECIMAL128, TYPE) \
    X(ALIGNAS, ALIGNAS) \
    X(ALIGNOF, SIZEOF) \
    X(BOOL, TYPE) \
    X(CONSTEXPR, STORAGE) \
    X(FALSE, NUMBER) \
    X(NULLPTR, NUMBER) \
    X(STATIC_ASSERT, STATIC_ASSERT) \
    X(THREAD_LOCAL, STORAGE) \
    X(TRUE, NUMBER) \
    X(TYPEOF_UNQUAL, TYPEOF) \
    X(TYPEOF, TYPEOF) \
    X(ASM, ASM) \
    X(FLOAT16, TYPE) \
    X(BF16, TYPE) \
    X(FLOAT128, TYPE) \
    X(IBM128, TYPE) \
    X(INT128, TYPE) \
    X(FUNC, NUMBER) \
    X(FUNCTION, NUMBER) \
    X(PRETTY_FUNCTION, NUMBER) \
    X(GNU_ALIGNOF, SIZEOF) \
    X(GNU_ALIGNOF2, SIZEOF) \
    X(GNU_ASM, ASM) \
    X(GNU_ASM2, ASM) \
    X(GNU_ATTRIBUTE, ATTRIBUTE) \
    X(GNU_ATTRIBUTE2, ATTRIBUTE) \
    X(AUTO_TYPE, TYPE) \
    X(BUILTIN_CHOOSE_EXPR, TYPE_ARGUMENT) \
    X(BUILTIN_OFFSETOF, TYPE_ARGUMENT) \
    X(BUILTIN_TYPES_COMPATIBLE_P, TYPE_ARGUMENT) \
    X(BUILTIN_VA_ARG, TYPE_ARGUMENT) \
    X(BUILTIN_CONVERTVECTOR, TYPE_ARGUMENT) \
    X(GNU_COMPLEX, TYPE) \
    X(GNU_COMPLEX2, TYPE) \
    X(GNU_CONST, QUALIFIER) \
    X(GNU_CONST2, QUALIFIER) \
    X(EXTENSION, EXTENSION) \
    X(GNU_IMAG, REAL_IMAG) \
    X(GNU_IMAG2, REAL_IMAG) \
    X(GNU_INLINE, STORAGE) \
    X(GNU_INLINE2, STORAGE) \
    X(LABEL, LABEL) \
    X(GNU_REAL, REAL_IMAG) \
    X(GNU_REAL2, REAL_IMAG) \
    X(GNU_RESTRICT, QUALIFIER) \
    X(GNU_RESTRICT2, QUALIFIER) \
    X(GNU_SIGNED, TYPE) \
    X(GNU_SIGNED2, TYPE) \
    X(GNU_THREAD, STORAGE) \
    X(GNU_TYPEOF, TYPEOF) \
    X(GNU_TYPEOF2, TYPEOF) \
    X(GNU_TYPEOF_UNQUAL, TYPEOF) \
    X(GNU_TYPEOF_UNQUAL2, TYPEOF) \
    X(GNU_VOLATILE, QUALIFIER) \
    X(GNU_VOLATILE2, QUALIFIER) \
    X(NONNULL, QUALIFIER) \
    X(NULLABLE, QUALIFIER) \
    X(NULLABLE_RESULT, QUALIFIER) \
    X(NULL_UNSPECIFIED, QUALIFIER) \
    X(PRIVATE_EXTERN, STORAGE) \
    X(COUNTOF, SIZEOF) \
    X(EXTINT, TYPEOF) \
    X(ARM_AGNOSTIC, UNKNOWN) \
    X(ARM_IN, UNKNOWN) \
    X(ARM_INOUT, UNKNOWN) \
    X(ARM_LOCALLY_STREAMING, UNKNOWN) \
    X(ARM_NEW, UNKNOWN) \
    X(ARM_OUT, UNKNOWN) \
    X(ARM_PRESERVES, UNKNOWN) \
    X(ARM_STREAMING, UNKNOWN) \
    X(ARM_STREAMING_COMPATIBLE, UNKNOWN) \
    X(BUILTIN_COLUMN, TYPE_ARGUMENT) \
    X(BUILTIN_FILE, TYPE_ARGUMENT) \
    X(BUILTIN_FILE_NAME, TYPE_ARGUMENT) \
    X(BUILTIN_FUNCTION, TYPE_ARGUMENT) \
    X(BUILTIN_LINE, TYPE_ARGUMENT) \
    X(BUILTIN_AVAILABLE, TYPE_ARGUMENT) \
    X(BUILTIN_BIT_CAST, TYPE_ARGUMENT) \
    X(BUILTIN_OMP_REQUIRED_SIMD_ALIGN, TYPE_ARGUMENT) \
    X(BUILTIN_PTRAUTH_TYPE_DISCRIMINATOR, TYPE_ARGUMENT) \
    X(BUILTIN_VECTORELEMENTS, TYPE_ARGUMENT) \
    X(CDECL, CALLING_CONVENTION) \
    X(FASTCALL, CALLING_CONVENTION) \
    X(FP16, TYPE) \
    X(FUNCREF, QUALIFIER) \
    X(IS_BITWISE_CLONEABLE, TYPE_ARGUMENT) \
    X(IS_DESTRUCTIBLE, TYPE_ARGUMENT) \
    X(IS_NOTHROW_DESTRUCTIBLE, TYPE_ARGUMENT) \
    X(MODULE_PRIVATE, STORAGE) \
    X(GNU_NULLPTR, NUMBER) \
    X(OB_TRAP, QUALIFIER) \
    X(OB_WRAP, QUALIFIER) \
    X(OBJC_NO, NUMBER) \
    X(OBJC_YES, NUMBER) \
    X(PASCAL, CALLING_CONVENTION) \
    X(PTRAUTH, UNKNOWN) \
    X(REGCALL, CALLING_CONVENTION) \
    X(STDCALL, CALLING_CONVENTION) \
    X(THISCALL, CALLING_CONVENTION) \
    X(VECTORCALL, CALLING_CONVENTION)

typedef struct ParseKeywordKind ParseKeywordKind;
struct ParseKeywordKind
{
    IdentId name;
    uint8_t kind;
};

static const ParseKeywordKind parse_keyword_kinds[] =
{
#define BUSTER_PARSE_KEYWORD_KIND(name, kind) { C_NAME_##name, PARSE_KIND_##kind },
    BUSTER_PARSE_KEYWORD_KINDS(BUSTER_PARSE_KEYWORD_KIND)
#undef BUSTER_PARSE_KEYWORD_KIND
};

_Static_assert(sizeof(parse_keyword_kinds) / sizeof(parse_keyword_kinds[0]) == C_NAME_KEYWORD_COUNT, "every keyword needs a parser kind");

// The parser kind of each kind of the preprocessor's output (a C_TOKEN_*, a keyword's BUSTER_PP_KIND_KEYWORD + id - 1,
// or BUSTER_PP_KIND_PRAGMA_END), for parse_tokens_from_output's lookup: UNKNOWN for what C doesn't use after
// preprocessing (`#`, `##`, and C++'s `.*`, `->*` and `<=>`). parse_kind_table_initialize fills it.
static uint8_t parse_kind_table[256] __attribute__((aligned(64)));

static void parse_kind_table_initialize(void)
{
    static const uint8_t lexer_kinds[C_TOKEN_KIND_COUNT] =
    {
        [C_TOKEN_UNKNOWN] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_RAW_IDENTIFIER] = PARSE_KIND_IDENTIFIER,
        [C_TOKEN_NUMERIC_CONSTANT] = PARSE_KIND_NUMBER,
        [C_TOKEN_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_WIDE_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF8_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF16_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_UTF32_CHAR_CONSTANT] = PARSE_KIND_CHARACTER,
        [C_TOKEN_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_WIDE_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF8_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF16_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_UTF32_STRING_LITERAL] = PARSE_KIND_STRING,
        [C_TOKEN_L_SQUARE] = PARSE_KIND_L_SQUARE,
        [C_TOKEN_R_SQUARE] = PARSE_KIND_R_SQUARE,
        [C_TOKEN_L_PAREN] = PARSE_KIND_L_PAREN,
        [C_TOKEN_R_PAREN] = PARSE_KIND_R_PAREN,
        [C_TOKEN_L_BRACE] = PARSE_KIND_L_BRACE,
        [C_TOKEN_R_BRACE] = PARSE_KIND_R_BRACE,
        [C_TOKEN_PERIOD] = PARSE_KIND_PERIOD,
        [C_TOKEN_ELLIPSIS] = PARSE_KIND_ELLIPSIS,
        [C_TOKEN_AMP] = PARSE_KIND_AMP,
        [C_TOKEN_AMPAMP] = PARSE_KIND_AMPAMP,
        [C_TOKEN_AMPEQUAL] = PARSE_KIND_AMPEQUAL,
        [C_TOKEN_STAR] = PARSE_KIND_STAR,
        [C_TOKEN_STAREQUAL] = PARSE_KIND_STAREQUAL,
        [C_TOKEN_PLUS] = PARSE_KIND_PLUS,
        [C_TOKEN_PLUSPLUS] = PARSE_KIND_PLUSPLUS,
        [C_TOKEN_PLUSEQUAL] = PARSE_KIND_PLUSEQUAL,
        [C_TOKEN_MINUS] = PARSE_KIND_MINUS,
        [C_TOKEN_ARROW] = PARSE_KIND_ARROW,
        [C_TOKEN_MINUSMINUS] = PARSE_KIND_MINUSMINUS,
        [C_TOKEN_MINUSEQUAL] = PARSE_KIND_MINUSEQUAL,
        [C_TOKEN_TILDE] = PARSE_KIND_TILDE,
        [C_TOKEN_EXCLAIM] = PARSE_KIND_EXCLAIM,
        [C_TOKEN_EXCLAIMEQUAL] = PARSE_KIND_EXCLAIMEQUAL,
        [C_TOKEN_SLASH] = PARSE_KIND_SLASH,
        [C_TOKEN_SLASHEQUAL] = PARSE_KIND_SLASHEQUAL,
        [C_TOKEN_PERCENT] = PARSE_KIND_PERCENT,
        [C_TOKEN_PERCENTEQUAL] = PARSE_KIND_PERCENTEQUAL,
        [C_TOKEN_LESS] = PARSE_KIND_LESS,
        [C_TOKEN_LESSLESS] = PARSE_KIND_LESSLESS,
        [C_TOKEN_LESSEQUAL] = PARSE_KIND_LESSEQUAL,
        [C_TOKEN_LESSLESSEQUAL] = PARSE_KIND_LESSLESSEQUAL,
        [C_TOKEN_GREATER] = PARSE_KIND_GREATER,
        [C_TOKEN_GREATERGREATER] = PARSE_KIND_GREATERGREATER,
        [C_TOKEN_GREATEREQUAL] = PARSE_KIND_GREATEREQUAL,
        [C_TOKEN_GREATERGREATEREQUAL] = PARSE_KIND_GREATERGREATEREQUAL,
        [C_TOKEN_CARET] = PARSE_KIND_CARET,
        [C_TOKEN_CARETEQUAL] = PARSE_KIND_CARETEQUAL,
        [C_TOKEN_PIPE] = PARSE_KIND_PIPE,
        [C_TOKEN_PIPEPIPE] = PARSE_KIND_PIPEPIPE,
        [C_TOKEN_PIPEEQUAL] = PARSE_KIND_PIPEEQUAL,
        [C_TOKEN_QUESTION] = PARSE_KIND_QUESTION,
        [C_TOKEN_COLON] = PARSE_KIND_COLON,
        // Split into two colons by parse_tokens_from_output.
        [C_TOKEN_COLONCOLON] = PARSE_KIND_COLON,
        [C_TOKEN_SEMI] = PARSE_KIND_SEMI,
        [C_TOKEN_EQUAL] = PARSE_KIND_EQUAL,
        [C_TOKEN_EQUALEQUAL] = PARSE_KIND_EQUALEQUAL,
        [C_TOKEN_COMMA] = PARSE_KIND_COMMA,
        [C_TOKEN_HASH] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_HASHHASH] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_PERIODSTAR] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_ARROWSTAR] = PARSE_KIND_UNKNOWN,
        [C_TOKEN_SPACESHIP] = PARSE_KIND_UNKNOWN,
    };
    memset(parse_kind_table, PARSE_KIND_UNKNOWN, sizeof(parse_kind_table));
    memcpy(parse_kind_table, lexer_kinds, sizeof(lexer_kinds));
    for (uint32_t i = 0; i < sizeof(parse_keyword_kinds) / sizeof(parse_keyword_kinds[0]); i += 1)
    {
        parse_kind_table[BUSTER_PP_KIND_KEYWORD + parse_keyword_kinds[i].name - 1] = parse_keyword_kinds[i].kind;
    }
}

// The typedef names clang declares before every translation unit on x86-64 (`clang -Xclang -ast-dump` of an empty file
// lists them). They are identifiers, not keywords, so a parse starts with them bound as type names at file scope.
static const char* const parse_predeclared_typedefs[] =
{
    "__int128_t",
    "__uint128_t",
    "__NSConstantString",
    "__builtin_ms_va_list",
    "__builtin_va_list",
};

#define BUSTER_PARSE_PREDECLARED_TYPEDEFS (sizeof(parse_predeclared_typedefs) / sizeof(parse_predeclared_typedefs[0]))

// ---------------------------------------------------------------------------------------------------------------------
// The input

// Every per-token array of the parser has BUSTER_PARSE_TOKEN_PADDING entries before token 0 and after the last token,
// holding PARSE_KIND_END and zeros, so that a kernel can load a whole block around any token.
#define BUSTER_PARSE_TOKEN_PADDING 128

// The most tokens a translation unit may have: positions, parents and keys are 32-bit, and the kernels add their
// padding and blocks past the last token.
#define BUSTER_PARSE_MAX_TOKENS (1u << 30)

typedef struct ParseTokens ParseTokens;
struct ParseTokens
{
    uint8_t* kinds;
    // Identifiers' and keywords' IdentIds (a keyword's says which one it is, as the kind merges them), and 0 for every
    // other token.
    uint32_t* names;
    // Each token's index in the preprocessor's output: its location and spelling are there. Both halves of a split
    // `::` have the `::`'s.
    uint32_t* sources;
    // The output's flags (BUSTER_PP_START_OF_LINE, BUSTER_PP_SPACE): clang's recovery reads line breaks in places.
    uint8_t* flags;
    uint32_t count;
    uint32_t capacity;
    // The pragmas, in order: each one's '#' in the preprocessor's output, and the parser token it comes before.
    uint32_t* pragma_sources;
    uint32_t* pragma_positions;
    uint32_t pragma_count;
};

// Whether the '#' at `hash` in the output, which starts a line, begins a pragma, and where the pragma's
// BUSTER_PP_KIND_PRAGMA_END is: the preprocessor writes a pragma as '#' starting a line, the pragma's tokens (none of
// which starts a line) and PRAGMA_END. A '#' a macro made can start a line too, but the next token that starts a line
// comes before any PRAGMA_END then.
static bool parse_pragma_at(const PPTokens* output, uint32_t hash, uint32_t* end)
{
    uint32_t i = hash + 1;
    while (i < output->count && output->kinds[i] != BUSTER_PP_KIND_PRAGMA_END && (output->flags[i] & BUSTER_PP_START_OF_LINE) == 0)
    {
        i += 1;
    }
    *end = i;
    return i < output->count && output->kinds[i] == BUSTER_PP_KIND_PRAGMA_END;
}

// One token of the output at `i` into the parser's tokens at `*written`, or a whole pragma from its '#'. Returns the
// index of the output token after it.
static uint32_t parse_convert_token(const PPTokens* output, uint32_t i, ParseTokens* tokens, uint32_t* written)
{
    const uint8_t kind = output->kinds[i];
    uint32_t end = i;
    uint32_t result = i + 1;
    if (kind == C_TOKEN_HASH && (output->flags[i] & BUSTER_PP_START_OF_LINE) != 0 && parse_pragma_at(output, i, &end))
    {
        tokens->pragma_sources[tokens->pragma_count] = i;
        tokens->pragma_positions[tokens->pragma_count] = *written;
        tokens->pragma_count += 1;
        result = end + 1;
    }
    else
    {
        const uint32_t w = *written;
        tokens->kinds[w] = parse_kind_table[kind];
        tokens->names[w] = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? output->values[i] : 0;
        tokens->sources[w] = i;
        tokens->flags[w] = output->flags[i];
        // C's `::` (C23's attribute scopes, and two colons of an asm statement) parses as two colons.
        const uint32_t halves = kind == C_TOKEN_COLONCOLON ? 2 : 1;
        tokens->kinds[w + 1] = halves == 2 ? PARSE_KIND_COLON : tokens->kinds[w + 1];
        tokens->names[w + 1] = halves == 2 ? 0 : tokens->names[w + 1];
        tokens->sources[w + 1] = halves == 2 ? i : tokens->sources[w + 1];
        tokens->flags[w + 1] = halves == 2 ? 0 : tokens->flags[w + 1];
        *written = w + halves;
    }
    return result;
}

// Fills the padding around the tokens.
static void parse_tokens_pad(ParseTokens* tokens)
{
    for (uint32_t i = 0; i < BUSTER_PARSE_TOKEN_PADDING; i += 1)
    {
        tokens->kinds[tokens->count + i] = PARSE_KIND_END;
        tokens->names[tokens->count + i] = 0;
        tokens->sources[tokens->count + i] = 0;
        tokens->flags[tokens->count + i] = 0;
        tokens->flags[-1 - (int64_t)i] = 0;
        tokens->kinds[-1 - (int64_t)i] = PARSE_KIND_END;
        tokens->names[-1 - (int64_t)i] = 0;
        tokens->sources[-1 - (int64_t)i] = 0;
    }
}

// The scalar twin of parse_tokens_from_output: token by token.
static void parse_tokens_from_output_scalar(const PPTokens* output, ParseTokens* tokens)
{
    uint32_t written = 0;
    tokens->pragma_count = 0;
    uint32_t i = 0;
    while (i < output->count)
    {
        i = parse_convert_token(output, i, tokens, &written);
    }
    tokens->count = written;
    parse_tokens_pad(tokens);
}

// The output's tokens as the parser's: kinds through parse_kind_table, names for identifiers and each token's index in
// the output, 64 tokens at a time. A run of tokens without a '#', a PRAGMA_END or a `::` is stored as it is, with one
// table lookup (two vpermi2b for the table's halves, picked by the kind's bit 7) and masked stores; each of those
// three goes through parse_convert_token. `tokens` has room for twice the output's tokens (every one a `::`) and its
// pragmas, with the padding on both sides.
static void parse_tokens_from_output(const PPTokens* output, ParseTokens* tokens)
{
    const __m512i table0 = _mm512_load_si512(parse_kind_table);
    const __m512i table1 = _mm512_load_si512(parse_kind_table + 64);
    const __m512i table2 = _mm512_load_si512(parse_kind_table + 128);
    const __m512i table3 = _mm512_load_si512(parse_kind_table + 192);
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t written = 0;
    tokens->pragma_count = 0;
    uint32_t i = 0;
    while (i < output->count)
    {
        const uint32_t left = output->count - i;
        const __mmask64 live = left >= 64 ? ~(__mmask64)0 : _bzhi_u64(~0ull, left);
        const __m512i kinds = _mm512_maskz_loadu_epi8(live, output->kinds + i);
        const uint64_t special = _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8(C_TOKEN_HASH)) |
                                 _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END)) |
                                 _mm512_mask_cmpeq_epi8_mask(live, kinds, _mm512_set1_epi8(C_TOKEN_COLONCOLON));
        // The run before the first special token, or the whole block.
        const uint32_t run = special != 0 ? (uint32_t)__builtin_ctzll(special) : (left >= 64 ? 64 : left);
        const __mmask64 stored = _bzhi_u64(~0ull, run);
        const __m512i low = _mm512_permutex2var_epi8(table0, kinds, table1);
        const __m512i high = _mm512_permutex2var_epi8(table2, kinds, table3);
        const __m512i parse = _mm512_mask_blend_epi8(_mm512_movepi8_mask(kinds), low, high);
        _mm512_mask_storeu_epi8(tokens->kinds + written, stored, parse);
        _mm512_mask_storeu_epi8(tokens->flags + written, stored, _mm512_maskz_loadu_epi8(live, output->flags + i));
        // Identifiers' and keywords' values are their IdentIds.
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(stored, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER)) |
                                     (_mm512_mask_cmpge_epu8_mask(stored, kinds, _mm512_set1_epi8(BUSTER_PP_KIND_KEYWORD)) &
                                      _mm512_mask_cmplt_epu8_mask(stored, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END)));
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const __mmask16 in_run = (__mmask16)(stored >> (16 * group));
            const __m512i names = _mm512_maskz_loadu_epi32((__mmask16)(identifiers >> (16 * group)), output->values + i + 16 * group);
            _mm512_mask_storeu_epi32(tokens->names + written + 16 * group, in_run, names);
            _mm512_mask_storeu_epi32(tokens->sources + written + 16 * group, in_run, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)(i + 16 * group))));
        }
        written += run;
        i += run;
        if (special != 0)
        {
            i = parse_convert_token(output, i, tokens, &written);
        }
    }
    tokens->count = written;
    parse_tokens_pad(tokens);
}

// ---------------------------------------------------------------------------------------------------------------------
// The tree
//
// The parser's output is a parent for every token: parents[i] is the token whose node is token i's parent, or
// BUSTER_PARSE_ROOT for the node of a top-level declaration. Every token is a node, punctuation included, and a node's
// children in source order are its operands in order. The tree is C's grammar written as operator precedence, so that
// a data-parallel kernel can build it with nearest-smaller-value searches (Bar-On and Vishkin, 1985):
//
//   - Each token has a role (ParseRole), which gives it a level (ParseLevel, higher binds tighter) and a behavior.
//     Expressions use C's precedence; declarations parse as the expressions they mirror, with the specifiers as
//     right-associative prefix operators below the comma (`int *p, q` is `int` applied to `*p, q`), and statements
//     sit below everything an expression or a declaration contains.
//   - Brackets: ( ) [ ] { }, ? :, and case/default ... :. A group's closer is its node; its opener is a leaf child of
//     the closer. The closer's role gives the group's level: a call's `)` is a postfix operator whose left operand is
//     the callee, a cast's `)` a prefix operator, a grouping's `)` an operand, a struct body's `}` a specifier.
//   - Keys are (depth, level), depth first. Each token i has L(i), the nearest token before it whose key is below its
//     left threshold, and R(i), the nearest after it below its right threshold. For a left-associative level the left
//     search is strict and the right one inclusive, for a right-associative one the other way round. An opener's key
//     is (its depth, BUSTER_PARSE_LEVEL_OPENER): no search at its own depth sees it, and every search from inside its
//     group stops at it.
//   - parents[i] is the one of L(i) and R(i) with the greater key, except that an opener, a terminator or a statement
//     head found as L(i) loses to any R(i) (so the expression in a group belongs to the closer, and a statement's
//     tokens to its terminator); equal keys go left on a left-associative level and right otherwise. Some roles
//     instead take L(i) (a qualifier after `*`, a string literal after a string literal), R(i) (`if`, `struct`,
//     `__attribute__`: keywords before what they introduce) or their closer (openers).
//   - Statements: in a block, a statement's node is its last token (`;`, or the `}` of a compound statement), except
//     that a statement with a head (if, while, for, switch, a label, case, default, else, do) has the head as its
//     node: an if-statement's node is the `)` after its condition, whose children are `if`, `(`, the condition, the
//     then-statement's node and the `else`; the `else` has the else-statement's node. A do-statement's node is its
//     final `;`, with `do` (whose child is the body's node) and the `)` of `while (...)` as children. Which head owns
//     which statement is decided by a statement stack (parse_statement_*), not by levels.
//
// So `if (a) x = b * c; else y = -z;` is
//
//     ) [if-head] <- if, (, a, ;1, else
//     ;1 <- =1 <- x, *  (* <- b, c)
//     else <- ;2 <- =2 <- y, -  (- <- z)

#define BUSTER_PARSE_ROOT UINT32_MAX
#define BUSTER_PARSE_NONE UINT32_MAX

typedef enum ParseLevel
{
    PARSE_LEVEL_NONE,
    PARSE_LEVEL_TERMINATOR,
    PARSE_LEVEL_HEAD,
    PARSE_LEVEL_ASM_COLON,
    PARSE_LEVEL_SEPARATOR,
    PARSE_LEVEL_ASSOCIATION,
    PARSE_LEVEL_SPECIFIER,
    PARSE_LEVEL_RETURN,
    PARSE_LEVEL_COMMA,
    PARSE_LEVEL_BITFIELD,
    PARSE_LEVEL_ASSIGN,
    PARSE_LEVEL_RANGE,
    PARSE_LEVEL_CONDITIONAL,
    PARSE_LEVEL_LOGICAL_OR,
    PARSE_LEVEL_LOGICAL_AND,
    PARSE_LEVEL_BITWISE_OR,
    PARSE_LEVEL_BITWISE_XOR,
    PARSE_LEVEL_BITWISE_AND,
    PARSE_LEVEL_EQUALITY,
    PARSE_LEVEL_RELATIONAL,
    PARSE_LEVEL_SHIFT,
    PARSE_LEVEL_ADDITIVE,
    PARSE_LEVEL_MULTIPLICATIVE,
    PARSE_LEVEL_PREFIX,
    PARSE_LEVEL_POSTFIX,
    PARSE_LEVEL_OPERAND,
    // Every threshold is at most BUSTER_PARSE_LEVEL_OPERAND + 1, so an opener is invisible at its own depth.
    PARSE_LEVEL_OPENER = 31,
} ParseLevel;

// A key is depth * BUSTER_PARSE_LEVELS + level.
#define BUSTER_PARSE_LEVELS 32

// How a role's parent is found. The statement stack then overrides the parents of the statement-level tokens of blocks
// (parse_statement_*): those a head owns, and `else` and `do`.
typedef enum ParseRule
{
    // The greater of L and R (see above).
    PARSE_RULE_SELECT,
    // L: a qualifier or an attribute after `*`, and a string literal after a string literal.
    PARSE_RULE_LEFT,
    // R: a keyword before the group it introduces, `struct`, a tag with a body.
    PARSE_RULE_RIGHT,
    // R, the matching closer: an opener's right threshold is one depth up.
    PARSE_RULE_OPENER,
} ParseRule;

// The roles: name, level, how the searches compare and the parent rule. The comparisons: L for left-associative
// (strict to the left, inclusive to the right), R for right-associative (the other way round), F for strict on both
// sides (operands and terminators), S for inclusive to the left (a string literal after a string literal, which finds
// the one before it) and T for a statement head, which searches as a terminator does, so that a head no other head
// owns belongs to its block.
#define BUSTER_PARSE_ROLES(X) \
    X(NONE, NONE, F, SELECT) \
    X(ERROR, OPERAND, F, SELECT) \
    X(OPERAND, OPERAND, F, SELECT) \
    X(MEMBER, OPERAND, F, SELECT) \
    X(STRING_NEXT, OPERAND, S, LEFT) \
    X(QUALIFIER_LEFT, OPERAND, F, LEFT) \
    X(LEAF_RIGHT, OPERAND, F, RIGHT) \
    X(SPECIFIER, SPECIFIER, R, SELECT) \
    X(TAG, SPECIFIER, R, SELECT) \
    X(PREFIX, PREFIX, R, SELECT) \
    X(POSTFIX, POSTFIX, L, SELECT) \
    X(MEMBER_OPERATOR, POSTFIX, L, SELECT) \
    X(MULTIPLICATIVE, MULTIPLICATIVE, L, SELECT) \
    X(ADDITIVE, ADDITIVE, L, SELECT) \
    X(SHIFT, SHIFT, L, SELECT) \
    X(RELATIONAL, RELATIONAL, L, SELECT) \
    X(EQUALITY, EQUALITY, L, SELECT) \
    X(BITWISE_AND, BITWISE_AND, L, SELECT) \
    X(BITWISE_XOR, BITWISE_XOR, L, SELECT) \
    X(BITWISE_OR, BITWISE_OR, L, SELECT) \
    X(LOGICAL_AND, LOGICAL_AND, L, SELECT) \
    X(LOGICAL_OR, LOGICAL_OR, L, SELECT) \
    X(ASSIGN, ASSIGN, R, SELECT) \
    X(RANGE, RANGE, L, SELECT) \
    X(BITFIELD, BITFIELD, L, SELECT) \
    X(COMMA, COMMA, L, SELECT) \
    X(RETURN, RETURN, R, SELECT) \
    X(ASSOCIATION, ASSOCIATION, L, SELECT) \
    X(SEPARATOR, SEPARATOR, L, SELECT) \
    X(ASM_COLON, ASM_COLON, L, SELECT) \
    X(TERMINATOR, TERMINATOR, F, SELECT) \
    X(ELSE, HEAD, T, SELECT) \
    X(DO, HEAD, T, SELECT) \
    X(LABEL, HEAD, T, SELECT) \
    X(OPEN_GROUP, OPENER, O, OPENER) \
    X(OPEN_CALL, OPENER, O, OPENER) \
    X(OPEN_FUNCTION, OPENER, O, OPENER) \
    X(OPEN_CAST, OPENER, O, OPENER) \
    X(OPEN_COMPOUND, OPENER, O, OPENER) \
    X(OPEN_SIZEOF, OPENER, O, OPENER) \
    X(OPEN_SPECIFIER, OPENER, O, OPENER) \
    X(OPEN_ATTRIBUTE, OPENER, O, OPENER) \
    X(OPEN_ATTRIBUTE_SPECIFIER, OPENER, O, OPENER) \
    X(OPEN_ATTRIBUTE_LEFT, OPENER, O, OPENER) \
    X(OPEN_ATTRIBUTE_RIGHT, OPENER, O, OPENER) \
    X(OPEN_ATTRIBUTE_LIST, OPENER, O, OPENER) \
    X(OPEN_ASM, OPENER, O, OPENER) \
    X(OPEN_KEYWORD, OPENER, O, OPENER) \
    X(OPEN_CONDITION, OPENER, O, OPENER) \
    X(OPEN_FOR, OPENER, O, OPENER) \
    X(OPEN_SUBSCRIPT, OPENER, O, OPENER) \
    X(OPEN_BLOCK, OPENER, O, OPENER) \
    X(OPEN_BODY, OPENER, O, OPENER) \
    X(OPEN_STRUCT, OPENER, O, OPENER) \
    X(OPEN_ENUM, OPENER, O, OPENER) \
    X(OPEN_INITIALIZER, OPENER, O, OPENER) \
    X(OPEN_COMPOUND_BODY, OPENER, O, OPENER) \
    X(OPEN_TERNARY, OPENER, O, OPENER) \
    X(OPEN_CASE, OPENER, O, OPENER) \
    X(CLOSE_GROUP, OPERAND, F, SELECT) \
    X(CLOSE_CALL, POSTFIX, L, SELECT) \
    X(CLOSE_FUNCTION, POSTFIX, L, SELECT) \
    X(CLOSE_CAST, PREFIX, R, SELECT) \
    X(CLOSE_COMPOUND, OPERAND, F, SELECT) \
    X(CLOSE_SIZEOF, OPERAND, F, SELECT) \
    X(CLOSE_SPECIFIER, SPECIFIER, R, SELECT) \
    X(CLOSE_ATTRIBUTE, POSTFIX, L, SELECT) \
    X(CLOSE_ATTRIBUTE_SPECIFIER, SPECIFIER, R, SELECT) \
    X(CLOSE_ATTRIBUTE_LEFT, OPERAND, F, LEFT) \
    X(CLOSE_ATTRIBUTE_RIGHT, OPERAND, F, RIGHT) \
    X(CLOSE_ATTRIBUTE_LIST, OPERAND, F, SELECT) \
    X(CLOSE_ASM, POSTFIX, L, SELECT) \
    X(CLOSE_KEYWORD, POSTFIX, L, SELECT) \
    X(CLOSE_CONDITION, HEAD, T, SELECT) \
    X(CLOSE_FOR, HEAD, T, SELECT) \
    X(CLOSE_SUBSCRIPT, POSTFIX, L, SELECT) \
    X(CLOSE_BLOCK, TERMINATOR, F, SELECT) \
    X(CLOSE_BODY, TERMINATOR, F, SELECT) \
    X(CLOSE_STRUCT, SPECIFIER, R, SELECT) \
    X(CLOSE_ENUM, SPECIFIER, R, SELECT) \
    X(CLOSE_INITIALIZER, OPERAND, F, SELECT) \
    X(CLOSE_COMPOUND_BODY, POSTFIX, L, SELECT) \
    X(CLOSE_TERNARY, CONDITIONAL, R, SELECT) \
    X(CLOSE_CASE, HEAD, T, SELECT)

typedef enum ParseRole
{
#define BUSTER_PARSE_ROLE_ENUM(name, level, compare, rule) PARSE_ROLE_##name,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_ENUM)
#undef BUSTER_PARSE_ROLE_ENUM
    PARSE_ROLE_COUNT,
} ParseRole;

_Static_assert(PARSE_ROLE_COUNT <= 128, "roles must fit 128-entry tables");

static const char* const parse_role_names[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_NAME(name, level, compare, rule) #name,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_NAME)
#undef BUSTER_PARSE_ROLE_NAME
};

static const uint8_t parse_role_levels[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_LEVEL(name, level, compare, rule) PARSE_LEVEL_##level,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_LEVEL)
#undef BUSTER_PARSE_ROLE_LEVEL
};

// The left and right thresholds, as levels: a search finds the nearest token whose key is below (depth, threshold).
#define BUSTER_PARSE_LEFT_THRESHOLD_L(level) (level)
#define BUSTER_PARSE_LEFT_THRESHOLD_R(level) ((level) + 1)
#define BUSTER_PARSE_LEFT_THRESHOLD_F(level) (level)
#define BUSTER_PARSE_LEFT_THRESHOLD_S(level) ((level) + 1)
#define BUSTER_PARSE_LEFT_THRESHOLD_T(level) PARSE_LEVEL_TERMINATOR
#define BUSTER_PARSE_RIGHT_THRESHOLD_L(level) ((level) + 1)
#define BUSTER_PARSE_RIGHT_THRESHOLD_R(level) (level)
#define BUSTER_PARSE_RIGHT_THRESHOLD_F(level) (level)
#define BUSTER_PARSE_RIGHT_THRESHOLD_S(level) (level)
#define BUSTER_PARSE_RIGHT_THRESHOLD_T(level) PARSE_LEVEL_TERMINATOR
// An opener's right search finds the first token at its own depth again: its closer. Its left search isn't used, but
// the scalar stacks pop what is at or above a token's left threshold before they push it, so it is the opener's own
// level: below that, they would drop the candidates before the opener.
#define BUSTER_PARSE_RIGHT_THRESHOLD_O(level) BUSTER_PARSE_LEVELS
#define BUSTER_PARSE_LEFT_THRESHOLD_O(level) (level)

static const uint8_t parse_role_left_thresholds[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_LEFT(name, level, compare, rule) BUSTER_PARSE_LEFT_THRESHOLD_##compare(PARSE_LEVEL_##level),
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_LEFT)
#undef BUSTER_PARSE_ROLE_LEFT
};

static const uint8_t parse_role_right_thresholds[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_RIGHT(name, level, compare, rule) BUSTER_PARSE_RIGHT_THRESHOLD_##compare(PARSE_LEVEL_##level),
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_RIGHT)
#undef BUSTER_PARSE_ROLE_RIGHT
};

static const uint8_t parse_role_rules[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_RULE(name, level, compare, rule) PARSE_RULE_##rule,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_RULE)
#undef BUSTER_PARSE_ROLE_RULE
};

// Which way equal keys go, by level: true for left. Only levels whose operators can be both L and R of a token tie.
static const bool parse_level_ties_left[BUSTER_PARSE_LEVELS] =
{
    [PARSE_LEVEL_ASM_COLON] = true,
    [PARSE_LEVEL_SEPARATOR] = true,
    [PARSE_LEVEL_ASSOCIATION] = true,
    [PARSE_LEVEL_COMMA] = true,
    [PARSE_LEVEL_BITFIELD] = true,
    [PARSE_LEVEL_RANGE] = true,
    [PARSE_LEVEL_LOGICAL_OR] = true,
    [PARSE_LEVEL_LOGICAL_AND] = true,
    [PARSE_LEVEL_BITWISE_OR] = true,
    [PARSE_LEVEL_BITWISE_XOR] = true,
    [PARSE_LEVEL_BITWISE_AND] = true,
    [PARSE_LEVEL_EQUALITY] = true,
    [PARSE_LEVEL_RELATIONAL] = true,
    [PARSE_LEVEL_SHIFT] = true,
    [PARSE_LEVEL_ADDITIVE] = true,
    [PARSE_LEVEL_MULTIPLICATIVE] = true,
    [PARSE_LEVEL_POSTFIX] = true,
};

// Whether an L candidate of this level loses to any R: openers, terminators and heads, whose tokens to their right
// never belong to them through the levels (the expression in a group belongs to its closer, a statement's tokens to
// its terminator, and `i++` in `for (...; ...; i++)` to the `)`).
BUSTER_INLINE bool parse_level_is_weak(uint8_t level)
{
    return level == PARSE_LEVEL_OPENER || level == PARSE_LEVEL_TERMINATOR || level == PARSE_LEVEL_HEAD;
}

// The parent from L and R and their keys (depth * BUSTER_PARSE_LEVELS + level) under PARSE_RULE_SELECT: equal keys are on
// the same level, which says which way they go.
BUSTER_INLINE uint32_t parse_select_keys(uint32_t left, uint32_t left_key, uint32_t right, uint32_t right_key)
{
    uint32_t result;
    const uint8_t left_level = (uint8_t)(left_key % BUSTER_PARSE_LEVELS);
    if (left == BUSTER_PARSE_NONE || (right != BUSTER_PARSE_NONE && parse_level_is_weak(left_level)))
    {
        result = right;
    }
    else if (right == BUSTER_PARSE_NONE)
    {
        result = left;
    }
    else
    {
        const bool take_left = left_key > right_key || (left_key == right_key && parse_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}

// The same from depths and levels.
BUSTER_INLINE uint32_t parse_select(uint32_t left, uint32_t left_depth, uint8_t left_level, uint32_t right, uint32_t right_depth, uint8_t right_level)
{
    uint32_t result;
    if (left == BUSTER_PARSE_NONE || (right != BUSTER_PARSE_NONE && parse_level_is_weak(left_level)))
    {
        result = right;
    }
    else if (right == BUSTER_PARSE_NONE)
    {
        result = left;
    }
    else
    {
        const uint64_t left_key = (uint64_t)left_depth * BUSTER_PARSE_LEVELS + left_level;
        const uint64_t right_key = (uint64_t)right_depth * BUSTER_PARSE_LEVELS + right_level;
        const bool take_left = left_key > right_key || (left_key == right_key && parse_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}

// The closer's role for each opener's role (PARSE_ROLE_NONE for what isn't an opener). An attribute's opener says where
// the attribute is: after a declarator (OPEN_ATTRIBUTE, a postfix group), among specifiers, after `*` or after struct,
// union or enum. A cast's group becomes a compound literal's when `{` follows its `)`, or an abstract function
// declarator's when no operand does; the parsers decide that at the closer.
static const uint8_t parse_closer_roles[PARSE_ROLE_COUNT] =
{
    [PARSE_ROLE_OPEN_GROUP] = PARSE_ROLE_CLOSE_GROUP,
    [PARSE_ROLE_OPEN_CALL] = PARSE_ROLE_CLOSE_CALL,
    [PARSE_ROLE_OPEN_FUNCTION] = PARSE_ROLE_CLOSE_FUNCTION,
    [PARSE_ROLE_OPEN_CAST] = PARSE_ROLE_CLOSE_CAST,
    [PARSE_ROLE_OPEN_COMPOUND] = PARSE_ROLE_CLOSE_COMPOUND,
    [PARSE_ROLE_OPEN_SIZEOF] = PARSE_ROLE_CLOSE_SIZEOF,
    [PARSE_ROLE_OPEN_SPECIFIER] = PARSE_ROLE_CLOSE_SPECIFIER,
    [PARSE_ROLE_OPEN_ATTRIBUTE] = PARSE_ROLE_CLOSE_ATTRIBUTE,
    [PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER] = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER,
    [PARSE_ROLE_OPEN_ATTRIBUTE_LEFT] = PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT,
    [PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT] = PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT,
    [PARSE_ROLE_OPEN_ATTRIBUTE_LIST] = PARSE_ROLE_CLOSE_ATTRIBUTE_LIST,
    [PARSE_ROLE_OPEN_ASM] = PARSE_ROLE_CLOSE_ASM,
    [PARSE_ROLE_OPEN_KEYWORD] = PARSE_ROLE_CLOSE_KEYWORD,
    [PARSE_ROLE_OPEN_CONDITION] = PARSE_ROLE_CLOSE_CONDITION,
    [PARSE_ROLE_OPEN_FOR] = PARSE_ROLE_CLOSE_FOR,
    [PARSE_ROLE_OPEN_SUBSCRIPT] = PARSE_ROLE_CLOSE_SUBSCRIPT,
    [PARSE_ROLE_OPEN_BLOCK] = PARSE_ROLE_CLOSE_BLOCK,
    [PARSE_ROLE_OPEN_BODY] = PARSE_ROLE_CLOSE_BODY,
    [PARSE_ROLE_OPEN_STRUCT] = PARSE_ROLE_CLOSE_STRUCT,
    [PARSE_ROLE_OPEN_ENUM] = PARSE_ROLE_CLOSE_ENUM,
    [PARSE_ROLE_OPEN_INITIALIZER] = PARSE_ROLE_CLOSE_INITIALIZER,
    [PARSE_ROLE_OPEN_COMPOUND_BODY] = PARSE_ROLE_CLOSE_COMPOUND_BODY,
    [PARSE_ROLE_OPEN_TERNARY] = PARSE_ROLE_CLOSE_TERNARY,
    [PARSE_ROLE_OPEN_CASE] = PARSE_ROLE_CLOSE_CASE,
};


// ---------------------------------------------------------------------------------------------------------------------
// Diagnostics
//
// The scalar parser reports at most one syntax error per top-level declaration, the first, then skips to the
// declaration's end. Each says what clang says (its message, and its place: the token clang names, or for a missing
// `;`, just after the token before it), so that a file both reject is rejected on the same line.

#define BUSTER_PARSE_MESSAGES(X) \
    X(NONE, "") \
    X(EXPECTED_EXPRESSION, "expected expression") \
    X(EXPECTED_IDENTIFIER, "expected identifier") \
    X(EXPECTED_IDENTIFIER_OR_PAREN, "expected identifier or '('") \
    X(EXPECTED_IDENTIFIER_OR_BRACE, "expected identifier or '{'") \
    X(EXPECTED_STATEMENT, "expected statement") \
    X(EXPECTED_SEMI_AFTER_EXPRESSION, "expected ';' after expression") \
    X(EXPECTED_SEMI_AT_END_OF_DECLARATION, "expected ';' at end of declaration") \
    X(EXPECTED_SEMI_AFTER_TOP_LEVEL_DECLARATOR, "expected ';' after top level declarator") \
    X(EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST, "expected ';' at end of declaration list") \
    X(EXPECTED_SEMI_AFTER_RETURN, "expected ';' after return statement") \
    X(EXPECTED_SEMI_AFTER_BREAK, "expected ';' after break statement") \
    X(EXPECTED_SEMI_AFTER_CONTINUE, "expected ';' after continue statement") \
    X(EXPECTED_SEMI_AFTER_GOTO, "expected ';' after goto statement") \
    X(EXPECTED_SEMI_AFTER_DO_WHILE, "expected ';' after do/while statement") \
    X(EXPECTED_SEMI_AFTER_ASM, "expected ';' after asm statement") \
    X(EXPECTED_SEMI_AFTER_TOP_LEVEL_ASM, "expected ';' after top-level asm block") \
    X(EXPECTED_SEMI_AFTER_STATIC_ASSERT, "expected ';' after '_Static_assert'") \
    X(EXPECTED_SEMI_AFTER_TAG, "expected ';' after %s") \
    X(EXPECTED_SEMI_IN_FOR, "expected ';' in 'for' statement specifier") \
    X(EXPECTED_R_PAREN, "expected ')'") \
    X(EXPECTED_R_SQUARE, "expected ']'") \
    X(EXPECTED_R_BRACE, "expected '}'") \
    X(EXPECTED_COLON, "expected ':'") \
    X(EXPECTED_COMMA, "expected ','") \
    X(EXPECTED_COLON_AFTER_CASE, "expected ':' after 'case'") \
    X(EXPECTED_COLON_AFTER_DEFAULT, "expected ':' after 'default'") \
    X(EXPECTED_L_PAREN_AFTER, "expected '(' after '%s'") \
    X(EXPECTED_L_PAREN, "expected '('") \
    X(EXPECTED_L_BRACE, "expected '{'") \
    X(EXPECTED_WHILE, "expected 'while' in do/while loop") \
    X(EXPECTED_MEMBER_NAME, "expected member name or ';' after declaration specifiers") \
    X(TYPE_NAME_REQUIRES_SPECIFIER, "type name requires a specifier or qualifier") \
    X(MISSING_COMMA_BETWEEN_ENUMERATORS, "missing ',' between enumerators") \
    X(EXPECTED_BRACE_OR_COMMA, "expected '}' or ','") \
    X(EXTRANEOUS_CLOSING_BRACE, "extraneous closing brace ('}')") \
    X(FUNCTION_DEFINITION_NOT_ALLOWED, "function definition is not allowed here") \
    X(EXPECTED_PARENTHESES_AROUND_TYPE_NAME, "expected parentheses around type name in %s expression") \
    X(NAMED_JUMP, "named '%s' is only supported in C2y") \
    X(ANONYMOUS_TAG_DECLARATION, "declaration of anonymous struct must be a definition") \
    X(EMPTY_ENUM, "use of empty enum") \
    X(TYPE_SPECIFIER_MISSING, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int") \
    X(EXTRANEOUS_BEFORE_SEMI, "extraneous '%s' before ';'") \
    X(EXPECTED_STRING, "expected string literal for diagnostic message in static_assert") \
    X(EXPECTED_TYPE, "expected a type") \
    X(EXPECTED_PARAMETER, "expected parameter declarator") \
    X(EXPECTED_ASM_STRING, "expected string literal in 'asm'") \
    X(EXPECTED_L_PAREN_AFTER_ASM_OPERAND, "expected '(' after 'asm operand'") \
    X(STATEMENT_EXPRESSION_AT_FILE_SCOPE, "statement expression not allowed at file scope") \
    X(EXPECTED_DESIGNATOR, "expected '=' or another designator") \
    X(POSTFIX_AFTER_UNARY, "expression cannot be followed by a postfix '%s' operator; add parentheses") \
    X(EXPECTED_L_PAREN_AFTER_ATTRIBUTE, "expected '(' after 'attribute'") \
    X(EXPECTED_FUNCTION_BODY, "expected function body after function declarator") \
    X(UNEXPECTED_TYPE_NAME, "unexpected type name '%s': expected identifier") \
    X(MEMBER_STORAGE_CLASS, "type name does not allow storage class to be specified") \
    X(MEMBER_FUNCTION_SPECIFIER, "type name does not allow function specifier to be specified") \
    X(UNEXPECTED_TOKEN, "unexpected token") \
    X(TOO_DEEP, "nesting too deep for the parser") \
    X(OUT_OF_MEMORY, "out of memory for the parser")

typedef enum ParseMessage
{
#define BUSTER_PARSE_MESSAGE_ENUM(name, text) PARSE_MESSAGE_##name,
    BUSTER_PARSE_MESSAGES(BUSTER_PARSE_MESSAGE_ENUM)
#undef BUSTER_PARSE_MESSAGE_ENUM
    PARSE_MESSAGE_COUNT,
} ParseMessage;

static const char* const parse_messages[PARSE_MESSAGE_COUNT] =
{
#define BUSTER_PARSE_MESSAGE_TEXT(name, text) text,
    BUSTER_PARSE_MESSAGES(BUSTER_PARSE_MESSAGE_TEXT)
#undef BUSTER_PARSE_MESSAGE_TEXT
};

// Where a diagnostic points: at its token, or just after the token before it, as clang puts a missing `;` and the other
// punctuators it expects after something.
#define BUSTER_PARSE_AT_TOKEN 0
#define BUSTER_PARSE_AFTER_PREVIOUS 1

typedef struct ParseDiagnostic ParseDiagnostic;
struct ParseDiagnostic
{
    uint32_t token;
    // The token whose spelling the message names (`%s`), or BUSTER_PARSE_NONE.
    uint32_t argument;
    uint8_t message;
    uint8_t place;
    // An error, or a warning (only errors fail the parse).
    bool error;
};

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser
//
// parse_scalar: the oracle, the fallback, and the home of diagnostics and recovery. One pass over the tokens with
// explicit stacks and no recursion:
//   - Frames (ParseFrame), one per open group: the group's role, and a mode that says what the group's next token may
//     be, as Carbon's parser keeps states on a stack. Pushing a group sets what its parent frame expects after it.
//     The frame's mode and flags give each token its role from the grammar, and reject what the grammar doesn't allow
//     there (parse_scalar_error).
//   - Bindings: the meaning of each name (typedef or not) in the current scope, restored from a log at the end of
//     each scope, so typedef names are looked up exactly where they appear (#128's binding stacks, with one level).
//   - The precedence stacks, shunting-yard's: the tokens that L of a later token may still be, keys increasing, and
//     the tokens still waiting for their R, right thresholds increasing. A token that ends a token's search resolves it.
//   - The statement stack: blocks and the statement heads that still own the statement after them.
//   - Recovery: after a syntax error the rest of the top-level declaration is skipped, its tokens become ERROR leaves of
//     the root, and the parse goes on at file scope.
//
// The trees it builds for what the vector path takes are the vector path's, token for token (test-parser checks it on
// every input); what the vector path never takes (K&R definitions, C23's attributes and enum bases, implicit int)
// still gets a tree of the same roles.

typedef enum ParseMode
{
    // The start of an item: a declaration or statement in a block, a declaration at file scope, a member, a parameter,
    // an enumerator, an initializer, a for clause, or a group that holds a type name or an expression.
    PARSE_MODE_ITEM,
    PARSE_MODE_SPECIFIERS,
    // After struct, union or enum: attributes, a tag or a body.
    PARSE_MODE_TAG,
    // After a tag that a body follows.
    PARSE_MODE_TAG_BODY,
    // After an enum's `:`: its underlying type's specifiers (C23's, which clang takes in every mode), then its body or
    // what ends the declaration.
    PARSE_MODE_ENUM_BASE,
    // In a declarator, before its name or the declarator in parentheses.
    PARSE_MODE_DECLARATOR,
    // After a declarator's name: its suffixes, then what ends it.
    PARSE_MODE_AFTER_DECLARATOR,
    // After `=` in a declaration or in an initializer list: `{` or an expression.
    PARSE_MODE_INITIALIZER,
    PARSE_MODE_OPERAND,
    PARSE_MODE_OPERATOR,
    // After `.` or `->`: the member's name.
    PARSE_MODE_MEMBER,
    // After a label's name: its `:`.
    PARSE_MODE_LABEL,
    // After `default`: its `:`, in a statement or in _Generic.
    PARSE_MODE_DEFAULT,
    PARSE_MODE_ASSOCIATION,
    // After an enumerator's name: attributes, `=`, `,` or the body's `}`.
    PARSE_MODE_ENUMERATOR,
    // A `;` and nothing else: after an asm statement, _Static_assert and the `)` of a do-statement's while.
    PARSE_MODE_END_STATEMENT,
    // After goto: a label's name, or GNU's `*` and an expression.
    PARSE_MODE_GOTO,
    // After break and continue: their `;`.
    PARSE_MODE_JUMP,
    // Before the `(` of a group a keyword introduces.
    PARSE_MODE_CONDITION_PAREN,
    PARSE_MODE_FOR_PAREN,
    PARSE_MODE_KEYWORD_PAREN,
    PARSE_MODE_SIZEOF_PAREN,
    PARSE_MODE_SPECIFIER_PAREN,
    PARSE_MODE_ATTRIBUTE_PAREN,
    PARSE_MODE_ASM_PAREN,
    // In `__attribute__(`: the list's `(`, then its `)`.
    PARSE_MODE_ATTRIBUTE_LIST,
    PARSE_MODE_ATTRIBUTE_END,
    // In `[[`: the list's `[`, then the second `]` after the list's.
    PARSE_MODE_STANDARD_ATTRIBUTE_LIST,
    PARSE_MODE_STANDARD_ATTRIBUTE_END,
    // In an attribute's list, whose tokens are a balanced run: names, arguments, scopes.
    PARSE_MODE_ATTRIBUTE_TOKENS,
    // After an asm operand's `[name]`: its constraint.
    PARSE_MODE_ASM_CONSTRAINT,
    // After a compound literal's type: its `{`.
    PARSE_MODE_COMPOUND_BODY,
    // The frame's closer and nothing else: after a parameter list's `...`.
    PARSE_MODE_CLOSE,
    // After an identifier of a K&R identifier list: `,` or `)`.
    PARSE_MODE_IDENTIFIER_LIST,
    // Between a K&R definition's `)` and its body: its parameters' declarations, then `{`.
    PARSE_MODE_KR_PARAMETERS,
} ParseMode;

// Frame flags: the frame's contents are a declaration (cleared when it ends), after `typedef`, after a type specifier.
#define BUSTER_PARSE_FRAME_DECLARATION 0x0001u
#define BUSTER_PARSE_FRAME_TYPEDEF 0x0002u
#define BUSTER_PARSE_FRAME_TYPE_SEEN 0x0004u
// The declarator's last suffix is a function's, so `{` after it starts a function body.
#define BUSTER_PARSE_FRAME_FUNCTION 0x0008u
// Declarator names here aren't bound: members and type names.
#define BUSTER_PARSE_FRAME_NO_BIND 0x0010u
// Right after `*` in a declarator, where qualifiers and attributes belong to the pointer.
#define BUSTER_PARSE_FRAME_AFTER_STAR 0x0020u
// An attribute's argument list, where keywords are names, and an asm statement's operands.
#define BUSTER_PARSE_FRAME_ATTRIBUTE 0x0040u
#define BUSTER_PARSE_FRAME_ASM 0x0080u
// The operand that comes next may be absent: what ends the expression may come instead (`return;`, `f()`, a for
// clause, an asm operand list, `a ?: b`).
#define BUSTER_PARSE_FRAME_EMPTY_OK 0x0100u
// Declarators here are abstract (type names), or may be either (parameters).
#define BUSTER_PARSE_FRAME_ABSTRACT 0x0200u
#define BUSTER_PARSE_FRAME_NAME_OPTIONAL 0x0400u
// The declarator's function suffix was a K&R identifier list, which its parameters' declarations may follow.
#define BUSTER_PARSE_FRAME_KR 0x0800u
// A declaration specifier of any kind came, so that a declaration without a type specifier is an implicit int.
#define BUSTER_PARSE_FRAME_SPECIFIED 0x1000u
// A conditional-expression is being read (case labels, bit-field widths, enumerator values, _Static_assert's and
// _Alignas's operands): an assignment or a comma operator at its top level is an error.
#define BUSTER_PARSE_FRAME_CONDITIONAL 0x2000u
// The frame's declaration is a K&R definition's parameter declarations, between its `)` and its body.
#define BUSTER_PARSE_FRAME_KR_DECLARATIONS 0x4000u
// A for-statement's first clause is a declaration.
#define BUSTER_PARSE_FRAME_FOR_DECLARATION 0x8000u
// A declaration whose declarators must have a name: anything but parameters and type names.
#define BUSTER_PARSE_FRAME_NAME_REQUIRED 0x10000u
// A subscript of an array declarator, where `static`, qualifiers and `*` may come.
#define BUSTER_PARSE_FRAME_ARRAY_DECLARATOR 0x80000u
// C23's `auto` came among the specifiers, so that a declaration without a type specifier infers its type.
#define BUSTER_PARSE_FRAME_AUTO 0x100000u
// The operand just read is `sizeof` or `_Alignof` of a type name, or GNU's `&&label`, which no postfix operator may
// follow without parentheses.
#define BUSTER_PARSE_FRAME_NO_POSTFIX 0x200000u
// The declaration's specifiers have no type, so its declarator's name gets clang's implicit int error (C99 and later).
#define BUSTER_PARSE_FRAME_TYPELESS 0x400000u
// The block is a function body (or nested in one), where statement expressions are allowed.
#define BUSTER_PARSE_FRAME_IN_FUNCTION 0x20000u
// The block's declarations and statements have started, so `__label__` may no longer come.
#define BUSTER_PARSE_FRAME_STARTED 0x40000u

// What the frame's current statement is, for the diagnostics of a missing `;`.
typedef enum ParseItemKind
{
    PARSE_ITEM_EXPRESSION,
    PARSE_ITEM_RETURN,
    PARSE_ITEM_GOTO,
    PARSE_ITEM_BREAK,
    PARSE_ITEM_CONTINUE,
    PARSE_ITEM_ASM,
    PARSE_ITEM_TOP_LEVEL_ASM,
    PARSE_ITEM_STATIC_ASSERT,
    PARSE_ITEM_DO_WHILE,
    // In an initializer list: an initializer that started with a designator, so `=` comes before its value.
    PARSE_ITEM_DESIGNATOR,
} ParseItemKind;

typedef struct ParseFrame ParseFrame;
struct ParseFrame
{
    // The opener, or BUSTER_PARSE_NONE for the file.
    uint32_t opener;
    // The binding log's length when the frame's scope began, or BUSTER_PARSE_NONE if the frame has no scope.
    uint32_t scope;
    uint32_t flags;
    // The opener's role, or PARSE_ROLE_NONE for the file.
    uint8_t role;
    uint8_t mode;
    // The closer's role, which attributes choose by where they are.
    uint8_t closer_role;
    // The keyword before the opener (the parser kind), for heads and keyword groups.
    uint8_t keyword;
    // What the parent frame's mode is when this frame closes.
    uint8_t continuation;
    // For an attribute or an asm label in this frame's contents: its closer's role, and this frame's mode after it.
    uint8_t attribute_closer_role;
    uint8_t attribute_continuation;
    // struct, union or enum, while their specifier is read.
    uint8_t tag_kind;
    uint8_t statement;
    // Which argument of a keyword group this is, which clause of a for-statement's, which section of an asm
    // statement's, or how many enumerators an enum's body has.
    uint16_t argument;
    // struct, union or enum's token, while their specifier is read.
    uint32_t tag_token;
};

typedef enum ParseMeaning
{
    PARSE_MEANING_ORDINARY,
    PARSE_MEANING_TYPEDEF,
} ParseMeaning;

typedef enum ParseStatementState
{
    PARSE_STATEMENT_BLOCK,
    // A head whose statement hasn't started, then one whose statement has.
    PARSE_STATEMENT_WAITING,
    PARSE_STATEMENT_STARTED,
    // An if-statement whose then-statement is complete, which an else may still continue.
    PARSE_STATEMENT_IF_DONE,
    // A do-statement whose body is complete, waiting for `while (...)`, then for its `;`.
    PARSE_STATEMENT_DO_DONE,
    PARSE_STATEMENT_DO_WHILE,
} ParseStatementState;

typedef struct ParseStatement ParseStatement;
struct ParseStatement
{
    // The head, or for a block BUSTER_PARSE_NONE.
    uint32_t token;
    // For a block, the head that owns it (its closer's parent), and for a do-statement the head that owns it (its final
    // `;`'s parent); BUSTER_PARSE_NONE if none does.
    uint32_t owner;
    // For a do-statement, the `)` of its `while (...)`; for a for-statement, the binding log's length at its scope.
    uint32_t extra;
    uint8_t state;
    // The head's keyword (PARSE_KIND_IF, PARSE_KIND_DO, ...), or for a block whether it is a statement of its block.
    uint8_t kind;
};

// A candidate on the scalar parser's L stack.
typedef struct ParseCandidate ParseCandidate;
struct ParseCandidate
{
    uint32_t position;
    uint32_t key;
};

// A token on the R stack, waiting for the token that ends its search, with what it needs then: its L and L's key (none
// for PARSE_RULE_RIGHT, which then takes R).
typedef struct ParseWaiting ParseWaiting;
struct ParseWaiting
{
    uint32_t position;
    uint32_t threshold;
    uint32_t left;
    uint32_t left_key;
};

// The parser's output, by token.
typedef struct ParseTree ParseTree;
struct ParseTree
{
    uint8_t* roles;
    // The node depth: a group's opener and closer are at the depth outside it, its contents one deeper.
    uint32_t* depths;
    uint32_t* parents;
};

// The bindings a parse starts from: a meaning for every IdentId, PARSE_MEANING_ORDINARY but for clang's predeclared
// typedef names, which every parse leaves as it found them.
typedef struct ParseMeanings ParseMeanings;
struct ParseMeanings
{
    uint8_t* meanings;
    uint32_t capacity;
};

typedef struct ParseScalar ParseScalar;
struct ParseScalar
{
    const uint8_t* kinds;
    const uint32_t* names;
    const uint8_t* flags;
    uint32_t count;
    CLanguage language;
    uint8_t* roles;
    uint32_t* depths;
    uint32_t* parents;
    ParseFrame* frames;
    uint32_t frame_count;
    ParseCandidate* candidates;
    uint32_t candidate_count;
    ParseWaiting* waiting;
    uint32_t waiting_count;
    // By name id.
    uint8_t* meanings;
    uint32_t* log_names;
    uint8_t* log_meanings;
    uint32_t log_count;
    // The scope that a function declarator's parameters leave open for the body that follows.
    uint32_t body_scope;
    ParseStatement* statements;
    uint32_t statement_count;
    // Recovery's stack of open brackets.
    uint8_t* brackets;
    // The current top-level declaration's first token, and the binding log's length then.
    uint32_t item_begin;
    uint32_t item_log;
    // The first error in the current top-level declaration: its token (BUSTER_PARSE_NONE if none), message and place.
    uint32_t error_token;
    uint32_t error_argument;
    uint8_t error_message;
    uint8_t error_place;
    // The diagnostics, in order.
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_count;
    uint32_t diagnostic_capacity;
    uint32_t errors;
};

// The messages of a missing `;`, which clang gives at the token instead when it is a `:` or a `,` (its "common typo").
BUSTER_INLINE bool parse_message_expects_semi(uint8_t message)
{
    return message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION || message == PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_DECLARATOR || message == PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_RETURN || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_BREAK ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_CONTINUE || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_GOTO ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_DO_WHILE || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_ASM ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_ASM || message == PARSE_MESSAGE_EXPECTED_SEMI_AFTER_STATIC_ASSERT ||
           message == PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR;
}

BUSTER_INLINE void parse_scalar_error_argument(ParseScalar* parser, uint32_t token, uint8_t message, uint8_t place, uint32_t argument)
{
    if (parser->error_token == BUSTER_PARSE_NONE)
    {
        const uint8_t kind = parser->kinds[token];
        place = parse_message_expects_semi(message) && (kind == PARSE_KIND_COLON || kind == PARSE_KIND_COMMA) ? BUSTER_PARSE_AT_TOKEN : place;
        parser->error_token = token;
        parser->error_message = message;
        parser->error_place = place;
        parser->error_argument = argument;
    }
}

BUSTER_INLINE void parse_scalar_error(ParseScalar* parser, uint32_t token, uint8_t message, uint8_t place)
{
    parse_scalar_error_argument(parser, token, message, place, BUSTER_PARSE_NONE);
}

// A diagnostic that doesn't stop the parse: a warning, or an error clang reports without changing how it parses (a
// missing type specifier, which C99 and later make an error).
static void parse_scalar_note(ParseScalar* parser, uint32_t token, uint8_t message, bool error)
{
    if (parser->diagnostic_count < parser->diagnostic_capacity)
    {
        parser->diagnostics[parser->diagnostic_count] = (ParseDiagnostic){
            .token = token,
            .argument = BUSTER_PARSE_NONE,
            .message = message,
            .place = BUSTER_PARSE_AT_TOKEN,
            .error = error,
        };
        parser->diagnostic_count += 1;
    }
    parser->errors += error;
}

BUSTER_INLINE bool parse_scalar_is_typedef_name(const ParseScalar* parser, uint32_t token)
{
    return parser->kinds[token] == PARSE_KIND_IDENTIFIER && parser->meanings[parser->names[token]] == PARSE_MEANING_TYPEDEF;
}

// Whether the token can start a type name: a specifier or qualifier keyword, an attribute or a typedef name.
BUSTER_INLINE bool parse_scalar_starts_type(const ParseScalar* parser, uint32_t token)
{
    const uint8_t kind = parser->kinds[token];
    return kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_ATOMIC ||
           kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATTRIBUTE || parse_scalar_is_typedef_name(parser, token);
}

// Whether the token can start a declaration: a type name, or a storage class, typedef, _Alignas, _Static_assert,
// __label__ or __extension__ before one. In a block, a typedef name followed by `:` is a label instead.
BUSTER_INLINE bool parse_scalar_starts_declaration(const ParseScalar* parser, uint32_t token, bool block)
{
    uint32_t i = token;
    while (parser->kinds[i] == PARSE_KIND_EXTENSION)
    {
        i += 1;
    }
    const uint8_t kind = parser->kinds[i];
    const bool label = block && parser->kinds[i] == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON;
    return !label && (parse_scalar_starts_type(parser, i) || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ALIGNAS ||
                      kind == PARSE_KIND_STATIC_ASSERT || kind == PARSE_KIND_LABEL || kind == PARSE_KIND_CALLING_CONVENTION);
}

// Whether `[` at `token` starts a C23 attribute: `[[`, which C has nowhere else (a subscript or a designator can't
// start with `[`).
BUSTER_INLINE bool parse_scalar_standard_attribute(const ParseScalar* parser, uint32_t token)
{
    return parser->kinds[token] == PARSE_KIND_L_SQUARE && parser->kinds[token + 1] == PARSE_KIND_L_SQUARE;
}

BUSTER_INLINE void parse_scalar_bind(ParseScalar* parser, uint32_t token, uint8_t meaning)
{
    const uint32_t name = parser->names[token];
    parser->log_names[parser->log_count] = name;
    parser->log_meanings[parser->log_count] = parser->meanings[name];
    parser->log_count += 1;
    parser->meanings[name] = meaning;
}

BUSTER_INLINE void parse_scalar_restore(ParseScalar* parser, uint32_t mark)
{
    while (parser->log_count > mark)
    {
        parser->log_count -= 1;
        parser->meanings[parser->log_names[parser->log_count]] = parser->log_meanings[parser->log_count];
    }
}

// The innermost frame with a scope: where a declaration's names are bound.
BUSTER_INLINE bool parse_scalar_binds(const ParseScalar* parser)
{
    return (parser->frames[parser->frame_count - 1].flags & BUSTER_PARSE_FRAME_NO_BIND) == 0;
}

// The flags a group's frame inherits from its parent: no binding inside members and type names (only an attribute
// list's own items are names whatever their spelling: `__aligned__(__alignof__(long long))` is C inside its call), and
// being in a function.
#define BUSTER_PARSE_FRAME_INHERITED (BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_IN_FUNCTION)

// Pushes a frame for the group `opener` opens; the current frame continues in `continuation` once it closes. Returns
// 0 (and an error) if the frames are full, which only nesting deeper than the tokens' count can make.
BUSTER_INLINE ParseFrame* parse_scalar_push(ParseScalar* parser, uint32_t opener, uint8_t role, uint8_t mode, uint8_t continuation, bool scope)
{
    ParseFrame* parent = &parser->frames[parser->frame_count - 1];
    ParseFrame* frame = &parser->frames[parser->frame_count];
    parser->frame_count += 1;
    *frame = (ParseFrame){
        .opener = opener,
        .scope = scope ? parser->log_count : BUSTER_PARSE_NONE,
        .flags = parent->flags & BUSTER_PARSE_FRAME_INHERITED,
        .role = role,
        .mode = mode,
        .closer_role = parse_closer_roles[role],
        .keyword = parser->kinds[(int64_t)opener - 1],
        .continuation = continuation,
        .attribute_closer_role = PARSE_ROLE_NONE,
        .attribute_continuation = PARSE_MODE_ITEM,
        .tag_kind = 0,
        .statement = PARSE_ITEM_EXPRESSION,
        .argument = 0,
        .tag_token = BUSTER_PARSE_NONE,
    };
    return frame;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's statement stack

BUSTER_INLINE ParseStatement* parse_statement_top(ParseScalar* parser)
{
    return parser->statement_count != 0 ? &parser->statements[parser->statement_count - 1] : 0;
}

BUSTER_INLINE void parse_statement_pop(ParseScalar* parser)
{
    ParseStatement* top = parse_statement_top(parser);
    // A for-statement's scope, which its clauses' declarations are bound in, ends with the statement.
    if (top->kind == PARSE_KIND_FOR && top->extra != BUSTER_PARSE_NONE)
    {
        parse_scalar_restore(parser, top->extra);
    }
    parser->statement_count -= 1;
}

// A statement just completed: so did every statement whose head owned it, up to an if, which an else may still
// continue, or a do, which its while continues.
BUSTER_INLINE void parse_statement_complete(ParseScalar* parser)
{
    bool completing = true;
    while (completing)
    {
        ParseStatement* top = parse_statement_top(parser);
        completing = top != 0 && top->state == PARSE_STATEMENT_STARTED;
        if (completing && top->kind == PARSE_KIND_IF)
        {
            top->state = PARSE_STATEMENT_IF_DONE;
            completing = false;
        }
        else if (completing && top->kind == PARSE_KIND_DO)
        {
            top->state = PARSE_STATEMENT_DO_DONE;
            completing = false;
        }
        else if (completing)
        {
            parse_statement_pop(parser);
        }
    }
}

// Anything but an else ends the if-statements that were waiting for one.
BUSTER_INLINE void parse_statement_flush(ParseScalar* parser)
{
    ParseStatement* top = parse_statement_top(parser);
    while (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
    {
        parse_statement_pop(parser);
        parse_statement_complete(parser);
        top = parse_statement_top(parser);
    }
}

// The head that owns the statement starting now, if one is waiting for it; it isn't waiting any more.
BUSTER_INLINE uint32_t parse_statement_owner(ParseScalar* parser)
{
    uint32_t result = BUSTER_PARSE_NONE;
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_WAITING)
    {
        result = top->token;
        top->state = PARSE_STATEMENT_STARTED;
    }
    return result;
}

BUSTER_INLINE void parse_statement_open_block(ParseScalar* parser, bool statement)
{
    uint32_t owner = BUSTER_PARSE_NONE;
    if (statement)
    {
        parse_statement_flush(parser);
        owner = parse_statement_owner(parser);
    }
    parser->statements[parser->statement_count] = (ParseStatement){
        .token = BUSTER_PARSE_NONE,
        .owner = owner,
        .extra = BUSTER_PARSE_NONE,
        .state = PARSE_STATEMENT_BLOCK,
        .kind = statement,
    };
    parser->statement_count += 1;
}

BUSTER_INLINE void parse_statement_close_block(ParseScalar* parser, uint32_t closer)
{
    parse_statement_flush(parser);
    // C23 lets a label end a block (clang takes it in every mode); anything else left waiting is an error the modes
    // have reported.
    ParseStatement* top = parse_statement_top(parser);
    while (top != 0 && top->state != PARSE_STATEMENT_BLOCK)
    {
        parse_statement_pop(parser);
        top = parse_statement_top(parser);
    }
    if (top != 0)
    {
        const bool statement = top->kind != 0;
        if (top->owner != BUSTER_PARSE_NONE)
        {
            parser->parents[closer] = top->owner;
        }
        parser->statement_count -= 1;
        if (statement)
        {
            parse_statement_complete(parser);
        }
    }
}

BUSTER_INLINE void parse_statement_terminator(ParseScalar* parser, uint32_t terminator)
{
    parse_statement_flush(parser);
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_DO_WHILE)
    {
        parser->parents[top->token] = terminator;
        parser->parents[top->extra] = terminator;
        if (top->owner != BUSTER_PARSE_NONE)
        {
            parser->parents[terminator] = top->owner;
        }
        parser->statement_count -= 1;
    }
    else
    {
        const uint32_t owner = parse_statement_owner(parser);
        if (owner != BUSTER_PARSE_NONE)
        {
            parser->parents[terminator] = owner;
        }
    }
    parse_statement_complete(parser);
}

// A head: the `)` of if, while, for or switch, a label's or case's `:`, or `do`. `kind` is its keyword. A while after a
// do's body is its do's instead.
BUSTER_INLINE void parse_statement_head(ParseScalar* parser, uint32_t head, uint8_t kind, uint32_t scope)
{
    parse_statement_flush(parser);
    ParseStatement* top = parse_statement_top(parser);
    if (kind == PARSE_KIND_WHILE && top != 0 && top->state == PARSE_STATEMENT_DO_DONE)
    {
        top->state = PARSE_STATEMENT_DO_WHILE;
        top->extra = head;
    }
    else
    {
        const uint32_t owner = parse_statement_owner(parser);
        if (owner != BUSTER_PARSE_NONE && kind != PARSE_KIND_DO)
        {
            parser->parents[head] = owner;
        }
        parser->statements[parser->statement_count] = (ParseStatement){
            .token = head,
            .owner = kind == PARSE_KIND_DO ? owner : BUSTER_PARSE_NONE,
            .extra = scope,
            .state = PARSE_STATEMENT_WAITING,
            .kind = kind,
        };
        parser->statement_count += 1;
    }
}

BUSTER_INLINE void parse_statement_else(ParseScalar* parser, uint32_t token)
{
    ParseStatement* top = parse_statement_top(parser);
    if (top != 0 && top->state == PARSE_STATEMENT_IF_DONE)
    {
        parser->parents[token] = top->token;
        *top = (ParseStatement){
            .token = token,
            .owner = BUSTER_PARSE_NONE,
            .extra = BUSTER_PARSE_NONE,
            .state = PARSE_STATEMENT_WAITING,
            .kind = PARSE_KIND_ELSE,
        };
    }
    else
    {
        // clang reads an else without an if as the start of an expression.
        parse_scalar_error(parser, token, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's precedence stacks

// Packs a role's level, left and right thresholds and rule for parse_scalar_emit, which reads them all at once.
#define BUSTER_PARSE_ROLE_INFO(role) ((uint32_t)parse_role_levels[role] | (uint32_t)parse_role_left_thresholds[role] << 8 | \
                                      (uint32_t)parse_role_right_thresholds[role] << 16 | (uint32_t)parse_role_rules[role] << 24)

static uint32_t parse_role_info[PARSE_ROLE_COUNT];

static void parse_role_info_initialize(void)
{
    for (uint32_t role = 0; role < PARSE_ROLE_COUNT; role += 1)
    {
        parse_role_info[role] = BUSTER_PARSE_ROLE_INFO(role);
    }
}

// Adds token `token` with its role and depth to the tree. Keys count depths from 1 here, so that a sentinel at the
// bottom of each stack (key 0 and threshold 0, under every key and threshold) ends every search without a check of the
// count: an L of BUSTER_PARSE_NONE, and an R search that nothing ends.
BUSTER_INLINE void parse_scalar_emit(ParseScalar* parser, uint32_t token, uint8_t role, uint32_t depth)
{
    uint32_t* const parents = parser->parents;
    ParseCandidate* const candidates = parser->candidates;
    ParseWaiting* const waiting = parser->waiting;
    uint32_t candidate_count = parser->candidate_count;
    uint32_t waiting_count = parser->waiting_count;
    const uint32_t info = parse_role_info[role];
    const uint32_t base = (depth + 1) * BUSTER_PARSE_LEVELS;
    const uint32_t key = base + (info & 0xff);
    const uint8_t rule = (uint8_t)(info >> 24);
    parser->roles[token] = role;
    parser->depths[token] = depth;
    parents[token] = BUSTER_PARSE_NONE;
    // The tokens this one ends the right search of. The statement stack has already set the parents of what a head
    // owns.
    while (waiting[waiting_count - 1].threshold > key)
    {
        waiting_count -= 1;
        const ParseWaiting entry = waiting[waiting_count];
        const uint32_t parent = parse_select_keys(entry.left, entry.left_key, token, key);
        if (parents[entry.position] == BUSTER_PARSE_NONE)
        {
            parents[entry.position] = parent;
        }
    }
    // L, then this token replaces every candidate it hides.
    const uint32_t left_threshold = base + ((info >> 8) & 0xff);
    while (candidates[candidate_count - 1].key >= left_threshold)
    {
        candidate_count -= 1;
    }
    const ParseCandidate left = candidates[candidate_count - 1];
    while (candidates[candidate_count - 1].key >= key)
    {
        candidate_count -= 1;
    }
    candidates[candidate_count] = (ParseCandidate){ .position = token, .key = key };
    candidate_count += 1;
    if (rule == PARSE_RULE_LEFT)
    {
        parents[token] = left.position;
    }
    else if (rule != PARSE_RULE_OPENER)
    {
        const bool right_only = rule == PARSE_RULE_RIGHT;
        waiting[waiting_count] = (ParseWaiting){
            .position = token,
            .threshold = base + ((info >> 16) & 0xff),
            .left = right_only ? BUSTER_PARSE_NONE : left.position,
            .left_key = left.key,
        };
        waiting_count += 1;
    }
    parser->candidate_count = candidate_count;
    parser->waiting_count = waiting_count;
}

// Resolves what is still waiting at the end, which has no R.
static void parse_scalar_finish(ParseScalar* parser)
{
    while (parser->waiting_count > 1)
    {
        parser->waiting_count -= 1;
        const ParseWaiting entry = parser->waiting[parser->waiting_count];
        const uint32_t parent = parse_select_keys(entry.left, entry.left_key, BUSTER_PARSE_NONE, 0);
        if (parser->parents[entry.position] == BUSTER_PARSE_NONE)
        {
            parser->parents[entry.position] = parent;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's grammar

// What a token does to the statement stack, after it is added to the tree.
typedef enum ParseEvent
{
    PARSE_EVENT_NONE,
    PARSE_EVENT_OPEN_BLOCK,
    PARSE_EVENT_OPEN_STATEMENT_BLOCK,
    PARSE_EVENT_CLOSE_BLOCK,
    PARSE_EVENT_TERMINATOR,
    PARSE_EVENT_HEAD,
    PARSE_EVENT_ELSE,
} ParseEvent;

// One token's decision: its role, its depth, and its statement event.
typedef struct ParseStep ParseStep;
struct ParseStep
{
    uint32_t depth;
    uint32_t scope;
    uint8_t role;
    uint8_t event;
    // The head's keyword, for PARSE_EVENT_HEAD.
    uint8_t keyword;
    // Run the token through the frame's new mode.
    bool again;
};

BUSTER_INLINE bool parse_frame_is_block(const ParseFrame* frame)
{
    return frame->role == PARSE_ROLE_OPEN_BLOCK || frame->role == PARSE_ROLE_OPEN_BODY;
}

// Whether a comma separates declarators or is the comma operator (COMMA), or separates arguments, parameters,
// initializers or enumerators (SEPARATOR, below the specifiers), by the innermost group of ( [ { around it: the ?: and
// case groups don't count, so the kernels decide it from the bracket depth of ( [ { alone.
BUSTER_INLINE uint8_t parse_frame_comma_role(const ParseScalar* parser)
{
    uint32_t index = parser->frame_count - 1;
    while (index > 0 && (parser->frames[index].role == PARSE_ROLE_OPEN_TERNARY || parser->frames[index].role == PARSE_ROLE_OPEN_CASE))
    {
        index -= 1;
    }
    const uint8_t role = parser->frames[index].role;
    const bool comma = role == PARSE_ROLE_NONE || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_STRUCT ||
                       role == PARSE_ROLE_OPEN_FOR;
    return comma ? PARSE_ROLE_COMMA : PARSE_ROLE_SEPARATOR;
}

BUSTER_INLINE bool parse_kind_is_closer(uint8_t kind)
{
    return kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
}

// Whether token `i` can't start an expression, as clang's isNotExpressionStart says after a comma operator: braces,
// some statement keywords, and declaration specifiers.
static bool parse_scalar_not_expression_start(const ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    const bool keyword = kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_FOR || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_IF ||
                         kind == PARSE_KIND_ELSE || kind == PARSE_KIND_GOTO;
    const bool specifier = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE ||
                           kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS ||
                           parse_scalar_is_typedef_name(parser, i);
    return keyword || specifier;
}

// Whether token `i` may start a declarator, as clang's MightBeDeclarator asks after a declaration's comma.
static bool parse_scalar_might_be_declarator(const ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    return kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_STAR || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_CALLING_CONVENTION ||
           kind == PARSE_KIND_QUALIFIER || parse_scalar_standard_attribute(parser, i);
}

// The error for token `i`, which can't continue what the frame holds: clang's "expected" of the item or the group, after
// the token before for a missing `;` (and the other punctuators clang expects at the end of something), at the token
// for a group's closer.
static void parse_scalar_unexpected(ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t opener_kind = frame->opener != BUSTER_PARSE_NONE ? parser->kinds[frame->opener] : PARSE_KIND_END;
    const bool statements = frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame);
    if (statements && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE) && parser->kinds[i + 1] == PARSE_KIND_SEMI)
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXTRANEOUS_BEFORE_SEMI, BUSTER_PARSE_AT_TOKEN, i);
    }
    else if (frame->role == PARSE_ROLE_NONE && frame->mode == PARSE_MODE_AFTER_DECLARATOR && (frame->flags & BUSTER_PARSE_FRAME_FUNCTION) &&
             (frame->flags & BUSTER_PARSE_FRAME_KR) == 0 && kind != PARSE_KIND_END && (kind == PARSE_KIND_EXTENSION || !parse_scalar_starts_declaration(parser, i, false)))
    {
        // After a function's declarator, what can't continue the declaration is taken for a missing body, but a
        // declaration specifier for the next declaration after a missing `;`.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_FUNCTION_BODY, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_NONE)
    {
        parse_scalar_error(parser, i, (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_DECLARATOR : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN,
                           (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? BUSTER_PARSE_AFTER_PREVIOUS : BUSTER_PARSE_AT_TOKEN);
    }
    else if (parse_frame_is_block(frame))
    {
        const uint8_t message = (frame->flags & BUSTER_PARSE_FRAME_DECLARATION) ? PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION :
                                frame->statement == PARSE_ITEM_RETURN ? PARSE_MESSAGE_EXPECTED_SEMI_AFTER_RETURN : PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION;
        parse_scalar_error(parser, i, message, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION_LIST, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_FOR)
    {
        // A third `;` where the last clause's expression would start: clang expects the expression.
        const uint8_t message = frame->argument < 2 ? PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR :
                                (frame->flags & BUSTER_PARSE_FRAME_EMPTY_OK) && frame->mode == PARSE_MODE_OPERAND ? PARSE_MESSAGE_EXPECTED_EXPRESSION : PARSE_MESSAGE_EXPECTED_R_PAREN;
        parse_scalar_error(parser, i, message, frame->argument < 2 ? BUSTER_PARSE_AFTER_PREVIOUS : BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_OPEN_CASE)
    {
        const bool is_default = parser->kinds[frame->opener] == PARSE_KIND_DEFAULT;
        parse_scalar_error(parser, i, is_default ? PARSE_MESSAGE_EXPECTED_COLON_AFTER_DEFAULT : PARSE_MESSAGE_EXPECTED_COLON_AFTER_CASE, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_TERNARY)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_OPEN_ENUM && kind == PARSE_KIND_IDENTIFIER)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_MISSING_COMMA_BETWEEN_ENUMERATORS, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_ENUM)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_BRACE_OR_COMMA, BUSTER_PARSE_AT_TOKEN);
    }
    else if (frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->argument == 0 && (frame->keyword == PARSE_KIND_STATIC_ASSERT || parser->names[(int64_t)frame->opener - 1] == C_NAME_BUILTIN_OFFSETOF))
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COMMA, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (frame->role == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC && frame->argument != 0 && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT))
    {
        // A _Generic association's type, which its `:` follows.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else
    {
        const uint8_t message = opener_kind == PARSE_KIND_L_SQUARE ? PARSE_MESSAGE_EXPECTED_R_SQUARE : opener_kind == PARSE_KIND_L_BRACE ? PARSE_MESSAGE_EXPECTED_R_BRACE :
                                PARSE_MESSAGE_EXPECTED_R_PAREN;
        parse_scalar_error(parser, i, message, BUSTER_PARSE_AT_TOKEN);
    }
}

// Closes the current frame with token `i` (a `)`, `]`, `}`, or the `:` of ? or case).
static void parse_scalar_close(ParseScalar* parser, uint32_t i, ParseStep* step)
{
    ParseFrame* frame = &parser->frames[parser->frame_count - 1];
    const uint8_t kind = parser->kinds[i];
    const bool colon = kind == PARSE_KIND_COLON;
    const uint8_t opener_kind = frame->opener != BUSTER_PARSE_NONE ? parser->kinds[frame->opener] : PARSE_KIND_END;
    const bool matches = colon ? (frame->role == PARSE_ROLE_OPEN_TERNARY || frame->role == PARSE_ROLE_OPEN_CASE) :
                         (kind == PARSE_KIND_R_PAREN && opener_kind == PARSE_KIND_L_PAREN) || (kind == PARSE_KIND_R_SQUARE && opener_kind == PARSE_KIND_L_SQUARE) ||
                         (kind == PARSE_KIND_R_BRACE && opener_kind == PARSE_KIND_L_BRACE);
    if (!matches && parser->frame_count == 1)
    {
        parse_scalar_error(parser, i, kind == PARSE_KIND_R_BRACE ? PARSE_MESSAGE_EXTRANEOUS_CLOSING_BRACE : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (!matches)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (frame->role == PARSE_ROLE_OPEN_FOR && frame->argument != 2)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_IN_FOR, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else
    {
        uint8_t role = frame->closer_role;
        uint8_t continuation = frame->continuation;
        if (frame->role == PARSE_ROLE_OPEN_CAST)
        {
            if (parser->kinds[i + 1] == PARSE_KIND_L_BRACE)
            {
                parser->roles[frame->opener] = PARSE_ROLE_OPEN_COMPOUND;
                role = PARSE_ROLE_CLOSE_COMPOUND;
                continuation = PARSE_MODE_COMPOUND_BODY;
            }
            else
            {
                continuation = PARSE_MODE_OPERAND;
            }
        }
        if (frame->role == PARSE_ROLE_OPEN_FUNCTION && parser->kinds[i + 1] == PARSE_KIND_L_BRACE)
        {
            // A function definition's parameters stay bound in its body.
            parser->body_scope = frame->scope;
        }
        else if (frame->role == PARSE_ROLE_OPEN_FUNCTION && (frame->flags & BUSTER_PARSE_FRAME_KR))
        {
            // A K&R definition's parameters are declared after its `)`, in the scope its body continues.
            parser->body_scope = frame->scope;
        }
        else if (frame->role == PARSE_ROLE_OPEN_FOR)
        {
            step->scope = frame->scope;
        }
        else if (frame->scope != BUSTER_PARSE_NONE)
        {
            parse_scalar_restore(parser, frame->scope);
        }
        if (frame->role == PARSE_ROLE_OPEN_CONDITION || frame->role == PARSE_ROLE_OPEN_FOR || frame->role == PARSE_ROLE_OPEN_CASE)
        {
            step->event = PARSE_EVENT_HEAD;
            step->keyword = frame->role == PARSE_ROLE_OPEN_CASE ? PARSE_KIND_CASE : frame->keyword;
        }
        else if (frame->role == PARSE_ROLE_OPEN_BLOCK || frame->role == PARSE_ROLE_OPEN_BODY)
        {
            step->event = PARSE_EVENT_CLOSE_BLOCK;
        }
        const bool kr = frame->role == PARSE_ROLE_OPEN_FUNCTION && (frame->flags & BUSTER_PARSE_FRAME_KR) != 0;
        const bool type_operand = frame->role == PARSE_ROLE_OPEN_SIZEOF && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT) != 0;
        parser->parents[frame->opener] = i;
        parser->frame_count -= 1;
        ParseFrame* parent = &parser->frames[parser->frame_count - 1];
        parent->mode = continuation;
        // The while of a do-statement: its `;` must follow.
        if (frame->role == PARSE_ROLE_OPEN_CONDITION && frame->keyword == PARSE_KIND_WHILE && parse_frame_is_block(parent))
        {
            const ParseStatement* top = parse_statement_top(parser);
            parent->mode = top != 0 && top->state == PARSE_STATEMENT_DO_DONE ? PARSE_MODE_END_STATEMENT : continuation;
        }
        parent->flags = kr ? parent->flags | BUSTER_PARSE_FRAME_KR : parent->flags;
        parent->flags = type_operand ? parent->flags | BUSTER_PARSE_FRAME_NO_POSTFIX : parent->flags;
        step->role = role;
        step->depth = parser->frame_count - 1;
    }
}

// A closer after an expression or a declarator: it ends the group the item is in, but a statement, a declaration or a
// member needs its `;` first. A struct's last member may go without one (clang warns, and so does GCC).
static void parse_scalar_close_item(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    if (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) || (frame->role == PARSE_ROLE_OPEN_STRUCT && parser->kinds[i] != PARSE_KIND_R_BRACE))
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else
    {
        parse_scalar_close(parser, i, step);
    }
}

// Opens a group with token `i`: the opener's depth is its parent's.
BUSTER_INLINE ParseFrame* parse_scalar_open(ParseScalar* parser, uint32_t i, uint8_t role, uint8_t mode, uint8_t continuation, bool scope, ParseStep* step)
{
    step->role = role;
    step->depth = parser->frame_count - 1;
    return parse_scalar_push(parser, i, role, mode, continuation, scope);
}

// What a declaration's state is reset to by the `;` (or `{` of a body) that ends it.
#define BUSTER_PARSE_FRAME_ITEM_STATE (BUSTER_PARSE_FRAME_DECLARATION | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_FUNCTION | \
                                       BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_EMPTY_OK | BUSTER_PARSE_FRAME_KR | BUSTER_PARSE_FRAME_SPECIFIED | \
                                       BUSTER_PARSE_FRAME_CONDITIONAL | BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_NAME_OPTIONAL | \
                                       BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_AUTO | BUSTER_PARSE_FRAME_TYPELESS)

// The end of a declaration's `;`, or of an expression statement's, in any frame that holds them.
static void parse_scalar_terminator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    step->role = PARSE_ROLE_TERMINATOR;
    if (frame->role == PARSE_ROLE_OPEN_FOR && frame->argument < 2)
    {
        frame->argument += 1;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) || frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // A K&R definition's parameter declarations go on until its body.
        const bool kr = (frame->flags & BUSTER_PARSE_FRAME_KR_DECLARATIONS) != 0;
        frame->mode = kr ? PARSE_MODE_KR_PARAMETERS : PARSE_MODE_ITEM;
        step->event = parse_frame_is_block(frame) ? PARSE_EVENT_TERMINATOR : PARSE_EVENT_NONE;
        frame->flags &= ~BUSTER_PARSE_FRAME_ITEM_STATE;
        frame->statement = PARSE_ITEM_EXPRESSION;
    }
    else
    {
        parse_scalar_unexpected(parser, i, frame);
    }
}

// The specifiers of a declaration start at `i`: with names required (declarations), optional (parameters) or absent
// (type names).
BUSTER_INLINE void parse_scalar_begin_declaration(ParseFrame* frame, uint32_t names, ParseStep* step)
{
    frame->mode = PARSE_MODE_SPECIFIERS;
    frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_DECLARATION | names;
    step->again = true;
}

// The start of an expression at `i` in the frame: `empty` if it may be absent.
BUSTER_INLINE void parse_scalar_begin_expression(ParseFrame* frame, bool empty, bool conditional, ParseStep* step)
{
    frame->mode = PARSE_MODE_OPERAND;
    frame->flags = (frame->flags & ~(BUSTER_PARSE_FRAME_EMPTY_OK | BUSTER_PARSE_FRAME_CONDITIONAL)) | (empty ? BUSTER_PARSE_FRAME_EMPTY_OK : 0) |
                   (conditional ? BUSTER_PARSE_FRAME_CONDITIONAL : 0);
    step->again = true;
}

// A C23 attribute's first `[`, in the place `role` says (OPEN_ATTRIBUTE_SPECIFIER among specifiers, OPEN_ATTRIBUTE after
// a declarator, ...), after which the frame continues in `continuation`.
BUSTER_INLINE void parse_scalar_standard_attribute_open(ParseScalar* parser, uint32_t i, uint8_t role, uint8_t continuation, ParseStep* step)
{
    ParseFrame* group = parse_scalar_open(parser, i, role, PARSE_MODE_STANDARD_ATTRIBUTE_LIST, continuation, false, step);
    group->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
}

// The kinds of argument a keyword group takes: a type name, an expression, either, or the keyword's own (none: a call
// with no arguments).
#define BUSTER_PARSE_ARGUMENT_EXPRESSION 0
#define BUSTER_PARSE_ARGUMENT_TYPE 1
#define BUSTER_PARSE_ARGUMENT_EITHER 2

// What argument `argument` of the keyword group whose keyword is `keyword` (its IdentId) may be, and how many arguments
// the group takes at most and at least.
static uint8_t parse_keyword_argument(IdentId keyword, uint8_t kind, uint32_t argument, uint32_t* most, uint32_t* least)
{
    uint8_t result = BUSTER_PARSE_ARGUMENT_EXPRESSION;
    *most = 1;
    *least = 1;
    if (kind == PARSE_KIND_STATIC_ASSERT)
    {
        *most = 2;
    }
    else if (kind == PARSE_KIND_GENERIC)
    {
        *most = UINT32_MAX;
        *least = 2;
        result = argument == 0 ? BUSTER_PARSE_ARGUMENT_EXPRESSION : BUSTER_PARSE_ARGUMENT_TYPE;
    }
    else
    {
        switch (keyword)
        {
            case C_NAME_BUILTIN_OFFSETOF:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_TYPE : BUSTER_PARSE_ARGUMENT_EXPRESSION;
                break;
            case C_NAME_BUILTIN_VA_ARG:
            case C_NAME_BUILTIN_CONVERTVECTOR:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_EXPRESSION : BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            case C_NAME_BUILTIN_BIT_CAST:
                *most = 2;
                *least = 2;
                result = argument == 0 ? BUSTER_PARSE_ARGUMENT_TYPE : BUSTER_PARSE_ARGUMENT_EXPRESSION;
                break;
            case C_NAME_BUILTIN_TYPES_COMPATIBLE_P:
                *most = 2;
                *least = 2;
                result = BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            case C_NAME_BUILTIN_CHOOSE_EXPR:
                *most = 3;
                *least = 3;
                break;
            case C_NAME_BUILTIN_COLUMN:
            case C_NAME_BUILTIN_FILE:
            case C_NAME_BUILTIN_FILE_NAME:
            case C_NAME_BUILTIN_FUNCTION:
            case C_NAME_BUILTIN_LINE:
                *most = 0;
                *least = 0;
                break;
            case C_NAME_BUILTIN_VECTORELEMENTS:
                result = BUSTER_PARSE_ARGUMENT_EITHER;
                break;
            case C_NAME_BUILTIN_OMP_REQUIRED_SIMD_ALIGN:
            case C_NAME_BUILTIN_PTRAUTH_TYPE_DISCRIMINATOR:
            case C_NAME_IS_BITWISE_CLONEABLE:
            case C_NAME_IS_DESTRUCTIBLE:
            case C_NAME_IS_NOTHROW_DESTRUCTIBLE:
                result = BUSTER_PARSE_ARGUMENT_TYPE;
                break;
            default:
                *most = UINT32_MAX;
                *least = 0;
                result = BUSTER_PARSE_ARGUMENT_EITHER;
                break;
        }
    }
    return result;
}

static void parse_scalar_item(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    const bool block = parse_frame_is_block(frame);
    if (group == PARSE_ROLE_OPEN_ENUM)
    {
        if (kind == PARSE_KIND_IDENTIFIER)
        {
            step->role = PARSE_ROLE_OPERAND;
            parse_scalar_bind(parser, i, PARSE_MEANING_ORDINARY);
            frame->mode = PARSE_MODE_ENUMERATOR;
            frame->argument += 1;
        }
        else if (kind == PARSE_KIND_R_BRACE && frame->argument == 0)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EMPTY_ENUM, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_OPEN_INITIALIZER || group == PARSE_ROLE_OPEN_COMPOUND_BODY)
    {
        if (kind == PARSE_KIND_L_BRACE)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_INITIALIZER, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, step);
        }
        else if (kind == PARSE_KIND_PERIOD)
        {
            step->role = PARSE_ROLE_MEMBER_OPERATOR;
            frame->mode = PARSE_MODE_MEMBER;
            frame->statement = PARSE_ITEM_DESIGNATOR;
        }
        else if (kind == PARSE_KIND_L_SQUARE)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
            frame->statement = PARSE_ITEM_DESIGNATOR;
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else
        {
            frame->statement = PARSE_ITEM_EXPRESSION;
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_KEYWORD)
    {
        uint32_t most;
        uint32_t least;
        const uint8_t argument = parse_keyword_argument(parser->names[(int64_t)frame->opener - 1], frame->keyword, frame->argument, &most, &least);
        const bool type = parse_scalar_starts_type(parser, i);
        if (kind == PARSE_KIND_R_PAREN && frame->argument == 0 && least == 0)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (frame->argument >= most)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
        }
        else if (frame->keyword == PARSE_KIND_GENERIC && frame->argument != 0 && kind == PARSE_KIND_DEFAULT)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_ASSOCIATION;
        }
        else if (frame->keyword == PARSE_KIND_STATIC_ASSERT && frame->argument == 1)
        {
            // The message: string literals only.
            if (kind == PARSE_KIND_STRING)
            {
                parse_scalar_begin_expression(frame, false, true, step);
            }
            else
            {
                parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_STRING, BUSTER_PARSE_AT_TOKEN);
            }
        }
        else if (argument != BUSTER_PARSE_ARGUMENT_EXPRESSION && type)
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NO_BIND, step);
        }
        else if (argument == BUSTER_PARSE_ARGUMENT_TYPE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_TYPE, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, frame->keyword == PARSE_KIND_STATIC_ASSERT, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_SIZEOF || group == PARSE_ROLE_OPEN_SPECIFIER)
    {
        // `_Atomic(` takes a type name, `_BitInt(` and `_ExtInt(` a constant, `typeof(`, `_Alignas(` and `sizeof(` either.
        const IdentId keyword = parser->names[(int64_t)frame->opener - 1];
        const bool bit_int = keyword == C_NAME_BITINT || keyword == C_NAME_EXTINT;
        const bool type = parse_scalar_starts_type(parser, i) && !bit_int;
        if (type)
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NO_BIND, step);
        }
        else if (frame->keyword == PARSE_KIND_ATOMIC)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_TYPE, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, frame->keyword == PARSE_KIND_ALIGNAS || bit_int, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_FUNCTION)
    {
        if (kind == PARSE_KIND_ELLIPSIS && frame->argument != 0)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_CLOSE;
        }
        else if (kind == PARSE_KIND_ELLIPSIS)
        {
            // C23's `f(...)`, which clang takes in every mode with a warning before C23.
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_CLOSE;
        }
        else if (kind == PARSE_KIND_R_PAREN && frame->argument == 0)
        {
            parse_scalar_close(parser, i, step);
        }
        else if ((kind != PARSE_KIND_IDENTIFIER || parse_scalar_is_typedef_name(parser, i)) && (frame->flags & BUSTER_PARSE_FRAME_KR))
        {
            // A K&R identifier list holds identifiers only.
            parse_scalar_error_argument(parser, i, kind == PARSE_KIND_IDENTIFIER ? PARSE_MESSAGE_UNEXPECTED_TYPE_NAME : PARSE_MESSAGE_EXPECTED_IDENTIFIER,
                                        BUSTER_PARSE_AT_TOKEN, i);
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_OPTIONAL, step);
        }
        else if (kind == PARSE_KIND_IDENTIFIER && (frame->argument == 0 || (frame->flags & BUSTER_PARSE_FRAME_KR)))
        {
            // A K&R identifier list.
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_IDENTIFIER_LIST;
            frame->flags |= BUSTER_PARSE_FRAME_KR;
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_PARAMETER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_OPEN_FOR)
    {
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_FOR_DECLARATION, step);
        }
        else
        {
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else if (group == PARSE_ROLE_OPEN_STRUCT)
    {
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_TYPE_NAME_REQUIRES_SPECIFIER, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (group == PARSE_ROLE_NONE)
    {
        // File scope.
        if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (parse_kind_is_closer(kind))
        {
            parse_scalar_close(parser, i, step);
        }
        else if (kind == PARSE_KIND_ASM)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_ASM_PAREN;
            frame->statement = PARSE_ITEM_TOP_LEVEL_ASM;
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (parse_scalar_starts_declaration(parser, i, false) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else if (kind == PARSE_KIND_IDENTIFIER)
        {
            // An implicit int: a declaration without specifiers, which C99 and later make an error that clang reports
            // without changing how it parses.
            frame->mode = PARSE_MODE_DECLARATOR;
            frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_DECLARATION | BUSTER_PARSE_FRAME_NAME_REQUIRED;
            if (parser->language.standard >= 1999)
            {
                parse_scalar_note(parser, i, PARSE_MESSAGE_TYPE_SPECIFIER_MISSING, true);
            }
            step->again = true;
        }
        else
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
        }
    }
    else if (block)
    {
        // A statement or a declaration. What the statement stack waits for decides what may come: after a head, a
        // statement (no declaration); after a do's body, its while.
        if (kind != PARSE_KIND_ELSE)
        {
            parse_statement_flush(parser);
        }
        const ParseStatement* top = parse_statement_top(parser);
        const bool waiting = top != 0 && top->state == PARSE_STATEMENT_WAITING;
        // A label may end a block or come before a declaration (C23's, which clang takes in every mode).
        const bool labeled = waiting && (top->kind == PARSE_KIND_COLON || top->kind == PARSE_KIND_CASE);
        const bool started = (frame->flags & BUSTER_PARSE_FRAME_STARTED) != 0;
        frame->flags |= kind != PARSE_KIND_LABEL ? BUSTER_PARSE_FRAME_STARTED : 0;
        if (top != 0 && top->state == PARSE_STATEMENT_DO_DONE && kind != PARSE_KIND_WHILE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_WHILE, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_SEMI)
        {
            parse_scalar_terminator(parser, i, frame, step);
        }
        else if (kind == PARSE_KIND_R_BRACE && waiting && !labeled)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_STATEMENT, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_R_BRACE)
        {
            parse_scalar_close(parser, i, step);
        }
        else if (parse_kind_is_closer(kind))
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_L_BRACE)
        {
            ParseFrame* inner = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BLOCK, PARSE_MODE_ITEM, PARSE_MODE_ITEM, true, step);
            inner->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
            step->event = PARSE_EVENT_OPEN_STATEMENT_BLOCK;
        }
        else if (kind == PARSE_KIND_IF || kind == PARSE_KIND_WHILE || kind == PARSE_KIND_SWITCH)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_CONDITION_PAREN;
        }
        else if (kind == PARSE_KIND_FOR)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_FOR_PAREN;
        }
        else if (kind == PARSE_KIND_DO)
        {
            step->role = PARSE_ROLE_DO;
            step->event = PARSE_EVENT_HEAD;
            step->keyword = PARSE_KIND_DO;
        }
        else if (kind == PARSE_KIND_ELSE)
        {
            step->role = PARSE_ROLE_ELSE;
            step->event = PARSE_EVENT_ELSE;
        }
        else if (kind == PARSE_KIND_CASE)
        {
            ParseFrame* label = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CASE, PARSE_MODE_OPERAND, PARSE_MODE_ITEM, false, step);
            label->flags |= BUSTER_PARSE_FRAME_CONDITIONAL;
        }
        else if (kind == PARSE_KIND_DEFAULT)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CASE, PARSE_MODE_DEFAULT, PARSE_MODE_ITEM, false, step);
        }
        else if (kind == PARSE_KIND_RETURN)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_OPERAND;
            frame->flags = (frame->flags & ~BUSTER_PARSE_FRAME_ITEM_STATE) | BUSTER_PARSE_FRAME_EMPTY_OK;
            frame->statement = PARSE_ITEM_RETURN;
        }
        else if (kind == PARSE_KIND_GOTO)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_GOTO;
            frame->statement = PARSE_ITEM_GOTO;
        }
        else if (kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE)
        {
            step->role = PARSE_ROLE_RETURN;
            frame->mode = PARSE_MODE_JUMP;
            frame->statement = kind == PARSE_KIND_BREAK ? PARSE_ITEM_BREAK : PARSE_ITEM_CONTINUE;
        }
        else if (kind == PARSE_KIND_ASM)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_ASM_PAREN;
            frame->statement = PARSE_ITEM_ASM;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->kinds[i + 1] == PARSE_KIND_COLON)
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_LABEL;
        }
        else if (kind == PARSE_KIND_LABEL && (waiting || started))
        {
            // GNU's local labels are declared at the start of a block only.
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if ((parse_scalar_starts_declaration(parser, i, true) || kind == PARSE_KIND_STATIC_ASSERT) && waiting && !labeled)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (kind == PARSE_KIND_STATIC_ASSERT)
        {
            step->role = PARSE_ROLE_LEAF_RIGHT;
            frame->mode = PARSE_MODE_KEYWORD_PAREN;
        }
        else if (parse_scalar_starts_declaration(parser, i, true) || parse_scalar_standard_attribute(parser, i))
        {
            parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED, step);
        }
        else
        {
            frame->statement = PARSE_ITEM_EXPRESSION;
            parse_scalar_begin_expression(frame, false, false, step);
        }
    }
    else
    {
        parse_scalar_begin_expression(frame, false, false, step);
    }
}

// A declaration's specifiers have ended at `i`: a declarator, or the declaration's end, comes. A declaration without a
// type specifier has an implicit int, which C99 and later make an error (but C23's `auto`, which infers the type).
BUSTER_INLINE void parse_scalar_end_specifiers(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint32_t flags = frame->flags;
    const bool typeless = (flags & BUSTER_PARSE_FRAME_TYPE_SEEN) == 0 && (flags & BUSTER_PARSE_FRAME_SPECIFIED) != 0;
    const bool inferred = (flags & BUSTER_PARSE_FRAME_AUTO) != 0 && parser->language.standard >= 2023;
    frame->flags |= typeless && !inferred && parser->language.standard >= 1999 ? BUSTER_PARSE_FRAME_TYPELESS : 0;
    frame->mode = PARSE_MODE_DECLARATOR;
    step->again = true;
}

// Whether token `i` may follow a struct's, union's or enum's body: what may follow a type specifier, as clang's
// isValidAfterTypeSpecifier has it. A qualifier or a storage class followed by a type specifier is taken for the next
// declaration's, after a missing `;`.
static bool parse_scalar_valid_after_tag(const ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t next = parser->kinds[i + 1];
    const bool next_type = next == PARSE_KIND_TYPE || next == PARSE_KIND_STRUCT || next == PARSE_KIND_ENUM || next == PARSE_KIND_TYPEOF || parse_scalar_is_typedef_name(parser, i + 1);
    bool result = false;
    switch (kind)
    {
        case PARSE_KIND_SEMI:
        case PARSE_KIND_STAR:
        case PARSE_KIND_AMPAMP:
        case PARSE_KIND_IDENTIFIER:
        case PARSE_KIND_R_PAREN:
        case PARSE_KIND_ELLIPSIS:
        case PARSE_KIND_COMMA:
        case PARSE_KIND_L_SQUARE:
        case PARSE_KIND_L_PAREN:
        case PARSE_KIND_ATTRIBUTE: result = true; break;
        case PARSE_KIND_COLON: result = frame->role == PARSE_ROLE_OPEN_STRUCT; break;
        case PARSE_KIND_QUALIFIER:
        case PARSE_KIND_STORAGE:
        case PARSE_KIND_TYPEDEF:
        case PARSE_KIND_ATOMIC: result = !next_type; break;
        default: break;
    }
    return result;
}

static void parse_scalar_specifiers(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t previous_role = i > 0 ? parser->roles[i - 1] : PARSE_ROLE_NONE;
    // The role before, past the attributes after a struct's body (`struct S { ... } __attribute__((packed))`), which are
    // the struct specifier's own.
    int64_t before = (int64_t)i - 1;
    while (before > 0 && parser->roles[before] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER)
    {
        uint32_t open = 0;
        do
        {
            open += parser->kinds[before] == PARSE_KIND_R_PAREN;
            open -= parser->kinds[before] == PARSE_KIND_L_PAREN;
            before -= 1;
        } while (before > 0 && open != 0);
        // Now on the attribute's keyword: the token before it.
        before -= 1;
    }
    const uint8_t tag_role = before >= 0 ? parser->roles[before] : PARSE_ROLE_NONE;
    const bool after_tag_body = tag_role == PARSE_ROLE_CLOSE_STRUCT || tag_role == PARSE_ROLE_CLOSE_ENUM;
    const bool new_line = (parser->flags[i] & BUSTER_PP_START_OF_LINE) != 0;
    if ((after_tag_body || (previous_role == PARSE_ROLE_TAG && new_line)) && !parse_scalar_valid_after_tag(parser, i, frame))
    {
        // A tag without a body, then what can't follow a type on the next line: clang takes it for a missing `;` too.
        // A struct's definition must be followed by what may follow a type: clang reads another type as a missing `;`.
        const uint32_t opener = parser->frames[parser->frame_count - 1].tag_token;
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TAG, BUSTER_PARSE_AFTER_PREVIOUS, opener);
    }
    else if (kind == PARSE_KIND_TYPE)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_EXTENSION && (frame->flags & (BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_TYPE_SEEN)) == 0)
    {
        // `__extension__` starts a declaration, before its specifiers.
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if ((kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF) && frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // A member has no storage class, and no function specifier.
        const IdentId name = parser->names[i];
        const bool function = name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2 || name == C_NAME_NORETURN;
        parse_scalar_error(parser, i, function ? PARSE_MESSAGE_MEMBER_FUNCTION_SPECIFIER : PARSE_MESSAGE_MEMBER_STORAGE_CLASS, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_CALLING_CONVENTION)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_SPECIFIED | (parser->names[i] == C_NAME_AUTO ? BUSTER_PARSE_FRAME_AUTO : 0);
    }
    else if (kind == PARSE_KIND_LABEL)
    {
        // GNU's `__label__ a, b;` declares labels, which have no type.
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_TYPEDEF)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->flags |= (kind != PARSE_KIND_ALIGNAS ? BUSTER_PARSE_FRAME_TYPE_SEEN : 0) | BUSTER_PARSE_FRAME_SPECIFIED;
        frame->mode = PARSE_MODE_SPECIFIER_PAREN;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_SPECIFIERS;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_SPECIFIERS, step);
    }
    else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
        frame->mode = PARSE_MODE_TAG;
        frame->tag_kind = kind;
        frame->tag_token = i;
    }
    else if (kind == PARSE_KIND_IDENTIFIER && parse_scalar_is_typedef_name(parser, i) && (frame->flags & BUSTER_PARSE_FRAME_TYPE_SEEN) == 0)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_SPECIFIED;
    }
    else if (kind == PARSE_KIND_SEMI && (frame->flags & BUSTER_PARSE_FRAME_NAME_REQUIRED))
    {
        // A declaration without declarators: `struct S;`, `struct S { ... };`, `int;`.
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && frame->role == PARSE_ROLE_OPEN_STRUCT)
    {
        // An unnamed bit-field.
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
        step->again = true;
    }
    else
    {
        parse_scalar_end_specifiers(parser, i, frame, step);
    }
}

static void parse_scalar_tag(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    // An enum's underlying type (C23's `enum E : int`), which a struct's bit-field width (`enum E : 3`) isn't.
    const bool base = kind == PARSE_KIND_COLON && frame->tag_kind == PARSE_KIND_ENUM && parse_scalar_starts_type(parser, i + 1);
    if (kind == PARSE_KIND_ATTRIBUTE)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT;
        frame->attribute_continuation = frame->mode;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT, frame->mode, step);
    }
    else if (kind == PARSE_KIND_IDENTIFIER && frame->mode == PARSE_MODE_TAG)
    {
        const uint8_t next = parser->kinds[i + 1];
        const bool body = next == PARSE_KIND_L_BRACE || (next == PARSE_KIND_COLON && frame->tag_kind == PARSE_KIND_ENUM && parse_scalar_starts_type(parser, i + 2));
        step->role = body ? PARSE_ROLE_LEAF_RIGHT : PARSE_ROLE_TAG;
        frame->mode = body ? PARSE_MODE_TAG_BODY : PARSE_MODE_SPECIFIERS;
    }
    else if (kind == PARSE_KIND_L_BRACE)
    {
        const bool is_enum = frame->tag_kind == PARSE_KIND_ENUM;
        ParseFrame* body = parse_scalar_open(parser, i, is_enum ? PARSE_ROLE_OPEN_ENUM : PARSE_ROLE_OPEN_STRUCT, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
        body->flags |= is_enum ? 0 : BUSTER_PARSE_FRAME_NO_BIND;
        body->tag_token = frame->tag_token;
    }
    else if (base)
    {
        step->role = PARSE_ROLE_SPECIFIER;
        frame->mode = PARSE_MODE_ENUM_BASE;
    }
    else if (frame->mode == PARSE_MODE_TAG)
    {
        parse_scalar_error_argument(parser, frame->tag_token, PARSE_MESSAGE_ANONYMOUS_TAG_DECLARATION, BUSTER_PARSE_AT_TOKEN, frame->tag_token);
    }
    else
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_BRACE, BUSTER_PARSE_AT_TOKEN);
    }
}

// After an enum's `:`: its underlying type's specifiers and qualifiers, then its body or the declaration's end.
static void parse_scalar_enum_base(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || parse_scalar_is_typedef_name(parser, i) || (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN))
    {
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (kind == PARSE_KIND_L_BRACE)
    {
        ParseFrame* body = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ENUM, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
        body->tag_token = frame->tag_token;
    }
    else
    {
        frame->mode = PARSE_MODE_SPECIFIERS;
        step->again = true;
    }
}

// The flags a declarator's group inherits from the frame its declarator is in.
#define BUSTER_PARSE_FRAME_DECLARATOR_STATE (BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_DECLARATION | BUSTER_PARSE_FRAME_ABSTRACT | \
                                             BUSTER_PARSE_FRAME_NAME_OPTIONAL | BUSTER_PARSE_FRAME_NAME_REQUIRED)

static void parse_scalar_declarator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const bool after_star = (frame->flags & BUSTER_PARSE_FRAME_AFTER_STAR) != 0;
    const bool abstract = (frame->flags & (BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_NAME_OPTIONAL)) != 0;
    if (kind == PARSE_KIND_STAR)
    {
        step->role = PARSE_ROLE_PREFIX;
        frame->flags |= BUSTER_PARSE_FRAME_AFTER_STAR;
    }
    else if ((kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_CALLING_CONVENTION || (kind == PARSE_KIND_ATOMIC && parser->kinds[i + 1] != PARSE_KIND_L_PAREN)) && after_star)
    {
        step->role = PARSE_ROLE_QUALIFIER_LEFT;
    }
    else if (kind == PARSE_KIND_CALLING_CONVENTION)
    {
        // `void (__cdecl *f)(void)`: a calling convention before the declarator in its group.
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (kind == PARSE_KIND_ATTRIBUTE)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = after_star ? PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT : PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = PARSE_MODE_DECLARATOR;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, after_star ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT : PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER, PARSE_MODE_DECLARATOR, step);
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        const uint8_t next = parser->kinds[i + 1];
        // Where the declarator may be abstract, `(` after the specifiers or a `*` is a function's parameters if they
        // start (a type, `)` or `...`), else a group; where it needs a name, it is a group, whose name comes inside.
        const bool function = abstract && (parse_scalar_starts_type(parser, i + 1) || next == PARSE_KIND_R_PAREN || next == PARSE_KIND_ELLIPSIS ||
                                           parse_scalar_standard_attribute(parser, i + 1));
        frame->flags &= ~BUSTER_PARSE_FRAME_AFTER_STAR;
        if (function)
        {
            parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FUNCTION, PARSE_MODE_ITEM, PARSE_MODE_AFTER_DECLARATOR, true, step);
            frame->flags |= BUSTER_PARSE_FRAME_FUNCTION;
        }
        else
        {
            const uint32_t inherited = frame->flags & BUSTER_PARSE_FRAME_DECLARATOR_STATE;
            ParseFrame* group = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_DECLARATOR, PARSE_MODE_AFTER_DECLARATOR, false, step);
            group->flags |= inherited;
        }
    }
    else if (kind == PARSE_KIND_IDENTIFIER && (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT))
    {
        // A type name has no declarator name: clang expects what ends the type name instead.
        uint32_t f = parser->frame_count - 1;
        while (f > 0 && parser->frames[f].role == PARSE_ROLE_OPEN_GROUP && (parser->frames[f].flags & BUSTER_PARSE_FRAME_ABSTRACT))
        {
            f -= 1;
        }
        parse_scalar_unexpected(parser, i, &parser->frames[f]);
    }
    else if (kind == PARSE_KIND_IDENTIFIER)
    {
        step->role = PARSE_ROLE_OPERAND;
        if (frame->flags & BUSTER_PARSE_FRAME_TYPELESS)
        {
            // clang reports the implicit int at the declarator's name.
            parse_scalar_note(parser, i, PARSE_MESSAGE_TYPE_SPECIFIER_MISSING, true);
            frame->flags &= ~BUSTER_PARSE_FRAME_TYPELESS;
        }
        if (parse_scalar_binds(parser))
        {
            parse_scalar_bind(parser, i, (frame->flags & BUSTER_PARSE_FRAME_TYPEDEF) ? PARSE_MEANING_TYPEDEF : PARSE_MEANING_ORDINARY);
        }
        frame->flags &= ~(BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_FUNCTION);
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
    }
    else if ((frame->flags & BUSTER_PARSE_FRAME_NAME_REQUIRED) && !(kind == PARSE_KIND_COLON && frame->role == PARSE_ROLE_OPEN_STRUCT))
    {
        parse_scalar_error(parser, i, frame->role == PARSE_ROLE_OPEN_STRUCT ? PARSE_MESSAGE_EXPECTED_MEMBER_NAME : PARSE_MESSAGE_EXPECTED_IDENTIFIER_OR_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else
    {
        frame->flags &= ~BUSTER_PARSE_FRAME_AFTER_STAR;
        frame->mode = PARSE_MODE_AFTER_DECLARATOR;
        step->again = true;
    }
}

static void parse_scalar_after_declarator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    const bool abstract = (frame->flags & BUSTER_PARSE_FRAME_ABSTRACT) != 0;
    const bool definition_scope = group == PARSE_ROLE_NONE || parse_frame_is_block(frame);
    if (kind == PARSE_KIND_L_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FUNCTION, PARSE_MODE_ITEM, PARSE_MODE_AFTER_DECLARATOR, true, step);
        frame->flags = (frame->flags | BUSTER_PARSE_FRAME_FUNCTION) & ~BUSTER_PARSE_FRAME_KR;
    }
    else if (kind == PARSE_KIND_L_SQUARE && !parse_scalar_standard_attribute(parser, i))
    {
        ParseFrame* array = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_AFTER_DECLARATOR, false, step);
        array->flags |= BUSTER_PARSE_FRAME_ARRAY_DECLARATOR | BUSTER_PARSE_FRAME_EMPTY_OK;
        frame->flags &= ~(BUSTER_PARSE_FRAME_FUNCTION | BUSTER_PARSE_FRAME_KR);
    }
    else if (kind == PARSE_KIND_ATTRIBUTE || (kind == PARSE_KIND_ASM && definition_scope))
    {
        // An attribute after the declarator, or an asm label, which renames the declared symbol.
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE;
        frame->attribute_continuation = PARSE_MODE_AFTER_DECLARATOR;
    }
    else if (parse_scalar_standard_attribute(parser, i))
    {
        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_MODE_AFTER_DECLARATOR, step);
    }
    else if (kind == PARSE_KIND_EQUAL && !abstract)
    {
        step->role = PARSE_ROLE_ASSIGN;
        frame->mode = PARSE_MODE_INITIALIZER;
    }
    else if (kind == PARSE_KIND_COMMA && parse_frame_comma_role(parser) == PARSE_ROLE_COMMA && (parser->flags[i + 1] & BUSTER_PP_START_OF_LINE) &&
             !parse_scalar_might_be_declarator(parser, i + 1))
    {
        // A comma, then a line break and what can't start a declarator: clang takes the comma for a `;`.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_COMMA && !(abstract && group != PARSE_ROLE_OPEN_KEYWORD))
    {
        step->role = parse_frame_comma_role(parser);
        frame->mode = step->role == PARSE_ROLE_COMMA ? PARSE_MODE_DECLARATOR : PARSE_MODE_ITEM;
        frame->flags &= ~(BUSTER_PARSE_FRAME_FUNCTION | BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_KR);
        if (step->role == PARSE_ROLE_SEPARATOR)
        {
            frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_AUTO);
            frame->argument += 1;
        }
        if (group == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC)
        {
            // A _Generic association's type is followed by its `:`.
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AFTER_PREVIOUS);
        }
    }
    else if (kind == PARSE_KIND_SEMI)
    {
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && group == PARSE_ROLE_OPEN_STRUCT)
    {
        step->role = PARSE_ROLE_BITFIELD;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags |= BUSTER_PARSE_FRAME_CONDITIONAL;
    }
    else if (kind == PARSE_KIND_COLON && group == PARSE_ROLE_OPEN_KEYWORD && frame->keyword == PARSE_KIND_GENERIC)
    {
        step->role = PARSE_ROLE_ASSOCIATION;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_DECLARATION);
        frame->flags |= parser->frames[parser->frame_count - 2].flags & BUSTER_PARSE_FRAME_NO_BIND;
    }
    else if (kind == PARSE_KIND_L_BRACE && definition_scope && (frame->flags & (BUSTER_PARSE_FRAME_FUNCTION | BUSTER_PARSE_FRAME_KR)))
    {
        if (group != PARSE_ROLE_NONE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_FUNCTION_DEFINITION_NOT_ALLOWED, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            ParseFrame* body = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BODY, PARSE_MODE_ITEM, PARSE_MODE_ITEM, false, step);
            body->scope = parser->body_scope;
            body->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
            parser->body_scope = BUSTER_PARSE_NONE;
            frame->flags &= ~BUSTER_PARSE_FRAME_ITEM_STATE;
            step->event = PARSE_EVENT_OPEN_BLOCK;
        }
    }
    else if ((frame->flags & BUSTER_PARSE_FRAME_KR) && group == PARSE_ROLE_NONE && parse_scalar_starts_declaration(parser, i, false))
    {
        // A K&R definition's parameter declarations, before its body.
        frame->mode = PARSE_MODE_KR_PARAMETERS;
        step->again = true;
    }
    else if (parse_kind_is_closer(kind))
    {
        parse_scalar_close_item(parser, i, frame, step);
    }
    else
    {
        parse_scalar_unexpected(parser, i, frame);
    }
}

// Between a K&R definition's `)` and its body: its parameters' declarations.
static void parse_scalar_kr_parameters(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_L_BRACE)
    {
        ParseFrame* body = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BODY, PARSE_MODE_ITEM, PARSE_MODE_ITEM, false, step);
        body->scope = parser->body_scope;
        body->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
        parser->body_scope = BUSTER_PARSE_NONE;
        frame->flags &= ~(BUSTER_PARSE_FRAME_ITEM_STATE | BUSTER_PARSE_FRAME_KR_DECLARATIONS);
        step->event = PARSE_EVENT_OPEN_BLOCK;
    }
    else if (parse_scalar_starts_declaration(parser, i, false))
    {
        parse_scalar_begin_declaration(frame, BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_KR_DECLARATIONS, step);
    }
    else
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_BRACE, BUSTER_PARSE_AT_TOKEN);
    }
}

// How many colons an asm statement may have: 3 (outputs, inputs, clobbers), or 4 with `goto` among its qualifiers
// (labels).
static uint32_t parse_scalar_asm_sections(const ParseScalar* parser, const ParseFrame* frame)
{
    uint32_t result = 3;
    for (int64_t t = (int64_t)frame->opener - 1; t >= 0 && parser->kinds[t] != PARSE_KIND_ASM; t -= 1)
    {
        result = parser->kinds[t] == PARSE_KIND_GOTO ? 4 : result;
    }
    return result;
}

// In an expression, where an operand comes next.
static void parse_scalar_operand(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint32_t flags = frame->flags;
    const bool attribute = (flags & BUSTER_PARSE_FRAME_ATTRIBUTE) != 0;
    const bool asm_operands = (flags & BUSTER_PARSE_FRAME_ASM) != 0;
    const bool empty = (flags & BUSTER_PARSE_FRAME_EMPTY_OK) != 0;
    const uint8_t previous = parser->kinds[(int64_t)i - 1];
    if (asm_operands && i == frame->opener + 1 && kind != PARSE_KIND_STRING)
    {
        // An asm statement's template comes first.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
    }
    else if (previous == PARSE_KIND_AMPAMP && parser->roles[(int64_t)i - 1] == PARSE_ROLE_PREFIX && kind != PARSE_KIND_IDENTIFIER)
    {
        // GNU's `&&label` takes a label's name.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
    }
    else if (asm_operands && frame->argument != 0 && !(kind == PARSE_KIND_STRING || kind == PARSE_KIND_COLON || kind == PARSE_KIND_R_PAREN ||
                                                     (kind == PARSE_KIND_L_SQUARE && frame->argument <= 2) || (kind == PARSE_KIND_IDENTIFIER && frame->argument == 4)))
    {
        // An operand starts with its symbolic name or its constraint, a clobber is a string, and asm goto's labels are names.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_ASM_STRING, BUSTER_PARSE_AT_TOKEN);
    }
    else if (attribute && frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind != PARSE_KIND_IDENTIFIER && kind < PARSE_KIND_TYPE && kind != PARSE_KIND_COMMA &&
             kind != PARSE_KIND_R_PAREN)
    {
        // An attribute list's items are names (or empty).
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (attribute && frame->role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind == PARSE_KIND_COMMA)
    {
        // An empty item.
        step->role = PARSE_ROLE_SEPARATOR;
    }
    else if (kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING || (attribute && kind >= PARSE_KIND_TYPE))
    {
        if (!attribute && parse_scalar_is_typedef_name(parser, i))
        {
            // clang: "unexpected type name: expected expression".
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            step->role = PARSE_ROLE_OPERAND;
            frame->mode = PARSE_MODE_OPERATOR;
            frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
            // GNU's `&&label`, the address of a label.
            frame->flags |= previous == PARSE_KIND_AMPAMP && parser->roles[(int64_t)i - 1] == PARSE_ROLE_PREFIX ? BUSTER_PARSE_FRAME_NO_POSTFIX : 0;
        }
    }
    else if (kind == PARSE_KIND_L_PAREN && !attribute && parse_scalar_starts_type(parser, i + 1))
    {
        ParseFrame* cast = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CAST, PARSE_MODE_SPECIFIERS, PARSE_MODE_OPERAND, false, step);
        cast->flags |= BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_DECLARATION;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_PAREN && parser->kinds[i + 1] == PARSE_KIND_L_BRACE && (flags & BUSTER_PARSE_FRAME_IN_FUNCTION) == 0)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_STATEMENT_EXPRESSION_AT_FILE_SCOPE, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_GROUP, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_BRACE && frame->role == PARSE_ROLE_OPEN_GROUP && previous == PARSE_KIND_L_PAREN && i == frame->opener + 1)
    {
        // A statement expression.
        ParseFrame* block = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_BLOCK, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, true, step);
        block->flags |= BUSTER_PARSE_FRAME_IN_FUNCTION;
        step->event = PARSE_EVENT_OPEN_BLOCK;
    }
    else if (kind == PARSE_KIND_MINUS || kind == PARSE_KIND_PLUS || kind == PARSE_KIND_EXCLAIM || kind == PARSE_KIND_TILDE || kind == PARSE_KIND_STAR ||
             kind == PARSE_KIND_AMP || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS || kind == PARSE_KIND_AMPAMP ||
             kind == PARSE_KIND_EXTENSION || kind == PARSE_KIND_REAL_IMAG)
    {
        step->role = PARSE_ROLE_PREFIX;
        // `[*]`, an array parameter's unspecified size: the `]` may come.
        const bool unspecified = kind == PARSE_KIND_STAR && (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR) && parser->kinds[i + 1] == PARSE_KIND_R_SQUARE;
        frame->flags = unspecified ? frame->flags | BUSTER_PARSE_FRAME_EMPTY_OK : frame->flags & ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_SIZEOF)
    {
        step->role = PARSE_ROLE_PREFIX;
        frame->mode = parser->kinds[i + 1] == PARSE_KIND_L_PAREN ? PARSE_MODE_SIZEOF_PAREN : PARSE_MODE_OPERAND;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_TYPE_ARGUMENT || kind == PARSE_KIND_GENERIC)
    {
        step->role = PARSE_ROLE_LEAF_RIGHT;
        frame->mode = PARSE_MODE_KEYWORD_PAREN;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_SQUARE && asm_operands)
    {
        // An asm operand's symbolic name, then its constraint.
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_ASM_CONSTRAINT, false, step);
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if ((kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_QUALIFIER) && (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR))
    {
        // `[static 10]` and `[const]` in an array parameter.
        step->role = PARSE_ROLE_SPECIFIER;
    }
    else if (parse_kind_is_closer(kind) && empty && frame->role != PARSE_ROLE_NONE && !parse_frame_is_block(frame) && frame->role != PARSE_ROLE_OPEN_STRUCT)
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_SEMI && empty)
    {
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (kind == PARSE_KIND_COLON && empty && frame->role == PARSE_ROLE_OPEN_TERNARY)
    {
        // GNU's `a ?: b`.
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_COLON && asm_operands && frame->argument >= parse_scalar_asm_sections(parser, frame))
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (kind == PARSE_KIND_COLON && asm_operands)
    {
        step->role = PARSE_ROLE_ASM_COLON;
        frame->argument += 1;
    }
    else if (previous == PARSE_KIND_SIZEOF && parse_scalar_starts_type(parser, i))
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_PARENTHESES_AROUND_TYPE_NAME, BUSTER_PARSE_AFTER_PREVIOUS, i - 1);
    }
    else
    {
        // clang puts a prefix `++` or `--` back when no operand follows it, and so expects the expression at the first of
        // them.
        uint32_t at = i;
        while (at > 0 && (parser->kinds[at - 1] == PARSE_KIND_PLUSPLUS || parser->kinds[at - 1] == PARSE_KIND_MINUSMINUS) && parser->roles[at - 1] == PARSE_ROLE_PREFIX)
        {
            at -= 1;
        }
        parse_scalar_error(parser, at, PARSE_MESSAGE_EXPECTED_EXPRESSION, BUSTER_PARSE_AT_TOKEN);
    }
}

BUSTER_INLINE uint8_t parse_binary_role(uint8_t kind)
{
    uint8_t result = PARSE_ROLE_NONE;
    switch (kind)
    {
        case PARSE_KIND_STAR:
        case PARSE_KIND_SLASH:
        case PARSE_KIND_PERCENT: result = PARSE_ROLE_MULTIPLICATIVE; break;
        case PARSE_KIND_PLUS:
        case PARSE_KIND_MINUS: result = PARSE_ROLE_ADDITIVE; break;
        case PARSE_KIND_LESSLESS:
        case PARSE_KIND_GREATERGREATER: result = PARSE_ROLE_SHIFT; break;
        case PARSE_KIND_LESS:
        case PARSE_KIND_GREATER:
        case PARSE_KIND_LESSEQUAL:
        case PARSE_KIND_GREATEREQUAL: result = PARSE_ROLE_RELATIONAL; break;
        case PARSE_KIND_EQUALEQUAL:
        case PARSE_KIND_EXCLAIMEQUAL: result = PARSE_ROLE_EQUALITY; break;
        case PARSE_KIND_AMP: result = PARSE_ROLE_BITWISE_AND; break;
        case PARSE_KIND_CARET: result = PARSE_ROLE_BITWISE_XOR; break;
        case PARSE_KIND_PIPE: result = PARSE_ROLE_BITWISE_OR; break;
        case PARSE_KIND_AMPAMP: result = PARSE_ROLE_LOGICAL_AND; break;
        case PARSE_KIND_PIPEPIPE: result = PARSE_ROLE_LOGICAL_OR; break;
        case PARSE_KIND_EQUAL:
        case PARSE_KIND_STAREQUAL:
        case PARSE_KIND_SLASHEQUAL:
        case PARSE_KIND_PERCENTEQUAL:
        case PARSE_KIND_PLUSEQUAL:
        case PARSE_KIND_MINUSEQUAL:
        case PARSE_KIND_LESSLESSEQUAL:
        case PARSE_KIND_GREATERGREATEREQUAL:
        case PARSE_KIND_AMPEQUAL:
        case PARSE_KIND_CARETEQUAL:
        case PARSE_KIND_PIPEEQUAL: result = PARSE_ROLE_ASSIGN; break;
        default: break;
    }
    return result;
}

// In an expression, after an operand.
static void parse_scalar_operator(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t binary = parse_binary_role(kind);
    const uint8_t group = frame->role;
    const uint32_t flags = frame->flags;
    const bool conditional = (flags & BUSTER_PARSE_FRAME_CONDITIONAL) != 0;
    const bool initializer_list = group == PARSE_ROLE_OPEN_INITIALIZER || group == PARSE_ROLE_OPEN_COMPOUND_BODY;
    const bool designator = frame->statement == PARSE_ITEM_DESIGNATOR && initializer_list;
    // A designator's subscript in an initializer list, where GNU's ranges `[1 ... 3]` may be.
    const bool designator_subscript = group == PARSE_ROLE_OPEN_SUBSCRIPT && parser->frame_count >= 2 &&
                                      parser->frames[parser->frame_count - 2].statement == PARSE_ITEM_DESIGNATOR;
    frame->flags &= ~BUSTER_PARSE_FRAME_NO_POSTFIX;
    if ((flags & BUSTER_PARSE_FRAME_NO_POSTFIX) && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_PERIOD || kind == PARSE_KIND_ARROW ||
                                                   kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS))
    {
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_POSTFIX_AFTER_UNARY, BUSTER_PARSE_AT_TOKEN, i);
    }
    else if ((flags & BUSTER_PARSE_FRAME_ASM) && (frame->argument == 1 || frame->argument == 2) && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING &&
             kind != PARSE_KIND_L_PAREN && kind != PARSE_KIND_STRING)
    {
        // An output's or input's constraint, which its operand in parentheses follows.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ASM_OPERAND, BUSTER_PARSE_AT_TOKEN);
    }
    else if ((kind == PARSE_KIND_COLON && (flags & BUSTER_PARSE_FRAME_ASM) && frame->argument >= parse_scalar_asm_sections(parser, frame)) ||
             (kind == PARSE_KIND_L_PAREN && (flags & BUSTER_PARSE_FRAME_ASM) && (frame->argument >= 3 || frame->argument == 0)))
    {
        // Too many sections, or a clobber (or a label) with an operand.
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
    }
    else if (group == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && kind != PARSE_KIND_L_PAREN && kind != PARSE_KIND_COMMA && !parse_kind_is_closer(kind))
    {
        // An attribute's name is followed by its arguments, the next attribute or the list's end.
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (designator && kind != PARSE_KIND_EQUAL && kind != PARSE_KIND_PERIOD && kind != PARSE_KIND_L_SQUARE)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_DESIGNATOR, BUSTER_PARSE_AT_TOKEN);
    }
    else if (binary == PARSE_ROLE_ASSIGN && conditional)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else if (binary != PARSE_ROLE_NONE)
    {
        step->role = binary;
        // A designator's `=` in an initializer list may be followed by a braced initializer.
        const bool initializer = binary == PARSE_ROLE_ASSIGN && initializer_list;
        frame->mode = initializer ? PARSE_MODE_INITIALIZER : PARSE_MODE_OPERAND;
        frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
        frame->statement = designator ? PARSE_ITEM_EXPRESSION : frame->statement;
    }
    else if (kind == PARSE_KIND_QUESTION)
    {
        ParseFrame* ternary = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_TERNARY, PARSE_MODE_OPERAND, PARSE_MODE_OPERAND, false, step);
        // GNU's `a ?: b`.
        ternary->flags |= BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_COLON && (group == PARSE_ROLE_OPEN_TERNARY || group == PARSE_ROLE_OPEN_CASE))
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_COLON && (flags & BUSTER_PARSE_FRAME_ASM))
    {
        step->role = PARSE_ROLE_ASM_COLON;
        frame->mode = PARSE_MODE_OPERAND;
        frame->flags |= BUSTER_PARSE_FRAME_EMPTY_OK;
        frame->argument += 1;
    }
    else if (kind == PARSE_KIND_L_PAREN)
    {
        // An asm operand's expression, after its constraint, may not be empty, as a call's arguments may.
        const bool operand = (flags & BUSTER_PARSE_FRAME_ASM) && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING;
        ParseFrame* call = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CALL, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
        call->flags |= operand ? 0 : BUSTER_PARSE_FRAME_EMPTY_OK;
    }
    else if (kind == PARSE_KIND_L_SQUARE)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SUBSCRIPT, PARSE_MODE_OPERAND, PARSE_MODE_OPERATOR, false, step);
    }
    else if (kind == PARSE_KIND_PERIOD || (kind == PARSE_KIND_ARROW && !designator))
    {
        step->role = PARSE_ROLE_MEMBER_OPERATOR;
        frame->mode = PARSE_MODE_MEMBER;
    }
    else if (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS)
    {
        step->role = PARSE_ROLE_POSTFIX;
    }
    else if (kind == PARSE_KIND_STRING && parser->kinds[(int64_t)i - 1] == PARSE_KIND_STRING)
    {
        step->role = PARSE_ROLE_STRING_NEXT;
    }
    else if (kind == PARSE_KIND_ELLIPSIS && (group == PARSE_ROLE_OPEN_CASE || designator_subscript))
    {
        step->role = PARSE_ROLE_RANGE;
        frame->mode = PARSE_MODE_OPERAND;
    }
    else if (kind == PARSE_KIND_COMMA)
    {
        step->role = parse_frame_comma_role(parser);
        const bool items = group == PARSE_ROLE_OPEN_ENUM || initializer_list || group == PARSE_ROLE_OPEN_FUNCTION || group == PARSE_ROLE_OPEN_KEYWORD;
        // Where the comma is an operator (not between arguments, items or declarators), clang ends the expression
        // before a comma that what follows can't continue: a brace, some statement keywords, a declaration specifier.
        const bool operator_comma = (flags & BUSTER_PARSE_FRAME_DECLARATION) == 0 && !items && group != PARSE_ROLE_OPEN_CALL && group != PARSE_ROLE_OPEN_ASM &&
                                    group != PARSE_ROLE_OPEN_ATTRIBUTE_LIST && group != PARSE_ROLE_OPEN_ATTRIBUTE && group != PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER &&
                                    group != PARSE_ROLE_OPEN_ATTRIBUTE_LEFT && group != PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
        // A declaration's comma, then a line break and what can't start a declarator: clang takes the comma for a `;`.
        const bool declaration_comma = (flags & BUSTER_PARSE_FRAME_DECLARATION) && step->role == PARSE_ROLE_COMMA && (parser->flags[i + 1] & BUSTER_PP_START_OF_LINE) &&
                                       !parse_scalar_might_be_declarator(parser, i + 1);
        if (operator_comma && parse_scalar_not_expression_start(parser, i + 1))
        {
            parse_scalar_unexpected(parser, i, frame);
        }
        else if (declaration_comma)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_SEMI_AT_END_OF_DECLARATION, BUSTER_PARSE_AT_TOKEN);
        }
        else if (group == PARSE_ROLE_OPEN_CASE || (conditional && group == PARSE_ROLE_OPEN_SPECIFIER) || (flags & BUSTER_PARSE_FRAME_ARRAY_DECLARATOR))
        {
            // No comma at the top of a case label, of `_Alignas(` and `_BitInt(`, or of an array declarator's size (an
            // assignment-expression).
            parse_scalar_unexpected(parser, i, frame);
        }
        else if (flags & BUSTER_PARSE_FRAME_DECLARATION)
        {
            // The end of an initializer: the next declarator, or the next parameter.
            frame->mode = step->role == PARSE_ROLE_COMMA ? PARSE_MODE_DECLARATOR : PARSE_MODE_ITEM;
            frame->flags &= ~(BUSTER_PARSE_FRAME_FUNCTION | BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_CONDITIONAL | BUSTER_PARSE_FRAME_KR);
            if (step->role == PARSE_ROLE_SEPARATOR)
            {
                frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_SPECIFIED | BUSTER_PARSE_FRAME_AUTO);
                frame->argument += 1;
            }
        }
        else
        {
            frame->mode = items ? PARSE_MODE_ITEM : PARSE_MODE_OPERAND;
            // A keyword group counts its arguments (a for-statement's frame counts its clauses, which commas don't end).
            frame->argument += group == PARSE_ROLE_OPEN_KEYWORD;
            if (items)
            {
                frame->flags &= ~(BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_CONDITIONAL |
                                  BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_SPECIFIED);
                frame->flags |= parser->frames[parser->frame_count - 2].flags & BUSTER_PARSE_FRAME_NO_BIND;
                frame->statement = PARSE_ITEM_EXPRESSION;
            }
            frame->flags &= ~BUSTER_PARSE_FRAME_EMPTY_OK;
        }
    }
    else if (kind == PARSE_KIND_SEMI)
    {
        parse_scalar_terminator(parser, i, frame, step);
    }
    else if (parse_kind_is_closer(kind))
    {
        parse_scalar_close_item(parser, i, frame, step);
    }
    else
    {
        parse_scalar_unexpected(parser, i, frame);
    }
}

// The token after a keyword that a group follows.
static void parse_scalar_paren(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t mode = frame->mode;
    const uint8_t kind = parser->kinds[i];
    if (mode == PARSE_MODE_ASM_PAREN && (kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_GOTO))
    {
        // asm volatile, asm inline, asm goto.
        step->role = PARSE_ROLE_LEAF_RIGHT;
    }
    else if (kind != PARSE_KIND_L_PAREN && mode == PARSE_MODE_ATTRIBUTE_PAREN && parser->kinds[(int64_t)i - 1] == PARSE_KIND_ATTRIBUTE)
    {
        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ATTRIBUTE, BUSTER_PARSE_AFTER_PREVIOUS);
    }
    else if (kind != PARSE_KIND_L_PAREN)
    {
        // The keyword clang names: the one before, or for asm the one before its qualifiers.
        uint32_t keyword = i - 1;
        while (mode == PARSE_MODE_ASM_PAREN && keyword > 0 && parser->kinds[keyword] != PARSE_KIND_ASM)
        {
            keyword -= 1;
        }
        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER, BUSTER_PARSE_AT_TOKEN, keyword);
    }
    else if (mode == PARSE_MODE_CONDITION_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_CONDITION, PARSE_MODE_OPERAND, PARSE_MODE_ITEM, false, step);
    }
    else if (mode == PARSE_MODE_FOR_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_FOR, PARSE_MODE_ITEM, PARSE_MODE_ITEM, true, step);
    }
    else if (mode == PARSE_MODE_KEYWORD_PAREN)
    {
        // _Static_assert where a declaration is ends with its `;`.
        const bool declaration = parser->kinds[(int64_t)i - 1] == PARSE_KIND_STATIC_ASSERT && (frame->role == PARSE_ROLE_NONE || parse_frame_is_block(frame) ||
                                                                                     frame->role == PARSE_ROLE_OPEN_STRUCT);
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_KEYWORD, PARSE_MODE_ITEM, declaration ? PARSE_MODE_END_STATEMENT : PARSE_MODE_OPERATOR, false, step);
        frame->statement = declaration ? PARSE_ITEM_STATIC_ASSERT : frame->statement;
    }
    else if (mode == PARSE_MODE_SIZEOF_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SIZEOF, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, step);
    }
    else if (mode == PARSE_MODE_SPECIFIER_PAREN)
    {
        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_SPECIFIER, PARSE_MODE_ITEM, PARSE_MODE_SPECIFIERS, false, step);
    }
    else if (mode == PARSE_MODE_ASM_PAREN)
    {
        const bool statement = frame->statement == PARSE_ITEM_ASM || frame->statement == PARSE_ITEM_TOP_LEVEL_ASM;
        ParseFrame* group = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ASM, PARSE_MODE_OPERAND, statement ? PARSE_MODE_END_STATEMENT : PARSE_MODE_OPERATOR, false, step);
        group->flags |= BUSTER_PARSE_FRAME_ASM;
    }
    else
    {
        // An attribute: `__attribute__((list))`, or `__declspec(list)` and an asm label's `("name")`.
        const uint8_t closer_role = frame->attribute_closer_role;
        const uint8_t continuation = frame->attribute_continuation;
        // `__attribute__` takes a list in two parentheses (ATTRIBUTE_LIST rejects one); an asm label takes its string in one.
        const bool list = parser->kinds[(int64_t)i - 1] == PARSE_KIND_ATTRIBUTE;
        const uint8_t opener_role = closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER :
                                    closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT ? PARSE_ROLE_OPEN_ATTRIBUTE_LEFT :
                                    closer_role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ? PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT : PARSE_ROLE_OPEN_ATTRIBUTE;
        ParseFrame* group = parse_scalar_open(parser, i, opener_role, list ? PARSE_MODE_ATTRIBUTE_LIST : PARSE_MODE_OPERAND, continuation, false, step);
        group->flags |= list ? 0 : BUSTER_PARSE_FRAME_ATTRIBUTE;
    }
}

// An attribute list's tokens, as a balanced run: C23's `[[gnu::unused, deprecated("why")]]`.
static void parse_scalar_attribute_tokens(ParseScalar* parser, uint32_t i, ParseFrame* frame, ParseStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
    {
        const uint8_t role = kind == PARSE_KIND_L_PAREN ? PARSE_ROLE_OPEN_GROUP : kind == PARSE_KIND_L_SQUARE ? PARSE_ROLE_OPEN_SUBSCRIPT : PARSE_ROLE_OPEN_INITIALIZER;
        ParseFrame* group = parse_scalar_open(parser, i, role, PARSE_MODE_ATTRIBUTE_TOKENS, PARSE_MODE_ATTRIBUTE_TOKENS, false, step);
        group->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
        // Each group's closer is an operand, so that its contents and the tokens around it stay a flat run.
        group->closer_role = kind == PARSE_KIND_L_PAREN ? PARSE_ROLE_CLOSE_GROUP : kind == PARSE_KIND_L_SQUARE ? PARSE_ROLE_CLOSE_ATTRIBUTE_LIST : PARSE_ROLE_CLOSE_INITIALIZER;
    }
    else if (parse_kind_is_closer(kind))
    {
        parse_scalar_close(parser, i, step);
    }
    else if (kind == PARSE_KIND_END)
    {
        parse_scalar_unexpected(parser, i, frame);
    }
    else
    {
        step->role = kind == PARSE_KIND_COMMA ? PARSE_ROLE_SEPARATOR : PARSE_ROLE_OPERAND;
    }
}

// What a frame whose statement must end with `;` reports when something else comes, after the token before it.
static void parse_scalar_missing_semi(ParseScalar* parser, uint32_t i, const ParseFrame* frame)
{
    uint8_t message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_EXPRESSION;
    switch (frame->statement)
    {
        case PARSE_ITEM_GOTO: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_GOTO; break;
        case PARSE_ITEM_BREAK: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_BREAK; break;
        case PARSE_ITEM_CONTINUE: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_CONTINUE; break;
        case PARSE_ITEM_ASM: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_ASM; break;
        case PARSE_ITEM_TOP_LEVEL_ASM: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_TOP_LEVEL_ASM; break;
        case PARSE_ITEM_STATIC_ASSERT: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_STATIC_ASSERT; break;
        case PARSE_ITEM_DO_WHILE: message = PARSE_MESSAGE_EXPECTED_SEMI_AFTER_DO_WHILE; break;
        default: break;
    }
    parse_scalar_error(parser, i, message, BUSTER_PARSE_AFTER_PREVIOUS);
}

static void parse_scalar_token(ParseScalar* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    ParseStep step = {
        .depth = parser->frame_count - 1,
        .scope = BUSTER_PARSE_NONE,
        .role = PARSE_ROLE_ERROR,
        .event = PARSE_EVENT_NONE,
        .keyword = 0,
        .again = true,
    };
    while (step.again && parser->error_token == BUSTER_PARSE_NONE)
    {
        step.again = false;
        step.depth = parser->frame_count - 1;
        ParseFrame* frame = &parser->frames[parser->frame_count - 1];
        if (kind == PARSE_KIND_END && frame->mode == PARSE_MODE_ITEM && frame->opener != BUSTER_PARSE_NONE && parser->kinds[frame->opener] == PARSE_KIND_L_BRACE)
        {
            parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_BRACE, BUSTER_PARSE_AT_TOKEN);
        }
        else
        {
            switch (frame->mode)
            {
                case PARSE_MODE_ITEM: parse_scalar_item(parser, i, frame, &step); break;
                case PARSE_MODE_SPECIFIERS: parse_scalar_specifiers(parser, i, frame, &step); break;
                case PARSE_MODE_TAG:
                case PARSE_MODE_TAG_BODY: parse_scalar_tag(parser, i, frame, &step); break;
                case PARSE_MODE_ENUM_BASE: parse_scalar_enum_base(parser, i, frame, &step); break;
                case PARSE_MODE_DECLARATOR: parse_scalar_declarator(parser, i, frame, &step); break;
                case PARSE_MODE_AFTER_DECLARATOR: parse_scalar_after_declarator(parser, i, frame, &step); break;
                case PARSE_MODE_KR_PARAMETERS: parse_scalar_kr_parameters(parser, i, frame, &step); break;
                case PARSE_MODE_INITIALIZER:
                    if (kind == PARSE_KIND_L_BRACE)
                    {
                        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_INITIALIZER, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, &step);
                    }
                    else
                    {
                        parse_scalar_begin_expression(frame, false, false, &step);
                    }
                    break;
                case PARSE_MODE_OPERAND: parse_scalar_operand(parser, i, frame, &step); break;
                case PARSE_MODE_OPERATOR: parse_scalar_operator(parser, i, frame, &step); break;
                case PARSE_MODE_MEMBER:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_MEMBER;
                        frame->mode = PARSE_MODE_OPERATOR;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_LABEL:
                    step.role = PARSE_ROLE_LABEL;
                    step.event = PARSE_EVENT_HEAD;
                    step.keyword = PARSE_KIND_COLON;
                    frame->mode = PARSE_MODE_ITEM;
                    break;
                case PARSE_MODE_DEFAULT:
                    if (kind == PARSE_KIND_COLON)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON_AFTER_DEFAULT, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_ASSOCIATION:
                    if (kind == PARSE_KIND_COLON)
                    {
                        step.role = PARSE_ROLE_ASSOCIATION;
                        frame->mode = PARSE_MODE_OPERAND;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_COLON, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_ENUMERATOR:
                    if (kind == PARSE_KIND_EQUAL)
                    {
                        step.role = PARSE_ROLE_ASSIGN;
                        parse_scalar_begin_expression(frame, false, true, &step);
                        step.again = false;
                    }
                    else if (kind == PARSE_KIND_COMMA)
                    {
                        step.role = PARSE_ROLE_SEPARATOR;
                        frame->mode = PARSE_MODE_ITEM;
                    }
                    else if (kind == PARSE_KIND_R_BRACE)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else if (kind == PARSE_KIND_ATTRIBUTE)
                    {
                        step.role = PARSE_ROLE_LEAF_RIGHT;
                        frame->mode = PARSE_MODE_ATTRIBUTE_PAREN;
                        frame->attribute_closer_role = PARSE_ROLE_CLOSE_ATTRIBUTE;
                        frame->attribute_continuation = PARSE_MODE_ENUMERATOR;
                    }
                    else if (parse_scalar_standard_attribute(parser, i))
                    {
                        parse_scalar_standard_attribute_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE, PARSE_MODE_ENUMERATOR, &step);
                    }
                    else if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_MISSING_COMMA_BETWEEN_ENUMERATORS, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_BRACE_OR_COMMA, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_END_STATEMENT:
                    if (kind == PARSE_KIND_SEMI)
                    {
                        parse_scalar_terminator(parser, i, frame, &step);
                    }
                    else
                    {
                        // The `)` of a do-statement's while has its own message.
                        const ParseStatement* top = parse_statement_top(parser);
                        if (top != 0 && top->state == PARSE_STATEMENT_DO_WHILE && parse_frame_is_block(frame) && frame->statement == PARSE_ITEM_EXPRESSION)
                        {
                            frame->statement = PARSE_ITEM_DO_WHILE;
                        }
                        parse_scalar_missing_semi(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_GOTO:
                    if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_END_STATEMENT;
                    }
                    else if (kind == PARSE_KIND_STAR)
                    {
                        // GNU's computed goto.
                        step.role = PARSE_ROLE_PREFIX;
                        frame->mode = PARSE_MODE_OPERAND;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_IDENTIFIER, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_JUMP:
                    if (kind == PARSE_KIND_SEMI)
                    {
                        parse_scalar_terminator(parser, i, frame, &step);
                    }
                    else if (kind == PARSE_KIND_IDENTIFIER)
                    {
                        // C2y's `break label;`, which clang rejects in C23 and before.
                        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_NAMED_JUMP, BUSTER_PARSE_AT_TOKEN, i - 1);
                    }
                    else
                    {
                        parse_scalar_missing_semi(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_LIST:
                    if (kind == PARSE_KIND_L_PAREN)
                    {
                        ParseFrame* list = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_MODE_OPERAND, PARSE_MODE_ATTRIBUTE_END, false, &step);
                        list->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE | BUSTER_PARSE_FRAME_EMPTY_OK;
                    }
                    else
                    {
                        parse_scalar_error_argument(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER, BUSTER_PARSE_AT_TOKEN, i - 1);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_END:
                    if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AFTER_PREVIOUS);
                    }
                    break;
                case PARSE_MODE_STANDARD_ATTRIBUTE_LIST:
                {
                    ParseFrame* list = parse_scalar_open(parser, i, PARSE_ROLE_OPEN_ATTRIBUTE_LIST, PARSE_MODE_ATTRIBUTE_TOKENS, PARSE_MODE_STANDARD_ATTRIBUTE_END, false, &step);
                    list->flags |= BUSTER_PARSE_FRAME_ATTRIBUTE;
                    break;
                }
                case PARSE_MODE_STANDARD_ATTRIBUTE_END:
                    if (kind == PARSE_KIND_R_SQUARE)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_SQUARE, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_ATTRIBUTE_TOKENS: parse_scalar_attribute_tokens(parser, i, frame, &step); break;
                case PARSE_MODE_ASM_CONSTRAINT:
                    if (kind == PARSE_KIND_STRING)
                    {
                        step.role = PARSE_ROLE_OPERAND;
                        frame->mode = PARSE_MODE_OPERATOR;
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_PAREN_AFTER_ASM_OPERAND, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_COMPOUND_BODY:
                    if (kind == PARSE_KIND_L_BRACE)
                    {
                        parse_scalar_open(parser, i, PARSE_ROLE_OPEN_COMPOUND_BODY, PARSE_MODE_ITEM, PARSE_MODE_OPERATOR, false, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_L_BRACE, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                case PARSE_MODE_CLOSE:
                    if (parse_kind_is_closer(kind))
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_unexpected(parser, i, frame);
                    }
                    break;
                case PARSE_MODE_IDENTIFIER_LIST:
                    if (kind == PARSE_KIND_COMMA)
                    {
                        step.role = PARSE_ROLE_SEPARATOR;
                        frame->mode = PARSE_MODE_ITEM;
                        frame->argument += 1;
                    }
                    else if (kind == PARSE_KIND_R_PAREN)
                    {
                        parse_scalar_close(parser, i, &step);
                    }
                    else
                    {
                        parse_scalar_error(parser, i, PARSE_MESSAGE_EXPECTED_R_PAREN, BUSTER_PARSE_AT_TOKEN);
                    }
                    break;
                default: parse_scalar_paren(parser, i, frame, &step); break;
            }
        }
    }
    if (parser->error_token == BUSTER_PARSE_NONE && i < parser->count)
    {
        parse_scalar_emit(parser, i, step.role, step.depth);
        switch (step.event)
        {
            case PARSE_EVENT_OPEN_BLOCK: parse_statement_open_block(parser, false); break;
            case PARSE_EVENT_OPEN_STATEMENT_BLOCK: parse_statement_open_block(parser, true); break;
            case PARSE_EVENT_CLOSE_BLOCK: parse_statement_close_block(parser, i); break;
            case PARSE_EVENT_TERMINATOR: parse_statement_terminator(parser, i); break;
            case PARSE_EVENT_HEAD:
                // Only heads in blocks: a case's `:` closes its group into the block it is in.
                if (parse_frame_is_block(&parser->frames[parser->frame_count - 1]))
                {
                    parse_statement_head(parser, i, step.keyword, step.scope);
                }
                break;
            case PARSE_EVENT_ELSE: parse_statement_else(parser, i); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's entry point and recovery

// The last token of the top-level declaration an error stopped at `token`: its `;` at file scope, or for an error in a
// function body the `}` that closes the body, with the brackets the frames hold open counted, and a closer that closes
// nothing open skipped (or the last token, if the input ends first). `stack` has room for a bracket per token and frame.
static uint32_t parse_scalar_skip(const ParseScalar* parser, uint32_t token, uint8_t* stack)
{
    uint32_t depth = 0;
    for (uint32_t f = 1; f < parser->frame_count; f += 1)
    {
        const uint8_t opener = parser->kinds[parser->frames[f].opener];
        if (opener == PARSE_KIND_L_PAREN || opener == PARSE_KIND_L_SQUARE || opener == PARSE_KIND_L_BRACE)
        {
            stack[depth] = opener;
            depth += 1;
        }
    }
    const bool body = parser->frame_count > 1 && parser->frames[1].role == PARSE_ROLE_OPEN_BODY;
    uint32_t result = parser->count != 0 ? parser->count - 1 : 0;
    bool done = false;
    for (uint32_t i = token; !done && i < parser->count; i += 1)
    {
        const uint8_t kind = parser->kinds[i];
        const uint8_t opener = kind == PARSE_KIND_R_PAREN ? PARSE_KIND_L_PAREN : kind == PARSE_KIND_R_SQUARE ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_BRACE;
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            stack[depth] = kind;
            depth += 1;
        }
        else if (parse_kind_is_closer(kind) && depth != 0 && stack[depth - 1] == opener)
        {
            depth -= 1;
            done = depth == 0 && body && kind == PARSE_KIND_R_BRACE;
        }
        else if (kind == PARSE_KIND_R_BRACE && depth == 0)
        {
            // A `}` with nothing open: an extraneous one, which the declaration takes.
            done = true;
        }
        else
        {
            done = kind == PARSE_KIND_SEMI && depth == 0;
        }
        result = done ? i : result;
    }
    return result;
}

// Back to file scope, between top-level declarations.
static void parse_scalar_reset(ParseScalar* parser)
{
    parser->frame_count = 1;
    parser->frames[0] = (ParseFrame){
        .opener = BUSTER_PARSE_NONE,
        .scope = 0,
        .flags = 0,
        .role = PARSE_ROLE_NONE,
        .mode = PARSE_MODE_ITEM,
        .closer_role = PARSE_ROLE_NONE,
        .keyword = 0,
        .continuation = PARSE_MODE_ITEM,
        .attribute_closer_role = PARSE_ROLE_NONE,
        .attribute_continuation = PARSE_MODE_ITEM,
        .tag_kind = 0,
        .statement = PARSE_ITEM_EXPRESSION,
        .argument = 0,
        .tag_token = BUSTER_PARSE_NONE,
    };
    parser->statement_count = 0;
    // Only top-level declarations' last tokens wait across them, and with no R their parent is the root anyway.
    parser->candidate_count = 1;
    parser->waiting_count = 1;
    parser->body_scope = BUSTER_PARSE_NONE;
    parser->error_token = BUSTER_PARSE_NONE;
}

// The first error of the current top-level declaration, stopped at token `stopped`: its diagnostic, then the rest of
// the declaration skipped. Its tokens become ERROR leaves of the root, the parse goes on at file scope, and the names
// it bound are unbound. Returns the declaration's last token.
static uint32_t parse_scalar_recover(ParseScalar* parser, uint32_t stopped)
{
    if (parser->diagnostic_count < parser->diagnostic_capacity)
    {
        parser->diagnostics[parser->diagnostic_count] = (ParseDiagnostic){
            .token = parser->error_token,
            .argument = parser->error_argument,
            .message = parser->error_message,
            .place = parser->error_place,
            .error = true,
        };
        parser->diagnostic_count += 1;
    }
    parser->errors += 1;
    const uint32_t end = parse_scalar_skip(parser, stopped, parser->brackets);
    for (uint32_t t = parser->item_begin; t <= end && t < parser->count; t += 1)
    {
        parser->roles[t] = PARSE_ROLE_ERROR;
        parser->depths[t] = 0;
        parser->parents[t] = BUSTER_PARSE_ROOT;
    }
    parse_scalar_restore(parser, parser->item_log);
    parse_scalar_reset(parser);
    return end;
}

typedef struct ParseScalarResult ParseScalarResult;
struct ParseScalarResult
{
    // The errors found: syntax errors (one per top-level declaration at most), and the errors clang reports without
    // changing how it parses (a missing type specifier).
    uint32_t errors;
    // The scratch arena couldn't hold the parser's stacks: nothing was parsed.
    bool out_of_memory;
};

// Parses tokens [0, count) into `tree`, whose arrays have `count` entries, with the scratch arena's memory for its
// stacks. `kinds` and `names` have the padding of ParseTokens around them. The diagnostics go to `diagnostics`, which has
// room for `capacity`, and their number to `*diagnostic_count`.
static ParseScalarResult parse_scalar(const uint8_t* kinds, const uint32_t* names, const uint8_t* flags, uint32_t count, CLanguage language, ParseMeanings* meanings,
                                      ParseTree* tree, Arena* scratch, ParseDiagnostic* diagnostics, uint32_t capacity, uint32_t* diagnostic_count)
{
    ParseScalarResult result = { .errors = 0, .out_of_memory = false };
    const uint64_t mark = scratch->length;
    const uint64_t slots = (uint64_t)count + 2;
    ParseScalar parser = {
        .kinds = kinds,
        .names = names,
        .flags = flags,
        .count = count,
        .language = language,
        .roles = tree->roles,
        .depths = tree->depths,
        .parents = tree->parents,
        .frames = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseFrame, slots),
        .frame_count = 1,
        .candidates = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseCandidate, slots + 1),
        .candidate_count = 1,
        .waiting = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseWaiting, slots + 1),
        .waiting_count = 1,
        .meanings = meanings->meanings,
        .log_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .log_meanings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .log_count = 0,
        .body_scope = BUSTER_PARSE_NONE,
        .statements = BUSTER_ARENA_PUSH_ARRAY(scratch, ParseStatement, slots),
        .statement_count = 0,
        .brackets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, 2 * slots),
        .item_begin = 0,
        .item_log = 0,
        .error_token = BUSTER_PARSE_NONE,
        .error_argument = BUSTER_PARSE_NONE,
        .error_message = PARSE_MESSAGE_NONE,
        .error_place = BUSTER_PARSE_AT_TOKEN,
        .diagnostics = diagnostics,
        .diagnostic_count = 0,
        .diagnostic_capacity = capacity,
        .errors = 0,
    };
    if (parser.frames == 0 || parser.candidates == 0 || parser.waiting == 0 || parser.log_names == 0 || parser.log_meanings == 0 || parser.statements == 0 ||
        parser.brackets == 0)
    {
        result.out_of_memory = true;
    }
    else
    {
        // The stacks' sentinels.
        parser.candidates[0] = (ParseCandidate){ .position = BUSTER_PARSE_NONE, .key = 0 };
        parser.waiting[0] = (ParseWaiting){ .position = BUSTER_PARSE_NONE, .threshold = 0, .left = BUSTER_PARSE_NONE, .left_key = 0 };
        parse_scalar_reset(&parser);
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (parser.frame_count == 1 && parser.frames[0].mode == PARSE_MODE_ITEM)
            {
                parser.item_begin = i;
                parser.item_log = parser.log_count;
            }
            parse_scalar_token(&parser, i);
            if (parser.error_token != BUSTER_PARSE_NONE)
            {
                i = parse_scalar_recover(&parser, i);
            }
        }
        // The end of the input, as a token of its own: whatever is still open is an error there.
        if (parser.frame_count != 1 || parser.frames[0].mode != PARSE_MODE_ITEM)
        {
            parse_scalar_token(&parser, count);
            if (parser.error_token == BUSTER_PARSE_NONE)
            {
                parse_scalar_error(&parser, count, PARSE_MESSAGE_UNEXPECTED_TOKEN, BUSTER_PARSE_AT_TOKEN);
            }
            parse_scalar_recover(&parser, count);
        }
        parse_scalar_finish(&parser);
        parse_scalar_restore(&parser, 0);
        result.errors = parser.errors;
        *diagnostic_count = parser.diagnostic_count;
    }
    arena_pop_to(scratch, mark);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The entry point

typedef struct ParseOptions ParseOptions;
struct ParseOptions
{
    // The scalar parser alone, for test-parser's differential checks.
    bool scalar;
};

// What a parse did, for benchmark-parser and test-parser.
typedef struct ParseStatistics ParseStatistics;
struct ParseStatistics
{
    // Top-level declarations the scalar parser took on the vector path, and their tokens.
    uint64_t fallback_items;
    uint64_t fallback_tokens;
    // Tokens the vector path flagged.
    uint64_t flagged_tokens;
    // The whole unit went to the scalar parser: unbalanced brackets, a typedef the pass can't read, a do without its
    // while, or an error the fallback found (which the scalar parser then reports as it would alone).
    bool whole;
};

typedef struct Parser Parser;
struct Parser
{
    ParseOptions options;
    CLanguage language;
    ParseTokens tokens;
    ParseTree tree;
    ParseMeanings meanings;
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_count;
    uint32_t diagnostic_capacity;
    uint32_t errors;
    ParseStatistics statistics;
    // The token columns and the tree's, each in an arena of its own, so that their first pages are base pages
    // (lane.c), and the scalar parser's stacks.
    Arena columns[11];
    Arena scratch;
};

static bool parse_tables_ready;

// The tables every parse reads, built once.
static void parse_tables_initialize(void)
{
    if (!parse_tables_ready)
    {
        parse_kind_table_initialize();
        parse_role_info_initialize();
        parse_tables_ready = true;
    }
}

static bool parse_initialize(Parser* parser, const ParseOptions* options, CLanguage language)
{
    parse_tables_initialize();
    *parser = (Parser){
        .options = *options,
        .language = language,
    };
    parser->scratch = arena_reserve(1ull << 36);
    return parser->scratch.capacity != 0;
}

static void parse_release(Parser* parser)
{
    for (uint32_t i = 0; i < sizeof(parser->columns) / sizeof(parser->columns[0]); i += 1)
    {
        arena_release(&parser->columns[i]);
    }
    arena_release(&parser->scratch);
}

// A padded column of `count` entries of `size` bytes in arena `column`, from its token 0 (the padding is before it).
static void* parse_column(Parser* parser, uint32_t column, uint64_t count, uint64_t size)
{
    const uint64_t slots = count + 2 * BUSTER_PARSE_TOKEN_PADDING;
    arena_release(&parser->columns[column]);
    parser->columns[column] = arena_reserve(slots * size + 64);
    uint8_t* bytes = arena_push_array(&parser->columns[column], slots, size, 64);
    return bytes != 0 ? bytes + BUSTER_PARSE_TOKEN_PADDING * size : 0;
}

// The meaning of every name the unit may use, ordinary but for clang's predeclared typedef names.
static bool parse_prepare_meanings(Parser* parser, InternTable* table)
{
    bool result = true;
    IdentId predeclared[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
    for (uint32_t i = 0; result && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        predeclared[i] = intern_find_or_add(table, (const uint8_t*)parse_predeclared_typedefs[i], (uint32_t)strlen(parse_predeclared_typedefs[i]));
        result = predeclared[i] != 0;
    }
    const uint32_t capacity = table->count;
    parser->meanings.meanings = result ? parse_column(parser, 9, capacity, 1) : 0;
    parser->meanings.capacity = capacity;
    result = result && parser->meanings.meanings != 0;
    if (result)
    {
        memset(parser->meanings.meanings, PARSE_MEANING_ORDINARY, capacity);
        for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            parser->meanings.meanings[predeclared[i]] = PARSE_MEANING_TYPEDEF;
        }
    }
    return result;
}

// Parses the unit the preprocessor `pp` produced (pp_run's output). Returns false if it has errors or the parser ran out
// of memory; the diagnostics are in parser->diagnostics (parse_print_diagnostics prints them).
static bool parse_translation_unit(Parser* parser, Preprocessor* pp)
{
    const PPTokens* output = &pp->output;
    bool result = output->count <= BUSTER_PARSE_MAX_TOKENS / 2;
    const uint64_t capacity = 2 * (uint64_t)output->count + 64;
    parser->diagnostic_count = 0;
    parser->errors = 0;
    parser->statistics = (ParseStatistics){ 0 };
    if (result)
    {
        ParseTokens* tokens = &parser->tokens;
        tokens->kinds = parse_column(parser, 0, capacity, 1);
        tokens->names = parse_column(parser, 1, capacity, 4);
        tokens->sources = parse_column(parser, 2, capacity, 4);
        tokens->pragma_sources = parse_column(parser, 3, (uint64_t)output->count + 1, 4);
        tokens->pragma_positions = parse_column(parser, 4, (uint64_t)output->count + 1, 4);
        tokens->flags = parse_column(parser, 10, capacity, 1);
        tokens->capacity = (uint32_t)capacity;
        result = tokens->kinds != 0 && tokens->names != 0 && tokens->sources != 0 && tokens->pragma_sources != 0 && tokens->pragma_positions != 0 &&
                 tokens->flags != 0;
    }
    if (result)
    {
        if (parser->options.scalar)
        {
            parse_tokens_from_output_scalar(output, &parser->tokens);
        }
        else
        {
            parse_tokens_from_output(output, &parser->tokens);
        }
        const uint32_t count = parser->tokens.count;
        parser->tree.roles = parse_column(parser, 5, count, 1);
        parser->tree.parents = parse_column(parser, 6, count, 4);
        parser->tree.depths = parse_column(parser, 7, count, 4);
        parser->diagnostic_capacity = count + 16;
        parser->diagnostics = parse_column(parser, 8, parser->diagnostic_capacity, sizeof(ParseDiagnostic));
        result = parser->tree.roles != 0 && parser->tree.parents != 0 && parser->tree.depths != 0 && parser->diagnostics != 0 && parse_prepare_meanings(parser, pp->table);
    }
    if (result)
    {
        const ParseScalarResult parsed = parse_scalar(parser->tokens.kinds, parser->tokens.names, parser->tokens.flags, parser->tokens.count, parser->language, &parser->meanings,
                                                      &parser->tree, &parser->scratch, parser->diagnostics, parser->diagnostic_capacity, &parser->diagnostic_count);
        parser->errors = parsed.errors;
        result = !parsed.out_of_memory && parsed.errors == 0;
        if (parsed.out_of_memory)
        {
            parser->errors += 1;
        }
    }
    return result;
}

// The location of the end of the main file, where clang puts what is missing at the end: its last newline, as clang's
// end-of-file token sits on it.
static uint32_t parse_end_location(const Preprocessor* pp)
{
    uint32_t result = 0;
    for (uint32_t r = 0; r < pp->range_count && result == 0; r += 1)
    {
        const PPRange* range = &pp->ranges[r];
        if (range->file == pp->main_file && range->parent == BUSTER_PP_NONE)
        {
            const uint32_t length = range->length - 1;
            result = range->base + (length != 0 && range->bytes[length - 1] == '\n' ? length - 1 : length);
        }
    }
    return result;
}

// The location just after parser token `t`'s spelling.
static uint32_t parse_token_end(const Parser* parser, const Preprocessor* pp, uint32_t t)
{
    const uint32_t s = parser->tokens.sources[t];
    const uint8_t kind = pp->output.kinds[s];
    const uint32_t location = pp->output.locations[s];
    uint32_t result = pp->output.values[s];
    if (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END))
    {
        result = location + intern_length(pp->table, pp->output.values[s]);
    }
    else if (kind == C_TOKEN_COLONCOLON)
    {
        // The first half of a split `::` ends at the second.
        result = location + 1 + (t + 1 < parser->tokens.count && parser->tokens.sources[t + 1] == s ? 0 : 1);
    }
    return result;
}

// Where a diagnostic points: the location of its token, or of the end of the token before it.
static uint32_t parse_diagnostic_location(const Parser* parser, const Preprocessor* pp, const ParseDiagnostic* diagnostic)
{
    const uint32_t t = diagnostic->token;
    uint32_t result;
    if (diagnostic->place == BUSTER_PARSE_AFTER_PREVIOUS && t > 0 && t - 1 < parser->tokens.count)
    {
        result = parse_token_end(parser, pp, t - 1);
    }
    else if (t < parser->tokens.count)
    {
        result = pp->output.locations[parser->tokens.sources[t]];
    }
    else
    {
        result = parse_end_location(pp);
    }
    return result;
}

// The message of a diagnostic, with the spelling of the token it names, into `text`.
static void parse_diagnostic_message(const Parser* parser, const Preprocessor* pp, const ParseDiagnostic* diagnostic, char* text, uint32_t capacity)
{
    char spelling[64] = "";
    if (diagnostic->argument != BUSTER_PARSE_NONE && diagnostic->argument < parser->tokens.count)
    {
        const uint32_t a = diagnostic->argument;
        const uint32_t start = pp->output.locations[parser->tokens.sources[a]];
        const uint32_t end = parse_token_end(parser, pp, a);
        const uint8_t* bytes = pp_location_bytes(pp, start);
        const uint32_t length = bytes != 0 && end > start ? (end - start < sizeof(spelling) - 1 ? end - start : (uint32_t)sizeof(spelling) - 1) : 0;
        memcpy(spelling, bytes, length);
        spelling[length] = 0;
    }
    snprintf(text, capacity, parse_messages[diagnostic->message], spelling);
}

// Prints the parse's diagnostics, as clang does: "file:line:column: error: message".
static void parse_print_diagnostics(const Parser* parser, Preprocessor* pp)
{
    for (uint32_t d = 0; d < parser->diagnostic_count; d += 1)
    {
        const ParseDiagnostic* diagnostic = &parser->diagnostics[d];
        char text[256];
        parse_diagnostic_message(parser, pp, diagnostic, text, sizeof(text));
        pp_diagnose(pp, diagnostic->error ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING, parse_diagnostic_location(parser, pp, diagnostic), "%s", text);
    }
}

// `compiler [-fsyntax-only] [options] file.c`: preprocesses and parses the file, with -E's options. For now that is all
// the compiler does: nothing is written.
static __attribute__((unused)) int run_parse_command(int count, char** arguments)
{
    int result = 1;
    Arena arena = arena_reserve(1ull << 30);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    bool scalar = false;
    // --scalar runs the scalar twins of both the preprocessor and the parser.
    for (int i = 0; i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
    }
    const bool parsed = pp_parse_options(count, arguments, &options, &arena, &input, &output, &markers);
    if (!parsed)
    {
        fprintf(stderr, "usage: compiler [-fsyntax-only] [-I dir] [-isystem dir] [-iquote dir] [-idirafter dir] [-D name[=value]] [-U name] "
                        "[-include file] [-std=...] [-trigraphs] [-fno-trigraphs] [-ffreestanding] [-O...] [-nostdinc] [--predefines file] [--scalar] file\n");
    }
    else
    {
        static InternTable table;
        static Preprocessor pp;
        static Parser parser;
        const ParseOptions parse_options = { .scalar = scalar };
        if (!intern_initialize(&table) || !pp_initialize(&pp, &options, &table) || !parse_initialize(&parser, &parse_options, options.language))
        {
            fprintf(stderr, "can't reserve memory\n");
        }
        else
        {
            const bool preprocessed = pp_run(&pp, input);
            const bool parsed_unit = preprocessed && parse_translation_unit(&parser, &pp);
            if (preprocessed)
            {
                parse_print_diagnostics(&parser, &pp);
            }
            result = parsed_unit ? 0 : 1;
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&table);
        }
    }
    arena_release(&arena);
    return result;
}
