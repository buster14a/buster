// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The data-parallel parsing spike (#186): does preprocessed C parse faster on one Zen 5 core with branch-free AVX-512
// kernels than with a scalar shunting-yard parser? Both build the same tree: a parent for every token. This file holds
// the token refinement the parser reads, the tree's definition, the scalar parser (the oracle and the fallback) and the
// kernels with their scalar twins. parse_spike_benchmark.c includes it and runs the `spike-parse` command.
//
// docs/data-parallel-parsing.md describes the design and its measurements on the 9700X.
//
// Layout, in this order:
//   - Token kinds: SpikeKind (BUSTER_SPIKE_KINDS), the keyword table (spike_keywords) and SpikeTokens.
//   - Refinement: the identifier table (SpikeNames, spike_intern), spike_refine (lexer tokens to parser tokens: keywords
//     resolved, literals merged, pragma lines dropped, identifiers interned).
//   - The tree: roles, levels, thresholds and rules (BUSTER_SPIKE_ROLES), and spike_select, which picks a parent from L
//     and R.
//   - The scalar parser (spike_parse_scalar): grammar modes on a frame stack, the statement stack, and the precedence
//     stacks that resolve L and R as tokens arrive. It is the oracle and the fallback.
//   - The kernels' scalar twins: K1 brackets, K3 type names, K2 roles, K4 parents, K5 statements, the fallback, and
//     spike_parse_twins, all of them in turn.
//   - The kernels in AVX-512: K1 in byte lanes (with each block's view for K2, SpikeBracketBlock; and in 16-bit lanes for
//     the comparison), K4 in 16-bit and in byte lanes, K2 (with the statement contexts K5 reads), K3's classification, K1's second part and K5 (spike_statements_avx512:
//     owners by compress and expand, and a sparse loop for else and do); K4 with the stack's top in registers
//     (spike_top_parents8_avx512, the vector path's: every block's in-block work, then the two passes' window work);
//     spike_parse_vector, the data-parallel parser.
//   - The postorder, from the parents: spike_postorder (a walk that emits each subtree's end) and
//     spike_postorder_sorted (positions without a branch on the tree).

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

// FNV-1a (Fowler, Noll and Vo; public domain), its high bits folded into the low ones the table indexes by.
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
BUSTER_INLINE bool spike_level_is_weak(uint8_t level)
{
    return level == SPIKE_LEVEL_OPENER || level == SPIKE_LEVEL_TERMINATOR || level == SPIKE_LEVEL_HEAD;
}

// The parent from L and R and their keys (depth * BUSTER_SPIKE_LEVELS + level) under SPIKE_RULE_SELECT: equal keys are on
// the same level, which says which way they go.
BUSTER_INLINE uint32_t spike_select_keys(uint32_t left, uint32_t left_key, uint32_t right, uint32_t right_key)
{
    uint32_t result;
    const uint8_t left_level = (uint8_t)(left_key % BUSTER_SPIKE_LEVELS);
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
        const bool take_left = left_key > right_key || (left_key == right_key && spike_level_ties_left[left_level]);
        result = take_left ? left : right;
    }
    return result;
}

// The same from depths and levels.
BUSTER_INLINE uint32_t spike_select(uint32_t left, uint32_t left_depth, uint8_t left_level, uint32_t right, uint32_t right_depth, uint8_t right_level)
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

// A candidate on the scalar parser's L stack.
typedef struct SpikeParserCandidate SpikeParserCandidate;
struct SpikeParserCandidate
{
    uint32_t position;
    uint32_t key;
};

// A token on the R stack, waiting for the token that ends its search, with what it needs then: its L and L's key (none
// for SPIKE_RULE_RIGHT, which then takes R).
typedef struct SpikeParserWaiting SpikeParserWaiting;
struct SpikeParserWaiting
{
    uint32_t position;
    uint32_t threshold;
    uint32_t left;
    uint32_t left_key;
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
    SpikeParserCandidate* candidates;
    uint32_t candidate_count;
    SpikeParserWaiting* waiting;
    uint32_t waiting_count;
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

BUSTER_INLINE void spike_parser_error(SpikeParser* parser, uint32_t token, const char* message)
{
    if (parser->error_token == BUSTER_SPIKE_NONE)
    {
        parser->error_token = token;
        parser->error_message = message;
    }
}

BUSTER_INLINE bool spike_parser_is_typedef_name(const SpikeParser* parser, uint32_t token)
{
    return parser->kinds[token] == SPIKE_KIND_IDENTIFIER && parser->meanings[parser->names[token]] == SPIKE_MEANING_TYPEDEF;
}

// Whether the token can start a type name: a specifier or qualifier keyword, an attribute or a typedef name.
BUSTER_INLINE bool spike_parser_starts_type(const SpikeParser* parser, uint32_t token)
{
    const uint8_t kind = parser->kinds[token];
    return kind == SPIKE_KIND_TYPE || kind == SPIKE_KIND_STRUCT || kind == SPIKE_KIND_ENUM || kind == SPIKE_KIND_QUALIFIER || kind == SPIKE_KIND_ATOMIC ||
           kind == SPIKE_KIND_TYPEOF || kind == SPIKE_KIND_ATTRIBUTE || spike_parser_is_typedef_name(parser, token);
}

// Whether the token can start a declaration: a type name, or a storage class, typedef, _Alignas, _Static_assert,
// __label__ or __extension__ before one. In a block, a typedef name followed by `:` is a label instead.
BUSTER_INLINE bool spike_parser_starts_declaration(const SpikeParser* parser, uint32_t token, bool block)
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

BUSTER_INLINE void spike_parser_bind(SpikeParser* parser, uint32_t token, uint8_t meaning)
{
    const uint32_t name = parser->names[token];
    parser->log_names[parser->log_count] = name;
    parser->log_meanings[parser->log_count] = parser->meanings[name];
    parser->log_count += 1;
    parser->meanings[name] = meaning;
}

BUSTER_INLINE void spike_parser_restore(SpikeParser* parser, uint32_t mark)
{
    while (parser->log_count > mark)
    {
        parser->log_count -= 1;
        parser->meanings[parser->log_names[parser->log_count]] = parser->log_meanings[parser->log_count];
    }
}

// The innermost frame with a scope: where a declaration's names are bound.
BUSTER_INLINE bool spike_parser_binds(const SpikeParser* parser)
{
    return (parser->frames[parser->frame_count - 1].flags & BUSTER_SPIKE_FRAME_NO_BIND) == 0;
}

// Pushes a frame for the group `opener` opens; the current frame continues in `continuation` once it closes.
BUSTER_INLINE SpikeFrame* spike_parser_push(SpikeParser* parser, uint32_t opener, uint8_t role, uint8_t mode, uint8_t continuation, bool scope)
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

BUSTER_INLINE SpikeStatement* spike_statement_top(SpikeParser* parser)
{
    return parser->statement_count != 0 ? &parser->statements[parser->statement_count - 1] : 0;
}

BUSTER_INLINE void spike_statement_pop(SpikeParser* parser)
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
BUSTER_INLINE void spike_statement_complete(SpikeParser* parser)
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
BUSTER_INLINE void spike_statement_flush(SpikeParser* parser)
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
BUSTER_INLINE uint32_t spike_statement_owner(SpikeParser* parser)
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

BUSTER_INLINE void spike_statement_open_block(SpikeParser* parser, bool statement)
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

BUSTER_INLINE void spike_statement_close_block(SpikeParser* parser, uint32_t closer)
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

BUSTER_INLINE void spike_statement_terminator(SpikeParser* parser, uint32_t terminator)
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
BUSTER_INLINE void spike_statement_head(SpikeParser* parser, uint32_t head, uint8_t kind, uint32_t scope)
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

BUSTER_INLINE void spike_statement_else(SpikeParser* parser, uint32_t token)
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

// Packs a role's level, left and right thresholds and rule for spike_parser_emit, which reads them all at once.
#define BUSTER_SPIKE_ROLE_INFO(role) ((uint32_t)spike_role_levels[role] | (uint32_t)spike_role_left_thresholds[role] << 8 | \
                                      (uint32_t)spike_role_right_thresholds[role] << 16 | (uint32_t)spike_role_rules[role] << 24)

static uint32_t spike_role_info[SPIKE_ROLE_COUNT];

static void spike_role_info_init(void)
{
    for (uint32_t role = 0; role < SPIKE_ROLE_COUNT; role += 1)
    {
        spike_role_info[role] = BUSTER_SPIKE_ROLE_INFO(role);
    }
}

// Adds token `token` with its role and depth to the tree. Keys count depths from 1 here, so that a sentinel at the
// bottom of each stack (key 0 and threshold 0, under every key and threshold) ends every search without a check of the
// count: an L of BUSTER_SPIKE_NONE, and an R search that nothing ends.
BUSTER_INLINE void spike_parser_emit(SpikeParser* parser, uint32_t token, uint8_t role, uint32_t depth)
{
    uint32_t* const parents = parser->parents;
    SpikeParserCandidate* const candidates = parser->candidates;
    SpikeParserWaiting* const waiting = parser->waiting;
    uint32_t candidate_count = parser->candidate_count;
    uint32_t waiting_count = parser->waiting_count;
    const uint32_t info = spike_role_info[role];
    const uint32_t base = (depth + 1) * BUSTER_SPIKE_LEVELS;
    const uint32_t key = base + (info & 0xff);
    const uint8_t rule = (uint8_t)(info >> 24);
    parser->roles[token] = role;
    parser->depths[token] = depth;
    parents[token] = BUSTER_SPIKE_NONE;
    // The tokens this one ends the right search of. The statement stack has already set the parents of what a head
    // owns.
    while (waiting[waiting_count - 1].threshold > key)
    {
        waiting_count -= 1;
        const SpikeParserWaiting entry = waiting[waiting_count];
        const uint32_t parent = spike_select_keys(entry.left, entry.left_key, token, key);
        if (parents[entry.position] == BUSTER_SPIKE_NONE)
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
    const SpikeParserCandidate left = candidates[candidate_count - 1];
    while (candidates[candidate_count - 1].key >= key)
    {
        candidate_count -= 1;
    }
    candidates[candidate_count] = (SpikeParserCandidate){ .position = token, .key = key };
    candidate_count += 1;
    if (rule == SPIKE_RULE_LEFT)
    {
        parents[token] = left.position;
    }
    else if (rule != SPIKE_RULE_OPENER)
    {
        const bool right_only = rule == SPIKE_RULE_RIGHT;
        waiting[waiting_count] = (SpikeParserWaiting){
            .position = token,
            .threshold = base + ((info >> 16) & 0xff),
            .left = right_only ? BUSTER_SPIKE_NONE : left.position,
            .left_key = left.key,
        };
        waiting_count += 1;
    }
    parser->candidate_count = candidate_count;
    parser->waiting_count = waiting_count;
}

// Resolves what is still waiting at the end, which has no R.
static void spike_parser_finish(SpikeParser* parser)
{
    while (parser->waiting_count > 1)
    {
        parser->waiting_count -= 1;
        const SpikeParserWaiting entry = parser->waiting[parser->waiting_count];
        const uint32_t parent = spike_select_keys(entry.left, entry.left_key, BUSTER_SPIKE_NONE, 0);
        if (parser->parents[entry.position] == BUSTER_SPIKE_NONE)
        {
            parser->parents[entry.position] = parent;
        }
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

BUSTER_INLINE bool spike_frame_is_block(const SpikeFrame* frame)
{
    return frame->role == SPIKE_ROLE_OPEN_BLOCK || frame->role == SPIKE_ROLE_OPEN_BODY;
}

// Whether a comma separates declarators or is the comma operator (COMMA), or separates arguments, parameters,
// initializers or enumerators (SEPARATOR, below the specifiers), by the innermost group of ( [ { around it: the ?: and
// case groups don't count, so the kernels decide it from the bracket depth of ( [ { alone.
BUSTER_INLINE uint8_t spike_frame_comma_role(const SpikeParser* parser)
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

BUSTER_INLINE bool spike_closer_matches(uint8_t kind, uint8_t opener_role)
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
BUSTER_INLINE void spike_parser_close(SpikeParser* parser, uint32_t i, SpikeStep* step)
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
BUSTER_INLINE SpikeFrame* spike_parser_open(SpikeParser* parser, uint32_t i, uint8_t role, uint8_t mode, uint8_t continuation, bool scope, SpikeStep* step)
{
    step->role = role;
    step->depth = parser->frame_count - 1;
    return spike_parser_push(parser, i, role, mode, continuation, scope);
}

BUSTER_INLINE bool spike_kind_is_closer(uint8_t kind)
{
    return kind == SPIKE_KIND_R_PAREN || kind == SPIKE_KIND_R_SQUARE || kind == SPIKE_KIND_R_BRACE;
}

// The end of a declaration's `;`, or of an expression statement's, in any frame that holds them.
BUSTER_INLINE void spike_parser_terminator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_item(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_specifiers(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_tag(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_declarator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_after_declarator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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
BUSTER_INLINE void spike_parser_operand(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE uint8_t spike_binary_role(uint8_t kind)
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
BUSTER_INLINE void spike_parser_operator(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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
BUSTER_INLINE void spike_parser_paren(SpikeParser* parser, uint32_t i, SpikeFrame* frame, SpikeStep* step)
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

BUSTER_INLINE void spike_parser_token(SpikeParser* parser, uint32_t i)
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
        .candidates = BUSTER_ARENA_PUSH_ARRAY(scratch, SpikeParserCandidate, slots + 1),
        .candidate_count = 1,
        .waiting = BUSTER_ARENA_PUSH_ARRAY(scratch, SpikeParserWaiting, slots + 1),
        .waiting_count = 1,
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
    if (parser.frames == 0 || parser.candidates == 0 || parser.waiting == 0 || parser.log_names == 0 || parser.log_meanings == 0 || parser.statements == 0)
    {
        result.error_token = 0;
        result.error_message = "out of memory";
    }
    else
    {
        // The stacks' sentinels.
        parser.candidates[0] = (SpikeParserCandidate){ .position = BUSTER_SPIKE_NONE, .key = 0 };
        parser.waiting[0] = (SpikeParserWaiting){ .position = BUSTER_SPIKE_NONE, .threshold = 0, .left = BUSTER_SPIKE_NONE, .left_key = 0 };
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
        if (parser.error_token == BUSTER_SPIKE_NONE)
        {
            spike_parser_finish(&parser);
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

// What K1 finds of each block of 64 tokens, which K2 reads instead of working it out again from the depths and enclosing
// openers: each token's enclosing opener as a lane of the block (0xFF if the opener is before the block), the depth
// before each token relative to the block's first token, and the block's openers still open at its end.
typedef struct SpikeBracketBlock SpikeBracketBlock;
struct SpikeBracketBlock
{
    uint8_t opener_lanes[64];
    int8_t relative_depths[64];
    uint64_t open;
} __attribute__((aligned(64)));

// The working arrays of the kernels, by token, and what they found.
typedef struct SpikeKernels SpikeKernels;
struct SpikeKernels
{
    uint8_t* vkinds;
    // The depth before each token of ( [ { (K1), and each token's node depth in every group, ? : and case : included,
    // where a closer is outside its group (K1's second part).
    uint32_t* depths;
    uint32_t* node_depths;
    uint32_t* enclosing;
    // K1's view of each block of 64 tokens.
    SpikeBracketBlock* bracket_blocks;
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
    // By name: 0, or SPIKE_TYPEDEF_FILE, or SPIKE_TYPEDEF_SCOPED for a name some block declares as a typedef.
    uint8_t* typedef_names;
    // The file's typedef names in order, each with the token that declares it and its scope, for the fallback.
    uint32_t* typedef_log;
    uint32_t* typedef_tokens;
    uint8_t* typedef_scopes;
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

// K1's view of the block of tokens [block, block + live) from their depths and enclosing openers (`codes` and `deltas`
// as for spike_brackets_scalar): the twin's, which the check compares with the vector K1's.
static void spike_bracket_block(const uint8_t* codes, const int8_t* deltas, const uint32_t* depths, const uint32_t* enclosing, uint32_t block, uint32_t live,
                                SpikeBracketBlock* out)
{
    uint64_t open = 0;
    int64_t lowest = INT64_MAX;
    for (uint32_t lane = 64; lane-- > 0;)
    {
        const uint32_t i = block + lane;
        const bool alive = lane < live;
        const uint32_t opener = alive ? enclosing[i] : BUSTER_SPIKE_NONE;
        const int64_t depth = alive ? depths[i] : 0;
        const int8_t delta = alive ? deltas[codes[i]] : 0;
        out->opener_lanes[lane] = opener != BUSTER_SPIKE_NONE && opener >= block && opener - block < 64 ? (uint8_t)(opener - block) : 0xFF;
        out->relative_depths[lane] = (int8_t)(depth - (alive ? (int64_t)depths[block] : depth));
        // Open at the end if no token after it leaves a lower depth than its group's.
        open |= (uint64_t)(alive && delta > 0 && depth < lowest) << lane;
        lowest = alive && depth + delta < lowest ? depth + delta : lowest;
    }
    out->open = open;
}

// Every block's, as spike_brackets_avx512 writes them, from the twin's depths and enclosing openers.
static void spike_bracket_blocks(const uint8_t* codes, const int8_t* deltas, const uint32_t* depths, const uint32_t* enclosing, uint32_t count, SpikeBracketBlock* blocks)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        spike_bracket_block(codes, deltas, depths, enclosing, block, count - block < 64 ? count - block : 64, &blocks[block / 64]);
    }
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
    // The typedef keywords, 64 tokens at a time; a declaration the pass reads may run past later ones' blocks.
    uint32_t next = 0;
    for (uint32_t block = 0; block < count && !kernels->unsupported; block += 64)
    {
        const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
        uint64_t keywords = _mm512_mask_cmpeq_epi8_mask(live, _mm512_loadu_si512(kinds + block), _mm512_set1_epi8(SPIKE_KIND_TYPEDEF));
        while (keywords != 0 && !kernels->unsupported)
        {
            const uint32_t t = block + (uint32_t)__builtin_ctzll(keywords);
            keywords &= keywords - 1;
            if (t >= next)
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
                        kernels->typedef_scopes[kernels->typedef_count] = scope;
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
                next = i;
            }
        }
    }
}

// Marks the file's typedef names again from its log, as spike_typedefs left them, for a stage run apart from the pass.
static void spike_typedefs_mark(SpikeKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        const uint32_t id = kernels->typedef_log[i];
        kernels->typedef_names[id] = kernels->typedef_names[id] == BUSTER_SPIKE_TYPEDEF_SCOPED ? BUSTER_SPIKE_TYPEDEF_SCOPED : kernels->typedef_scopes[i];
    }
}

// Clears the file's typedef names, for the next file.
static void spike_typedefs_clear(SpikeKernels* kernels)
{
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_names[kernels->typedef_log[i]] = 0;
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
// label if none is (a `:` it has resolved before is resolved again the same way). Then the depth of the secondary
// brackets, added to the depth of ( [ {, gives every token's node depth for K4. `secondary` has a slot per token for
// the matching stack.
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
        else if (role == SPIKE_ROLE_CLOSE_TERNARY || role == SPIKE_ROLE_CLOSE_CASE || role == SPIKE_ROLE_LABEL)
        {
            const uint32_t depth = kernels->depths[i];
            // Openers left open in a group that has ended: invalid C, for the scalar parser to report.
            while (open != 0 && kernels->depths[secondary[open - 1]] > depth)
            {
                kernels->flags[secondary[open - 1]] = 1;
                kernels->unbalanced = true;
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
        kernels->unbalanced = true;
        open -= 1;
    }
    // The node depths: the depth of ( [ { and of ? : and case : groups before each token, less one for a closer. Every
    // closer here has its opener (or the file is unbalanced, and the scalar parser takes it), so the sum never goes
    // below 0.
    uint32_t inner = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t role = kernels->roles[i];
        const uint32_t before = inner;
        inner += role == SPIKE_ROLE_OPEN_TERNARY || role == SPIKE_ROLE_OPEN_CASE;
        inner -= role == SPIKE_ROLE_CLOSE_TERNARY || role == SPIKE_ROLE_CLOSE_CASE;
        kernels->node_depths[i] = kernels->depths[i] + before - (spike_role_deltas[role] < 0);
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
    return kernels->node_depths[token];
}

BUSTER_INLINE void spike_parents_resolve(SpikeKernels* kernels, uint32_t token, uint32_t right, uint32_t right_key)
{
    const uint8_t role = kernels->roles[token];
    uint32_t parent;
    if (spike_role_rules[role] == SPIKE_RULE_RIGHT || spike_role_rules[role] == SPIKE_RULE_OPENER)
    {
        parent = right;
    }
    else
    {
        parent = spike_select_keys(kernels->lefts[token], kernels->left_key_values[token], right, right_key);
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
            spike_parents_resolve(kernels, kernels->waiting[right_count], i, key);
        }
        const uint32_t left_threshold = base + spike_role_left_thresholds[role];
        while (left_count != 0 && kernels->stack_keys[left_count - 1] >= left_threshold)
        {
            left_count -= 1;
        }
        const uint32_t left = left_count != 0 ? kernels->stack[left_count - 1] : BUSTER_SPIKE_NONE;
        const uint32_t left_key = left_count != 0 ? kernels->stack_keys[left_count - 1] : 0;
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
            kernels->left_key_values[i] = left_key;
            kernels->waiting[right_count] = i;
            kernels->waiting_thresholds[right_count] = base + spike_role_right_thresholds[role];
            right_count += 1;
        }
    }
    while (right_count != 0)
    {
        right_count -= 1;
        spike_parents_resolve(kernels, kernels->waiting[right_count], BUSTER_SPIKE_NONE, 0);
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

// One token through the statement stack, if it is a statement-level token of a block: the stack sets the parents of the
// statements heads own, of `else` and of a do-statement's parts. Returns false if it is an else without an if.
static bool spike_statement_event(SpikeKernels* kernels, SpikeStatementStack* stack, uint32_t i)
{
    bool result = true;
    const uint8_t role = kernels->roles[i];
    const uint32_t opener = kernels->enclosing[i];
    const uint8_t group = opener != BUSTER_SPIKE_NONE ? kernels->roles[opener] : SPIKE_ROLE_NONE;
    // A closer's statement context is its opener's; the enclosing groups are those of ( [ { only, since no statement is
    // inside a ? : or case group.
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
            spike_k5_flush(stack);
            owner = spike_k5_owner(stack);
        }
        stack->items[stack->count] = (SpikeStatement){ .token = BUSTER_SPIKE_NONE, .owner = owner, .extra = BUSTER_SPIKE_NONE, .state = SPIKE_STATEMENT_BLOCK, .kind = statement };
        stack->count += 1;
    }
    else if (role == SPIKE_ROLE_CLOSE_BLOCK || role == SPIKE_ROLE_CLOSE_BODY)
    {
        spike_k5_flush(stack);
        SpikeStatement* top = spike_k5_top(stack);
        while (top != 0 && top->state != SPIKE_STATEMENT_BLOCK)
        {
            stack->count -= 1;
            top = spike_k5_top(stack);
        }
        if (top != 0)
        {
            const bool statement = top->kind != 0;
            if (top->owner != BUSTER_SPIKE_NONE)
            {
                kernels->parents[i] = top->owner;
            }
            stack->count -= 1;
            if (statement)
            {
                spike_k5_complete(stack);
            }
        }
    }
    else if (role == SPIKE_ROLE_TERMINATOR && in_block)
    {
        spike_k5_flush(stack);
        SpikeStatement* top = spike_k5_top(stack);
        if (top != 0 && top->state == SPIKE_STATEMENT_DO_WHILE)
        {
            kernels->parents[top->token] = i;
            kernels->parents[top->extra] = i;
            if (top->owner != BUSTER_SPIKE_NONE)
            {
                kernels->parents[i] = top->owner;
            }
            stack->count -= 1;
        }
        else
        {
            const uint32_t owner = spike_k5_owner(stack);
            if (owner != BUSTER_SPIKE_NONE)
            {
                kernels->parents[i] = owner;
            }
        }
        spike_k5_complete(stack);
    }
    else if (((role == SPIKE_ROLE_CLOSE_CONDITION || role == SPIKE_ROLE_CLOSE_FOR) && closer_in_block) ||
             ((role == SPIKE_ROLE_CLOSE_CASE || role == SPIKE_ROLE_LABEL || role == SPIKE_ROLE_DO) && in_block))
    {
        const uint8_t keyword = role == SPIKE_ROLE_CLOSE_CONDITION ? kernels->vkinds[opener - 1] : role == SPIKE_ROLE_CLOSE_FOR ? SPIKE_KIND_FOR :
                                role == SPIKE_ROLE_DO ? SPIKE_KIND_DO : SPIKE_KIND_CASE;
        spike_k5_flush(stack);
        SpikeStatement* top = spike_k5_top(stack);
        if (keyword == SPIKE_KIND_WHILE && top != 0 && top->state == SPIKE_STATEMENT_DO_DONE)
        {
            top->state = SPIKE_STATEMENT_DO_WHILE;
            top->extra = i;
        }
        else
        {
            const uint32_t owner = spike_k5_owner(stack);
            if (owner != BUSTER_SPIKE_NONE && keyword != SPIKE_KIND_DO)
            {
                kernels->parents[i] = owner;
            }
            stack->items[stack->count] = (SpikeStatement){
                .token = i,
                .owner = keyword == SPIKE_KIND_DO ? owner : BUSTER_SPIKE_NONE,
                .extra = BUSTER_SPIKE_NONE,
                .state = SPIKE_STATEMENT_WAITING,
                .kind = keyword,
            };
            stack->count += 1;
        }
    }
    else if (role == SPIKE_ROLE_ELSE)
    {
        SpikeStatement* top = spike_k5_top(stack);
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
    return result;
}

// K5, the twin: every token through spike_statement_event. Returns false if an else has no if.
static bool spike_statements(SpikeKernels* kernels, uint32_t count, SpikeStatement* items)
{
    SpikeStatementStack stack = { .items = items, .count = 0, .parents = kernels->parents };
    bool result = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result &= spike_statement_event(kernels, &stack, i);
    }
    return result;
}

// What the vector K5 reads of each token's surroundings, a bit per token for 64 tokens: whether its statement context is
// a block (OPEN_BLOCK or OPEN_BODY), as spike_statement_event reads it (the group around the token, or for a closer of
// ( [ { the group around its opener); and whether it is a closer whose opener follows `if`, or `while`. The vector K2
// writes them, and spike_statement_contexts checks them.
typedef struct SpikeStatementContext SpikeStatementContext;
struct SpikeStatementContext
{
    uint64_t blocks;
    uint64_t ifs;
    uint64_t whiles;
};

// The statement contexts of tokens [block, block + live).
static SpikeStatementContext spike_statement_context_block(const SpikeKernels* kernels, uint32_t block, uint32_t live)
{
    SpikeStatementContext result = { .blocks = 0, .ifs = 0, .whiles = 0 };
    for (uint32_t lane = 0; lane < live; lane += 1)
    {
        const uint32_t i = block + lane;
        const uint32_t opener = kernels->enclosing[i];
        const bool closer = spike_kind_deltas[kernels->vkinds[i]] < 0;
        const uint32_t group = closer && opener != BUSTER_SPIKE_NONE ? kernels->enclosing[opener] : opener;
        const uint8_t role = group != BUSTER_SPIKE_NONE ? kernels->roles[group] : SPIKE_ROLE_NONE;
        const uint8_t keyword = closer && opener != BUSTER_SPIKE_NONE ? kernels->vkinds[opener - 1] : SPIKE_KIND_END;
        result.blocks |= (uint64_t)(role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY) << lane;
        result.ifs |= (uint64_t)(keyword == SPIKE_KIND_IF) << lane;
        result.whiles |= (uint64_t)(keyword == SPIKE_KIND_WHILE) << lane;
    }
    return result;
}

// Every block's statement contexts, one per 64 tokens.
static void spike_statement_contexts(const SpikeKernels* kernels, uint32_t count, SpikeStatementContext* contexts)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        contexts[block / 64] = spike_statement_context_block(kernels, block, count - block < 64 ? count - block : 64);
    }
}

// The spike's K5 in AVX-512: only the tokens whose roles the statement stack reads, from a mask per block, through the
// twin's stack. spike_statements_avx512, the vector path's, replaced it.
static bool spike_statements_mask_avx512(SpikeKernels* kernels, uint32_t count, SpikeStatement* items)
{
    SpikeStatementStack stack = { .items = items, .count = 0, .parents = kernels->parents };
    bool result = true;
    uint8_t relevant[128] = { 0 };
    relevant[SPIKE_ROLE_OPEN_BLOCK] = 1;
    relevant[SPIKE_ROLE_OPEN_BODY] = 1;
    relevant[SPIKE_ROLE_CLOSE_BLOCK] = 1;
    relevant[SPIKE_ROLE_CLOSE_BODY] = 1;
    relevant[SPIKE_ROLE_TERMINATOR] = 1;
    relevant[SPIKE_ROLE_CLOSE_CONDITION] = 1;
    relevant[SPIKE_ROLE_CLOSE_FOR] = 1;
    relevant[SPIKE_ROLE_CLOSE_CASE] = 1;
    relevant[SPIKE_ROLE_LABEL] = 1;
    relevant[SPIKE_ROLE_DO] = 1;
    relevant[SPIKE_ROLE_ELSE] = 1;
    const __m512i relevant_low = _mm512_loadu_si512(relevant);
    const __m512i relevant_high = _mm512_loadu_si512(relevant + 64);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i bits = _mm512_permutex2var_epi8(relevant_low, _mm512_loadu_si512(kernels->roles + block), relevant_high);
        uint64_t lanes = _mm512_test_epi8_mask(bits, bits) & (count - block < 64 ? (((uint64_t)1 << (count - block)) - 1) : ~(uint64_t)0);
        while (lanes != 0)
        {
            const uint32_t i = block + (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            result &= spike_statement_event(kernels, &stack, i);
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
// The scalar parser on tokens [begin, end] in place, with the file-scope typedef names declared before begin visible.
static void spike_fallback_item(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t begin, uint32_t end, SpikeMeanings* meanings, Arena* scratch,
                                uint32_t* visible, SpikeFallback* result)
{
    while (*visible < kernels->typedef_count && kernels->typedef_tokens[*visible] < begin)
    {
        const uint32_t id = kernels->typedef_log[*visible];
        meanings->meanings[id] = kernels->typedef_names[id] == BUSTER_SPIKE_TYPEDEF_FILE ? SPIKE_MEANING_TYPEDEF : meanings->meanings[id];
        *visible += 1;
    }
    SpikeTree tree = { .roles = kernels->roles + begin, .depths = kernels->node_depths + begin, .parents = kernels->parents + begin };
    const SpikeParseResult parse = spike_parse_scalar(kinds + begin, names + begin, end + 1 - begin, meanings, &tree, scratch);
    if (parse.error_token != BUSTER_SPIKE_NONE && result->error_token == BUSTER_SPIKE_NONE)
    {
        result->error_token = begin + parse.error_token;
    }
    for (uint32_t j = begin; j <= end; j += 1)
    {
        kernels->parents[j] = kernels->parents[j] == BUSTER_SPIKE_ROOT ? BUSTER_SPIKE_ROOT : kernels->parents[j] + begin;
    }
    result->items += 1;
    result->tokens += end + 1 - begin;
}

// Parses every top-level declaration that has a flagged token with the scalar parser, in place: roles, depths and
// parents of its tokens. The scalar parser sees the file-scope typedef names declared before each one. With
// `everything`, the whole file is one declaration (unbalanced brackets, or a typedef the pass can't read). The
// declarations end where a terminator or a function body's closer is at node depth 0, found 64 tokens at a time.
static SpikeFallback spike_fallback(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count, bool everything, SpikeMeanings* meanings,
                                    Arena* scratch)
{
    SpikeFallback result = { .items = 0, .tokens = 0, .error_token = BUSTER_SPIKE_NONE };
    uint32_t visible = 0;
    if (everything && count != 0)
    {
        spike_fallback_item(kernels, kinds, names, 0, count - 1, meanings, scratch, &visible, &result);
    }
    else
    {
        uint32_t begin = 0;
        bool flagged = false;
        for (uint32_t block = 0; block < count; block += 64)
        {
            const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
            const __m512i flag_bytes = _mm512_maskz_loadu_epi8(live, kernels->flags + block);
            uint64_t flags = _mm512_test_epi8_mask(flag_bytes, flag_bytes);
            const __m512i role = _mm512_loadu_si512(kernels->roles + block);
            __mmask64 top = 0;
            for (uint32_t q = 0; q < 4; q += 1)
            {
                top |= (__mmask64)_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(kernels->node_depths + block + 16 * q), _mm512_setzero_si512()) << (16 * q);
            }
            uint64_t ends = top & live & (_mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(SPIKE_ROLE_TERMINATOR)) | _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(SPIKE_ROLE_CLOSE_BODY)));
            if (!flagged && flags == 0)
            {
                begin = ends != 0 ? block + 64 - (uint32_t)__builtin_clzll(ends) : begin;
            }
            else
            {
                while (ends != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctzll(ends);
                    ends &= ends - 1;
                    const uint64_t through = lane == 63 ? ~(uint64_t)0 : (((uint64_t)1 << (lane + 1)) - 1);
                    flagged |= (flags & through) != 0;
                    flags &= ~through;
                    if (flagged)
                    {
                        spike_fallback_item(kernels, kinds, names, begin, block + lane, meanings, scratch, &visible, &result);
                    }
                    begin = block + lane + 1;
                    flagged = false;
                }
                flagged |= flags != 0;
            }
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
static void spike_role_tables_init(void);
static void spike_statement_tables_init(void);

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
        .node_depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .enclosing = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .bracket_blocks = BUSTER_ARENA_PUSH_ARRAY(arena, SpikeBracketBlock, slots / 64 + 1),
        .roles = roles != 0 ? roles + BUSTER_SPIKE_TOKEN_PADDING : 0,
        .parents = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .flags = flags != 0 ? flags + BUSTER_SPIKE_TOKEN_PADDING : 0,
        .stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .stack_keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .waiting_thresholds = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .lefts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .left_key_values = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_names = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, name_capacity),
        .typedef_log = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_tokens = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .typedef_scopes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
    };
    const bool result = kernels->vkinds != 0 && kernels->depths != 0 && kernels->node_depths != 0 && kernels->enclosing != 0 && kernels->bracket_blocks != 0 &&
                        kernels->roles != 0 &&
                        kernels->parents != 0 && kernels->flags != 0 && kernels->stack != 0 && kernels->stack_keys != 0 && kernels->waiting != 0 &&
                        kernels->waiting_thresholds != 0 && kernels->lefts != 0 && kernels->left_key_values != 0 && kernels->typedef_names != 0 && kernels->typedef_log != 0 &&
                        kernels->typedef_tokens != 0 && kernels->typedef_scopes != 0;
    if (result)
    {
        memset(kernels->typedef_names, 0, name_capacity);
        memset(vkinds, SPIKE_KIND_END, slots);
        memset(roles, SPIKE_ROLE_NONE, slots);
        memset(flags, 0, slots);
    }
    spike_role_info_init();
    spike_kernels_init_tables();
    spike_shifts_init();
    spike_parent_tables_init();
    spike_role_tables_init();
    spike_statement_tables_init();
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
    // Shift up by 2 taking lanes 0 and 1 of the second source below that.
    __m512i up2_with_fill;
    // For K4 with the stack's top in registers: byte lanes reversed (lane i gets 63 - i), and every lane 63; with
    // _mm512_maskz_permutexvar_epi8 under BUSTER_SPIKE_WORD_BYTES, half h of a byte vector as 16-bit lanes in order
    // (words) and in the order _mm512_unpacklo_epi16 and _mm512_unpackhi_epi16 put back in order as 32-bit lanes
    // (unpacked: word 8l + k gets byte 32h + 4l + k, word 8l + 4 + k byte 32h + 16 + 4l + k); and under
    // BUSTER_SPIKE_DWORD_BYTES, quarter q as 32-bit lanes.
    __m512i reverse8;
    __m512i last8;
    __m512i words[2];
    __m512i unpacked[2];
    __m512i dwords[4];
};

#define BUSTER_SPIKE_WORD_BYTES 0x5555555555555555ull
#define BUSTER_SPIKE_DWORD_BYTES 0x1111111111111111ull

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
    uint8_t up2[64];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        up2[i] = (uint8_t)(i >= 2 ? i - 2 : 64 + i);
    }
    spike_shifts.up2_with_fill = _mm512_loadu_si512(up2);
    uint8_t reverse8[64];
    uint8_t last8[64];
    for (uint32_t i = 0; i < 64; i += 1)
    {
        reverse8[i] = (uint8_t)(63 - i);
        last8[i] = 63;
    }
    spike_shifts.reverse8 = _mm512_loadu_si512(reverse8);
    spike_shifts.last8 = _mm512_loadu_si512(last8);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        uint8_t words[64];
        uint8_t unpacked[64];
        for (uint32_t w = 0; w < 32; w += 1)
        {
            const uint32_t l = w / 8;
            const uint32_t k = w % 8;
            words[2 * w] = (uint8_t)(32 * h + w);
            words[2 * w + 1] = 0;
            unpacked[2 * w] = (uint8_t)(k < 4 ? 32 * h + 4 * l + k : 32 * h + 16 + 4 * l + k - 4);
            unpacked[2 * w + 1] = 0;
        }
        spike_shifts.words[h] = _mm512_loadu_si512(words);
        spike_shifts.unpacked[h] = _mm512_loadu_si512(unpacked);
    }
    for (uint32_t q = 0; q < 4; q += 1)
    {
        uint8_t dwords[64];
        for (uint32_t d = 0; d < 16; d += 1)
        {
            dwords[4 * d] = (uint8_t)(16 * q + d);
            dwords[4 * d + 1] = 0;
            dwords[4 * d + 2] = 0;
            dwords[4 * d + 3] = 0;
        }
        spike_shifts.dwords[q] = _mm512_loadu_si512(dwords);
    }
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

// One block of K1's twin, for a block whose enclosing openers reach further back than the vector path's 16 depths. Out
// of line, so that this rare path leaves the block loop's code alone.
__attribute__((noinline)) static void spike_brackets_block_scalar(const uint8_t* codes, const int8_t* deltas, uint32_t begin, uint32_t end, uint32_t* depths, uint32_t* enclosing,
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
// vpermd away. `open_stack` needs an entry per depth the input reaches, and 16 of BUSTER_SPIKE_NONE below depth 0. Each
// block's view goes to `blocks` (SpikeBracketBlock), for K2. Returns false if a closer has no opener or a group is still
// open at the end.
static bool spike_brackets_avx512(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* open_stack,
                                  SpikeBracketBlock* blocks)
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
        // The block's view for K2, which needs nothing from before the block: a lane whose opener isn't in the block has
        // it before the block, and the openers still open at its end are those that no token from them on in the block
        // leaves at a lower depth than theirs.
        __m512i suffix = after;
        for (uint32_t k = 0; k < 6; k += 1)
        {
            suffix = _mm512_min_epi8(suffix, spike_down8(suffix, k, fill));
        }
        const __mmask64 openers = _mm512_cmpgt_epi8_mask(delta, _mm512_setzero_si512()) & live_mask;
        uint64_t open = openers & _mm512_cmpeq_epi8_mask(suffix, after);
        SpikeBracketBlock* view = &blocks[block / 64];
        _mm512_store_si512(view->opener_lanes, _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), found & live_mask, p));
        _mm512_store_si512(view->relative_depths, _mm512_maskz_mov_epi8(live_mask, before));
        view->open = open;
        // Tokens whose opener is before the block are at or below its starting depth; at most 15 depths below it the
        // vector path handles them.
        const __mmask64 deep = _mm512_mask_cmplt_epi8_mask(~found & live_mask, before, _mm512_set1_epi8(-15));
        if (deep != 0)
        {
            spike_brackets_block_scalar(codes, deltas, block, block + live, depths, enclosing, open_stack, &base);
        }
        else
        {
            // The open openers at the 16 depths below the block's start (open_stack has BUSTER_SPIKE_DEPTH_PADDING entries
            // of no opener below depth 0).
            const __m512i below_vector = _mm512_loadu_si512(open_stack + base - 16);
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
            // The openers still open at the block's end, onto open_stack.
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
    // Both by level: bit 0 weak, bit 1 ties to the left.
    uint16_t weak_ties[32];
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
        spike_parent_tables.weak_ties[level] = (uint16_t)(spike_level_is_weak((uint8_t)level) | (spike_level_ties_left[level] << 1));
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

BUSTER_INLINE SpikeKeys spike_block_keys(const uint8_t* roles, const uint32_t* node_depths, uint32_t block)
{
    const __m512i role = _mm512_zextsi256_si512(_mm256_loadu_si256((const __m256i*)(roles + block)));
    const __m512i node = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi32_epi16(_mm512_loadu_si512(node_depths + block))),
                                            _mm512_cvtepi32_epi16(_mm512_loadu_si512(node_depths + block + 16)), 1);
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
// tokens, and BUSTER_SPIKE_CANDIDATE_PADDING entries below index 0 that hold key 0 and BUSTER_SPIKE_NONE.
#define BUSTER_SPIKE_CANDIDATE_PADDING 32

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
    const uint32_t count = stack->count;
    const uint32_t start = count >= 32 ? count - 32 : 0;
    const uint32_t missing = 32 - (count - start);
    // The stack has BUSTER_SPIKE_CANDIDATE_PADDING entries below it, key 0 and no position: the window's lanes below
    // the stack, which a search finds only when nothing above does, and which then read as no candidate.
    SpikeWindow result;
    result.keys = _mm512_loadu_si512(stack->keys + (int64_t)count - 32);
    result.positions_low = _mm512_loadu_si512(stack->positions + (int64_t)count - 32);
    result.positions_high = _mm512_loadu_si512(stack->positions + (int64_t)count - 16);
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

// Pops the candidates a block hides, those whose key isn't below the block's minimum: a run at the stack's top, since keys
// increase upward, counted 32 at a time. Every real key is at least 1 (no role has level 0), so the padding's 0 stays.
BUSTER_INLINE void spike_pop_hidden(SpikeCandidates* stack, uint16_t minimum)
{
    bool popping = true;
    while (popping)
    {
        const __m512i top = _mm512_loadu_si512(stack->keys + (int64_t)stack->count - 32);
        const uint32_t hidden = (uint32_t)__builtin_popcount(_mm512_cmpge_epu16_mask(top, _mm512_set1_epi16((short)minimum)));
        stack->count -= hidden;
        popping = hidden == 32;
    }
}

// Pushes n positions reversed (the last first): the right passes' prefix minima, nearest on top. n is at most 32 here.
BUSTER_INLINE void spike_push_reversed(SpikeCandidates* stack, const uint32_t* positions, uint32_t n)
{
    const __m512i low = _mm512_loadu_si512(positions);
    const __m512i high = _mm512_loadu_si512(positions + 16);
    const __m512i lanes = _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16));
    const __m512i first = _mm512_sub_epi32(_mm512_set1_epi32((int)n - 1), lanes);
    const __m512i second = _mm512_sub_epi32(first, _mm512_set1_epi32(16));
    _mm512_storeu_si512(stack->positions + stack->count, _mm512_permutex2var_epi32(low, first, high));
    _mm512_storeu_si512(stack->positions + stack->count + 16, _mm512_permutex2var_epi32(low, second, high));
}

