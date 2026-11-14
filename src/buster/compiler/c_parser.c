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
// tokens, so the postorder follows from the parents without a walk (parse_tree_columns).
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
// main.c includes this interface after c_preprocessor.c. The types, token/tree readers and release helpers stay
// local to their consumers. The parser's implementation and tables live in c_parser_impl.c, included only in the
// frontend unit that defines BUSTER_PARSER_ENTRY; the other units call its declarations below. A standalone
// consumer, such as benchmark-parser, retains its own complete static implementation.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "host_isa.h"
#include "baked.h"

// The linkage of the entry points that main.c's units compile in one of them, their home, and call there (main.c): a
// unit of its own, such as a benchmark's, has them static, as every other function here.
#ifndef BUSTER_PARSER_ENTRY
#define BUSTER_PARSER_ENTRY static
#endif

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
// The second colon split from one preprocessing `::` token, distinct from adjacent expanded `:` tokens.
#define BUSTER_PARSE_COLONCOLON_SECOND 0x10u
// A `#pragma pack` comes just before the token (parse_mark_pragma_packs, #1127): clang reads it among a declaration's
// specifiers, after a tag's body too, where it takes it for what may follow a type. The preprocessor's output tokens
// never have this bit, which is a context's pending flag alone (BUSTER_PP_JOIN_LINE), so the parser's tokens take it.
#define BUSTER_PARSE_AFTER_PRAGMA_PACK 0x80u
_Static_assert(BUSTER_PARSE_AFTER_PRAGMA_PACK == BUSTER_PP_JOIN_LINE, "a parser token flag only the preprocessor's pending flags have");

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


static const uint8_t parse_role_rules[PARSE_ROLE_COUNT] =
{
#define BUSTER_PARSE_ROLE_RULE(name, level, compare, rule) PARSE_RULE_##rule,
    BUSTER_PARSE_ROLES(BUSTER_PARSE_ROLE_RULE)
#undef BUSTER_PARSE_ROLE_RULE
};


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
    X(EXPECTED_END_OF_ENUMERATOR, "expected '= constant-expression' or end of enumerator definition") \
    X(EXTRANEOUS_CLOSING_BRACE, "extraneous closing brace ('}')") \
    X(FUNCTION_DEFINITION_NOT_ALLOWED, "function definition is not allowed here") \
    X(FUNCTION_DEFINITION_TYPEDEF, "function definition declared 'typedef'") \
    X(EXPECTED_PARENTHESES_AROUND_TYPE_NAME, "expected parentheses around type name in %s expression") \
    X(NAMED_JUMP, "named '%s' is only supported in C2y") \
    X(ANONYMOUS_TAG_DECLARATION, "declaration of anonymous %s must be a definition") \
    X(EMPTY_ENUM, "use of empty enum") \
    X(TYPE_SPECIFIER_MISSING, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]") \
    X(TYPE_SPECIFIER_REQUIRED, "a type specifier is required for all declarations") \
    X(UNKNOWN_TYPE_NAME, "unknown type name '%s'") \
    X(MUST_USE_STRUCT_TAG, "must use 'struct' tag to refer to type '%s'") \
    X(MUST_USE_UNION_TAG, "must use 'union' tag to refer to type '%s'") \
    X(MUST_USE_ENUM_TAG, "must use 'enum' tag to refer to type '%s'") \
    X(EXTRANEOUS_BEFORE_SEMI, "extraneous '%s' before ';'") \
    X(UNEXPECTED_SEMI_BEFORE, "unexpected ';' before '%s'") \
    X(EXTRANEOUS_AFTER_CONDITION, "extraneous ')' after condition, expected a statement") \
    X(KEYWORD_AS_PARAMETER, "invalid parameter name: '%s' is a keyword") \
    X(EXPECTED_STRING, "expected string literal for diagnostic message in static_assert") \
    X(EXPECTED_TYPE, "expected a type") \
    X(EXPECTED_PLATFORM_NAME, "expected a platform name here") \
    X(EXPECTED_VERSION, "expected a version of the form 'major[.minor[.subminor]]'") \
    X(EXPECTED_PARAMETER, "expected parameter declarator") \
    X(EXPECTED_ASM_STRING, "expected string literal in 'asm'") \
    X(EXPECTED_ATTRIBUTE_STRING, "expected string literal as argument of '%s' attribute") \
    X(EXPECTED_ASM_QUALIFIER, "expected 'volatile', 'inline', 'goto', or '('") \
    X(DUPLICATE_ASM_VOLATILE, "duplicate asm qualifier 'volatile'") \
    X(DUPLICATE_ASM_INLINE, "duplicate asm qualifier 'inline'") \
    X(DUPLICATE_ASM_GOTO, "duplicate asm qualifier 'goto'") \
    X(MEANINGLESS_ASM_VOLATILE, "meaningless 'volatile' on asm outside function") \
    X(MEANINGLESS_ASM_INLINE, "meaningless 'inline' on asm outside function") \
    X(MEANINGLESS_ASM_GOTO, "meaningless 'goto' on asm outside function") \
    X(EXPECTED_L_PAREN_AFTER_ASM, "expected '(' after 'asm'") \
    X(EXPECTED_L_PAREN_AFTER_ASM_OPERAND, "expected '(' after 'asm operand'") \
    X(STATEMENT_EXPRESSION_AT_FILE_SCOPE, "statement expression not allowed at file scope") \
    X(EXPECTED_DESIGNATOR, "expected '=' or another designator") \
    X(POSTFIX_AFTER_UNARY, "expression cannot be followed by a postfix '%s' operator; add parentheses") \
    X(EXPECTED_L_PAREN_AFTER_ATTRIBUTE, "expected '(' after 'attribute'") \
    X(EXPECTED_FUNCTION_BODY, "expected function body after function declarator") \
    X(UNEXPECTED_TYPE_NAME, "unexpected type name '%s': expected identifier") \
    X(UNEXPECTED_TYPE_NAME_EXPRESSION, "unexpected type name '%s': expected expression") \
    X(COUNTED_BY_INTEGER, "'counted_by' requires a non-boolean integer type argument") \
    X(SIZED_BY_INTEGER, "'sized_by' requires a non-boolean integer type argument") \
    X(COUNTED_BY_OR_NULL_INTEGER, "'counted_by_or_null' requires a non-boolean integer type argument") \
    X(SIZED_BY_OR_NULL_INTEGER, "'sized_by_or_null' requires a non-boolean integer type argument") \
    X(PARAMETER_REDEFINITION, "redefinition of parameter '%s'") \
    X(PARAMETER_MISSING, "parameter named '%s' is missing") \
    X(DECLARATION_WITHOUT_PARAMETER, "declaration does not declare a parameter") \
    X(PARAMETER_STORAGE_CLASS, "invalid storage class specifier in function declarator") \
    X(ATTRIBUTE_LIST_NOT_ALLOWED, "an attribute list cannot appear here") \
    X(NOT_ATTRIBUTE, "C++11 only allows consecutive left square brackets when introducing an attribute") \
    X(EXPECTED_FIELD_DESIGNATOR, "expected a field designator, such as '.field = 4'") \
    X(EXPECTED_L_PAREN_AFTER_DO_WHILE, "expected '(' after 'do/while'") \
    X(BRACKETS_AFTER_NAME, "brackets are not allowed here; to declare an array, place the brackets after the identifier") \
    X(MEMBER_STORAGE_CLASS, "type name does not allow storage class to be specified") \
    X(MEMBER_FUNCTION_SPECIFIER, "type name does not allow function specifier to be specified") \
    X(UNEXPECTED_TOKEN, "unexpected token") \
    X(TOO_DEEP, "nesting too deep for the parser") \
    X(OUT_OF_MEMORY, "out of memory for the parser") \
    X(INVALID_GNU_METADATA, "inconsistent GNU syntax metadata from the parser") \
    X(TOO_MANY_TOKENS, "too many tokens for the parser")

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
    // The opener clang's "to match this '('" note is at, of an expected closer's error (#147), or BUSTER_PARSE_NONE.
    uint32_t note;
    // The token where the parser knew it, where that is after `token` (a counted_by family's second error, at its
    // argument's first token, known at the type name the first is at, #1842), or 0 for `token` itself.
    uint32_t publication;
    // A missing `;` after a declaration's declarator before what clang takes for a declaration specifier (#147): clang's
    // ParseDeclGroup goes on as if the `;` were there, and reads the specifier as the next declaration's first.
    bool resumed;
    // A parameter's missing or unknown type. A diagnostic-only view may recover it as int after proving the whole
    // enclosing declaration structurally complete, while preserving the original tokens and error.
    bool parameter_type;
    // A balanced malformed type-name tail is removed without inserting a declaration terminator.
    bool resume_delete;
    // A K&R parameter declaration's missing semicolon skips its malformed tail through the next semicolon, or up
    // to the mandatory body brace. Its recovery view replaces [token, resume_end) with one semicolon; zero means
    // the ordinary insertion before token. This is diagnostic-only metadata, never acceptance of the input.
    uint32_t resume_end;
    // A later declarator of an unknown type is never declared by clang. Its failed initializer discards the list
    // from its first comma, before the diagnostic's token. Zero keeps the ordinary [token, resume_end) range.
    uint32_t resume_begin;
    // A case's or default's missing `:` before a `;`, which clang's recovery reads as the label's empty statement
    // (check_prefix_resume puts a `:` before the `;` and parses the view again).
    bool resume_colon;
    // An expression statement's missing `;` after a name alone or a name's call at the statement's start (#1447), which
    // clang's ParseExprStatement reads past as if the `;` were there (it is resumed) once its parser has classified the
    // name: a typo it corrects to a keyword or a type name parses the statement again with that (c_check_typo.c), and an
    // undeclared one makes the expression invalid, whose rest is skipped to the `;` without this error. The checker's
    // view decides which (check_driver_typo_edits).
    bool probe;
    // A tentative typo's tail that a recovery view left out (CheckPrefixTails, check_driver_typo_tail).
    bool tail;
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
    // After GNU attributes at a block-item boundary: the next token chooses a declaration or a statement.
    PARSE_MODE_BLOCK_ATTRIBUTE,
    // In `[[`: the list's `[`, then the second `]` after the list's.
    PARSE_MODE_STANDARD_ATTRIBUTE_LIST,
    PARSE_MODE_STANDARD_ATTRIBUTE_END,
    // In `[[`'s list (parse_scalar_standard_attribute_item): an item's name, or an empty item's `,` or the list's `]`;
    // the second colon of a scope's `::`, and the name after it; after a name and after a scoped one, `(` and its
    // arguments, `,` or `]` (and after a name, `::`); after the arguments, `,` or `]`.
    PARSE_MODE_STANDARD_ATTRIBUTE_NAME,
    PARSE_MODE_STANDARD_ATTRIBUTE_SCOPE,
    PARSE_MODE_STANDARD_ATTRIBUTE_SCOPED_NAME,
    PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_NAME,
    PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_SCOPED_NAME,
    PARSE_MODE_STANDARD_ATTRIBUTE_AFTER_ARGUMENTS,
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
    // __builtin_offsetof's member designator: a member's name (the first, or after `.`), whatever the ordinary names
    // mean; then after a name or a subscript, `.`, `[` or the group's `)`.
    PARSE_MODE_OFFSETOF_MEMBER,
    PARSE_MODE_OFFSETOF_DESIGNATOR,
    // __builtin_available's specs (#148), as clang's ParseAvailabilitySpec reads them: a `*`, or a platform's name (an
    // identifier, whatever the ordinary names mean) and then its version (a number); after one, `,` or the group's `)`.
    PARSE_MODE_AVAILABILITY_SPEC,
    PARSE_MODE_AVAILABILITY_VERSION,
    PARSE_MODE_AVAILABILITY_NEXT,
    // After a _Static_assert's message's first string, or an asm label's: more strings, then the group's `)`.
    PARSE_MODE_MESSAGE,
    // An asm label's string (`int x asm("y")`), and an asm operand's symbolic name in `[...]`.
    PARSE_MODE_ASM_LABEL,
    PARSE_MODE_ASM_NAME,
    // GNU local labels have their own namespace and take an identifier list, never declarators.
    PARSE_MODE_LOCAL_LABEL_NAME,
    PARSE_MODE_LOCAL_LABEL_END,
} ParseMode;

