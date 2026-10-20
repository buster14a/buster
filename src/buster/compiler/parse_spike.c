// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The data-parallel parsing spike (#186): does preprocessed C parse faster on one Zen 5 core with branch-free AVX-512
// kernels than with a scalar shunting-yard parser? Both build the same tree: a parent for every token. This file holds
// the token refinement the parser reads, the tree's definition, the scalar parser (the oracle and the fallback) and the
// kernels with their scalar twins. parse_spike_benchmark.c includes it and runs the `spike-parse` command.
//
// Layout, in this order:
//   - Token kinds: SpikeKind (BUSTER_SPIKE_KINDS), the keyword table (spike_keywords) and SpikeTokens.
//   - Refinement: the identifier table (SpikeNames, spike_intern), spike_refine (lexer tokens to parser tokens: keywords
//     resolved, literals merged, pragma lines dropped, identifiers interned).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// Token kinds

// The kinds the parser reads. The lexer's punctuators keep their own kinds; literals are merged by what the grammar
// needs (any number, any character constant, any string literal); keywords are merged by what they mean to the
// parser, since `int` and `long` parse alike. The language is clang's default C, gnu17 (`clang -E -P` preprocesses in
// it), so C23's keywords (bool, true, nullptr, alignas, static_assert, constexpr, ...) are identifiers there.
#define BUSTER_SPIKE_KINDS(X) \
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

typedef enum SpikeKind
{
#define BUSTER_SPIKE_KIND_ENUM(name) SPIKE_KIND_##name,
    BUSTER_SPIKE_KINDS(BUSTER_SPIKE_KIND_ENUM)
#undef BUSTER_SPIKE_KIND_ENUM
    SPIKE_KIND_COUNT,
} SpikeKind;

// Kinds index 128-entry tables, two zmm registers for vpermi2b.
_Static_assert(SPIKE_KIND_COUNT <= 128, "spike kinds must fit 128-entry tables");

static const char* const spike_kind_names[SPIKE_KIND_COUNT] =
{
#define BUSTER_SPIKE_KIND_NAME(name) #name,
    BUSTER_SPIKE_KINDS(BUSTER_SPIKE_KIND_NAME)
#undef BUSTER_SPIKE_KIND_NAME
};

// What each keyword spelling means to the parser. The spellings are clang's in gnu17 on x86-64 Linux (each checked
// with clang 23.1.3): `_Float128`, `_Float64` and `__float80` are identifiers there (glibc declares the first two as
// typedefs), as are MS spellings like `__declspec` and `__ptr32`, while the calling conventions (`__cdecl`, ...) and the
// nullability qualifiers are keywords. `__int128_t`, `__uint128_t` and `__builtin_va_list` are clang's predeclared
// typedefs, treated as type keywords since nothing redeclares them.
typedef struct SpikeKeyword SpikeKeyword;
struct SpikeKeyword
{
    const char* spelling;
    uint8_t kind;
};

static const SpikeKeyword spike_keywords[] =
{
    { "void", SPIKE_KIND_TYPE },
    { "char", SPIKE_KIND_TYPE },
    { "short", SPIKE_KIND_TYPE },
    { "int", SPIKE_KIND_TYPE },
    { "long", SPIKE_KIND_TYPE },
    { "float", SPIKE_KIND_TYPE },
    { "double", SPIKE_KIND_TYPE },
    { "signed", SPIKE_KIND_TYPE },
    { "__signed", SPIKE_KIND_TYPE },
    { "__signed__", SPIKE_KIND_TYPE },
    { "unsigned", SPIKE_KIND_TYPE },
    { "_Bool", SPIKE_KIND_TYPE },
    { "_Complex", SPIKE_KIND_TYPE },
    { "__complex", SPIKE_KIND_TYPE },
    { "__complex__", SPIKE_KIND_TYPE },
    { "_Imaginary", SPIKE_KIND_TYPE },
    { "__int128", SPIKE_KIND_TYPE },
    { "__int128_t", SPIKE_KIND_TYPE },
    { "__uint128_t", SPIKE_KIND_TYPE },
    { "__builtin_va_list", SPIKE_KIND_TYPE },
    { "__builtin_ms_va_list", SPIKE_KIND_TYPE },
    { "__auto_type", SPIKE_KIND_TYPE },
    { "_Float16", SPIKE_KIND_TYPE },
    { "__float128", SPIKE_KIND_TYPE },
    { "__ibm128", SPIKE_KIND_TYPE },
    { "__fp16", SPIKE_KIND_TYPE },
    { "__bf16", SPIKE_KIND_TYPE },
    { "_Decimal32", SPIKE_KIND_TYPE },
    { "_Decimal64", SPIKE_KIND_TYPE },
    { "_Decimal128", SPIKE_KIND_TYPE },
    { "struct", SPIKE_KIND_STRUCT },
    { "union", SPIKE_KIND_STRUCT },
    { "enum", SPIKE_KIND_ENUM },
    { "const", SPIKE_KIND_QUALIFIER },
    { "__const", SPIKE_KIND_QUALIFIER },
    { "__const__", SPIKE_KIND_QUALIFIER },
    { "volatile", SPIKE_KIND_QUALIFIER },
    { "__volatile", SPIKE_KIND_QUALIFIER },
    { "__volatile__", SPIKE_KIND_QUALIFIER },
    { "restrict", SPIKE_KIND_QUALIFIER },
    { "__restrict", SPIKE_KIND_QUALIFIER },
    { "__restrict__", SPIKE_KIND_QUALIFIER },
    { "_Nonnull", SPIKE_KIND_QUALIFIER },
    { "_Nullable", SPIKE_KIND_QUALIFIER },
    { "_Nullable_result", SPIKE_KIND_QUALIFIER },
    { "_Null_unspecified", SPIKE_KIND_QUALIFIER },
    { "__cdecl", SPIKE_KIND_CALLING_CONVENTION },
    { "__stdcall", SPIKE_KIND_CALLING_CONVENTION },
    { "__fastcall", SPIKE_KIND_CALLING_CONVENTION },
    { "__thiscall", SPIKE_KIND_CALLING_CONVENTION },
    { "__regcall", SPIKE_KIND_CALLING_CONVENTION },
    { "__vectorcall", SPIKE_KIND_CALLING_CONVENTION },
    { "__pascal", SPIKE_KIND_CALLING_CONVENTION },
    { "_Atomic", SPIKE_KIND_ATOMIC },
    { "extern", SPIKE_KIND_STORAGE },
    { "static", SPIKE_KIND_STORAGE },
    { "auto", SPIKE_KIND_STORAGE },
    { "register", SPIKE_KIND_STORAGE },
    { "_Thread_local", SPIKE_KIND_STORAGE },
    { "__thread", SPIKE_KIND_STORAGE },
    { "inline", SPIKE_KIND_STORAGE },
    { "__inline", SPIKE_KIND_STORAGE },
    { "__inline__", SPIKE_KIND_STORAGE },
    { "_Noreturn", SPIKE_KIND_STORAGE },
    { "typedef", SPIKE_KIND_TYPEDEF },
    { "__attribute__", SPIKE_KIND_ATTRIBUTE },
    { "__attribute", SPIKE_KIND_ATTRIBUTE },
    { "asm", SPIKE_KIND_ASM },
    { "__asm", SPIKE_KIND_ASM },
    { "__asm__", SPIKE_KIND_ASM },
    { "_Alignas", SPIKE_KIND_ALIGNAS },
    { "typeof", SPIKE_KIND_TYPEOF },
    { "__typeof", SPIKE_KIND_TYPEOF },
    { "__typeof__", SPIKE_KIND_TYPEOF },
    { "__typeof_unqual", SPIKE_KIND_TYPEOF },
    { "__typeof_unqual__", SPIKE_KIND_TYPEOF },
    { "_BitInt", SPIKE_KIND_TYPEOF },
    { "sizeof", SPIKE_KIND_SIZEOF },
    { "_Alignof", SPIKE_KIND_SIZEOF },
    { "__alignof", SPIKE_KIND_SIZEOF },
    { "__alignof__", SPIKE_KIND_SIZEOF },
    { "__builtin_va_arg", SPIKE_KIND_TYPE_ARGUMENT },
    { "__builtin_offsetof", SPIKE_KIND_TYPE_ARGUMENT },
    { "__builtin_types_compatible_p", SPIKE_KIND_TYPE_ARGUMENT },
    { "__builtin_convertvector", SPIKE_KIND_TYPE_ARGUMENT },
    { "__builtin_bit_cast", SPIKE_KIND_TYPE_ARGUMENT },
    { "_Generic", SPIKE_KIND_GENERIC },
    { "_Static_assert", SPIKE_KIND_STATIC_ASSERT },
    { "__extension__", SPIKE_KIND_EXTENSION },
    { "__real", SPIKE_KIND_REAL_IMAG },
    { "__real__", SPIKE_KIND_REAL_IMAG },
    { "__imag", SPIKE_KIND_REAL_IMAG },
    { "__imag__", SPIKE_KIND_REAL_IMAG },
    { "__label__", SPIKE_KIND_LABEL },
    { "if", SPIKE_KIND_IF },
    { "else", SPIKE_KIND_ELSE },
    { "switch", SPIKE_KIND_SWITCH },
    { "case", SPIKE_KIND_CASE },
    { "default", SPIKE_KIND_DEFAULT },
    { "while", SPIKE_KIND_WHILE },
    { "do", SPIKE_KIND_DO },
    { "for", SPIKE_KIND_FOR },
    { "goto", SPIKE_KIND_GOTO },
    { "continue", SPIKE_KIND_CONTINUE },
    { "break", SPIKE_KIND_BREAK },
    { "return", SPIKE_KIND_RETURN },
};

// The parser's input, struct-of-arrays. Every array has BUSTER_SPIKE_TOKEN_PADDING entries before token 0 and after the
// last token, holding SPIKE_KIND_END and zeros, so a kernel can load a whole block around any token.
#define BUSTER_SPIKE_TOKEN_PADDING 128

typedef struct SpikeTokens SpikeTokens;
struct SpikeTokens
{
    uint8_t* kinds;
    // Identifier ids from 1 (SpikeNames), and 0 for every other token.
    uint32_t* names;
    // The token's bytes in the source, for diagnostics.
    uint32_t* starts;
    uint32_t* ends;
    uint64_t count;
    // Tokens of #pragma lines that clang -E -P kept, which spike_refine drops.
    uint64_t pragma_tokens;
};

// ---------------------------------------------------------------------------------------------------------------------
// Refinement

// The identifier table: every spelling gets an id from 1, in order of first appearance; keywords are interned first,
// so a spelling's parser kind is kinds[id]. Open addressing with linear probing over a power-of-two table. It is
// outside what the spike times (#116's job), so it is simple rather than fast.
typedef struct SpikeNames SpikeNames;
struct SpikeNames
{
    // slots[i] is an id, or 0 for an empty slot.
    uint32_t* slots;
    uint64_t slot_mask;
    // By id: the spelling (offset and length in `bytes`), its hash, and its parser kind.
    uint32_t* offsets;
    uint32_t* lengths;
    uint64_t* hashes;
    uint8_t* kinds;
    uint32_t count;
    uint32_t capacity;
    char* bytes;
    uint64_t byte_count;
    uint64_t byte_capacity;
};

static uint64_t spike_hash(const uint8_t* bytes, uint64_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint64_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    hash ^= hash >> 29;
    return hash;
}

// The id of a spelling, interned if it is new; 0 if the table is full.
static uint32_t spike_intern(SpikeNames* names, const uint8_t* bytes, uint64_t length)
{
    uint32_t result = 0;
    const uint64_t hash = spike_hash(bytes, length);
    uint64_t slot = hash & names->slot_mask;
    bool searching = true;
    while (searching)
    {
        const uint32_t id = names->slots[slot];
        if (id == 0)
        {
            searching = false;
            if (names->count + 1 < names->capacity && names->byte_count + length <= names->byte_capacity)
            {
                result = names->count + 1;
                names->count = result;
                names->offsets[result] = (uint32_t)names->byte_count;
                names->lengths[result] = (uint32_t)length;
                names->hashes[result] = hash;
                names->kinds[result] = SPIKE_KIND_IDENTIFIER;
                memcpy(names->bytes + names->byte_count, bytes, length);
                names->byte_count += length;
                names->slots[slot] = result;
            }
        }
        else if (names->hashes[id] == hash && names->lengths[id] == length && memcmp(names->bytes + names->offsets[id], bytes, length) == 0)
        {
            searching = false;
            result = id;
        }
        else
        {
            slot = (slot + 1) & names->slot_mask;
        }
    }
    return result;
}

// Interns the keywords, so that each spelling's kind is set. Returns false if the table is too small.
static bool spike_names_add_keywords(SpikeNames* names)
{
    bool result = true;
    for (uint64_t i = 0; result && i < sizeof(spike_keywords) / sizeof(spike_keywords[0]); i += 1)
    {
        const SpikeKeyword* keyword = &spike_keywords[i];
        const uint32_t id = spike_intern(names, (const uint8_t*)keyword->spelling, strlen(keyword->spelling));
        result = id != 0;
        if (result)
        {
            names->kinds[id] = keyword->kind;
        }
    }
    return result;
}

