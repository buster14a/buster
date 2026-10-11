#pragma once

// The implicit postorder C syntax tree (GitHub #3102).
//
// c_ast_build commits the final expanded token stream of one translation unit
// into a complete syntax tree in one forward pass: declarations and
// declarators, statements, expressions, initializers and designators,
// attributes, assembly and the supported GNU/C23 forms, function bodies
// included. Nothing is re-parsed afterwards and no pointer tree exists at any
// point; the tree is the append order itself.
//
// Topology is implicit in node order. Nodes are stored in postorder: every
// subtree occupies one contiguous interval of node indices, children precede
// their parent, and a subtree's root is its interval's last node. The one
// structural field is `extents[n]`, the number of nodes in the subtree rooted
// at n, the root included:
//
//     subtree(n)    = [n + 1 - extents[n], n + 1)
//     last child    = n - 1                       (when n has children)
//     previous      = child - extents[child]      (until subtree_begin(n))
//
// The children of n are therefore the maximal subtrees tiling
// [subtree_begin(n), n), discovered backward from n - 1 with one dependent
// load per child. For `a + b * c`:
//
//     index  kind      extent
//     0      a         1
//     1      b         1
//     2      c         1
//     3      multiply  3
//     4      add       5
//
// The builder never reads back a published node. Each open construct keeps
// the node index at which its subtree began on an explicit frame, and its
// parent is appended with extent `append_position + 1 - begin` once grammar,
// precedence and associativity commit it. The parent's kind is chosen at that
// point, which is why a declaration that turns out to be a function definition
// or a parenthesis that turns out to be a cast needs no rewrite.
//
// Every kind has a child contract in C_AST_KIND_LIST, checked by
// c_ast_validate:
//   LEAF      no children.
//   FIXED     exactly `a` children, positional.
//   RANGE     `a` to `b` children; the documented order is positional, and an
//             optional child is always the trailing one.
//   PRESENCE  `a` required children plus one optional child per set bit of
//             the low byte of data (C_AST_PRESENCE_MASK); children appear in
//             source order, and the per-kind comment names each bit's slot.
//             Bits 8 and up carry the kind's flags.
//   LIST      `a` fixed leading children, a variadic middle and `b` fixed
//             trailing children. In the implicit layout data is the middle
//             count; c_ast_list_count answers it in every layout.
//
// Node rows are structure of arrays, so a consumer scanning kinds touches
// one byte per node:
//   kinds    CAstKind, one byte.
//   extents  the subtree node count (>= 1).
//   tokens   provenance: the final-stream token index the kind's comment
//            names (operator, keyword, name, or first token). Spelling,
//            symbol, literal facts and source location are reached through it.
//   data     the kind's payload: an interned symbol, a CAstWord, presence
//            bits and flags, or a list word.
//
// Layouts (CAstLayout) are variants of the same tree for measurement. The
// forward pass always produces the implicit layout; HYBRID and EXPLICIT add
// child-root slices in finalization, and their cost is reported in
// CAstStatistics. Navigation goes through the accessors below, which give the
// same answer in every layout.
//
// Ownership: the four node columns are written once, in place, where the
// published tree keeps them: each grows in its own private arena (CAstStorage)
// that reserves address space and commits it as the builder appends, so no
// node is staged and copied. The tree owns those arenas until c_ast_release;
// the storage record, the diagnostics and the HYBRID/EXPLICIT slices live in
// the caller's arena. Builder frames, stacks and bindings live in the phase
// arena (CAstOptions.phase_arena, else a private one created and retired by
// the build) and are released before return. A failed build releases its
// columns itself. The tree references the preprocessing result's tokens and
// symbols by index and requires that result to stay alive.
//
// Scope: the builder resolves the names that change the parse at their
// declaration points -- typedef names against ordinary identifiers, with
// block, function-prototype, function-body and `for` scopes and C17 6.2.1p7
// visibility (a declarator's name is visible from the end of its declarator).
// It decides no types and binds no other uses; semantic completion owns that.
//
// Entry points: c_ast_build, c_ast_release, c_ast_validate, c_ast_dump,
// c_ast_child_count, c_ast_list_count, c_ast_child_at, c_ast_children,
// c_ast_walk_begin / c_ast_walk_next, c_ast_kind_name, c_ast_word_name.

#include <buster/lib/compiler/frontend/c/c.h>

// The token lookahead the forward cursor may request beyond the current token
// (C_AST_LOOKAHEAD tokens, the current one included): `identifier :` at a
// statement start and `( type-name` decisions need two. The refill window is
// never smaller than this, and tests vary the refill batch to prove no rule
// reads further.
#define C_AST_LOOKAHEAD 3