// Frame flags: the frame's contents are a declaration (cleared when it ends), after `typedef`, after a type specifier.
#define BUSTER_PARSE_FRAME_DECLARATION 0x0001u
#define BUSTER_PARSE_FRAME_TYPEDEF 0x0002u
#define BUSTER_PARSE_FRAME_TYPE_SEEN 0x0004u
// A `*` came in the frame's part of a declarator (a group's, or the declaration's own).
#define BUSTER_PARSE_FRAME_POINTER 0x0008u
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
// A parameter list that is the declared name's own function declarator, in a file or a block: a definition's, if a
// body follows the declarator. Its bindings end at its `)` as any list's do, which keeps them for that body.
#define BUSTER_PARSE_FRAME_DEFINITION_PARAMETERS 0x800000u
// The declaration's current declarator follows a comma: a function definition has one declarator, so no body follows.
#define BUSTER_PARSE_FRAME_LATER_DECLARATOR 0x1000000u
// GNU attributes seen at a block-item boundary, classified only to resolve declaration/expression ambiguity.
#define BUSTER_PARSE_FRAME_STATEMENT_ATTRIBUTE 0x2000000u
#define BUSTER_PARSE_FRAME_DECLARATION_ATTRIBUTE 0x4000000u
// A GNU attribute specifier, even an empty one, seen there: after one, the names decide, and C23's attributes alone don't.
#define BUSTER_PARSE_FRAME_GNU_ATTRIBUTE 0x10000000u
#define BUSTER_PARSE_FRAME_BLOCK_ATTRIBUTES (BUSTER_PARSE_FRAME_STATEMENT_ATTRIBUTE | BUSTER_PARSE_FRAME_DECLARATION_ATTRIBUTE | BUSTER_PARSE_FRAME_GNU_ATTRIBUTE)
// GNU attributes came after a bit-field's width (`int x : 3 __attribute__((packed))`, the member's): more of them, or
// what ends the member's declarator, may follow, and nothing else. A flag and not a mode of its own, which cost the scalar
// parser instructions on every token (#430's review): a new mode's values moved the switch's cases, and a second call of
// parse_scalar_operator made it a function of its own.
#define BUSTER_PARSE_FRAME_AFTER_WIDTH 0x8000000u
// A file-scope declaration's leading run of attribute lists, as clang's ParseTopLevelDecl reads them before its
// ParseExternalDeclaration (#1447): C23's and GNU's in any order, then `__extension__`s, which the declaration's tokens
// have been alone so far (the last of them just before the token). Another `__extension__` goes on with it, as one at a
// declaration's start does; the specifiers after it are ParseDeclarationSpecifiers', where an `__extension__` isn't one.
#define BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES 0x20000000u
// An unknown type specifier was diagnosed in this declaration. Clang parses the first initializer, but an invalid
// type makes later declarators stop before their initializers (ParseDeclGroup's isInvalidType check, #2640).
#define BUSTER_PARSE_FRAME_UNKNOWN_TYPE 0x40000000u

// What a declarator's name is, from the derivation its declarator applies to it first (going out from the name, past
// groups without a `*`), as clang's Declarator::isFunctionDeclarator decides it: none read yet, not known yet, a
// function (whose `{` may start a body), or anything else.
typedef enum ParseDerivation
{
    PARSE_DERIVATION_NONE,
    PARSE_DERIVATION_PENDING,
    PARSE_DERIVATION_FUNCTION,
    PARSE_DERIVATION_OTHER,
} ParseDerivation;

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
    // An initializer started by a designator: `=` must precede its value unless it consists of one array subscript.
    PARSE_ITEM_DESIGNATOR,
    // After a declaration's `( expression-list )` initializer (clang's C++ direct initializer, #1127): only what ends
    // the declarator may come.
    PARSE_ITEM_DIRECT_INITIALIZER,
    PARSE_ITEM_ARRAY_DESIGNATOR,
    // GNU's old-style `member: value` designator, whose colon precedes its value.
    PARSE_ITEM_OLD_STYLE_DESIGNATOR,
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
    // In a frame that holds declarations: the current declarator's name (BUSTER_PARSE_NONE if none, or if it isn't
    // bound), whose binding waits until the declarator is complete, as its scope begins there (C 6.2.1p7), and what it
    // is bound as; what the declarator derives the name as (ParseDerivation), and while that is pending, the index of
    // the frame the name is in (the declarator's innermost group that hasn't closed past it). An enum's body holds its
    // current enumerator's name, which is bound once the enumerator ends.
    uint32_t name;
    uint32_t name_frame;
    // A missing type is diagnosed only after its declarator's syntax has been read. Zero, or the name token + 1.
    uint32_t typeless_name;
    // The first comma after an unknown type's first declarator, plus one. Its later declarators have no Sema binding.
    uint32_t invalid_declarators;
    // The frame a declarator's name in this frame belongs to: this one, or for a group, its parent's.
    uint32_t declaration;
    uint8_t name_meaning;
    uint8_t derivation;
    // What a comma at this frame's top level is (PARSE_ROLE_COMMA or PARSE_ROLE_SEPARATOR), by its innermost group of
    // ( [ {: the ?: and case groups take their parent's.
    uint8_t comma_role;
    // The asm statement's volatile, inline and goto qualifiers, checked before its `(`.
    uint8_t asm_qualifiers;
};

// What a name is bound as: an ordinary identifier or a typedef name, and for a K&R identifier list's names, which are
// ordinary identifiers as far as parsing goes: one in the list being read, a definition's parameter its declarations
// haven't declared yet, and one they have. PARSE_MEANING_ORDINARY is no binding the parse has seen (what a declarator
// binds as an ordinary identifier, ParseFrame.name_meaning, before parse_scalar_bind makes it PARSE_MEANING_DECLARED):
// clang's lookup at a block item's GNU attributes tells a declared name from an undeclared one
// (parse_scalar_attribute_name).
// The kind of a tag in scope (ParseScalar.tags), 0 for none.
#define BUSTER_PARSE_TAG_STRUCT 1
#define BUSTER_PARSE_TAG_UNION 2
#define BUSTER_PARSE_TAG_ENUM 3

typedef enum ParseMeaning
{
    PARSE_MEANING_ORDINARY,
    PARSE_MEANING_TYPEDEF,
    PARSE_MEANING_KR_LISTED,
    PARSE_MEANING_KR_PARAMETER,
    PARSE_MEANING_KR_DECLARED,
    PARSE_MEANING_DECLARED,
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
    // For a do-statement, the `)` of its `while (...)`; for an if-statement, the binding log's length where its
    // then-statement begins, which an else restores.
    uint32_t extra;
    // For a selection or an iteration statement, which is a block, as each of its substatements is (C 6.8.4p3,
    // 6.8.5p5): the binding log's length where its scope begins (its condition's `(`, or `do`), which its end restores.
    // BUSTER_PARSE_NONE for blocks and labels.
    uint32_t scope;
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

// The spellings of the attributes whose arguments clang's parser reads its own way (parse_scalar_custom_attribute).
#define BUSTER_PARSE_CUSTOM_ATTRIBUTES 12u

// The bindings a parse starts from: a meaning for every IdentId, PARSE_MEANING_ORDINARY but for clang's predeclared
// typedef names, which every parse leaves as it found them.
typedef struct ParseMeanings ParseMeanings;
struct ParseMeanings
{
    uint8_t* meanings;
    uint32_t capacity;
    // The scalar parser reads attribute spellings only where they decide declaration versus statement syntax.
    const InternTable* table;
    // availability's, external_source_symbol's, type_tag_for_datatype's, objc_bridge_related's, swift_newtype's and
    // swift_wrapper's names, and their keyword spellings (#1447), found once in `table` where the meanings are made (0 for a name the unit doesn't have):
    // parse_flag_custom_attributes reads these, not the table, which a caller may have released by then (the parser
    // benchmark's is).
    IdentId custom_attributes[BUSTER_PARSE_CUSTOM_ATTRIBUTES];
};

typedef struct ParseScalar ParseScalar;
typedef struct ParseBindingTrace ParseBindingTrace;
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
    const InternTable* table;
    uint32_t* log_names;
    uint8_t* log_meanings;
    uint32_t log_count;
    // What a definition's parameter list bound, by name and meaning, kept at its `)` for the body that may follow its
    // declarator, which binds them again (C 6.2.1p4: their scope is the body's, not the rest of the declarator's); and
    // for an entry of a tag the list declared (#147, ParseScalar.tags), the tag's kind (0 for none) and name.
    uint32_t* kept_names;
    uint8_t* kept_meanings;
    uint8_t* kept_tags;
    uint32_t* kept_tag_names;
    uint32_t kept_count;
    // A K&R definition's parameter declarations' scope, which its body's `{` ends, and the first token of the current one.
    uint32_t body_scope;
    uint32_t kr_declaration;
    // A typeless K&R function waits until its parameter declarations have been parsed, before the body begins.
    uint32_t typeless_function;
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
    // The opener the error's "to match this" note is at (ParseDiagnostic.note), or BUSTER_PARSE_NONE.
    uint32_t error_note;
    // A second error clang gives after the first (#1842): a counted_by family attribute's of an argument whose type name
    // it read as an expression's, at the argument's first token (BUSTER_PARSE_NONE if none), and its message.
    uint32_t error_follow_token;
    uint8_t error_follow_message;
    uint8_t error_message;
    uint8_t error_place;
    // Whether the error is a missing `;` that clang's recovery reads past (ParseDiagnostic.resumed).
    bool error_resumed;
    bool error_resume_delete;
    uint32_t error_resume_end;
    uint32_t error_resume_begin;
    // Whether the error is a case's or default's missing `:` before a `;` (ParseDiagnostic.resume_colon).
    bool error_resume_colon;
    // Whether the error is an expression statement's missing `;` after its first name (ParseDiagnostic.probe).
    bool error_probe;
    // The diagnostics, in order.
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_count;
    uint32_t diagnostic_capacity;
    uint32_t errors;
    // Whether a block item's GNU attributes came before an identifier no binding of the parse names
    // (ParseScalarResult.undecided).
    bool undecided;
    // The tag names in scope (#147, parse_scalar_bind_tag): a BUSTER_PARSE_TAG_* kind per name, 0 for none, and the
    // declarations that made them, each its undo-log entry's position, its name and the kind it hid, which a scope's end
    // pops with its entry (parse_scalar_restore).
    uint8_t* tags;
    uint32_t* tag_positions;
    uint32_t* tag_names;
    uint8_t* tag_hidden;
    uint32_t tag_count;
    // Optional source-event recorder. Null for syntax-only parsing and the vector fallback.
    ParseBindingTrace* binding_trace;
};

#include "buster/compiler/c_parser_binding_trace.c"


// The flags a group's frame inherits from its parent: no binding inside members and type names (only an attribute
// list's own items are names whatever their spelling: `__aligned__(__alignof__(long long))` is C inside its call), and
// being in a function.
#define BUSTER_PARSE_FRAME_INHERITED (BUSTER_PARSE_FRAME_NO_BIND | BUSTER_PARSE_FRAME_IN_FUNCTION)


// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's precedence stacks

// Packs a role's level, left and right thresholds and rule for parse_scalar_emit, which reads them all at once.
#define BUSTER_PARSE_ROLE_INFO(role) ((uint32_t)parse_role_levels[role] | (uint32_t)parse_role_left_thresholds[role] << 8 | \
                                      (uint32_t)parse_role_right_thresholds[role] << 16 | (uint32_t)parse_role_rules[role] << 24)


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


// What a declaration's state is reset to by the `;` (or `{` of a body) that ends it.
#define BUSTER_PARSE_FRAME_ITEM_STATE (BUSTER_PARSE_FRAME_DECLARATION | BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_TYPE_SEEN | BUSTER_PARSE_FRAME_POINTER | \
                                       BUSTER_PARSE_FRAME_AFTER_STAR | BUSTER_PARSE_FRAME_EMPTY_OK | BUSTER_PARSE_FRAME_KR | BUSTER_PARSE_FRAME_SPECIFIED | \
                                       BUSTER_PARSE_FRAME_CONDITIONAL | BUSTER_PARSE_FRAME_NAME_REQUIRED | BUSTER_PARSE_FRAME_NAME_OPTIONAL | \
                                       BUSTER_PARSE_FRAME_ABSTRACT | BUSTER_PARSE_FRAME_AUTO | BUSTER_PARSE_FRAME_TYPELESS | \
                                       BUSTER_PARSE_FRAME_LATER_DECLARATOR | BUSTER_PARSE_FRAME_AFTER_WIDTH | BUSTER_PARSE_FRAME_LEADING_ATTRIBUTES | \
                                       BUSTER_PARSE_FRAME_UNKNOWN_TYPE)


// The kinds of argument a keyword group takes: a type name, an expression, either, or the keyword's own (none: a call
// with no arguments).
#define BUSTER_PARSE_ARGUMENT_EXPRESSION 0
#define BUSTER_PARSE_ARGUMENT_TYPE 1
#define BUSTER_PARSE_ARGUMENT_EITHER 2


// The flags a declarator's group inherits from the frame its declarator is in.
#define BUSTER_PARSE_FRAME_DECLARATOR_STATE (BUSTER_PARSE_FRAME_TYPEDEF | BUSTER_PARSE_FRAME_DECLARATION | BUSTER_PARSE_FRAME_ABSTRACT | \
                                             BUSTER_PARSE_FRAME_NAME_OPTIONAL | BUSTER_PARSE_FRAME_NAME_REQUIRED)


typedef struct ParseScalarResult ParseScalarResult;
struct ParseScalarResult
{
    // The errors found: a top-level declaration's first syntax error and any malformed attribute tail it leaves,
    // and the errors clang reports without changing how it parses (a missing type specifier).
    uint32_t errors;
    // The scratch arena couldn't hold the parser's stacks: nothing was parsed.
    bool out_of_memory;
    // A typedef name was declared again at file scope as an ordinary identifier, which changes how what follows parses.
    bool retyped;
    // A block item's GNU attributes came before an identifier that no binding of the parse names, and no `(` follows
    // (parse_scalar_attribute_name): an implicit int's declaration where the name is undeclared, clang's error where an
    // earlier file-scope declaration the tokens don't have declares it. Exact where the tokens start at the unit's.
    bool undecided;
};