// The parser kind of each lexer kind that isn't an identifier; SPIKE_KIND_UNKNOWN for what C doesn't use after
// preprocessing (`::`, `#`, `##` and C++'s `.*`, `->*` and `<=>`).
static uint8_t spike_kind_from_lexer(uint8_t lexer_kind)
{
    uint8_t result = SPIKE_KIND_UNKNOWN;
    switch (lexer_kind)
    {
        case C_TOKEN_NUMERIC_CONSTANT: result = SPIKE_KIND_NUMBER; break;
        case C_TOKEN_CHAR_CONSTANT:
        case C_TOKEN_WIDE_CHAR_CONSTANT:
        case C_TOKEN_UTF8_CHAR_CONSTANT:
        case C_TOKEN_UTF16_CHAR_CONSTANT:
        case C_TOKEN_UTF32_CHAR_CONSTANT: result = SPIKE_KIND_CHARACTER; break;
        case C_TOKEN_STRING_LITERAL:
        case C_TOKEN_WIDE_STRING_LITERAL:
        case C_TOKEN_UTF8_STRING_LITERAL:
        case C_TOKEN_UTF16_STRING_LITERAL:
        case C_TOKEN_UTF32_STRING_LITERAL: result = SPIKE_KIND_STRING; break;
        case C_TOKEN_L_SQUARE: result = SPIKE_KIND_L_SQUARE; break;
        case C_TOKEN_R_SQUARE: result = SPIKE_KIND_R_SQUARE; break;
        case C_TOKEN_L_PAREN: result = SPIKE_KIND_L_PAREN; break;
        case C_TOKEN_R_PAREN: result = SPIKE_KIND_R_PAREN; break;
        case C_TOKEN_L_BRACE: result = SPIKE_KIND_L_BRACE; break;
        case C_TOKEN_R_BRACE: result = SPIKE_KIND_R_BRACE; break;
        case C_TOKEN_PERIOD: result = SPIKE_KIND_PERIOD; break;
        case C_TOKEN_ELLIPSIS: result = SPIKE_KIND_ELLIPSIS; break;
        case C_TOKEN_AMP: result = SPIKE_KIND_AMP; break;
        case C_TOKEN_AMPAMP: result = SPIKE_KIND_AMPAMP; break;
        case C_TOKEN_AMPEQUAL: result = SPIKE_KIND_AMPEQUAL; break;
        case C_TOKEN_STAR: result = SPIKE_KIND_STAR; break;
        case C_TOKEN_STAREQUAL: result = SPIKE_KIND_STAREQUAL; break;
        case C_TOKEN_PLUS: result = SPIKE_KIND_PLUS; break;
        case C_TOKEN_PLUSPLUS: result = SPIKE_KIND_PLUSPLUS; break;
        case C_TOKEN_PLUSEQUAL: result = SPIKE_KIND_PLUSEQUAL; break;
        case C_TOKEN_MINUS: result = SPIKE_KIND_MINUS; break;
        case C_TOKEN_ARROW: result = SPIKE_KIND_ARROW; break;
        case C_TOKEN_MINUSMINUS: result = SPIKE_KIND_MINUSMINUS; break;
        case C_TOKEN_MINUSEQUAL: result = SPIKE_KIND_MINUSEQUAL; break;
        case C_TOKEN_TILDE: result = SPIKE_KIND_TILDE; break;
        case C_TOKEN_EXCLAIM: result = SPIKE_KIND_EXCLAIM; break;
        case C_TOKEN_EXCLAIMEQUAL: result = SPIKE_KIND_EXCLAIMEQUAL; break;
        case C_TOKEN_SLASH: result = SPIKE_KIND_SLASH; break;
        case C_TOKEN_SLASHEQUAL: result = SPIKE_KIND_SLASHEQUAL; break;
        case C_TOKEN_PERCENT: result = SPIKE_KIND_PERCENT; break;
        case C_TOKEN_PERCENTEQUAL: result = SPIKE_KIND_PERCENTEQUAL; break;
        case C_TOKEN_LESS: result = SPIKE_KIND_LESS; break;
        case C_TOKEN_LESSLESS: result = SPIKE_KIND_LESSLESS; break;
        case C_TOKEN_LESSEQUAL: result = SPIKE_KIND_LESSEQUAL; break;
        case C_TOKEN_LESSLESSEQUAL: result = SPIKE_KIND_LESSLESSEQUAL; break;
        case C_TOKEN_GREATER: result = SPIKE_KIND_GREATER; break;
        case C_TOKEN_GREATERGREATER: result = SPIKE_KIND_GREATERGREATER; break;
        case C_TOKEN_GREATEREQUAL: result = SPIKE_KIND_GREATEREQUAL; break;
        case C_TOKEN_GREATERGREATEREQUAL: result = SPIKE_KIND_GREATERGREATEREQUAL; break;
        case C_TOKEN_CARET: result = SPIKE_KIND_CARET; break;
        case C_TOKEN_CARETEQUAL: result = SPIKE_KIND_CARETEQUAL; break;
        case C_TOKEN_PIPE: result = SPIKE_KIND_PIPE; break;
        case C_TOKEN_PIPEPIPE: result = SPIKE_KIND_PIPEPIPE; break;
        case C_TOKEN_PIPEEQUAL: result = SPIKE_KIND_PIPEEQUAL; break;
        case C_TOKEN_QUESTION: result = SPIKE_KIND_QUESTION; break;
        case C_TOKEN_COLON: result = SPIKE_KIND_COLON; break;
        case C_TOKEN_SEMI: result = SPIKE_KIND_SEMI; break;
        case C_TOKEN_EQUAL: result = SPIKE_KIND_EQUAL; break;
        case C_TOKEN_EQUALEQUAL: result = SPIKE_KIND_EQUALEQUAL; break;
        case C_TOKEN_COMMA: result = SPIKE_KIND_COMMA; break;
        default: break;
    }
    return result;
}

// Turns `count` lexer tokens of `source` into parser tokens in `output`, whose arrays have room for 2 * count tokens
// (a `::` becomes two colons) plus the padding on both sides (output->kinds etc. point at token 0). Lines that start with `#` (pragmas, the only
// directives `clang -E -P` keeps) are dropped. Returns false if the identifier table is full.
static bool spike_refine(const uint8_t* source, const uint8_t* lexer_kinds, const uint32_t* starts, const uint32_t* ends, uint64_t count, SpikeNames* names,
                         SpikeTokens* output)
{
    bool result = true;
    uint64_t written = 0;
    uint64_t pragma_tokens = 0;
    bool in_pragma = false;
    for (uint64_t i = 0; result && i < count; i += 1)
    {
        const uint8_t lexer_kind = lexer_kinds[i] & ~BUSTER_C_TOKEN_START_OF_LINE;
        const bool start_of_line = (lexer_kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
        in_pragma = start_of_line ? lexer_kind == C_TOKEN_HASH : in_pragma;
        if (in_pragma)
        {
            pragma_tokens += 1;
        }
        else
        {
            uint8_t kind = spike_kind_from_lexer(lexer_kind);
            uint32_t name = 0;
            if (lexer_kind == C_TOKEN_RAW_IDENTIFIER)
            {
                name = spike_intern(names, source + starts[i], ends[i] - starts[i]);
                result = name != 0;
                kind = result ? names->kinds[name] : SPIKE_KIND_UNKNOWN;
                // Keywords carry no name; only identifiers do.
                name = kind == SPIKE_KIND_IDENTIFIER ? name : 0;
            }
            if (lexer_kind == C_TOKEN_COLONCOLON)
            {
                // C17 has no `::`: two colons, as in an asm statement's empty operand list.
                output->kinds[written] = SPIKE_KIND_COLON;
                output->names[written] = 0;
                output->starts[written] = starts[i];
                output->ends[written] = starts[i] + 1;
                written += 1;
                kind = SPIKE_KIND_COLON;
                output->starts[written] = starts[i] + 1;
            }
            else
            {
                output->starts[written] = starts[i];
            }
            output->kinds[written] = kind;
            output->names[written] = name;
            output->ends[written] = ends[i];
            written += 1;
        }
    }
    for (uint64_t i = 0; i < BUSTER_SPIKE_TOKEN_PADDING; i += 1)
    {
        output->kinds[written + i] = SPIKE_KIND_END;
        output->names[written + i] = 0;
        output->kinds[-1 - (int64_t)i] = SPIKE_KIND_END;
        output->names[-1 - (int64_t)i] = 0;
    }
    output->count = written;
    output->pragma_tokens = pragma_tokens;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The tree
//
// The parser's output is a parent for every token: parents[i] is the token whose node is token i's parent, or
// BUSTER_SPIKE_ROOT for the node of a top-level declaration. Every token is a node, punctuation included, and a node's
// children in source order are its operands in order. The tree is C's grammar written as operator precedence, so that
// a data-parallel kernel can build it with nearest-smaller-value searches (Bar-On and Vishkin, 1985):
//
//   - Each token has a role (SpikeRole), which gives it a level (SpikeLevel, higher binds tighter) and a behavior.
//     Expressions use C's precedence; declarations parse as the expressions they mirror, with the specifiers as
//     right-associative prefix operators below the comma (`int *p, q` is `int` applied to `*p, q`), and statements
//     sit below everything an expression or a declaration contains.
//   - Brackets: ( ) [ ] { }, ? :, and case/default ... :. A group's closer is its node; its opener is a leaf child of
//     the closer. The closer's role gives the group's level: a call's `)` is a postfix operator whose left operand is
//     the callee, a cast's `)` a prefix operator, a grouping's `)` an operand, a struct body's `}` a specifier.
//   - Keys are (depth, level), depth first. Each token i has L(i), the nearest token before it whose key is below its
//     left threshold, and R(i), the nearest after it below its right threshold. For a left-associative level the left
//     search is strict and the right one inclusive, for a right-associative one the other way round. An opener's key
//     is (its depth, BUSTER_SPIKE_LEVEL_OPENER): no search at its own depth sees it, and every search from inside its
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
//     which statement is decided by a statement stack (spike_statement_*), not by levels.
//
// So `if (a) x = b * c; else y = -z;` is
//
//     ) [if-head] <- if, (, a, ;1, else
//     ;1 <- =1 <- x, *  (* <- b, c)
//     else <- ;2 <- =2 <- y, -  (- <- z)

#define BUSTER_SPIKE_ROOT UINT32_MAX
#define BUSTER_SPIKE_NONE UINT32_MAX

typedef enum SpikeLevel
{
    SPIKE_LEVEL_NONE,
    SPIKE_LEVEL_TERMINATOR,
    SPIKE_LEVEL_HEAD,
    SPIKE_LEVEL_ASM_COLON,
    SPIKE_LEVEL_SEPARATOR,
    SPIKE_LEVEL_ASSOCIATION,
    SPIKE_LEVEL_SPECIFIER,
    SPIKE_LEVEL_RETURN,
    SPIKE_LEVEL_COMMA,
    SPIKE_LEVEL_BITFIELD,
    SPIKE_LEVEL_ASSIGN,
    SPIKE_LEVEL_RANGE,
    SPIKE_LEVEL_CONDITIONAL,
    SPIKE_LEVEL_LOGICAL_OR,
    SPIKE_LEVEL_LOGICAL_AND,
    SPIKE_LEVEL_BITWISE_OR,
    SPIKE_LEVEL_BITWISE_XOR,
    SPIKE_LEVEL_BITWISE_AND,
    SPIKE_LEVEL_EQUALITY,
    SPIKE_LEVEL_RELATIONAL,
    SPIKE_LEVEL_SHIFT,
    SPIKE_LEVEL_ADDITIVE,
    SPIKE_LEVEL_MULTIPLICATIVE,
    SPIKE_LEVEL_PREFIX,
    SPIKE_LEVEL_POSTFIX,
    SPIKE_LEVEL_OPERAND,
    // Every threshold is at most BUSTER_SPIKE_LEVEL_OPERAND + 1, so an opener is invisible at its own depth.
    SPIKE_LEVEL_OPENER = 31,
} SpikeLevel;

// A key is depth * BUSTER_SPIKE_LEVELS + level.
#define BUSTER_SPIKE_LEVELS 32

// How a role's parent is found. The statement stack then overrides the parents of the statement-level tokens of blocks
// (spike_statement_*): those a head owns, and `else` and `do`.
typedef enum SpikeRule
{
    // The greater of L and R (see above).
    SPIKE_RULE_SELECT,
    // L: a qualifier or an attribute after `*`, and a string literal after a string literal.
    SPIKE_RULE_LEFT,
    // R: a keyword before the group it introduces, `struct`, a tag with a body.
    SPIKE_RULE_RIGHT,
    // The matching closer.
    SPIKE_RULE_OPENER,
} SpikeRule;

// The roles: name, level, how the searches compare and the parent rule. The comparisons: L for left-associative
// (strict to the left, inclusive to the right), R for right-associative (the other way round), F for strict on both
// sides (operands and terminators), S for inclusive to the left (a string literal after a string literal, which finds
// the one before it) and T for a statement head, which searches as a terminator does, so that a head no other head
// owns belongs to its block.
#define BUSTER_SPIKE_ROLES(X) \
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
    X(OPEN_GROUP, OPENER, F, OPENER) \
    X(OPEN_CALL, OPENER, F, OPENER) \
    X(OPEN_FUNCTION, OPENER, F, OPENER) \
    X(OPEN_CAST, OPENER, F, OPENER) \
    X(OPEN_COMPOUND, OPENER, F, OPENER) \
    X(OPEN_SIZEOF, OPENER, F, OPENER) \
    X(OPEN_SPECIFIER, OPENER, F, OPENER) \
    X(OPEN_ATTRIBUTE, OPENER, F, OPENER) \
    X(OPEN_ATTRIBUTE_LIST, OPENER, F, OPENER) \
    X(OPEN_ASM, OPENER, F, OPENER) \
    X(OPEN_KEYWORD, OPENER, F, OPENER) \
    X(OPEN_CONDITION, OPENER, F, OPENER) \
    X(OPEN_FOR, OPENER, F, OPENER) \
    X(OPEN_SUBSCRIPT, OPENER, F, OPENER) \
    X(OPEN_BLOCK, OPENER, F, OPENER) \
    X(OPEN_BODY, OPENER, F, OPENER) \
    X(OPEN_STRUCT, OPENER, F, OPENER) \
    X(OPEN_ENUM, OPENER, F, OPENER) \
    X(OPEN_INITIALIZER, OPENER, F, OPENER) \
    X(OPEN_COMPOUND_BODY, OPENER, F, OPENER) \
    X(OPEN_TERNARY, OPENER, F, OPENER) \
    X(OPEN_CASE, OPENER, F, OPENER) \
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

typedef enum SpikeRole
{
#define BUSTER_SPIKE_ROLE_ENUM(name, level, compare, rule) SPIKE_ROLE_##name,
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_ENUM)
#undef BUSTER_SPIKE_ROLE_ENUM
    SPIKE_ROLE_COUNT,
} SpikeRole;

_Static_assert(SPIKE_ROLE_COUNT <= 128, "roles must fit 128-entry tables");

static const char* const spike_role_names[SPIKE_ROLE_COUNT] =
{
#define BUSTER_SPIKE_ROLE_NAME(name, level, compare, rule) #name,
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_NAME)
#undef BUSTER_SPIKE_ROLE_NAME
};