// One block of K4's left pass in AVX-512, 32 tokens in 16-bit lanes: every token's L and L's key. Within the block, spike_previous_smaller16;
// before it, the stack of candidates. Then the block's keys replace the candidates they hide, and its suffix minima go
// on top. Node depths must be at most BUSTER_SPIKE_MAX_VECTOR_DEPTH.
BUSTER_INLINE void spike_parents_left16_block(const uint8_t* roles, const uint32_t* node_depths, uint32_t block, uint32_t live, uint32_t* lefts, uint16_t* left_keys,
                                               SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi16((short)0xFFFF);
    const __mmask32 live_mask = live == 32 ? ~(__mmask32)0 : (((__mmask32)1 << live) - 1);
    const SpikeKeys keys = spike_block_keys(roles, node_depths, block);
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
    spike_pop_hidden(stack, minimum);
    const __mmask32 minima = _mm512_mask_cmplt_epu16_mask(live_mask, block_keys, spike_down16(suffix, 0, ones));
    _mm512_storeu_si512(stack->keys + stack->count, _mm512_maskz_compress_epi16(minima, block_keys));
    const __m512i lanes_low = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16)));
    const __m512i lanes_high = _mm512_add_epi32(lanes_low, _mm512_set1_epi32(16));
    const uint32_t low_count = (uint32_t)__builtin_popcount(minima & 0xFFFF);
    _mm512_storeu_si512(stack->positions + stack->count, _mm512_maskz_compress_epi32((__mmask16)minima, lanes_low));
    _mm512_storeu_si512(stack->positions + stack->count + low_count, _mm512_maskz_compress_epi32((__mmask16)(minima >> 16), lanes_high));
    stack->count += (uint32_t)__builtin_popcount(minima);
}

// K4's left pass in 16-bit lanes.
static void spike_parents_left_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, SpikeCandidates* stack)
{
    stack->count = 0;
    for (uint32_t block = 0; block < count; block += 32)
    {
        spike_parents_left16_block(roles, node_depths, block, count - block < 32 ? count - block : 32, lefts, left_keys, stack);
    }
}

// One block of K4's right pass in AVX-512, 32 tokens in 16-bit lanes: every token's R, as the left pass found L, then
// its parent by its role's rule.
BUSTER_INLINE void spike_parents_right16_block(const uint8_t* roles, const uint32_t* node_depths, uint32_t block, uint32_t live, const uint32_t* lefts,
                                                const uint16_t* left_keys, uint32_t* parents, SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi16((short)0xFFFF);
    const __m512i weak_table = _mm512_loadu_si512(spike_parent_tables.weak);
    const __m512i ties_table = _mm512_loadu_si512(spike_parent_tables.ties);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
    const __mmask32 live_mask = live == 32 ? ~(__mmask32)0 : (((__mmask32)1 << live) - 1);
    const SpikeKeys keys = spike_block_keys(roles, node_depths, block);
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
    spike_pop_hidden(stack, minimum);
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
    if (n <= 32)
    {
        spike_push_reversed(stack, positions, n);
    }
    else
    {
        for (uint32_t i = 0; i < n; i += 1)
        {
            stack->positions[stack->count + i] = positions[n - 1 - i];
        }
    }
    stack->count += n;
}