// ---------------------------------------------------------------------------------------------------------------------
// The kernels
//
// The data-parallel path builds the same tree as parse_scalar in passes over the whole token array, each a kernel
// with a scalar twin whose output is the same:
//   - K1, brackets (parse_brackets_*): the depth before each token and its enclosing opener (a closer's is its opener),
//     run on the kinds for ( [ {, then on the roles for every group, ? : and case ... : included.
//   - K3, type names (parse_typedefs, parse_classify_*): a scalar pass over the typedef declarations only, in source
//     order, then every identifier is classified against the set of names they declare.
//   - K2, roles (parse_roles_*): each token's role from its kind, its neighbors' kinds, what the token before it is
//     (ParsePred, from that token's role) and the role of the group it is in. A token the rules can't be sure of is
//     flagged, and the top-level declaration it is in goes to the scalar parser.
//   - K4, parents (parse_parents_*): L and R by nearest-smaller-value searches on (depth, level) keys, then each
//     role's rule.
//   - K5, statements (parse_statements): the statement stack over the statement-level tokens of blocks.
//   - The fallback (parse_fallback): the scalar parser on every top-level declaration with a flagged token.

// What the token before says about the next one, from its role (and for specifiers and `*`, its kind).
typedef enum ParsePred
{
    // A declaration or a statement may start: the start, after `;` and the `{` and `}` of blocks, a label, case.
    PARSE_PRED_ITEM,
    // A statement but not a declaration starts: after else, do, and the `)` of if, while, for and switch.
    PARSE_PRED_HEAD,
    // An operand just ended.
    PARSE_PRED_OPERAND,
    // After `*` or a qualifier or attribute that belongs to one.
    PARSE_PRED_STAR,
    // After a type specifier.
    PARSE_PRED_TYPE,
    // After another specifier: a qualifier, a storage class, typedef, an attribute.
    PARSE_PRED_SPECIFIER,
    // A type name may start: the `(` of a cast, sizeof, a call or parameter list and a keyword's group, a separator.
    PARSE_PRED_PARAMETER,
    // An operand comes next.
    PARSE_PRED_EXPRESSION,
    // After a keyword that introduces what follows: struct, union, enum, if, __attribute__, typeof, ...
    PARSE_PRED_LEAF,
    PARSE_PRED_COUNT,
} ParsePred;


// What K1 finds of each block of 64 tokens, which K2 reads instead of working it out again from the depths and enclosing
// openers: each token's enclosing opener as a lane of the block (0xFF if the opener is before the block), the depth
// before each token relative to the block's first token, and the block's openers still open at its end.
typedef struct ParseBracketBlock ParseBracketBlock;
struct ParseBracketBlock
{
    uint8_t opener_lanes[64];
    int8_t relative_depths[64];
    uint64_t open;
} __attribute__((aligned(64)));

// What K2's rules read of each lane's kind and its neighbors' kinds, the same in every round, for a block
// (parse_roles_block_avx512): `base` and `alternative` (parse_rules_apply8) and `rules`, the BUSTER_PARSE_RULE_* bits
// of the rules that apply to the lane; and in `facts`, a `{`'s BUSTER_PARSE_BRACE_* bits, or the BUSTER_PARSE_CODE_*
// of a comma, a colon, an ellipsis, or an attribute's or an asm's `(`. parse_role_statics_avx512 works them out.
typedef struct ParseRoleStatics ParseRoleStatics;
struct ParseRoleStatics
{
    uint8_t base[64];
    uint8_t alternative[64];
    uint8_t rules[64];
    uint8_t facts[64];
} __attribute__((aligned(64)));

// The working arrays of the kernels, by token, and what they found.
typedef struct ParseKernels ParseKernels;
struct ParseKernels
{
    uint8_t* vkinds;
    // The depth before each token of ( [ { (K1), and each token's node depth in every group, ? : and case : included,
    // where a closer is outside its group (K1's second part).
    uint32_t* depths;
    uint32_t* node_depths;
    uint32_t* enclosing;
    // K1's view of each block of 64 tokens, and what K2's rules read of each block's kinds.
    ParseBracketBlock* bracket_blocks;
    ParseRoleStatics* role_statics;
    uint8_t* roles;
    uint32_t* parents;
    // Nonzero for a token the rules can't be sure of: its top-level declaration is parsed by the scalar parser.
    uint8_t* flags;
    // The scratch stack K1's twin needs, and K4's.
    uint32_t* stack;
    uint32_t* stack_keys;
    uint32_t* waiting;
    uint32_t* waiting_thresholds;
    uint32_t* lefts;
    uint32_t* left_key_values;
    // By name: whether the unit declares it as a typedef, its first declarator (0 for clang's predeclared names),
    // and the current head of its declaration stack (record + 1; zero means no source declaration).
    uint8_t* typedef_names;
    uint32_t* typedef_first;
    uint32_t* typedef_heads;
    // The file's typedef declarations in order, with their complete-declarator activation points, exclusive scope
    // ends and previous declarations of the same name. K3 replays these sparse records while classifying identifier
    // masks. Ordinary shadows still go to the scalar parser when K2 sees the declarator hiding a typedef name.
    uint32_t* typedef_log;
    uint32_t* typedef_tokens;
    uint32_t* typedef_points;
    uint32_t* typedef_ends;
    uint32_t* typedef_previous;
    uint8_t* typedef_scopes;
    uint32_t typedef_count;
    // K6's frames, an entry per node depth, and each token's class.
    uint8_t* check_group;
    uint8_t* check_kind;
    uint8_t* check_part;
    uint8_t* check_items;
    uint8_t* check_typed;
    uint8_t* check_after_body;
    uint8_t* check_classes;
    // The group each opener opens and each closer closes, by K6 (PARSE_GROUP_OTHER for the other tokens): what tells a
    // declarator's parameter list from a call's arguments (parse_function_roles, #1968).
    uint8_t* check_groups;
    // What the kernels report.
    bool unbalanced;
    bool unsupported;
};

#define BUSTER_PARSE_TYPEDEF_FILE 1
#define BUSTER_PARSE_TYPEDEF_SCOPED 2


// ---------------------------------------------------------------------------------------------------------------------
// K5, statements

// The statement stack of parse_parser's, over the kernels' arrays: the same states and rules.
typedef struct ParseStatementStack ParseStatementStack;
struct ParseStatementStack
{
    ParseStatement* items;
    uint32_t count;
    uint32_t* parents;
};


// What the vector K5 reads of each token's surroundings, a bit per token for 64 tokens: whether its statement context is
// a block (OPEN_BLOCK or OPEN_BODY), as parse_statement_event reads it (the group around the token, or for a closer of
// ( [ { the group around its opener); and whether it is a closer whose opener follows `if`, or `while`. The vector K2
// writes them, and parse_statement_contexts checks them.
typedef struct ParseStatementContext ParseStatementContext;
struct ParseStatementContext
{
    uint64_t blocks;
    uint64_t ifs;
    uint64_t whiles;
};


// ---------------------------------------------------------------------------------------------------------------------
// K6, the checks
//
// The kernels build the tree that C's grammar gives well-formed code. K6 checks that the code is well-formed, so that
// every top-level declaration the vector path takes is one the scalar parser accepts, with the same tree. Each token
// gets a class, its role refined by its kind or its typed state where what may follow it differs, and a mode: its
// group's type (a block, a parameter list, a call's arguments, a declarator in parentheses, ...) and, in a group of
// items, its part of its item (its start, a declaration's specifiers and declarators, an initializer, a bit-field's
// width, a statement). A token passes if its class may follow the class before it (parse_check_tables.follows), may
// appear in its mode (allows), may end its group if the next token closes it (ends), and doesn't skip the name a
// declaration needs (named); and a label's name must start its statement, and a for's parentheses hold three clauses.
// A token that fails is flagged: its top-level declaration goes to the scalar parser, which reports the error, or parses
// it where the checks are stricter than C. What the checks don't take (asm statements, the builtins that take a type,
// statement expressions, K&R parameter lists, storage classes in parameters, C23's attributes, _Alignas) fails them
// wherever it is.
//
// The twin keeps a frame per node depth. The vector kernel finds the same modes with nearest-smaller-value searches: a
// token's item starts at the nearest token at its depth before it that starts one, or at itself, and its part comes
// from the nearest start, `,`, `=` or bit-field `:` at its depth before it.

// The classes beyond the roles (a class below PARSE_ROLE_COUNT is that role, as nothing refines it): OPERAND is then a
// name, SPECIFIER a specifier other than a type before any type specifier, PREFIX a prefix operator other than `*`,
// RETURN `return`, OPEN_CASE `case`, and CLOSE_ATTRIBUTE_SPECIFIER an attribute's closer among the specifiers before any
// type specifier.
#define BUSTER_PARSE_CLASSES(X) \
    X(CONSTANT) \
    X(STRING) \
    X(ELLIPSIS) \
    X(WORD) \
    X(TYPE) \
    X(TYPED) \
    X(STORAGE) \
    X(TYPED_STORAGE) \
    X(TYPED_ATTRIBUTE) \
    X(STAR) \
    X(SIZEOF) \
    X(LABEL_ADDRESS) \
    X(TAG_KEYWORD) \
    X(ATTRIBUTE_KEYWORD) \
    X(ASM_KEYWORD) \
    X(ASM_QUALIFIER) \
    X(SPECIFIER_KEYWORD) \
    X(ALIGNAS_KEYWORD) \
    X(OPERAND_KEYWORD) \
    X(OFFSETOF_KEYWORD) \
    X(VA_ARG_KEYWORD) \
    X(BIT_CAST_KEYWORD) \
    X(ATOMIC_KEYWORD) \
    X(BITINT_KEYWORD) \
    X(ALIGNAS_CLOSE) \
    X(TYPED_ALIGNAS_CLOSE) \
    X(EXTENSION) \
    X(ASSERT_KEYWORD) \
    X(STATEMENT_KEYWORD) \
    X(BODY_NAME) \
    X(JUMP) \
    X(GOTO) \
    X(DEFAULT) \
    X(ARROW) \
    X(LOCAL_LABEL)

typedef enum ParseClass
{
    PARSE_CLASS_ROLES = PARSE_ROLE_COUNT - 1,
#define BUSTER_PARSE_CLASS_ENUM(name) PARSE_CLASS_##name,
    BUSTER_PARSE_CLASSES(BUSTER_PARSE_CLASS_ENUM)
#undef BUSTER_PARSE_CLASS_ENUM
    PARSE_CLASS_COUNT,
} ParseClass;

_Static_assert(PARSE_CLASS_COUNT <= 128, "classes must fit 128-entry tables");

// The group types.
typedef enum ParseGroupType
{
    PARSE_GROUP_FILE,
    PARSE_GROUP_BLOCK,
    PARSE_GROUP_STRUCT,
    PARSE_GROUP_ENUM,
    PARSE_GROUP_PARAMETERS,
    PARSE_GROUP_ARGUMENTS,
    // Declarators in parentheses: one that needs a name, an abstract one, and a parameter's, which may have one.
    PARSE_GROUP_DECLARATOR,
    PARSE_GROUP_ABSTRACT,
    PARSE_GROUP_OPTIONAL,
    PARSE_GROUP_EXPRESSION,
    // A cast's or a compound literal's type name, and sizeof's, typeof's and _Atomic's type name or expression.
    PARSE_GROUP_TYPE,
    PARSE_GROUP_SIZEOF,
    // A subscript (which has an expression), an array declarator's (which may be empty), and an array designator's
    // (which has an assignment-expression, #1867).
    PARSE_GROUP_SUBSCRIPT,
    PARSE_GROUP_ARRAY,
    PARSE_GROUP_DESIGNATOR,
    PARSE_GROUP_CASE,
    PARSE_GROUP_FOR,
    PARSE_GROUP_INITIALIZER,
    PARSE_GROUP_ATTRIBUTE,
    PARSE_GROUP_ATTRIBUTE_LIST,
    PARSE_GROUP_ASM_LABEL,
    // __builtin_offsetof's (a type name, then a member designator) and __builtin_va_arg's (an expression, then a type
    // name).
    PARSE_GROUP_OFFSETOF,
    PARSE_GROUP_VA_ARG,
    // __builtin_bit_cast's (a type name, then an expression), _Static_assert's (a constant expression, then perhaps a
    // message), _Alignas's (a type name or a constant expression) and _BitInt's (a constant expression).
    PARSE_GROUP_BIT_CAST,
    PARSE_GROUP_ASSERT,
    PARSE_GROUP_ALIGNAS,
    PARSE_GROUP_BITINT,
    // What the checks don't take: asm statements and the other builtins' groups.
    PARSE_GROUP_OTHER,
    PARSE_GROUP_COUNT,
} ParseGroupType;

