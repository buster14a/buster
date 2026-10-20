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

// The kernels read virtual kinds: the parser kinds, and SPIKE_VKIND_TYPE_NAME for an identifier that a typedef
// declaration of the file declares (K3, spike_typedefs).
#define SPIKE_VKIND_TYPE_NAME SPIKE_KIND_COUNT
#define BUSTER_SPIKE_VKINDS (SPIKE_KIND_COUNT + 1)

// Virtual kinds index 128-entry tables, two zmm registers for vpermi2b.
_Static_assert(BUSTER_SPIKE_VKINDS <= 128, "spike kinds must fit 128-entry tables");

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
    // R, the matching closer: an opener's right threshold is one depth up.
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
// An opener's right search finds the first token at its own depth again: its closer. Its left search isn't used, but
// the scalar stacks pop what is at or above a token's left threshold before they push it, so it is the opener's own
// level: below that, they would drop the candidates before the opener.
#define BUSTER_SPIKE_RIGHT_THRESHOLD_O(level) BUSTER_SPIKE_LEVELS
#define BUSTER_SPIKE_LEFT_THRESHOLD_O(level) (level)

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

// The closer's role for each opener's role (SPIKE_ROLE_NONE for what isn't an opener). An attribute's opener says where
// the attribute is: after a declarator (OPEN_ATTRIBUTE, a postfix group), among specifiers, after `*` or after struct,
// union or enum. A cast's group becomes a compound literal's when `{` follows its `)`, or an abstract function
// declarator's when no operand does; the parsers decide that at the closer.
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
    [SPIKE_ROLE_OPEN_ATTRIBUTE_SPECIFIER] = SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_LEFT] = SPIKE_ROLE_CLOSE_ATTRIBUTE_LEFT,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_RIGHT] = SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT,
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
// __label__ or __extension__ before one. In a block, a typedef name followed by `:` is a label instead.
static bool spike_parser_starts_declaration(const SpikeParser* parser, uint32_t token, bool block)
{
    uint32_t i = token;
    while (parser->kinds[i] == SPIKE_KIND_EXTENSION)
    {
        i += 1;
    }
    const uint8_t kind = parser->kinds[i];
    const bool label = block && parser->kinds[i] == SPIKE_KIND_IDENTIFIER && parser->kinds[i + 1] == SPIKE_KIND_COLON;
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

// Whether a comma separates declarators or is the comma operator (COMMA), or separates arguments, parameters,
// initializers or enumerators (SEPARATOR, below the specifiers), by the innermost group of ( [ { around it: the ?: and
// case groups don't count, so the kernels decide it from the bracket depth of ( [ { alone.
static uint8_t spike_frame_comma_role(const SpikeParser* parser)
{
    uint32_t index = parser->frame_count - 1;
    while (index > 0 && (parser->frames[index].role == SPIKE_ROLE_OPEN_TERNARY || parser->frames[index].role == SPIKE_ROLE_OPEN_CASE))
    {
        index -= 1;
    }
    const uint8_t role = parser->frames[index].role;
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
        else if (spike_parser_starts_declaration(parser, i, block))
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
            frame->mode = spike_parser_starts_declaration(parser, i, block) ? SPIKE_MODE_SPECIFIERS : SPIKE_MODE_OPERAND;
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
    else if (spike_parser_starts_declaration(parser, i, block))
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
        step->role = spike_frame_comma_role(parser);
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
        step->role = spike_frame_comma_role(parser);
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
        const uint8_t opener_role = closer_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? SPIKE_ROLE_OPEN_ATTRIBUTE_SPECIFIER :
                                    closer_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_LEFT ? SPIKE_ROLE_OPEN_ATTRIBUTE_LEFT :
                                    closer_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT ? SPIKE_ROLE_OPEN_ATTRIBUTE_RIGHT : SPIKE_ROLE_OPEN_ATTRIBUTE;
        SpikeFrame* group = spike_parser_open(parser, i, opener_role, list ? SPIKE_MODE_ATTRIBUTE_LIST : SPIKE_MODE_OPERAND, continuation, false, step);
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

// ---------------------------------------------------------------------------------------------------------------------
// The kernels
//
// The data-parallel path builds the same tree as spike_parse_scalar in passes over the whole token array, each a kernel
// with a scalar twin whose output is the same:
//   - K1, brackets (spike_brackets_*): the depth before each token and its enclosing opener (a closer's is its opener),
//     run on the kinds for ( [ {, then on the roles for every group, ? : and case ... : included.
//   - K3, type names (spike_typedefs, spike_classify_*): a scalar pass over the typedef declarations only, in source
//     order, then every identifier is classified against the set of names they declare.
//   - K2, roles (spike_roles_*): each token's role from its kind, its neighbors' kinds, what the token before it is
//     (SpikePred, from that token's role) and the role of the group it is in. A token the rules can't be sure of is
//     flagged, and the top-level declaration it is in goes to the scalar parser.
//   - K4, parents (spike_parents_*): L and R by nearest-smaller-value searches on (depth, level) keys, then each
//     role's rule.
//   - K5, statements (spike_statements): the statement stack over the statement-level tokens of blocks.
//   - The fallback (spike_fallback): the scalar parser on every top-level declaration with a flagged token.

// What the token before says about the next one, from its role (and for specifiers and `*`, its kind).
typedef enum SpikePred
{
    // A declaration or a statement may start: the start, after `;` and the `{` and `}` of blocks, a label, case.
    SPIKE_PRED_ITEM,
    // A statement but not a declaration starts: after else, do, and the `)` of if, while, for and switch.
    SPIKE_PRED_HEAD,
    // An operand just ended.
    SPIKE_PRED_OPERAND,
    // After `*` or a qualifier or attribute that belongs to one.
    SPIKE_PRED_STAR,
    // After a type specifier.
    SPIKE_PRED_TYPE,
    // After another specifier: a qualifier, a storage class, typedef, an attribute.
    SPIKE_PRED_SPECIFIER,
    // A type name may start: the `(` of a cast, sizeof, a call or parameter list and a keyword's group, a separator.
    SPIKE_PRED_PARAMETER,
    // An operand comes next.
    SPIKE_PRED_EXPRESSION,
    // After a keyword that introduces what follows: struct, union, enum, if, __attribute__, typeof, ...
    SPIKE_PRED_LEAF,
    SPIKE_PRED_COUNT,
} SpikePred;

static const uint8_t spike_role_preds[SPIKE_ROLE_COUNT] =
{
    [SPIKE_ROLE_NONE] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_ERROR] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_OPERAND] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_MEMBER] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_STRING_NEXT] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_QUALIFIER_LEFT] = SPIKE_PRED_STAR,
    [SPIKE_ROLE_LEAF_RIGHT] = SPIKE_PRED_LEAF,
    // SPECIFIER: SPIKE_PRED_TYPE for a type keyword or a typedef name.
    [SPIKE_ROLE_SPECIFIER] = SPIKE_PRED_SPECIFIER,
    [SPIKE_ROLE_TAG] = SPIKE_PRED_TYPE,
    // PREFIX: SPIKE_PRED_STAR for `*`.
    [SPIKE_ROLE_PREFIX] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_POSTFIX] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_MEMBER_OPERATOR] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_MULTIPLICATIVE] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_ADDITIVE] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_SHIFT] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_RELATIONAL] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_EQUALITY] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_BITWISE_AND] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_BITWISE_XOR] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_BITWISE_OR] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_LOGICAL_AND] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_LOGICAL_OR] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_ASSIGN] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_RANGE] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_BITFIELD] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_COMMA] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_RETURN] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_ASSOCIATION] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_SEPARATOR] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_ASM_COLON] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_TERMINATOR] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_ELSE] = SPIKE_PRED_HEAD,
    [SPIKE_ROLE_DO] = SPIKE_PRED_HEAD,
    [SPIKE_ROLE_LABEL] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_OPEN_GROUP] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_CALL] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_FUNCTION] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_CAST] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_COMPOUND] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_SIZEOF] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_SPECIFIER] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_ATTRIBUTE] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_SPECIFIER] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_LEFT] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_RIGHT] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_ATTRIBUTE_LIST] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_ASM] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_KEYWORD] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_CONDITION] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_FOR] = SPIKE_PRED_PARAMETER,
    [SPIKE_ROLE_OPEN_SUBSCRIPT] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_BLOCK] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_OPEN_BODY] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_OPEN_STRUCT] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_OPEN_ENUM] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_INITIALIZER] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_COMPOUND_BODY] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_TERNARY] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_OPEN_CASE] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_CLOSE_GROUP] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_CALL] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_FUNCTION] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_CAST] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_CLOSE_COMPOUND] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_SIZEOF] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_SPECIFIER] = SPIKE_PRED_TYPE,
    [SPIKE_ROLE_CLOSE_ATTRIBUTE] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER] = SPIKE_PRED_SPECIFIER,
    [SPIKE_ROLE_CLOSE_ATTRIBUTE_LEFT] = SPIKE_PRED_STAR,
    [SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT] = SPIKE_PRED_LEAF,
    [SPIKE_ROLE_CLOSE_ATTRIBUTE_LIST] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_ASM] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_KEYWORD] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_CONDITION] = SPIKE_PRED_HEAD,
    [SPIKE_ROLE_CLOSE_FOR] = SPIKE_PRED_HEAD,
    [SPIKE_ROLE_CLOSE_SUBSCRIPT] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_BLOCK] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_CLOSE_BODY] = SPIKE_PRED_ITEM,
    [SPIKE_ROLE_CLOSE_STRUCT] = SPIKE_PRED_TYPE,
    [SPIKE_ROLE_CLOSE_ENUM] = SPIKE_PRED_TYPE,
    [SPIKE_ROLE_CLOSE_INITIALIZER] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_COMPOUND_BODY] = SPIKE_PRED_OPERAND,
    [SPIKE_ROLE_CLOSE_TERNARY] = SPIKE_PRED_EXPRESSION,
    [SPIKE_ROLE_CLOSE_CASE] = SPIKE_PRED_ITEM,
};

// The kinds of the next token after which an opener whose contents start a type name is a cast (an operand follows),
// as opposed to an abstract function declarator.
static bool spike_vkind_starts_operand(uint8_t vkind)
{
    return vkind == SPIKE_KIND_IDENTIFIER || vkind == SPIKE_VKIND_TYPE_NAME || vkind == SPIKE_KIND_NUMBER || vkind == SPIKE_KIND_CHARACTER ||
           vkind == SPIKE_KIND_STRING || vkind == SPIKE_KIND_L_PAREN || vkind == SPIKE_KIND_MINUS || vkind == SPIKE_KIND_PLUS || vkind == SPIKE_KIND_EXCLAIM ||
           vkind == SPIKE_KIND_TILDE || vkind == SPIKE_KIND_STAR || vkind == SPIKE_KIND_AMP || vkind == SPIKE_KIND_PLUSPLUS || vkind == SPIKE_KIND_MINUSMINUS ||
           vkind == SPIKE_KIND_AMPAMP || vkind == SPIKE_KIND_SIZEOF || vkind == SPIKE_KIND_REAL_IMAG || vkind == SPIKE_KIND_EXTENSION ||
           vkind == SPIKE_KIND_TYPE_ARGUMENT || vkind == SPIKE_KIND_GENERIC;
}