// K4's right pass in 16-bit lanes, from the last block to the first.
static void spike_parents_right_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents,
                                       SpikeCandidates* stack)
{
    stack->count = 0;
    for (uint32_t b = (count + 31) / 32; b-- > 0;)
    {
        const uint32_t block = b * 32;
        spike_parents_right16_block(roles, node_depths, block, count - block < 32 ? count - block : 32, lefts, left_keys, parents, stack);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K2 in AVX-512
//
// The rules of spike_role_rule, for 64 tokens at once in byte lanes. A token's role depends on the token before it
// (through its pred class), on its group's role and on the typed state of the specifiers before it, so the block is
// evaluated in rounds: each round computes every lane's role from the previous round's, until nothing changes. Every
// dependency points backward, so the fixpoint is the sequential twin's result, and a round fixes at least one more lane:
// a block that hasn't converged after BUSTER_SPIKE_ROLE_ROUNDS rounds is done by the twin's loop. What crosses blocks:
// the roles and kinds of the two tokens before the block, its typed state, and by depth the role of each open group's
// opener (with the typed state before an attribute's keyword).
//
// The rounds start from each kind's commonest role, refined where that is often wrong by the rules a round applies to
// `(`, the unary operators, tags, `{` and the closers, fed with those guesses. A round's roles are final as soon as no
// lane reads anything that changed in them (its features, spike_features8): no further round needs to confirm them. On
// sqlite3 and the libc corpus nearly every block converges in one round.

#define BUSTER_SPIKE_ROLE_ROUNDS 8

typedef enum SpikeRuleClass
{
    SPIKE_RULE_CLASS_FIXED,
    SPIKE_RULE_CLASS_STRING,
    SPIKE_RULE_CLASS_IDENTIFIER,
    SPIKE_RULE_CLASS_TYPE_NAME,
    SPIKE_RULE_CLASS_QUALIFIER,
    SPIKE_RULE_CLASS_STORAGE,
    SPIKE_RULE_CLASS_GOTO,
    SPIKE_RULE_CLASS_ATOMIC,
    SPIKE_RULE_CLASS_EXTENSION,
    SPIKE_RULE_CLASS_COMMA,
    SPIKE_RULE_CLASS_COLON,
    SPIKE_RULE_CLASS_ELLIPSIS,
    SPIKE_RULE_CLASS_UNARY,
    SPIKE_RULE_CLASS_PAREN,
    SPIKE_RULE_CLASS_BRACE,
    SPIKE_RULE_CLASS_CLOSER,
} SpikeRuleClass;

// What `(` is after a kind: the opener's role, or one of these to be decided by what came before the keyword.
#define BUSTER_SPIKE_PAREN_ATTRIBUTE 0xFE
#define BUSTER_SPIKE_PAREN_ASM 0xFD

// Bits of SpikeRoleTables.vkind_bits.
#define BUSTER_SPIKE_VKIND_STARTS_TYPE 0x01
#define BUSTER_SPIKE_VKIND_STARTS_DECLARATION 0x02
#define BUSTER_SPIKE_VKIND_TYPE_WORD 0x04
#define BUSTER_SPIKE_VKIND_STARTS_OPERAND 0x08
#define BUSTER_SPIKE_VKIND_NAME 0x10
#define BUSTER_SPIKE_VKIND_KEYWORD 0x20
#define BUSTER_SPIKE_VKIND_ASM_QUALIFIER 0x40

// Bits of SpikeRoleTables.group_bits, by a group's role.
#define BUSTER_SPIKE_GROUP_BLOCK 0x01
#define BUSTER_SPIKE_GROUP_INITIALIZER 0x02
#define BUSTER_SPIKE_GROUP_ATTRIBUTE_LIST 0x04
#define BUSTER_SPIKE_GROUP_CAST 0x08

// Bits of SpikeRoleTables.role_bits, by role.
#define BUSTER_SPIKE_ROLE_PROPAGATES 0x01
#define BUSTER_SPIKE_ROLE_TYPE_IF_WORD 0x02
#define BUSTER_SPIKE_ROLE_TYPE 0x04
#define BUSTER_SPIKE_ROLE_COPIES_TYPED 0x08
#define BUSTER_SPIKE_ROLE_HEAD_CLOSER 0x10
#define BUSTER_SPIKE_ROLE_CALL_CLOSER 0x20

// The roles whose identity, beyond their pred class, a round reads from the token before (or from a group's opener),
// in the high bits of SpikeRoleTables.features.
#define BUSTER_SPIKE_FEATURE_ATTRIBUTE_RIGHT 0x10
#define BUSTER_SPIKE_FEATURE_LEAF_RIGHT 0x20
#define BUSTER_SPIKE_FEATURE_HEAD_CLOSER 0x30
#define BUSTER_SPIKE_FEATURE_CALL_CLOSER 0x40
#define BUSTER_SPIKE_FEATURE_COMPOUND_CLOSER 0x50
#define BUSTER_SPIKE_FEATURE_BLOCK 0x60
#define BUSTER_SPIKE_FEATURE_INITIALIZER 0x70

typedef struct SpikeRoleTables SpikeRoleTables;
struct SpikeRoleTables
{
    // By virtual kind. guesses: the first round's guess of each lane's role, the most frequent on sqlite3.c and the libc
    // corpus; it changes how many rounds a block takes, never their result.
    uint8_t guesses[128];
    uint8_t fixed[128];
    uint8_t fixed_flags[128];
    uint8_t classes[128];
    uint8_t unary[128];
    uint8_t binary[128];
    uint8_t paren_previous[128];
    uint8_t cast_closers[128];
    uint8_t vkind_bits[128];
    // By role.
    uint8_t preds[128];
    uint8_t comma[128];
    uint8_t colon[128];
    uint8_t ellipsis[128];
    uint8_t closers[128];
    uint8_t group_bits[128];
    uint8_t role_bits[128];
    // What a round reads of a token's role: its pred class (before the fixes by kind) and, as BUSTER_SPIKE_FEATURE_*
    // in the high bits, whether it is one of the roles the rules name.
    uint8_t features[128];
    // By pred class.
    uint8_t attribute_by_before[64];
    uint8_t pred_bits[16];
};

static SpikeRoleTables spike_role_tables;

static void spike_role_tables_init(void)
{
    SpikeRoleTables* t = &spike_role_tables;
    memset(t, 0, sizeof(*t));
    for (uint32_t k = 0; k < 128; k += 1)
    {
        t->fixed[k] = SPIKE_ROLE_ERROR;
        t->fixed_flags[k] = 1;
        t->classes[k] = SPIKE_RULE_CLASS_FIXED;
        const uint8_t binary = k < BUSTER_SPIKE_VKINDS ? spike_binary_role((uint8_t)k) : SPIKE_ROLE_NONE;
        if (binary != SPIKE_ROLE_NONE)
        {
            t->fixed[k] = binary;
            t->fixed_flags[k] = 0;
        }
        const uint8_t vk = (uint8_t)k;
        t->vkind_bits[k] = (uint8_t)((k < BUSTER_SPIKE_VKINDS && spike_vkind_starts_type(vk) ? BUSTER_SPIKE_VKIND_STARTS_TYPE : 0) |
                                     (k < BUSTER_SPIKE_VKINDS && spike_vkind_starts_declaration(vk) ? BUSTER_SPIKE_VKIND_STARTS_DECLARATION : 0) |
                                     (k == SPIKE_KIND_TYPE || k == SPIKE_VKIND_TYPE_NAME ? BUSTER_SPIKE_VKIND_TYPE_WORD : 0) |
                                     (k < BUSTER_SPIKE_VKINDS && spike_vkind_starts_operand(vk) ? BUSTER_SPIKE_VKIND_STARTS_OPERAND : 0) |
                                     (k == SPIKE_KIND_IDENTIFIER || k == SPIKE_VKIND_TYPE_NAME ? BUSTER_SPIKE_VKIND_NAME : 0) |
                                     ((k >= SPIKE_KIND_TYPE && k < BUSTER_SPIKE_VKINDS) || k == SPIKE_KIND_IDENTIFIER ? BUSTER_SPIKE_VKIND_KEYWORD : 0) |
                                     (k == SPIKE_KIND_QUALIFIER || k == SPIKE_KIND_STORAGE || k == SPIKE_KIND_GOTO ? BUSTER_SPIKE_VKIND_ASM_QUALIFIER : 0));
        t->cast_closers[k] = k == SPIKE_KIND_L_BRACE ? SPIKE_ROLE_CLOSE_COMPOUND : k < BUSTER_SPIKE_VKINDS && spike_vkind_starts_operand(vk) ? SPIKE_ROLE_CLOSE_CAST :
                                                                                                                                   SPIKE_ROLE_CLOSE_FUNCTION;
    }
    static const uint8_t fixed_kinds[][2] = {
        { SPIKE_KIND_NUMBER, SPIKE_ROLE_OPERAND },
        { SPIKE_KIND_CHARACTER, SPIKE_ROLE_OPERAND },
        { SPIKE_KIND_TYPE, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_TYPEDEF, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_LABEL, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_STRUCT, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_ENUM, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_TYPEOF, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_ALIGNAS, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_ATTRIBUTE, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_ASM, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_TYPE_ARGUMENT, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_STATIC_ASSERT, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_IF, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_WHILE, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_SWITCH, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_FOR, SPIKE_ROLE_LEAF_RIGHT },
        { SPIKE_KIND_SIZEOF, SPIKE_ROLE_PREFIX },
        { SPIKE_KIND_REAL_IMAG, SPIKE_ROLE_PREFIX },
        { SPIKE_KIND_EXCLAIM, SPIKE_ROLE_PREFIX },
        { SPIKE_KIND_TILDE, SPIKE_ROLE_PREFIX },
        { SPIKE_KIND_ELSE, SPIKE_ROLE_ELSE },
        { SPIKE_KIND_DO, SPIKE_ROLE_DO },
        { SPIKE_KIND_CASE, SPIKE_ROLE_OPEN_CASE },
        { SPIKE_KIND_DEFAULT, SPIKE_ROLE_OPEN_CASE },
        { SPIKE_KIND_RETURN, SPIKE_ROLE_RETURN },
        { SPIKE_KIND_BREAK, SPIKE_ROLE_RETURN },
        { SPIKE_KIND_CONTINUE, SPIKE_ROLE_RETURN },
        { SPIKE_KIND_SEMI, SPIKE_ROLE_TERMINATOR },
        { SPIKE_KIND_QUESTION, SPIKE_ROLE_OPEN_TERNARY },
        { SPIKE_KIND_PERIOD, SPIKE_ROLE_MEMBER_OPERATOR },
        { SPIKE_KIND_ARROW, SPIKE_ROLE_MEMBER_OPERATOR },
        { SPIKE_KIND_L_SQUARE, SPIKE_ROLE_OPEN_SUBSCRIPT },
    };
    for (uint32_t i = 0; i < sizeof(fixed_kinds) / sizeof(fixed_kinds[0]); i += 1)
    {
        t->fixed[fixed_kinds[i][0]] = fixed_kinds[i][1];
        t->fixed_flags[fixed_kinds[i][0]] = 0;
    }
    for (uint32_t k = 0; k < 128; k += 1)
    {
        t->guesses[k] = t->fixed[k];
    }
    static const uint8_t guesses[][2] = {
        { SPIKE_KIND_IDENTIFIER, SPIKE_ROLE_OPERAND },
        { SPIKE_KIND_STRING, SPIKE_ROLE_OPERAND },
        { SPIKE_KIND_R_SQUARE, SPIKE_ROLE_CLOSE_SUBSCRIPT },
        { SPIKE_KIND_L_PAREN, SPIKE_ROLE_OPEN_CALL },
        { SPIKE_KIND_R_PAREN, SPIKE_ROLE_CLOSE_CALL },
        { SPIKE_KIND_L_BRACE, SPIKE_ROLE_OPEN_BLOCK },
        { SPIKE_KIND_R_BRACE, SPIKE_ROLE_CLOSE_BLOCK },
        { SPIKE_KIND_ELLIPSIS, SPIKE_ROLE_OPERAND },
        { SPIKE_KIND_AMP, SPIKE_ROLE_BITWISE_AND },
        { SPIKE_KIND_AMPAMP, SPIKE_ROLE_LOGICAL_AND },
        { SPIKE_KIND_STAR, SPIKE_ROLE_PREFIX },
        { SPIKE_KIND_PLUS, SPIKE_ROLE_ADDITIVE },
        { SPIKE_KIND_PLUSPLUS, SPIKE_ROLE_POSTFIX },
        { SPIKE_KIND_MINUS, SPIKE_ROLE_ADDITIVE },
        { SPIKE_KIND_MINUSMINUS, SPIKE_ROLE_POSTFIX },
        { SPIKE_KIND_COLON, SPIKE_ROLE_CLOSE_TERNARY },
        { SPIKE_KIND_COMMA, SPIKE_ROLE_SEPARATOR },
        { SPIKE_KIND_QUALIFIER, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_CALLING_CONVENTION, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_STORAGE, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_GOTO, SPIKE_ROLE_RETURN },
        { SPIKE_KIND_ATOMIC, SPIKE_ROLE_SPECIFIER },
        { SPIKE_KIND_EXTENSION, SPIKE_ROLE_SPECIFIER },
        { SPIKE_VKIND_TYPE_NAME, SPIKE_ROLE_SPECIFIER },
    };
    for (uint32_t i = 0; i < sizeof(guesses) / sizeof(guesses[0]); i += 1)
    {
        t->guesses[guesses[i][0]] = guesses[i][1];
    }
    // _Generic is the scalar parser's.
    t->fixed[SPIKE_KIND_GENERIC] = SPIKE_ROLE_LEAF_RIGHT;
    t->fixed_flags[SPIKE_KIND_GENERIC] = 1;
    static const uint8_t class_kinds[][2] = {
        { SPIKE_KIND_STRING, SPIKE_RULE_CLASS_STRING },
        { SPIKE_KIND_IDENTIFIER, SPIKE_RULE_CLASS_IDENTIFIER },
        { SPIKE_VKIND_TYPE_NAME, SPIKE_RULE_CLASS_TYPE_NAME },
        { SPIKE_KIND_QUALIFIER, SPIKE_RULE_CLASS_QUALIFIER },
        { SPIKE_KIND_CALLING_CONVENTION, SPIKE_RULE_CLASS_QUALIFIER },
        { SPIKE_KIND_STORAGE, SPIKE_RULE_CLASS_STORAGE },
        { SPIKE_KIND_GOTO, SPIKE_RULE_CLASS_GOTO },
        { SPIKE_KIND_ATOMIC, SPIKE_RULE_CLASS_ATOMIC },
        { SPIKE_KIND_EXTENSION, SPIKE_RULE_CLASS_EXTENSION },
        { SPIKE_KIND_COMMA, SPIKE_RULE_CLASS_COMMA },
        { SPIKE_KIND_COLON, SPIKE_RULE_CLASS_COLON },
        { SPIKE_KIND_ELLIPSIS, SPIKE_RULE_CLASS_ELLIPSIS },
        { SPIKE_KIND_STAR, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_AMP, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_PLUS, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_MINUS, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_AMPAMP, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_PLUSPLUS, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_MINUSMINUS, SPIKE_RULE_CLASS_UNARY },
        { SPIKE_KIND_L_PAREN, SPIKE_RULE_CLASS_PAREN },
        { SPIKE_KIND_L_BRACE, SPIKE_RULE_CLASS_BRACE },
        { SPIKE_KIND_R_PAREN, SPIKE_RULE_CLASS_CLOSER },
        { SPIKE_KIND_R_SQUARE, SPIKE_RULE_CLASS_CLOSER },
        { SPIKE_KIND_R_BRACE, SPIKE_RULE_CLASS_CLOSER },
    };
    for (uint32_t i = 0; i < sizeof(class_kinds) / sizeof(class_kinds[0]); i += 1)
    {
        t->classes[class_kinds[i][0]] = class_kinds[i][1];
        t->fixed_flags[class_kinds[i][0]] = 0;
    }
    t->unary[SPIKE_KIND_STAR] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_AMP] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_PLUS] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_MINUS] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_AMPAMP] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_PLUSPLUS] = SPIKE_ROLE_PREFIX;
    t->unary[SPIKE_KIND_MINUSMINUS] = SPIKE_ROLE_PREFIX;
    t->binary[SPIKE_KIND_STAR] = SPIKE_ROLE_MULTIPLICATIVE;
    t->binary[SPIKE_KIND_AMP] = SPIKE_ROLE_BITWISE_AND;
    t->binary[SPIKE_KIND_PLUS] = SPIKE_ROLE_ADDITIVE;
    t->binary[SPIKE_KIND_MINUS] = SPIKE_ROLE_ADDITIVE;
    t->binary[SPIKE_KIND_AMPAMP] = SPIKE_ROLE_LOGICAL_AND;
    t->binary[SPIKE_KIND_PLUSPLUS] = SPIKE_ROLE_POSTFIX;
    t->binary[SPIKE_KIND_MINUSMINUS] = SPIKE_ROLE_POSTFIX;
    t->paren_previous[SPIKE_KIND_IF] = SPIKE_ROLE_OPEN_CONDITION;
    t->paren_previous[SPIKE_KIND_WHILE] = SPIKE_ROLE_OPEN_CONDITION;
    t->paren_previous[SPIKE_KIND_SWITCH] = SPIKE_ROLE_OPEN_CONDITION;
    t->paren_previous[SPIKE_KIND_FOR] = SPIKE_ROLE_OPEN_FOR;
    t->paren_previous[SPIKE_KIND_SIZEOF] = SPIKE_ROLE_OPEN_SIZEOF;
    t->paren_previous[SPIKE_KIND_TYPEOF] = SPIKE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[SPIKE_KIND_ALIGNAS] = SPIKE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[SPIKE_KIND_ATOMIC] = SPIKE_ROLE_OPEN_SPECIFIER;
    t->paren_previous[SPIKE_KIND_ATTRIBUTE] = BUSTER_SPIKE_PAREN_ATTRIBUTE;
    t->paren_previous[SPIKE_KIND_ASM] = BUSTER_SPIKE_PAREN_ASM;
    t->paren_previous[SPIKE_KIND_TYPE_ARGUMENT] = SPIKE_ROLE_OPEN_KEYWORD;
    t->paren_previous[SPIKE_KIND_GENERIC] = SPIKE_ROLE_OPEN_KEYWORD;
    t->paren_previous[SPIKE_KIND_STATIC_ASSERT] = SPIKE_ROLE_OPEN_KEYWORD;
    for (uint32_t r = 0; r < 128; r += 1)
    {
        const uint8_t role = r < SPIKE_ROLE_COUNT ? (uint8_t)r : SPIKE_ROLE_NONE;
        t->preds[r] = spike_role_preds[role];
        const bool comma = role == SPIKE_ROLE_NONE || role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY || role == SPIKE_ROLE_OPEN_STRUCT ||
                           role == SPIKE_ROLE_OPEN_FOR;
        t->comma[r] = comma ? SPIKE_ROLE_COMMA : SPIKE_ROLE_SEPARATOR;
        t->colon[r] = role == SPIKE_ROLE_OPEN_ASM ? SPIKE_ROLE_ASM_COLON : role == SPIKE_ROLE_OPEN_STRUCT ? SPIKE_ROLE_BITFIELD : SPIKE_ROLE_CLOSE_TERNARY;
        t->ellipsis[r] = role == SPIKE_ROLE_OPEN_CALL || role == SPIKE_ROLE_OPEN_FUNCTION || role == SPIKE_ROLE_OPEN_CAST ? SPIKE_ROLE_OPERAND : SPIKE_ROLE_RANGE;
        t->closers[r] = spike_closer_roles[role] != SPIKE_ROLE_NONE ? spike_closer_roles[role] : SPIKE_ROLE_ERROR;
        t->group_bits[r] = (uint8_t)((role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY ? BUSTER_SPIKE_GROUP_BLOCK : 0) |
                                     (role == SPIKE_ROLE_OPEN_INITIALIZER || role == SPIKE_ROLE_OPEN_COMPOUND_BODY ? BUSTER_SPIKE_GROUP_INITIALIZER : 0) |
                                     (role == SPIKE_ROLE_OPEN_ATTRIBUTE_LIST ? BUSTER_SPIKE_GROUP_ATTRIBUTE_LIST : 0) |
                                     (role == SPIKE_ROLE_OPEN_CAST ? BUSTER_SPIKE_GROUP_CAST : 0));
        t->role_bits[r] = (uint8_t)((spike_role_propagates_typed(role) ? BUSTER_SPIKE_ROLE_PROPAGATES : 0) |
                                    (role == SPIKE_ROLE_SPECIFIER ? BUSTER_SPIKE_ROLE_TYPE_IF_WORD : 0) |
                                    (role == SPIKE_ROLE_TAG || role == SPIKE_ROLE_CLOSE_STRUCT || role == SPIKE_ROLE_CLOSE_ENUM || role == SPIKE_ROLE_CLOSE_SPECIFIER ?
                                     BUSTER_SPIKE_ROLE_TYPE : 0) |
                                    (role == SPIKE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? BUSTER_SPIKE_ROLE_COPIES_TYPED : 0) |
                                    (role == SPIKE_ROLE_CLOSE_CONDITION || role == SPIKE_ROLE_CLOSE_FOR ? BUSTER_SPIKE_ROLE_HEAD_CLOSER : 0) |
                                    (role == SPIKE_ROLE_CLOSE_CALL || role == SPIKE_ROLE_CLOSE_FUNCTION ? BUSTER_SPIKE_ROLE_CALL_CLOSER : 0));
    }
    for (uint32_t r = 0; r < 128; r += 1)
    {
        const uint8_t role = r < SPIKE_ROLE_COUNT ? (uint8_t)r : SPIKE_ROLE_NONE;
        const uint8_t named = role == SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT ? BUSTER_SPIKE_FEATURE_ATTRIBUTE_RIGHT :
                              role == SPIKE_ROLE_LEAF_RIGHT ? BUSTER_SPIKE_FEATURE_LEAF_RIGHT :
                              role == SPIKE_ROLE_CLOSE_CONDITION || role == SPIKE_ROLE_CLOSE_FOR ? BUSTER_SPIKE_FEATURE_HEAD_CLOSER :
                              role == SPIKE_ROLE_CLOSE_CALL || role == SPIKE_ROLE_CLOSE_FUNCTION ? BUSTER_SPIKE_FEATURE_CALL_CLOSER :
                              role == SPIKE_ROLE_CLOSE_COMPOUND ? BUSTER_SPIKE_FEATURE_COMPOUND_CLOSER :
                              role == SPIKE_ROLE_OPEN_BLOCK || role == SPIKE_ROLE_OPEN_BODY ? BUSTER_SPIKE_FEATURE_BLOCK :
                              role == SPIKE_ROLE_OPEN_INITIALIZER || role == SPIKE_ROLE_OPEN_COMPOUND_BODY ? BUSTER_SPIKE_FEATURE_INITIALIZER : 0;
        t->features[r] = (uint8_t)(spike_role_preds[role] | named);
    }
    for (uint32_t p = 0; p < 64; p += 1)
    {
        t->attribute_by_before[p] = p == SPIKE_PRED_OPERAND ? SPIKE_ROLE_OPEN_ATTRIBUTE : p == SPIKE_PRED_STAR ? SPIKE_ROLE_OPEN_ATTRIBUTE_LEFT :
                                    p == SPIKE_PRED_LEAF ? SPIKE_ROLE_OPEN_ATTRIBUTE_RIGHT : SPIKE_ROLE_OPEN_ATTRIBUTE_SPECIFIER;
    }
    for (uint32_t p = 0; p < 16; p += 1)
    {
        t->pred_bits[p] = (uint8_t)(p < 8 ? 1u << p : 0);
    }
}

// The pred classes as one-hot bits; SPIKE_PRED_LEAF is no bit.
#define BUSTER_SPIKE_PRED_BIT(pred) ((uint8_t)(1u << (pred)))

// K2's stacks of an entry per depth, with BUSTER_SPIKE_DEPTH_PADDING entries below depth 0: the role of the open opener
// at each depth, the typed state just before its keyword, and the kind of the token before it.
typedef struct SpikeGroupStacks SpikeGroupStacks;
struct SpikeGroupStacks
{
    uint8_t* roles;
    uint8_t* typed;
    uint8_t* keywords;
};

typedef struct SpikeRoleCarry SpikeRoleCarry;
struct SpikeRoleCarry
{
    // The roles of the two tokens before the block (SPIKE_ROLE_NONE before the start), and the typed state of each of
    // the 64 tokens before it.
    uint8_t previous_role;
    uint8_t before_role;
    uint64_t typed;
    // By depth: the role of the open opener at that depth, the typed state just before its keyword, and the kind of the
    // token before it.
    uint8_t* group_roles;
    uint8_t* group_typed;
    uint8_t* group_keywords;
};

BUSTER_INLINE __m512i spike_table8(const uint8_t* table, __m512i index)
{
    return _mm512_permutex2var_epi8(_mm512_loadu_si512(table), index, _mm512_loadu_si512(table + 64));
}

BUSTER_INLINE __mmask64 spike_equal8(__m512i x, uint8_t value)
{
    return _mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8((char)value));
}

// What a round reads of each lane's role: its features, with the pred class fixed by kind as spike_preds8 fixes it.
// Two role vectors with the same features (and typed states) make a round compute the same roles.
BUSTER_INLINE __m512i spike_features8(__m512i role, __mmask64 word, __mmask64 star)
{
    const SpikeRoleTables* t = &spike_role_tables;
    __m512i features = spike_table8(t->features, role);
    features = _mm512_mask_mov_epi8(features, spike_equal8(role, SPIKE_ROLE_SPECIFIER) & word, _mm512_set1_epi8(SPIKE_PRED_TYPE));
    features = _mm512_mask_mov_epi8(features, spike_equal8(role, SPIKE_ROLE_PREFIX) & star, _mm512_set1_epi8(SPIKE_PRED_STAR));
    return features;
}

// The pred class of each lane's previous token (index and one-hot bits), from its role and kind.
BUSTER_INLINE __m512i spike_preds8(__m512i role, __m512i vkind)
{
    const SpikeRoleTables* t = &spike_role_tables;
    __m512i pred = spike_table8(t->preds, role);
    const __mmask64 word = _mm512_test_epi8_mask(spike_table8(t->vkind_bits, vkind), _mm512_set1_epi8(BUSTER_SPIKE_VKIND_TYPE_WORD));
    pred = _mm512_mask_mov_epi8(pred, spike_equal8(role, SPIKE_ROLE_SPECIFIER) & word, _mm512_set1_epi8(SPIKE_PRED_TYPE));
    pred = _mm512_mask_mov_epi8(pred, spike_equal8(role, SPIKE_ROLE_PREFIX) & spike_equal8(vkind, SPIKE_KIND_STAR), _mm512_set1_epi8(SPIKE_PRED_STAR));
    return pred;
}

BUSTER_INLINE __m512i spike_pred_bits8(__m512i pred)
{
    return _mm512_shuffle_epi8(_mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)spike_role_tables.pred_bits)), pred);
}

// The typed state of 64 lanes from their generate, propagate and carry-in bits: t[i] = g[i] | (p[i] & t[i - 1]), as an
// add's carries.
BUSTER_INLINE uint64_t spike_typed_carries(uint64_t generate, uint64_t propagate, uint64_t carry_in, uint64_t* carry_out)
{
    const uint64_t x = generate | propagate;
    uint64_t sum;
    uint64_t sum_carry = __builtin_add_overflow(x, generate, &sum);
    uint64_t total;
    sum_carry |= __builtin_add_overflow(sum, carry_in, &total);
    // The carry into each bit, then the carry out of it.
    const uint64_t into = total ^ x ^ generate;
    *carry_out = sum_carry;
    return (into >> 1) | (sum_carry << 63);
}