// What an item is, by its start: a statement or an expression, a declaration (or a designator, or an enumerator), or a
// _Static_assert.
#define BUSTER_PARSE_ITEM_OTHER 0
#define BUSTER_PARSE_ITEM_DECLARATION 1
#define BUSTER_PARSE_ITEM_ASSERTION 2

// The parts of an item, in the groups of items.
typedef enum ParsePart
{
    PARSE_PART_START,
    PARSE_PART_DECLARATION,
    PARSE_PART_INITIALIZER,
    PARSE_PART_WIDTH,
    PARSE_PART_OTHER,
} ParsePart;

#define BUSTER_PARSE_MODES(X) \
    X(NONE) \
    X(FILE_START) \
    X(FILE_DECLARATION) \
    X(FILE_INITIALIZER) \
    X(BLOCK_START) \
    X(BLOCK_DECLARATION) \
    X(BLOCK_INITIALIZER) \
    X(BLOCK_STATEMENT) \
    X(STRUCT_START) \
    X(STRUCT_DECLARATION) \
    X(STRUCT_WIDTH) \
    X(PARAMETERS_START) \
    X(PARAMETERS_DECLARATION) \
    X(FOR_START) \
    X(FOR_DECLARATION) \
    X(FOR_INITIALIZER) \
    X(FOR_EXPRESSION) \
    X(FOR_CLAUSE_START) \
    X(TYPE_START) \
    X(TYPE_DECLARATION) \
    X(TYPE_ARGUMENT) \
    X(SIZEOF_START) \
    X(SIZEOF_TYPE) \
    X(SIZEOF_EXPRESSION) \
    X(ENUM_START) \
    X(ENUM_NAME) \
    X(ENUM_VALUE) \
    X(ARGUMENTS_START) \
    X(ARGUMENTS) \
    X(DECLARATOR) \
    X(ABSTRACT) \
    X(OPTIONAL) \
    X(EXPRESSION) \
    X(CASE) \
    X(SUBSCRIPT) \
    X(ARRAY) \
    X(INITIALIZER_START) \
    X(INITIALIZER_DESIGNATOR) \
    X(INITIALIZER) \
    X(ATTRIBUTE) \
    X(ATTRIBUTE_LIST_START) \
    X(ATTRIBUTE_LIST) \
    X(ASM_LABEL) \
    X(MEMBER_START) \
    X(MEMBER) \
    X(CONDITIONAL) \
    X(ASSERT_CONDITION) \
    X(ASSERTION) \
    X(MESSAGE_START) \
    X(MESSAGE)

typedef enum ParseCheckMode
{
#define BUSTER_PARSE_MODE_ENUM(name) PARSE_CHECK_##name,
    BUSTER_PARSE_MODES(BUSTER_PARSE_MODE_ENUM)
#undef BUSTER_PARSE_MODE_ENUM
    PARSE_CHECK_MODE_COUNT,
} ParseCheckMode;

_Static_assert(PARSE_CHECK_MODE_COUNT <= 64, "modes must fit 64-entry tables");

// A set of classes, two words of bits.
typedef struct ParseClassSet ParseClassSet;
struct ParseClassSet
{
    uint64_t words[2];
};

// K6's tables: by class, the classes that may follow it; by mode, the classes that may appear in it, and those that may
// end a group in it; and the classes that need a name after them where one is required, and those that show a name
// was skipped after them.
typedef struct ParseCheckTables ParseCheckTables;
struct ParseCheckTables
{
    ParseClassSet follows[128];
    ParseClassSet allows[64];
    ParseClassSet ends[64];
    ParseClassSet before_name;
    ParseClassSet after_name;
    ParseClassSet operand_starts;
    ParseClassSet specifier_ends;
    uint64_t named_modes;
    uint64_t declaration_modes;
};


// What K6 carries per node depth (its frames, which the twin and the vector kernel share): the open group's type,
// what its current item is (BUSTER_PARSE_ITEM_*), its part, how many items it has started (to 3), whether the
// specifiers before its opener's keyword had a type (for _Alignas's closer, which is a specifier as an attribute is),
// and whether the last token at the depth closed a tag's body (or was an attribute specifier after one). An array of
// bytes each, by node depth.
typedef struct ParseCheckFrames ParseCheckFrames;
struct ParseCheckFrames
{
    uint8_t* group;
    uint8_t* kind;
    uint8_t* part;
    uint8_t* items;
    uint8_t* typed;
    uint8_t* after_body;
};

// What K6 carries from token to token: the class, role, node depth, mode, item and start bit of the token before, and
// the class of the one before that.
typedef struct ParseCheckCarry ParseCheckCarry;
struct ParseCheckCarry
{
    uint8_t before_previous;
    uint8_t previous;
    uint8_t previous_role;
    uint8_t previous_mode;
    uint8_t previous_items;
    uint8_t previous_start;
    uint8_t previous_group;
    uint32_t previous_depth;
};


// Why a token failed K6's checks, for test-parser's reports: bits of the checks.
#define BUSTER_PARSE_CHECK_PAIR 0x01
#define BUSTER_PARSE_CHECK_MODE 0x02
#define BUSTER_PARSE_CHECK_END 0x04
#define BUSTER_PARSE_CHECK_NAME 0x08
#define BUSTER_PARSE_CHECK_LABEL 0x10
#define BUSTER_PARSE_CHECK_CLAUSES 0x20
#define BUSTER_PARSE_CHECK_ERROR 0x40
#define BUSTER_PARSE_CHECK_TAG 0x80


// ---------------------------------------------------------------------------------------------------------------------
// The fallback

// What the scalar parser reads, for the fallback's ranges.
typedef struct ParseInput ParseInput;
struct ParseInput
{
    const uint8_t* kinds;
    const uint32_t* names;
    const uint8_t* flags;
    uint32_t count;
    CLanguage language;
    ParseMeanings* meanings;
    Arena* scratch;
    // Room for the scalar parser's diagnostics of a range, which the fallback drops: an error sends the whole unit to the
    // scalar parser, which reports it as it would alone.
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_capacity;
};

typedef struct ParseFallback ParseFallback;
struct ParseFallback
{
    // Top-level declarations the scalar parser parsed, and their tokens.
    uint32_t items;
    uint64_t tokens;
    // The scalar parser found an error in one, ran out of memory, saw one declare a typedef name again as an ordinary
    // identifier, or met a name only earlier file-scope declarations decide (ParseScalarResult.undecided): the whole unit
    // goes to the scalar parser.
    bool errors;
};


// ---------------------------------------------------------------------------------------------------------------------
// The kernels' scalar twins together

typedef struct ParseKernelResult ParseKernelResult;
struct ParseKernelResult
{
    ParseFallback fallback;
    uint64_t flagged_tokens;
    bool everything;
};


// ---------------------------------------------------------------------------------------------------------------------
// The kernels in AVX-512
//
// Each works on blocks of 64 tokens in byte lanes or 32 in 16-bit lanes, and carries what crosses a block boundary in
// a small state: the depth, the openers still open, the stacks of candidates of the nearest-smaller-value searches.
// Loads past the last token read the padding (BUSTER_PARSE_TOKEN_PADDING), which holds PARSE_KIND_END and zeros, and
// stores past it write the arrays' slack; the results there are never read.

// SIMD kernels use intrinsic types directly.

// Index vectors for _mm512_permutex2var_epi8 and _epi16 that shift lanes up (lane i gets lane i - s of the first
// source) or down (lane i gets lane i + s), taking the second source's lane 0 where that falls outside the vector.
typedef struct ParseShifts ParseShifts;
struct ParseShifts
{
    __m512i up8[6];
    __m512i down8[6];
    __m512i up16[5];
    __m512i down16[5];
    __m512i lanes8;
    __m512i lanes16;
    // Shift up by 2 taking lanes 0 and 1 of the second source below that.
    __m512i up2_with_fill;
    // For K4 with the stack's top in registers: byte lanes reversed (lane i gets 63 - i), and every lane 63; with
    // _mm512_maskz_permutexvar_epi8 under BUSTER_PARSE_WORD_BYTES, half h of a byte vector as 16-bit lanes in order
    // (words) and in the order _mm512_unpacklo_epi16 and _mm512_unpackhi_epi16 put back in order as 32-bit lanes
    // (unpacked: word 8l + k gets byte 32h + 4l + k, word 8l + 4 + k byte 32h + 16 + 4l + k); and under
    // BUSTER_PARSE_DWORD_BYTES, quarter q as 32-bit lanes.
    __m512i reverse8;
    __m512i last8;
    __m512i words[2];
    __m512i unpacked[2];
    __m512i dwords[4];
};

#define BUSTER_PARSE_WORD_BYTES 0x5555555555555555ull
#define BUSTER_PARSE_DWORD_BYTES 0x1111111111111111ull


// ---------------------------------------------------------------------------------------------------------------------
// K4 in AVX-512

// Keys in 16-bit lanes are node depth * 32 + level, so the vector path takes depths below this, and the twin the rest.
#define BUSTER_PARSE_MAX_VECTOR_DEPTH 2047

// K4's per-role tables, as bytes: a role's level, whether it closes a group, its thresholds and its parent rule; and per
// level, whether an L candidate of it is weak (parse_level_is_weak) and which way equal keys go.
typedef struct ParseParentTables ParseParentTables;
struct ParseParentTables
{
    uint8_t levels[128];
    uint8_t closers[128];
    uint8_t left_thresholds[128];
    uint8_t right_thresholds[128];
    uint8_t rules[128];
    uint16_t weak[32];
    uint16_t ties[32];
    // Both by level: bit 0 weak, bit 1 ties to the left.
    uint16_t weak_ties[32];
};


// ---------------------------------------------------------------------------------------------------------------------
// K2 in AVX-512
//
// The rules of parse_role_rule, for 64 tokens at once in byte lanes. A token's role depends on the token before it
// (through its pred class), on its group's role and on the typed state of the specifiers before it, so the block is
// evaluated in rounds: each round computes every lane's role from the previous round's, until nothing changes. Every
// dependency points backward, so the fixpoint is the sequential twin's result, and a round fixes at least one more lane:
// a block that hasn't converged after BUSTER_PARSE_ROLE_ROUNDS rounds is done by the twin's loop. What crosses blocks:
// the features of the two tokens before the block, its typed state, and by depth the role of each open group's opener
// (with the typed state before an attribute's keyword, and the kind before it).
//
// What each lane's kind and its neighbors' kinds decide is worked out for every block in a pass before the rounds
// (parse_role_statics_avx512), and a round reads the rest from features (parse_features8): a lane's role where no rule
// holds, the roles its rules give it, and the rules that apply to it as bits, which one table lookup on the features of
// the token before tests. The rounds start from those rules applied to each kind's commonest role before them, with the
// closers by their openers. A round's roles are final as soon as no lane reads anything that changed in them: no further
// round needs to confirm them. On sqlite3 and the libc corpus nearly every block converges in one round, so the block
// loop runs one, and a block that needs more leaves it.

#define BUSTER_PARSE_ROLE_ROUNDS 8
// The blocks whose statics (parse_role_statics_avx512) are worked out together, before their rounds: 8 KB of them.
#define BUSTER_PARSE_ROLE_CHUNK 32
// The rounds the block loop runs (parse_roles_rounds_avx512); a block that needs more leaves it.
#define BUSTER_PARSE_ROLE_LOOP_ROUNDS 1

typedef enum ParseRuleClass
{
    PARSE_RULE_CLASS_FIXED,
    PARSE_RULE_CLASS_STRING,
    PARSE_RULE_CLASS_IDENTIFIER,
    PARSE_RULE_CLASS_TYPE_NAME,
    PARSE_RULE_CLASS_QUALIFIER,
    PARSE_RULE_CLASS_STORAGE,
    PARSE_RULE_CLASS_GOTO,
    PARSE_RULE_CLASS_ATOMIC,
    PARSE_RULE_CLASS_ATTRIBUTE,
    PARSE_RULE_CLASS_EXTENSION,
    PARSE_RULE_CLASS_COMMA,
    PARSE_RULE_CLASS_COLON,
    PARSE_RULE_CLASS_ELLIPSIS,
    PARSE_RULE_CLASS_UNARY,
    PARSE_RULE_CLASS_PAREN,
    PARSE_RULE_CLASS_BRACE,
    PARSE_RULE_CLASS_CLOSER,
} ParseRuleClass;

// What `(` is after a kind: the opener's role, or one of these to be decided by what came before the keyword.
#define BUSTER_PARSE_PAREN_ATTRIBUTE 0xFE
#define BUSTER_PARSE_PAREN_ASM 0xFD

// Bits of ParseRoleTables.vkind_bits.
#define BUSTER_PARSE_VKIND_STARTS_TYPE 0x01
#define BUSTER_PARSE_VKIND_STARTS_DECLARATION 0x02
#define BUSTER_PARSE_VKIND_TYPE_WORD 0x04
#define BUSTER_PARSE_VKIND_STARTS_OPERAND 0x08
#define BUSTER_PARSE_VKIND_NAME 0x10
#define BUSTER_PARSE_VKIND_KEYWORD 0x20
#define BUSTER_PARSE_VKIND_ASM_QUALIFIER 0x40