#define C_AST_NODE_INVALID UINT32_MAX

// Nodes a column reservation holds before the columns move to reservations of
// twice the capacity. It is address space, not memory: 13 bytes per node across
// the four columns (3.25 GiB), committed only as the builder appends. It is
// not a bound on any tree: a larger one moves, and the move is the only copy a
// build makes (CAstStatistics.column_copy_bytes). The unity self-host tree is
// about 1% of it.
#define C_AST_COLUMN_CAPACITY (1u << 28)
// Nodes every column commits at a time; the one predictable branch per append
// is reaching the committed end. C_AST_NODE_LIMIT is a multiple of it.
#define C_AST_COLUMN_STEP (1u << 16)

#define C_AST_PRESENCE_MASK 0xffu

// X(name, contract, a, b): see the contract classes above. The comment
// after each row is its child contract and payload; `token` is its tokens[]
// anchor and `data` its payload.
#define C_AST_KIND_LIST(X)                                                                                                                     \
    /* ---- translation unit and external declarations ---- */                                                                                \
    X(TRANSLATION_UNIT, LIST, 0, 0)        /* external declarations; token: end-of-file token */                                            \
    X(DECLARATION, LIST, 1, 0)             /* DECL_SPECIFIERS, INIT_DECLARATOR*; token: first token */                                      \
    X(FUNCTION_DEFINITION, LIST, 2, 1)     /* DECL_SPECIFIERS, declarator, K&R DECLARATION*, COMPOUND_STATEMENT; token: first token */      \
    X(EMPTY_DECLARATION, LEAF, 0, 0)       /* a stray file-scope `;`; token: `;` */                                                         \
    X(STATIC_ASSERT, RANGE, 1, 2)          /* condition, [STRING message]; token: keyword */                                                \
    X(ASM_TOP_LEVEL, FIXED, 1, 1)          /* STRING; token: keyword */                                                                     \
    X(PRAGMA, LEAF, 0, 0)                  /* a C_TOKEN_PRAGMA token, in stream order; token: the pragma */                                 \
    /* ---- declaration specifiers ---- */                                                                                                  \
    X(DECL_SPECIFIERS, LIST, 0, 0)         /* specifier items in source order; token: first token */                                        \
    X(SPECIFIER_WORD, LEAF, 0, 0)          /* data: CAstWord (normalized: __const__ is CONST); token: the word */                           \
    X(TYPEDEF_NAME, LEAF, 0, 0)            /* data: symbol; token: the name */                                                              \
    X(STRUCT_SPECIFIER, PRESENCE, 0, 0)    /* bits 0 attributes, 1 TAG_NAME, 2 MEMBER_LIST, 3 trailing attributes; token: keyword */       \
    X(UNION_SPECIFIER, PRESENCE, 0, 0)     /* as STRUCT_SPECIFIER */                                                                        \
    X(ENUM_SPECIFIER, PRESENCE, 0, 0)      /* bits 0 attributes, 1 TAG_NAME, 2 TYPE_NAME (C23 fixed type), 3 ENUMERATOR_LIST,              \
                                              4 trailing attributes; token: keyword */                                                      \
    X(TAG_NAME, LEAF, 0, 0)                /* data: symbol; token: the tag */                                                               \
    X(MEMBER_LIST, LIST, 0, 0)             /* MEMBER_DECLARATION, STATIC_ASSERT, EMPTY_DECLARATION or PRAGMA items; token: `{` */        \
    X(MEMBER_DECLARATION, LIST, 1, 0)      /* DECL_SPECIFIERS, MEMBER_DECLARATOR*; token: first token */                                    \
    X(MEMBER_DECLARATOR, PRESENCE, 0, 0)   /* bits 0 declarator, 1 bit-field width, 2 trailing ATTRIBUTE_LIST, 3 leading ATTRIBUTE_LIST     \
                                              (written before the declarator or the `:`, `int a, __attribute__((x)) b;`; first when set).   \
                                              Then the declarator, and the width and the trailing list in the order written (identify those \
                                              two by kind); token: name, else `:` */                                                        \
    X(ENUMERATOR_LIST, LIST, 0, 0)         /* ENUMERATOR*; token: `{` */                                                                    \
    X(ENUMERATOR, PRESENCE, 0, 0)          /* bits 0 attributes, 1 value; token: the name */                                                \
    X(TYPEOF, FIXED, 1, 1)                 /* TYPE_NAME or expression; token: keyword */                                                    \
    X(TYPEOF_UNQUAL, FIXED, 1, 1)          /* TYPE_NAME or expression; token: keyword */                                                    \
    X(ATOMIC_SPECIFIER, FIXED, 1, 1)       /* `_Atomic ( TYPE_NAME )`; token: keyword */                                                    \
    X(ALIGNAS, FIXED, 1, 1)                /* TYPE_NAME or expression; token: keyword */                                                    \
    X(BITINT, FIXED, 1, 1)                 /* `_BitInt ( width )`; token: keyword */                                                        \
    /* ---- declarators ---- */                                                                                                             \
    X(INIT_DECLARATOR, PRESENCE, 1, 0)     /* bit 3 leading ATTRIBUTE_LIST (written before the declarator, `int a, __attribute__((x)) b;`;  \
                                              first when set), the declarator, then bits 0 ASM_LABEL and 1 trailing ATTRIBUTE_LIST in the   \
                                              order written (identify those two by kind), 2 initializer (last); a declarator can carry      \
                                              both lists; token: declarator's name token, else its first token */                           \
    X(ASM_LABEL, FIXED, 1, 1)              /* STRING; token: keyword */                                                                     \
    X(DECLARATOR_NAME, LEAF, 0, 0)         /* data: symbol; token: the name */                                                              \
    X(DECLARATOR_POINTER, PRESENCE, 0, 0)  /* bits 0 ATTRIBUTE_LIST, 1 inner declarator; flags C_AST_QUALIFIER_*; token: `*` */            \
    X(DECLARATOR_ATTRIBUTED, FIXED, 2, 2)  /* ATTRIBUTE_LIST, inner declarator: attributes opening a parenthesized group whose first item   \
                                              is not `*`, `int (__attribute__((x)) p);` (a pointer keeps its list in DECLARATOR_POINTER);   \
                                              the list applies to everything inside the parentheses; token: the list's first token */       \
    X(DECLARATOR_ARRAY, PRESENCE, 0, 0)    /* bits 0 inner declarator, 1 size; flags C_AST_QUALIFIER_*, C_AST_ARRAY_*; token: `[` */       \
    X(DECLARATOR_FUNCTION, PRESENCE, 1, 0) /* bit 0 inner declarator, then the parameter or identifier list (last); token: `(` */          \
    X(PARAMETER_LIST, LIST, 0, 0)          /* PARAMETER*; `()` has none; token: `(` */                                                      \
    X(PARAMETER_LIST_VARIADIC, LIST, 0, 0) /* PARAMETER* followed by `...`; token: `(` */                                                   \
    X(IDENTIFIER_LIST, LIST, 0, 0)         /* K&R DECLARATOR_NAME*; token: `(` */                                                           \
    X(PARAMETER, PRESENCE, 1, 0)           /* DECL_SPECIFIERS, bits 0 declarator, 1 trailing ATTRIBUTE_LIST; token: first token */          \
    X(TYPE_NAME, PRESENCE, 1, 0)           /* DECL_SPECIFIERS, bit 0 abstract declarator; token: first token */                             \
    /* ---- attributes ---- */                                                                                                              \
    X(ATTRIBUTE_LIST, LIST, 0, 0)          /* consecutive ATTRIBUTE_GNU/_STANDARD/_DECLSPEC groups; token: first group's first token */  \
    X(ATTRIBUTE_GNU, LIST, 0, 0)           /* `__attribute__ (( ... ))`: ATTRIBUTE*; token: keyword */                                      \
    X(ATTRIBUTE_STANDARD, LIST, 0, 0)      /* `[[ ... ]]`: ATTRIBUTE*; token: first `[` */                                                  \
    X(ATTRIBUTE_DECLSPEC, LIST, 0, 0)      /* `__declspec ( ... )`: ATTRIBUTE*; token: keyword */                                           \
    X(ATTRIBUTE, LIST, 0, 0)               /* arguments (expressions or TYPE_NAME); token: name */                                          \
    X(ATTRIBUTE_SCOPED, LIST, 1, 0)        /* `ns::name`: ATTRIBUTE_NAMESPACE, arguments; token: name */                                    \
    X(ATTRIBUTE_NAMESPACE, LEAF, 0, 0)     /* token: the namespace word */                                                                  \
    /* ---- statements ---- */                                                                                                              \
    X(COMPOUND_STATEMENT, LIST, 0, 0)      /* block items; token: `{` */                                                                    \
    X(EXPRESSION_STATEMENT, FIXED, 1, 1)   /* expression; token: `;` */                                                                     \
    X(NULL_STATEMENT, LEAF, 0, 0)          /* token: `;` */                                                                                 \
    X(IF, FIXED, 2, 2)                     /* condition, then; token: keyword */                                                            \
    X(IF_ELSE, FIXED, 3, 3)                /* condition, then, else; token: keyword */                                                      \
    X(SWITCH, FIXED, 2, 2)                 /* condition, body; token: keyword */                                                            \
    X(WHILE, FIXED, 2, 2)                  /* condition, body; token: keyword */                                                            \
    X(DO_WHILE, FIXED, 2, 2)               /* body, condition; token: `do` */                                                               \
    X(FOR, PRESENCE, 1, 0)                 /* bits 0 init (DECLARATION or expression), 1 condition, 2 step, then body (last); token: kw */  \
    X(GOTO, LEAF, 0, 0)                    /* data: symbol; token: the label */                                                             \
    X(GOTO_COMPUTED, FIXED, 1, 1)          /* `goto *expr`: expression; token: keyword */                                                   \
    X(CONTINUE, LEAF, 0, 0)                /* token: keyword */                                                                             \
    X(BREAK, LEAF, 0, 0)                   /* token: keyword */                                                                             \
    X(RETURN, RANGE, 0, 1)                 /* [expression]; token: keyword */                                                               \
    X(LABELED, PRESENCE, 0, 0)             /* bits 0 ATTRIBUTE_LIST, 1 statement (C23 allows none); token: the label */                     \
    X(CASE, RANGE, 1, 2)                   /* value, [statement]; token: keyword */                                                         \
    X(CASE_RANGE, RANGE, 2, 3)             /* GNU `case lo ... hi:`: lo, hi, [statement]; token: keyword */                                 \
    X(DEFAULT, RANGE, 0, 1)                /* [statement]; token: keyword */                                                                \
    X(ATTRIBUTE_STATEMENT, FIXED, 1, 1)    /* `ATTRIBUTE_LIST ;` (fallthrough); token: `;` */                                               \
    X(ATTRIBUTED_STATEMENT, FIXED, 2, 2)   /* ATTRIBUTE_LIST, statement; token: first token */                                              \
    X(ASM, RANGE, 1, 5)                    /* STRING template, then ASM_OUTPUTS, ASM_INPUTS, ASM_CLOBBERS, ASM_LABELS as written;          \
                                              flags C_AST_ASM_*; token: keyword */                                                          \
    X(ASM_OUTPUTS, LIST, 0, 0)             /* ASM_OPERAND*; token: `:` */                                                                   \
    X(ASM_INPUTS, LIST, 0, 0)              /* ASM_OPERAND*; token: `:` */                                                                   \
    X(ASM_CLOBBERS, LIST, 0, 0)            /* STRING*; token: `:` */                                                                        \
    X(ASM_LABELS, LIST, 0, 0)              /* IDENTIFIER*; token: `:` */                                                                    \
    X(ASM_OPERAND, PRESENCE, 2, 0)         /* bit 0 ASM_SYMBOLIC_NAME (first), then STRING constraint, expression; token: constraint */    \
    X(ASM_SYMBOLIC_NAME, LEAF, 0, 0)       /* `[name]`; token: the name */                                                                  \
    /* ---- primary expressions ---- */                                                                                                     \
    X(IDENTIFIER, LEAF, 0, 0)              /* data: symbol (0 when uninterned); token: the identifier */                                    \
    X(NUMBER, LEAF, 0, 0)                  /* token: the preprocessing number (CNumberFacts are keyed by it) */                             \
    X(CHARACTER, LEAF, 0, 0)               /* token: the character literal */                                                               \
    X(STRING, LEAF, 0, 0)                  /* data: adjacent string-literal tokens (>= 1); token: the first */                              \
    X(BOOLEAN_CONSTANT, LEAF, 0, 0)        /* C23 `true`/`false`; data: 1 or 0; token: the word */                                          \
    X(NULLPTR, LEAF, 0, 0)                 /* C23 `nullptr`; token: the word */                                                             \
    X(LABEL_ADDRESS, LEAF, 0, 0)           /* GNU `&&label`; data: symbol; token: the label */                                              \
    X(STATEMENT_EXPRESSION, FIXED, 1, 1)   /* GNU `({ ... })`: COMPOUND_STATEMENT; token: `(` */                                            \
    X(GENERIC_SELECTION, LIST, 1, 0)       /* controlling expression, GENERIC_ASSOCIATION/GENERIC_DEFAULT*; token: keyword */               \
    X(GENERIC_ASSOCIATION, FIXED, 2, 2)    /* TYPE_NAME, expression; token: the type's first token */                                       \
    X(GENERIC_DEFAULT, FIXED, 1, 1)        /* expression; token: `default` */                                                               \
    /* ---- postfix ---- */                                                                                                                 \
    X(CALL, LIST, 1, 0)                    /* callee, arguments (expression, or TYPE_NAME where one starts); token: `(` */                  \
    X(INDEX, FIXED, 2, 2)                  /* base, index; token: `[` */                                                                    \
    X(MEMBER, FIXED, 1, 1)                 /* `.`: base; data: member symbol; token: member name */                                         \
    X(MEMBER_ARROW, FIXED, 1, 1)           /* `->`: base; data: member symbol; token: member name */                                        \
    X(POST_INCREMENT, FIXED, 1, 1)         /* token: operator */                                                                            \
    X(POST_DECREMENT, FIXED, 1, 1)         /* token: operator */                                                                            \
    X(COMPOUND_LITERAL, FIXED, 2, 2)       /* TYPE_NAME, INITIALIZER_LIST; token: `(` */                                                    \
    /* ---- unary (token: operator or keyword) ---- */                                                                                      \
    X(PRE_INCREMENT, FIXED, 1, 1)                                                                                                            \
    X(PRE_DECREMENT, FIXED, 1, 1)                                                                                                            \
    X(ADDRESS, FIXED, 1, 1)                                                                                                                  \
    X(DEREFERENCE, FIXED, 1, 1)                                                                                                              \
    X(PLUS, FIXED, 1, 1)                                                                                                                     \
    X(NEGATE, FIXED, 1, 1)                                                                                                                   \
    X(BIT_NOT, FIXED, 1, 1)                                                                                                                  \
    X(LOGICAL_NOT, FIXED, 1, 1)                                                                                                              \
    X(SIZEOF_EXPRESSION, FIXED, 1, 1)                                                                                                        \
    X(SIZEOF_TYPE, FIXED, 1, 1)            /* TYPE_NAME */                                                                                  \
    X(ALIGNOF_EXPRESSION, FIXED, 1, 1)     /* GNU */                                                                                        \
    X(ALIGNOF_TYPE, FIXED, 1, 1)           /* TYPE_NAME */                                                                                  \
    X(REAL, FIXED, 1, 1)                   /* GNU __real__ */                                                                               \
    X(IMAG, FIXED, 1, 1)                   /* GNU __imag__ */                                                                               \
    X(EXTENSION, FIXED, 1, 1)              /* GNU __extension__ */                                                                          \
    X(CAST, FIXED, 2, 2)                   /* TYPE_NAME, operand; token: `(` */                                                             \
    /* ---- binary (left, right; token: operator) ---- */                                                                                   \
    X(MULTIPLY, FIXED, 2, 2)                                                                                                                 \
    X(DIVIDE, FIXED, 2, 2)                                                                                                                   \
    X(REMAINDER, FIXED, 2, 2)                                                                                                                \
    X(ADD, FIXED, 2, 2)                                                                                                                      \
    X(SUBTRACT, FIXED, 2, 2)                                                                                                                 \
    X(SHIFT_LEFT, FIXED, 2, 2)                                                                                                               \
    X(SHIFT_RIGHT, FIXED, 2, 2)                                                                                                              \
    X(LESS, FIXED, 2, 2)                                                                                                                     \
    X(GREATER, FIXED, 2, 2)                                                                                                                  \
    X(LESS_EQUAL, FIXED, 2, 2)                                                                                                               \
    X(GREATER_EQUAL, FIXED, 2, 2)                                                                                                            \
    X(EQUAL, FIXED, 2, 2)                                                                                                                    \
    X(NOT_EQUAL, FIXED, 2, 2)                                                                                                                \
    X(BIT_AND, FIXED, 2, 2)                                                                                                                  \
    X(BIT_XOR, FIXED, 2, 2)                                                                                                                  \
    X(BIT_OR, FIXED, 2, 2)                                                                                                                   \
    X(LOGICAL_AND, FIXED, 2, 2)                                                                                                              \
    X(LOGICAL_OR, FIXED, 2, 2)                                                                                                               \
    X(CONDITIONAL, FIXED, 3, 3)            /* condition, then, else; token: `?` */                                                          \
    X(CONDITIONAL_OMITTED, FIXED, 2, 2)    /* GNU `a ?: b`: condition, else; token: `?` */                                                  \
    X(ASSIGN, FIXED, 2, 2)                                                                                                                   \
    X(MULTIPLY_ASSIGN, FIXED, 2, 2)                                                                                                          \
    X(DIVIDE_ASSIGN, FIXED, 2, 2)                                                                                                            \
    X(REMAINDER_ASSIGN, FIXED, 2, 2)                                                                                                         \
    X(ADD_ASSIGN, FIXED, 2, 2)                                                                                                               \
    X(SUBTRACT_ASSIGN, FIXED, 2, 2)                                                                                                          \
    X(SHIFT_LEFT_ASSIGN, FIXED, 2, 2)                                                                                                        \
    X(SHIFT_RIGHT_ASSIGN, FIXED, 2, 2)                                                                                                       \
    X(BIT_AND_ASSIGN, FIXED, 2, 2)                                                                                                           \
    X(BIT_XOR_ASSIGN, FIXED, 2, 2)                                                                                                           \
    X(BIT_OR_ASSIGN, FIXED, 2, 2)                                                                                                            \
    X(COMMA, FIXED, 2, 2)                                                                                                                    \
    /* ---- initializers ---- */                                                                                                            \
    X(INITIALIZER_LIST, LIST, 0, 0)        /* items: expression, INITIALIZER_LIST or DESIGNATION; token: `{` */                             \
    X(DESIGNATION, LIST, 0, 1)             /* designators (>= 1), then the value (last); token: first designator token */                   \
    X(DESIGNATOR_MEMBER, LEAF, 0, 0)       /* `.name`; data: symbol; token: the name */                                                     \
    X(DESIGNATOR_INDEX, FIXED, 1, 1)       /* `[index]`; token: `[` */                                                                      \
    X(DESIGNATOR_RANGE, FIXED, 2, 2)       /* GNU `[lo ... hi]`; token: `[` */