static bool spike_vkind_starts_type(uint8_t vkind)
{
    return vkind == SPIKE_KIND_TYPE || vkind == SPIKE_KIND_STRUCT || vkind == SPIKE_KIND_ENUM || vkind == SPIKE_KIND_QUALIFIER || vkind == SPIKE_KIND_ATOMIC ||
           vkind == SPIKE_KIND_TYPEOF || vkind == SPIKE_KIND_ATTRIBUTE || vkind == SPIKE_VKIND_TYPE_NAME;
}

static bool spike_vkind_starts_declaration(uint8_t vkind)
{
    return spike_vkind_starts_type(vkind) || vkind == SPIKE_KIND_STORAGE || vkind == SPIKE_KIND_TYPEDEF || vkind == SPIKE_KIND_ALIGNAS ||
           vkind == SPIKE_KIND_STATIC_ASSERT || vkind == SPIKE_KIND_LABEL || vkind == SPIKE_KIND_CALLING_CONVENTION || vkind == SPIKE_KIND_EXTENSION;
}

// The working arrays of the kernels, by token, and what they found.
typedef struct SpikeKernels SpikeKernels;
struct SpikeKernels
{
    uint8_t* vkinds;
    // The depth before each token: of ( [ { after the first K1 pass, of every group after the second.
    uint32_t* depths;
    uint32_t* enclosing;
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
    // By name: 0, or SPIKE_TYPEDEF_FILE, or SPIKE_TYPEDEF_SCOPED for a name some block declares as a typedef.
    uint8_t* typedef_names;
    // The file's typedef names in order, each with the token that declares it, for the fallback.
    uint32_t* typedef_log;
    uint32_t* typedef_tokens;
    uint32_t typedef_count;
    // What the kernels report.
    bool unbalanced;
    bool unsupported;
};

#define BUSTER_SPIKE_TYPEDEF_FILE 1
#define BUSTER_SPIKE_TYPEDEF_SCOPED 2

// ---------------------------------------------------------------------------------------------------------------------
// K1, brackets: the scalar twin

// The bracket deltas: of ( [ { by kind, for the first pass, and of every group by role, for the second.
static int8_t spike_kind_deltas[128];
static int8_t spike_role_deltas[128];

static void spike_kernels_init_tables(void)
{
    spike_kind_deltas[SPIKE_KIND_L_PAREN] = 1;
    spike_kind_deltas[SPIKE_KIND_L_SQUARE] = 1;
    spike_kind_deltas[SPIKE_KIND_L_BRACE] = 1;
    spike_kind_deltas[SPIKE_KIND_R_PAREN] = -1;
    spike_kind_deltas[SPIKE_KIND_R_SQUARE] = -1;
    spike_kind_deltas[SPIKE_KIND_R_BRACE] = -1;
    for (uint32_t role = 0; role < SPIKE_ROLE_COUNT; role += 1)
    {
        spike_role_deltas[role] = spike_role_rules[role] == SPIKE_RULE_OPENER ? 1 : 0;
        for (uint32_t opener = 0; opener < SPIKE_ROLE_COUNT; opener += 1)
        {
            if (spike_closer_roles[opener] == role && role != SPIKE_ROLE_NONE)
            {
                spike_role_deltas[role] = -1;
            }
        }
    }
}

// The depth before each token and its enclosing opener, from codes (kinds or roles) and their deltas. Returns false if
// a closer has no opener or a group is still open at the end.
static bool spike_brackets_scalar(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* stack)
{
    bool balanced = true;
    uint32_t depth = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const int8_t delta = deltas[codes[i]];
        depths[i] = depth;
        enclosing[i] = depth != 0 ? stack[depth - 1] : BUSTER_SPIKE_NONE;
        if (delta > 0)
        {
            stack[depth] = i;
            depth += 1;
        }
        else if (delta < 0 && depth == 0)
        {
            balanced = false;
        }
        else if (delta < 0)
        {
            depth -= 1;
        }
    }
    return balanced && depth == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// K3, type names

// The token after the group `opener` opens (or after `opener` if it isn't an opener): the first token after it whose
// depth is the opener's again. The typedef pass is the only user, over typedef declarations alone.
static uint32_t spike_after_group(const SpikeKernels* kernels, const uint8_t* kinds, uint32_t opener)
{
    const bool group = kinds[opener] == SPIKE_KIND_L_PAREN || kinds[opener] == SPIKE_KIND_L_SQUARE || kinds[opener] == SPIKE_KIND_L_BRACE;
    uint32_t result = opener + 1;
    while (group && kernels->depths[result] > kernels->depths[opener])
    {
        result += 1;
    }
    return result;
}

// The typedef pass: every typedef declaration, in source order, adds its declarators' names. A file-scope name is a type
// name everywhere after it; a name some block declares is marked SCOPED, and its uses are left to the scalar parser. A
// typedef declaration the pass can't read (a type specifier before `typedef`) marks the whole file unsupported.
static void spike_typedefs(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    kernels->typedef_count = 0;
    for (uint32_t t = 0; t < count && !kernels->unsupported; t += 1)
    {
        if (kinds[t] == SPIKE_KIND_TYPEDEF)
        {
            const uint32_t depth = kernels->depths[t];
            const uint8_t scope = depth == 0 ? BUSTER_SPIKE_TYPEDEF_FILE : BUSTER_SPIKE_TYPEDEF_SCOPED;
            // What may come before `typedef` in its specifiers: no type specifier.
            uint32_t back = t;
            while (back > 0 && (kinds[back - 1] == SPIKE_KIND_EXTENSION || kinds[back - 1] == SPIKE_KIND_STORAGE || kinds[back - 1] == SPIKE_KIND_QUALIFIER))
            {
                back -= 1;
            }
            const uint8_t before = back > 0 ? kinds[back - 1] : SPIKE_KIND_SEMI;
            if (before != SPIKE_KIND_SEMI && before != SPIKE_KIND_L_BRACE && before != SPIKE_KIND_R_BRACE && before != SPIKE_KIND_COLON)
            {
                kernels->unsupported = true;
            }
            // The specifiers.
            uint32_t i = t + 1;
            bool type_seen = false;
            bool specifiers = true;
            while (specifiers && i < count)
            {
                const uint8_t kind = kinds[i];
                if (kind == SPIKE_KIND_TYPE)
                {
                    type_seen = true;
                    i += 1;
                }
                else if (kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_STORAGE || kind == SPIKE_KIND_EXTENSION || kind == SPIKE_KIND_CALLING_CONVENTION ||
                         kind == SPIKE_KIND_TYPEDEF || (kind == SPIKE_KIND_ATOMIC && kinds[i + 1] != SPIKE_KIND_L_PAREN))
                {
                    i += 1;
                }
                else if (kind == SPIKE_KIND_ATOMIC || kind == SPIKE_KIND_TYPEOF)
                {
                    type_seen = true;
                    i = spike_after_group(kernels, kinds, i + 1);
                }
                else if (kind == SPIKE_KIND_ALIGNAS || kind == SPIKE_KIND_ATTRIBUTE)
                {
                    i = spike_after_group(kernels, kinds, i + 1);
                }
                else if (kind == SPIKE_KIND_STRUCT || kind == SPIKE_KIND_ENUM)
                {
                    type_seen = true;
                    i += 1;
                    while (kinds[i] == SPIKE_KIND_ATTRIBUTE)
                    {
                        i = spike_after_group(kernels, kinds, i + 1);
                    }
                    i += kinds[i] == SPIKE_KIND_IDENTIFIER;
                    i = kinds[i] == SPIKE_KIND_L_BRACE ? spike_after_group(kernels, kinds, i) : i;
                }
                else if (kind == SPIKE_KIND_IDENTIFIER && !type_seen && kernels->typedef_names[names[i]] != 0)
                {
                    type_seen = true;
                    i += 1;
                }
                else
                {
                    specifiers = false;
                }
            }
            // The declarators, to the `;` at the declaration's depth.
            bool declarators = true;
            while (declarators && i < count)
            {
                uint32_t name = BUSTER_SPIKE_NONE;
                bool searching = true;
                while (searching && i < count)
                {
                    const uint8_t kind = kinds[i];
                    if (kind == SPIKE_KIND_STAR || kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_CALLING_CONVENTION || kind == SPIKE_KIND_L_PAREN)
                    {
                        i += 1;
                    }
                    else if (kind == SPIKE_KIND_ATTRIBUTE)
                    {
                        i = spike_after_group(kernels, kinds, i + 1);
                    }
                    else
                    {
                        searching = false;
                        name = kind == SPIKE_KIND_IDENTIFIER ? i : BUSTER_SPIKE_NONE;
                    }
                }
                if (name != BUSTER_SPIKE_NONE)
                {
                    const uint32_t id = names[name];
                    kernels->typedef_names[id] = kernels->typedef_names[id] == BUSTER_SPIKE_TYPEDEF_SCOPED ? BUSTER_SPIKE_TYPEDEF_SCOPED : scope;
                    kernels->typedef_log[kernels->typedef_count] = id;
                    kernels->typedef_tokens[kernels->typedef_count] = name;
                    kernels->typedef_count += 1;
                    i = name + 1;
                }
                while (i < count && !(kernels->depths[i] == depth && (kinds[i] == SPIKE_KIND_COMMA || kinds[i] == SPIKE_KIND_SEMI)))
                {
                    i = spike_after_group(kernels, kinds, i);
                }
                declarators = i < count && kinds[i] == SPIKE_KIND_COMMA;
                i += 1;
            }
            t = i > t ? i - 1 : t;
        }
    }
}

// The declarator names of the typedef declarations are identifiers being declared, not uses of type names.
static void spike_classify_declarators(SpikeKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        const uint32_t token = kernels->typedef_tokens[i];
        kernels->vkinds[token] = SPIKE_KIND_IDENTIFIER;
        kernels->flags[token] = 0;
    }
}

// K3's classification, the scalar twin: the virtual kinds, and a flag on every use of a name some block declares as a
// typedef.
static void spike_classify_scalar(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t meaning = kinds[i] == SPIKE_KIND_IDENTIFIER ? kernels->typedef_names[names[i]] : 0;
        kernels->vkinds[i] = meaning != 0 ? SPIKE_VKIND_TYPE_NAME : kinds[i];
        kernels->flags[i] = meaning == BUSTER_SPIKE_TYPEDEF_SCOPED;
    }
    spike_classify_declarators(kernels);
}

// ---------------------------------------------------------------------------------------------------------------------
// K2, roles: the scalar twin

static uint8_t spike_pred_of(uint8_t role, uint8_t vkind)
{
    uint8_t result = spike_role_preds[role];
    if (role == SPIKE_ROLE_SPECIFIER && (vkind == SPIKE_KIND_TYPE || vkind == SPIKE_VKIND_TYPE_NAME))
    {
        result = SPIKE_PRED_TYPE;
    }
    else if (role == SPIKE_ROLE_PREFIX && vkind == SPIKE_KIND_STAR)
    {
        result = SPIKE_PRED_STAR;
    }
    return result;
}

// Whether a role is a type specifier, for the typed state of a declaration's specifiers.
static bool spike_role_is_type_specifier(uint8_t role, uint8_t vkind)
{
    return (role == SPIKE_ROLE_SPECIFIER && (vkind == SPIKE_KIND_TYPE || vkind == SPIKE_VKIND_TYPE_NAME)) || role == SPIKE_ROLE_TAG ||
           role == SPIKE_ROLE_CLOSE_STRUCT || role == SPIKE_ROLE_CLOSE_ENUM || role == SPIKE_ROLE_CLOSE_SPECIFIER;
}