// Bits of ParseRoleTables.group_rules, by a group's role: what its commas, colons and ellipses are (the low nibble,
// which ParseRoleTables.separators reads), whether its keywords are an attribute's names, whether its closer decides a
// cast, and whether it is a block, for the statement contexts.
#define BUSTER_PARSE_GROUP_RULE_COMMA 0x01
#define BUSTER_PARSE_GROUP_RULE_ASM_COLON 0x02
#define BUSTER_PARSE_GROUP_RULE_BITFIELD 0x04
#define BUSTER_PARSE_GROUP_RULE_PARAMETERS 0x08
#define BUSTER_PARSE_GROUP_RULE_NAMES 0x10
#define BUSTER_PARSE_GROUP_RULE_CAST_CLOSER 0x20
#define BUSTER_PARSE_GROUP_RULE_BLOCK 0x40

// The roles whose identity, beyond their pred class, a round reads from the token before (or from a group's opener),
// in the high bits of ParseRoleTables.features; and the features of those roles, which no other role has.
#define BUSTER_PARSE_FEATURE_ATTRIBUTE_RIGHT 0x10
#define BUSTER_PARSE_FEATURE_LEAF_RIGHT 0x20
#define BUSTER_PARSE_FEATURE_HEAD_CLOSER 0x30
#define BUSTER_PARSE_FEATURE_CALL_CLOSER 0x40
#define BUSTER_PARSE_FEATURE_COMPOUND_CLOSER 0x50
#define BUSTER_PARSE_FEATURE_BLOCK 0x60
#define BUSTER_PARSE_FEATURE_INITIALIZER 0x70
#define BUSTER_PARSE_FEATURES_ATTRIBUTE_RIGHT (BUSTER_PARSE_FEATURE_ATTRIBUTE_RIGHT | PARSE_PRED_LEAF)
#define BUSTER_PARSE_FEATURES_LEAF_RIGHT (BUSTER_PARSE_FEATURE_LEAF_RIGHT | PARSE_PRED_LEAF)
#define BUSTER_PARSE_FEATURES_HEAD_CLOSER (BUSTER_PARSE_FEATURE_HEAD_CLOSER | PARSE_PRED_HEAD)
#define BUSTER_PARSE_FEATURES_CALL_CLOSER (BUSTER_PARSE_FEATURE_CALL_CLOSER | PARSE_PRED_OPERAND)
#define BUSTER_PARSE_FEATURES_COMPOUND_CLOSER (BUSTER_PARSE_FEATURE_COMPOUND_CLOSER | PARSE_PRED_OPERAND)
#define BUSTER_PARSE_FEATURES_BLOCK (BUSTER_PARSE_FEATURE_BLOCK | PARSE_PRED_ITEM)
#define BUSTER_PARSE_FEATURES_INITIALIZER (BUSTER_PARSE_FEATURE_INITIALIZER | PARSE_PRED_EXPRESSION)

// The rules of a round that read the token before a lane: bits of ParseRoleTables.dynamic, by that token's features
// (what it says), and of each lane's `rules` (which of them apply to the lane; parse_roles_block_avx512). The rules
// that hold for a lane are the bits in both.
//   - STAR: a `*` (or a qualifier of one) before: a qualifier's, an _Atomic's, an attribute keyword's QUALIFIER_LEFT;
//   - LEAF_RIGHT: a LEAF_RIGHT before (an asm's qualifier): a qualifier's, a storage class's, goto's LEAF_RIGHT, a `(`'s
//     OPEN_ASM;
//   - OPERAND: an operand before: a binary operator's role, a call's `(`;
//   - ATTRIBUTE_RIGHT: an attribute's right closer before: a name's tag role;
//   - ITEM_OR_HEAD, ITEM_OR_PARAMETER and SPECIFIER: where a typedef name is a label's or a specifier;
//   - FUNCTION: a type, a specifier or a `*` before: a function's `(`.
#define BUSTER_PARSE_RULE_STAR 0x01
#define BUSTER_PARSE_RULE_LEAF_RIGHT 0x02
#define BUSTER_PARSE_RULE_OPERAND 0x04
#define BUSTER_PARSE_RULE_ATTRIBUTE_RIGHT 0x08
#define BUSTER_PARSE_RULE_ITEM_OR_HEAD 0x10
#define BUSTER_PARSE_RULE_ITEM_OR_PARAMETER 0x20
#define BUSTER_PARSE_RULE_SPECIFIER 0x40
#define BUSTER_PARSE_RULE_FUNCTION 0x80

// Bits of ParseRoleTables.self_bits, what a round reads of a lane's own kind: the classes whose rules read the token
// before, the classes the end of a round tests, a closer, a keyword (an attribute's name), and a kind with no role.
#define BUSTER_PARSE_SELF_READS_PREVIOUS 0x01
#define BUSTER_PARSE_SELF_IDENTIFIER 0x02
#define BUSTER_PARSE_SELF_PAREN 0x04
#define BUSTER_PARSE_SELF_TYPE_NAME 0x08
#define BUSTER_PARSE_SELF_BRACE 0x10
#define BUSTER_PARSE_SELF_CLOSER 0x20
#define BUSTER_PARSE_SELF_KEYWORD 0x40
#define BUSTER_PARSE_SELF_FLAGGED 0x80

// A lane's `facts` (parse_roles_block_avx512): for a `{` the token before decides, these bits (its kind's from
// ParseRoleTables.brace_previous, and its tag's keyword), which with its code (BUSTER_PARSE_BRACE_CODE_*) index
// ParseRoleTables.brace_rules; for a comma, a colon or an ellipsis, and an attribute's or an asm's `(`, its code, whose
// bits 4 and 5 and a nibble of what it reads index ParseRoleTables.separators and paren_by_before.
#define BUSTER_PARSE_BRACE_RULED 0x01
#define BUSTER_PARSE_BRACE_AFTER_NAME 0x08
#define BUSTER_PARSE_BRACE_NESTED 0x10
#define BUSTER_PARSE_BRACE_TAG_ENUM 0x20
#define BUSTER_PARSE_BRACE_TAG_NONE 0x40
#define BUSTER_PARSE_CODE_SEPARATOR 0x80
#define BUSTER_PARSE_CODE_COMMA 0x80
#define BUSTER_PARSE_CODE_COLON 0x90
#define BUSTER_PARSE_CODE_ELLIPSIS 0xA0
#define BUSTER_PARSE_CODE_PAREN 0x40
#define BUSTER_PARSE_CODE_ATTRIBUTE 0x40
#define BUSTER_PARSE_CODE_ASM 0x50

// What the token before a `{` says to the `{`'s rules (ParseRoleTables.brace_codes, by its features).
#define BUSTER_PARSE_BRACE_CODE_OTHER 0
#define BUSTER_PARSE_BRACE_CODE_ITEM 1
#define BUSTER_PARSE_BRACE_CODE_ATTRIBUTE_RIGHT 2
#define BUSTER_PARSE_BRACE_CODE_LEAF_RIGHT 3
#define BUSTER_PARSE_BRACE_CODE_HEAD 4
#define BUSTER_PARSE_BRACE_CODE_CALL 5
#define BUSTER_PARSE_BRACE_CODE_COMPOUND 6

typedef struct ParseRoleTables ParseRoleTables;
struct ParseRoleTables
{
    // By virtual kind. guesses: each lane's role before the rules that read roles, the most frequent on sqlite3.c and
    // the libc corpus; it changes how many rounds a block takes, never their result. guess_features: the guess's
    // features. fix_roles: the role whose pred class the kind fixes (parse_features8), else 0xFF.
    uint8_t guesses[128];
    uint8_t fixed[128];
    uint8_t fixed_flags[128];
    uint8_t classes[128];
    uint8_t binary[128];
    uint8_t paren_previous[128];
    uint8_t cast_closers[128];
    uint8_t vkind_bits[128];
    uint8_t guess_features[128];
    uint8_t self_bits[128];
    uint8_t fix_roles[128];
    uint8_t codes[128];
    uint8_t brace_previous[128];
    // By role.
    uint8_t closers[128];
    uint8_t group_rules[128];
    // What a round reads of a token's role: its pred class (before the fixes by kind) and, as BUSTER_PARSE_FEATURE_*
    // in the high bits, whether it is one of the roles the rules name.
    uint8_t features[128];
    // By features: the BUSTER_PARSE_RULE_* bits of what they say to the rules of the token after, and the
    // BUSTER_PARSE_BRACE_CODE_* they say to a `{`'s.
    uint8_t dynamic[128];
    uint8_t brace_codes[128];
    // By a `{`'s code and the BUSTER_PARSE_BRACE_* of its facts: its role, with 0x80 if it is flagged.
    uint8_t brace_rules[128];
    // By the low 6 bits of a group's features: BUSTER_PARSE_BRACE_NESTED for an initializer's.
    uint8_t nested_groups[64];
    // By a code and a nibble (BUSTER_PARSE_CODE_*): a comma's, colon's or ellipsis's role by its group's rules, and an
    // attribute's or asm's `(` by the pred class of the token before its keyword.
    uint8_t separators[64];
    uint8_t paren_by_before[64];
    // By the low nibble of a role parse_features8 fixes: its fixed features.
    uint8_t fixed_features[16];
};


// K2's stacks of an entry per depth, with BUSTER_PARSE_DEPTH_PADDING entries below depth 0: the role of the open opener
// at each depth, the typed state just before its keyword, and the kind of the token before it.
typedef struct ParseGroupStacks ParseGroupStacks;
struct ParseGroupStacks
{
    uint8_t* roles;
    uint8_t* typed;
    uint8_t* keywords;
};

typedef struct ParseRoleCarry ParseRoleCarry;
struct ParseRoleCarry
{
    // The features (parse_features8) of the two tokens before the block (PARSE_ROLE_NONE's before the start), and the
    // typed state of each of the 64 tokens before it.
    uint8_t previous_feature;
    uint8_t before_feature;
    uint64_t typed;
    // By depth: the role of the open opener at that depth, the typed state just before its keyword, and the kind of the
    // token before it.
    uint8_t* group_roles;
    uint8_t* group_typed;
    uint8_t* group_keywords;
};

// The group stacks' 64 entries below a block's starting depth (the roles of the open openers and the kinds before
// them), which the block loop (parse_roles_rounds_avx512) carries in registers from block to block: each block builds
// the next one's from its own and its open openers, so that the next block reads them without waiting for its stores
// to the stacks. Those stores overlap the window loaded after them only in part, and such a load waits until the store
// is written to the cache: with the windows loaded from the stacks, K2 took 0.45 cycles a token more on sqlite3.c and
// 0.3 more on the libc corpus.
typedef struct ParseGroupWindows ParseGroupWindows;
struct ParseGroupWindows
{
    __m512i roles;
    __m512i keywords;
};


// ---------------------------------------------------------------------------------------------------------------------
// K5 in AVX-512
//
// The twin's statement stack is a stack of levels, a block each, each holding the chain of heads that own the
// statement being read. Its parents follow from three facts, which make it data-parallel:
//   - A statement's first event is owned by the event before it in its level if that is a head: a head waits only until
//     its level's next event, and nothing else leaves one waiting (an else takes its if's place, a do waits as any head
//     does).
//   - The event before an event in its level is the event before it, except where a block that isn't a statement (a
//     statement expression, a nested function body) interrupts the level of a block around it: a statement block's
//     events end with its closer, an event of the level around it. So the owners are the events' positions compressed,
//     shifted up a lane and expanded back, with the block's last event carried to the next block. A statement block's
//     owner goes to its closer through a stack of the blocks open.
//   - The rest of what the stack decides is which if an else continues and which while ends a do, both found down the
//     chain of heads from the statement that ended before them: back through the heads (an else standing for its if)
//     to the first if or do, the first do for a while. Those events are few, and a sparse loop takes them in order,
//     with the terminators after a head's `)`, which a do-while's `while (...)` may own instead.
// Blocks that hold a non-statement block's opener or closer go through their events one at a time, with a stack of the
// levels interrupted. A do without its while, or a do-while's `)` not followed by its `;`, is for the twin: the stack's
// state there is no longer a chain of heads (parse_statements_vector then runs K4 again and the twin).

// K5's classes, a bit each, by role. The first four are events only in a statement context (in a block; K2's statement
// contexts), the others always are. HEAD is the `)` of if, while, for and switch, a case's `:` and a label's; the vector
// part turns a while's `)` (K2's statement contexts again) into WHILE, which a do-while's may be.
#define BUSTER_PARSE_STATEMENT_TERMINATOR 0x01
#define BUSTER_PARSE_STATEMENT_HEAD 0x02
#define BUSTER_PARSE_STATEMENT_WHILE 0x04
#define BUSTER_PARSE_STATEMENT_DO 0x08
#define BUSTER_PARSE_STATEMENT_ELSE 0x10
#define BUSTER_PARSE_STATEMENT_OPEN_BLOCK 0x20
#define BUSTER_PARSE_STATEMENT_OPEN_BODY 0x40
#define BUSTER_PARSE_STATEMENT_CLOSE 0x80
#define BUSTER_PARSE_STATEMENT_CONTEXTUAL \
    (BUSTER_PARSE_STATEMENT_TERMINATOR | BUSTER_PARSE_STATEMENT_HEAD | BUSTER_PARSE_STATEMENT_WHILE | BUSTER_PARSE_STATEMENT_DO)