static const uint8_t spike_role_levels[SPIKE_ROLE_COUNT] =
{
#define BUSTER_SPIKE_ROLE_LEVEL(name, level, compare, rule) SPIKE_LEVEL_##level,
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_LEVEL)
#undef BUSTER_SPIKE_ROLE_LEVEL
};

// The left and right thresholds, as levels: a search finds the nearest token whose key is below (depth, threshold).
#define BUSTER_SPIKE_LEFT_THRESHOLD_L(level) (level)
#define BUSTER_SPIKE_LEFT_THRESHOLD_R(level) ((level) + 1)
#define BUSTER_SPIKE_LEFT_THRESHOLD_F(level) (level)
#define BUSTER_SPIKE_LEFT_THRESHOLD_S(level) ((level) + 1)
#define BUSTER_SPIKE_LEFT_THRESHOLD_T(level) SPIKE_LEVEL_TERMINATOR
#define BUSTER_SPIKE_RIGHT_THRESHOLD_L(level) ((level) + 1)
#define BUSTER_SPIKE_RIGHT_THRESHOLD_R(level) (level)
#define BUSTER_SPIKE_RIGHT_THRESHOLD_F(level) (level)
#define BUSTER_SPIKE_RIGHT_THRESHOLD_S(level) (level)
#define BUSTER_SPIKE_RIGHT_THRESHOLD_T(level) SPIKE_LEVEL_TERMINATOR

static const uint8_t spike_role_left_thresholds[SPIKE_ROLE_COUNT] =
{
#define BUSTER_SPIKE_ROLE_LEFT(name, level, compare, rule) BUSTER_SPIKE_LEFT_THRESHOLD_##compare(SPIKE_LEVEL_##level),
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_LEFT)
#undef BUSTER_SPIKE_ROLE_LEFT
};

static const uint8_t spike_role_right_thresholds[SPIKE_ROLE_COUNT] =
{
#define BUSTER_SPIKE_ROLE_RIGHT(name, level, compare, rule) BUSTER_SPIKE_RIGHT_THRESHOLD_##compare(SPIKE_LEVEL_##level),
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_RIGHT)
#undef BUSTER_SPIKE_ROLE_RIGHT
};

static const uint8_t spike_role_rules[SPIKE_ROLE_COUNT] =
{
#define BUSTER_SPIKE_ROLE_RULE(name, level, compare, rule) SPIKE_RULE_##rule,
    BUSTER_SPIKE_ROLES(BUSTER_SPIKE_ROLE_RULE)
#undef BUSTER_SPIKE_ROLE_RULE
};

// Which way equal keys go, by level: true for left. Only levels whose operators can be both L and R of a token tie.
static const bool spike_level_ties_left[BUSTER_SPIKE_LEVELS] =
{
    [SPIKE_LEVEL_ASM_COLON] = true,
    [SPIKE_LEVEL_SEPARATOR] = true,
    [SPIKE_LEVEL_ASSOCIATION] = true,
    [SPIKE_LEVEL_COMMA] = true,
    [SPIKE_LEVEL_BITFIELD] = true,
    [SPIKE_LEVEL_RANGE] = true,
    [SPIKE_LEVEL_LOGICAL_OR] = true,
    [SPIKE_LEVEL_LOGICAL_AND] = true,
    [SPIKE_LEVEL_BITWISE_OR] = true,
    [SPIKE_LEVEL_BITWISE_XOR] = true,
    [SPIKE_LEVEL_BITWISE_AND] = true,
    [SPIKE_LEVEL_EQUALITY] = true,
    [SPIKE_LEVEL_RELATIONAL] = true,
    [SPIKE_LEVEL_SHIFT] = true,
    [SPIKE_LEVEL_ADDITIVE] = true,
    [SPIKE_LEVEL_MULTIPLICATIVE] = true,
    [SPIKE_LEVEL_POSTFIX] = true,
};

// Whether an L candidate of this level loses to any R: openers, terminators and heads, whose tokens to their right
// never belong to them through the levels (the expression in a group belongs to its closer, a statement's tokens to
// its terminator, and `i++` in `for (...; ...; i++)` to the `)`).
static bool spike_level_is_weak(uint8_t level)
{
    return level == SPIKE_LEVEL_OPENER || level == SPIKE_LEVEL_TERMINATOR || level == SPIKE_LEVEL_HEAD;
}