typedef enum CAstKind
{
#define C_AST_KIND_ENUMERATOR(name, contract, a, b) C_AST_##name,
    C_AST_KIND_LIST(C_AST_KIND_ENUMERATOR)
#undef C_AST_KIND_ENUMERATOR
    C_AST_KIND_COUNT,
} CAstKind;

BUSTER_CT_CHECK(C_AST_KIND_COUNT <= UINT8_MAX);

typedef enum CAstContract
{
    C_AST_CONTRACT_LEAF,
    C_AST_CONTRACT_FIXED,
    C_AST_CONTRACT_RANGE,
    C_AST_CONTRACT_PRESENCE,
    C_AST_CONTRACT_LIST,
} CAstContract;

// Flags above the presence byte.
enum
{
    C_AST_QUALIFIER_CONST = 1u << 8,
    C_AST_QUALIFIER_VOLATILE = 1u << 9,
    C_AST_QUALIFIER_RESTRICT = 1u << 10,
    C_AST_QUALIFIER_ATOMIC = 1u << 11,
    C_AST_QUALIFIER_NONNULL = 1u << 12,
    C_AST_QUALIFIER_NULLABLE = 1u << 13,
    C_AST_QUALIFIER_NULL_UNSPECIFIED = 1u << 14,
    // DECLARATOR_ARRAY: `[static n]` and the VLA `[*]`.
    C_AST_ARRAY_STATIC = 1u << 16,
    C_AST_ARRAY_STAR = 1u << 17,
    // ASM qualifiers.
    C_AST_ASM_VOLATILE = 1u << 8,
    C_AST_ASM_INLINE = 1u << 9,
    C_AST_ASM_GOTO = 1u << 10,
};