// Whether a role carries a declaration's typed state through: specifiers that aren't type specifiers, the keywords
// before struct, typeof and attribute groups.
static bool spike_role_propagates_typed(uint8_t role)
{
    return role == SPIKE_ROLE_SPECIFIER || role == SPIKE_ROLE_LEAF_RIGHT;
}

// The closer's role for the opener `opener` (whose role is final), and for a cast candidate the opener's final role.
static uint8_t spike_closer_role(SpikeKernels* kernels, uint32_t closer)
{
    const uint32_t opener = kernels->enclosing[closer];
    uint8_t result = SPIKE_ROLE_ERROR;
    if (opener != BUSTER_SPIKE_NONE)
    {
        const uint8_t opener_role = kernels->roles[opener];
        result = spike_closer_roles[opener_role];
        if (opener_role == SPIKE_ROLE_OPEN_CAST)
        {
            const uint8_t after = kernels->vkinds[closer + 1];
            const uint8_t role = after == SPIKE_KIND_L_BRACE ? SPIKE_ROLE_OPEN_COMPOUND : spike_vkind_starts_operand(after) ? SPIKE_ROLE_OPEN_CAST : SPIKE_ROLE_OPEN_FUNCTION;
            kernels->roles[opener] = role;
            result = spike_closer_roles[role];
        }
        else if (result == SPIKE_ROLE_NONE)
        {
            result = SPIKE_ROLE_ERROR;
        }
    }
    return result;
}

// The keyword of a tagged type whose body starts after `token`: back over the tag and the attribute groups between them
// (`struct __attribute__((packed)) S {`), through each group's closer's opener. SPIKE_KIND_END if there is none.
static uint8_t spike_tag_keyword(const SpikeKernels* kernels, uint32_t token)
{
    uint32_t t = token;
    bool searching = true;
    uint8_t result = SPIKE_KIND_END;
    while (searching)
    {
        const uint8_t kind = kernels->vkinds[t];
        const uint32_t opener = kind == SPIKE_KIND_R_PAREN ? kernels->enclosing[t] : BUSTER_SPIKE_NONE;
        if (opener != BUSTER_SPIKE_NONE && opener >= 2 && kernels->vkinds[opener - 1] == SPIKE_KIND_ATTRIBUTE)
        {
            t = opener - 2;
        }
        else if ((kind == SPIKE_KIND_IDENTIFIER || kind == SPIKE_VKIND_TYPE_NAME) && t > 0)
        {
            t -= 1;
        }
        else
        {
            searching = false;
            result = kind == SPIKE_KIND_STRUCT || kind == SPIKE_KIND_ENUM ? kind : SPIKE_KIND_END;
        }
    }
    return result;
}

// The role of token `i` from the rules, given the roles before it. Sets *flag if the rules can't be sure.
static uint8_t spike_role_rule(SpikeKernels* kernels, uint32_t i, bool typed, bool* flag)
{
    const uint8_t* vk = kernels->vkinds;
    const uint8_t kind = vk[i];
    const uint8_t previous = vk[i - 1];
    const uint8_t before = vk[i - 2];
    const uint8_t next = vk[i + 1];
    const uint8_t previous_role = i > 0 ? kernels->roles[i - 1] : SPIKE_ROLE_NONE;
    const uint8_t pred = i > 0 ? spike_pred_of(previous_role, previous) : SPIKE_PRED_ITEM;
    const uint8_t before_pred = i > 1 ? spike_pred_of(kernels->roles[i - 2], before) : SPIKE_PRED_ITEM;
    const uint32_t opener = kernels->enclosing[i];
    const uint8_t group = opener != BUSTER_SPIKE_NONE ? kernels->roles[opener] : SPIKE_ROLE_NONE;
    const bool operand = pred == SPIKE_PRED_OPERAND;
    const bool after_asm = previous == SPIKE_KIND_ASM || (previous_role == SPIKE_ROLE_LEAF_RIGHT && (previous == SPIKE_KIND_QUALIFIER || previous == SPIKE_KIND_STORAGE ||
                                                                                                    previous == SPIKE_KIND_GOTO));
    uint8_t result = SPIKE_ROLE_ERROR;
    if (group == SPIKE_ROLE_OPEN_ATTRIBUTE_LIST && (kind >= SPIKE_KIND_TYPE || kind == SPIKE_KIND_IDENTIFIER))
    {
        // An attribute's name, whatever its spelling.
        result = SPIKE_ROLE_OPERAND;
    }
    else
    {
        switch (kind)
        {
            case SPIKE_KIND_NUMBER:
            case SPIKE_KIND_CHARACTER: result = SPIKE_ROLE_OPERAND; break;
            case SPIKE_KIND_STRING: result = previous == SPIKE_KIND_STRING ? SPIKE_ROLE_STRING_NEXT : SPIKE_ROLE_OPERAND; break;
            case SPIKE_KIND_IDENTIFIER:
                if (previous == SPIKE_KIND_PERIOD || previous == SPIKE_KIND_ARROW)
                {
                    result = SPIKE_ROLE_MEMBER;
                }
                else if (previous == SPIKE_KIND_STRUCT || previous == SPIKE_KIND_ENUM || previous_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT)
                {
                    result = next == SPIKE_KIND_L_BRACE ? SPIKE_ROLE_LEAF_RIGHT : SPIKE_ROLE_TAG;
                }
                else
                {
                    result = SPIKE_ROLE_OPERAND;
                }
                break;
            case SPIKE_VKIND_TYPE_NAME:
                if (previous == SPIKE_KIND_PERIOD || previous == SPIKE_KIND_ARROW)
                {
                    result = SPIKE_ROLE_MEMBER;
                }
                else if (previous == SPIKE_KIND_STRUCT || previous == SPIKE_KIND_ENUM || previous_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT)
                {
                    result = next == SPIKE_KIND_L_BRACE ? SPIKE_ROLE_LEAF_RIGHT : SPIKE_ROLE_TAG;
                }
                else if (previous == SPIKE_KIND_GOTO)
                {
                    result = SPIKE_ROLE_OPERAND;
                }
                else if (next == SPIKE_KIND_COLON && (pred == SPIKE_PRED_ITEM || pred == SPIKE_PRED_HEAD) && (group == SPIKE_ROLE_OPEN_BLOCK || group == SPIKE_ROLE_OPEN_BODY))
                {
                    // A label spelled like a type name.
                    result = SPIKE_ROLE_OPERAND;
                }
                else if (pred == SPIKE_PRED_ITEM || pred == SPIKE_PRED_PARAMETER || (pred == SPIKE_PRED_SPECIFIER && !typed))
                {
                    result = SPIKE_ROLE_SPECIFIER;
                }
                else
                {
                    // A declarator spelled like a type name, or one in an expression: it hides the typedef.
                    result = SPIKE_ROLE_OPERAND;
                    *flag = true;
                }
                break;
            case SPIKE_KIND_TYPE:
            case SPIKE_KIND_TYPEDEF:
            case SPIKE_KIND_LABEL: result = SPIKE_ROLE_SPECIFIER; break;
            case SPIKE_KIND_QUALIFIER:
            case SPIKE_KIND_CALLING_CONVENTION:
                result = after_asm ? SPIKE_ROLE_LEAF_RIGHT : pred == SPIKE_PRED_STAR ? SPIKE_ROLE_QUALIFIER_LEFT : SPIKE_ROLE_SPECIFIER;
                break;
            case SPIKE_KIND_STORAGE: result = after_asm ? SPIKE_ROLE_LEAF_RIGHT : SPIKE_ROLE_SPECIFIER; break;
            case SPIKE_KIND_ATOMIC:
                result = next == SPIKE_KIND_L_PAREN ? SPIKE_ROLE_LEAF_RIGHT : pred == SPIKE_PRED_STAR ? SPIKE_ROLE_QUALIFIER_LEFT : SPIKE_ROLE_SPECIFIER;
                break;
            case SPIKE_KIND_EXTENSION: result = spike_vkind_starts_declaration(next) ? SPIKE_ROLE_SPECIFIER : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_GENERIC:
                result = SPIKE_ROLE_LEAF_RIGHT;
                *flag = true;
                break;
            case SPIKE_KIND_STRUCT:
            case SPIKE_KIND_ENUM:
            case SPIKE_KIND_TYPEOF:
            case SPIKE_KIND_ALIGNAS:
            case SPIKE_KIND_ATTRIBUTE:
            case SPIKE_KIND_ASM:
            case SPIKE_KIND_TYPE_ARGUMENT:
            case SPIKE_KIND_STATIC_ASSERT:
            case SPIKE_KIND_IF:
            case SPIKE_KIND_WHILE:
            case SPIKE_KIND_SWITCH:
            case SPIKE_KIND_FOR: result = SPIKE_ROLE_LEAF_RIGHT; break;
            case SPIKE_KIND_SIZEOF:
            case SPIKE_KIND_REAL_IMAG: result = SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_ELSE: result = SPIKE_ROLE_ELSE; break;
            case SPIKE_KIND_DO: result = SPIKE_ROLE_DO; break;
            case SPIKE_KIND_CASE:
            case SPIKE_KIND_DEFAULT: result = SPIKE_ROLE_OPEN_CASE; break;
            case SPIKE_KIND_GOTO: result = after_asm ? SPIKE_ROLE_LEAF_RIGHT : SPIKE_ROLE_RETURN; break;
            case SPIKE_KIND_RETURN:
            case SPIKE_KIND_BREAK:
            case SPIKE_KIND_CONTINUE: result = SPIKE_ROLE_RETURN; break;
            case SPIKE_KIND_SEMI: result = SPIKE_ROLE_TERMINATOR; break;
            case SPIKE_KIND_COMMA:
                result = group == SPIKE_ROLE_NONE || group == SPIKE_ROLE_OPEN_BLOCK || group == SPIKE_ROLE_OPEN_BODY || group == SPIKE_ROLE_OPEN_STRUCT ||
                         group == SPIKE_ROLE_OPEN_FOR ? SPIKE_ROLE_COMMA : SPIKE_ROLE_SEPARATOR;
                break;
            case SPIKE_KIND_QUESTION: result = SPIKE_ROLE_OPEN_TERNARY; break;
            case SPIKE_KIND_COLON:
                if (group == SPIKE_ROLE_OPEN_ASM)
                {
                    result = SPIKE_ROLE_ASM_COLON;
                }
                else if (group == SPIKE_ROLE_OPEN_STRUCT)
                {
                    result = SPIKE_ROLE_BITFIELD;
                }
                else
                {
                    // The closer of ? or case, or a label: spike_secondary decides.
                    result = SPIKE_ROLE_CLOSE_TERNARY;
                }
                break;
            case SPIKE_KIND_ELLIPSIS:
                // In a parameter list (a cast candidate may turn out to be one), else a range.
                result = group == SPIKE_ROLE_OPEN_CALL || group == SPIKE_ROLE_OPEN_FUNCTION || group == SPIKE_ROLE_OPEN_CAST ? SPIKE_ROLE_OPERAND : SPIKE_ROLE_RANGE;
                break;
            case SPIKE_KIND_STAR: result = operand ? SPIKE_ROLE_MULTIPLICATIVE : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_AMP: result = operand ? SPIKE_ROLE_BITWISE_AND : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_PLUS:
            case SPIKE_KIND_MINUS: result = operand ? SPIKE_ROLE_ADDITIVE : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_AMPAMP: result = operand ? SPIKE_ROLE_LOGICAL_AND : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_PLUSPLUS:
            case SPIKE_KIND_MINUSMINUS: result = operand ? SPIKE_ROLE_POSTFIX : SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_EXCLAIM:
            case SPIKE_KIND_TILDE: result = SPIKE_ROLE_PREFIX; break;
            case SPIKE_KIND_PERIOD:
            case SPIKE_KIND_ARROW: result = SPIKE_ROLE_MEMBER_OPERATOR; break;
            case SPIKE_KIND_L_SQUARE: result = SPIKE_ROLE_OPEN_SUBSCRIPT; break;
            case SPIKE_KIND_L_PAREN:
                if (previous == SPIKE_KIND_IF || previous == SPIKE_KIND_WHILE || previous == SPIKE_KIND_SWITCH)
                {
                    result = SPIKE_ROLE_OPEN_CONDITION;
                }
                else if (previous == SPIKE_KIND_FOR)
                {
                    result = SPIKE_ROLE_OPEN_FOR;
                }
                else if (previous == SPIKE_KIND_SIZEOF)
                {
                    result = SPIKE_ROLE_OPEN_SIZEOF;
                }
                else if (previous == SPIKE_KIND_TYPEOF || previous == SPIKE_KIND_ALIGNAS || previous == SPIKE_KIND_ATOMIC)
                {
                    result = SPIKE_ROLE_OPEN_SPECIFIER;
                }
                else if (previous == SPIKE_KIND_ATTRIBUTE)
                {
                    // Where the attribute is, by the token before its keyword.
                    result = before_pred == SPIKE_PRED_OPERAND ? SPIKE_ROLE_OPEN_ATTRIBUTE : before_pred == SPIKE_PRED_STAR ? SPIKE_ROLE_OPEN_ATTRIBUTE_LEFT :
                             before_pred == SPIKE_PRED_LEAF ? SPIKE_ROLE_OPEN_ATTRIBUTE_RIGHT : SPIKE_ROLE_OPEN_ATTRIBUTE_SPECIFIER;
                }
                else if (previous == SPIKE_KIND_L_PAREN && before == SPIKE_KIND_ATTRIBUTE)
                {
                    result = SPIKE_ROLE_OPEN_ATTRIBUTE_LIST;
                }
                else if (previous == SPIKE_KIND_ASM)
                {
                    // An asm statement at the start of a statement or a file-scope item, else an asm label.
                    result = before_pred == SPIKE_PRED_ITEM || before_pred == SPIKE_PRED_HEAD ? SPIKE_ROLE_OPEN_ASM : SPIKE_ROLE_OPEN_ATTRIBUTE;
                }
                else if (after_asm)
                {
                    result = SPIKE_ROLE_OPEN_ASM;
                }
                else if (previous == SPIKE_KIND_TYPE_ARGUMENT || previous == SPIKE_KIND_GENERIC || previous == SPIKE_KIND_STATIC_ASSERT)
                {
                    result = SPIKE_ROLE_OPEN_KEYWORD;
                }
                else if (operand)
                {
                    result = SPIKE_ROLE_OPEN_CALL;
                }
                else if (spike_vkind_starts_type(next))
                {
                    // A cast, a compound literal's type or an abstract function declarator, which its closer decides.
                    result = SPIKE_ROLE_OPEN_CAST;
                }
                else if ((pred == SPIKE_PRED_TYPE || pred == SPIKE_PRED_SPECIFIER || pred == SPIKE_PRED_STAR) && (next == SPIKE_KIND_R_PAREN || next == SPIKE_KIND_ELLIPSIS))
                {
                    result = SPIKE_ROLE_OPEN_FUNCTION;
                }
                else
                {
                    result = SPIKE_ROLE_OPEN_GROUP;
                }
                break;
            case SPIKE_KIND_L_BRACE:
                if (previous == SPIKE_KIND_EQUAL)
                {
                    result = SPIKE_ROLE_OPEN_INITIALIZER;
                }
                else if (previous == SPIKE_KIND_STRUCT || ((previous == SPIKE_KIND_IDENTIFIER || previous == SPIKE_VKIND_TYPE_NAME) && before == SPIKE_KIND_STRUCT))
                {
                    result = SPIKE_ROLE_OPEN_STRUCT;
                }
                else if (previous == SPIKE_KIND_ENUM || ((previous == SPIKE_KIND_IDENTIFIER || previous == SPIKE_VKIND_TYPE_NAME) && before == SPIKE_KIND_ENUM))
                {
                    result = SPIKE_ROLE_OPEN_ENUM;
                }
                else if (previous_role == SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT || (previous_role == SPIKE_ROLE_LEAF_RIGHT && (previous == SPIKE_KIND_IDENTIFIER ||
                                                                                                            previous == SPIKE_VKIND_TYPE_NAME)))
                {
                    // A tagged type's body after attributes: `struct __attribute__((packed)) S {`.
                    const uint8_t keyword = spike_tag_keyword(kernels, i - 1);
                    result = keyword == SPIKE_KIND_ENUM ? SPIKE_ROLE_OPEN_ENUM : SPIKE_ROLE_OPEN_STRUCT;
                    *flag = keyword == SPIKE_KIND_END;
                }
                else if (previous_role == SPIKE_ROLE_CLOSE_CONDITION || previous_role == SPIKE_ROLE_CLOSE_FOR || previous == SPIKE_KIND_L_PAREN)
                {
                    result = SPIKE_ROLE_OPEN_BLOCK;
                }
                else if (previous_role == SPIKE_ROLE_CLOSE_CALL || previous_role == SPIKE_ROLE_CLOSE_FUNCTION)
                {
                    result = SPIKE_ROLE_OPEN_BODY;
                }
                else if (previous_role == SPIKE_ROLE_CLOSE_COMPOUND)
                {
                    result = SPIKE_ROLE_OPEN_COMPOUND_BODY;
                }
                else if ((previous == SPIKE_KIND_L_BRACE || previous == SPIKE_KIND_COMMA) && (group == SPIKE_ROLE_OPEN_INITIALIZER || group == SPIKE_ROLE_OPEN_COMPOUND_BODY))
                {
                    result = SPIKE_ROLE_OPEN_INITIALIZER;
                }
                else if (pred == SPIKE_PRED_ITEM || pred == SPIKE_PRED_HEAD)
                {
                    result = SPIKE_ROLE_OPEN_BLOCK;
                }
                else
                {
                    result = SPIKE_ROLE_OPEN_BLOCK;
                    *flag = true;
                }
                break;
            case SPIKE_KIND_R_PAREN:
            case SPIKE_KIND_R_SQUARE:
            case SPIKE_KIND_R_BRACE: result = spike_closer_role(kernels, i); break;
            default:
                result = spike_binary_role(kind);
                if (result == SPIKE_ROLE_NONE)
                {
                    result = SPIKE_ROLE_ERROR;
                    *flag = true;
                }
                break;
        }
    }
    if (result == SPIKE_ROLE_ERROR)
    {
        *flag = true;
    }
    return result;
}