// The parent from L and R under SPIKE_RULE_SELECT. Keys are depth * BUSTER_SPIKE_LEVELS + level; equal keys are on the
// same level, which says which way they go.
static uint32_t spike_select(uint32_t left, uint32_t left_depth, uint8_t left_level, uint32_t right, uint32_t right_depth, uint8_t right_level)
{
    uint32_t result;
    if (left == BUSTER_SPIKE_NONE || (right != BUSTER_SPIKE_NONE && spike_level_is_weak(left_level)))
    {
        result = right;
    }
    else if (right == BUSTER_SPIKE_NONE)
    {
        result = left;
    }
    else
    {
        const uint64_t left_key = (uint64_t)left_depth * BUSTER_SPIKE_LEVELS + left_level;
        const uint64_t right_key = (uint64_t)right_depth * BUSTER_SPIKE_LEVELS + right_level;
        const bool take_left = left_key > right_key || (left_key == right_key && spike_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}

// The closer's role for each opener's role (SPIKE_ROLE_NONE for what isn't an opener). A cast's group becomes a
// compound literal's when `{` follows its `)`, and an attribute's closer depends on where the attribute is; the parsers
// decide those themselves.
static const uint8_t spike_closer_roles[SPIKE_ROLE_COUNT] =
{
    [SPIKE_ROLE_OPEN_GROUP] = SPIKE_ROLE_CLOSE_GROUP,
    [SPIKE_ROLE_OPEN_CALL] = SPIKE_ROLE_CLOSE_CALL,
    [SPIKE_ROLE_OPEN_FUNCTION] = SPIKE_ROLE_CLOSE_FUNCTION,
    [SPIKE_ROLE_OPEN_CAST] = SPIKE_ROLE_CLOSE_CAST,
    [SPIKE_ROLE_OPEN_COMPOUND] = SPIKE_ROLE_CLOSE_COMPOUND,
    [SPIKE_ROLE_OPEN_SIZEOF] = SPIKE_ROLE_CLOSE_SIZEOF,
    [SPIKE_ROLE_OPEN_SPECIFIER] = SPIKE_ROLE_CLOSE_SPECIFIER,
    [SPIKE_ROLE_OPEN_ATTRIBUTE] = SPIKE_ROLE_CLOSE_ATTRIBUTE,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_LIST] = SPIKE_ROLE_CLOSE_ATTRIBUTE_LIST,
    [SPIKE_ROLE_OPEN_ASM] = SPIKE_ROLE_CLOSE_ASM,
    [SPIKE_ROLE_OPEN_KEYWORD] = SPIKE_ROLE_CLOSE_KEYWORD,
    [SPIKE_ROLE_OPEN_CONDITION] = SPIKE_ROLE_CLOSE_CONDITION,
    [SPIKE_ROLE_OPEN_FOR] = SPIKE_ROLE_CLOSE_FOR,
    [SPIKE_ROLE_OPEN_SUBSCRIPT] = SPIKE_ROLE_CLOSE_SUBSCRIPT,
    [SPIKE_ROLE_OPEN_BLOCK] = SPIKE_ROLE_CLOSE_BLOCK,
    [SPIKE_ROLE_OPEN_BODY] = SPIKE_ROLE_CLOSE_BODY,
    [SPIKE_ROLE_OPEN_STRUCT] = SPIKE_ROLE_CLOSE_STRUCT,
    [SPIKE_ROLE_OPEN_ENUM] = SPIKE_ROLE_CLOSE_ENUM,
    [SPIKE_ROLE_OPEN_INITIALIZER] = SPIKE_ROLE_CLOSE_INITIALIZER,
    [SPIKE_ROLE_OPEN_COMPOUND_BODY] = SPIKE_ROLE_CLOSE_COMPOUND_BODY,
    [SPIKE_ROLE_OPEN_TERNARY] = SPIKE_ROLE_CLOSE_TERNARY,
    [SPIKE_ROLE_OPEN_CASE] = SPIKE_ROLE_CLOSE_CASE,
};

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser
//
// spike_parse_scalar: the oracle, and what the kernels fall back on. One pass over the tokens with explicit stacks and
// no recursion:
//   - Frames (SpikeFrame), one per open group: the group's role, and a mode that says what the group's next token may
//     be, as Carbon's parser keeps states on a stack. Pushing a group sets what its parent frame expects after it.
//     The frame's mode and flags give each token its role from the grammar: whether a declaration or an expression is
//     being read, whether an operand is expected, whether a type specifier was seen (for typedef names).
//   - Bindings: the meaning of each name (typedef or not) in the current scope, restored from a log at the end of
//     each scope, so typedef names are looked up exactly where they appear (#128's binding stacks, with one level).
//   - The precedence stacks, shunting-yard's: the tokens that L of a later token may still be, keys increasing, and
//     the tokens still waiting for their R, right thresholds increasing. A token that ends a token's search resolves it.
//   - The statement stack: blocks and the statement heads that still own the statement after them.

typedef enum SpikeMode
{
    // The start of an item: a declaration or statement in a block, a declaration at file scope, a member, a parameter,
    // an enumerator, an initializer, a for clause, or a group that holds a type name or an expression.
    SPIKE_MODE_ITEM,
    SPIKE_MODE_SPECIFIERS,
    // After struct, union or enum: attributes, a tag or a body.
    SPIKE_MODE_TAG,
    // After a tag that a body follows.
    SPIKE_MODE_TAG_BODY,
    // In a declarator, before its name or the declarator in parentheses.
    SPIKE_MODE_DECLARATOR,
    // After a declarator's name: its suffixes, then what ends it.
    SPIKE_MODE_AFTER_DECLARATOR,
    // After `=` in a declaration or in an initializer list: `{` or an expression.
    SPIKE_MODE_INITIALIZER,
    SPIKE_MODE_OPERAND,
    SPIKE_MODE_OPERATOR,
    // After `.` or `->`: the member's name.
    SPIKE_MODE_MEMBER,
    // After a label's name: its `:`.
    SPIKE_MODE_LABEL,
    // After `default`: its `:`, in a statement or in _Generic.
    SPIKE_MODE_DEFAULT,
    SPIKE_MODE_ASSOCIATION,
    // Before the `(` of a group a keyword introduces.
    SPIKE_MODE_CONDITION_PAREN,
    SPIKE_MODE_FOR_PAREN,
    SPIKE_MODE_KEYWORD_PAREN,
    SPIKE_MODE_SIZEOF_PAREN,
    SPIKE_MODE_SPECIFIER_PAREN,
    SPIKE_MODE_ATTRIBUTE_PAREN,
    SPIKE_MODE_ASM_PAREN,
    // In `__attribute__(`: the list's `(`, then its `)`.
    SPIKE_MODE_ATTRIBUTE_LIST,
    SPIKE_MODE_ATTRIBUTE_END,
    // After a compound literal's type: its `{`.
    SPIKE_MODE_COMPOUND_BODY,
} SpikeMode;

// The frame's contents are a declaration (cleared when it ends), after `typedef`, after a type specifier.
#define BUSTER_SPIKE_FRAME_DECLARATION 0x01
#define BUSTER_SPIKE_FRAME_TYPEDEF 0x02
#define BUSTER_SPIKE_FRAME_TYPE_SEEN 0x04
// The declarator's last suffix is a function's, so `{` after it starts a function body.
#define BUSTER_SPIKE_FRAME_FUNCTION 0x08
// Declarator names here aren't bound: members and type names.
#define BUSTER_SPIKE_FRAME_NO_BIND 0x10
// Right after `*` in a declarator, where qualifiers and attributes belong to the pointer.
#define BUSTER_SPIKE_FRAME_AFTER_STAR 0x20
// An attribute's argument list, where keywords are names, and an asm statement's operands.
#define BUSTER_SPIKE_FRAME_ATTRIBUTE 0x40
#define BUSTER_SPIKE_FRAME_ASM 0x80

typedef struct SpikeFrame SpikeFrame;
struct SpikeFrame
{
    // The opener, or BUSTER_SPIKE_NONE for the file.
    uint32_t opener;
    // The binding log's length when the frame's scope began, or BUSTER_SPIKE_NONE if the frame has no scope.
    uint32_t scope;
    // The opener's role, or SPIKE_ROLE_NONE for the file.
    uint8_t role;
    uint8_t mode;
    uint8_t flags;
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
    // Which argument of a keyword group this is.
    uint16_t argument;
};

typedef enum SpikeMeaning
{
    SPIKE_MEANING_ORDINARY,
    SPIKE_MEANING_TYPEDEF,
} SpikeMeaning;

typedef enum SpikeStatementState
{
    SPIKE_STATEMENT_BLOCK,
    // A head whose statement hasn't started, then one whose statement has.
    SPIKE_STATEMENT_WAITING,
    SPIKE_STATEMENT_STARTED,
    // An if-statement whose then-statement is complete, which an else may still continue.
    SPIKE_STATEMENT_IF_DONE,
    // A do-statement whose body is complete, waiting for `while (...)`, then for its `;`.
    SPIKE_STATEMENT_DO_DONE,
    SPIKE_STATEMENT_DO_WHILE,
} SpikeStatementState;

typedef struct SpikeStatement SpikeStatement;
struct SpikeStatement
{
    // The head, or for a block BUSTER_SPIKE_NONE.
    uint32_t token;
    // For a block, the head that owns it (its closer's parent), and for a do-statement the head that owns it (its final
    // `;`'s parent); BUSTER_SPIKE_NONE if none does.
    uint32_t owner;
    // For a do-statement, the `)` of its `while (...)`; for a for-statement, the binding log's length at its scope.
    uint32_t extra;
    uint8_t state;
    // The head's keyword (SPIKE_KIND_IF, SPIKE_KIND_DO, ...), or for a block whether it is a statement of its block.
    uint8_t kind;
};

typedef struct SpikeParser SpikeParser;
struct SpikeParser
{
    const uint8_t* kinds;
    const uint32_t* names;
    uint32_t count;
    uint8_t* roles;
    uint32_t* depths;
    uint32_t* parents;
    SpikeFrame* frames;
    uint32_t frame_count;
    uint32_t* left_positions;
    uint32_t* left_keys;
    uint32_t left_count;
    uint32_t* right_positions;
    uint32_t* right_thresholds;
    uint32_t right_count;
    // Each token's L, kept until its R comes.
    uint32_t* lefts;
    // By name id.
    uint8_t* meanings;
    uint32_t* log_names;
    uint8_t* log_meanings;
    uint32_t log_count;
    // The scope that a function declarator's parameters leave open for the body that follows.
    uint32_t body_scope;
    SpikeStatement* statements;
    uint32_t statement_count;
    // The first token the parser couldn't handle, or BUSTER_SPIKE_NONE.
    uint32_t error_token;
    const char* error_message;
};

static void spike_parser_error(SpikeParser* parser, uint32_t token, const char* message)
{
    if (parser->error_token == BUSTER_SPIKE_NONE)
    {
        parser->error_token = token;
        parser->error_message = message;
    }
}

static bool spike_parser_is_typedef_name(const SpikeParser* parser, uint32_t token)
{
    return parser->kinds[token] == SPIKE_KIND_IDENTIFIER && parser->meanings[parser->names[token]] == SPIKE_MEANING_TYPEDEF;
}

// Whether the token can start a type name: a specifier or qualifier keyword, an attribute or a typedef name.
static bool spike_parser_starts_type(const SpikeParser* parser, uint32_t token)
{
    const uint8_t kind = parser->kinds[token];
    return kind == SPIKE_KIND_TYPE || kind == SPIKE_KIND_STRUCT || kind == SPIKE_KIND_ENUM || kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_ATOMIC ||
           kind == SPIKE_KIND_TYPEOF || kind == SPIKE_KIND_ATTRIBUTE || spike_parser_is_typedef_name(parser, token);
}

// Whether the token can start a declaration: a type name, or a storage class, typedef, _Alignas, _Static_assert,
// __label__ or __extension__ before one. A typedef name followed by `:` is a label instead.
static bool spike_parser_starts_declaration(const SpikeParser* parser, uint32_t token)
{
    uint32_t i = token;
    while (parser->kinds[i] == SPIKE_KIND_EXTENSION)
    {
        i += 1;
    }
    const uint8_t kind = parser->kinds[i];
    const bool label = parser->kinds[i] == SPIKE_KIND_IDENTIFIER && parser->kinds[i + 1] == SPIKE_KIND_COLON;
    return !label && (spike_parser_starts_type(parser, i) || kind == SPIKE_KIND_STORAGE || kind == SPIKE_KIND_TYPEDEF || kind == SPIKE_KIND_ALIGNAS ||
                      kind == SPIKE_KIND_STATIC_ASSERT || kind == SPIKE_KIND_LABEL || kind == SPIKE_KIND_CALLING_CONVENTION);
}

static void spike_parser_bind(SpikeParser* parser, uint32_t token, uint8_t meaning)
{
    const uint32_t name = parser->names[token];
    parser->log_names[parser->log_count] = name;
    parser->log_meanings[parser->log_count] = parser->meanings[name];
    parser->log_count += 1;
    parser->meanings[name] = meaning;
}

static void spike_parser_restore(SpikeParser* parser, uint32_t mark)
{
    while (parser->log_count > mark)
    {
        parser->log_count -= 1;
        parser->meanings[parser->log_names[parser->log_count]] = parser->log_meanings[parser->log_count];
    }
}

// The innermost frame with a scope: where a declaration's names are bound.
static bool spike_parser_binds(const SpikeParser* parser)
{
    return (parser->frames[parser->frame_count - 1].flags & BUSTER_SPIKE_FRAME_NO_BIND) == 0;
}

// Pushes a frame for the group `opener` opens; the current frame continues in `continuation` once it closes.
static SpikeFrame* spike_parser_push(SpikeParser* parser, uint32_t opener, uint8_t role, uint8_t mode, uint8_t continuation, bool scope)
{
    SpikeFrame* parent = &parser->frames[parser->frame_count - 1];
    SpikeFrame* frame = &parser->frames[parser->frame_count];
    parser->frame_count += 1;
    *frame = (SpikeFrame){
        .opener = opener,
        .scope = scope ? parser->log_count : BUSTER_SPIKE_NONE,
        .role = role,
        .mode = mode,
        .flags = 0,
        .closer_role = spike_closer_roles[role],
        .keyword = parser->kinds[opener - 1],
        .continuation = continuation,
        .attribute_closer_role = SPIKE_ROLE_NONE,
        .attribute_continuation = SPIKE_MODE_ITEM,
        .tag_kind = 0,
        .argument = 0,
    };
    // Members and type names inside these groups aren't bound either. Only an attribute list's own items are names
    // whatever their spelling: `__aligned__(__alignof__(long long))` is C inside its call.
    frame->flags |= parent->flags & BUSTER_SPIKE_FRAME_NO_BIND;
    return frame;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's statement stack

static SpikeStatement* spike_statement_top(SpikeParser* parser)
{
    return parser->statement_count != 0 ? &parser->statements[parser->statement_count - 1] : 0;
}

static void spike_statement_pop(SpikeParser* parser)
{
    SpikeStatement* top = spike_statement_top(parser);
    // A for-statement's scope, which its clauses' declarations are bound in, ends with the statement.
    if (top->kind == SPIKE_KIND_FOR && top->extra != BUSTER_SPIKE_NONE)
    {
        spike_parser_restore(parser, top->extra);
    }
    parser->statement_count -= 1;
}

// A statement just completed: so did every statement whose head owned it, up to an if, which an else may still
// continue, or a do, which its while continues.
static void spike_statement_complete(SpikeParser* parser)
{
    bool completing = true;
    while (completing)
    {
        SpikeStatement* top = spike_statement_top(parser);
        completing = top != 0 && top->state == SPIKE_STATEMENT_STARTED;
        if (completing && top->kind == SPIKE_KIND_IF)
        {
            top->state = SPIKE_STATEMENT_IF_DONE;
            completing = false;
        }
        else if (completing && top->kind == SPIKE_KIND_DO)
        {
            top->state = SPIKE_STATEMENT_DO_DONE;
            completing = false;
        }
        else if (completing)
        {
            spike_statement_pop(parser);
        }
    }
}

// Anything but an else ends the if-statements that were waiting for one.
static void spike_statement_flush(SpikeParser* parser)
{
    SpikeStatement* top = spike_statement_top(parser);
    while (top != 0 && top->state == SPIKE_STATEMENT_IF_DONE)
    {
        spike_statement_pop(parser);
        spike_statement_complete(parser);
        top = spike_statement_top(parser);
    }
}

// The head that owns the statement starting now, if one is waiting for it; it isn't waiting any more.
static uint32_t spike_statement_owner(SpikeParser* parser)
{
    uint32_t result = BUSTER_SPIKE_NONE;
    SpikeStatement* top = spike_statement_top(parser);
    if (top != 0 && top->state == SPIKE_STATEMENT_WAITING)
    {
        result = top->token;
        top->state = SPIKE_STATEMENT_STARTED;
    }
    return result;
}

static void spike_statement_open_block(SpikeParser* parser, bool statement)
{
    uint32_t owner = BUSTER_SPIKE_NONE;
    if (statement)
    {
        spike_statement_flush(parser);
        owner = spike_statement_owner(parser);
    }
    parser->statements[parser->statement_count] = (SpikeStatement){
        .token = BUSTER_SPIKE_NONE,
        .owner = owner,
        .extra = BUSTER_SPIKE_NONE,
        .state = SPIKE_STATEMENT_BLOCK,
        .kind = statement,
    };
    parser->statement_count += 1;
}

static void spike_statement_close_block(SpikeParser* parser, uint32_t closer)
{
    spike_statement_flush(parser);
    // C23 lets a label end a block; anything else left waiting is an error the frames have already reported.
    SpikeStatement* top = spike_statement_top(parser);
    while (top != 0 && top->state != SPIKE_STATEMENT_BLOCK)
    {
        spike_statement_pop(parser);
        top = spike_statement_top(parser);
    }
    if (top != 0)
    {
        const bool statement = top->kind != 0;
        if (top->owner != BUSTER_SPIKE_NONE)
        {
            parser->parents[closer] = top->owner;
        }
        parser->statement_count -= 1;
        if (statement)
        {
            spike_statement_complete(parser);
        }
    }
}

static void spike_statement_terminator(SpikeParser* parser, uint32_t terminator)
{
    spike_statement_flush(parser);
    SpikeStatement* top = spike_statement_top(parser);
    if (top != 0 && top->state == SPIKE_STATEMENT_DO_WHILE)
    {
        parser->parents[top->token] = terminator;
        parser->parents[top->extra] = terminator;
        if (top->owner != BUSTER_SPIKE_NONE)
        {
            parser->parents[terminator] = top->owner;
        }
        parser->statement_count -= 1;
    }
    else
    {
        const uint32_t owner = spike_statement_owner(parser);
        if (owner != BUSTER_SPIKE_NONE)
        {
            parser->parents[terminator] = owner;
        }
    }
    spike_statement_complete(parser);
}

// A head: the `)` of if, while, for or switch, a label's or case's `:`, or `do`. `kind` is its keyword. A while after a
// do's body is its do's instead.
static void spike_statement_head(SpikeParser* parser, uint32_t head, uint8_t kind, uint32_t scope)
{
    spike_statement_flush(parser);
    SpikeStatement* top = spike_statement_top(parser);
    if (kind == SPIKE_KIND_WHILE && top != 0 && top->state == SPIKE_STATEMENT_DO_DONE)
    {
        top->state = SPIKE_STATEMENT_DO_WHILE;
        top->extra = head;
    }
    else
    {
        const uint32_t owner = spike_statement_owner(parser);
        if (owner != BUSTER_SPIKE_NONE && kind != SPIKE_KIND_DO)
        {
            parser->parents[head] = owner;
        }
        parser->statements[parser->statement_count] = (SpikeStatement){
            .token = head,
            .owner = kind == SPIKE_KIND_DO ? owner : BUSTER_SPIKE_NONE,
            .extra = scope,
            .state = SPIKE_STATEMENT_WAITING,
            .kind = kind,
        };
        parser->statement_count += 1;
    }
}

static void spike_statement_else(SpikeParser* parser, uint32_t token)
{
    SpikeStatement* top = spike_statement_top(parser);
    if (top != 0 && top->state == SPIKE_STATEMENT_IF_DONE)
    {
        parser->parents[token] = top->token;
        *top = (SpikeStatement){
            .token = token,
            .owner = BUSTER_SPIKE_NONE,
            .extra = BUSTER_SPIKE_NONE,
            .state = SPIKE_STATEMENT_WAITING,
            .kind = SPIKE_KIND_ELSE,
        };
    }
    else
    {
        spike_parser_error(parser, token, "else without if");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's precedence stacks

static void spike_parser_resolve(SpikeParser* parser, uint32_t token, uint32_t right)
{
    const uint8_t role = parser->roles[token];
    const uint32_t left = parser->lefts[token];
    uint32_t parent;
    if (spike_role_rules[role] == SPIKE_RULE_RIGHT)
    {
        parent = right;
    }
    else
    {
        const uint32_t left_depth = left != BUSTER_SPIKE_NONE ? parser->depths[left] : 0;
        const uint8_t left_level = left != BUSTER_SPIKE_NONE ? spike_role_levels[parser->roles[left]] : 0;
        const uint32_t right_depth = right != BUSTER_SPIKE_NONE ? parser->depths[right] : 0;
        const uint8_t right_level = right != BUSTER_SPIKE_NONE ? spike_role_levels[parser->roles[right]] : 0;
        parent = spike_select(left, left_depth, left_level, right, right_depth, right_level);
    }
    // The statement stack has already set the parents of what a head owns.
    if (parser->parents[token] == BUSTER_SPIKE_NONE)
    {
        parser->parents[token] = parent;
    }
}

// Adds token `token` with its role and depth to the tree.
static void spike_parser_emit(SpikeParser* parser, uint32_t token, uint8_t role, uint32_t depth)
{
    parser->roles[token] = role;
    parser->depths[token] = depth;
    parser->parents[token] = BUSTER_SPIKE_NONE;
    const uint32_t base = depth * BUSTER_SPIKE_LEVELS;
    const uint32_t key = base + spike_role_levels[role];
    const uint8_t rule = spike_role_rules[role];
    // The tokens this one ends the right search of.
    while (parser->right_count != 0 && parser->right_thresholds[parser->right_count - 1] > key)
    {
        parser->right_count -= 1;
        spike_parser_resolve(parser, parser->right_positions[parser->right_count], token);
    }
    // L, then this token replaces every candidate it hides.
    const uint32_t left_threshold = base + spike_role_left_thresholds[role];
    while (parser->left_count != 0 && parser->left_keys[parser->left_count - 1] >= left_threshold)
    {
        parser->left_count -= 1;
    }
    const uint32_t left = parser->left_count != 0 ? parser->left_positions[parser->left_count - 1] : BUSTER_SPIKE_NONE;
    while (parser->left_count != 0 && parser->left_keys[parser->left_count - 1] >= key)
    {
        parser->left_count -= 1;
    }
    parser->left_positions[parser->left_count] = token;
    parser->left_keys[parser->left_count] = key;
    parser->left_count += 1;
    if (rule == SPIKE_RULE_LEFT)
    {
        parser->parents[token] = left;
    }
    else if (rule != SPIKE_RULE_OPENER)
    {
        parser->lefts[token] = left;
        parser->right_positions[parser->right_count] = token;
        parser->right_thresholds[parser->right_count] = base + spike_role_right_thresholds[role];
        parser->right_count += 1;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's grammar

// What a token does to the statement stack, after it is added to the tree.
typedef enum SpikeEvent
{
    SPIKE_EVENT_NONE,
    SPIKE_EVENT_OPEN_BLOCK,
    SPIKE_EVENT_OPEN_STATEMENT_BLOCK,
    SPIKE_EVENT_CLOSE_BLOCK,
    SPIKE_EVENT_TERMINATOR,
    SPIKE_EVENT_HEAD,
    SPIKE_EVENT_ELSE,
} SpikeEvent;

// One token's decision: its role, its depth, and its statement event.
typedef struct SpikeStep SpikeStep;
struct SpikeStep
{
    uint32_t depth;
    uint32_t scope;
    uint8_t role;
    uint8_t event;
    // The head's keyword, for SPIKE_EVENT_HEAD.
    uint8_t keyword;
    // Run the token through the frame's new mode.
    bool again;
};

static bool spike_frame_is_block(const SpikeFrame* frame)
{
    return frame->role == SPIKE_ROLE_OPEN_BLOCK || frame->role == SPIKE_ROLE_OPEN_BODY;
}

// Whether a comma in this frame separates declarators or is the comma operator (COMMA), or separates arguments,
// parameters, initializers or enumerators (SEPARATOR, below the specifiers).
static uint8_t spike_frame_comma_role(const SpikeFrame* frame)
{
    const uint8_t role = frame->role;
    const bool comma = role == SPIKE_ROLE_NONE || role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY || role == SPIKE_ROLE_OPEN_STRUCT ||
                       role == SPIKE_ROLE_OPEN_FOR;
    return comma ? SPIKE_ROLE_COMMA : SPIKE_ROLE_SEPARATOR;
}

static bool spike_closer_matches(uint8_t kind, uint8_t opener_role)
{
    bool result;
    if (kind == SPIKE_KIND_R_SQUARE)
    {
        result = opener_role == SPIKE_ROLE_OPEN_SUBSCRIPT;
    }
    else if (kind == SPIKE_KIND_R_BRACE)
    {
        result = opener_role == SPIKE_ROLE_OPEN_BLOCK || opener_role == SPIKE_ROLE_OPEN_BODY || opener_role == SPIKE_ROLE_OPEN_STRUCT ||
                 opener_role == SPIKE_ROLE_OPEN_ENUM || opener_role == SPIKE_ROLE_OPEN_INITIALIZER || opener_role == SPIKE_ROLE_OPEN_COMPOUND_BODY;
    }
    else
    {
        result = opener_role != SPIKE_ROLE_NONE && opener_role != SPIKE_ROLE_OPEN_SUBSCRIPT && opener_role != SPIKE_ROLE_OPEN_TERNARY &&
                 opener_role != SPIKE_ROLE_OPEN_CASE && !(kind == SPIKE_KIND_R_PAREN && (opener_role == SPIKE_ROLE_OPEN_BLOCK ||
                 opener_role == SPIKE_ROLE_OPEN_BODY || opener_role == SPIKE_ROLE_OPEN_STRUCT || opener_role == SPIKE_ROLE_OPEN_ENUM ||
                 opener_role == SPIKE_ROLE_OPEN_INITIALIZER || opener_role == SPIKE_ROLE_OPEN_COMPOUND_BODY));
    }
    return result;
}

// Closes the current frame with token `i` (a `)`, `]`, `}`, or the `:` of ? or case).
static void spike_parser_close(SpikeParser* parser, uint32_t i, SpikeStep* step)
{
    SpikeFrame* frame = &parser->frames[parser->frame_count - 1];
    const uint8_t kind = parser->kinds[i];
    const bool colon = kind == SPIKE_KIND_COLON;
    const bool matches = colon ? (frame->role == SPIKE_ROLE_OPEN_TERNARY || frame->role == SPIKE_ROLE_OPEN_CASE) : spike_closer_matches(kind, frame->role);
    if (!matches || parser->frame_count < 2)
    {
        spike_parser_error(parser, i, "unmatched closer");
    }
    else
    {
        uint8_t role = frame->closer_role;
        uint8_t continuation = frame->continuation;
        if (frame->role == SPIKE_ROLE_OPEN_CAST)
        {
            if (parser->kinds[i + 1] == SPIKE_KIND_L_BRACE)
            {
                parser->roles[frame->opener] = SPIKE_ROLE_OPEN_COMPOUND;
                role = SPIKE_ROLE_CLOSE_COMPOUND;
                continuation = SPIKE_MODE_COMPOUND_BODY;
            }
            else
            {
                continuation = SPIKE_MODE_OPERAND;
            }
        }
        if (frame->role == SPIKE_ROLE_OPEN_FUNCTION && parser->kinds[i + 1] == SPIKE_KIND_L_BRACE)
        {
            // A function definition's parameters stay bound in its body.
            parser->body_scope = frame->scope;
        }
        else if (frame->role == SPIKE_ROLE_OPEN_FOR)
        {
            step->scope = frame->scope;
        }
        else if (frame->scope != BUSTER_SPIKE_NONE)
        {
            spike_parser_restore(parser, frame->scope);
        }
        if (frame->role == SPIKE_ROLE_OPEN_CONDITION || frame->role == SPIKE_ROLE_OPEN_FOR || frame->role == SPIKE_ROLE_OPEN_CASE)
        {
            step->event = SPIKE_EVENT_HEAD;
            step->keyword = frame->role == SPIKE_ROLE_OPEN_CASE ? SPIKE_KIND_CASE : frame->keyword;
        }
        else if (frame->role == SPIKE_ROLE_OPEN_BLOCK || frame->role == SPIKE_ROLE_OPEN_BODY)
        {
            step->event = SPIKE_EVENT_CLOSE_BLOCK;
        }
        parser->parents[frame->opener] = i;
        parser->frame_count -= 1;
        parser->frames[parser->frame_count - 1].mode = continuation;
        step->role = role;
        step->depth = parser->frame_count - 1;
    }
}

// Opens a group with token `i`: the opener's depth is its parent's.
static SpikeFrame* spike_parser_open(SpikeParser* parser, uint32_t i, uint8_t role, uint8_t mode, uint8_t continuation, bool scope, SpikeStep* step)
{
    step->role = role;
    step->depth = parser->frame_count - 1;
    return spike_parser_push(parser, i, role, mode, continuation, scope);
}

static bool spike_kind_is_closer(uint8_t kind)
{
    return kind == SPIKE_KIND_R_PAREN || kind == SPIKE_KIND_R_SQUARE || kind == SPIKE_KIND_R_BRACE;
}

// The end of a declaration's `;`, or of an expression statement's, in any frame that holds them.
static void spike_parser_terminator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    step->role = SPIKE_ROLE_TERMINATOR;
    if (frame->role == SPIKE_ROLE_OPEN_FOR)
    {
        frame->mode = SPIKE_MODE_OPERAND;
    }
    else if (frame->role == SPIKE_ROLE_NONE || spike_frame_is_block(frame) || frame->role == SPIKE_ROLE_OPEN_STRUCT)
    {
        frame->mode = SPIKE_MODE_ITEM;
        step->event = spike_frame_is_block(frame) ? SPIKE_EVENT_TERMINATOR : SPIKE_EVENT_NONE;
    }
    else
    {
        spike_parser_error(parser, i, "unexpected ;");
    }
    frame->flags &= ~(BUSTER_SPIKE_FRAME_DECLARATION | BUSTER_SPIKE_FRAME_TYPEDEF | BUSTER_SPIKE_FRAME_TYPE_SEEN | BUSTER_SPIKE_FRAME_FUNCTION |
                      BUSTER_SPIKE_FRAME_AFTER_STAR);
}

static void spike_parser_item(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    const bool block = spike_frame_is_block(frame);
    if (spike_kind_is_closer(kind))
    {
        spike_parser_close(parser, i, step);
    }
    else if (group == SPIKE_ROLE_OPEN_ENUM)
    {
        if (kind == SPIKE_KIND_IDENTIFIER)
        {
            step->role = SPIKE_ROLE_OPERAND;
            spike_parser_bind(parser, i, SPIKE_MEANING_ORDINARY);
            frame->mode = SPIKE_MODE_OPERATOR;
        }
        else
        {
            spike_parser_error(parser, i, "expected an enumerator");
        }
    }
    else if (group == SPIKE_ROLE_OPEN_INITIALIZER || group == SPIKE_ROLE_OPEN_COMPOUND_BODY)
    {
        if (kind == SPIKE_KIND_L_BRACE)
        {
            spike_parser_open(parser, i, SPIKE_ROLE_OPEN_INITIALIZER, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, false, step);
        }
        else if (kind == SPIKE_KIND_PERIOD)
        {
            step->role = SPIKE_ROLE_MEMBER_OPERATOR;
            frame->mode = SPIKE_MODE_MEMBER;
        }
        else if (kind == SPIKE_KIND_L_SQUARE)
        {
            spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SUBSCRIPT, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
        }
        else
        {
            frame->mode = SPIKE_MODE_OPERAND;
            step->again = true;
        }
    }
    else if (group == SPIKE_ROLE_OPEN_KEYWORD)
    {
        const bool type_argument = frame->keyword == SPIKE_KIND_TYPE_ARGUMENT || (frame->keyword == SPIKE_KIND_GENERIC && frame->argument != 0);
        if (frame->keyword == SPIKE_KIND_GENERIC && frame->argument != 0 && kind == SPIKE_KIND_DEFAULT)
        {
            step->role = SPIKE_ROLE_OPERAND;
            frame->mode = SPIKE_MODE_ASSOCIATION;
        }
        else if (type_argument && spike_parser_starts_type(parser, i))
        {
            frame->mode = SPIKE_MODE_SPECIFIERS;
            frame->flags |= BUSTER_SPIKE_FRAME_NO_BIND;
            step->again = true;
        }
        else
        {
            frame->mode = SPIKE_MODE_OPERAND;
            step->again = true;
        }
    }
    else if (group == SPIKE_ROLE_OPEN_SIZEOF || group == SPIKE_ROLE_OPEN_SPECIFIER)
    {
        frame->mode = spike_parser_starts_type(parser, i) ? SPIKE_MODE_SPECIFIERS : SPIKE_MODE_OPERAND;
        frame->flags |= frame->mode == SPIKE_MODE_SPECIFIERS ? BUSTER_SPIKE_FRAME_NO_BIND : 0;
        step->again = true;
    }
    else if (group == SPIKE_ROLE_OPEN_FUNCTION)
    {
        if (kind == SPIKE_KIND_ELLIPSIS)
        {
            step->role = SPIKE_ROLE_OPERAND;
            frame->mode = SPIKE_MODE_OPERATOR;
        }
        else if (spike_parser_starts_declaration(parser, i))
        {
            frame->mode = SPIKE_MODE_SPECIFIERS;
            frame->flags |= BUSTER_SPIKE_FRAME_DECLARATION;
            step->again = true;
        }
        else if (kind == SPIKE_KIND_IDENTIFIER)
        {
            // A K&R identifier list.
            step->role = SPIKE_ROLE_OPERAND;
            frame->mode = SPIKE_MODE_OPERATOR;
        }
        else
        {
            spike_parser_error(parser, i, "expected a parameter");
        }
    }
    else if (group == SPIKE_ROLE_OPEN_FOR)
    {
        if (kind == SPIKE_KIND_SEMI)
        {
            spike_parser_terminator(parser, i, frame, step);
        }
        else
        {
            frame->mode = spike_parser_starts_declaration(parser, i) ? SPIKE_MODE_SPECIFIERS : SPIKE_MODE_OPERAND;
            frame->flags |= frame->mode == SPIKE_MODE_SPECIFIERS ? BUSTER_SPIKE_FRAME_DECLARATION : 0;
            step->again = true;
        }
    }
    else if (kind == SPIKE_KIND_SEMI)
    {
        spike_parser_terminator(parser, i, frame, step);
    }
    else if (kind == SPIKE_KIND_ASM && (block || group == SPIKE_ROLE_NONE))
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ASM_PAREN;
    }
    else if (kind == SPIKE_KIND_STATIC_ASSERT)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_KEYWORD_PAREN;
    }
    else if (spike_parser_starts_declaration(parser, i))
    {
        frame->mode = SPIKE_MODE_SPECIFIERS;
        frame->flags |= BUSTER_SPIKE_FRAME_DECLARATION;
        step->again = true;
    }
    else if (!block)
    {
        spike_parser_error(parser, i, group == SPIKE_ROLE_OPEN_STRUCT ? "expected a member" : "expected a declaration");
    }
    else if (kind == SPIKE_KIND_L_BRACE)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_BLOCK, SPIKE_MODE_ITEM, SPIKE_MODE_ITEM, true, step);
        step->event = SPIKE_EVENT_OPEN_STATEMENT_BLOCK;
    }
    else if (kind == SPIKE_KIND_IF || kind == SPIKE_KIND_WHILE || kind == SPIKE_KIND_SWITCH)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_CONDITION_PAREN;
    }
    else if (kind == SPIKE_KIND_FOR)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_FOR_PAREN;
    }
    else if (kind == SPIKE_KIND_DO)
    {
        step->role = SPIKE_ROLE_DO;
        step->event = SPIKE_EVENT_HEAD;
        step->keyword = SPIKE_KIND_DO;
    }
    else if (kind == SPIKE_KIND_ELSE)
    {
        step->role = SPIKE_ROLE_ELSE;
        step->event = SPIKE_EVENT_ELSE;
    }
    else if (kind == SPIKE_KIND_CASE)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_CASE, SPIKE_MODE_OPERAND, SPIKE_MODE_ITEM, false, step);
    }
    else if (kind == SPIKE_KIND_DEFAULT)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_CASE, SPIKE_MODE_DEFAULT, SPIKE_MODE_ITEM, false, step);
    }
    else if (kind == SPIKE_KIND_RETURN || kind == SPIKE_KIND_GOTO || kind == SPIKE_KIND_BREAK || kind == SPIKE_KIND_CONTINUE)
    {
        step->role = SPIKE_ROLE_RETURN;
        frame->mode = SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_IDENTIFIER && parser->kinds[i + 1] == SPIKE_KIND_COLON)
    {
        step->role = SPIKE_ROLE_OPERAND;
        frame->mode = SPIKE_MODE_LABEL;
    }
    else
    {
        frame->mode = SPIKE_MODE_OPERAND;
        step->again = true;
    }
}