// Normalized keyword identities: every spelling of a keyword maps to one word
// (`__const__`, `__const` and `const` are CONST), decided once per build from
// the unit's symbol table and dialect. SPECIFIER_WORD carries one in data;
// the builder uses the rest to steer the parse. The token keeps the spelling.
#define C_AST_WORD_LIST(X)                                                                                                                     \
    X(NONE) /* an ordinary identifier */                                                                                                     \
    /* type specifiers */                                                                                                                    \
    X(VOID) X(CHAR) X(SHORT) X(INT) X(LONG) X(FLOAT) X(DOUBLE) X(SIGNED) X(UNSIGNED) X(BOOL) X(COMPLEX) X(IMAGINARY) X(INT128) X(FLOAT16)   \
    X(BF16) X(FLOAT128) X(BUILTIN_VA_LIST) X(AUTO_TYPE)                                                                                      \
    /* qualifiers */                                                                                                                         \
    X(CONST) X(VOLATILE) X(RESTRICT) X(ATOMIC) X(NONNULL) X(NULLABLE) X(NULL_UNSPECIFIED)                                                    \
    /* storage classes and function specifiers */                                                                                            \
    X(TYPEDEF) X(EXTERN) X(STATIC) X(AUTO) X(REGISTER) X(THREAD_LOCAL) X(CONSTEXPR) X(INLINE) X(NORETURN) X(EXTENSION)                      \
    /* structured specifiers and decorations */                                                                                              \
    X(STRUCT) X(UNION) X(ENUM) X(TYPEOF) X(TYPEOF_UNQUAL) X(ALIGNAS) X(BITINT) X(ATTRIBUTE) X(DECLSPEC) X(ASM)                             \
    /* statements */                                                                                                                         \
    X(IF) X(ELSE) X(SWITCH) X(CASE) X(DEFAULT) X(WHILE) X(DO) X(FOR) X(GOTO) X(CONTINUE) X(BREAK) X(RETURN) X(STATIC_ASSERT)              \
    /* expressions */                                                                                                                        \
    X(SIZEOF) X(ALIGNOF) X(GENERIC) X(REAL) X(IMAG) X(TRUE) X(FALSE) X(NULLPTR)