// One block of K2 in AVX-512 (see above). Returns the rounds it took, or 0 if it didn't converge. *context_out gets the
// block's statement contexts (spike_statement_contexts), from the roles it settles on. When it converges, it also puts
// the block's openers still open at its end onto the group stacks, and the roles of its last two tokens into `carry`.
static uint32_t spike_roles_block_avx512(SpikeKernels* kernels, uint32_t block, uint32_t live, SpikeRoleCarry* carry, const uint8_t* starts, uint64_t* typed_out,
                                         SpikeStatementContext* context_out)
{
    const SpikeRoleTables* t = &spike_role_tables;
    const uint8_t* vkinds = kernels->vkinds;
    const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
    const __m512i vk = _mm512_loadu_si512(vkinds + block);
    const __m512i vk_previous = _mm512_loadu_si512(vkinds + block - 1);
    const __m512i vk_before = _mm512_loadu_si512(vkinds + block - 2);
    const __m512i vk_next = _mm512_loadu_si512(vkinds + block + 1);
    const __m512i cls = spike_table8(t->classes, vk);
    const __m512i fixed = spike_table8(t->fixed, vk);
    const __mmask64 fixed_flags = _mm512_test_epi8_mask(spike_table8(t->fixed_flags, vk), _mm512_set1_epi8(1));
    const __m512i kind_bits = spike_table8(t->vkind_bits, vk);
    const __m512i previous_bits = spike_table8(t->vkind_bits, vk_previous);
    const __m512i before_bits = spike_table8(t->vkind_bits, vk_before);
    const __m512i next_bits = spike_table8(t->vkind_bits, vk_next);
    const uint32_t depth_base = kernels->depths[block];
    // The enclosing opener: a lane of the block, or an open group before it, whose role comes from carry->group_roles;
    // the window index of the group stacks for each token's depth before it, (depth - 1) - (base - 64); and the block's
    // openers still open at its end, the only ones a later block reads from the group stacks. K1 found them all.
    const SpikeBracketBlock* view = &kernels->bracket_blocks[block / 64];
    const __m512i enclosing_lane = _mm512_load_si512(view->opener_lanes);
    const __mmask64 inside = _mm512_cmplt_epu8_mask(enclosing_lane, _mm512_set1_epi8(64));
    const __m512i window_index = _mm512_add_epi8(_mm512_load_si512(view->relative_depths), _mm512_set1_epi8(63));
    const uint64_t open = view->open & live_mask;
    // The 64 depths below the block's start, from the group stacks (which have BUSTER_SPIKE_DEPTH_PADDING entries below
    // depth 0). Lanes with no enclosing opener at all are at depth 0: their entry is below depth 0, SPIKE_ROLE_NONE.
    const __m512i outside_group = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + (int64_t)depth_base - 64));
    const uint64_t outside_typed = _mm512_test_epi8_mask(_mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_typed + (int64_t)depth_base - 64)),
                                                         _mm512_set1_epi8(1));
    // The tagged-type keyword before a `{` after a tag or an attribute, which doesn't depend on roles. The rules read it
    // only after a tag's name or an attribute's `)` (after `__attribute__`), so a `{` after a `)` whose opener, in the
    // block, follows anything else (a function's or a statement's body) gets none.
    uint8_t tag_keywords[64];
    const __m512i previous_opener_lane = _mm512_permutex2var_epi8(enclosing_lane, spike_shifts.up8[0], _mm512_setzero_si512());
    const __mmask64 previous_inside = (inside << 1) & ~(__mmask64)1;
    const __mmask64 attribute_opener = spike_equal8(_mm512_permutexvar_epi8(previous_opener_lane, vk_previous), SPIKE_KIND_ATTRIBUTE) | ~previous_inside;
    uint64_t tag_lanes = spike_equal8(vk, SPIKE_KIND_L_BRACE) & ((spike_equal8(vk_previous, SPIKE_KIND_R_PAREN) & attribute_opener) |
                                                                   _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_NAME))) & live_mask;
    __mmask64 tag_enum = 0;
    __mmask64 tag_none = 0;
    while (tag_lanes != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(tag_lanes);
        tag_lanes &= tag_lanes - 1;
        tag_keywords[lane] = spike_tag_keyword(kernels, block + lane - 1);
        tag_enum |= (__mmask64)(tag_keywords[lane] == SPIKE_KIND_ENUM) << lane;
        tag_none |= (__mmask64)(tag_keywords[lane] == SPIKE_KIND_END) << lane;
    }
    // Masks of the kinds that don't change between rounds.
    const __mmask64 previous_member = spike_equal8(vk_previous, SPIKE_KIND_PERIOD) | spike_equal8(vk_previous, SPIKE_KIND_ARROW);
    const __mmask64 previous_tag_keyword = spike_equal8(vk_previous, SPIKE_KIND_STRUCT) | spike_equal8(vk_previous, SPIKE_KIND_ENUM);
    const __m512i tag_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_TAG), spike_equal8(vk_next, SPIKE_KIND_L_BRACE), _mm512_set1_epi8(SPIKE_ROLE_LEAF_RIGHT));
    const __mmask64 previous_asm = spike_equal8(vk_previous, SPIKE_KIND_ASM);
    const __mmask64 previous_asm_qualifier = _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_ASM_QUALIFIER));
    const __mmask64 next_paren = spike_equal8(vk_next, SPIKE_KIND_L_PAREN);
    const __mmask64 next_colon = spike_equal8(vk_next, SPIKE_KIND_COLON);
    const __mmask64 next_starts_type = _mm512_test_epi8_mask(next_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_STARTS_TYPE));
    const __mmask64 next_starts_declaration = _mm512_test_epi8_mask(next_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_STARTS_DECLARATION));
    const __mmask64 next_function = spike_equal8(vk_next, SPIKE_KIND_R_PAREN) | spike_equal8(vk_next, SPIKE_KIND_ELLIPSIS);
    const __mmask64 previous_name = _mm512_test_epi8_mask(previous_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_NAME));
    const __mmask64 attribute_list = spike_equal8(vk_previous, SPIKE_KIND_L_PAREN) & spike_equal8(vk_before, SPIKE_KIND_ATTRIBUTE);
    const __mmask64 keyword = _mm512_test_epi8_mask(kind_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_KEYWORD));
    const __mmask64 word = _mm512_test_epi8_mask(kind_bits, _mm512_set1_epi8(BUSTER_SPIKE_VKIND_TYPE_WORD));
    (void)before_bits;
    const __m512i paren_previous = spike_table8(t->paren_previous, vk_previous);
    const __mmask64 class_string = spike_equal8(cls, SPIKE_RULE_CLASS_STRING);
    const __mmask64 class_identifier = spike_equal8(cls, SPIKE_RULE_CLASS_IDENTIFIER);
    const __mmask64 class_type_name = spike_equal8(cls, SPIKE_RULE_CLASS_TYPE_NAME);
    const __mmask64 class_qualifier = spike_equal8(cls, SPIKE_RULE_CLASS_QUALIFIER);
    const __mmask64 class_storage = spike_equal8(cls, SPIKE_RULE_CLASS_STORAGE);
    const __mmask64 class_goto = spike_equal8(cls, SPIKE_RULE_CLASS_GOTO);
    const __mmask64 class_atomic = spike_equal8(cls, SPIKE_RULE_CLASS_ATOMIC);
    const __mmask64 class_extension = spike_equal8(cls, SPIKE_RULE_CLASS_EXTENSION);
    const __mmask64 class_comma = spike_equal8(cls, SPIKE_RULE_CLASS_COMMA);
    const __mmask64 class_colon = spike_equal8(cls, SPIKE_RULE_CLASS_COLON);
    const __mmask64 class_ellipsis = spike_equal8(cls, SPIKE_RULE_CLASS_ELLIPSIS);
    const __mmask64 class_unary = spike_equal8(cls, SPIKE_RULE_CLASS_UNARY);
    const __mmask64 class_paren = spike_equal8(cls, SPIKE_RULE_CLASS_PAREN);
    const __mmask64 class_brace = spike_equal8(cls, SPIKE_RULE_CLASS_BRACE);
    const __mmask64 class_closer = spike_equal8(cls, SPIKE_RULE_CLASS_CLOSER);
    const __mmask64 class_fixed = spike_equal8(cls, SPIKE_RULE_CLASS_FIXED);
    // The classes whose rules read the token before.
    const __mmask64 reads_previous = class_type_name | class_qualifier | class_storage | class_goto | class_atomic | class_unary | class_paren | class_brace;
    const __m512i unary = spike_table8(t->unary, vk);
    const __m512i binary = spike_table8(t->binary, vk);
    const __m512i cast_closer = spike_table8(t->cast_closers, vk_next);
    // The two roles before the block, for the shifts: lane 0 of the first fill, lanes 0 and 1 of the second.
    const __m512i fill_previous = _mm512_castsi128_si512(_mm_cvtsi32_si128(carry->previous_role));
    const __m512i fill_before = _mm512_castsi128_si512(_mm_cvtsi32_si128(carry->before_role | (carry->previous_role << 8)));
    const __m512i up2 = spike_shifts.up2_with_fill;
    // Rounds, from every lane's guessed role (or, for the check of the rounds' end, the roles in `starts`) and the typed
    // state it gives (without the attributes' copies).
    const __mmask64 star_kind = spike_equal8(vk, SPIKE_KIND_STAR);
    __m512i roles = starts != 0 ? _mm512_loadu_si512(starts + block) : spike_table8(t->guesses, vk);
    if (starts == 0)
    {
        // Where a kind's commonest role is often wrong, what a round would give from the others' guesses, by the same
        // rules: `(` (a call's, a group's, a condition's, an attribute's), * & + - && ++ --, a tag's name, `{` after
        // struct, enum or `=`, and the closers by their opener's guess. They change how many rounds a block takes,
        // never their result.
        const __m512i guess_previous = _mm512_permutex2var_epi8(roles, spike_shifts.up8[0], fill_previous);
        const __m512i guess_pred_bits = spike_pred_bits8(spike_preds8(guess_previous, vk_previous));
        const __mmask64 guess_operand = _mm512_test_epi8_mask(guess_pred_bits, _mm512_set1_epi8((char)BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_OPERAND)));
        const __mmask64 guess_function = _mm512_test_epi8_mask(guess_pred_bits, _mm512_set1_epi8((char)(BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_TYPE) |
                                                                                                      BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_SPECIFIER) |
                                                                                                      BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_STAR)))) & next_function;
        // An attribute's and an asm's `(` share their features with OPEN_ATTRIBUTE's.
        const __m512i by_previous = _mm512_mask_mov_epi8(paren_previous, _mm512_cmpge_epu8_mask(paren_previous, _mm512_set1_epi8((char)BUSTER_SPIKE_PAREN_ASM)),
                                                         _mm512_set1_epi8(SPIKE_ROLE_OPEN_ATTRIBUTE));
        __m512i paren_guess = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPEN_GROUP), guess_function, _mm512_set1_epi8(SPIKE_ROLE_OPEN_FUNCTION));
        paren_guess = _mm512_mask_mov_epi8(paren_guess, next_starts_type, _mm512_set1_epi8(SPIKE_ROLE_OPEN_CAST));
        paren_guess = _mm512_mask_mov_epi8(paren_guess, guess_operand, _mm512_set1_epi8(SPIKE_ROLE_OPEN_CALL));
        paren_guess = _mm512_mask_mov_epi8(paren_guess, previous_asm, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ASM));
        paren_guess = _mm512_mask_mov_epi8(paren_guess, attribute_list, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ATTRIBUTE_LIST));
        paren_guess = _mm512_mask_mov_epi8(paren_guess, _mm512_test_epi8_mask(paren_previous, paren_previous), by_previous);
        roles = _mm512_mask_mov_epi8(roles, class_paren, paren_guess);
        roles = _mm512_mask_mov_epi8(roles, class_unary, _mm512_mask_mov_epi8(unary, guess_operand, binary));
        roles = _mm512_mask_mov_epi8(roles, class_identifier & previous_tag_keyword, tag_role);
        __m512i brace_guess = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPEN_BLOCK), spike_equal8(vk_previous, SPIKE_KIND_EQUAL),
                                                   _mm512_set1_epi8(SPIKE_ROLE_OPEN_INITIALIZER));
        brace_guess = _mm512_mask_mov_epi8(brace_guess, spike_equal8(vk_previous, SPIKE_KIND_STRUCT) | (previous_name & spike_equal8(vk_before, SPIKE_KIND_STRUCT)),
                                           _mm512_set1_epi8(SPIKE_ROLE_OPEN_STRUCT));
        brace_guess = _mm512_mask_mov_epi8(brace_guess, spike_equal8(vk_previous, SPIKE_KIND_ENUM) | (previous_name & spike_equal8(vk_before, SPIKE_KIND_ENUM)),
                                           _mm512_set1_epi8(SPIKE_ROLE_OPEN_ENUM));
        roles = _mm512_mask_mov_epi8(roles, class_brace, brace_guess);
        const __m512i group_guess = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, roles));
        roles = _mm512_mask_mov_epi8(roles, class_closer, _mm512_mask_mov_epi8(spike_table8(t->closers, group_guess), spike_equal8(group_guess, SPIKE_ROLE_OPEN_CAST), cast_closer));
    }
    __m512i features = spike_features8(roles, word, star_kind);
    const __m512i guessed_bits = spike_table8(t->role_bits, roles);
    const uint64_t guessed_generate = (_mm512_test_epi8_mask(guessed_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_TYPE_IF_WORD)) & word) |
                                      _mm512_test_epi8_mask(guessed_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_TYPE));
    const uint64_t guessed_propagate = _mm512_test_epi8_mask(guessed_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_PROPAGATES)) & ~guessed_generate;
    uint64_t guessed_carry;
    uint64_t typed = spike_typed_carries(guessed_generate, guessed_propagate, carry->typed >> 63, &guessed_carry);
    __m512i new_roles = fixed;
    __m512i final_group = outside_group;
    uint64_t new_typed = 0;
    __mmask64 flags = 0;
    uint32_t rounds = 0;
    bool converged = false;
    while (!converged && rounds < BUSTER_SPIKE_ROLE_ROUNDS)
    {
        rounds += 1;
        const __m512i previous_role = _mm512_permutex2var_epi8(roles, spike_shifts.up8[0], fill_previous);
        const __m512i before_role = _mm512_permutex2var_epi8(roles, up2, fill_before);
        const __m512i pred = spike_preds8(previous_role, vk_previous);
        const __m512i pred_bits = spike_pred_bits8(pred);
        const __m512i before_pred = spike_preds8(before_role, vk_before);
        const __m512i before_pred_bits = spike_pred_bits8(before_pred);
        const __m512i group = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, roles));
        const __m512i group_bits = spike_table8(t->group_bits, group);
        const uint64_t typed_before = (typed << 1) | (carry->typed >> 63);
        const __mmask64 operand = _mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_OPERAND)));
        const __mmask64 star = _mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_STAR)));
        const __mmask64 item_or_head = _mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)(BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_ITEM) | BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_HEAD))));
        const __mmask64 before_item_or_head =
            _mm512_test_epi8_mask(before_pred_bits, _mm512_set1_epi8((char)(BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_ITEM) | BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_HEAD))));
        const __mmask64 previous_attribute_right = spike_equal8(previous_role, SPIKE_ROLE_CLOSE_ATTRIBUTE_RIGHT);
        const __mmask64 after_asm = previous_asm | (spike_equal8(previous_role, SPIKE_ROLE_LEAF_RIGHT) & previous_asm_qualifier);
        const __mmask64 in_block = _mm512_test_epi8_mask(group_bits, _mm512_set1_epi8(BUSTER_SPIKE_GROUP_BLOCK));
        // Each class's role. The classes split the lanes, so the roles combine with an OR of each class's lanes rather than
        // a chain of blends.
        __mmask64 flag = fixed_flags;
        // Strings and identifiers.
        const __m512i string_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPERAND), spike_equal8(vk_previous, SPIKE_KIND_STRING), _mm512_set1_epi8(SPIKE_ROLE_STRING_NEXT));
        const __mmask64 tag = previous_tag_keyword | previous_attribute_right;
        __m512i name_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPERAND), tag, tag_role);
        name_role = _mm512_mask_mov_epi8(name_role, previous_member, _mm512_set1_epi8(SPIKE_ROLE_MEMBER));
        // Typedef names.
        const __mmask64 type_position = _mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)(BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_ITEM) | BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_PARAMETER)))) |
                                        (_mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_SPECIFIER))) & ~typed_before);
        const __mmask64 type_label = next_colon & item_or_head & in_block;
        const __mmask64 type_settled = previous_member | tag | spike_equal8(vk_previous, SPIKE_KIND_GOTO) | type_label;
        __m512i type_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPERAND), ~type_settled & type_position, _mm512_set1_epi8(SPIKE_ROLE_SPECIFIER));
        type_role = _mm512_mask_mov_epi8(type_role, tag & ~previous_member, tag_role);
        type_role = _mm512_mask_mov_epi8(type_role, previous_member, _mm512_set1_epi8(SPIKE_ROLE_MEMBER));
        flag |= class_type_name & ~type_settled & ~type_position;
        // Qualifiers, storage classes, goto, _Atomic and __extension__.
        const __m512i qualifier_role = _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_SPECIFIER), star, _mm512_set1_epi8(SPIKE_ROLE_QUALIFIER_LEFT)),
                                                            after_asm, _mm512_set1_epi8(SPIKE_ROLE_LEAF_RIGHT));
        const __m512i storage_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_SPECIFIER), after_asm, _mm512_set1_epi8(SPIKE_ROLE_LEAF_RIGHT));
        const __m512i goto_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_RETURN), after_asm, _mm512_set1_epi8(SPIKE_ROLE_LEAF_RIGHT));
        const __m512i atomic_role = _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_SPECIFIER), star, _mm512_set1_epi8(SPIKE_ROLE_QUALIFIER_LEFT)),
                                                         next_paren, _mm512_set1_epi8(SPIKE_ROLE_LEAF_RIGHT));
        const __m512i extension_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_PREFIX), next_starts_declaration, _mm512_set1_epi8(SPIKE_ROLE_SPECIFIER));
        // * & + - && ++ --.
        const __m512i unary_role = _mm512_mask_mov_epi8(unary, operand, binary);
        // (.
        const __m512i attribute_role = _mm512_permutexvar_epi8(before_pred, _mm512_loadu_si512(t->attribute_by_before));
        const __m512i asm_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPEN_ATTRIBUTE), before_item_or_head, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ASM));
        __m512i by_previous = _mm512_mask_mov_epi8(paren_previous, spike_equal8(paren_previous, BUSTER_SPIKE_PAREN_ATTRIBUTE), attribute_role);
        by_previous = _mm512_mask_mov_epi8(by_previous, spike_equal8(paren_previous, BUSTER_SPIKE_PAREN_ASM), asm_role);
        const __mmask64 function = _mm512_test_epi8_mask(pred_bits, _mm512_set1_epi8((char)(BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_TYPE) | BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_SPECIFIER) |
                                                                                            BUSTER_SPIKE_PRED_BIT(SPIKE_PRED_STAR)))) & next_function;
        __m512i paren_role = _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPEN_GROUP), function, _mm512_set1_epi8(SPIKE_ROLE_OPEN_FUNCTION));
        paren_role = _mm512_mask_mov_epi8(paren_role, next_starts_type, _mm512_set1_epi8(SPIKE_ROLE_OPEN_CAST));
        paren_role = _mm512_mask_mov_epi8(paren_role, operand, _mm512_set1_epi8(SPIKE_ROLE_OPEN_CALL));
        paren_role = _mm512_mask_mov_epi8(paren_role, after_asm, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ASM));
        paren_role = _mm512_mask_mov_epi8(paren_role, attribute_list, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ATTRIBUTE_LIST));
        paren_role = _mm512_mask_mov_epi8(paren_role, _mm512_test_epi8_mask(paren_previous, paren_previous), by_previous);
        // {, from the lowest priority up.
        const __mmask64 previous_closer_head = _mm512_test_epi8_mask(spike_table8(t->role_bits, previous_role), _mm512_set1_epi8(BUSTER_SPIKE_ROLE_HEAD_CLOSER));
        const __mmask64 previous_closer_call = _mm512_test_epi8_mask(spike_table8(t->role_bits, previous_role), _mm512_set1_epi8(BUSTER_SPIKE_ROLE_CALL_CLOSER));
        const __mmask64 nested = (spike_equal8(vk_previous, SPIKE_KIND_L_BRACE) | spike_equal8(vk_previous, SPIKE_KIND_COMMA)) &
                                 _mm512_test_epi8_mask(group_bits, _mm512_set1_epi8(BUSTER_SPIKE_GROUP_INITIALIZER));
        const __mmask64 brace_tag = previous_attribute_right | (spike_equal8(previous_role, SPIKE_ROLE_LEAF_RIGHT) & previous_name);
        const __mmask64 brace_struct = spike_equal8(vk_previous, SPIKE_KIND_STRUCT) | (previous_name & spike_equal8(vk_before, SPIKE_KIND_STRUCT));
        const __mmask64 brace_enum = spike_equal8(vk_previous, SPIKE_KIND_ENUM) | (previous_name & spike_equal8(vk_before, SPIKE_KIND_ENUM));
        const __mmask64 brace_initializer = spike_equal8(vk_previous, SPIKE_KIND_EQUAL);
        __m512i brace_role = _mm512_set1_epi8(SPIKE_ROLE_OPEN_BLOCK);
        __mmask64 brace_flag = ~item_or_head;
        brace_role = _mm512_mask_mov_epi8(brace_role, nested, _mm512_set1_epi8(SPIKE_ROLE_OPEN_INITIALIZER));
        brace_flag &= ~nested;
        brace_role = _mm512_mask_mov_epi8(brace_role, spike_equal8(previous_role, SPIKE_ROLE_CLOSE_COMPOUND), _mm512_set1_epi8(SPIKE_ROLE_OPEN_COMPOUND_BODY));
        brace_role = _mm512_mask_mov_epi8(brace_role, previous_closer_call, _mm512_set1_epi8(SPIKE_ROLE_OPEN_BODY));
        brace_role = _mm512_mask_mov_epi8(brace_role, previous_closer_head | spike_equal8(vk_previous, SPIKE_KIND_L_PAREN), _mm512_set1_epi8(SPIKE_ROLE_OPEN_BLOCK));
        brace_flag &= ~(spike_equal8(previous_role, SPIKE_ROLE_CLOSE_COMPOUND) | previous_closer_call | previous_closer_head | spike_equal8(vk_previous, SPIKE_KIND_L_PAREN));
        brace_role = _mm512_mask_mov_epi8(brace_role, brace_tag, _mm512_mask_mov_epi8(_mm512_set1_epi8(SPIKE_ROLE_OPEN_STRUCT), tag_enum, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ENUM)));
        brace_flag = (brace_flag & ~brace_tag) | (brace_tag & tag_none);
        brace_role = _mm512_mask_mov_epi8(brace_role, brace_enum, _mm512_set1_epi8(SPIKE_ROLE_OPEN_ENUM));
        brace_role = _mm512_mask_mov_epi8(brace_role, brace_struct, _mm512_set1_epi8(SPIKE_ROLE_OPEN_STRUCT));
        brace_role = _mm512_mask_mov_epi8(brace_role, brace_initializer, _mm512_set1_epi8(SPIKE_ROLE_OPEN_INITIALIZER));
        brace_flag &= ~(brace_enum | brace_struct | brace_initializer);
        flag |= class_brace & brace_flag;
        // What depends on the group, from this round's openers (( and { by their rules, [ fixed): a closer and the commas,
        // colons and ellipses in a group then settle in the same round as their opener.
        const __m512i opener_role = _mm512_mask_mov_epi8(_mm512_mask_mov_epi8(fixed, class_paren, paren_role), class_brace, brace_role);
        const __m512i group_now = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, opener_role));
        final_group = group_now;
        const __m512i closer_role = _mm512_mask_mov_epi8(spike_table8(t->closers, group_now), spike_equal8(group_now, SPIKE_ROLE_OPEN_CAST), cast_closer);
        // The classes together; FIXED lanes keep their kind's role.
        const __m512i fixed_part = _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(class_fixed, fixed), _mm512_maskz_mov_epi8(class_string, string_role),
                                                             _mm512_maskz_mov_epi8(class_identifier, name_role), 0xFE);
        const __m512i specifier_part = _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(class_type_name, type_role), _mm512_maskz_mov_epi8(class_qualifier, qualifier_role),
                                                                 _mm512_maskz_mov_epi8(class_storage, storage_role), 0xFE);
        const __m512i keyword_part = _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(class_goto, goto_role), _mm512_maskz_mov_epi8(class_atomic, atomic_role),
                                                               _mm512_maskz_mov_epi8(class_extension, extension_role), 0xFE);
        const __m512i group_part = _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(class_comma, spike_table8(t->comma, group_now)),
                                                             _mm512_maskz_mov_epi8(class_colon, spike_table8(t->colon, group_now)),
                                                             _mm512_maskz_mov_epi8(class_ellipsis, spike_table8(t->ellipsis, group_now)), 0xFE);
        const __m512i operator_part = _mm512_ternarylogic_epi64(_mm512_maskz_mov_epi8(class_unary, unary_role), _mm512_maskz_mov_epi8(class_paren, paren_role),
                                                                _mm512_maskz_mov_epi8(class_brace, brace_role), 0xFE);
        __m512i role = _mm512_or_si512(_mm512_ternarylogic_epi64(fixed_part, specifier_part, keyword_part, 0xFE),
                                       _mm512_ternarylogic_epi64(group_part, operator_part, _mm512_maskz_mov_epi8(class_closer, closer_role), 0xFE));
        // An attribute's names, whatever their spelling.
        const __mmask64 attribute_name = _mm512_test_epi8_mask(spike_table8(t->group_bits, group_now), _mm512_set1_epi8(BUSTER_SPIKE_GROUP_ATTRIBUTE_LIST)) & keyword;
        role = _mm512_mask_mov_epi8(role, attribute_name, _mm512_set1_epi8(SPIKE_ROLE_OPERAND));
        flag &= ~attribute_name;
        flag |= spike_equal8(role, SPIKE_ROLE_ERROR);
        // The typed state.
        const __m512i role_bits = spike_table8(t->role_bits, role);
        const uint64_t generate = (_mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_TYPE_IF_WORD)) & word) |
                                  _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_TYPE));
        const uint64_t propagate = _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_PROPAGATES)) & ~generate;
        uint64_t copies = _mm512_test_epi8_mask(role_bits, _mm512_set1_epi8(BUSTER_SPIKE_ROLE_COPIES_TYPED));
        // An attribute's closer takes the state from before the attribute's keyword, two tokens before its opener.
        uint64_t copied = 0;
        uint64_t copy_lanes = copies & live_mask;
        while (copy_lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(copy_lanes);
            copy_lanes &= copy_lanes - 1;
            const uint32_t opener = kernels->enclosing[block + lane];
            bool value = false;
            if (opener != BUSTER_SPIKE_NONE && opener >= 2 && opener - 2 >= block)
            {
                value = (typed >> (opener - 2 - block)) & 1;
            }
            else if (opener != BUSTER_SPIKE_NONE && opener >= 2 && opener - 2 + 64 >= block)
            {
                value = (carry->typed >> (opener - 2 + 64 - block)) & 1;
            }
            else if (opener != BUSTER_SPIKE_NONE && opener >= 2)
            {
                value = (outside_typed >> lane) & 1;
            }
            copied |= (uint64_t)value << lane;
        }
        uint64_t carry_out;
        new_typed = spike_typed_carries(generate | copied, propagate & ~copies, carry->typed >> 63, &carry_out);
        new_roles = role;
        flags = flag;
        // The round's roles are final when no lane reads what changed in them, so that the next round would compute them
        // again. Of the roles before it a round reads, per class: the features of the token before (all of them, or for
        // an identifier whether it is CLOSE_ATTRIBUTE_RIGHT), the pred of the token two back (for `(`), the features of
        // the lane's opener (for typedef names and `{`), and the typed state before (for typedef names); an attribute's
        // closer reads typed states before its opener. The commas, colons, ellipses, closers and attribute names of a
        // group read its opener as this round sets it, which changes only if the opener is a lane that reads a change.
        const __m512i new_features = spike_features8(new_roles, word, star_kind);
        const uint64_t changed = _mm512_cmpneq_epi8_mask(new_features, features);
        const __m512i attribute_right = _mm512_set1_epi8((char)(BUSTER_SPIKE_FEATURE_ATTRIBUTE_RIGHT | SPIKE_PRED_LEAF));
        const uint64_t right_changed = _mm512_cmpeq_epi8_mask(new_features, attribute_right) ^ _mm512_cmpeq_epi8_mask(features, attribute_right);
        const uint64_t typed_changed = (new_typed ^ typed) & live_mask;
        const uint64_t opener_changed = _mm512_test_epi8_mask(_mm512_permutexvar_epi8(enclosing_lane, _mm512_movm_epi8(changed)), _mm512_set1_epi8(-1)) & inside;
        const uint64_t affected = ((changed << 1) & reads_previous) | ((right_changed << 1) & class_identifier) | ((changed << 2) & class_paren) |
                            (opener_changed & (class_type_name | class_brace)) | ((typed_changed << 1) & class_type_name) | (typed_changed != 0 ? copies : 0);
        converged = (affected & live_mask) == 0;
        roles = new_roles;
        features = new_features;
        typed = new_typed;
    }
    // The statement contexts, from the final roles (the last round's openers' roles, its group roles): a cast candidate's
    // opener that its closer turns into another role later is no block either way. A closer's is its opener's group: the
    // opener's lane's, or the group two depths below the closer's depth on the group stack.
    const __m512i outside_outer = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + (int64_t)depth_base - 65));
    const __m512i outer_group = _mm512_mask_mov_epi8(outside_outer, inside, _mm512_permutexvar_epi8(enclosing_lane, final_group));
    const __m512i context_group = _mm512_mask_mov_epi8(final_group, class_closer, outer_group);
    // A closer's opener's keyword: the kind before the opener's lane, or the group stack's.
    const __m512i outside_keyword = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_keywords + (int64_t)depth_base - 64));
    const __m512i opener_keyword = _mm512_mask_mov_epi8(outside_keyword, inside, _mm512_permutexvar_epi8(enclosing_lane, vk_previous));
    *context_out = (SpikeStatementContext){
        .blocks = _mm512_test_epi8_mask(spike_table8(t->group_bits, context_group), _mm512_set1_epi8(BUSTER_SPIKE_GROUP_BLOCK)) & live_mask,
        .ifs = spike_equal8(opener_keyword, SPIKE_KIND_IF) & class_closer & live_mask,
        .whiles = spike_equal8(opener_keyword, SPIKE_KIND_WHILE) & class_closer & live_mask,
    };
    _mm512_storeu_si512(kernels->roles + block, roles);
    // The flags from K3 stay.
    const __m512i old_flags = _mm512_loadu_si512(kernels->flags + block);
    _mm512_storeu_si512(kernels->flags + block, _mm512_mask_mov_epi8(old_flags, flags & live_mask, _mm512_set1_epi8(1)));
    *typed_out = typed;
    if (converged)
    {
        // The openers still open at the block's end, for the later blocks that read the group stacks, from the registers
        // (a byte loaded right after the 64-byte stores above would wait for them): they sit at consecutive depths from the
        // first one's, in lane order, so each stack takes them compressed, in one store from that depth. What the store
        // writes past them is above every group open after the block, which no block reads before its opener writes it.
        // The stores stay in the stacks: K2 runs on balanced files only, whose depths are at most half their tokens, and
        // the stacks have an entry per token and 2 * BUSTER_SPIKE_TOKEN_PADDING more.
        if (open != 0)
        {
            const uint32_t depth = depth_base + (uint32_t)(int32_t)view->relative_depths[__builtin_ctzll(open)];
            const uint64_t typed_two_back = (typed << 2) | (carry->typed >> 62);
            _mm512_storeu_si512(carry->group_roles + depth, _mm512_maskz_compress_epi8(open, roles));
            _mm512_storeu_si512(carry->group_typed + depth, _mm512_maskz_compress_epi8(open, _mm512_maskz_mov_epi8(typed_two_back, _mm512_set1_epi8(1))));
            _mm512_storeu_si512(carry->group_keywords + depth, _mm512_maskz_compress_epi8(open, vk_previous));
        }
        const __m512i last_two = _mm512_permutexvar_epi8(_mm512_set1_epi16((short)((live - 1) | (live >= 2 ? live - 2 : live - 1) << 8)), roles);
        const uint32_t bytes = (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(last_two));
        carry->previous_role = (uint8_t)bytes;
        carry->before_role = (uint8_t)(bytes >> 8);
    }
    return converged ? rounds : 0;
}

// The statement contexts of a block the twin's loop took, from its final roles, as a converged block works them out
// (spike_roles_block_avx512): spike_statement_context_block's, in AVX-512. A token's group is its opener's lane's role,
// or the group stacks' at the depth below its own, and a closer's context is its opener's group.
static SpikeStatementContext spike_statement_context_avx512(const SpikeKernels* kernels, const SpikeRoleCarry* carry, uint32_t block, uint32_t live)
{
    const SpikeRoleTables* t = &spike_role_tables;
    const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
    const SpikeBracketBlock* view = &kernels->bracket_blocks[block / 64];
    const __m512i enclosing_lane = _mm512_load_si512(view->opener_lanes);
    const __mmask64 inside = _mm512_cmplt_epu8_mask(enclosing_lane, _mm512_set1_epi8(64));
    const __m512i window_index = _mm512_add_epi8(_mm512_load_si512(view->relative_depths), _mm512_set1_epi8(63));
    const int64_t depth_base = kernels->depths[block];
    const __m512i roles = _mm512_loadu_si512(kernels->roles + block);
    const __m512i vk = _mm512_loadu_si512(kernels->vkinds + block);
    const __m512i vk_previous = _mm512_loadu_si512(kernels->vkinds + block - 1);
    const __mmask64 closers = _mm512_cmplt_epi8_mask(spike_table8((const uint8_t*)spike_kind_deltas, vk), _mm512_setzero_si512()) & live_mask;
    const __m512i outside_group = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + depth_base - 64));
    const __m512i group = _mm512_mask_mov_epi8(outside_group, inside, _mm512_permutexvar_epi8(enclosing_lane, roles));
    const __m512i outside_outer = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_roles + depth_base - 65));
    const __m512i outer_group = _mm512_mask_mov_epi8(outside_outer, inside, _mm512_permutexvar_epi8(enclosing_lane, group));
    const __m512i context_group = _mm512_mask_mov_epi8(group, closers, outer_group);
    const __m512i outside_keyword = _mm512_permutexvar_epi8(window_index, _mm512_loadu_si512(carry->group_keywords + depth_base - 64));
    const __m512i opener_keyword = _mm512_mask_mov_epi8(outside_keyword, inside, _mm512_permutexvar_epi8(enclosing_lane, vk_previous));
    const SpikeStatementContext result = {
        .blocks = _mm512_test_epi8_mask(spike_table8(t->group_bits, context_group), _mm512_set1_epi8(BUSTER_SPIKE_GROUP_BLOCK)) & live_mask,
        .ifs = spike_equal8(opener_keyword, SPIKE_KIND_IF) & closers,
        .whiles = spike_equal8(opener_keyword, SPIKE_KIND_WHILE) & closers,
    };
    return result;
}