static void spike_parser_specifiers(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == SPIKE_KIND_TYPE)
    {
        step->role = SPIKE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_SPIKE_FRAME_TYPE_SEEN;
    }
    else if (kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_STORAGE || kind == SPIKE_KIND_EXTENSION || kind == SPIKE_KIND_LABEL ||
             kind == SPIKE_KIND_CALLING_CONVENTION)
    {
        step->role = SPIKE_ROLE_SPECIFIER;
    }
    else if (kind == SPIKE_KIND_TYPEDEF)
    {
        step->role = SPIKE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_SPIKE_FRAME_TYPEDEF;
    }
    else if (kind == SPIKE_KIND_ATOMIC && parser->kinds[i + 1] != SPIKE_KIND_L_PAREN)
    {
        step->role = SPIKE_ROLE_SPECIFIER;
    }
    else if (kind == SPIKE_KIND_ATOMIC || kind == SPIKE_KIND_TYPEOF || kind == SPIKE_KIND_ALIGNAS)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->flags |= kind != SPIKE_KIND_ALIGNAS ? BUSTER_SPIKE_FRAME_TYPE_SEEN : 0;
        frame->mode = SPIKE_MODE_SPECIFIER_PAREN;
    }
    else if (kind == SPIKE_KIND_ATTRIBUTE)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = SPIKE_MODE_SPECIFIERS;
    }
    else if (kind == SPIKE_KIND_STRUCT || kind == SPIKE_KIND_ENUM)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->flags |= BUSTER_SPIKE_FRAME_TYPE_SEEN;
        frame->mode = SPIKE_MODE_TAG;
        frame->tag_kind = kind;
    }
    else if (kind == SPIKE_KIND_IDENTIFIER && spike_parser_is_typedef_name(parser, i) && (frame->flags & BUSTER_SPIKE_FRAME_TYPE_SEEN) == 0)
    {
        step->role = SPIKE_ROLE_SPECIFIER;
        frame->flags |= BUSTER_SPIKE_FRAME_TYPE_SEEN;
    }
    else
    {
        frame->mode = SPIKE_MODE_DECLARATOR;
        step->again = true;
    }
}