typedef enum CAstWord
{
#define C_AST_WORD_ENUMERATOR(name) C_AST_WORD_##name,
    C_AST_WORD_LIST(C_AST_WORD_ENUMERATOR)
#undef C_AST_WORD_ENUMERATOR
    C_AST_WORD_COUNT,
} CAstWord;

BUSTER_CT_CHECK(C_AST_WORD_COUNT <= UINT8_MAX);

typedef enum CAstLayout
{
    // Extents only: the topology the forward pass writes.
    C_AST_LAYOUT_IMPLICIT,
    // Implicit, plus a child-root slice for every LIST node. A LIST node's
    // data becomes the slice offset into CAst.children; the slice is
    // [count, root...] over all its children in source order.
    C_AST_LAYOUT_HYBRID,
    // The explicit-index control: implicit, plus CAst.slices[n] for every node
    // with children (UINT32_MAX for leaves), each a slice as above. Data keeps
    // the implicit meaning.
    C_AST_LAYOUT_EXPLICIT,
    C_AST_LAYOUT_COUNT,
} CAstLayout;

typedef struct CAstOptions CAstOptions;
struct CAstOptions
{
    // Phase arena for transient builder state; null creates and retires a
    // private one.
    Arena* phase_arena;
    CAstLayout layout;
    // Tokens per cursor refill. 0 borrows the whole final stream in place (the
    // production setting); any other value copies the stream through a ring
    // window of that many tokens (at least C_AST_LOOKAHEAD), the setting tests
    // use to prove refill-boundary invariance.
    u32 refill_batch;
    // Nodes the first column reservations hold. 0 is the production setting,
    // C_AST_COLUMN_CAPACITY; tests set a small value so ordinary trees outgrow
    // it and move.
    u32 column_capacity;
};