// K2, the scalar twin: every token's role in source order, so that each rule sees the final roles before it. The typed
// state of a declaration's specifiers (whether a type specifier came before) runs along with it: a type specifier sets
// it, the other specifiers and the keywords before struct and attribute groups carry it, an attribute's closer takes
// it from before the attribute, and anything else clears it.
static void spike_roles_scalar(SpikeKernels* kernels, uint32_t count, uint8_t* typed)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const bool typed_before = i > 0 && typed[i - 1];
        bool flag = false;
        const uint8_t role = spike_role_rule(kernels, i, typed_before, &flag);
        kernels->roles[i] = role;
        kernels->flags[i] |= flag;
        const uint32_t opener = kernels->enclosing[i];
        if (role == SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER && opener != BUSTER_SPIKE_NONE && opener >= 2)
        {
            typed[i] = typed[opener - 2];
        }
        else
        {
            typed[i] = spike_role_is_type_specifier(role, kernels->vkinds[i]) || (spike_role_propagates_typed(role) && typed_before);
        }
    }
}

// K1's second part, the groups of ? : and case ... : (the secondary brackets), over their tokens only: each `:` that
// isn't an asm operand's or a bit-field's closes the innermost ? or case still open in the same group of ( [ {, or is a
// label if none is. Then the depth of the secondary brackets, added to the depth of ( [ {, gives every token's depth for
// K4. `secondary` has a slot per token for the matching stack.
static void spike_secondary_scalar(SpikeKernels* kernels, uint32_t count, uint32_t* secondary)
{
    uint32_t open = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        if (role == SPIKE_ROLE_OPEN_TERNARY || role == SPIKE_ROLE_OPEN_CASE)
        {
            secondary[open] = i;
            open += 1;
        }
        else if (role == SPIKE_ROLE_CLOSE_TERNARY)
        {
            const uint32_t depth = kernels->depths[i];
            // Openers left open in a group that has ended: invalid C.
            while (open != 0 && kernels->depths[secondary[open - 1]] > depth)
            {
                kernels->flags[secondary[open - 1]] = 1;
                open -= 1;
            }
            if (open != 0 && kernels->depths[secondary[open - 1]] == depth)
            {
                const uint32_t opener = secondary[open - 1];
                open -= 1;
                kernels->roles[i] = kernels->roles[opener] == SPIKE_ROLE_OPEN_CASE ? SPIKE_ROLE_CLOSE_CASE : SPIKE_ROLE_CLOSE_TERNARY;
            }
            else
            {
                const uint8_t previous = kernels->vkinds[i - 1];
                kernels->roles[i] = SPIKE_ROLE_LABEL;
                kernels->flags[i] |= previous != SPIKE_KIND_IDENTIFIER && previous != SPIKE_VKIND_TYPE_NAME;
            }
        }
    }
    while (open != 0)
    {
        kernels->flags[secondary[open - 1]] = 1;
        open -= 1;
    }
    // The depth of ? : and case : groups, added to each token's.
    uint32_t inner = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t before = inner;
        inner += role == SPIKE_ROLE_OPEN_TERNARY || role == SPIKE_ROLE_OPEN_CASE;
        inner -= (role == SPIKE_ROLE_CLOSE_TERNARY || role == SPIKE_ROLE_CLOSE_CASE) && inner != 0;
        kernels->depths[i] += before;
    }
}