// A block K2's rounds didn't settle, through the twin's loop, from its start: its roles, flags and typed states, its
// statement contexts, and its openers still open at its end (K1's view has them) onto the group stacks, at their
// depths, for the later blocks that read them. Returns its typed states as a mask. Out of line, so that this rare path
// leaves the rounds' code alone.
__attribute__((noinline)) static uint64_t spike_roles_twin_block(SpikeKernels* kernels, uint32_t block, uint32_t live, uint8_t* typed, SpikeRoleCarry* carry,
                                                                  SpikeStatementContext* context)
{
    for (uint32_t i = block; i < block + live; i += 1)
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
    const uint64_t result = _mm512_test_epi8_mask(_mm512_loadu_si512(typed + block), _mm512_set1_epi8(1));
    *context = spike_statement_context_avx512(kernels, carry, block, live);
    uint64_t openers = kernels->bracket_blocks[block / 64].open;
    while (openers != 0)
    {
        const uint32_t i = block + (uint32_t)__builtin_ctzll(openers);
        openers &= openers - 1;
        carry->group_roles[kernels->depths[i]] = kernels->roles[i];
        carry->group_typed[kernels->depths[i]] = i >= 2 ? typed[i - 2] : 0;
        carry->group_keywords[kernels->depths[i]] = kernels->vkinds[i - 1];
    }
    carry->previous_role = kernels->roles[block + live - 1];
    carry->before_role = live >= 2 ? kernels->roles[block + live - 2] : carry->previous_role;
    return result;
}

// K2's rounds on the blocks from `block` on, in order, until one doesn't converge (or, with `twin_only`, at once): their
// roles, typed states (into `typed` as bytes too) and statement contexts. Returns the first block they didn't settle, or
// count. The loop calls nothing, and keeps the carry in a copy whose address never leaves it, so that its constants
// and the carry stay in registers from block to block.
static uint32_t spike_roles_rounds_avx512(SpikeKernels* kernels, uint32_t count, uint8_t* typed, SpikeStatementContext* contexts, uint64_t* rounds,
                                          const uint8_t* starts, bool twin_only, SpikeRoleCarry* carry_io, uint32_t block)
{
    SpikeRoleCarry carry = *carry_io;
    bool converging = !twin_only;
    while (block < count && converging)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        uint64_t typed_mask = 0;
        SpikeStatementContext context;
        const uint32_t taken = spike_roles_block_avx512(kernels, block, live, &carry, starts, &typed_mask, &context);
        converging = taken != 0;
        if (converging)
        {
            rounds[taken] += 1;
            _mm512_storeu_si512(typed + block, _mm512_maskz_mov_epi8(typed_mask, _mm512_set1_epi8(1)));
            // A cast candidate's opener takes the role its closer decided, as spike_closer_role does; one that stays a cast
            // keeps OPEN_CAST.
            const __m512i role = _mm512_loadu_si512(kernels->roles + block);
            uint64_t casts = (spike_equal8(role, SPIKE_ROLE_CLOSE_COMPOUND) | spike_equal8(role, SPIKE_ROLE_CLOSE_FUNCTION)) &
                             (live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1));
            while (casts != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(casts);
                casts &= casts - 1;
                const uint32_t opener = kernels->enclosing[block + lane];
                if (opener != BUSTER_SPIKE_NONE && kernels->roles[opener] == SPIKE_ROLE_OPEN_CAST)
                {
                    const uint8_t closer_role = kernels->roles[block + lane];
                    kernels->roles[opener] = closer_role == SPIKE_ROLE_CLOSE_COMPOUND ? SPIKE_ROLE_OPEN_COMPOUND :
                                             closer_role == SPIKE_ROLE_CLOSE_CAST ? SPIKE_ROLE_OPEN_CAST : SPIKE_ROLE_OPEN_FUNCTION;
                }
            }
            contexts[block / 64] = context;
            carry.typed = live == 64 ? typed_mask : typed_mask << (64 - live);
            block += 64;
        }
    }
    *carry_io = carry;
    return block < count ? block : count;
}

// K2 in AVX-512: the blocks in order, each in rounds; a block that doesn't converge runs the twin's loop. Each block's
// enclosing openers and depths come from K1's view of it (kernels->bracket_blocks). `typed` is a byte per token (the
// twin's array, which the fallback and the group stacks read), and `groups` holds the stacks of an entry per depth.
// `contexts` gets each block's statement contexts, which the vector K5 reads. `rounds` (BUSTER_SPIKE_ROLE_ROUNDS + 1
// counters) counts the blocks by the rounds they took, index 0 for those the twin did. In a check, `starts` gives every
// token the role its rounds start from instead of its kind's guess (else 0), and `twin_only` sends every block to the
// twin's loop.
static void spike_roles_starting_avx512(SpikeKernels* kernels, uint32_t count, uint8_t* typed, const SpikeGroupStacks* groups, SpikeStatementContext* contexts,
                                        uint64_t* rounds, const uint8_t* starts, bool twin_only)
{
    SpikeRoleCarry carry = {
        .previous_role = SPIKE_ROLE_NONE,
        .before_role = SPIKE_ROLE_NONE,
        .typed = 0,
        .group_roles = groups->roles,
        .group_typed = groups->typed,
        .group_keywords = groups->keywords,
    };
    uint32_t block = 0;
    while (block < count)
    {
        block = spike_roles_rounds_avx512(kernels, count, typed, contexts, rounds, starts, twin_only, &carry, block);
        if (block < count)
        {
            const uint32_t live = count - block < 64 ? count - block : 64;
            SpikeStatementContext context;
            const uint64_t typed_mask = spike_roles_twin_block(kernels, block, live, typed, &carry, &context);
            rounds[0] += 1;
            contexts[block / 64] = context;
            carry.typed = live == 64 ? typed_mask : typed_mask << (64 - live);
            block += 64;
        }
    }
}

static void spike_roles_avx512(SpikeKernels* kernels, uint32_t count, uint8_t* typed, const SpikeGroupStacks* groups, SpikeStatementContext* contexts, uint64_t* rounds)
{
    spike_roles_starting_avx512(kernels, count, typed, groups, contexts, rounds, 0, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// K3's classification, K1's second part and K5 in AVX-512

// K3's classification in AVX-512, with a gather: 16 identifiers' typedef bytes at a time (vpgatherdd of the dword at
// each name's byte, so `typedef_names` has 3 bytes of slack). Lanes that aren't identifiers read name 0.
static void spike_classify_gather_avx512(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    const __m512i identifier = _mm512_set1_epi8(SPIKE_KIND_IDENTIFIER);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i kind = _mm512_loadu_si512(kinds + block);
        const __mmask64 names_mask = _mm512_cmpeq_epi8_mask(kind, identifier);
        __mmask64 type_names = 0;
        __mmask64 scoped = 0;
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __mmask16 lanes = (__mmask16)(names_mask >> (16 * q));
            const __m512i ids = _mm512_maskz_loadu_epi32(lanes, names + block + 16 * q);
            const __m512i bytes = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, ids, kernels->typedef_names, 1), _mm512_set1_epi32(0xFF));
            type_names |= (__mmask64)_mm512_test_epi32_mask(bytes, bytes) << (16 * q);
            scoped |= (__mmask64)_mm512_cmpeq_epi32_mask(bytes, _mm512_set1_epi32(BUSTER_SPIKE_TYPEDEF_SCOPED)) << (16 * q);
        }
        _mm512_storeu_si512(kernels->vkinds + block, _mm512_mask_mov_epi8(kind, type_names, _mm512_set1_epi8(SPIKE_VKIND_TYPE_NAME)));
        _mm512_storeu_si512(kernels->flags + block, _mm512_maskz_mov_epi8(scoped, _mm512_set1_epi8(1)));
    }
    spike_classify_declarators(kernels);
}

// The same with a scalar load per identifier, over the block's identifier mask.
static void spike_classify_mask_avx512(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count)
{
    const __m512i identifier = _mm512_set1_epi8(SPIKE_KIND_IDENTIFIER);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i kind = _mm512_loadu_si512(kinds + block);
        uint64_t lanes = _mm512_cmpeq_epi8_mask(kind, identifier);
        uint64_t type_names = 0;
        uint64_t scoped = 0;
        while (lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            const uint8_t meaning = kernels->typedef_names[names[block + lane]];
            type_names |= (uint64_t)(meaning != 0) << lane;
            scoped |= (uint64_t)(meaning == BUSTER_SPIKE_TYPEDEF_SCOPED) << lane;
        }
        _mm512_storeu_si512(kernels->vkinds + block, _mm512_mask_mov_epi8(kind, type_names, _mm512_set1_epi8(SPIKE_VKIND_TYPE_NAME)));
        _mm512_storeu_si512(kernels->flags + block, _mm512_maskz_mov_epi8(scoped, _mm512_set1_epi8(1)));
    }
    spike_classify_declarators(kernels);
}

// K1's second part in AVX-512: the matching runs over the ? case default and `:` tokens only (a mask per block, then
// the twin's stack), and the depth of their groups is a prefix sum of their deltas, added to each token's depth.
static void spike_secondary_avx512(SpikeKernels* kernels, uint32_t count, uint32_t* secondary)
{
    uint32_t open = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i role = _mm512_loadu_si512(kernels->roles + block);
        uint64_t lanes = (spike_equal8(role, SPIKE_ROLE_OPEN_TERNARY) | spike_equal8(role, SPIKE_ROLE_OPEN_CASE) | spike_equal8(role, SPIKE_ROLE_CLOSE_TERNARY) |
                          spike_equal8(role, SPIKE_ROLE_CLOSE_CASE) | spike_equal8(role, SPIKE_ROLE_LABEL)) &
                         (count - block < 64 ? (((uint64_t)1 << (count - block)) - 1) : ~(uint64_t)0);
        while (lanes != 0)
        {
            const uint32_t i = block + (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            if (kernels->roles[i] == SPIKE_ROLE_OPEN_TERNARY || kernels->roles[i] == SPIKE_ROLE_OPEN_CASE)
            {
                secondary[open] = i;
                open += 1;
            }
            else
            {
                const uint32_t depth = kernels->depths[i];
                while (open != 0 && kernels->depths[secondary[open - 1]] > depth)
                {
                    kernels->flags[secondary[open - 1]] = 1;
                    kernels->unbalanced = true;
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
    }
    while (open != 0)
    {
        kernels->flags[secondary[open - 1]] = 1;
        kernels->unbalanced = true;
        open -= 1;
    }
    // The node depths: +1 at an opener, -1 at a closer, exclusive prefix sums, added to the depth of ( [ {, less one
    // at a closer of any group.
    int8_t deltas[128] = { 0 };
    deltas[SPIKE_ROLE_OPEN_TERNARY] = 1;
    deltas[SPIKE_ROLE_OPEN_CASE] = 1;
    deltas[SPIKE_ROLE_CLOSE_TERNARY] = -1;
    deltas[SPIKE_ROLE_CLOSE_CASE] = -1;
    int8_t closers[128] = { 0 };
    for (uint32_t r = 0; r < SPIKE_ROLE_COUNT; r += 1)
    {
        closers[r] = (int8_t)(spike_role_deltas[r] < 0);
    }
    const __m512i delta_low = _mm512_loadu_si512(deltas);
    const __m512i delta_high = _mm512_loadu_si512(deltas + 64);
    const __m512i closer_low = _mm512_loadu_si512(closers);
    const __m512i closer_high = _mm512_loadu_si512(closers + 64);
    uint32_t inner = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i role = _mm512_loadu_si512(kernels->roles + block);
        const __m512i delta = _mm512_permutex2var_epi8(delta_low, role, delta_high);
        const __m512i after = spike_prefix_sum8(delta);
        // The secondary depth before each token, less one at a closer.
        const __m512i before = _mm512_sub_epi8(_mm512_sub_epi8(after, delta), _mm512_permutex2var_epi8(closer_low, role, closer_high));
        const __m512i base = _mm512_set1_epi32((int)inner);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __m128i quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                    q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
            const uint32_t* depths = kernels->depths + block + 16 * q;
            _mm512_storeu_si512(kernels->node_depths + block + 16 * q, _mm512_add_epi32(_mm512_loadu_si512(depths), _mm512_add_epi32(base, _mm512_cvtepi8_epi32(quarter))));
        }
        int8_t after_bytes[64];
        _mm512_storeu_si512(after_bytes, after);
        const uint32_t live = count - block < 64 ? count - block : 64;
        inner += (uint32_t)(int32_t)after_bytes[live - 1];
    }
}

// spike_after_colons over the `{` tokens after a resolved colon only.
static void spike_after_colons_avx512(SpikeKernels* kernels, uint32_t count)
{
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __m512i previous = _mm512_loadu_si512(kernels->roles + block - 1);
        const __m512i kind = _mm512_loadu_si512(kernels->vkinds + block);
        uint64_t lanes = (spike_equal8(previous, SPIKE_ROLE_CLOSE_CASE) | spike_equal8(previous, SPIKE_ROLE_LABEL)) & spike_equal8(kind, SPIKE_KIND_L_BRACE) &
                         (count - block < 64 ? (((uint64_t)1 << (count - block)) - 1) : ~(uint64_t)0);
        // Token 0 has no colon before it.
        lanes &= block == 0 ? ~(uint64_t)1 : ~(uint64_t)0;
        while (lanes != 0)
        {
            const uint32_t i = block + (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            kernels->roles[i] = SPIKE_ROLE_OPEN_BLOCK;
            kernels->flags[i] = 0;
        }
    }
}

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
// state there is no longer a chain of heads (spike_statements_vector then runs K4 again and the twin).

// K5's classes, a bit each, by role. The first four are events only in a statement context (in a block; K2's statement
// contexts), the others always are. HEAD is the `)` of if, while, for and switch, a case's `:` and a label's; the vector
// part turns a while's `)` (K2's statement contexts again) into WHILE, which a do-while's may be.
#define BUSTER_SPIKE_STATEMENT_TERMINATOR 0x01
#define BUSTER_SPIKE_STATEMENT_HEAD 0x02
#define BUSTER_SPIKE_STATEMENT_WHILE 0x04
#define BUSTER_SPIKE_STATEMENT_DO 0x08
#define BUSTER_SPIKE_STATEMENT_ELSE 0x10
#define BUSTER_SPIKE_STATEMENT_OPEN_BLOCK 0x20
#define BUSTER_SPIKE_STATEMENT_OPEN_BODY 0x40
#define BUSTER_SPIKE_STATEMENT_CLOSE 0x80
#define BUSTER_SPIKE_STATEMENT_CONTEXTUAL \
    (BUSTER_SPIKE_STATEMENT_TERMINATOR | BUSTER_SPIKE_STATEMENT_HEAD | BUSTER_SPIKE_STATEMENT_WHILE | BUSTER_SPIKE_STATEMENT_DO)
#define BUSTER_SPIKE_STATEMENT_ALWAYS \
    (BUSTER_SPIKE_STATEMENT_ELSE | BUSTER_SPIKE_STATEMENT_OPEN_BLOCK | BUSTER_SPIKE_STATEMENT_OPEN_BODY | BUSTER_SPIKE_STATEMENT_CLOSE)
// The heads, each of which waits for its level's next event; what takes a waiting head as its parent (a do's owner goes
// to its final `;`); and the ends of statements.
#define BUSTER_SPIKE_STATEMENT_HEADS (BUSTER_SPIKE_STATEMENT_HEAD | BUSTER_SPIKE_STATEMENT_WHILE | BUSTER_SPIKE_STATEMENT_DO | BUSTER_SPIKE_STATEMENT_ELSE)
#define BUSTER_SPIKE_STATEMENT_OWNED (BUSTER_SPIKE_STATEMENT_TERMINATOR | BUSTER_SPIKE_STATEMENT_HEAD | BUSTER_SPIKE_STATEMENT_WHILE)
#define BUSTER_SPIKE_STATEMENT_ENDS (BUSTER_SPIKE_STATEMENT_TERMINATOR | BUSTER_SPIKE_STATEMENT_CLOSE)

static uint8_t spike_statement_classes[128];

static void spike_statement_tables_init(void)
{
    memset(spike_statement_classes, 0, sizeof(spike_statement_classes));
    spike_statement_classes[SPIKE_ROLE_TERMINATOR] = BUSTER_SPIKE_STATEMENT_TERMINATOR;
    spike_statement_classes[SPIKE_ROLE_CLOSE_CONDITION] = BUSTER_SPIKE_STATEMENT_HEAD;
    spike_statement_classes[SPIKE_ROLE_CLOSE_FOR] = BUSTER_SPIKE_STATEMENT_HEAD;
    spike_statement_classes[SPIKE_ROLE_CLOSE_CASE] = BUSTER_SPIKE_STATEMENT_HEAD;
    spike_statement_classes[SPIKE_ROLE_LABEL] = BUSTER_SPIKE_STATEMENT_HEAD;
    spike_statement_classes[SPIKE_ROLE_DO] = BUSTER_SPIKE_STATEMENT_DO;
    spike_statement_classes[SPIKE_ROLE_ELSE] = BUSTER_SPIKE_STATEMENT_ELSE;
    spike_statement_classes[SPIKE_ROLE_OPEN_BLOCK] = BUSTER_SPIKE_STATEMENT_OPEN_BLOCK;
    spike_statement_classes[SPIKE_ROLE_OPEN_BODY] = BUSTER_SPIKE_STATEMENT_OPEN_BODY;
    spike_statement_classes[SPIKE_ROLE_CLOSE_BLOCK] = BUSTER_SPIKE_STATEMENT_CLOSE;
    spike_statement_classes[SPIKE_ROLE_CLOSE_BODY] = BUSTER_SPIKE_STATEMENT_CLOSE;
}

typedef enum SpikeStatementsVerdict
{
    SPIKE_STATEMENTS_OK,
    // The twin's false: an else without an if.
    SPIKE_STATEMENTS_ELSE_WITHOUT_IF,
    // Only the twin's stack decides: a do without its while, or a do-while's `)` not followed by its `;`.
    SPIKE_STATEMENTS_TWIN,
} SpikeStatementsVerdict;

// A level that a block which isn't a statement interrupts: its last event and that event's classes, and the closer
// that ends the interruption.
typedef struct SpikeStatementLevel SpikeStatementLevel;
struct SpikeStatementLevel
{
    uint32_t last;
    uint32_t closer;
    uint8_t last_classes;
};

typedef struct SpikeStatementPass SpikeStatementPass;
struct SpikeStatementPass
{
    // The last event of the current level, and its classes.
    uint32_t last;
    SpikeStatementLevel* levels;
    uint32_t level_count;
    // How many blocks (OPEN_BLOCK and OPEN_BODY) are open, and for each, by depth, the owner of its statement: the head
    // before a statement block's opener, else BUSTER_SPIKE_NONE.
    uint32_t block_depth;
    uint32_t* owners;
    // Where the branch-free stores a lane doesn't make go: the owners' last slot, which no depth reaches.
    uint32_t* sink;
    // The dos whose while hasn't come, and a do-while's `)` whose `;` comes next, with its do.
    uint32_t open_dos;
    uint32_t pending_while;
    uint32_t pending_do;
    // The sparse loop runs a block behind (spike_statement_block): the last block with events, its sparse events, and
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
typedef struct SpikeStatementCounts SpikeStatementCounts;
struct SpikeStatementCounts
{
    uint64_t blocks;
    uint64_t interrupted_blocks;
    uint64_t sparse;
    uint64_t twins;
};

// The events of a block from their classes and statement contexts.
BUSTER_INLINE uint64_t spike_statement_events8(__m512i classes, uint64_t context, __mmask64 live_mask)
{
    const uint64_t contextual = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_CONTEXTUAL)) & context;
    const uint64_t always = _mm512_test_epi8_mask(classes, _mm512_set1_epi8((char)BUSTER_SPIKE_STATEMENT_ALWAYS));
    return (contextual | always) & live_mask;
}

// The first event after `token`, or BUSTER_SPIKE_NONE.
static uint32_t spike_statement_next(const SpikeKernels* kernels, uint32_t count, const SpikeStatementContext* contexts, uint32_t token)
{
    uint32_t block = token - token % 64;
    uint64_t after = token % 64 == 63 ? 0 : ~(uint64_t)0 << (token % 64 + 1);
    uint64_t events = 0;
    bool searching = true;
    while (searching)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const __m512i classes = spike_table8(spike_statement_classes, _mm512_loadu_si512(kernels->roles + block));
        events = spike_statement_events8(classes, contexts[block / 64].blocks, live_mask) & after;
        after = ~(uint64_t)0;
        searching = events == 0 && block + 64 < count;
        block += searching ? 64 : 0;
    }
    return events != 0 ? block + (uint32_t)__builtin_ctzll(events) : BUSTER_SPIKE_NONE;
}

BUSTER_INLINE bool spike_statement_is_head(const SpikeKernels* kernels, uint32_t token)
{
    return token != BUSTER_SPIKE_NONE && (spike_statement_classes[kernels->roles[token]] & BUSTER_SPIKE_STATEMENT_HEADS) != 0;
}

BUSTER_INLINE bool spike_statement_bit(uint64_t mask, uint32_t token)
{
    return ((mask >> (token % 64)) & 1) != 0;
}

// The innermost head of the chain that owned the statement ending at `end`, if `end` is a terminator (the event before
// it) or a block's closer (its owner, which the closer's `previous` holds); it need not be a head, and then the chain
// is empty or `end` ends no statement.
BUSTER_INLINE uint32_t spike_statement_chain(const SpikeKernels* kernels, const uint32_t* previous, uint32_t end)
{
    const uint8_t classes = end != BUSTER_SPIKE_NONE ? spike_statement_classes[kernels->roles[end]] : 0;
    return (classes & BUSTER_SPIKE_STATEMENT_ENDS) != 0 ? previous[end] : BUSTER_SPIKE_NONE;
}

// One event the vector part leaves, in order: an else (its if), a do, a terminator after a head's `)` (that head's,
// unless the head is a do-while's while), or a while's `)` after the end of a statement while a do waits (the do's, if
// the chain down from that statement holds one).
static void spike_statement_sparse(SpikeKernels* kernels, uint32_t count, const SpikeStatementContext* contexts, uint32_t* previous, SpikeStatementPass* state,
                                   uint32_t i)
{
    const uint8_t role = kernels->roles[i];
    if (role == SPIKE_ROLE_ELSE)
    {
        // The first if or do down the chain from the statement that ended before the else: an if is the else's.
        uint32_t head = spike_statement_chain(kernels, previous, previous[i]);
        bool matched = false;
        bool searching = true;
        while (searching)
        {
            const bool is_head = spike_statement_is_head(kernels, head);
            const bool is_do = is_head && kernels->roles[head] == SPIKE_ROLE_DO;
            matched = is_head && spike_statement_bit(contexts[head / 64].ifs, head);
            searching = is_head && !is_do && !matched;
            head = searching ? previous[head] : head;
        }
        if (matched)
        {
            kernels->parents[i] = head;
            // From now on the else stands for its if in the chains.
            previous[i] = previous[head];
        }
        else
        {
            state->else_without_if = true;
        }
    }
    else if (role == SPIKE_ROLE_DO)
    {
        state->open_dos += 1;
    }
    else if (role == SPIKE_ROLE_TERMINATOR)
    {
        const uint32_t head = previous[i];
        if (head == state->pending_while)
        {
            // A do-while's `;`: the parent of the do and of the while's `)`, and owned by what owned the do. In the
            // chains it stands for the do-statement, whose owner is the do's.
            const uint32_t owner = previous[state->pending_do];
            kernels->parents[state->pending_do] = i;
            kernels->parents[head] = i;
            if (spike_statement_is_head(kernels, owner))
            {
                kernels->parents[i] = owner;
            }
            previous[i] = owner;
            state->pending_while = BUSTER_SPIKE_NONE;
        }
        else
        {
            kernels->parents[i] = head;
        }
    }
    else if (state->open_dos != 0)
    {
        // A while after the end of a statement is a do's if the first do down the chain comes before the chain ends.
        uint32_t head = spike_statement_chain(kernels, previous, previous[i]);
        while (spike_statement_is_head(kernels, head) && kernels->roles[head] != SPIKE_ROLE_DO)
        {
            head = previous[head];
        }
        if (spike_statement_is_head(kernels, head))
        {
            // Its `;` must come next.
            const uint32_t next = spike_statement_next(kernels, count, contexts, i);
            state->twin |= next == BUSTER_SPIKE_NONE || kernels->roles[next] != SPIKE_ROLE_TERMINATOR;
            state->pending_while = i;
            state->pending_do = head;
            state->open_dos -= 1;
        }
    }
}

// A block's openers and closers of blocks in order, without a branch on which: an opener puts its statement's owner on
// the stack (for a lane of `owned`, the event before it: before it in `events`, else `carried`, or `positions[lane]`
// in a block that interrupts a level; else BUSTER_SPIKE_NONE), and its closer takes it as its parent, and as its
// `previous`, where the chains find the chain its statement ended. Each one's slot is the depth before the block and
// the openers and closers before it, so the iterations chain only through the stack. The stores a lane doesn't make go
// to `sink`, a slot of a live array that nothing reads, which keeps the compiler from turning them back into branches.
// Returns the depth after the block.
static uint32_t spike_statement_brackets(uint64_t opens, uint64_t closes, uint64_t owned, uint64_t events, uint32_t carried, const uint32_t* positions,
                                         uint32_t block, uint32_t depth, uint32_t* owners, uint32_t* parents, uint32_t* previous, uint32_t* sink)
{
    uint64_t brackets = opens | closes;
    while (brackets != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(brackets);
        brackets &= brackets - 1;
        const uint64_t bit = (uint64_t)1 << lane;
        const uint64_t below = bit - 1;
        const bool open = (opens & bit) != 0;
        const int64_t before = (int64_t)depth + __builtin_popcountll(opens & below) - __builtin_popcountll(closes & below);
        // A closer with nothing open (an unbalanced file, which the fallback takes whole) leaves the stack alone.
        const bool pop = !open && before > 0;
        const int64_t raw = before - (open ? 0 : 1);
        const uint32_t slot = (uint32_t)(raw & ~(raw >> 63));
        const uint32_t popped = owners[slot];
        const uint64_t earlier = events & below;
        const uint32_t last = block + 63 - (uint32_t)__builtin_clzll(earlier | 1);
        const uint32_t previous_event = positions != 0 ? positions[lane] : __builtin_unpredictable(earlier != 0) ? last : carried;
        const uint32_t owner = __builtin_unpredictable((owned & bit) != 0) ? previous_event : BUSTER_SPIKE_NONE;
        uint32_t* push = __builtin_unpredictable(open) ? &owners[slot] : sink;
        *push = owner;
        uint32_t* parent = __builtin_unpredictable(pop && popped != BUSTER_SPIKE_NONE) ? &parents[block + lane] : sink;
        *parent = popped;
        uint32_t* chain = __builtin_unpredictable(open) ? sink : &previous[block + lane];
        *chain = pop ? popped : BUSTER_SPIKE_NONE;
    }
    const int64_t after = (int64_t)depth + __builtin_popcountll(opens) - __builtin_popcountll(closes);
    return after > 0 ? (uint32_t)after : 0;
}