#define BUSTER_PARSE_STATEMENT_ALWAYS \
    (BUSTER_PARSE_STATEMENT_ELSE | BUSTER_PARSE_STATEMENT_OPEN_BLOCK | BUSTER_PARSE_STATEMENT_OPEN_BODY | BUSTER_PARSE_STATEMENT_CLOSE)
// The heads, each of which waits for its level's next event; what takes a waiting head as its parent (a do's owner goes
// to its final `;`); and the ends of statements.
#define BUSTER_PARSE_STATEMENT_HEADS (BUSTER_PARSE_STATEMENT_HEAD | BUSTER_PARSE_STATEMENT_WHILE | BUSTER_PARSE_STATEMENT_DO | BUSTER_PARSE_STATEMENT_ELSE)
#define BUSTER_PARSE_STATEMENT_OWNED (BUSTER_PARSE_STATEMENT_TERMINATOR | BUSTER_PARSE_STATEMENT_HEAD | BUSTER_PARSE_STATEMENT_WHILE)
#define BUSTER_PARSE_STATEMENT_ENDS (BUSTER_PARSE_STATEMENT_TERMINATOR | BUSTER_PARSE_STATEMENT_CLOSE)


typedef enum ParseStatementsVerdict
{
    PARSE_STATEMENTS_OK,
    // The twin's false: an else without an if.
    PARSE_STATEMENTS_ELSE_WITHOUT_IF,
    // Only the twin's stack decides: a do without its while, or a do-while's `)` not followed by its `;`.
    PARSE_STATEMENTS_TWIN,
} ParseStatementsVerdict;

// A level that a block which isn't a statement interrupts: its last event and that event's classes, and the closer
// that ends the interruption.
typedef struct ParseStatementLevel ParseStatementLevel;
struct ParseStatementLevel
{
    uint32_t last;
    uint32_t closer;
    uint8_t last_classes;
};

typedef struct ParseStatementPass ParseStatementPass;
struct ParseStatementPass
{
    // The last event of the current level, and its classes.
    uint32_t last;
    ParseStatementLevel* levels;
    uint32_t level_count;
    // How many blocks (OPEN_BLOCK and OPEN_BODY) are open, and for each, by depth, the owner of its statement: the head
    // before a statement block's opener, else BUSTER_PARSE_NONE.
    uint32_t block_depth;
    uint32_t* owners;
    // Where the branch-free stores a lane doesn't make go: the owners' last slot, which no depth reaches.
    uint32_t* sink;
    // The dos whose while hasn't come, and a do-while's `)` whose `;` comes next, with its do.
    uint32_t open_dos;
    uint32_t pending_while;
    uint32_t pending_do;
    // The sparse loop runs a block behind (parse_statement_block): the last block with events, its sparse events, and
    // whether a do is among them.
    uint32_t sparse_block;
    uint64_t sparse;
    bool sparse_dos;
    uint8_t last_classes;
    bool else_without_if;
    bool twin;
};

// What K5 in AVX-512 did: the blocks with events, those it took one event at a time, the events its sparse loop took,
// and the files the twin did.
typedef struct ParseStatementCounts ParseStatementCounts;
struct ParseStatementCounts
{
    uint64_t blocks;
    uint64_t interrupted_blocks;
    uint64_t sparse;
    uint64_t twins;
};


// ---------------------------------------------------------------------------------------------------------------------
// K1 in 16-bit lanes, for the comparison with byte lanes

// ---------------------------------------------------------------------------------------------------------------------
// K4 in byte lanes
//
// 64 tokens a block, keys (node depth - the block's lowest) * 32 + level in bytes: a block whose node depths span at most
// BUSTER_PARSE_BYTE_DEPTH_RANGE fits (an opener's right threshold is one depth further up). Wider blocks run as two
// 16-bit blocks. The candidate stacks keep 16-bit keys, so both kinds of block read and write them alike.

#define BUSTER_PARSE_BYTE_DEPTH_RANGE 6


// The block's keys and thresholds in bytes.
typedef struct ParseKeys8 ParseKeys8;
struct ParseKeys8
{
    __m512i keys;
    __m512i left;
    __m512i right;
    __m512i rules;
};


// ---------------------------------------------------------------------------------------------------------------------
// K4 with the stack's top in registers
//
// In the byte-lane passes above, a block reads the top of the candidate stack from memory that the block before it has
// just written at another offset, so the load can't take the stores' data and waits for them to commit; and a block's
// in-block work, about 140 cycles of latency in some 450 ops, hardly overlaps the next block's. Not for want of reorder
// buffer: Zen 5 keeps waiting vector ops in its floating-point scheduling queues, which one block's chains fill (half the
// cycles of a loop of that work alone stall on them: de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall), so
// the next block's ops reach them only when they come close in program order. So K4 runs as three loops:
//   - parse_top_prepare8_avx512: every block's in-block work for both passes, which depends on no other block (the
//     searches by parse_previous_smaller8's and parse_next_smaller8's tables, both passes' candidates for other blocks,
//     the least key), BUSTER_PARSE_PREPARED_TOGETHER blocks at a time, each step for all of them before the next.
//   - The left pass and the right pass: each block's window work on it. The top of the stack lives in registers
//     (ParseTop): its BUSTER_PARSE_TOP_LANES nearest candidates, nearest first. A block looks there for the candidates
//     its lanes didn't find in it, pops the ones its least key hides with a compress and pushes its own with an expand,
//     so that what one block hands the next is a compare, a compress and an expand on registers.
//
// The whole stack is in memory too (ParseSlots), nearest first and growing downward, written by each push with a
// masked store. A lane whose candidate is deeper than the window searches it there, and a window that pops leave with
// fewer candidates than the stack holds is reloaded from it. Neither happens on sqlite3 or the libc corpus, whose
// stacks hold at most 32 candidates before a block; deep nesting does both.
//
// The left pass's candidates for later blocks are a block's suffix minima, the right pass's its prefix minima, each
// pushed nearest first. A block whose depths span more than BUSTER_PARSE_BYTE_DEPTH_RANGE runs as aligned parts that
// fit, in its pass's order, each with its own in-block work.

#define BUSTER_PARSE_TOP_LANES 32

typedef struct ParseTop ParseTop;
struct ParseTop
{
    // The nearest candidates, nearest first: 16-bit keys (0 below the stack's bottom, below every real key) and the
    // positions' low and high halves (0xFFFF below the bottom, which reads as BUSTER_PARSE_NONE).
    __m512i keys;
    __m512i low;
    __m512i high;
    // The candidates in the stack, and how many of them the window holds: the lesser of `count` and the window's
    // lanes, between blocks.
    uint32_t count;
    uint32_t held;
};

// The stack in memory, nearest first: candidate s (0 at the bottom) at slot capacity - 1 - s, so the window's
// candidates are the slots from capacity - count. BUSTER_PARSE_TOP_LANES slots after the last hold key 0 and position
// halves 0xFFFF, as the window does below the bottom.
typedef struct ParseSlots ParseSlots;
struct ParseSlots
{
    uint16_t* keys;
    uint16_t* low;
    uint16_t* high;
    uint32_t capacity;
};


// A block's candidates, each lane's nearest below its threshold, as 16-bit keys (32 lanes a register) and positions
// (16 lanes a register).
typedef struct ParseFound8 ParseFound8;
struct ParseFound8
{
    __m512i keys[2];
    __m512i positions[4];
};


// A block's in-block work for both passes, which depends on no other block, so that every block's runs first, many
// blocks in flight at once, and the passes are left with the window work, whose chain from block to block is short.
typedef struct ParseBlockWork ParseBlockWork;
struct ParseBlockWork
{
    // The keys relative to the block's lowest node depth * 32 (dead lanes 0xFF), and the searches' thresholds.
    uint8_t keys[64];
    uint8_t left_thresholds[64];
    uint8_t right_thresholds[64];
    // Each lane's L and R in the block: a lane, or 64 or more for none.
    uint8_t lefts[64];
    uint8_t rights[64];
    // The block's candidates for each pass's later blocks: its suffix minima, by lane from the block's end (the left
    // pass pushes the nearest first), and its prefix minima.
    uint64_t left_minima;
    uint64_t right_minima;
    // The lowest node depth, or BUSTER_PARSE_NONE for a block whose depths span more than BUSTER_PARSE_BYTE_DEPTH_RANGE,
    // which the passes run as parts; and the block's least key.
    uint32_t base;
    uint8_t minimum;
} __attribute__((aligned(64)));

// Blocks prepared together, each step for all of them before the next, so that their chains share the scheduling
// queues. Four were faster on the libc corpus's small files and slower on sqlite3, whose blocks' work streams through
// L3.
#define BUSTER_PARSE_PREPARED_TOGETHER 2

_Static_assert(64 * BUSTER_PARSE_PREPARED_TOGETHER <= BUSTER_PARSE_TOKEN_PADDING, "the last group of prepared blocks reads the token arrays' padding");


// ---------------------------------------------------------------------------------------------------------------------
// K6 in AVX-512
//
// 64 tokens a block in byte lanes, on the twin's frames and carry: a block the kernel can't take goes to the twin
// (parse_check_range) on the same frames and carry, as do the rare tokens it leaves to scalar code (a keyword's class
// by its name, _Alignas's closer, statement expressions, attributes after a tag's body). Each lane's node depth, less
// the block's least, plus one, is its level (1 to 64; a block whose depths span more goes to the twin). One key per lane,
// 4 * (level - 1) plus a rank (0 for a token that starts an item, 1 for an `=`, `,` or bit-field `:`, 2 for any
// other), and one table of window minima give each lane, by three searches, the nearest token before it in the block:
//   - at a lower level (E): the opener of its group, whose group's type is the lane's group's;
//   - at its level that starts an item (S1, or the lane itself if it starts one): its item's start, whose class says
//     what the item is and, with the start before it, the item's place in its group;
//   - at its level that starts an item or switches a declaration's part (S2): its part.
// Where a search finds nothing in the block, the lane's level's frame has what it would have said. A group's type
// comes from its opener's class and mode, and the mode from the opener's own group's type: the openers in a block take
// rounds to their fixed point, from what each opener would open in an expression (past BUSTER_PARSE_CHECK_ROUNDS, the
// twin takes the block). Then
// the checks, by table: the pairs, the modes and the ends with the classes compressed into kinds (the classes the
// tables treat alike), so that each two-dimensional table is 64 by 64 bits; and last, the frames of the depths still
// open after the block, from the last lane at each.

#define BUSTER_PARSE_CHECK_ROUNDS 16

// Bits of ParseCheckVectorTables.class_bits, by class.
#define BUSTER_PARSE_CHECK_BEFORE_NAME 0x01
#define BUSTER_PARSE_CHECK_AFTER_NAME 0x02
#define BUSTER_PARSE_CHECK_SPECIFIER_END 0x04
#define BUSTER_PARSE_CHECK_OPERAND_START 0x08
#define BUSTER_PARSE_CHECK_TRANSPARENT 0x10
#define BUSTER_PARSE_CHECK_BODY_CLOSER 0x20
#define BUSTER_PARSE_CHECK_SWITCH 0x40
#define BUSTER_PARSE_CHECK_POSTFIX 0x80

// Bits of ParseCheckVectorTables.role_bits, by role: what refines its class (the low 3 bits: 1 OPERAND, 2 SPECIFIER,
// 3 PREFIX, 4 LEAF_RIGHT, 5 QUALIFIER_LEFT, 6 RETURN, 7 OPEN_CASE), whether it opens or closes a group, whether an
// item starts after it (its pred is an item's or a head's), and whether its class may depend on the token's name.
#define BUSTER_PARSE_CHECK_REFINE 0x07
#define BUSTER_PARSE_CHECK_OPENER 0x08
#define BUSTER_PARSE_CHECK_CLOSER 0x10
#define BUSTER_PARSE_CHECK_ITEM_AFTER 0x20
#define BUSTER_PARSE_CHECK_RARE 0x40

// Bits of ParseCheckVectorTables.mode_bits, by mode.
#define BUSTER_PARSE_CHECK_NAMED 0x01
#define BUSTER_PARSE_CHECK_DECLARATION 0x02
#define BUSTER_PARSE_CHECK_IN_EXPRESSION 0x04

// Bits of ParseCheckVectorTables.after_tag, by virtual kind: what may follow a tag's body always, in a struct (`:`),
// or unless a type specifier follows (a qualifier or storage class); and whether the kind is a type specifier's.
#define BUSTER_PARSE_CHECK_TAG_ALWAYS 0x01
#define BUSTER_PARSE_CHECK_TAG_COLON 0x02
#define BUSTER_PARSE_CHECK_TAG_UNLESS_TYPE 0x04
#define BUSTER_PARSE_CHECK_TAG_TYPE 0x08