// The roles that depend on whether a `:` before them ends a case label or a label (a statement starts) or a ternary's
// middle (an operand follows), which the first K2 pass guessed as the latter: a `{` after a case label is a block; a
// typedef name there stays flagged, since what follows it was decided from the guess.
static void spike_after_colons(SpikeKernels* kernels, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint8_t previous = kernels->roles[i - 1];
        if ((previous == SPIKE_ROLE_CLOSE_CASE || previous == SPIKE_ROLE_LABEL) && kernels->vkinds[i] == SPIKE_KIND_L_BRACE)
        {
            kernels->roles[i] = SPIKE_ROLE_OPEN_BLOCK;
            kernels->flags[i] = 0;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K4, parents: the scalar twin

// A token's node depth: a closer's is outside its group.
static uint32_t spike_node_depth(const SpikeKernels* kernels, uint32_t token)
{
    return kernels->depths[token] - (spike_role_deltas[kernels->roles[token]] < 0);
}

static void spike_parents_resolve(SpikeKernels* kernels, uint32_t token, uint32_t right)
{
    const uint8_t role = kernels->roles[token];
    const uint32_t left = kernels->lefts[token];
    uint32_t parent;
    if (spike_role_rules[role] == SPIKE_RULE_RIGHT || spike_role_rules[role] == SPIKE_RULE_OPENER)
    {
        parent = right;
    }
    else
    {
        const uint32_t left_depth = left != BUSTER_SPIKE_NONE ? spike_node_depth(kernels, left) : 0;
        const uint8_t left_level = left != BUSTER_SPIKE_NONE ? spike_role_levels[kernels->roles[left]] : 0;
        const uint32_t right_depth = right != BUSTER_SPIKE_NONE ? spike_node_depth(kernels, right) : 0;
        const uint8_t right_level = right != BUSTER_SPIKE_NONE ? spike_role_levels[kernels->roles[right]] : 0;
        parent = spike_select(left, left_depth, left_level, right, right_depth, right_level);
    }
    kernels->parents[token] = parent;
}

// K4, the scalar twin: L and R of every token with the two stacks of spike_parser_emit, then each role's rule; an
// opener's parent is its closer.
static void spike_parents_scalar(SpikeKernels* kernels, uint32_t count)
{
    uint32_t left_count = 0;
    uint32_t right_count = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t base = spike_node_depth(kernels, i) * BUSTER_SPIKE_LEVELS;
        const uint32_t key = base + spike_role_levels[role];
        const uint8_t rule = spike_role_rules[role];
        while (right_count != 0 && kernels->waiting_thresholds[right_count - 1] > key)
        {
            right_count -= 1;
            spike_parents_resolve(kernels, kernels->waiting[right_count], i);
        }
        const uint32_t left_threshold = base + spike_role_left_thresholds[role];
        while (left_count != 0 && kernels->stack_keys[left_count - 1] >= left_threshold)
        {
            left_count -= 1;
        }
        const uint32_t left = left_count != 0 ? kernels->stack[left_count - 1] : BUSTER_SPIKE_NONE;
        while (left_count != 0 && kernels->stack_keys[left_count - 1] >= key)
        {
            left_count -= 1;
        }
        kernels->stack[left_count] = i;
        kernels->stack_keys[left_count] = key;
        left_count += 1;
        if (rule == SPIKE_RULE_LEFT)
        {
            kernels->parents[i] = left;
        }
        else
        {
            kernels->lefts[i] = left;
            kernels->waiting[right_count] = i;
            kernels->waiting_thresholds[right_count] = base + spike_role_right_thresholds[role];
            right_count += 1;
        }
    }
    while (right_count != 0)
    {
        right_count -= 1;
        spike_parents_resolve(kernels, kernels->waiting[right_count], BUSTER_SPIKE_NONE);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K5, statements

// The statement stack of spike_parser's, over the kernels' arrays: the same states and rules.
typedef struct SpikeStatementStack SpikeStatementStack;
struct SpikeStatementStack
{
    SpikeStatement* items;
    uint32_t count;
    uint32_t* parents;
};

static SpikeStatement* spike_k5_top(SpikeStatementStack* stack)
{
    return stack->count != 0 ? &stack->items[stack->count - 1] : 0;
}

static void spike_k5_complete(SpikeStatementStack* stack)
{
    bool completing = true;
    while (completing)
    {
        SpikeStatement* top = spike_k5_top(stack);
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
            stack->count -= 1;
        }
    }
}

static void spike_k5_flush(SpikeStatementStack* stack)
{
    SpikeStatement* top = spike_k5_top(stack);
    while (top != 0 && top->state == SPIKE_STATEMENT_IF_DONE)
    {
        stack->count -= 1;
        spike_k5_complete(stack);
        top = spike_k5_top(stack);
    }
}

static uint32_t spike_k5_owner(SpikeStatementStack* stack)
{
    uint32_t result = BUSTER_SPIKE_NONE;
    SpikeStatement* top = spike_k5_top(stack);
    if (top != 0 && top->state == SPIKE_STATEMENT_WAITING)
    {
        result = top->token;
        top->state = SPIKE_STATEMENT_STARTED;
    }
    return result;
}

// K5: the statement-level tokens of blocks, in order, through the statement stack, which sets the parents of the
// statements heads own, of `else` and of a do-statement's parts. Returns false if an else has no if.
static bool spike_statements(SpikeKernels* kernels, uint32_t count, SpikeStatement* items)
{
    SpikeStatementStack stack = { .items = items, .count = 0, .parents = kernels->parents };
    bool result = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t opener = kernels->enclosing[i];
        const uint8_t group = opener != BUSTER_SPIKE_NONE ? kernels->roles[opener] : SPIKE_ROLE_NONE;
        // A closer's statement context is its opener's; the enclosing groups are those of ( [ { only, since no
        // statement is inside a ? : or case group.
        const uint32_t outer = opener != BUSTER_SPIKE_NONE ? kernels->enclosing[opener] : BUSTER_SPIKE_NONE;
        const uint8_t outer_group = outer != BUSTER_SPIKE_NONE ? kernels->roles[outer] : SPIKE_ROLE_NONE;
        const bool in_block = group == SPIKE_ROLE_OPEN_BLOCK || group == SPIKE_ROLE_OPEN_BODY;
        const bool closer_in_block = outer_group == SPIKE_ROLE_OPEN_BLOCK || outer_group == SPIKE_ROLE_OPEN_BODY;
        if (role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY)
        {
            const bool statement = role == SPIKE_ROLE_OPEN_BLOCK && in_block;
            uint32_t owner = BUSTER_SPIKE_NONE;
            if (statement)
            {
                spike_k5_flush(&stack);
                owner = spike_k5_owner(&stack);
            }
            stack.items[stack.count] = (SpikeStatement){ .token = BUSTER_SPIKE_NONE, .owner = owner, .extra = BUSTER_SPIKE_NONE, .state = SPIKE_STATEMENT_BLOCK, .kind = statement };
            stack.count += 1;
        }
        else if (role == SPIKE_ROLE_CLOSE_BLOCK || role == SPIKE_ROLE_CLOSE_BODY)
        {
            spike_k5_flush(&stack);
            SpikeStatement* top = spike_k5_top(&stack);
            while (top != 0 && top->state != SPIKE_STATEMENT_BLOCK)
            {
                stack.count -= 1;
                top = spike_k5_top(&stack);
            }
            if (top != 0)
            {
                const bool statement = top->kind != 0;
                if (top->owner != BUSTER_SPIKE_NONE)
                {
                    kernels->parents[i] = top->owner;
                }
                stack.count -= 1;
                if (statement)
                {
                    spike_k5_complete(&stack);
                }
            }
        }
        else if (role == SPIKE_ROLE_TERMINATOR && in_block)
        {
            spike_k5_flush(&stack);
            SpikeStatement* top = spike_k5_top(&stack);
            if (top != 0 && top->state == SPIKE_STATEMENT_DO_WHILE)
            {
                kernels->parents[top->token] = i;
                kernels->parents[top->extra] = i;
                if (top->owner != BUSTER_SPIKE_NONE)
                {
                    kernels->parents[i] = top->owner;
                }
                stack.count -= 1;
            }
            else
            {
                const uint32_t owner = spike_k5_owner(&stack);
                if (owner != BUSTER_SPIKE_NONE)
                {
                    kernels->parents[i] = owner;
                }
            }
            spike_k5_complete(&stack);
        }
        else if (((role == SPIKE_ROLE_CLOSE_CONDITION || role == SPIKE_ROLE_CLOSE_FOR) && closer_in_block) ||
                 ((role == SPIKE_ROLE_CLOSE_CASE || role == SPIKE_ROLE_LABEL || role == SPIKE_ROLE_DO) && in_block))
        {
            const uint8_t keyword = role == SPIKE_ROLE_CLOSE_CONDITION ? kernels->vkinds[opener - 1] : role == SPIKE_ROLE_CLOSE_FOR ? SPIKE_KIND_FOR :
                                    role == SPIKE_ROLE_DO ? SPIKE_KIND_DO : SPIKE_KIND_CASE;
            spike_k5_flush(&stack);
            SpikeStatement* top = spike_k5_top(&stack);
            if (keyword == SPIKE_KIND_WHILE && top != 0 && top->state == SPIKE_STATEMENT_DO_DONE)
            {
                top->state = SPIKE_STATEMENT_DO_WHILE;
                top->extra = i;
            }
            else
            {
                const uint32_t owner = spike_k5_owner(&stack);
                if (owner != BUSTER_SPIKE_NONE && keyword != SPIKE_KIND_DO)
                {
                    kernels->parents[i] = owner;
                }
                stack.items[stack.count] = (SpikeStatement){
                    .token = i,
                    .owner = keyword == SPIKE_KIND_DO ? owner : BUSTER_SPIKE_NONE,
                    .extra = BUSTER_SPIKE_NONE,
                    .state = SPIKE_STATEMENT_WAITING,
                    .kind = keyword,
                };
                stack.count += 1;
            }
        }
        else if (role == SPIKE_ROLE_ELSE)
        {
            SpikeStatement* top = spike_k5_top(&stack);
            if (top != 0 && top->state == SPIKE_STATEMENT_IF_DONE)
            {
                kernels->parents[i] = top->token;
                *top = (SpikeStatement){ .token = i, .owner = BUSTER_SPIKE_NONE, .extra = BUSTER_SPIKE_NONE, .state = SPIKE_STATEMENT_WAITING, .kind = SPIKE_KIND_ELSE };
            }
            else
            {
                result = false;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The fallback

typedef struct SpikeFallback SpikeFallback;
struct SpikeFallback
{
    // Top-level declarations the scalar parser parsed, and their tokens.
    uint32_t items;
    uint64_t tokens;
    // The first token of a declaration the scalar parser couldn't parse alone, or BUSTER_SPIKE_NONE.
    uint32_t error_token;
};

// Parses every top-level declaration that has a flagged token with the scalar parser, in place: roles, depths and
// parents of its tokens. The scalar parser sees the file-scope typedef names declared before each one. With
// `everything`, the whole file is one declaration (unbalanced brackets, or a typedef the pass can't read).
static SpikeFallback spike_fallback(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count, bool everything, SpikeMeanings* meanings,
                                    Arena* scratch)
{
    SpikeFallback result = { .items = 0, .tokens = 0, .error_token = BUSTER_SPIKE_NONE };
    uint32_t begin = 0;
    bool flagged = false;
    uint32_t visible = 0;
    for (uint32_t i = 0; i < count && result.error_token == BUSTER_SPIKE_NONE; i += 1)
    {
        flagged |= kernels->flags[i] != 0;
        const uint8_t role = kernels->roles[i];
        const bool end = everything ? i + 1 == count : spike_node_depth(kernels, i) == 0 && (role == SPIKE_ROLE_TERMINATOR || role == SPIKE_ROLE_CLOSE_BODY);
        if (end && (flagged || everything))
        {
            // The file-scope typedef names declared before this declaration.
            while (visible < kernels->typedef_count && kernels->typedef_tokens[visible] < begin)
            {
                const uint32_t id = kernels->typedef_log[visible];
                meanings->meanings[id] = kernels->typedef_names[id] == BUSTER_SPIKE_TYPEDEF_FILE ? SPIKE_MEANING_TYPEDEF : meanings->meanings[id];
                visible += 1;
            }
            SpikeTree tree = { .roles = kernels->roles + begin, .depths = kernels->depths + begin, .parents = kernels->parents + begin };
            const SpikeParseResult parse = spike_parse_scalar(kinds + begin, names + begin, i + 1 - begin, meanings, &tree, scratch);
            if (parse.error_token != BUSTER_SPIKE_NONE)
            {
                result.error_token = begin + parse.error_token;
            }
            for (uint32_t j = begin; j <= i; j += 1)
            {
                kernels->parents[j] = kernels->parents[j] == BUSTER_SPIKE_ROOT ? BUSTER_SPIKE_ROOT : kernels->parents[j] + begin;
            }
            result.items += 1;
            result.tokens += i + 1 - begin;
        }
        if (end)
        {
            begin = i + 1;
            flagged = false;
        }
    }
    for (uint32_t i = 0; i < visible; i += 1)
    {
        meanings->meanings[kernels->typedef_log[i]] = SPIKE_MEANING_ORDINARY;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The kernels' scalar twins together

typedef struct SpikeKernelResult SpikeKernelResult;
struct SpikeKernelResult
{
    SpikeFallback fallback;
    uint64_t flagged_tokens;
    bool everything;
};

static void spike_shifts_init(void);
static void spike_parent_tables_init(void);

// Reserves the kernels' arrays for up to `capacity` tokens, with padding around each per-token array, and up to
// `name_capacity` names.
static bool spike_kernels_reserve(SpikeKernels* kernels, Arena* arena, uint64_t capacity, uint64_t name_capacity)
{
    const uint64_t slots = capacity + 2 * BUSTER_SPIKE_TOKEN_PADDING;
    uint8_t* vkinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint8_t* roles = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    *kernels = (SpikeKernels){
        .vkinds = vkinds != 0 ? vkinds + BUSTER_SPIKE_TOKEN_PADDING : 0,
        .depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .enclosing = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .roles = roles != 0 ? roles + BUSTER_SPIKE_TOKEN_PADDING : 0,
        .parents = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .flags = flags != 0 ? flags + BUSTER_SPIKE_TOKEN_PADDING : 0,
        .stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .stack_keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting_thresholds = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .lefts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_names = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, name_capacity),
        .typedef_log = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_tokens = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
    };
    const bool result = kernels->vkinds != 0 && kernels->depths != 0 && kernels->enclosing != 0 && kernels->roles != 0 &&
                        kernels->parents != 0 && kernels->flags != 0 && kernels->stack != 0 && kernels->stack_keys != 0 && kernels->waiting != 0 &&
                        kernels->waiting_thresholds != 0 && kernels->lefts != 0 && kernels->typedef_names != 0 && kernels->typedef_log != 0 &&
                        kernels->typedef_tokens != 0;
    if (result)
    {
        memset(kernels->typedef_names, 0, name_capacity);
        memset(vkinds, SPIKE_KIND_END, slots);
        memset(roles, SPIKE_ROLE_NONE, slots);
        memset(flags, 0, slots);
    }
    spike_kernels_init_tables();
    spike_shifts_init();
    spike_parent_tables_init();
    return result;
}

// Every kernel's scalar twin in turn, then the fallback, into kernels->parents. `typed` and `statements` are scratch
// arrays of a byte and a SpikeStatement per token.
static SpikeKernelResult spike_parse_twins(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count, SpikeMeanings* meanings,
                                           Arena* scratch, uint8_t* typed, SpikeStatement* statements)
{
    SpikeKernelResult result = { .flagged_tokens = 0, .everything = false };
    kernels->unbalanced = false;
    kernels->unsupported = false;
    // K1, on ( [ {.
    kernels->unbalanced = !spike_brackets_scalar(kinds, spike_kind_deltas, count, kernels->depths, kernels->enclosing, kernels->stack);
    // K3.
    if (!kernels->unbalanced)
    {
        spike_typedefs(kernels, kinds, names, count);
    }
    spike_classify_scalar(kernels, kinds, names, count);
    // K2.
    spike_roles_scalar(kernels, count, typed);
    // K1's second part, and the roles after the colons it resolved.
    spike_secondary_scalar(kernels, count, kernels->stack);
    spike_after_colons(kernels, count);
    // K4 and K5.
    spike_parents_scalar(kernels, count);
    const bool statements_ok = spike_statements(kernels, count, statements);
    for (uint32_t i = 0; i < count; i += 1)
    {
        result.flagged_tokens += kernels->flags[i] != 0;
    }
    result.everything = kernels->unbalanced || kernels->unsupported || !statements_ok;
    result.fallback = spike_fallback(kernels, kinds, names, count, result.everything, meanings, scratch);
    // The names this file declared as typedefs, for the next file.
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_names[kernels->typedef_log[i]] = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The kernels in AVX-512
//
// Each works on blocks of 64 tokens in byte lanes or 32 in 16-bit lanes, and carries what crosses a block boundary in
// a small state: the depth, the openers still open, the stacks of candidates of the nearest-smaller-value searches.
// Loads past the last token read the padding (BUSTER_SPIKE_TOKEN_PADDING), which holds SPIKE_KIND_END and zeros, and
// stores past it write the arrays' slack; the results there are never read.

typedef uint8_t SpikeU8x64 __attribute__((ext_vector_type(64)));

// Index vectors for _mm512_permutex2var_epi8 and _epi16 that shift lanes up (lane i gets lane i - s of the first
// source) or down (lane i gets lane i + s), taking the second source's lane 0 where that falls outside the vector.
typedef struct SpikeShifts SpikeShifts;
struct SpikeShifts
{
    __m512i up8[6];
    __m512i down8[6];
    __m512i up16[5];
    __m512i down16[5];
    __m512i lanes8;
    __m512i lanes16;
};

static SpikeShifts spike_shifts;

static void spike_shifts_init(void)
{
    for (uint32_t k = 0; k < 6; k += 1)
    {
        uint8_t up[64];
        uint8_t down[64];
        const uint32_t s = 1u << k;
        for (uint32_t i = 0; i < 64; i += 1)
        {
            up[i] = (uint8_t)(i >= s ? i - s : 64);
            down[i] = (uint8_t)(i + s < 64 ? i + s : 64);
        }
        spike_shifts.up8[k] = _mm512_loadu_si512(up);
        spike_shifts.down8[k] = _mm512_loadu_si512(down);
    }
    for (uint32_t k = 0; k < 5; k += 1)
    {
        uint16_t up[32];
        uint16_t down[32];
        const uint32_t s = 1u << k;
        for (uint32_t i = 0; i < 32; i += 1)
        {
            up[i] = (uint16_t)(i >= s ? i - s : 32);
            down[i] = (uint16_t)(i + s < 32 ? i + s : 32);
        }
        spike_shifts.up16[k] = _mm512_loadu_si512(up);
        spike_shifts.down16[k] = _mm512_loadu_si512(down);
    }
    uint8_t lanes8[64];
    uint16_t lanes16[32];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        lanes8[i] = (uint8_t)i;
    }
    for (uint32_t i = 0; i < 32; i += 1)
    {
        lanes16[i] = (uint16_t)i;
    }
    spike_shifts.lanes8 = _mm512_loadu_si512(lanes8);
    spike_shifts.lanes16 = _mm512_loadu_si512(lanes16);
}

BUSTER_INLINE __m512i spike_up8(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi8(x, spike_shifts.up8[k], fill);
}

BUSTER_INLINE __m512i spike_down8(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi8(x, spike_shifts.down8[k], fill);
}

BUSTER_INLINE __m512i spike_up16(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi16(x, spike_shifts.up16[k], fill);
}

BUSTER_INLINE __m512i spike_down16(__m512i x, uint32_t k, __m512i fill)
{
    return _mm512_permutex2var_epi16(x, spike_shifts.down16[k], fill);
}

// The nearest previous lane whose key is below the lane's threshold, in byte lanes (keys and thresholds unsigned): the
// lane index, or a negative byte (bit 7 set) if no earlier lane in the vector qualifies. The windows of the doubling
// table are built with shifts and vpminub, and searched with in-register permutes: 32, 16, 8, 4, 2 and 1 lanes back.
BUSTER_INLINE __m512i spike_previous_smaller8(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    const __m512i one = _mm512_set1_epi8(1);
    __m512i windows[6];
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], spike_up8(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_sub_epi8(spike_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        // 0 where every key in the window is at or above the threshold, so the search moves past it.
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_sub_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// The same in 16-bit lanes: 16, 8, 4, 2 and 1 lanes back; a negative lane (bit 15 set) if none qualifies.
BUSTER_INLINE __m512i spike_previous_smaller16(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi16((short)0xFFFF);
    const __m512i one = _mm512_set1_epi16(1);
    __m512i windows[5];
    windows[0] = keys;
    for (uint32_t k = 1; k < 5; k += 1)
    {
        windows[k] = _mm512_min_epu16(windows[k - 1], spike_up16(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_sub_epi16(spike_shifts.lanes16, one);
    for (int32_t k = 4; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi16(p, windows[k]);
        const __m512i below = _mm512_min_epu16(_mm512_subs_epu16(thresholds, v), one);
        const __m512i step = _mm512_set1_epi16((short)(1 << k));
        p = _mm512_sub_epi16(p, _mm512_andnot_si512(_mm512_sub_epi16(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// The nearest next lane whose key is below the lane's threshold, in 16-bit lanes: the lane index, or 32 or more if no
// later lane in the vector qualifies.
BUSTER_INLINE __m512i spike_next_smaller16(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi16((short)0xFFFF);
    const __m512i one = _mm512_set1_epi16(1);
    __m512i windows[5];
    windows[0] = keys;
    for (uint32_t k = 1; k < 5; k += 1)
    {
        windows[k] = _mm512_min_epu16(windows[k - 1], spike_down16(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_add_epi16(spike_shifts.lanes16, one);
    for (int32_t k = 4; k >= 0; k -= 1)
    {
        // A lane at or past the end reads lane p & 31, whatever it holds: p only grows from there, and stays past it.
        const __m512i v = _mm512_permutexvar_epi16(p, windows[k]);
        const __m512i below = _mm512_min_epu16(_mm512_subs_epu16(thresholds, v), one);
        const __m512i step = _mm512_set1_epi16((short)(1 << k));
        p = _mm512_add_epi16(p, _mm512_andnot_si512(_mm512_sub_epi16(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// K1 in AVX-512

// The inclusive prefix sum of signed bytes, in six shifted adds.
BUSTER_INLINE __m512i spike_prefix_sum8(__m512i x)
{
    const __m512i zero = _mm512_setzero_si512();
    for (uint32_t k = 0; k < 6; k += 1)
    {
        x = _mm512_add_epi8(x, spike_up8(x, k, zero));
    }
    return x;
}

BUSTER_INLINE __m512i spike_prefix_sum16(__m512i x)
{
    const __m512i zero = _mm512_setzero_si512();
    for (uint32_t k = 0; k < 5; k += 1)
    {
        x = _mm512_add_epi16(x, spike_up16(x, k, zero));
    }
    return x;
}

// One block of K1's twin, for a block whose enclosing openers reach further back than the vector path's 16 depths.
static void spike_brackets_block_scalar(const uint8_t* codes, const int8_t* deltas, uint32_t begin, uint32_t end, uint32_t* depths, uint32_t* enclosing,
                                        uint32_t* open_stack, int64_t* depth)
{
    for (uint32_t i = begin; i < end; i += 1)
    {
        const int8_t delta = deltas[codes[i]];
        depths[i] = (uint32_t)*depth;
        enclosing[i] = *depth > 0 ? open_stack[*depth - 1] : BUSTER_SPIKE_NONE;
        if (delta > 0)
        {
            open_stack[*depth] = i;
        }
        *depth += delta;
        *depth = *depth < 0 ? 0 : *depth;
    }
}

// K1 in AVX-512, 64 tokens a block in byte lanes. The depth is a prefix sum of the deltas; a token's enclosing opener is
// the nearest previous token whose depth before it is lower, found in the block with spike_previous_smaller8 and before
// it in open_stack (the open opener at each depth), of which the 16 entries below the block's starting depth are one
// vpermd away. `open_stack` needs an entry per depth the input reaches. Returns false if a closer has no opener or a
// group is still open at the end.
static bool spike_brackets_avx512(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* open_stack)
{
    const __m512i delta_low = _mm512_loadu_si512(deltas);
    const __m512i delta_high = _mm512_loadu_si512(deltas + 64);
    const __m512i bias = _mm512_set1_epi8(64);
    const __m512i fill = _mm512_set1_epi8(0x7F);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
    int64_t base = 0;
    bool balanced = true;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i code = _mm512_loadu_si512(codes + block);
        const __m512i delta = _mm512_permutex2var_epi8(delta_low, code, delta_high);
        const __m512i after = spike_prefix_sum8(delta);
        const __m512i before = _mm512_sub_epi8(after, delta);
        const __m512i keys = _mm512_add_epi8(before, bias);
        const __m512i p = spike_previous_smaller8(keys, keys);
        const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        // Tokens whose opener is before the block are at or below its starting depth; at most 15 depths below it the
        // vector path handles them.
        const __mmask64 deep = _mm512_mask_cmplt_epi8_mask(~found & live_mask, before, _mm512_set1_epi8(-15));
        if (deep != 0)
        {
            spike_brackets_block_scalar(codes, deltas, block, block + live, depths, enclosing, open_stack, &base);
        }
        else
        {
            // The open openers at the 16 depths below the block's start.
            uint32_t below[16];
            for (int64_t j = 0; j < 16; j += 1)
            {
                const int64_t level = base - 16 + j;
                below[j] = level >= 0 ? open_stack[level] : BUSTER_SPIKE_NONE;
            }
            const __m512i below_vector = _mm512_loadu_si512(below);
            const __m512i base_vector = _mm512_set1_epi32((int)base);
            const __m512i block_vector = _mm512_set1_epi32((int)block);
            for (uint32_t q = 0; q < 4; q += 1)
            {
                const __m128i p_quarter = q == 0 ? _mm512_extracti32x4_epi32(p, 0) : q == 1 ? _mm512_extracti32x4_epi32(p, 1) :
                                          q == 2 ? _mm512_extracti32x4_epi32(p, 2) : _mm512_extracti32x4_epi32(p, 3);
                const __m128i before_quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                               q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
                const __m512i relative = _mm512_cvtepi8_epi32(before_quarter);
                const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu8_epi32(p_quarter));
                const __m512i outside = _mm512_permutexvar_epi32(_mm512_add_epi32(relative, _mm512_set1_epi32(15)), below_vector);
                const __mmask16 found_quarter = (__mmask16)(found >> (16 * q));
                _mm512_storeu_si512(enclosing + block + 16 * q, _mm512_mask_blend_epi32(found_quarter, outside, inside));
                _mm512_storeu_si512(depths + block + 16 * q, _mm512_add_epi32(base_vector, relative));
            }
            (void)none;
            // The openers still open at the block's end, onto open_stack: an opener is if no token from it on in the
            // block has a lower depth after it than it has.
            __m512i suffix = after;
            for (uint32_t k = 0; k < 6; k += 1)
            {
                suffix = _mm512_min_epi8(suffix, spike_down8(suffix, k, fill));
            }
            const __mmask64 openers = _mm512_cmpgt_epi8_mask(delta, _mm512_setzero_si512()) & live_mask;
            uint64_t open = openers & _mm512_cmpeq_epi8_mask(suffix, after);
            if (open != 0)
            {
                int8_t before_bytes[64];
                _mm512_storeu_si512(before_bytes, before);
                while (open != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctzll(open);
                    open &= open - 1;
                    open_stack[base + before_bytes[lane]] = block + lane;
                }
            }
            int8_t after_bytes[64];
            _mm512_storeu_si512(after_bytes, after);
            const __mmask64 negative = _mm512_cmplt_epi8_mask(_mm512_add_epi8(after, _mm512_set1_epi8((char)(base < 64 ? base : 64))), _mm512_setzero_si512()) & live_mask;
            balanced &= base >= 64 || negative == 0;
            base += after_bytes[live - 1];
            base = base < 0 ? 0 : base;
        }
    }
    return balanced && base == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// K4 in AVX-512

// Keys in 16-bit lanes are node depth * 32 + level, so the vector path takes depths below this, and the twin the rest.
#define BUSTER_SPIKE_MAX_VECTOR_DEPTH 2047

// K4's per-role tables, as bytes: a role's level, whether it closes a group, its thresholds and its parent rule; and per
// level, whether an L candidate of it is weak (spike_level_is_weak) and which way equal keys go.
typedef struct SpikeParentTables SpikeParentTables;
struct SpikeParentTables
{
    uint8_t levels[128];
    uint8_t closers[128];
    uint8_t left_thresholds[128];
    uint8_t right_thresholds[128];
    uint8_t rules[128];
    uint16_t weak[32];
    uint16_t ties[32];
};

static SpikeParentTables spike_parent_tables;

static void spike_parent_tables_init(void)
{
    for (uint32_t role = 0; role < SPIKE_ROLE_COUNT; role += 1)
    {
        spike_parent_tables.levels[role] = spike_role_levels[role];
        spike_parent_tables.closers[role] = spike_role_deltas[role] < 0;
        spike_parent_tables.left_thresholds[role] = spike_role_left_thresholds[role];
        spike_parent_tables.right_thresholds[role] = spike_role_right_thresholds[role];
        spike_parent_tables.rules[role] = spike_role_rules[role];
    }
    for (uint32_t level = 0; level < 32; level += 1)
    {
        spike_parent_tables.weak[level] = spike_level_is_weak((uint8_t)level);
        spike_parent_tables.ties[level] = spike_level_ties_left[level];
    }
}

BUSTER_INLINE __m512i spike_lookup8(const uint8_t* table, __m512i index)
{
    return _mm512_permutex2var_epi8(_mm512_loadu_si512(table), index, _mm512_loadu_si512(table + 64));
}

// The 32 roles of a block widened to 16-bit lanes, through a byte table.
BUSTER_INLINE __m512i spike_lookup8_to16(const uint8_t* table, __m512i roles)
{
    return _mm512_cvtepu8_epi16(_mm512_castsi512_si256(spike_lookup8(table, roles)));
}

// A block's keys and thresholds: 32 tokens from `block`, in 16-bit lanes.
typedef struct SpikeKeys SpikeKeys;
struct SpikeKeys
{
    __m512i keys;
    __m512i left;
    __m512i right;
    __m512i rules;
};

BUSTER_INLINE SpikeKeys spike_block_keys(const uint8_t* roles, const uint32_t* depths, uint32_t block)
{
    const __m512i role = _mm512_zextsi256_si512(_mm256_loadu_si256((const __m256i*)(roles + block)));
    const __m512i depth = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi32_epi16(_mm512_loadu_si512(depths + block))),
                                             _mm512_cvtepi32_epi16(_mm512_loadu_si512(depths + block + 16)), 1);
    const __m512i node = _mm512_sub_epi16(depth, spike_lookup8_to16(spike_parent_tables.closers, role));
    const __m512i base = _mm512_slli_epi16(node, 5);
    SpikeKeys result = {
        .keys = _mm512_add_epi16(base, spike_lookup8_to16(spike_parent_tables.levels, role)),
        .left = _mm512_add_epi16(base, spike_lookup8_to16(spike_parent_tables.left_thresholds, role)),
        .right = _mm512_add_epi16(base, spike_lookup8_to16(spike_parent_tables.right_thresholds, role)),
        .rules = spike_lookup8_to16(spike_parent_tables.rules, role),
    };
    return result;
}

// A stack of search candidates from other blocks: positions and keys, keys increasing upward, with room for the
// tokens.
typedef struct SpikeCandidates SpikeCandidates;
struct SpikeCandidates
{
    uint32_t* positions;
    uint16_t* keys;
    uint32_t count;
};

// The topmost candidate below each lane's threshold, among the stack's top 32: its index in the window (a negative
// 16-bit lane if none), with the window's keys and positions loaded. `deep` gets the lanes for which every one of the
// 32 is at or above the threshold while the stack holds more below them.
typedef struct SpikeWindow SpikeWindow;
struct SpikeWindow
{
    __m512i index;
    __m512i keys;
    __m512i positions_low;
    __m512i positions_high;
    __mmask32 valid;
    __mmask32 deep;
};

BUSTER_INLINE SpikeWindow spike_search_window(const SpikeCandidates* stack, __m512i thresholds)
{
    uint16_t keys[32];
    uint32_t positions[32];
    const uint32_t count = stack->count;
    const uint32_t start = count >= 32 ? count - 32 : 0;
    const uint32_t missing = 32 - (count - start);
    // The window's bottom lanes are padding below the stack: key 0, which a search finds only when nothing above does,
    // and which then reads as no candidate.
    for (uint32_t i = 0; i < 32; i += 1)
    {
        keys[i] = i < missing ? 0 : stack->keys[start + i - missing];
        positions[i] = i < missing ? BUSTER_SPIKE_NONE : stack->positions[start + i - missing];
    }
    SpikeWindow result;
    result.keys = _mm512_loadu_si512(keys);
    result.positions_low = _mm512_loadu_si512(positions);
    result.positions_high = _mm512_loadu_si512(positions + 16);
    // From -1, steps of 16, 8, 4, 2, 1 and 1 reach every index up to 31.
    static const short steps[6] = { 16, 8, 4, 2, 1, 1 };
    __m512i j = _mm512_set1_epi16(-1);
    for (uint32_t s = 0; s < 6; s += 1)
    {
        const __m512i candidate = _mm512_add_epi16(j, _mm512_set1_epi16(steps[s]));
        const __m512i v = _mm512_permutexvar_epi16(candidate, result.keys);
        j = _mm512_mask_mov_epi16(j, _mm512_cmplt_epu16_mask(v, thresholds), candidate);
    }
    result.index = j;
    const __mmask32 any = _mm512_cmpge_epi16_mask(j, _mm512_setzero_si512());
    result.valid = _mm512_mask_cmpge_epi16_mask(any, j, _mm512_set1_epi16((short)missing));
    result.deep = start != 0 ? ~any : 0;
    return result;
}

// The window's positions for 16 lanes of indices (low or high half of a 16-bit index vector).
BUSTER_INLINE __m512i spike_window_positions(const SpikeWindow* window, __m256i index)
{
    return _mm512_permutex2var_epi32(window->positions_low, _mm512_cvtepu16_epi32(index), window->positions_high);
}

// The scalar search of the whole stack, for a lane the window doesn't reach.
static uint32_t spike_search_stack(const SpikeCandidates* stack, uint32_t threshold, uint16_t* key)
{
    uint32_t low = 0;
    uint32_t high = stack->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (stack->keys[middle] < threshold)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const uint32_t result = low != 0 ? stack->positions[low - 1] : BUSTER_SPIKE_NONE;
    *key = low != 0 ? stack->keys[low - 1] : 0;
    return result;
}

// K4's left pass in AVX-512: every token's L and L's key, 32 tokens a block. Within the block, spike_previous_smaller16;
// before it, the stack of candidates. Then the block's keys replace the candidates they hide, and its suffix minima go
// on top. Depths must be at most BUSTER_SPIKE_MAX_VECTOR_DEPTH.
static void spike_parents_left_avx512(const uint8_t* roles, const uint32_t* depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi16((short)0xFFFF);
    stack->count = 0;
    for (uint32_t block = 0; block < count; block += 32)
    {
        const uint32_t live = count - block < 32 ? count - block : 32;
        const __mmask32 live_mask = live == 32 ? ~(__mmask32)0 : (((__mmask32)1 << live) - 1);
        const SpikeKeys keys = spike_block_keys(roles, depths, block);
        const __m512i block_keys = _mm512_mask_mov_epi16(ones, live_mask, keys.keys);
        const __m512i p = spike_previous_smaller16(block_keys, keys.left);
        const __mmask32 found = _mm512_cmplt_epu16_mask(p, _mm512_set1_epi16(32));
        const SpikeWindow window = spike_search_window(stack, keys.left);
        const __m512i inside_key = _mm512_permutexvar_epi16(p, block_keys);
        const __m512i outside_key = _mm512_maskz_permutexvar_epi16(window.valid, window.index, window.keys);
        _mm512_storeu_si512(left_keys + block, _mm512_mask_mov_epi16(outside_key, found, inside_key));
        const __m512i block_vector = _mm512_set1_epi32((int)block);
        const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
        for (uint32_t half = 0; half < 2; half += 1)
        {
            const __m256i p_half = half == 0 ? _mm512_castsi512_si256(p) : _mm512_extracti64x4_epi64(p, 1);
            const __m256i j_half = half == 0 ? _mm512_castsi512_si256(window.index) : _mm512_extracti64x4_epi64(window.index, 1);
            const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(p_half));
            const __m512i outside = _mm512_mask_mov_epi32(none, (__mmask16)(window.valid >> (16 * half)), spike_window_positions(&window, j_half));
            _mm512_storeu_si512(lefts + block + 16 * half, _mm512_mask_mov_epi32(outside, (__mmask16)(found >> (16 * half)), inside));
        }
        // The lanes the window didn't reach.
        uint32_t deep = window.deep & ~found & live_mask;
        if (deep != 0)
        {
            uint16_t thresholds[32];
            _mm512_storeu_si512(thresholds, keys.left);
            while (deep != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(deep);
                deep &= deep - 1;
                lefts[block + lane] = spike_search_stack(stack, thresholds[lane], &left_keys[block + lane]);
            }
        }
        // The block's suffix minima, which hide every candidate whose key isn't below the block's minimum.
        __m512i suffix = block_keys;
        for (uint32_t k = 0; k < 5; k += 1)
        {
            suffix = _mm512_min_epu16(suffix, spike_down16(suffix, k, ones));
        }
        const uint16_t minimum = (uint16_t)_mm_extract_epi16(_mm512_castsi512_si128(suffix), 0);
        while (stack->count != 0 && stack->keys[stack->count - 1] >= minimum)
        {
            stack->count -= 1;
        }
        const __mmask32 minima = _mm512_mask_cmplt_epu16_mask(live_mask, block_keys, spike_down16(suffix, 0, ones));
        _mm512_storeu_si512(stack->keys + stack->count, _mm512_maskz_compress_epi16(minima, block_keys));
        const __m512i lanes_low = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16)));
        const __m512i lanes_high = _mm512_add_epi32(lanes_low, _mm512_set1_epi32(16));
        const uint32_t low_count = (uint32_t)__builtin_popcount(minima & 0xFFFF);
        _mm512_storeu_si512(stack->positions + stack->count, _mm512_maskz_compress_epi32((__mmask16)minima, lanes_low));
        _mm512_storeu_si512(stack->positions + stack->count + low_count, _mm512_maskz_compress_epi32((__mmask16)(minima >> 16), lanes_high));
        stack->count += (uint32_t)__builtin_popcount(minima);
    }
}

// K4's right pass in AVX-512, from the last block to the first: every token's R, as the left pass found L, then its
// parent by its role's rule.
static void spike_parents_right_avx512(const uint8_t* roles, const uint32_t* depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents,
                                       SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi16((short)0xFFFF);
    const __m512i weak_table = _mm512_loadu_si512(spike_parent_tables.weak);
    const __m512i ties_table = _mm512_loadu_si512(spike_parent_tables.ties);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
    stack->count = 0;
    const uint32_t blocks = (count + 31) / 32;
    for (uint32_t b = blocks; b-- > 0;)
    {
        const uint32_t block = b * 32;
        const uint32_t live = count - block < 32 ? count - block : 32;
        const __mmask32 live_mask = live == 32 ? ~(__mmask32)0 : (((__mmask32)1 << live) - 1);
        const SpikeKeys keys = spike_block_keys(roles, depths, block);
        const __m512i block_keys = _mm512_mask_mov_epi16(ones, live_mask, keys.keys);
        const __m512i p = spike_next_smaller16(block_keys, keys.right);
        const __mmask32 found = _mm512_cmplt_epu16_mask(p, _mm512_set1_epi16(32));
        const SpikeWindow window = spike_search_window(stack, keys.right);
        const __m512i inside_key = _mm512_permutexvar_epi16(p, block_keys);
        const __m512i outside_key = _mm512_maskz_permutexvar_epi16(window.valid, window.index, window.keys);
        __m512i right_key = _mm512_mask_mov_epi16(outside_key, found, inside_key);
        const __m512i block_vector = _mm512_set1_epi32((int)block);
        __m512i right[2];
        for (uint32_t half = 0; half < 2; half += 1)
        {
            const __m256i p_half = half == 0 ? _mm512_castsi512_si256(p) : _mm512_extracti64x4_epi64(p, 1);
            const __m256i j_half = half == 0 ? _mm512_castsi512_si256(window.index) : _mm512_extracti64x4_epi64(window.index, 1);
            const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(p_half));
            const __m512i outside = _mm512_mask_mov_epi32(none, (__mmask16)(window.valid >> (16 * half)), spike_window_positions(&window, j_half));
            right[half] = _mm512_mask_mov_epi32(outside, (__mmask16)(found >> (16 * half)), inside);
        }
        uint32_t deep = window.deep & ~found & live_mask;
        if (deep != 0)
        {
            uint16_t thresholds[32];
            uint16_t key_lanes[32];
            uint32_t right_lanes[32];
            _mm512_storeu_si512(thresholds, keys.right);
            _mm512_storeu_si512(key_lanes, right_key);
            _mm512_storeu_si512(right_lanes, right[0]);
            _mm512_storeu_si512(right_lanes + 16, right[1]);
            while (deep != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(deep);
                deep &= deep - 1;
                right_lanes[lane] = spike_search_stack(stack, thresholds[lane], &key_lanes[lane]);
            }
            right_key = _mm512_loadu_si512(key_lanes);
            right[0] = _mm512_loadu_si512(right_lanes);
            right[1] = _mm512_loadu_si512(right_lanes + 16);
        }
        // The parents.
        const __m512i left_key = _mm512_loadu_si512(left_keys + block);
        const __m512i left_level = _mm512_and_si512(left_key, _mm512_set1_epi16(31));
        const __mmask32 weak = _mm512_test_epi16_mask(_mm512_permutexvar_epi16(left_level, weak_table), ones);
        const __mmask32 ties = _mm512_test_epi16_mask(_mm512_permutexvar_epi16(left_level, ties_table), ones);
        const __mmask32 greater = _mm512_cmpgt_epu16_mask(left_key, right_key);
        const __mmask32 equal = _mm512_cmpeq_epi16_mask(left_key, right_key);
        const __mmask32 rule_left = _mm512_cmpeq_epi16_mask(keys.rules, _mm512_set1_epi16(SPIKE_RULE_LEFT));
        const __mmask32 rule_right = _mm512_cmpge_epu16_mask(keys.rules, _mm512_set1_epi16(SPIKE_RULE_RIGHT));
        __mmask32 left_valid = 0;
        __mmask32 right_valid = 0;
        __m512i left[2];
        for (uint32_t half = 0; half < 2; half += 1)
        {
            left[half] = _mm512_loadu_si512(lefts + block + 16 * half);
            left_valid |= (__mmask32)_mm512_cmpneq_epi32_mask(left[half], none) << (16 * half);
            right_valid |= (__mmask32)_mm512_cmpneq_epi32_mask(right[half], none) << (16 * half);
        }
        const __mmask32 select_left = left_valid & (~right_valid | (~weak & (greater | (equal & ties))));
        const __mmask32 take_left = rule_left | (~rule_right & select_left);
        for (uint32_t half = 0; half < 2; half += 1)
        {
            _mm512_storeu_si512(parents + block + 16 * half, _mm512_mask_mov_epi32(right[half], (__mmask16)(take_left >> (16 * half)), left[half]));
        }
        // The block's prefix minima go on the stack, nearest on top: pushed from the right.
        __m512i prefix = block_keys;
        for (uint32_t k = 0; k < 5; k += 1)
        {
            prefix = _mm512_min_epu16(prefix, spike_up16(prefix, k, ones));
        }
        const uint16_t minimum = (uint16_t)_mm_extract_epi16(_mm512_extracti32x4_epi32(prefix, 3), 7);
        while (stack->count != 0 && stack->keys[stack->count - 1] >= minimum)
        {
            stack->count -= 1;
        }
        const __mmask32 minima = _mm512_mask_cmplt_epu16_mask(live_mask, block_keys, spike_up16(prefix, 0, ones));
        const uint32_t n = (uint32_t)__builtin_popcount(minima);
        // Reversed: lane i of the stores is minimum n - 1 - i.
        const __m512i reverse = _mm512_sub_epi16(_mm512_set1_epi16((short)(n - 1)), spike_shifts.lanes16);
        _mm512_storeu_si512(stack->keys + stack->count, _mm512_permutexvar_epi16(reverse, _mm512_maskz_compress_epi16(minima, block_keys)));
        const __m512i lanes_low = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16)));
        const __m512i lanes_high = _mm512_add_epi32(lanes_low, _mm512_set1_epi32(16));
        uint32_t positions[32];
        const uint32_t low_count = (uint32_t)__builtin_popcount(minima & 0xFFFF);
        _mm512_storeu_si512(positions, _mm512_maskz_compress_epi32((__mmask16)minima, lanes_low));
        _mm512_storeu_si512(positions + low_count, _mm512_maskz_compress_epi32((__mmask16)(minima >> 16), lanes_high));
        for (uint32_t i = 0; i < n; i += 1)
        {
            stack->positions[stack->count + i] = positions[n - 1 - i];
        }
        stack->count += n;
    }
}