// One block of K5 in AVX-512 with events (see above), whose classes and statement contexts are `classes` and `context`.
static void spike_statement_block(SpikeKernels* kernels, uint32_t count, const SpikeStatementContext* contexts, uint32_t* previous, SpikeStatementPass* state,
                                  SpikeStatementCounts* counts, uint32_t block, __m512i classes, SpikeStatementContext context, uint64_t events)
{
    const uint64_t open_blocks = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_OPEN_BLOCK)) & events;
    const uint64_t opens = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_OPEN_BLOCK | BUSTER_SPIKE_STATEMENT_OPEN_BODY)) & events;
    const uint64_t closes = _mm512_test_epi8_mask(classes, _mm512_set1_epi8((char)BUSTER_SPIKE_STATEMENT_CLOSE)) & events;
    // Whether the block interrupts a level: it holds the opener of a block that isn't a statement inside another block,
    // or the closer of one.
    uint64_t candidates = opens & ~(open_blocks & context.blocks);
    bool interrupting = state->level_count != 0 && state->levels[state->level_count - 1].closer < block + 64;
    while (candidates != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(candidates);
        candidates &= candidates - 1;
        const uint64_t below = ((uint64_t)1 << lane) - 1;
        interrupting |= (int64_t)state->block_depth + __builtin_popcountll(opens & below) - __builtin_popcountll(closes & below) > 0;
    }
    const uint32_t carried = state->last;
    uint32_t positions[64];
    __m512i previous_positions[4];
    __m512i previous_classes;
    if (interrupting)
    {
        // One event at a time, each level's last event kept while a block that isn't a statement interrupts it.
        counts->interrupted_blocks += 1;
        uint8_t class_bytes[64];
        uint8_t previous_bytes[64];
        _mm512_storeu_si512(class_bytes, classes);
        uint32_t depth = state->block_depth;
        uint64_t lanes = events;
        while (lanes != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(lanes);
            lanes &= lanes - 1;
            const uint32_t i = block + lane;
            positions[lane] = state->last;
            previous_bytes[lane] = state->last_classes;
            const SpikeStatementLevel* top = state->level_count != 0 ? &state->levels[state->level_count - 1] : 0;
            const bool open = (class_bytes[lane] & (BUSTER_SPIKE_STATEMENT_OPEN_BLOCK | BUSTER_SPIKE_STATEMENT_OPEN_BODY)) != 0;
            const bool statement = (class_bytes[lane] & BUSTER_SPIKE_STATEMENT_OPEN_BLOCK) != 0 && spike_statement_bit(context.blocks, i);
            if (top != 0 && top->closer == i)
            {
                state->last = top->last;
                state->last_classes = top->last_classes;
                state->level_count -= 1;
            }
            else
            {
                if (open && !statement && depth != 0)
                {
                    // Its closer is the opener's parent (K4).
                    state->levels[state->level_count] = (SpikeStatementLevel){ .last = state->last, .closer = kernels->parents[i], .last_classes = state->last_classes };
                    state->level_count += 1;
                }
                state->last = i;
                state->last_classes = class_bytes[lane];
            }
            depth += open ? 1 : 0;
            depth -= (class_bytes[lane] & BUSTER_SPIKE_STATEMENT_CLOSE) != 0 && depth != 0 ? 1 : 0;
        }
        for (uint32_t q = 0; q < 4; q += 1)
        {
            previous_positions[q] = _mm512_maskz_loadu_epi32((__mmask16)(events >> (16 * q)), positions + 16 * q);
        }
        previous_classes = _mm512_maskz_loadu_epi8(events, previous_bytes);
    }
    else
    {
        // Each event's previous event: the event lanes compressed, shifted up a lane and expanded back; the first one's is
        // the event carried from the blocks before.
        const __m512i dense = _mm512_maskz_compress_epi8(events, spike_shifts.lanes8);
        const __m512i before = _mm512_maskz_expand_epi8(events, _mm512_permutex2var_epi8(dense, spike_shifts.up8[0], _mm512_set1_epi8((char)0xFF)));
        const uint64_t first = events & (0 - events);
        previous_classes = _mm512_mask_mov_epi8(_mm512_permutexvar_epi8(before, classes), first, _mm512_set1_epi8((char)state->last_classes));
        const __m512i block_vector = _mm512_set1_epi32((int)block);
        const __m512i carried_vector = _mm512_set1_epi32((int)carried);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __m128i quarter = q == 0 ? _mm512_extracti32x4_epi32(before, 0) : q == 1 ? _mm512_extracti32x4_epi32(before, 1) :
                                    q == 2 ? _mm512_extracti32x4_epi32(before, 2) : _mm512_extracti32x4_epi32(before, 3);
            previous_positions[q] = _mm512_mask_mov_epi32(_mm512_add_epi32(block_vector, _mm512_cvtepu8_epi32(quarter)), (__mmask16)(first >> (16 * q)), carried_vector);
        }
        const uint32_t last = 63 - (uint32_t)__builtin_clzll(events);
        state->last = block + last;
        state->last_classes = (uint8_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_set1_epi8((char)last), classes)));
    }
    const uint64_t after_head = _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_HEADS)) & events;
    const uint64_t owned_blocks = open_blocks & context.blocks & after_head;
    const uint64_t dos = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_DO)) & events;
    uint64_t sparse = (_mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_ELSE)) & events) | dos;
    uint64_t deferred = 0;
    if (state->open_dos != 0 || state->sparse_dos || dos != 0)
    {
        // While a do may wait for its while: a while's `)` after the end of a statement may be its, and a terminator
        // after a while's `)` its `;`. The sparse loop decides.
        deferred = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_TERMINATOR)) & events &
                   _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_WHILE));
        sparse |= deferred | (_mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_WHILE)) & events &
                              _mm512_test_epi8_mask(previous_classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_ENDS)));
    }
    const uint64_t owned = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_OWNED)) & after_head & ~deferred;
    for (uint32_t q = 0; q < 4; q += 1)
    {
        _mm512_mask_storeu_epi32(kernels->parents + block + 16 * q, (__mmask16)(owned >> (16 * q)), previous_positions[q]);
        _mm512_mask_storeu_epi32(previous + block + 16 * q, (__mmask16)(events >> (16 * q)), previous_positions[q]);
    }
    // The blocks' openers and closers, an opener's owner the event before it.
    state->block_depth = spike_statement_brackets(opens, closes, owned_blocks, events, carried, interrupting ? positions : 0, block, state->block_depth, state->owners,
                                                  kernels->parents, previous, state->sink);
    // The block before's sparse events, then this block's later: a block behind, the loads of `previous` find the
    // stores to it done.
    uint64_t behind = state->sparse;
    while (behind != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(behind);
        behind &= behind - 1;
        spike_statement_sparse(kernels, count, contexts, previous, state, state->sparse_block + lane);
    }
    counts->sparse += (uint64_t)__builtin_popcountll(sparse);
    state->sparse_block = block;
    state->sparse = sparse;
    state->sparse_dos = dos != 0;
}

// K5 in AVX-512 (see above), from each block's statement contexts (K2's), with `previous` a slot per token for each
// event's previous event in its level, `owners` count + 2 slots (one per block open, and the sink), and `levels` room
// for the levels interrupted, one per token at most. Writes the parents spike_statements writes when it returns
// SPIKE_STATEMENTS_OK; when the twin would return false it returns SPIKE_STATEMENTS_ELSE_WITHOUT_IF, and then, as for
// the twin, the parents don't matter: the fallback takes the file.
static SpikeStatementsVerdict spike_statements_avx512(SpikeKernels* kernels, uint32_t count, const SpikeStatementContext* contexts, uint32_t* previous,
                                                      uint32_t* owners, SpikeStatementLevel* levels, SpikeStatementCounts* counts)
{
    SpikeStatementPass state = {
        .last = BUSTER_SPIKE_NONE,
        .levels = levels,
        .level_count = 0,
        .block_depth = 0,
        .owners = owners,
        .sink = owners + count + 1,
        .open_dos = 0,
        .pending_while = BUSTER_SPIKE_NONE,
        .pending_do = BUSTER_SPIKE_NONE,
        .sparse_block = 0,
        .sparse = 0,
        .sparse_dos = false,
        .last_classes = 0,
        .else_without_if = false,
        .twin = false,
    };
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const SpikeStatementContext context = contexts[block / 64];
        const __m512i table_classes = spike_table8(spike_statement_classes, _mm512_loadu_si512(kernels->roles + block));
        const uint64_t events = spike_statement_events8(table_classes, context.blocks, live_mask);
        if (events != 0)
        {
            const uint64_t heads = _mm512_test_epi8_mask(table_classes, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_HEAD));
            const __m512i classes = _mm512_mask_mov_epi8(table_classes, heads & context.whiles, _mm512_set1_epi8(BUSTER_SPIKE_STATEMENT_WHILE));
            counts->blocks += 1;
            spike_statement_block(kernels, count, contexts, previous, &state, counts, block, classes, context, events);
        }
    }
    while (state.sparse != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(state.sparse);
        state.sparse &= state.sparse - 1;
        spike_statement_sparse(kernels, count, contexts, previous, &state, state.sparse_block + lane);
    }
    const bool twin = state.twin || state.open_dos != 0 || state.pending_while != BUSTER_SPIKE_NONE;
    return twin ? SPIKE_STATEMENTS_TWIN : state.else_without_if ? SPIKE_STATEMENTS_ELSE_WITHOUT_IF : SPIKE_STATEMENTS_OK;
}

// ---------------------------------------------------------------------------------------------------------------------
// K1 in 16-bit lanes, for the comparison with byte lanes

// K1 in AVX-512 with 32 tokens a block in 16-bit lanes: the same as spike_brackets_avx512, but a block's depth can't drop
// more than 31 below its start, so the 32 open openers below it (two registers for vpermi2d) always hold the enclosing
// opener of a token whose opener is before the block.
static bool spike_brackets16_avx512(const uint8_t* codes, const int8_t* deltas, uint32_t count, uint32_t* depths, uint32_t* enclosing, uint32_t* open_stack)
{
    const __m512i delta_low = _mm512_loadu_si512(deltas);
    const __m512i delta_high = _mm512_loadu_si512(deltas + 64);
    const __m512i bias = _mm512_set1_epi16(64);
    const __m512i fill = _mm512_set1_epi16(0x7FFF);
    int64_t base = 0;
    bool balanced = true;
    for (uint32_t block = 0; block < count; block += 32)
    {
        const __m512i code = _mm512_zextsi256_si512(_mm256_loadu_si256((const __m256i*)(codes + block)));
        const __m512i delta = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(_mm512_permutex2var_epi8(delta_low, code, delta_high)));
        const __m512i after = spike_prefix_sum16(delta);
        const __m512i before = _mm512_sub_epi16(after, delta);
        const __m512i keys = _mm512_add_epi16(before, bias);
        const __m512i p = spike_previous_smaller16(keys, keys);
        const __mmask32 found = _mm512_cmplt_epu16_mask(p, _mm512_set1_epi16(32));
        const uint32_t live = count - block < 32 ? count - block : 32;
        const __mmask32 live_mask = live == 32 ? ~(__mmask32)0 : (((__mmask32)1 << live) - 1);
        // The open openers at the 32 depths below the block's start: index 31 + the depth before, relative to the start.
        const __m512i below_low = _mm512_loadu_si512(open_stack + base - 32);
        const __m512i below_high = _mm512_loadu_si512(open_stack + base - 16);
        const __m512i base_vector = _mm512_set1_epi32((int)base);
        const __m512i block_vector = _mm512_set1_epi32((int)block);
        for (uint32_t half = 0; half < 2; half += 1)
        {
            const __m256i p_half = half == 0 ? _mm512_castsi512_si256(p) : _mm512_extracti64x4_epi64(p, 1);
            const __m256i before_half = half == 0 ? _mm512_castsi512_si256(before) : _mm512_extracti64x4_epi64(before, 1);
            const __m512i relative = _mm512_cvtepi16_epi32(before_half);
            const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu16_epi32(p_half));
            const __m512i outside = _mm512_permutex2var_epi32(below_low, _mm512_add_epi32(relative, _mm512_set1_epi32(31)), below_high);
            _mm512_storeu_si512(enclosing + block + 16 * half, _mm512_mask_blend_epi32((__mmask16)(found >> (16 * half)), outside, inside));
            _mm512_storeu_si512(depths + block + 16 * half, _mm512_add_epi32(base_vector, relative));
        }
        __m512i suffix = after;
        for (uint32_t k = 0; k < 5; k += 1)
        {
            suffix = _mm512_min_epi16(suffix, spike_down16(suffix, k, fill));
        }
        uint32_t open = _mm512_cmpgt_epi16_mask(delta, _mm512_setzero_si512()) & live_mask & _mm512_cmpeq_epi16_mask(suffix, after);
        if (open != 0)
        {
            int16_t before_lanes[32];
            _mm512_storeu_si512(before_lanes, before);
            while (open != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(open);
                open &= open - 1;
                open_stack[base + before_lanes[lane]] = block + lane;
            }
        }
        int16_t after_lanes[32];
        _mm512_storeu_si512(after_lanes, after);
        const __mmask32 negative = _mm512_cmplt_epi16_mask(_mm512_add_epi16(after, _mm512_set1_epi16((short)(base < 64 ? base : 64))), _mm512_setzero_si512()) & live_mask;
        balanced &= base >= 64 || negative == 0;
        base += after_lanes[live - 1];
        base = base < 0 ? 0 : base;
    }
    return balanced && base == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// K4 in byte lanes
//
// 64 tokens a block, keys (node depth - the block's lowest) * 32 + level in bytes: a block whose node depths span at most
// BUSTER_SPIKE_BYTE_DEPTH_RANGE fits (an opener's right threshold is one depth further up). Wider blocks run as two
// 16-bit blocks. The candidate stacks keep 16-bit keys, so both kinds of block read and write them alike.

#define BUSTER_SPIKE_BYTE_DEPTH_RANGE 6

// The nearest next lane whose key is below the lane's threshold, in byte lanes: the lane index, or 64 or more if none.
BUSTER_INLINE __m512i spike_next_smaller8(__m512i keys, __m512i thresholds)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    const __m512i one = _mm512_set1_epi8(1);
    __m512i windows[6];
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], spike_down8(windows[k - 1], k - 1, fill));
    }
    __m512i p = _mm512_add_epi8(spike_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        // p stays below 128: it starts at 64 or less and grows by at most 63.
        p = _mm512_add_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// A block's node depths relative to `base`, as bytes, and the block's lowest and highest node depths.
BUSTER_INLINE __m512i spike_relative_depths8(const uint32_t* node_depths, uint32_t block, __mmask64 live_mask, uint32_t* lowest, uint32_t* highest)
{
    __m512i low = _mm512_set1_epi32(-1);
    __m512i high = _mm512_setzero_si512();
    __m512i depth[4];
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __mmask16 live = (__mmask16)(live_mask >> (16 * q));
        depth[q] = _mm512_loadu_si512(node_depths + block + 16 * q);
        low = _mm512_mask_min_epu32(low, live, low, depth[q]);
        high = _mm512_mask_max_epu32(high, live, high, depth[q]);
    }
    *lowest = _mm512_reduce_min_epu32(low);
    *highest = _mm512_reduce_max_epu32(high);
    const __m512i base = _mm512_set1_epi32((int)*lowest);
    __m512i result = _mm512_setzero_si512();
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[0], base)), 0);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[1], base)), 1);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[2], base)), 2);
    result = _mm512_inserti32x4(result, _mm512_cvtepi32_epi8(_mm512_sub_epi32(depth[3], base)), 3);
    return result;
}

// The window of the stack's top 32 candidates with keys relative to base * 32, saturated to a byte (a candidate below
// the block's depth is below every key in it, one above it is above every one), searched for each lane's threshold.
typedef struct SpikeWindow8 SpikeWindow8;
struct SpikeWindow8
{
    __m512i index;
    __m512i keys;
    __m512i positions_low;
    __m512i positions_high;
    __mmask64 valid;
    __mmask64 deep;
};

BUSTER_INLINE SpikeWindow8 spike_search_window8(const SpikeCandidates* stack, uint32_t base, __m512i thresholds)
{
    const uint32_t count = stack->count;
    const uint32_t start = count >= 32 ? count - 32 : 0;
    const uint32_t missing = 32 - (count - start);
    SpikeWindow8 result;
    result.keys = _mm512_loadu_si512(stack->keys + (int64_t)count - 32);
    result.positions_low = _mm512_loadu_si512(stack->positions + (int64_t)count - 32);
    result.positions_high = _mm512_loadu_si512(stack->positions + (int64_t)count - 16);
    const __m512i relative = _mm512_subs_epu16(result.keys, _mm512_set1_epi16((short)(base * BUSTER_SPIKE_LEVELS)));
    const __m512i keys8 = _mm512_castsi256_si512(_mm512_cvtusepi16_epi8(relative));
    static const char steps[6] = { 16, 8, 4, 2, 1, 1 };
    __m512i j = _mm512_set1_epi8(-1);
    for (uint32_t s = 0; s < 6; s += 1)
    {
        const __m512i candidate = _mm512_add_epi8(j, _mm512_set1_epi8(steps[s]));
        const __m512i v = _mm512_permutexvar_epi8(candidate, keys8);
        j = _mm512_mask_mov_epi8(j, _mm512_cmplt_epu8_mask(v, thresholds), candidate);
    }
    result.index = j;
    const __mmask64 any = _mm512_cmpge_epi8_mask(j, _mm512_setzero_si512());
    result.valid = _mm512_mask_cmpge_epi8_mask(any, j, _mm512_set1_epi8((char)missing));
    result.deep = start != 0 ? ~any : 0;
    return result;
}

// The positions and 16-bit keys of 64 lanes' candidates, in-block lanes `p` or window lanes `j`, by quarter and half.
BUSTER_INLINE void spike_candidates8(__m512i p, __mmask64 found, const SpikeWindow8* window, __m512i keys8, uint32_t block, uint32_t base, uint32_t* positions,
                                     uint16_t* keys)
{
    const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
    const __m512i block_vector = _mm512_set1_epi32((int)block);
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __m128i p_quarter = q == 0 ? _mm512_extracti32x4_epi32(p, 0) : q == 1 ? _mm512_extracti32x4_epi32(p, 1) :
                                  q == 2 ? _mm512_extracti32x4_epi32(p, 2) : _mm512_extracti32x4_epi32(p, 3);
        const __m128i j_quarter = q == 0 ? _mm512_extracti32x4_epi32(window->index, 0) : q == 1 ? _mm512_extracti32x4_epi32(window->index, 1) :
                                  q == 2 ? _mm512_extracti32x4_epi32(window->index, 2) : _mm512_extracti32x4_epi32(window->index, 3);
        const __m512i inside = _mm512_add_epi32(block_vector, _mm512_cvtepu8_epi32(p_quarter));
        const __m512i outside = _mm512_mask_mov_epi32(none, (__mmask16)(window->valid >> (16 * q)),
                                                      _mm512_permutex2var_epi32(window->positions_low, _mm512_cvtepu8_epi32(j_quarter), window->positions_high));
        _mm512_storeu_si512(positions + 16 * q, _mm512_mask_mov_epi32(outside, (__mmask16)(found >> (16 * q)), inside));
    }
    const __m512i inside_keys8 = _mm512_permutexvar_epi8(p, keys8);
    const __m512i offset = _mm512_set1_epi16((short)(base * BUSTER_SPIKE_LEVELS));
    for (uint32_t half = 0; half < 2; half += 1)
    {
        const __m256i key_half = half == 0 ? _mm512_castsi512_si256(inside_keys8) : _mm512_extracti64x4_epi64(inside_keys8, 1);
        const __m256i j_half = half == 0 ? _mm512_castsi512_si256(window->index) : _mm512_extracti64x4_epi64(window->index, 1);
        const __m512i inside = _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(key_half));
        const __m512i outside = _mm512_maskz_permutexvar_epi16((__mmask32)(window->valid >> (32 * half)), _mm512_cvtepu8_epi16(j_half), window->keys);
        _mm512_storeu_si512(keys + 32 * half, _mm512_mask_mov_epi16(outside, (__mmask32)(found >> (32 * half)), inside));
    }
}

// The block's keys and thresholds in bytes.
typedef struct SpikeKeys8 SpikeKeys8;
struct SpikeKeys8
{
    __m512i keys;
    __m512i left;
    __m512i right;
    __m512i rules;
};

BUSTER_INLINE SpikeKeys8 spike_block_keys8(const uint8_t* roles, __m512i relative, uint32_t block)
{
    const __m512i role = _mm512_loadu_si512(roles + block);
    // relative < 8, so a 16-bit shift by 5 moves each byte within itself.
    const __m512i base = _mm512_slli_epi16(relative, 5);
    SpikeKeys8 result = {
        .keys = _mm512_add_epi8(base, spike_lookup8(spike_parent_tables.levels, role)),
        .left = _mm512_add_epi8(base, spike_lookup8(spike_parent_tables.left_thresholds, role)),
        .right = _mm512_add_epi8(base, spike_lookup8(spike_parent_tables.right_thresholds, role)),
        .rules = spike_lookup8(spike_parent_tables.rules, role),
    };
    return result;
}

// One 64-token block of K4's left pass in byte lanes.
static void spike_parents_left8_block(const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block, __mmask64 live_mask, uint32_t* lefts, uint16_t* left_keys,
                                      SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi8((char)0xFF);
    const SpikeKeys8 keys = spike_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(ones, live_mask, keys.keys);
    const __m512i p = spike_previous_smaller8(block_keys, keys.left);
    const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
    const SpikeWindow8 window = spike_search_window8(stack, base, keys.left);
    spike_candidates8(p, found, &window, block_keys, block, base, lefts + block, left_keys + block);
    uint64_t deep = window.deep & ~found & live_mask;
    if (deep != 0)
    {
        uint8_t thresholds[64];
        _mm512_storeu_si512(thresholds, keys.left);
        while (deep != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(deep);
            deep &= deep - 1;
            lefts[block + lane] = spike_search_stack(stack, base * BUSTER_SPIKE_LEVELS + thresholds[lane], &left_keys[block + lane]);
        }
    }
    __m512i suffix = block_keys;
    for (uint32_t k = 0; k < 6; k += 1)
    {
        suffix = _mm512_min_epu8(suffix, spike_down8(suffix, k, ones));
    }
    const uint16_t minimum = (uint16_t)(base * BUSTER_SPIKE_LEVELS + (uint32_t)_mm_extract_epi8(_mm512_castsi512_si128(suffix), 0));
    spike_pop_hidden(stack, minimum);
    const __mmask64 minima = _mm512_mask_cmplt_epu8_mask(live_mask, block_keys, spike_down8(suffix, 0, ones));
    const __m512i compressed = _mm512_maskz_compress_epi8(minima, block_keys);
    const __m512i offset = _mm512_set1_epi16((short)(base * BUSTER_SPIKE_LEVELS));
    _mm512_storeu_si512(stack->keys + stack->count, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(compressed))));
    _mm512_storeu_si512(stack->keys + stack->count + 32, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(compressed, 1))));
    uint32_t written = stack->count;
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __mmask16 part = (__mmask16)(minima >> (16 * q));
        const __m512i lanes = _mm512_add_epi32(_mm512_set1_epi32((int)(block + 16 * q)), _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16)));
        _mm512_storeu_si512(stack->positions + written, _mm512_maskz_compress_epi32(part, lanes));
        written += (uint32_t)__builtin_popcount(part);
    }
    stack->count = written;
}

// K4's left pass in byte lanes: 64-token blocks, and the 16-bit blocks for those whose depths span too much.
static void spike_parents_left8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, SpikeCandidates* stack,
                                       uint64_t* wide_blocks)
{
    stack->count = 0;
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        uint32_t lowest;
        uint32_t highest;
        const __m512i relative = spike_relative_depths8(node_depths, block, live_mask, &lowest, &highest);
        if (highest - lowest <= BUSTER_SPIKE_BYTE_DEPTH_RANGE)
        {
            spike_parents_left8_block(roles, relative, lowest, block, live_mask, lefts, left_keys, stack);
        }
        else
        {
            *wide_blocks += 1;
            spike_parents_left16_block(roles, node_depths, block, live < 32 ? live : 32, lefts, left_keys, stack);
            if (live > 32)
            {
                spike_parents_left16_block(roles, node_depths, block + 32, live - 32, lefts, left_keys, stack);
            }
        }
    }
}