static void spike_parser_tag(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    if (kind == SPIKE_KIND_ATTRIBUTE)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT;
        frame->attribute_continuation = SPIKE_MODE_TAG;
    }
    else if (kind == SPIKE_KIND_IDENTIFIER && frame->mode == SPIKE_MODE_TAG)
    {
        const bool body = parser->kinds[i + 1] == SPIKE_KIND_L_BRACE;
        step->role = body ? SPIKE_ROLE_LEAF_RIGHT : SPIKE_ROLE_TAG;
        frame->mode = body ? SPIKE_MODE_TAG_BODY : SPIKE_MODE_SPECIFIERS;
    }
    else if (kind == SPIKE_KIND_L_BRACE)
    {
        const bool is_enum = frame->tag_kind == SPIKE_KIND_ENUM;
        SpikeFrame* body = spike_parser_open(parser, i, is_enum ? SPIKE_ROLE_OPEN_ENUM : SPIKE_ROLE_OPEN_STRUCT, SPIKE_MODE_ITEM, SPIKE_MODE_SPECIFIERS, false, step);
        body->flags |= is_enum ? 0 : BUSTER_SPIKE_FRAME_NO_BIND;
    }
    else
    {
        spike_parser_error(parser, i, "expected a tag or a body");
    }
}

static void spike_parser_declarator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const bool after_star = (frame->flags & BUSTER_SPIKE_FRAME_AFTER_STAR) != 0;
    if (kind == SPIKE_KIND_STAR)
    {
        step->role = SPIKE_ROLE_PREFIX;
        frame->flags |= BUSTER_SPIKE_FRAME_AFTER_STAR;
    }
    else if ((kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_CALLING_CONVENTION) && after_star)
    {
        step->role = SPIKE_ROLE_QUALIFIER_LEFT;
    }
    else if (kind == SPIKE_KIND_CALLING_CONVENTION)
    {
        // `void (__cdecl *f)(void)`: a calling convention before the declarator in its group.
        step->role = SPIKE_ROLE_SPECIFIER;
    }
    else if (kind == SPIKE_KIND_ATTRIBUTE)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = after_star ? SPIKE_ROLE_CLOSE_ATTRIBUTE_LEFT : SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        frame->attribute_continuation = SPIKE_MODE_DECLARATOR;
    }
    else if (kind == SPIKE_KIND_L_PAREN)
    {
        const uint8_t next = parser->kinds[i + 1];
        const bool function = spike_parser_starts_type(parser, i + 1) || next == SPIKE_KIND_R_PAREN || next == SPIKE_KIND_ELLIPSIS;
        frame->flags &= ~BUSTER_SPIKE_FRAME_AFTER_STAR;
        if (function)
        {
            spike_parser_open(parser, i, SPIKE_ROLE_OPEN_FUNCTION, SPIKE_MODE_ITEM, SPIKE_MODE_AFTER_DECLARATOR, true, step);
            frame->flags |= BUSTER_SPIKE_FRAME_FUNCTION;
        }
        else
        {
            const uint8_t inherited = frame->flags & (BUSTER_SPIKE_FRAME_TYPEDEF | BUSTER_SPIKE_FRAME_DECLARATION);
            SpikeFrame* group = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_GROUP, SPIKE_MODE_DECLARATOR, SPIKE_MODE_AFTER_DECLARATOR, false, step);
            group->flags |= inherited;
        }
    }
    else if (kind == SPIKE_KIND_IDENTIFIER)
    {
        step->role = SPIKE_ROLE_OPERAND;
        if (spike_parser_binds(parser))
        {
            spike_parser_bind(parser, i, (frame->flags & BUSTER_SPIKE_FRAME_TYPEDEF) ? SPIKE_MEANING_TYPEDEF : SPIKE_MEANING_ORDINARY);
        }
        frame->flags &= ~(BUSTER_SPIKE_FRAME_AFTER_STAR | BUSTER_SPIKE_FRAME_FUNCTION);
        frame->mode = SPIKE_MODE_AFTER_DECLARATOR;
    }
    else
    {
        frame->flags &= ~BUSTER_SPIKE_FRAME_AFTER_STAR;
        frame->mode = SPIKE_MODE_AFTER_DECLARATOR;
        step->again = true;
    }
}