typedef struct ParseCheckVectorTables ParseCheckVectorTables;
struct ParseCheckVectorTables
{
    // By class: its left kind (its row of follows) and its kinds in allows and in ends (its columns there); and by left
    // kind * 16 + class / 8, follows' bits, and by mode * 8 + kind / 8, allows' and ends'.
    uint8_t left[128] __attribute__((aligned(64)));
    uint8_t allows_kind[128] __attribute__((aligned(64)));
    uint8_t ends_kind[128] __attribute__((aligned(64)));
    uint8_t follows[1024] __attribute__((aligned(64)));
    uint8_t allows[512] __attribute__((aligned(64)));
    uint8_t ends[512] __attribute__((aligned(64)));
    uint8_t class_bits[128] __attribute__((aligned(64)));
    uint8_t mode_bits[64] __attribute__((aligned(64)));
    // A virtual kind selects an eight-byte row of the seven role refinements (index 0 unused). Kinds with the same
    // row share it, so one joint lookup replaces seven lookups and blends. refine_index holds the byte offset.
    uint8_t refine_index[128] __attribute__((aligned(64)));
    uint8_t refine[256] __attribute__((aligned(64)));
    uint8_t typed[128] __attribute__((aligned(64)));
    uint8_t role_bits[128] __attribute__((aligned(64)));
    uint8_t closer_kind[128] __attribute__((aligned(64)));
    // By class: what an item it starts is (in a group other than an enum or an initializer list), whether it starts a
    // designator, the part after it in a declaration, and the group an opener of it opens (by its class alone); by the
    // class before an attribute's, a specifier's or a keyword's group, that group; and by (opener variant: 1 a call's,
    // 2 a group's, 3 a subscript's, else 0) * 64 + mode, the group.
    uint8_t item[128] __attribute__((aligned(64)));
    uint8_t designator[128] __attribute__((aligned(64)));
    uint8_t part_after[128] __attribute__((aligned(64)));
    uint8_t opener_group[128] __attribute__((aligned(64)));
    uint8_t keyword_group[128] __attribute__((aligned(64)));
    uint8_t variant[128] __attribute__((aligned(64)));
    uint8_t mode_group[256] __attribute__((aligned(64)));
    // By group * 4 + the item's place (0 the first, 1 the second, 2 a later one): the pseudo-group; and by pseudo-group *
    // 6 + state (0 an item's start, 1 another item's, 2 an assertion's, 3 an initializer's, 4 a width's, 5 a
    // declaration's), the mode; by kind * 8 + part, the state of a token that doesn't start its item.
    uint8_t pseudo[128] __attribute__((aligned(64)));
    uint8_t modes[256] __attribute__((aligned(64)));
    uint8_t states[64] __attribute__((aligned(64)));
    uint8_t after_tag[128] __attribute__((aligned(64)));
    // A bit per position in a byte, for the two-dimensional tables' tests.
    uint8_t bit[64] __attribute__((aligned(64)));
    uint32_t left_kinds;
    uint32_t allows_kinds;
    uint32_t ends_kinds;
    uint32_t pseudo_groups;
};


// What K6's vector kernel works out for a block, kept for its scalar parts.
typedef struct ParseCheckBlock ParseCheckBlock;
struct ParseCheckBlock
{
    uint8_t classes[64];
    uint8_t groups[64];
    uint8_t kinds[64];
    uint8_t parts[64];
    uint8_t items[64];
    uint8_t modes[64];
    uint8_t opened[64];
    uint8_t levels[64];
    uint8_t starts[64];
} __attribute__((aligned(64)));


// ---------------------------------------------------------------------------------------------------------------------
// The vector path together

// What the vector kernels need besides ParseKernels' arrays.
typedef struct ParseVectorBuffers ParseVectorBuffers;
struct ParseVectorBuffers
{
    // By depth: K1's open openers, and K2's group stacks.
    uint32_t* open_stack;
    ParseGroupStacks groups;
    // By token: K2's typed states, K4's L and its key.
    uint8_t* typed;
    uint32_t* lefts;
    uint16_t* left_keys;
    // K4 with the stack's top in registers: each block's in-block work, and the stack's slots.
    ParseBlockWork* works;
    ParseSlots slots;
    uint32_t* secondary;
    ParseStatement* statements;
    // K2's statement contexts, per 64 tokens, and for K5 each event's previous event in its level, the owners of the
    // open blocks and the levels interrupted.
    ParseStatementContext* contexts;
    uint32_t* statement_previous;
    uint32_t* statement_owners;
    ParseStatementLevel* statement_levels;
    // Only the kernel benchmark uses these traversal buffers. Production tree-only parsing leaves them null;
    // clients requesting traversal columns build them after popping the vector workspace (#628).
    uint32_t* last_children;
    uint32_t* before;
    uint32_t* postorder;
    uint32_t* positions;
    uint32_t* subtree_ends;
    uint32_t* first_children;
    uint32_t* next_siblings;
    uint32_t* subtree_starts;
    uint32_t* sizes;
    // K2's blocks by the rounds they took (index 0: the twin did them), and K4's blocks whose depths span more than
    // byte lanes hold, which run as two 16-bit blocks.
    uint64_t rounds[BUSTER_PARSE_ROLE_ROUNDS + 1];
    uint64_t wide_blocks;
    ParseStatementCounts statement_counts;
    // K6's blocks that its twin took.
    uint64_t check_twins;
};

// The by-depth stacks have BUSTER_PARSE_DEPTH_PADDING entries below depth 0: no opener, role NONE, not typed. K2 reads
// 128 below a block's starting depth.
#define BUSTER_PARSE_DEPTH_PADDING 128


// ---------------------------------------------------------------------------------------------------------------------
// The postorder, from the parents
//
// Every subtree is a run of tokens: a token's L and R bound its subtree, as in a Cartesian tree, and a statement's
// head owns the statement that follows it. So a token with no child after it ends its subtree, and the subtrees that
// end with it are those of the ancestors whose last child, after them, leads down to it. The postorder emits each such
// token in order, then climbs while it is its parent's last child and comes after the parent. Children come in source
// order, each child's subtree before the next. The benchmark's check compares the result with a depth-first walk.

static uint32_t parse_postorder(const uint32_t* parents, uint32_t count, uint32_t* last_children, uint32_t* postorder)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        last_children[i] = BUSTER_PARSE_NONE;
    }
    // In order, so that each token's last write is its last child.
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t parent = parents[i];
        if (parent != BUSTER_PARSE_NONE)
        {
            last_children[parent] = i;
        }
    }
    uint32_t written = 0;
    for (uint32_t t = 0; t < count; t += 1)
    {
        // No child after t: none at all (BUSTER_PARSE_NONE + 1 wraps to 0), or the last one before it.
        if (last_children[t] + 1 <= t)
        {
            postorder[written] = t;
            written += 1;
            uint32_t child = t;
            uint32_t parent = parents[t];
            while (parent < child && last_children[parent] == child)
            {
                postorder[written] = parent;
                written += 1;
                child = parent;
                parent = parents[parent];
            }
        }
    }
    return written;
}