// One 64-token block of K4's right pass in byte lanes, with the parents.
static void spike_parents_right8_block(const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block, __mmask64 live_mask, const uint32_t* lefts,
                                       const uint16_t* left_keys, uint32_t* parents, SpikeCandidates* stack)
{
    const __m512i ones = _mm512_set1_epi8((char)0xFF);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_SPIKE_NONE);
    const __m512i weak_table = _mm512_loadu_si512(spike_parent_tables.weak);
    const __m512i ties_table = _mm512_loadu_si512(spike_parent_tables.ties);
    const SpikeKeys8 keys = spike_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(ones, live_mask, keys.keys);
    const __m512i p = spike_next_smaller8(block_keys, keys.right);
    const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
    const SpikeWindow8 window = spike_search_window8(stack, base, keys.right);
    uint32_t rights[64];
    uint16_t right_keys[64];
    spike_candidates8(p, found, &window, block_keys, block, base, rights, right_keys);
    uint64_t deep = window.deep & ~found & live_mask;
    if (deep != 0)
    {
        uint8_t thresholds[64];
        _mm512_storeu_si512(thresholds, keys.right);
        while (deep != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(deep);
            deep &= deep - 1;
            rights[lane] = spike_search_stack(stack, base * BUSTER_SPIKE_LEVELS + thresholds[lane], &right_keys[lane]);
        }
    }
    // The parents, 32 lanes at a time with 16-bit keys.
    const __mmask64 rule_left = spike_equal8(keys.rules, SPIKE_RULE_LEFT);
    const __mmask64 rule_right = _mm512_cmpge_epu8_mask(keys.rules, _mm512_set1_epi8(SPIKE_RULE_RIGHT));
    for (uint32_t half = 0; half < 2; half += 1)
    {
        const __m512i left_key = _mm512_loadu_si512(left_keys + block + 32 * half);
        const __m512i right_key = _mm512_loadu_si512(right_keys + 32 * half);
        const __m512i left_level = _mm512_and_si512(left_key, _mm512_set1_epi16(31));
        const __mmask32 weak = _mm512_test_epi16_mask(_mm512_permutexvar_epi16(left_level, weak_table), _mm512_set1_epi16(-1));
        const __mmask32 ties = _mm512_test_epi16_mask(_mm512_permutexvar_epi16(left_level, ties_table), _mm512_set1_epi16(-1));
        const __mmask32 greater = _mm512_cmpgt_epu16_mask(left_key, right_key);
        const __mmask32 equal = _mm512_cmpeq_epi16_mask(left_key, right_key);
        __mmask32 left_valid = 0;
        __mmask32 right_valid = 0;
        __m512i left[2];
        __m512i right[2];
        for (uint32_t q = 0; q < 2; q += 1)
        {
            left[q] = _mm512_loadu_si512(lefts + block + 32 * half + 16 * q);
            right[q] = _mm512_loadu_si512(rights + 32 * half + 16 * q);
            left_valid |= (__mmask32)_mm512_cmpneq_epi32_mask(left[q], none) << (16 * q);
            right_valid |= (__mmask32)_mm512_cmpneq_epi32_mask(right[q], none) << (16 * q);
        }
        const __mmask32 select_left = left_valid & (~right_valid | (~weak & (greater | (equal & ties))));
        const __mmask32 take_left = (__mmask32)(rule_left >> (32 * half)) | (~(__mmask32)(rule_right >> (32 * half)) & select_left);
        for (uint32_t q = 0; q < 2; q += 1)
        {
            _mm512_storeu_si512(parents + block + 32 * half + 16 * q, _mm512_mask_mov_epi32(right[q], (__mmask16)(take_left >> (16 * q)), left[q]));
        }
    }
    // The prefix minima onto the stack, nearest on top.
    __m512i prefix = block_keys;
    for (uint32_t k = 0; k < 6; k += 1)
    {
        prefix = _mm512_min_epu8(prefix, spike_up8(prefix, k, ones));
    }
    const uint16_t minimum = (uint16_t)(base * BUSTER_SPIKE_LEVELS + (uint32_t)_mm_extract_epi8(_mm512_extracti32x4_epi32(prefix, 3), 15));
    spike_pop_hidden(stack, minimum);
    const __mmask64 minima = _mm512_mask_cmplt_epu8_mask(live_mask, block_keys, spike_up8(prefix, 0, ones));
    const uint32_t n = (uint32_t)__builtin_popcountll(minima);
    const __m512i reverse = _mm512_sub_epi8(_mm512_set1_epi8((char)(n - 1)), spike_shifts.lanes8);
    const __m512i reversed = _mm512_permutexvar_epi8(reverse, _mm512_maskz_compress_epi8(minima, block_keys));
    const __m512i offset = _mm512_set1_epi16((short)(base * BUSTER_SPIKE_LEVELS));
    _mm512_storeu_si512(stack->keys + stack->count, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(reversed))));
    _mm512_storeu_si512(stack->keys + stack->count + 32, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(reversed, 1))));
    uint32_t positions[64];
    uint32_t written = 0;
    for (uint32_t q = 0; q < 4; q += 1)
    {
        const __mmask16 part = (__mmask16)(minima >> (16 * q));
        const __m512i lanes = _mm512_add_epi32(_mm512_set1_epi32((int)(block + 16 * q)), _mm512_cvtepu16_epi32(_mm512_castsi512_si256(spike_shifts.lanes16)));
        _mm512_storeu_si512(positions + written, _mm512_maskz_compress_epi32(part, lanes));
        written += (uint32_t)__builtin_popcount(part);
    }
    if (n <= 32)
    {
        spike_push_reversed(stack, positions, n);
    }
    else
    {
        for (uint32_t i = 0; i < n; i += 1)
        {
            stack->positions[stack->count + i] = positions[n - 1 - i];
        }
    }
    stack->count += n;
}

static void spike_parents_right8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents,
                                        SpikeCandidates* stack)
{
    stack->count = 0;
    for (uint32_t b = (count + 63) / 64; b-- > 0;)
    {
        const uint32_t block = b * 64;
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        uint32_t lowest;
        uint32_t highest;
        const __m512i relative = spike_relative_depths8(node_depths, block, live_mask, &lowest, &highest);
        if (highest - lowest <= BUSTER_SPIKE_BYTE_DEPTH_RANGE)
        {
            spike_parents_right8_block(roles, relative, lowest, block, live_mask, lefts, left_keys, parents, stack);
        }
        else
        {
            if (live > 32)
            {
                spike_parents_right16_block(roles, node_depths, block + 32, live - 32, lefts, left_keys, parents, stack);
            }
            spike_parents_right16_block(roles, node_depths, block, live < 32 ? live : 32, lefts, left_keys, parents, stack);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// K4 with the stack's top in registers
//
// In the byte-lane passes above, a block reads the top of the candidate stack from memory that the block before it has
// just written at another offset, so the load can't take the stores' data and waits for them to commit; and a block's
// in-block work, about 140 cycles of latency in some 450 ops, hardly overlaps the next block's. Not for want of reorder
// buffer: Zen 5 keeps waiting vector ops in its floating-point scheduling queues, which one block's chains fill (half the
// cycles of a loop of that work alone stall on them: de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall), so
// the next block's ops reach them only when they come close in program order. So K4 runs as three loops:
//   - spike_top_prepare8_avx512: every block's in-block work for both passes, which depends on no other block (the
//     searches by spike_previous_smaller8's and spike_next_smaller8's tables, both passes' candidates for other blocks,
//     the least key), BUSTER_SPIKE_PREPARED_TOGETHER blocks at a time, each step for all of them before the next.
//   - The left pass and the right pass: each block's window work on it. The top of the stack lives in registers
//     (SpikeTop): its BUSTER_SPIKE_TOP_LANES nearest candidates, nearest first. A block looks there for the candidates
//     its lanes didn't find in it, pops the ones its least key hides with a compress and pushes its own with an expand,
//     so that what one block hands the next is a compare, a compress and an expand on registers.
//
// The whole stack is in memory too (SpikeSlots), nearest first and growing downward, written by each push with a
// masked store. A lane whose candidate is deeper than the window searches it there, and a window that pops leave with
// fewer candidates than the stack holds is reloaded from it. Neither happens on sqlite3 or the libc corpus, whose
// stacks hold at most 32 candidates before a block; deep nesting does both.
//
// The left pass's candidates for later blocks are a block's suffix minima, the right pass's its prefix minima, each
// pushed nearest first. A block whose depths span more than BUSTER_SPIKE_BYTE_DEPTH_RANGE runs as aligned parts that
// fit, in its pass's order, each with its own in-block work.

#define BUSTER_SPIKE_TOP_LANES 32

typedef struct SpikeTop SpikeTop;
struct SpikeTop
{
    // The nearest candidates, nearest first: 16-bit keys (0 below the stack's bottom, below every real key) and the
    // positions' low and high halves (0xFFFF below the bottom, which reads as BUSTER_SPIKE_NONE).
    __m512i keys;
    __m512i low;
    __m512i high;
    // The candidates in the stack, and how many of them the window holds: the lesser of `count` and the window's
    // lanes, between blocks.
    uint32_t count;
    uint32_t held;
};

// The stack in memory, nearest first: candidate s (0 at the bottom) at slot capacity - 1 - s, so the window's
// candidates are the slots from capacity - count. BUSTER_SPIKE_TOP_LANES slots after the last hold key 0 and position
// halves 0xFFFF, as the window does below the bottom.
typedef struct SpikeSlots SpikeSlots;
struct SpikeSlots
{
    uint16_t* keys;
    uint16_t* low;
    uint16_t* high;
    uint32_t capacity;
};

BUSTER_INLINE void spike_top_reset(SpikeTop* top)
{
    top->keys = _mm512_setzero_si512();
    top->low = _mm512_set1_epi16(-1);
    top->high = _mm512_set1_epi16(-1);
    top->count = 0;
    top->held = 0;
}

// The window's first `reach` candidates from memory (the rest as below the bottom), for a stack that holds `count`.
BUSTER_INLINE void spike_top_reload(SpikeTop* top, const SpikeSlots* slots, uint32_t reach)
{
    const __mmask32 lanes = reach == BUSTER_SPIKE_TOP_LANES ? ~(__mmask32)0 : (((__mmask32)1 << reach) - 1);
    const uint32_t slot = slots->capacity - top->count;
    top->keys = _mm512_maskz_loadu_epi16(lanes, slots->keys + slot);
    top->low = _mm512_mask_loadu_epi16(_mm512_set1_epi16(-1), lanes, slots->low + slot);
    top->high = _mm512_mask_loadu_epi16(_mm512_set1_epi16(-1), lanes, slots->high + slot);
    top->held = top->count < reach ? top->count : reach;
}

// How many of the `below` candidates under the window a minimum hides: those whose key isn't below it, a run from the
// nearest, counted 32 at a time. The padding's key 0 ends the run.
static uint32_t spike_slots_hidden(const SpikeSlots* slots, uint32_t below, uint16_t minimum)
{
    uint32_t hidden = 0;
    bool popping = true;
    while (popping)
    {
        const __m512i keys = _mm512_loadu_si512(slots->keys + slots->capacity - below + hidden);
        const uint32_t run = (uint32_t)__builtin_popcount(_mm512_cmpge_epu16_mask(keys, _mm512_set1_epi16((short)minimum)));
        hidden += run;
        popping = run == 32 && hidden < below;
    }
    return hidden < below ? hidden : below;
}

// The nearest of the stack's candidates below `skip`, the window's, whose key is below `threshold`: a binary search, as
// keys decrease from the nearest. Its position, or BUSTER_SPIKE_NONE with key 0.
static uint32_t spike_slots_search(const SpikeSlots* slots, uint32_t count, uint32_t skip, uint32_t threshold, uint16_t* key)
{
    uint32_t low = slots->capacity - count + skip;
    uint32_t high = slots->capacity;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (slots->keys[middle] < threshold)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    const bool found = low < slots->capacity;
    *key = found ? slots->keys[low] : 0;
    return found ? ((uint32_t)slots->high[low] << 16) | slots->low[low] : BUSTER_SPIKE_NONE;
}

// The nearest window candidate below each lane's threshold: the first of the window's 32 keys (relative to the block,
// saturated to bytes, nearest first, so non-increasing; lanes 32 to 63 zero) below it, 0 to 31, or 32 if none is. A
// binary search for how many lead it at or above the threshold: steps of 16, 8, 4, 2, 1 and 1.
BUSTER_INLINE __m512i spike_top_search8(__m512i window, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i c = _mm512_setzero_si512();
    for (int32_t k = 4; k >= -1; k -= 1)
    {
        const uint32_t step = k >= 0 ? 1u << k : 1u;
        const __m512i v = _mm512_permutexvar_epi8(_mm512_add_epi8(c, _mm512_set1_epi8((char)(step - 1))), window);
        // 0xFF where v is at or above the threshold, so the search moves past it.
        const __m512i past = _mm512_sub_epi8(_mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one), one);
        c = _mm512_add_epi8(c, _mm512_and_si512(past, _mm512_set1_epi8((char)step)));
    }
    return c;
}

// The lanes' candidates from memory for the lanes in `deep`, whose candidate is under the window (rare: no lane of
// sqlite3 or the libc corpus has one), into a block's candidates as 16-bit keys and 32-bit positions.
static void spike_top_deep(const SpikeSlots* slots, uint32_t count, uint32_t reach, __mmask64 deep, __m512i thresholds, uint32_t base, uint32_t* positions,
                           uint16_t* keys)
{
    uint8_t threshold_bytes[64];
    _mm512_storeu_si512(threshold_bytes, thresholds);
    while (deep != 0)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(deep);
        deep &= deep - 1;
        positions[lane] = spike_slots_search(slots, count, reach, base * BUSTER_SPIKE_LEVELS + threshold_bytes[lane], &keys[lane]);
    }
}

// A block's candidates, each lane's nearest below its threshold, as 16-bit keys (32 lanes a register) and positions
// (16 lanes a register).
typedef struct SpikeFound8 SpikeFound8;
struct SpikeFound8
{
    __m512i keys[2];
    __m512i positions[4];
};

// One block's (or part's) window work, after its in-block search: the lanes' candidates (in the block where p is below
// 64, else in the window, else in memory under it), then the pop and the push. `block_keys` are its keys relative to
// base * 32 (dead lanes 0xFF), `p` and `thresholds` its search's, and `push_keys` and `push_lanes` the keys and block
// lanes from which `minima` picks its candidates for later blocks, nearest first; `minimum` holds the block's least key
// in every lane. `reach` is the window's lanes: BUSTER_SPIKE_TOP_LANES, or fewer in the check of the paths through
// memory.
BUSTER_INLINE SpikeFound8 spike_top_block8(SpikeTop* top, const SpikeSlots* slots, uint32_t reach, __m512i block_keys, __m512i thresholds, __m512i p,
                                           __m512i push_keys, __m512i push_lanes, __mmask64 minima, __m512i minimum, uint32_t block, uint32_t base, __mmask64 live)
{
    const __m512i ones = _mm512_set1_epi16(-1);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i offset = _mm512_set1_epi16((short)(base * BUSTER_SPIKE_LEVELS));
    // The window's keys relative to the block, saturated: a key below the block's lowest depth is below every key in it,
    // and one too far above it above every threshold.
    const __m512i window = _mm512_zextsi256_si512(_mm512_cvtusepi16_epi8(_mm512_subs_epu16(top->keys, offset)));
    const __m512i c = spike_top_search8(window, thresholds);
    const __mmask64 found = _mm512_cmplt_epu8_mask(p, _mm512_set1_epi8(64));
    SpikeFound8 result;
    // Keys: the block's from its key bytes, the window's from its keys (index 32 picks the second table's 0).
    const __m512i inside_keys = _mm512_permutexvar_epi8(p, block_keys);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i inside = _mm512_add_epi16(offset, _mm512_maskz_permutexvar_epi8(BUSTER_SPIKE_WORD_BYTES, spike_shifts.words[h], inside_keys));
        const __m512i outside = _mm512_permutex2var_epi16(top->keys, _mm512_maskz_permutexvar_epi8(BUSTER_SPIKE_WORD_BYTES, spike_shifts.words[h], c), zero);
        result.keys[h] = _mm512_mask_mov_epi16(outside, (__mmask32)(found >> (32 * h)), inside);
    }
    // Positions: the block's are block + lane; the window's from their halves, put together as 32-bit lanes by unpacking
    // in an order that leaves them in lane order.
    const __m512i block_vector = _mm512_set1_epi32((int)block);
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i index = _mm512_maskz_permutexvar_epi8(BUSTER_SPIKE_WORD_BYTES, spike_shifts.unpacked[h], c);
        const __m512i low = _mm512_permutex2var_epi16(top->low, index, ones);
        const __m512i high = _mm512_permutex2var_epi16(top->high, index, ones);
        const __m512i outside[2] = {
            _mm512_unpacklo_epi16(low, high),
            _mm512_unpackhi_epi16(low, high),
        };
        for (uint32_t q = 0; q < 2; q += 1)
        {
            const __m512i inside = _mm512_add_epi32(block_vector, _mm512_maskz_permutexvar_epi8(BUSTER_SPIKE_DWORD_BYTES, spike_shifts.dwords[2 * h + q], p));
            result.positions[2 * h + q] = _mm512_mask_mov_epi32(outside[q], (__mmask16)(found >> (32 * h + 16 * q)), inside);
        }
    }
    // Lanes whose candidate is under the window: before the push, which may write over the slots they read.
    const __mmask64 deep = top->count > reach ? _mm512_mask_cmpge_epu8_mask(live & ~found, c, _mm512_set1_epi8((char)reach)) : 0;
    if (deep != 0)
    {
        uint32_t position_lanes[64];
        uint16_t key_lanes[64];
        for (uint32_t q = 0; q < 4; q += 1)
        {
            _mm512_storeu_si512(position_lanes + 16 * q, result.positions[q]);
        }
        _mm512_storeu_si512(key_lanes, result.keys[0]);
        _mm512_storeu_si512(key_lanes + 32, result.keys[1]);
        spike_top_deep(slots, top->count, reach, deep, thresholds, base, position_lanes, key_lanes);
        for (uint32_t q = 0; q < 4; q += 1)
        {
            result.positions[q] = _mm512_loadu_si512(position_lanes + 16 * q);
        }
        result.keys[0] = _mm512_loadu_si512(key_lanes);
        result.keys[1] = _mm512_loadu_si512(key_lanes + 32);
    }
    // The pop: the window's candidates whose key isn't below the block's minimum are hidden, nearest first; the padding's
    // key 0 is below every real key.
    const __mmask32 kept = _mm256_cmplt_epu8_mask(_mm512_castsi512_si256(window), _mm512_castsi512_si256(minimum));
    const uint32_t hidden = (uint32_t)__builtin_popcount(~kept);
    uint32_t popped = hidden;
    if (hidden == top->held && top->count > top->held)
    {
        // The window emptied: the candidates under it may be hidden too.
        const uint16_t least = (uint16_t)(base * BUSTER_SPIKE_LEVELS + (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(minimum)) % 256);
        popped += spike_slots_hidden(slots, top->count - top->held, least);
    }
    // The push: the block's candidates, nearest first, into memory and onto the window.
    const uint32_t n = (uint32_t)__builtin_popcountll(minima);
    const __m512i new_keys = _mm512_maskz_compress_epi8(minima, push_keys);
    const __m512i new_lanes = _mm512_maskz_compress_epi8(minima, push_lanes);
    const __m512i keys = _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(new_keys)));
    const __m512i low = _mm512_add_epi16(_mm512_set1_epi16((short)(block & 0xFFFF)), _mm512_cvtepu8_epi16(_mm512_castsi512_si256(new_lanes)));
    const __m512i high = _mm512_set1_epi16((short)(block >> 16));
    const uint32_t count = top->count - popped + n;
    const uint32_t slot = slots->capacity - count;
    const __mmask32 stored = n >= 32 ? ~(__mmask32)0 : (((__mmask32)1 << n) - 1);
    _mm512_mask_storeu_epi16(slots->keys + slot, stored, keys);
    _mm512_mask_storeu_epi16(slots->low + slot, stored, low);
    _mm512_mask_storeu_epi16(slots->high + slot, stored, high);
    if (n > 32)
    {
        const __mmask32 rest = ((__mmask32)1 << (n - 32)) - 1;
        _mm512_mask_storeu_epi16(slots->keys + slot + 32, rest, _mm512_add_epi16(offset, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(new_keys, 1))));
        _mm512_mask_storeu_epi16(slots->low + slot + 32, rest,
                                 _mm512_add_epi16(_mm512_set1_epi16((short)(block & 0xFFFF)), _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(new_lanes, 1))));
        _mm512_mask_storeu_epi16(slots->high + slot + 32, rest, high);
    }
    // The kept candidates move up to the window's start, then down past the pushed ones.
    const __mmask32 after = n >= 32 ? 0 : ~(((__mmask32)1 << n) - 1);
    const __mmask32 reached = reach == BUSTER_SPIKE_TOP_LANES ? ~(__mmask32)0 : (((__mmask32)1 << reach) - 1);
    top->keys = _mm512_maskz_mov_epi16(reached, _mm512_mask_expand_epi16(keys, after, _mm512_maskz_compress_epi16(kept, top->keys)));
    top->low = _mm512_mask_mov_epi16(ones, reached, _mm512_mask_expand_epi16(low, after, _mm512_mask_compress_epi16(ones, kept, top->low)));
    top->high = _mm512_mask_mov_epi16(ones, reached, _mm512_mask_expand_epi16(high, after, _mm512_mask_compress_epi16(ones, kept, top->high)));
    const uint32_t held = top->held - hidden + n;
    top->count = count;
    top->held = held < reach ? held : reach;
    if (top->held < reach && top->count > top->held)
    {
        spike_top_reload(top, slots, reach);
    }
    return result;
}

// The doubling tables of the in-block searches, as spike_previous_smaller8 and spike_next_smaller8 build them:
// windows[k] holds in each lane the least key of the 2^k lanes ending at it (up) or starting at it (down).
BUSTER_INLINE void spike_windows_up8(__m512i keys, __m512i* windows)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], spike_up8(windows[k - 1], k - 1, fill));
    }
}

BUSTER_INLINE void spike_windows_down8(__m512i keys, __m512i* windows)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    windows[0] = keys;
    for (uint32_t k = 1; k < 6; k += 1)
    {
        windows[k] = _mm512_min_epu8(windows[k - 1], spike_down8(windows[k - 1], k - 1, fill));
    }
}

// spike_previous_smaller8's search on its table: a lane's nearest previous lane below its threshold, or a lane of 64 or
// more (bit 7 set) if none is.
BUSTER_INLINE __m512i spike_search_up8(const __m512i* windows, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i p = _mm512_sub_epi8(spike_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_sub_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// spike_next_smaller8's: the nearest next lane below the threshold, or 64 or more.
BUSTER_INLINE __m512i spike_search_down8(const __m512i* windows, __m512i thresholds)
{
    const __m512i one = _mm512_set1_epi8(1);
    __m512i p = _mm512_add_epi8(spike_shifts.lanes8, one);
    for (int32_t k = 5; k >= 0; k -= 1)
    {
        const __m512i v = _mm512_permutexvar_epi8(p, windows[k]);
        const __m512i below = _mm512_min_epu8(_mm512_subs_epu8(thresholds, v), one);
        const __m512i step = _mm512_set1_epi8((char)(1 << k));
        p = _mm512_add_epi8(p, _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), below), step));
    }
    return p;
}

// A block's in-block work for both passes, which depends on no other block, so that every block's runs first, many
// blocks in flight at once, and the passes are left with the window work, whose chain from block to block is short.
typedef struct SpikeBlockWork SpikeBlockWork;
struct SpikeBlockWork
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
    // The lowest node depth, or BUSTER_SPIKE_NONE for a block whose depths span more than BUSTER_SPIKE_BYTE_DEPTH_RANGE,
    // which the passes run as parts; and the block's least key.
    uint32_t base;
    uint8_t minimum;
} __attribute__((aligned(64)));

// Blocks prepared together, each step for all of them before the next, so that their chains share the scheduling
// queues. Four were faster on the libc corpus's small files and slower on sqlite3, whose blocks' work streams through
// L3.
#define BUSTER_SPIKE_PREPARED_TOGETHER 2

_Static_assert(64 * BUSTER_SPIKE_PREPARED_TOGETHER <= BUSTER_SPIKE_TOKEN_PADDING, "the last group of prepared blocks reads the token arrays' padding");

// Every block's in-block work for both passes. `works` has room for a group of BUSTER_SPIKE_PREPARED_TOGETHER blocks
// past the last, and the token arrays BUSTER_SPIKE_TOKEN_PADDING tokens of padding, which the last group may read.
static void spike_top_prepare8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, SpikeBlockWork* works)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    for (uint32_t first = 0; first < count; first += 64 * BUSTER_SPIKE_PREPARED_TOGETHER)
    {
        __mmask64 live_masks[BUSTER_SPIKE_PREPARED_TOGETHER];
        __m512i relative[BUSTER_SPIKE_PREPARED_TOGETHER];
        uint32_t lowest[BUSTER_SPIKE_PREPARED_TOGETHER];
        uint32_t highest[BUSTER_SPIKE_PREPARED_TOGETHER];
        SpikeKeys8 keys[BUSTER_SPIKE_PREPARED_TOGETHER];
        __m512i block_keys[BUSTER_SPIKE_PREPARED_TOGETHER];
        __m512i up[BUSTER_SPIKE_PREPARED_TOGETHER][6];
        __m512i down[BUSTER_SPIKE_PREPARED_TOGETHER][6];
        for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
        {
            const uint32_t block = first + 64 * b;
            // A block past the end has no live lane.
            const uint32_t live = block >= count ? 0 : count - block < 64 ? count - block : 64;
            live_masks[b] = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
            relative[b] = _mm512_maskz_mov_epi8(live_masks[b], spike_relative_depths8(node_depths, block, live_masks[b], &lowest[b], &highest[b]));
        }
        for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
        {
            keys[b] = spike_block_keys8(roles, relative[b], first + 64 * b);
            block_keys[b] = _mm512_mask_mov_epi8(fill, live_masks[b], keys[b].keys);
            up[b][0] = block_keys[b];
            down[b][0] = block_keys[b];
        }
        for (uint32_t k = 1; k < 6; k += 1)
        {
            for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
            {
                up[b][k] = _mm512_min_epu8(up[b][k - 1], spike_up8(up[b][k - 1], k - 1, fill));
                down[b][k] = _mm512_min_epu8(down[b][k - 1], spike_down8(down[b][k - 1], k - 1, fill));
            }
        }
        __m512i lefts[BUSTER_SPIKE_PREPARED_TOGETHER];
        __m512i rights[BUSTER_SPIKE_PREPARED_TOGETHER];
        const __m512i one = _mm512_set1_epi8(1);
        for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
        {
            lefts[b] = _mm512_sub_epi8(spike_shifts.lanes8, one);
            rights[b] = _mm512_add_epi8(spike_shifts.lanes8, one);
        }
        // The searches of spike_search_up8 and spike_search_down8, step by step.
        for (int32_t k = 5; k >= 0; k -= 1)
        {
            const __m512i step = _mm512_set1_epi8((char)(1 << k));
            for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
            {
                const __m512i left = _mm512_permutexvar_epi8(lefts[b], up[b][k]);
                const __m512i right = _mm512_permutexvar_epi8(rights[b], down[b][k]);
                const __m512i left_below = _mm512_min_epu8(_mm512_subs_epu8(keys[b].left, left), one);
                const __m512i right_below = _mm512_min_epu8(_mm512_subs_epu8(keys[b].right, right), one);
                lefts[b] = _mm512_sub_epi8(lefts[b], _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), left_below), step));
                rights[b] = _mm512_add_epi8(rights[b], _mm512_andnot_si512(_mm512_sub_epi8(_mm512_setzero_si512(), right_below), step));
            }
        }
        for (uint32_t b = 0; b < BUSTER_SPIKE_PREPARED_TOGETHER; b += 1)
        {
            SpikeBlockWork* work = &works[first / 64 + b];
            // A block that doesn't fit gets the rest too, unused.
            work->base = highest[b] - lowest[b] <= BUSTER_SPIKE_BYTE_DEPTH_RANGE ? lowest[b] : BUSTER_SPIKE_NONE;
            // A candidate is below every key between it and the block's end on its pass's side.
            const __m512i prefix = _mm512_min_epu8(up[b][5], spike_up8(up[b][5], 5, fill));
            const __m512i suffix = _mm512_min_epu8(down[b][5], spike_down8(down[b][5], 5, fill));
            const __mmask64 suffix_minima = _mm512_cmplt_epu8_mask(block_keys[b], spike_down8(suffix, 0, fill));
            work->right_minima = _mm512_cmplt_epu8_mask(block_keys[b], spike_up8(prefix, 0, fill));
            work->left_minima = _mm512_movepi8_mask(_mm512_permutexvar_epi8(spike_shifts.reverse8, _mm512_movm_epi8(suffix_minima)));
            work->minimum = (uint8_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(suffix));
            _mm512_store_si512(work->keys, block_keys[b]);
            _mm512_store_si512(work->left_thresholds, keys[b].left);
            _mm512_store_si512(work->right_thresholds, keys[b].right);
            _mm512_store_si512(work->lefts, lefts[b]);
            _mm512_store_si512(work->rights, rights[b]);
        }
    }
}