static void spike_parser_after_declarator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t group = frame->role;
    if (kind == SPIKE_KIND_L_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_FUNCTION, SPIKE_MODE_ITEM, SPIKE_MODE_AFTER_DECLARATOR, true, step);
        frame->flags |= BUSTER_SPIKE_FRAME_FUNCTION;
    }
    else if (kind == SPIKE_KIND_L_SQUARE)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SUBSCRIPT, SPIKE_MODE_OPERAND, SPIKE_MODE_AFTER_DECLARATOR, false, step);
        frame->flags &= ~BUSTER_SPIKE_FRAME_FUNCTION;
    }
    else if (kind == SPIKE_KIND_ATTRIBUTE)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = SPIKE_ROLE_CLOSE_ATTRIBUTE;
        frame->attribute_continuation = SPIKE_MODE_AFTER_DECLARATOR;
    }
    else if (kind == SPIKE_KIND_ASM)
    {
        // An asm label, which renames the declared symbol.
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = SPIKE_ROLE_CLOSE_ATTRIBUTE;
        frame->attribute_continuation = SPIKE_MODE_AFTER_DECLARATOR;
    }
    else if (kind == SPIKE_KIND_EQUAL)
    {
        step->role = SPIKE_ROLE_ASSIGN;
        frame->mode = SPIKE_MODE_INITIALIZER;
    }
    else if (kind == SPIKE_KIND_COMMA)
    {
        step->role = spike_frame_comma_role(frame);
        frame->mode = step->role == SPIKE_ROLE_COMMA ? SPIKE_MODE_DECLARATOR : SPIKE_MODE_ITEM;
        frame->flags &= ~(BUSTER_SPIKE_FRAME_FUNCTION | BUSTER_SPIKE_FRAME_AFTER_STAR);
        if (step->role == SPIKE_ROLE_SEPARATOR)
        {
            frame->flags &= ~(BUSTER_SPIKE_FRAME_TYPE_SEEN | BUSTER_SPIKE_FRAME_TYPEDEF);
            frame->argument += 1;
        }
    }
    else if (kind == SPIKE_KIND_SEMI)
    {
        spike_parser_terminator(parser, i, frame, step);
    }
    else if (kind == SPIKE_KIND_COLON && group == SPIKE_ROLE_OPEN_STRUCT)
    {
        step->role = SPIKE_ROLE_BITFIELD;
        frame->mode = SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_COLON && group == SPIKE_ROLE_OPEN_KEYWORD && frame->keyword == SPIKE_KIND_GENERIC)
    {
        step->role = SPIKE_ROLE_ASSOCIATION;
        frame->mode = SPIKE_MODE_OPERAND;
        frame->flags &= ~(BUSTER_SPIKE_FRAME_TYPE_SEEN | BUSTER_SPIKE_FRAME_NO_BIND);
    }
    else if (kind == SPIKE_KIND_L_BRACE && (group == SPIKE_ROLE_NONE || spike_frame_is_block(frame)) && (frame->flags & BUSTER_SPIKE_FRAME_FUNCTION))
    {
        SpikeFrame* body = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_BODY, SPIKE_MODE_ITEM, SPIKE_MODE_ITEM, false, step);
        body->scope = parser->body_scope;
        parser->body_scope = BUSTER_SPIKE_NONE;
        frame->flags &= ~(BUSTER_SPIKE_FRAME_DECLARATION | BUSTER_SPIKE_FRAME_TYPEDEF | BUSTER_SPIKE_FRAME_TYPE_SEEN | BUSTER_SPIKE_FRAME_FUNCTION);
        step->event = SPIKE_EVENT_OPEN_BLOCK;
    }
    else if (spike_kind_is_closer(kind))
    {
        spike_parser_close(parser, i, step);
    }
    else
    {
        spike_parser_error(parser, i, "unexpected token after a declarator");
    }
}

// In an expression, where an operand comes next.
static void spike_parser_operand(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const bool attribute = (frame->flags & BUSTER_SPIKE_FRAME_ATTRIBUTE) != 0;
    if (kind == SPIKE_KIND_IDENTIFIER || kind == SPIKE_KIND_NUMBER || kind == SPIKE_KIND_CHARACTER || kind == SPIKE_KIND_STRING ||
        (attribute && kind >= SPIKE_KIND_TYPE))
    {
        step->role = SPIKE_ROLE_OPERAND;
        frame->mode = SPIKE_MODE_OPERATOR;
        if (!attribute && spike_parser_is_typedef_name(parser, i))
        {
            spike_parser_error(parser, i, "a type name where an expression is expected");
        }
    }
    else if (kind == SPIKE_KIND_L_PAREN && spike_parser_starts_type(parser, i + 1) && !attribute)
    {
        SpikeFrame* cast = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_CAST, SPIKE_MODE_SPECIFIERS, SPIKE_MODE_OPERAND, false, step);
        cast->flags |= BUSTER_SPIKE_FRAME_NO_BIND;
    }
    else if (kind == SPIKE_KIND_L_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_GROUP, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
    }
    else if (kind == SPIKE_KIND_L_BRACE && frame->role == SPIKE_ROLE_OPEN_GROUP && parser->kinds[i - 1] == SPIKE_KIND_L_PAREN)
    {
        // A statement expression.
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_BLOCK, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, true, step);
        step->event = SPIKE_EVENT_OPEN_BLOCK;
    }
    else if (kind == SPIKE_KIND_MINUS || kind == SPIKE_KIND_PLUS || kind == SPIKE_KIND_EXCLAIM || kind == SPIKE_KIND_TILDE || kind == SPIKE_KIND_STAR ||
             kind == SPIKE_KIND_AMP || kind == SPIKE_KIND_PLUSPLUS || kind == SPIKE_KIND_MINUSMINUS || kind == SPIKE_KIND_AMPAMP ||
             kind == SPIKE_KIND_EXTENSION || kind == SPIKE_KIND_REAL_IMAG)
    {
        step->role = SPIKE_ROLE_PREFIX;
    }
    else if (kind == SPIKE_KIND_SIZEOF)
    {
        step->role = SPIKE_ROLE_PREFIX;
        frame->mode = parser->kinds[i + 1] == SPIKE_KIND_L_PAREN ? SPIKE_MODE_SIZEOF_PAREN : SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_TYPE_ARGUMENT || kind == SPIKE_KIND_GENERIC)
    {
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_KEYWORD_PAREN;
    }
    else if (kind == SPIKE_KIND_PERIOD && (frame->role == SPIKE_ROLE_OPEN_INITIALIZER || frame->role == SPIKE_ROLE_OPEN_COMPOUND_BODY))
    {
        step->role = SPIKE_ROLE_MEMBER_OPERATOR;
        frame->mode = SPIKE_MODE_MEMBER;
    }
    else if (kind == SPIKE_KIND_L_SQUARE && (frame->role == SPIKE_ROLE_OPEN_INITIALIZER || frame->role == SPIKE_ROLE_OPEN_COMPOUND_BODY || (frame->flags & BUSTER_SPIKE_FRAME_ASM)))
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SUBSCRIPT, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
    }
    else if ((kind == SPIKE_KIND_STORAGE || kind == SPIKE_KIND_QUALIFIER) && frame->role == SPIKE_ROLE_OPEN_SUBSCRIPT)
    {
        // `[static 10]` and `[const]` in an array parameter.
        step->role = SPIKE_ROLE_SPECIFIER;
    }
    else if (spike_kind_is_closer(kind))
    {
        spike_parser_close(parser, i, step);
    }
    else if (kind == SPIKE_KIND_SEMI)
    {
        spike_parser_terminator(parser, i, frame, step);
    }
    else if (kind == SPIKE_KIND_COLON && (frame->role == SPIKE_ROLE_OPEN_TERNARY || frame->role == SPIKE_ROLE_OPEN_CASE))
    {
        // GNU's `a ?: b`.
        spike_parser_close(parser, i, step);
    }
    else if (kind == SPIKE_KIND_COLON && (frame->flags & BUSTER_SPIKE_FRAME_ASM))
    {
        step->role = SPIKE_ROLE_ASM_COLON;
    }
    else
    {
        spike_parser_error(parser, i, "expected an expression");
    }
}

static uint8_t spike_binary_role(uint8_t kind)
{
    uint8_t result = SPIKE_ROLE_NONE;
    switch (kind)
    {
        case SPIKE_KIND_STAR:
        case SPIKE_KIND_SLASH:
        case SPIKE_KIND_PERCENT: result = SPIKE_ROLE_MULTIPLICATIVE; break;
        case SPIKE_KIND_PLUS:
        case SPIKE_KIND_MINUS: result = SPIKE_ROLE_ADDITIVE; break;
        case SPIKE_KIND_LESSLESS:
        case SPIKE_KIND_GREATERGREATER: result = SPIKE_ROLE_SHIFT; break;
        case SPIKE_KIND_LESS:
        case SPIKE_KIND_GREATER:
        case SPIKE_KIND_LESSEQUAL:
        case SPIKE_KIND_GREATEREQUAL: result = SPIKE_ROLE_RELATIONAL; break;
        case SPIKE_KIND_EQUALEQUAL:
        case SPIKE_KIND_EXCLAIMEQUAL: result = SPIKE_ROLE_EQUALITY; break;
        case SPIKE_KIND_AMP: result = SPIKE_ROLE_BITWISE_AND; break;
        case SPIKE_KIND_CARET: result = SPIKE_ROLE_BITWISE_XOR; break;
        case SPIKE_KIND_PIPE: result = SPIKE_ROLE_BITWISE_OR; break;
        case SPIKE_KIND_AMPAMP: result = SPIKE_ROLE_LOGICAL_AND; break;
        case SPIKE_KIND_PIPEPIPE: result = SPIKE_ROLE_LOGICAL_OR; break;
        case SPIKE_KIND_EQUAL:
        case SPIKE_KIND_STAREQUAL:
        case SPIKE_KIND_SLASHEQUAL:
        case SPIKE_KIND_PERCENTEQUAL:
        case SPIKE_KIND_PLUSEQUAL:
        case SPIKE_KIND_MINUSEQUAL:
        case SPIKE_KIND_LESSLESSEQUAL:
        case SPIKE_KIND_GREATERGREATEREQUAL:
        case SPIKE_KIND_AMPEQUAL:
        case SPIKE_KIND_CARETEQUAL:
        case SPIKE_KIND_PIPEEQUAL: result = SPIKE_ROLE_ASSIGN; break;
        default: break;
    }
    return result;
}

// In an expression, after an operand.
static void spike_parser_operator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t kind = parser->kinds[i];
    const uint8_t binary = spike_binary_role(kind);
    const uint8_t group = frame->role;
    if (binary != SPIKE_ROLE_NONE)
    {
        step->role = binary;
        // A designator's `=` in an initializer list may be followed by a braced initializer.
        const bool initializer = binary == SPIKE_ROLE_ASSIGN && (group == SPIKE_ROLE_OPEN_INITIALIZER || group == SPIKE_ROLE_OPEN_COMPOUND_BODY);
        frame->mode = initializer ? SPIKE_MODE_INITIALIZER : SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_QUESTION)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_TERNARY, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERAND, false, step);
    }
    else if (kind == SPIKE_KIND_COLON && (group == SPIKE_ROLE_OPEN_TERNARY || group == SPIKE_ROLE_OPEN_CASE))
    {
        spike_parser_close(parser, i, step);
    }
    else if (kind == SPIKE_KIND_COLON && (frame->flags & BUSTER_SPIKE_FRAME_ASM))
    {
        step->role = SPIKE_ROLE_ASM_COLON;
        frame->mode = SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_L_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_CALL, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
    }
    else if (kind == SPIKE_KIND_L_SQUARE)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SUBSCRIPT, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
    }
    else if (kind == SPIKE_KIND_PERIOD || kind == SPIKE_KIND_ARROW)
    {
        step->role = SPIKE_ROLE_MEMBER_OPERATOR;
        frame->mode = SPIKE_MODE_MEMBER;
    }
    else if (kind == SPIKE_KIND_PLUSPLUS || kind == SPIKE_KIND_MINUSMINUS)
    {
        step->role = SPIKE_ROLE_POSTFIX;
    }
    else if (kind == SPIKE_KIND_STRING && parser->kinds[i - 1] == SPIKE_KIND_STRING)
    {
        step->role = SPIKE_ROLE_STRING_NEXT;
    }
    else if (kind == SPIKE_KIND_STRING && (frame->flags & BUSTER_SPIKE_FRAME_ASM))
    {
        // An asm operand's constraint after its symbolic name: `[name] "=r" (x)`.
        step->role = SPIKE_ROLE_OPERAND;
    }
    else if (kind == SPIKE_KIND_ELLIPSIS)
    {
        step->role = SPIKE_ROLE_RANGE;
        frame->mode = SPIKE_MODE_OPERAND;
    }
    else if (kind == SPIKE_KIND_ATTRIBUTE)
    {
        // After an enumerator, or a label's statement attribute.
        step->role = SPIKE_ROLE_LEAF_RIGHT;
        frame->mode = SPIKE_MODE_ATTRIBUTE_PAREN;
        frame->attribute_closer_role = SPIKE_ROLE_CLOSE_ATTRIBUTE;
        frame->attribute_continuation = SPIKE_MODE_OPERATOR;
    }
    else if (kind == SPIKE_KIND_COMMA)
    {
        step->role = spike_frame_comma_role(frame);
        if (frame->flags & BUSTER_SPIKE_FRAME_DECLARATION)
        {
            // The end of an initializer: the next declarator, or the next parameter.
            frame->mode = step->role == SPIKE_ROLE_COMMA ? SPIKE_MODE_DECLARATOR : SPIKE_MODE_ITEM;
            frame->flags &= ~(BUSTER_SPIKE_FRAME_FUNCTION | BUSTER_SPIKE_FRAME_AFTER_STAR);
        }
        else
        {
            const bool items = group == SPIKE_ROLE_OPEN_ENUM || group == SPIKE_ROLE_OPEN_INITIALIZER || group == SPIKE_ROLE_OPEN_COMPOUND_BODY ||
                               group == SPIKE_ROLE_OPEN_FUNCTION || group == SPIKE_ROLE_OPEN_KEYWORD;
            frame->mode = items ? SPIKE_MODE_ITEM : SPIKE_MODE_OPERAND;
            frame->argument += 1;
            if (items)
            {
                frame->flags &= ~(BUSTER_SPIKE_FRAME_TYPE_SEEN | BUSTER_SPIKE_FRAME_TYPEDEF | BUSTER_SPIKE_FRAME_NO_BIND);
                frame->flags |= parser->frames[parser->frame_count - 2].flags & BUSTER_SPIKE_FRAME_NO_BIND;
            }
        }
    }
    else if (kind == SPIKE_KIND_SEMI)
    {
        spike_parser_terminator(parser, i, frame, step);
    }
    else if (spike_kind_is_closer(kind))
    {
        spike_parser_close(parser, i, step);
    }
    else
    {
        spike_parser_error(parser, i, "expected an operator");
    }
}