// Work counts. Counters are live only when C_AST_COUNTERS is nonzero (test
// builds); timed builds compile them out and leave these fields zero, except
// for the sizes, which are always reported.
typedef struct CAstStatistics CAstStatistics;
struct CAstStatistics
{
    // Always reported.
    u64 node_count;
    u64 retained_bytes;      // the columns (13 bytes per node), plus children/slices in HYBRID and EXPLICIT
    u64 transient_high_water; // peak builder bytes in the phase arena: frames, stacks, bindings, refill ring
    u64 column_copy_bytes;   // bytes copied moving the columns to larger reservations; 0 below C_AST_COLUMN_CAPACITY
    u64 finalize_child_entries; // slice entries written by HYBRID/EXPLICIT finalization
    // Counted only under C_AST_COUNTERS.
    u64 tokens_consumed;
    u64 token_peeks;
    u64 refills;
    u64 frames_pushed;
    u64 frame_high_water;
    u64 operator_high_water;
    u64 binding_lookups;
    u64 bindings_published;
};

// The private arenas that hold a built tree's kinds, extents, tokens and data
// columns. One record per build, in the caller's arena; every copy of the CAst
// points at it, so c_ast_release through any copy retires them once.
typedef struct CAstStorage CAstStorage;

struct CAst
{
    // In the arenas of `storage`, not in the caller's arena.
    u8* kinds;
    u32* extents;
    u32* tokens;
    u32* data;
    // HYBRID and EXPLICIT: slice storage. EXPLICIT also fills `slices`.
    u32* children;
    u32* slices;
    u32 node_count;
    u32 children_count;
    // node_count - 1 when complete: the TRANSLATION_UNIT.
    u32 root;
    CAstLayout layout;
    // Null for an empty tree, a failed build or a released one.
    CAstStorage* storage;
};