// The prefix minima of byte lanes: lane i gets the least of lanes 0 to i.
BUSTER_INLINE __m512i spike_prefix_minima8(__m512i x)
{
    const __m512i fill = _mm512_set1_epi8((char)0xFF);
    for (uint32_t k = 0; k < 6; k += 1)
    {
        x = _mm512_min_epu8(x, spike_up8(x, k, fill));
    }
    return x;
}

// A block's L and L's key into the left pass's outputs; `partial` stores only the live lanes.
BUSTER_INLINE void spike_top_store_lefts8(const SpikeFound8* found, uint32_t block, __mmask64 live, bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    for (uint32_t h = 0; h < 2; h += 1)
    {
        if (partial)
        {
            _mm512_mask_storeu_epi16(left_keys + block + 32 * h, (__mmask32)(live >> (32 * h)), found->keys[h]);
        }
        else
        {
            _mm512_storeu_si512(left_keys + block + 32 * h, found->keys[h]);
        }
    }
    for (uint32_t q = 0; q < 4; q += 1)
    {
        if (partial)
        {
            _mm512_mask_storeu_epi32(lefts + block + 16 * q, (__mmask16)(live >> (16 * q)), found->positions[q]);
        }
        else
        {
            _mm512_storeu_si512(lefts + block + 16 * q, found->positions[q]);
        }
    }
}

// One block or part (the live lanes) of the left pass: L by spike_previous_smaller8 in the block, else from the
// window. Its candidates for later blocks are its suffix minima, which in the reversed block are its prefix minima,
// nearest first.
BUSTER_INLINE void spike_top_left8_block(SpikeTop* top, const SpikeSlots* slots, uint32_t reach, const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block,
                                         __mmask64 live, bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    const SpikeKeys8 keys = spike_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), live, keys.keys);
    const __m512i p = spike_previous_smaller8(block_keys, keys.left);
    const __m512i reversed = _mm512_permutexvar_epi8(spike_shifts.reverse8, block_keys);
    const __m512i prefix = spike_prefix_minima8(reversed);
    // A lane is a candidate if its key is below every key nearer the block's end; dead lanes (0xFF) never are.
    const __mmask64 minima = _mm512_cmplt_epu8_mask(reversed, spike_up8(prefix, 0, _mm512_set1_epi8((char)0xFF)));
    const __m512i minimum = _mm512_permutexvar_epi8(spike_shifts.last8, prefix);
    const SpikeFound8 found = spike_top_block8(top, slots, reach, block_keys, keys.left, p, reversed, spike_shifts.reverse8, minima, minimum, block, base, live);
    spike_top_store_lefts8(&found, block, live, partial, lefts, left_keys);
}

// A part of a block, lanes [start, start + size): the live lanes among them, and their depths relative to the part's
// lowest, which it returns in *base; *fits says whether they span at most BUSTER_SPIKE_BYTE_DEPTH_RANGE.
BUSTER_INLINE __mmask64 spike_top_part(const uint32_t* node_depths, uint32_t block, __mmask64 live, uint32_t start, uint32_t size, __m512i* relative, uint32_t* base,
                                       bool* fits)
{
    const __mmask64 lanes = (size == 64 ? ~(__mmask64)0 : ((((__mmask64)1 << size) - 1) << start)) & live;
    uint32_t highest;
    // Dead lanes' depths are 0, so that spike_block_keys8's 16-bit shift never carries a byte into a live neighbor.
    *relative = _mm512_maskz_mov_epi8(lanes, spike_relative_depths8(node_depths, block, lanes, base, &highest));
    *fits = highest - *base <= BUSTER_SPIKE_BYTE_DEPTH_RANGE;
    return lanes;
}

// One prepared block of the left pass: its window work on the in-block work of spike_top_prepare8_avx512.
BUSTER_INLINE void spike_top_left8_prepared(SpikeTop* top, const SpikeSlots* slots, uint32_t reach, const SpikeBlockWork* work, uint32_t block, __mmask64 live,
                                            bool partial, uint32_t* lefts, uint16_t* left_keys)
{
    const __m512i block_keys = _mm512_load_si512(work->keys);
    const __m512i reversed = _mm512_permutexvar_epi8(spike_shifts.reverse8, block_keys);
    const SpikeFound8 found = spike_top_block8(top, slots, reach, block_keys, _mm512_load_si512(work->left_thresholds), _mm512_load_si512(work->lefts), reversed,
                                               spike_shifts.reverse8, work->left_minima, _mm512_set1_epi8((char)work->minimum), block, work->base, live);
    spike_top_store_lefts8(&found, block, live, partial, lefts, left_keys);
}

// K4's left pass with the stack's top in registers, on the prepared blocks.
BUSTER_INLINE void spike_top_left8_pass(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, const SpikeBlockWork* works,
                                        const SpikeSlots* slots, uint32_t reach, uint64_t* wide_blocks)
{
    SpikeTop top;
    spike_top_reset(&top);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const SpikeBlockWork* work = &works[block / 64];
        if (work->base != BUSTER_SPIKE_NONE)
        {
            spike_top_left8_prepared(&top, slots, reach, work, block, live_mask, live != 64, lefts, left_keys);
        }
        else
        {
            // Aligned parts from the block's start, each the largest that fits.
            *wide_blocks += 1;
            __m512i relative;
            uint32_t base;
            uint32_t start = 0;
            while (start < live)
            {
                uint32_t size = start == 0 ? 32 : 1u << __builtin_ctz(start);
                size = size > 32 ? 32 : size;
                bool part_fits = false;
                __mmask64 lanes = 0;
                while (!part_fits)
                {
                    lanes = spike_top_part(node_depths, block, live_mask, start, size, &relative, &base, &part_fits);
                    part_fits |= size == 1;
                    size = part_fits ? size : size / 2;
                }
                spike_top_left8_block(&top, slots, reach, roles, relative, base, block, lanes, true, lefts, left_keys);
                start += size;
            }
        }
    }
}

// The parents of a block's live lanes from their R (`found`), their L and their roles' rules, as
// spike_parents_right8_block picks them.
BUSTER_INLINE void spike_top_parents8(const SpikeFound8* found, __m512i rules, uint32_t block, __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys,
                                      uint32_t* parents)
{
    const __m512i weak_ties_table = _mm512_loadu_si512(spike_parent_tables.weak_ties);
    // 32 lanes at a time with 16-bit keys. No real key is 0, which marks no candidate.
    const __mmask64 rule_left = spike_equal8(rules, SPIKE_RULE_LEFT);
    const __mmask64 rule_right = _mm512_cmpge_epu8_mask(rules, _mm512_set1_epi8(SPIKE_RULE_RIGHT));
    for (uint32_t h = 0; h < 2; h += 1)
    {
        const __m512i left_key = _mm512_loadu_si512(left_keys + block + 32 * h);
        const __m512i right_key = found->keys[h];
        const __m512i weak_ties = _mm512_permutexvar_epi16(_mm512_and_si512(left_key, _mm512_set1_epi16(31)), weak_ties_table);
        const __mmask32 weak = _mm512_test_epi16_mask(weak_ties, _mm512_set1_epi16(1));
        const __mmask32 ties = _mm512_test_epi16_mask(weak_ties, _mm512_set1_epi16(2));
        const __mmask32 greater = _mm512_cmpgt_epu16_mask(left_key, right_key);
        const __mmask32 equal = _mm512_cmpeq_epi16_mask(left_key, right_key);
        const __mmask32 left_valid = _mm512_test_epi16_mask(left_key, left_key);
        const __mmask32 right_valid = _mm512_test_epi16_mask(right_key, right_key);
        __m512i left[2];
        for (uint32_t q = 0; q < 2; q += 1)
        {
            left[q] = _mm512_loadu_si512(lefts + block + 32 * h + 16 * q);
        }
        const __mmask32 select_left = left_valid & (~right_valid | (~weak & (greater | (equal & ties))));
        const __mmask32 take_left = (__mmask32)(rule_left >> (32 * h)) | (~(__mmask32)(rule_right >> (32 * h)) & select_left);
        for (uint32_t q = 0; q < 2; q += 1)
        {
            const __m512i parent = _mm512_mask_mov_epi32(found->positions[2 * h + q], (__mmask16)(take_left >> (16 * q)), left[q]);
            if (partial)
            {
                _mm512_mask_storeu_epi32(parents + block + 32 * h + 16 * q, (__mmask16)(live >> (32 * h + 16 * q)), parent);
            }
            else
            {
                _mm512_storeu_si512(parents + block + 32 * h + 16 * q, parent);
            }
        }
    }
}

// One block or part of the right pass: R by spike_next_smaller8 in the block, else from the window, then the parent
// by the role's rule. Its candidates for earlier blocks are its prefix minima.
BUSTER_INLINE void spike_top_right8_block(SpikeTop* top, const SpikeSlots* slots, uint32_t reach, const uint8_t* roles, __m512i relative, uint32_t base, uint32_t block,
                                          __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents)
{
    const SpikeKeys8 keys = spike_block_keys8(roles, relative, block);
    const __m512i block_keys = _mm512_mask_mov_epi8(_mm512_set1_epi8((char)0xFF), live, keys.keys);
    const __m512i p = spike_next_smaller8(block_keys, keys.right);
    const __m512i prefix = spike_prefix_minima8(block_keys);
    const __mmask64 minima = _mm512_cmplt_epu8_mask(block_keys, spike_up8(prefix, 0, _mm512_set1_epi8((char)0xFF)));
    const __m512i minimum = _mm512_permutexvar_epi8(spike_shifts.last8, prefix);
    const SpikeFound8 found = spike_top_block8(top, slots, reach, block_keys, keys.right, p, block_keys, spike_shifts.lanes8, minima, minimum, block, base, live);
    spike_top_parents8(&found, keys.rules, block, live, partial, lefts, left_keys, parents);
}

// One prepared block of the right pass.
BUSTER_INLINE void spike_top_right8_prepared(SpikeTop* top, const SpikeSlots* slots, uint32_t reach, const uint8_t* roles, const SpikeBlockWork* work, uint32_t block,
                                             __mmask64 live, bool partial, const uint32_t* lefts, const uint16_t* left_keys, uint32_t* parents)
{
    const __m512i block_keys = _mm512_load_si512(work->keys);
    const SpikeFound8 found = spike_top_block8(top, slots, reach, block_keys, _mm512_load_si512(work->right_thresholds), _mm512_load_si512(work->rights), block_keys,
                                               spike_shifts.lanes8, work->right_minima, _mm512_set1_epi8((char)work->minimum), block, work->base, live);
    spike_top_parents8(&found, spike_lookup8(spike_parent_tables.rules, _mm512_loadu_si512(roles + block)), block, live, partial, lefts, left_keys, parents);
}

// K4's right pass with the stack's top in registers, from the last block to the first, on the prepared blocks; a block
// whose depths don't fit runs as aligned parts from its end, each the largest that fits.
BUSTER_INLINE void spike_top_right8_pass(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys,
                                         const SpikeBlockWork* works, uint32_t* parents, const SpikeSlots* slots, uint32_t reach)
{
    SpikeTop top;
    spike_top_reset(&top);
    for (uint32_t b = (count + 63) / 64; b-- > 0;)
    {
        const uint32_t block = b * 64;
        const uint32_t live = count - block < 64 ? count - block : 64;
        const __mmask64 live_mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
        const SpikeBlockWork* work = &works[b];
        if (work->base != BUSTER_SPIKE_NONE)
        {
            spike_top_right8_prepared(&top, slots, reach, roles, work, block, live_mask, live != 64, lefts, left_keys, parents);
        }
        else
        {
            uint32_t end = live;
            while (end > 0)
            {
                uint32_t size = 1u << __builtin_ctz(end);
                size = size > 32 ? 32 : size;
                bool part_fits = false;
                __mmask64 lanes = 0;
                __m512i relative;
                uint32_t part_base;
                while (!part_fits)
                {
                    lanes = spike_top_part(node_depths, block, live_mask, end - size, size, &relative, &part_base, &part_fits);
                    part_fits |= size == 1;
                    size = part_fits ? size : size / 2;
                }
                spike_top_right8_block(&top, slots, reach, roles, relative, part_base, block, lanes, true, lefts, left_keys, parents);
                end -= size;
            }
        }
    }
}

// The passes alone, as the vector path runs them, for the benchmark's stages.
static void spike_top_left8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, const SpikeBlockWork* works,
                                   const SpikeSlots* slots, uint64_t* wide_blocks)
{
    spike_top_left8_pass(roles, node_depths, count, lefts, left_keys, works, slots, BUSTER_SPIKE_TOP_LANES, wide_blocks);
}

static void spike_top_right8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, const uint32_t* lefts, const uint16_t* left_keys,
                                    const SpikeBlockWork* works, uint32_t* parents, const SpikeSlots* slots)
{
    spike_top_right8_pass(roles, node_depths, count, lefts, left_keys, works, parents, slots, BUSTER_SPIKE_TOP_LANES);
}

// K4 with the stack's top in registers: every block's in-block work, then the left pass, then the right pass.
static void spike_top_parents8_avx512(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, SpikeBlockWork* works,
                                      uint32_t* parents, const SpikeSlots* slots, uint64_t* wide_blocks)
{
    spike_top_prepare8_avx512(roles, node_depths, count, works);
    spike_top_left8_avx512(roles, node_depths, count, lefts, left_keys, works, slots, wide_blocks);
    spike_top_right8_avx512(roles, node_depths, count, lefts, left_keys, works, parents, slots);
}

// The same with a window of `reach` lanes, fewer than BUSTER_SPIKE_TOP_LANES, so that the check takes the paths through
// memory (deep lanes, reloads, pops under the window) that real stacks never need.
static void spike_top_parents8_reach(const uint8_t* roles, const uint32_t* node_depths, uint32_t count, uint32_t* lefts, uint16_t* left_keys, SpikeBlockWork* works,
                                     uint32_t* parents, const SpikeSlots* slots, uint32_t reach, uint64_t* wide_blocks)
{
    spike_top_prepare8_avx512(roles, node_depths, count, works);
    spike_top_left8_pass(roles, node_depths, count, lefts, left_keys, works, slots, reach, wide_blocks);
    spike_top_right8_pass(roles, node_depths, count, lefts, left_keys, works, parents, slots, reach);
}

// Memory for the stack's slots, with their padding.
static bool spike_slots_reserve(SpikeSlots* slots, Arena* arena, uint64_t capacity)
{
    const uint64_t count = capacity + BUSTER_SPIKE_TOP_LANES;
    slots->keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->low = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->high = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    slots->capacity = (uint32_t)capacity;
    const bool result = slots->keys != 0 && slots->low != 0 && slots->high != 0;
    if (result)
    {
        for (uint64_t i = capacity; i < count; i += 1)
        {
            slots->keys[i] = 0;
            slots->low[i] = 0xFFFF;
            slots->high[i] = 0xFFFF;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The vector path together

// What the vector kernels need besides SpikeKernels' arrays.
typedef struct SpikeVectorBuffers SpikeVectorBuffers;
struct SpikeVectorBuffers
{
    // By depth: K1's open openers, and K2's group stacks.
    uint32_t* open_stack;
    SpikeGroupStacks groups;
    // By token: K2's typed states, K4's L and its key.
    uint8_t* typed;
    uint32_t* lefts;
    uint16_t* left_keys;
    SpikeCandidates candidates;
    // K4 with the stack's top in registers: each block's in-block work, and the stack's slots.
    SpikeBlockWork* works;
    SpikeSlots slots;
    uint32_t* secondary;
    SpikeStatement* statements;
    // K2's statement contexts, per 64 tokens, and for K5 each event's previous event in its level, the owners of the
    // open blocks and the levels interrupted.
    SpikeStatementContext* contexts;
    uint32_t* statement_previous;
    uint32_t* statement_owners;
    SpikeStatementLevel* statement_levels;
    // The postorder's: each token's last child, the end of its subtree, the counting sort's cursors, and the postorder.
    uint32_t* last_children;
    uint32_t* subtree_ends;
    uint32_t* cursors;
    uint32_t* postorder;
    // K2's blocks by the rounds they took (index 0: the twin did them), and K4's blocks whose depths span more than
    // byte lanes hold, which run as two 16-bit blocks.
    uint64_t rounds[BUSTER_SPIKE_ROLE_ROUNDS + 1];
    uint64_t wide_blocks;
    SpikeStatementCounts statement_counts;
};

// Candidates for K4's searches, with their padding below index 0.
static bool spike_candidates_reserve(SpikeCandidates* candidates, Arena* arena, uint64_t capacity)
{
    const uint64_t slots = capacity + BUSTER_SPIKE_CANDIDATE_PADDING + 64;
    uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    uint16_t* keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, slots);
    const bool result = positions != 0 && keys != 0;
    if (result)
    {
        for (uint32_t i = 0; i < BUSTER_SPIKE_CANDIDATE_PADDING; i += 1)
        {
            positions[i] = BUSTER_SPIKE_NONE;
            keys[i] = 0;
        }
        candidates->positions = positions + BUSTER_SPIKE_CANDIDATE_PADDING;
        candidates->keys = keys + BUSTER_SPIKE_CANDIDATE_PADDING;
        candidates->count = 0;
    }
    return result;
}

// The by-depth stacks have BUSTER_SPIKE_DEPTH_PADDING entries below depth 0: no opener, role NONE, not typed. K2 reads
// 65 below a block's starting depth.
#define BUSTER_SPIKE_DEPTH_PADDING 128

static bool spike_vector_buffers_reserve(SpikeVectorBuffers* buffers, Arena* arena, uint64_t capacity)
{
    const uint64_t slots = capacity + 2 * BUSTER_SPIKE_TOKEN_PADDING;
    uint32_t* open_stack = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots + BUSTER_SPIKE_DEPTH_PADDING);
    uint8_t* group_roles = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots + BUSTER_SPIKE_DEPTH_PADDING);
    uint8_t* group_typed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots + BUSTER_SPIKE_DEPTH_PADDING);
    uint8_t* group_keywords = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots + BUSTER_SPIKE_DEPTH_PADDING);
    *buffers = (SpikeVectorBuffers){
        .open_stack = open_stack != 0 ? open_stack + BUSTER_SPIKE_DEPTH_PADDING : 0,
        .groups = {
            .roles = group_roles != 0 ? group_roles + BUSTER_SPIKE_DEPTH_PADDING : 0,
            .typed = group_typed != 0 ? group_typed + BUSTER_SPIKE_DEPTH_PADDING : 0,
            .keywords = group_keywords != 0 ? group_keywords + BUSTER_SPIKE_DEPTH_PADDING : 0,
        },
        .typed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots),
        .lefts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .left_keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, slots),
        .works = BUSTER_ARENA_PUSH_ARRAY(arena, SpikeBlockWork, slots / 64 + BUSTER_SPIKE_PREPARED_TOGETHER),
        .secondary = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .statements = BUSTER_ARENA_PUSH_ARRAY(arena, SpikeStatement, slots),
        .contexts = BUSTER_ARENA_PUSH_ARRAY(arena, SpikeStatementContext, slots / 64 + 1),
        .statement_previous = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .statement_owners = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .statement_levels = BUSTER_ARENA_PUSH_ARRAY(arena, SpikeStatementLevel, slots),
        .last_children = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .subtree_ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .cursors = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .postorder = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots),
        .rounds = { 0 },
        .wide_blocks = 0,
        .statement_counts = { 0 },
    };
    bool result = open_stack != 0 && group_roles != 0 && group_typed != 0 && group_keywords != 0 && buffers->typed != 0 && buffers->lefts != 0 && buffers->left_keys != 0 &&
                  buffers->secondary != 0 && buffers->statements != 0 && buffers->contexts != 0 && buffers->statement_previous != 0 && buffers->statement_owners != 0 &&
                  buffers->statement_levels != 0 &&
                  buffers->last_children != 0 && buffers->subtree_ends != 0 && buffers->cursors != 0 && buffers->postorder != 0 &&
                  buffers->works != 0 && spike_candidates_reserve(&buffers->candidates, arena, capacity) &&
                  spike_slots_reserve(&buffers->slots, arena, capacity);
    if (result)
    {
        for (uint32_t i = 0; i < BUSTER_SPIKE_DEPTH_PADDING; i += 1)
        {
            open_stack[i] = BUSTER_SPIKE_NONE;
            group_roles[i] = SPIKE_ROLE_NONE;
            group_typed[i] = 0;
            group_keywords[i] = SPIKE_KIND_END;
        }
    }
    return result;
}

// The deepest node depth, which decides whether K4's 16-bit keys can hold it.
static uint32_t spike_deepest(const SpikeKernels* kernels, uint32_t count)
{
    __m512i deepest = _mm512_setzero_si512();
    for (uint32_t block = 0; block < count; block += 16)
    {
        const __mmask16 live = count - block < 16 ? (__mmask16)((1u << (count - block)) - 1) : (__mmask16)0xFFFF;
        deepest = _mm512_max_epu32(deepest, _mm512_maskz_loadu_epi32(live, kernels->node_depths + block));
    }
    return _mm512_reduce_max_epu32(deepest);
}

// K4 as the vector path runs it: in byte lanes with the stack's top in registers, but for nesting too deep for its
// 16-bit keys, which the twin takes.
static void spike_parents_vector(SpikeKernels* kernels, uint32_t count, SpikeVectorBuffers* buffers)
{
    if (spike_deepest(kernels, count) <= BUSTER_SPIKE_MAX_VECTOR_DEPTH)
    {
        spike_top_parents8_avx512(kernels->roles, kernels->node_depths, count, buffers->lefts, buffers->left_keys, buffers->works, kernels->parents, &buffers->slots,
                                  &buffers->wide_blocks);
    }
    else
    {
        spike_parents_scalar(kernels, count);
    }
}

// K5 as the vector path runs it, after K4: in AVX-512, or where only the twin's stack decides, K4 again (for its
// parents, which K5 overwrote) and the twin. Returns false if an else has no if.
static bool spike_statements_vector(SpikeKernels* kernels, uint32_t count, SpikeVectorBuffers* buffers)
{
    const SpikeStatementsVerdict verdict = spike_statements_avx512(kernels, count, buffers->contexts, buffers->statement_previous, buffers->statement_owners,
                                                                   buffers->statement_levels, &buffers->statement_counts);
    bool result = verdict == SPIKE_STATEMENTS_OK;
    if (verdict == SPIKE_STATEMENTS_TWIN)
    {
        buffers->statement_counts.twins += 1;
        spike_parents_vector(kernels, count, buffers);
        result = spike_statements(kernels, count, buffers->statements);
    }
    return result;
}

// Every kernel in AVX-512 in turn, then the fallback, into kernels->parents: the data-parallel parser.
static SpikeKernelResult spike_parse_vector(SpikeKernels* kernels, const uint8_t* kinds, const uint32_t* names, uint32_t count, SpikeMeanings* meanings,
                                            Arena* scratch, SpikeVectorBuffers* buffers)
{
    SpikeKernelResult result = { .flagged_tokens = 0, .everything = false };
    kernels->unbalanced = false;
    kernels->unsupported = false;
    // K1, on ( [ {.
    kernels->unbalanced = !spike_brackets_avx512(kinds, spike_kind_deltas, count, kernels->depths, kernels->enclosing, buffers->open_stack, kernels->bracket_blocks);
    // K3.
    if (!kernels->unbalanced)
    {
        spike_typedefs(kernels, kinds, names, count);
    }
    spike_classify_mask_avx512(kernels, kinds, names, count);
    // K2.
    spike_roles_avx512(kernels, count, buffers->typed, &buffers->groups, buffers->contexts, buffers->rounds);
    // K1's second part, and the roles after the colons it resolved.
    spike_secondary_avx512(kernels, count, buffers->secondary);
    spike_after_colons_avx512(kernels, count);
    // K4 and K5.
    spike_parents_vector(kernels, count, buffers);
    const bool statements_ok = spike_statements_vector(kernels, count, buffers);
    for (uint32_t block = 0; block < count; block += 64)
    {
        const __mmask64 live = count - block < 64 ? (((__mmask64)1 << (count - block)) - 1) : ~(__mmask64)0;
        const __m512i flags = _mm512_maskz_loadu_epi8(live, kernels->flags + block);
        result.flagged_tokens += (uint64_t)__builtin_popcountll(_mm512_test_epi8_mask(flags, flags));
    }
    result.everything = kernels->unbalanced || kernels->unsupported || !statements_ok;
    result.fallback = spike_fallback(kernels, kinds, names, count, result.everything, meanings, scratch);
    for (uint32_t i = 0; i < kernels->typedef_count; i += 1)
    {
        kernels->typedef_names[kernels->typedef_log[i]] = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The postorder, from the parents
//
// Every subtree is a run of tokens: a token's L and R bound its subtree, as in a Cartesian tree, and a statement's
// head owns the statement that follows it. So a token with no child after it ends its subtree, and the subtrees that
// end with it are those of the ancestors whose last child, after them, leads down to it. The postorder emits each such
// token in order, then climbs while it is its parent's last child and comes after the parent. Children come in source
// order, each child's subtree before the next. The benchmark's check compares the result with a depth-first walk.

static uint32_t spike_postorder(const uint32_t* parents, uint32_t count, uint32_t* last_children, uint32_t* postorder)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        last_children[i] = BUSTER_SPIKE_NONE;
    }
    // In order, so that each token's last write is its last child.
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t parent = parents[i];
        if (parent != BUSTER_SPIKE_NONE)
        {
            last_children[parent] = i;
        }
    }
    uint32_t written = 0;
    for (uint32_t t = 0; t < count; t += 1)
    {
        // No child after t: none at all (BUSTER_SPIKE_NONE + 1 wraps to 0), or the last one before it.
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

// The same postorder without a branch on the tree, from each token's subtree end: end(v) from its last child after it
// (or v itself), backward. In postorder a subtree's tokens are a run that ends with its root, after every token whose
// subtree ends before it, and its other tokens are those its subtree's source run holds, so the root's position is
// C(v) + end(v) - v, where C(v), the number of tokens whose subtree ends before v, is an exclusive prefix sum of how
// many subtrees end at each token. last_children has count + 1 entries: the roots' writes go to the last.
static void spike_postorder_sorted(const uint32_t* parents, uint32_t count, uint32_t* last_children, uint32_t* ends, uint32_t* before, uint32_t* postorder)
{
    // 0: no child after the token, as a child at 0 comes after none.
    memset(last_children, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    memset(before, 0, (uint64_t)count * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i += 1)
    {
        last_children[parents[i] == BUSTER_SPIKE_NONE ? count : parents[i]] = i;
    }
    for (uint32_t v = count; v-- > 0;)
    {
        const uint32_t last = last_children[v];
        // ends[last] is v's end when last comes after v, and was written then; otherwise it is unused. Unpredictable,
        // so that Clang selects (cmov) rather than branching around the load.
        const uint32_t end = ends[last];
        const uint32_t own = __builtin_unpredictable(last > v) ? end : v;
        ends[v] = own;
        before[own] += 1;
    }
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t ending = before[i];
        before[i] = sum;
        sum += ending;
    }
    for (uint32_t v = 0; v < count; v += 1)
    {
        postorder[before[v] + ends[v] - v] = v;
    }
}