// The token after a keyword that a group follows.
static void spike_parser_paren(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
{
    const uint8_t mode = frame->mode;
    const uint8_t kind = parser->kinds[i];
    if (mode == SPIKE_MODE_ASM_PAREN && (kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_STORAGE || kind == SPIKE_KIND_GOTO))
    {
        // asm volatile, asm inline, asm goto.
        step->role = SPIKE_ROLE_LEAF_RIGHT;
    }
    else if (kind != SPIKE_KIND_L_PAREN)
    {
        spike_parser_error(parser, i, "expected (");
    }
    else if (mode == SPIKE_MODE_CONDITION_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_CONDITION, SPIKE_MODE_OPERAND, SPIKE_MODE_ITEM, false, step);
    }
    else if (mode == SPIKE_MODE_FOR_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_FOR, SPIKE_MODE_ITEM, SPIKE_MODE_ITEM, true, step);
    }
    else if (mode == SPIKE_MODE_KEYWORD_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_KEYWORD, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, false, step);
    }
    else if (mode == SPIKE_MODE_SIZEOF_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SIZEOF, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, false, step);
    }
    else if (mode == SPIKE_MODE_SPECIFIER_PAREN)
    {
        spike_parser_open(parser, i, SPIKE_ROLE_OPEN_SPECIFIER, SPIKE_MODE_ITEM, SPIKE_MODE_SPECIFIERS, false, step);
    }
    else if (mode == SPIKE_MODE_ASM_PAREN)
    {
        SpikeFrame* group = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_ASM, SPIKE_MODE_OPERAND, SPIKE_MODE_OPERATOR, false, step);
        group->flags |= BUSTER_SPIKE_FRAME_ASM;
    }
    else
    {
        // An attribute: `__attribute__((list))`, or `__declspec(list)` and an asm label's `("name")`.
        const uint8_t closer_role = frame->attribute_closer_role;
        const uint8_t continuation = frame->attribute_continuation;
        const bool list = parser->kinds[i - 1] == SPIKE_KIND_ATTRIBUTE && parser->kinds[i + 1] == SPIKE_KIND_L_PAREN;
        SpikeFrame* group = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_ATTRIBUTE, list ? SPIKE_MODE_ATTRIBUTE_LIST : SPIKE_MODE_OPERAND, continuation, false, step);
        group->closer_role = closer_role;
        group->flags |= list ? 0 : BUSTER_SPIKE_FRAME_ATTRIBUTE;
    }
}

static void spike_parser_token(SpikeParser* parser, uint32_t i)
{
    const uint8_t kind = parser->kinds[i];
    SpikeStep step = {
        .depth = parser->frame_count - 1,
        .scope = BUSTER_SPIKE_NONE,
        .role = SPIKE_ROLE_ERROR,
        .event = SPIKE_EVENT_NONE,
        .keyword = 0,
        .again = true,
    };
    while (step.again && parser->error_token == BUSTER_SPIKE_NONE)
    {
        step.again = false;
        step.depth = parser->frame_count - 1;
        SpikeFrame* frame = &parser->frames[parser->frame_count - 1];
        switch (frame->mode)
        {
            case SPIKE_MODE_ITEM: spike_parser_item(parser, i, frame, &step); break;
            case SPIKE_MODE_SPECIFIERS: spike_parser_specifiers(parser, i, frame, &step); break;
            case SPIKE_MODE_TAG:
            case SPIKE_MODE_TAG_BODY: spike_parser_tag(parser, i, frame, &step); break;
            case SPIKE_MODE_DECLARATOR: spike_parser_declarator(parser, i, frame, &step); break;
            case SPIKE_MODE_AFTER_DECLARATOR: spike_parser_after_declarator(parser, i, frame, &step); break;
            case SPIKE_MODE_INITIALIZER:
                if (kind == SPIKE_KIND_L_BRACE)
                {
                    spike_parser_open(parser, i, SPIKE_ROLE_OPEN_INITIALIZER, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, false, &step);
                }
                else
                {
                    frame->mode = SPIKE_MODE_OPERAND;
                    step.again = true;
                }
                break;
            case SPIKE_MODE_OPERAND: spike_parser_operand(parser, i, frame, &step); break;
            case SPIKE_MODE_OPERATOR: spike_parser_operator(parser, i, frame, &step); break;
            case SPIKE_MODE_MEMBER:
                if (kind == SPIKE_KIND_IDENTIFIER)
                {
                    step.role = SPIKE_ROLE_MEMBER;
                    frame->mode = SPIKE_MODE_OPERATOR;
                }
                else
                {
                    spike_parser_error(parser, i, "expected a member name");
                }
                break;
            case SPIKE_MODE_LABEL:
                step.role = SPIKE_ROLE_LABEL;
                step.event = SPIKE_EVENT_HEAD;
                step.keyword = SPIKE_KIND_COLON;
                frame->mode = SPIKE_MODE_ITEM;
                break;
            case SPIKE_MODE_DEFAULT:
                if (kind == SPIKE_KIND_COLON)
                {
                    spike_parser_close(parser, i, &step);
                }
                else
                {
                    spike_parser_error(parser, i, "expected : after default");
                }
                break;
            case SPIKE_MODE_ASSOCIATION:
                if (kind == SPIKE_KIND_COLON)
                {
                    step.role = SPIKE_ROLE_ASSOCIATION;
                    frame->mode = SPIKE_MODE_OPERAND;
                }
                else
                {
                    spike_parser_error(parser, i, "expected : in _Generic");
                }
                break;
            case SPIKE_MODE_ATTRIBUTE_LIST:
                if (kind == SPIKE_KIND_L_PAREN)
                {
                    SpikeFrame* list = spike_parser_open(parser, i, SPIKE_ROLE_OPEN_ATTRIBUTE_LIST, SPIKE_MODE_OPERAND, SPIKE_MODE_ATTRIBUTE_END, false, &step);
                    list->flags |= BUSTER_SPIKE_FRAME_ATTRIBUTE;
                }
                else
                {
                    spike_parser_error(parser, i, "expected ( in __attribute__");
                }
                break;
            case SPIKE_MODE_ATTRIBUTE_END:
                spike_parser_close(parser, i, &step);
                break;
            case SPIKE_MODE_COMPOUND_BODY:
                if (kind == SPIKE_KIND_L_BRACE)
                {
                    spike_parser_open(parser, i, SPIKE_ROLE_OPEN_COMPOUND_BODY, SPIKE_MODE_ITEM, SPIKE_MODE_OPERATOR, false, &step);
                }
                else
                {
                    spike_parser_error(parser, i, "expected { after a compound literal's type");
                }
                break;
            default: spike_parser_paren(parser, i, frame, &step); break;
        }
    }
    if (parser->error_token == BUSTER_SPIKE_NONE)
    {
        spike_parser_emit(parser, i, step.role, step.depth);
        switch (step.event)
        {
            case SPIKE_EVENT_OPEN_BLOCK: spike_statement_open_block(parser, false); break;
            case SPIKE_EVENT_OPEN_STATEMENT_BLOCK: spike_statement_open_block(parser, true); break;
            case SPIKE_EVENT_CLOSE_BLOCK: spike_statement_close_block(parser, i); break;
            case SPIKE_EVENT_TERMINATOR: spike_statement_terminator(parser, i); break;
            case SPIKE_EVENT_HEAD:
                // Only heads in blocks: a case's `:` closes its group into the block it is in.
                if (spike_frame_is_block(&parser->frames[parser->frame_count - 1]))
                {
                    spike_statement_head(parser, i, step.keyword, step.scope);
                }
                break;
            case SPIKE_EVENT_ELSE: spike_statement_else(parser, i); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar parser's entry point

// The parser's output, by token.
typedef struct SpikeTree SpikeTree;
struct SpikeTree
{
    uint8_t* roles;
    // The node depth: a group's opener and closer are at the depth outside it, its contents one deeper.
    uint32_t* depths;
    uint32_t* parents;
};

// The bindings a parse starts from: a meaning for every name id, all SPIKE_MEANING_ORDINARY, which every parse leaves
// as it found them.
typedef struct SpikeMeanings SpikeMeanings;
struct SpikeMeanings
{
    uint8_t* meanings;
    uint32_t capacity;
};

typedef struct SpikeParseResult SpikeParseResult;
struct SpikeParseResult
{
    // The first token the parser couldn't handle, or BUSTER_SPIKE_NONE if it parsed everything.
    uint32_t error_token;
    const char* error_message;
};

// Parses tokens [0, count) into `tree`, whose arrays have count entries, with the scratch arena's memory.
static SpikeParseResult spike_parse_scalar(const uint8_t* kinds, const uint32_t* names, uint32_t count, SpikeMeanings* meanings, SpikeTree* tree, Arena* scratch)
{
    SpikeParseResult result = { .error_token = BUSTER_SPIKE_NONE, .error_message = 0 };
    const uint64_t mark = scratch->length;
    const uint64_t slots = (uint64_t)count + 2;
    SpikeParser parser = {
        .kinds = kinds,
        .names = names,
        .count = count,
        .roles = tree->roles,
        .depths = tree->depths,
        .parents = tree->parents,
        .frames = BUSTER_ARENA_PUSH_ARRAY(scratch, SpikeFrame, slots),
        .frame_count = 1,
        .left_positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .left_keys = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .left_count = 0,
        .right_positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .right_thresholds = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .right_count = 0,
        .lefts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .meanings = meanings->meanings,
        .log_names = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots),
        .log_meanings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots),
        .log_count = 0,
        .body_scope = BUSTER_SPIKE_NONE,
        .statements = BUSTER_ARENA_PUSH_ARRAY(scratch, SpikeStatement, slots),
        .statement_count = 0,
        .error_token = BUSTER_SPIKE_NONE,
        .error_message = 0,
    };
    if (parser.frames == 0 || parser.left_keys == 0 || parser.right_thresholds == 0 || parser.lefts == 0 || parser.log_meanings == 0 || parser.statements == 0)
    {
        result.error_token = 0;
        result.error_message = "out of memory";
    }
    else
    {
        parser.frames[0] = (SpikeFrame){
            .opener = BUSTER_SPIKE_NONE,
            .scope = 0,
            .role = SPIKE_ROLE_NONE,
            .mode = SPIKE_MODE_ITEM,
            .flags = 0,
            .closer_role = SPIKE_ROLE_NONE,
            .keyword = 0,
            .continuation = SPIKE_MODE_ITEM,
            .attribute_closer_role = SPIKE_ROLE_NONE,
            .attribute_continuation = SPIKE_MODE_ITEM,
            .tag_kind = 0,
            .argument = 0,
        };
        for (uint32_t i = 0; i < count && parser.error_token == BUSTER_SPIKE_NONE; i += 1)
        {
            spike_parser_token(&parser, i);
        }
        if (parser.error_token == BUSTER_SPIKE_NONE && (parser.frame_count != 1 || parser.frames[0].mode != SPIKE_MODE_ITEM))
        {
            spike_parser_error(&parser, count, "unexpected end of input");
        }
        // What is still waiting has no R.
        while (parser.error_token == BUSTER_SPIKE_NONE && parser.right_count != 0)
        {
            parser.right_count -= 1;
            spike_parser_resolve(&parser, parser.right_positions[parser.right_count], BUSTER_SPIKE_NONE);
        }
        spike_parser_restore(&parser, 0);
        result.error_token = parser.error_token;
        result.error_message = parser.error_message;
    }
    arena_pop_to(scratch, mark);
    return result;
}