typedef struct CAstResult CAstResult;
struct CAstResult
{
    CAst ast;
    CAstStatistics statistics;
    CDiagnostic* diagnostics;
    u32 diagnostic_count;
    // False after any syntax error; the tree is then empty (node_count 0), so
    // a failed build never publishes a partial tree.
    bool complete;
};

BUSTER_F_DECL CAstResult c_ast_build(Arena* arena, CPreprocessResult preprocess, CAstOptions options);
// Ends the use of a built tree's column arenas. Each returns every committed
// page beyond C_PHASE_ARENA_RETAINED_SIZE to the OS and parks its reservation
// in the calling thread's reuse pool, which must be the thread that built the
// tree (docs/agents/parallelism.md). Afterwards `ast` is empty and the columns
// every other copy points at are gone; the caller's arena keeps the storage
// record and any HYBRID/EXPLICIT slices. Idempotent through any copy, and a
// no-op for an empty tree.
BUSTER_F_DECL void c_ast_release(CAst* ast);

// Checks every node's contract, extent tiling and containment, layout slices
// against the implicit topology, and that the root spans the tree. Returns
// C_AST_NODE_INVALID when the tree is well formed, else the first offending
// node.
BUSTER_F_DECL u32 c_ast_validate(CAst const* ast);

// An S-expression of the subtree at `node`, for tests and debugging. Each node
// prints as `(kind_name` [` ` annotation] [` ` child]... `)` in source order,
// where kind_name is the C_AST_KIND_LIST name in lower case and the
// annotation is:
//   IDENTIFIER, TYPEDEF_NAME, TAG_NAME, DECLARATOR_NAME, MEMBER, MEMBER_ARROW,
//   GOTO, LABELED, ENUMERATOR, DESIGNATOR_MEMBER, LABEL_ADDRESS, ATTRIBUTE,
//   ATTRIBUTE_SCOPED, ATTRIBUTE_NAMESPACE, ASM_SYMBOLIC_NAME, NUMBER,
//   CHARACTER, BOOLEAN_CONSTANT
//                       the anchor token's spelling
//   STRING              every token of the run, separated by one space
//   SPECIFIER_WORD      c_ast_word_name of the word (normalized spelling)
//   DECLARATOR_POINTER, DECLARATOR_ARRAY
//                       the set flags among const volatile restrict _Atomic
//                       _Nonnull _Nullable _Null_unspecified static *, in
//                       that order
//   ASM                 the set flags among volatile inline goto
// and other kinds print none.
BUSTER_F_DECL String8 c_ast_dump(Arena* arena, CAst const* ast, CPreprocessResult preprocess, u32 node);