// The tree's columns (#186, #544): what the checker reads of the tree besides the parents and the roles, from the
// parents, in three passes without a branch on the tree, where the postorder alone took five and the checker's reader
// two more for the rest. For each token:
//   - its subtree's end, its last token (every subtree is a run of tokens): its own, or its last child's where that
//     child comes after it;
//   - its first child and its next sibling, in token order (BUSTER_PARSE_NONE where it has none);
//   - its position in the postorder, and the postorder itself, the token at each position. In postorder a subtree's
//     tokens are a run that ends with its root, after every token whose subtree ends before it, and its other tokens
//     are those its source run holds, so the root's position is C(v) + end(v) - v, where C(v), the number of tokens
//     whose subtree ends before v, is an exclusive prefix sum of how many subtrees end at each token.
// The first pass, forward, writes each token's last child (each parent's last write); the second, backward, sees a
// token after its children after it, so that its subtree's end is its last child's, and pushes each token on its
// parent's list of children, which leaves the first child at its head and each sibling linked to the next; the third,
// forward, takes the prefix sum as it goes. `last_children` and `before` have count + 1 entries, and each column count
// + 1: the last is the sentinel the checker's reader had (its own end, no child, no sibling), and the roots' writes go
// to it during the passes. With `sizes` (else 0), each position also gets its subtree's size, end(v) - start(v) + 1,
// where start(v) is its first child's start where that child comes before it, else v, in `starts`.
//
// Data-parallel? The passes are a scatter's loads and stores: per token, two of the backward pass's loads and a
// read-modify-write land at the token's parent, its last child and its subtree's end, and each forward pass makes a
// store at its parent or its position. On Zen 5 a zmm `vpgatherdd` takes about 12.6 cycles for 16 lanes and a
// `vpscatterdd` 17 (docs/coding-style.md), against three scalar loads and two stores a cycle, and a block's writes to
// one parent conflict, since a parent's children are near it: the passes stay scalar. The third pass in AVX-512 (its
// prefix sum in registers, the positions a vector store) and the counts kept in the positions column were each slower
// on the 9700X, and so was one fused backward pass, which leaves each end at its parent through a select's address:
// docs/c-parser.md's "The tree's columns" has the numbers.
static void parse_tree_columns(const uint32_t* parents, uint32_t count, uint32_t* last_children, uint32_t* before, uint32_t* postorder, uint32_t* positions,
                               uint32_t* ends, uint32_t* first_children, uint32_t* next_siblings, uint32_t* starts, uint32_t* sizes)
{
    // 0: no child after the token, as a child at 0 comes after none.
    memset(last_children, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    memset(first_children, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    memset(before, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i += 1)
    {
        last_children[parents[i] == BUSTER_PARSE_NONE ? count : parents[i]] = i;
    }
    for (uint32_t v = count; v-- > 0;)
    {
        const uint32_t parent = parents[v];
        const uint32_t slot = parent != BUSTER_PARSE_NONE ? parent : count;
        const uint32_t sibling = first_children[slot];
        next_siblings[v] = parent != BUSTER_PARSE_NONE ? sibling : BUSTER_PARSE_NONE;
        first_children[slot] = v;
        // ends[last] is v's end when last comes after v, and was written then; otherwise it is unused. Unpredictable,
        // so that Clang selects (cmov) rather than branching around the load.
        const uint32_t last = last_children[v];
        const uint32_t end = ends[last];
        const uint32_t own = __builtin_unpredictable(last > v) ? end : v;
        ends[v] = own;
        before[own] += 1;
    }
    first_children[count] = BUSTER_PARSE_NONE;
    next_siblings[count] = BUSTER_PARSE_NONE;
    ends[count] = count;
    uint32_t sum = 0;
    if (sizes == 0)
    {
        for (uint32_t v = 0; v < count; v += 1)
        {
            const uint32_t position = sum + ends[v] - v;
            positions[v] = position;
            postorder[position] = v;
            sum += before[v];
        }
    }
    else
    {
        for (uint32_t v = 0; v < count; v += 1)
        {
            const uint32_t position = sum + ends[v] - v;
            positions[v] = position;
            postorder[position] = v;
            sum += before[v];
            // starts[v] is read only where the first child comes before v, which wrote it; v's own slot otherwise.
            const uint32_t first = first_children[v];
            const bool before_v = first < v;
            const uint32_t start = starts[before_v ? first : v];
            starts[v] = __builtin_unpredictable(before_v) ? start : v;
            sizes[position] = ends[v] - starts[v] + 1;
        }
    }
    positions[count] = count;
}

// The tree's columns but the postorder, from the parents and their postorder, as the checker's reader derived them
// before the parser published them: parse_tree_columns' twin, which test-parser compares with it, and the reader's for
// a tree that comes without them (the prefix and repair views of c_check_prefix.c and c_check_repair.c). Each token's
// children are linked from the last back to the first, then each token's position is read off the postorder, which
// also gives each parent after its children, so that its end is the greatest of its own and theirs.
static void parse_tree_links(const uint32_t* parents, const uint32_t* postorder, uint32_t count, uint32_t* positions, uint32_t* ends, uint32_t* first_children,
                             uint32_t* next_siblings)
{
    for (uint32_t i = 0; i <= count; i += 1)
    {
        first_children[i] = BUSTER_PARSE_NONE;
        next_siblings[i] = BUSTER_PARSE_NONE;
        ends[i] = i;
    }
    for (uint32_t i = count; i-- > 0;)
    {
        const uint32_t parent = parents[i];
        if (parent < count)
        {
            next_siblings[i] = first_children[parent];
            first_children[parent] = i;
        }
    }
    for (uint32_t k = 0; k < count; k += 1)
    {
        const uint32_t token = postorder[k];
        positions[token] = k;
        const uint32_t parent = parents[token];
        if (parent < count && ends[parent] < ends[token])
        {
            ends[parent] = ends[token];
        }
    }
    positions[count] = count;
}

// ---------------------------------------------------------------------------------------------------------------------
// The entry point

#include "buster/compiler/c_gnu.c"

typedef struct ParseOptions ParseOptions;
struct ParseOptions
{
    // The scalar parser alone, for test-parser's differential checks.
    bool scalar;
    // Build the syntax tree and diagnostics without the postorder and the tree's columns that later stages consume.
    bool tree_only;
    // Also each postorder position's subtree size (Parser.sizes), which test-parser checks. A checker reader borrows
    // this optional column for complete-subtree queries or derives its own when the parse does not retain it.
    bool sizes;
    // Retain bounded GNU attribute and asm-label records for a semantic consumer; syntax-only callers leave it off.
    bool extension_metadata;
    // Where a parse's temporaries go (the vector path's arrays, the scalar parser's stacks and the postorder's passes),
    // which it pops at its end: the caller's arena, whose pages its next stage then takes again (the C driver's checker
    // reads its scratch from where the parse's were), or null for the parser's own.
    Arena* scratch;
};

// What a parse did, for benchmark-parser and test-parser.
typedef struct ParseStatistics ParseStatistics;
struct ParseStatistics
{
    // Top-level declarations the scalar parser took on the vector path, and their tokens.
    uint64_t fallback_items;
    uint64_t fallback_tokens;
    // Tokens the vector path flagged: none where the whole unit went to the scalar parser before K6.
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
    // Beside each token, a name a diagnostic's argument there is spelled by instead of its source, or 0; or NULL for none:
    // a statement start's typo corrected to a keyword or a type name (#1447, check_driver_typo_errors).
    const uint32_t* respelled;
    // Borrowed through checking and emission, like the tokens' source indices. Resource bytes stay owned by pp.
    const Preprocessor* preprocessor;
    // Beside each token of a view that leaves a parse's tokens out (#2531's step 3, c_check_attested.c's
    // check_attested_view), its index in that parse, which names the objects a token numbers (parse_origin); or NULL
    // where the indices are the parse's own.
    const uint32_t* origins;
    ParseTokens tokens;
    ParseTree tree;
    // The tree in postorder, for a unit without errors: the token at each position, and with ParseOptions.sizes its
    // subtree's size (its subtree is the `sizes[k]` positions ending at k).
    uint32_t* postorder;
    uint32_t* sizes;
    // The tree's columns, with the postorder (parse_tree_columns), each count + 1 entries long, the last a sentinel:
    // each token's position in the postorder, its subtree's last token, its first child and its next sibling
    // (BUSTER_PARSE_NONE where it has none). The checker's reader reads them in place of deriving them.
    uint32_t* positions;
    uint32_t* ends;
    uint32_t* first_children;
    uint32_t* next_siblings;
    ParseMeanings meanings;
    ParseDiagnostic* diagnostics;
    uint32_t diagnostic_count;
    uint32_t diagnostic_capacity;
    // The diagnostics' and the meanings' columns in the tokens' arena (parse_translation_unit), and the meanings it has
    // room for.
    ParseDiagnostic* diagnostic_column;
    uint8_t* meaning_column;
    uint32_t meaning_room;
    uint32_t errors;
    // A checker's view of a failed parse that covers the whole unit, every error of which its recovery read past
    // (check_prefix_resume): its reader recovers from missing names as a complete unit's does (#2255).
    bool whole_view;
    // A checker's view of the complete declarations before a failed parse's error (check_prefix_make, #1799): its reader
    // recovers from missing names as a whole unit's does. C declares a name before its uses, so a name the prefix doesn't
    // declare where it is used the whole unit doesn't either, and the declarations past the prefix are no part of it.
    bool prefix_view;
    // A checked diagnostic prefix includes already diagnosed parameter types. This enables typo searches for
    // their stand-in int specifiers; ordinary parses and emission never enable it.
    bool recovered_parameter_types;
    // Private diagnostic-view specifiers recovered from already diagnosed unknown non-parameter types. Their
    // declarations remain invalid, so initializer conversions and later uses cannot treat the stand-in int as real.
    const uint8_t* recovered_types;
    // What stopped the parse where no diagnostic says it: PARSE_MESSAGE_OUT_OF_MEMORY or PARSE_MESSAGE_TOO_MANY_TOKENS,
    // or PARSE_MESSAGE_NONE.
    ParseMessage fatal;
    ParseStatistics statistics;
    GnuSyntax gnu;
    Arena gnu_arena;
    // The token columns and the tree's, whose first pages are each base pages (lane.c): those a parse reserves together
    // are the tables of one arena, the tokens' in the first, the tree's in the sixth and the postorder's in the twelfth
    // (parse_columns, #1128), and a column reserved alone has an arena of its own (parse_column). Then a parse's
    // temporaries without ParseOptions.scratch.
    Arena columns[17];
    Arena scratch;
    // The vector path's arrays (ParseKernels and ParseVectorBuffers): the tree's in its columns, the rest a parse's
    // temporaries, which only the parse reads; and clang's predeclared typedef names' IdentIds.
    ParseKernels kernels;
    ParseVectorBuffers buffers;
    IdentId predeclared[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
};

// Token `token`'s index in the parse `parser` was made from (Parser.origins): its own, but in a view that leaves tokens
// out. The local labels numbered by a token (a string literal's, .Lbuster_string_N) take it, so that a view's object is
// the parse's to the byte.
static inline __attribute__((unused)) uint32_t parse_origin(const Parser* parser, uint32_t token)
{
    return parser->origins != NULL ? parser->origins[token] : token;
}

// A compact #embed operand's immutable bytes. Synthetic parsers have no preprocessor and therefore no resources.
static __attribute__((unused)) const uint8_t* parse_embed_bytes(const Parser* parser, uint32_t token, uint32_t* length)
{
    const Preprocessor* pp = parser->preprocessor;
    const uint8_t* result = NULL;
    *length = 0;
    if (pp != NULL && pp->embed_count != 0 && token < parser->tokens.count && parser->tokens.sources != NULL)
    {
        const uint32_t source = parser->tokens.sources[token];
        if (source < pp->output.count && pp->output.kinds[source] == BUSTER_PP_KIND_EMBED && pp->output.values[source] < pp->embed_count)
        {
            const PPEmbed* embed = pp->embeds + pp->output.values[source];
            if (embed->data < pp->data_count && embed->offset <= pp->datas[embed->data].length &&
                embed->length != 0 && embed->length <= pp->datas[embed->data].length - embed->offset)
            {
                result = pp->datas[embed->data].bytes + embed->offset;
                *length = embed->length;
            }
        }
    }
    return result;
}


static void parse_release(Parser* parser)
{
    for (uint32_t i = 0; i < sizeof(parser->columns) / sizeof(parser->columns[0]); i += 1)
    {
        arena_release(&parser->columns[i]);
    }
    arena_release(&parser->scratch);
    arena_release(&parser->gnu_arena);
    *parser = (Parser){ 0 };
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
        // The end of its spelling, read again from its bytes: a line splice or a UCN makes it longer than the name's
        // interned bytes.
        const uint32_t range = pp_find_range(pp, location);
        const PPRange* bytes = range != BUSTER_PP_NONE ? &pp->ranges[range] : 0;
        result = bytes != 0 ? bytes->base + pp_identifier_end(pp, bytes->bytes, bytes->length, location - bytes->base) :
                              location + intern_length(pp->table, pp->output.values[s]);
    }
    else if (kind == C_TOKEN_COLONCOLON)
    {
        // The first half of a split `::` ends at the second.
        result = location + 1 + (t + 1 < parser->tokens.count && parser->tokens.sources[t + 1] == s ? 0 : 1);
    }
    return result;
}

// Where a diagnostic points: the location of its token, or of the end of the token before it.
static uint32_t parse_primary_location(const Preprocessor* pp, uint32_t source)
{
    uint32_t location = source < pp->output.count ? pp->output.locations[source] : 0;
    uint32_t origin = pp_history_at(pp, source);
    const uint64_t maximum = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    for (uint64_t step = 0; origin != 0 && step < maximum; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT && index < pp->substitution_count)
        {
            origin = pp->substitutions[index].source;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED && index < pp->generated_count)
        {
            location = pp->generated[index].anchor;
            origin = pp->generated[index].expansion;
        }
        else if (kind == 0 && index < pp->history_count)
        {
            location = pp->history[index].begin;
            origin = pp->history[index].parent;
        }
        else
        {
            origin = 0;
        }
    }
    return location;
}

// A missing separator after an expanded token belongs after the invocation, including its closing parenthesis.
// The spelling helper stays separate: diagnostic arguments still quote the token's actual source bytes.
static uint32_t parse_expansion_context(const Preprocessor* pp, uint32_t source)
{
    uint32_t result = 0;
    uint32_t origin = pp_history_at(pp, source);
    const uint64_t maximum = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    for (uint64_t step = 0; origin != 0 && step < maximum; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT && index < pp->substitution_count)
        {
            origin = pp->substitutions[index].expansion;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED && index < pp->generated_count)
        {
            origin = pp->generated[index].expansion;
        }
        else if (kind == 0 && index < pp->history_count)
        {
            result = index + 1;
            origin = pp->history[index].parent;
        }
        else
        {
            origin = 0;
        }
    }
    return result;
}

static uint32_t parse_primary_end(const Parser* parser, const Preprocessor* pp, uint32_t token)
{
    const uint32_t source = parser->tokens.sources[token];
    uint32_t result = parse_token_end(parser, pp, token);
    uint32_t origin = pp_history_at(pp, source);
    uint32_t location = BUSTER_PP_NONE;
    const uint64_t maximum = (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count;
    for (uint64_t step = 0; origin != 0 && step < maximum; step += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT && index < pp->substitution_count)
        {
            origin = pp->substitutions[index].expansion;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED && index < pp->generated_count)
        {
            origin = pp->generated[index].expansion;
        }
        else if (kind == 0 && index < pp->history_count)
        {
            location = pp->history[index].end;
            origin = pp->history[index].end_origin;
        }
        else
        {
            origin = 0;
        }
    }
    const uint32_t range = location != BUSTER_PP_NONE ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    if (range != BUSTER_PP_NONE)
    {
        const PPRange* bytes = &pp->ranges[range];
        const uint32_t offset = location - bytes->base;
        result = bytes->bytes[offset] == ')' ? location + 1 : bytes->base + pp_identifier_end(pp, bytes->bytes, bytes->length, offset);
    }
    return result;
}

static uint32_t parse_diagnostic_location(const Parser* parser, const Preprocessor* pp, const ParseDiagnostic* diagnostic)
{
    const uint32_t t = diagnostic->token;
    uint32_t result;
    if (diagnostic->place == BUSTER_PARSE_AFTER_PREVIOUS && t > 0 && t - 1 < parser->tokens.count)
    {
        const uint32_t previous = parse_expansion_context(pp, parser->tokens.sources[t - 1]);
        const bool inside = previous != 0 && t < parser->tokens.count && previous == parse_expansion_context(pp, parser->tokens.sources[t]);
        result = inside ? parse_primary_location(pp, parser->tokens.sources[t]) : parse_primary_end(parser, pp, t - 1);
    }
    else if (t < parser->tokens.count)
    {
        result = parse_primary_location(pp, parser->tokens.sources[t]);
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
    const uint32_t respelled = parser->respelled != NULL && diagnostic->argument < parser->tokens.count ? parser->respelled[diagnostic->argument] : 0;
    if (respelled != 0)
    {
        const uint32_t length = intern_length(pp->table, respelled);
        snprintf(spelling, sizeof(spelling), "%.*s", (int)length, intern_bytes(pp->table, respelled));
    }
    else if (diagnostic->argument != BUSTER_PARSE_NONE && diagnostic->argument < parser->tokens.count)
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

// The entry points share one implementation in main.c's frontend unit.
BUSTER_PARSER_ENTRY ParseScalarResult parse_scalar_traced(const uint8_t* kinds, const uint32_t* names, const uint8_t* flags, uint32_t count, CLanguage language, ParseMeanings* meanings,
                                             ParseTree* tree, Arena* scratch, ParseDiagnostic* diagnostics, uint32_t capacity, uint32_t* diagnostic_count, ParseBindingTrace* trace);
BUSTER_PARSER_ENTRY ParseScalarResult parse_scalar(const uint8_t* kinds, const uint32_t* names, const uint8_t* flags, uint32_t count, CLanguage language, ParseMeanings* meanings,
                                      ParseTree* tree, Arena* scratch, ParseDiagnostic* diagnostics, uint32_t capacity, uint32_t* diagnostic_count);
BUSTER_PARSER_ENTRY bool parse_tables_initialize(void);
BUSTER_PARSER_ENTRY __attribute__((unused)) bool parse_tables_initialized(void);
BUSTER_PARSER_ENTRY bool parse_initialize(Parser* parser, const ParseOptions* options, CLanguage language);
BUSTER_PARSER_ENTRY bool parse_translation_unit(Parser* parser, Preprocessor* pp);
BUSTER_PARSER_ENTRY void parse_print_diagnostics(const Parser* parser, Preprocessor* pp);

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND
#include "buster/compiler/c_parser_impl.c"
#else
// The optional binding and GNU metadata readers include their types with their implementations. Their callers
// belong to the parser's home; retain those semantic references here without emitting code or disabling warnings.
static __attribute__((unused)) void parse_implementation_interfaces(void)
{
    (void)&parse_binding_trace_rebind;
    (void)&parse_binding_trace_tag;
    (void)&parse_binding_trace_labels;
    (void)&parse_binding_trace_identifier;
    (void)&gnu_syntax_build;
}
#endif