BUSTER_F_DECL String8 c_ast_kind_name(CAstKind kind);
BUSTER_F_DECL String8 c_ast_word_name(CAstWord word);
BUSTER_F_DECL CAstContract c_ast_kind_contract(CAstKind kind);

// The first node of n's subtree.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 c_ast_subtree_begin(CAst const* ast, u32 node)
{
    return node + 1 - ast->extents[node];
}

// Direct child count: a slice read in HYBRID/EXPLICIT where one exists, else
// a backward walk over the children's extents.
BUSTER_F_DECL u32 c_ast_child_count(CAst const* ast, u32 node);
// The variadic middle of a LIST node.
BUSTER_F_DECL u32 c_ast_list_count(CAst const* ast, u32 node);
// The index-th child in source order, or C_AST_NODE_INVALID.
BUSTER_F_DECL u32 c_ast_child_at(CAst const* ast, u32 node, u32 index);
// Writes up to `capacity` child roots in source order; returns the child
// count (which may exceed capacity).
BUSTER_F_DECL u32 c_ast_children(CAst const* ast, u32 node, u32* roots, u32 capacity);

// Source-order traversal with enter and exit events on an explicit stack: an
// interior node is entered before its children and exited after them; a leaf
// yields only C_AST_WALK_ENTER. A consumer that needs context before a child
// (a branch, a short-circuit arm, an unevaluated operand) acts on enter; one
// that needs operand results acts on exit.
typedef enum CAstWalkEvent
{
    C_AST_WALK_DONE,
    C_AST_WALK_ENTER,
    C_AST_WALK_EXIT,
} CAstWalkEvent;

typedef struct CAstWalk CAstWalk;
struct CAstWalk
{
    CAst const* ast;
    // Entries are node indices; the high bit marks a pending exit.
    u32* stack;
    u32 count;
    u32 capacity;
    Arena* arena;
    u64 steps;
};

BUSTER_F_DECL CAstWalk c_ast_walk_begin(Arena* arena, CAst const* ast, u32 node);
BUSTER_F_DECL CAstWalkEvent c_ast_walk_next(CAstWalk* walk, u32* node_out);
