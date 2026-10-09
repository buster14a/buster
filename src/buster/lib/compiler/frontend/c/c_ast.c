// The implicit postorder C syntax tree (GitHub #3102): the forward builder, the
// sealed columns and everything that reads them back. c_ast.h is the contract
// (node kinds, child contracts, payloads, layouts); this file owns how one
// forward pass over the final token stream produces it.
//
// Ownership. c_ast_build owns the builder, the explicit stacks and the growth
// chunks in a phase arena (CAstOptions.phase_arena, else a private one) and
// releases them before it returns. The sealed CAst lives in the caller's
// arena; so do the diagnostics. The tree references the preprocessing result's
// tokens and symbols by index, so that result must outlive it. The only shared
// state a build writes is the unit's symbol table, and only to intern the
// spelling of a token the intern pass never saw (hand-built streams).
//
// Shape of the pass. The parser never recurses on the C stack. Every open
// construct is a small frame (CAstFrame, 32 bytes) on one explicit stack, and
// c_ast_run is a loop that dispatches the top frame to its c_ast_step_*
// function. A step advances its frame by one or more states, may push child
// frames, and returns to the loop; a frame that is done appends its node (the
// parent comes after its children, extent = append position + 1 - begin) and
// pops. Steps never call each other, and a step never touches its frame after
// pushing (the stack may have moved). Expressions are a shunting-yard over an
// explicit operator stack (CAstOperator) inside one frame: parentheses and the
// `?` of a conditional are marker entries, so nesting costs stack entries and
// not frames. The builder never reads a published node back: everything a
// later decision needs travels in frames and in the ret_* return registers a
// popped frame leaves for its parent. No decision reads more than
// C_AST_LOOKAHEAD tokens.
//
// Names. A symbol-indexed byte table (CAstBuilder.info) answers, with one
// load per identifier, whether it is a keyword (a CAstWord), a typedef name,
// or a declared ordinary name; declarations write it at their declaration
// points and scope exits undo it. Keywords are never rebound.
//
// Layout map (search these symbols):
//   c_ast_word_spellings, c_ast_info_flags       keyword table and classes
//   c_ast_binary_info, c_ast_prefix_kinds        operator tables
//   CAstBuilder, CAstFrame, CAstOperator         builder state and records
//   c_ast_peek, c_ast_advance                    token cursor (alias or ring)
//   c_ast_append, c_ast_chunk_next, c_ast_seal   node columns: append and seal
//   c_ast_bind, c_ast_scope_push/pop             typedef-name bindings
//   c_ast_info, c_ast_type_name_at               classification
//   c_ast_fail, c_ast_fail_expected              the one structured diagnostic
//   c_ast_step_expr, c_ast_step_call,
//   c_ast_step_generic                           expression frames
//   c_ast_step_init_list, c_ast_step_designation initializers
//   c_ast_step_specs, c_ast_step_keyword_paren,
//   c_ast_step_type_name                         specifiers and type-names
//   c_ast_step_declarator, c_ast_step_array_suffix,
//   c_ast_step_function_suffix,
//   c_ast_step_param_list, c_ast_step_param      declarators and parameters
//   c_ast_step_init_decl, c_ast_step_decl        declarations, function bodies
//   c_ast_step_struct, c_ast_step_member_list,
//   c_ast_step_member_decl,
//   c_ast_step_member_declarator, c_ast_step_enum,
//   c_ast_step_enumerator_list, c_ast_step_enumerator   tags
//   c_ast_step_attr_list, c_ast_step_attr_group  attributes
//   c_ast_step_static_assert, c_ast_step_asm     static assertions, assembly
//   c_ast_start_statement, c_ast_step_block,
//   c_ast_step_if .. c_ast_step_simple_statement statements
//   c_ast_step_translation_unit, c_ast_run       the root frame and the loop
//   c_ast_builder_init, c_ast_build              setup and the entry point
//   c_ast_finalize_layout                        HYBRID and EXPLICIT slices
//   c_ast_child_count .. c_ast_walk_next         accessors and traversal
//   c_ast_validate, c_ast_dump                   checker and S-expression dump

#include "c_internal.h"
#include <buster/lib/compiler/frontend/c/c_ast.h>

#ifndef C_AST_COUNTERS
#define C_AST_COUNTERS BUSTER_INCLUDE_TESTS
#endif

#if C_AST_COUNTERS
#define C_AST_COUNT(builder, field, amount) ((builder)->statistics.field += (u64)(amount))
#else
#define C_AST_COUNT(builder, field, amount) ((void)(builder))
#endif

// Past this the node index could not stay below C_AST_NODE_INVALID.
#define C_AST_NODE_LIMIT 0xffff0000u

enum
{
    // Nodes per growth chunk; a power of two so the in-chunk slot is a mask.
    C_AST_CHUNK_SHIFT = 14,
    C_AST_CHUNK_NODES = 1 << C_AST_CHUNK_SHIFT,
    C_AST_CHUNK_MASK = C_AST_CHUNK_NODES - 1,
    // Bytes of one chunk: the kind byte plus three u32 columns per node.
    C_AST_NODE_BYTES = 1 + 3 * 4,
    C_AST_CHUNK_BYTES = C_AST_CHUNK_NODES * C_AST_NODE_BYTES,
    C_AST_INITIAL_STACK = 256,
};

// What the symbol-indexed table says about an identifier. 0 is an identifier
// no declaration has bound, 1..C_AST_WORD_COUNT-1 is a keyword (a CAstWord, the
// value never bound), and the two top values are the binding of a declared
// name. One byte load answers keyword, typedef name and shadowed name.
#define C_AST_INFO_TYPEDEF 0xfeu
#define C_AST_INFO_ORDINARY 0xffu
BUSTER_CT_CHECK(C_AST_WORD_COUNT < C_AST_INFO_TYPEDEF);
BUSTER_CT_CHECK(C_AST_KIND_COUNT < 240);

// Operator-stack kinds that are not node kinds: the two markers that stop a
// reduction.
#define C_AST_OP_PAREN 250u
#define C_AST_OP_QUESTION 251u

enum
{
    C_AST_PREC_NONE = 0,
    C_AST_PREC_COMMA = 1,
    C_AST_PREC_ASSIGN = 2,
    C_AST_PREC_CONDITIONAL = 3,
    C_AST_PREC_LOGICAL_OR = 4,
    C_AST_PREC_LOGICAL_AND = 5,
    C_AST_PREC_BIT_OR = 6,
    C_AST_PREC_BIT_XOR = 7,
    C_AST_PREC_BIT_AND = 8,
    C_AST_PREC_EQUALITY = 9,
    C_AST_PREC_RELATIONAL = 10,
    C_AST_PREC_SHIFT = 11,
    C_AST_PREC_ADDITIVE = 12,
    C_AST_PREC_MULTIPLICATIVE = 13,
    C_AST_PREC_PREFIX = 14,
};

// ---- keywords -------------------------------------------------------------

typedef enum CAstGate
{
    C_AST_GATE_ALWAYS,
    C_AST_GATE_GNU,
    C_AST_GATE_C23,
    C_AST_GATE_GNU_OR_C23,
} CAstGate;

typedef struct CAstWordSpelling CAstWordSpelling;
struct CAstWordSpelling
{
    String8 spelling;
    u8 word;
    u8 gate;
};

#define C_AST_W(text, word, gate) {S8_INITIALIZER(text), (u8)C_AST_WORD_##word, (u8)C_AST_GATE_##gate}

// Every spelling the existing frontend's keyword predicates accept
// (c_declaration_keyword_for_dialect, c_parse_type_word_for_dialect,
// c_parse_type_qualifier_word, c_parse_alignof_word) with the same dialect
// gates, plus the statement and expression words the syntax steers on. The
// table is searched by spelling only for the symbols the unit's table already
// holds (the build) and for tokens the intern pass never saw (the slow path).
BUSTER_GLOBAL_LOCAL CAstWordSpelling const c_ast_word_spellings[] = {
    C_AST_W("void", VOID, ALWAYS),
    C_AST_W("char", CHAR, ALWAYS),
    C_AST_W("short", SHORT, ALWAYS),
    C_AST_W("int", INT, ALWAYS),
    C_AST_W("long", LONG, ALWAYS),
    C_AST_W("float", FLOAT, ALWAYS),
    C_AST_W("double", DOUBLE, ALWAYS),
    C_AST_W("signed", SIGNED, ALWAYS),
    C_AST_W("__signed", SIGNED, ALWAYS),
    C_AST_W("__signed__", SIGNED, ALWAYS),
    C_AST_W("unsigned", UNSIGNED, ALWAYS),
    C_AST_W("_Bool", BOOL, ALWAYS),
    C_AST_W("bool", BOOL, C23),
    C_AST_W("_Complex", COMPLEX, ALWAYS),
    C_AST_W("__complex", COMPLEX, ALWAYS),
    C_AST_W("__complex__", COMPLEX, ALWAYS),
    C_AST_W("_Imaginary", IMAGINARY, ALWAYS),
    C_AST_W("__int128", INT128, ALWAYS),
    C_AST_W("__int8", INT8, ALWAYS),
    C_AST_W("_Float16", FLOAT16, ALWAYS),
    C_AST_W("__bf16", BF16, ALWAYS),
    // GNU and Clang's builtin binary128 word. `_Float128`, `_Float64x` and
    // `_Float128x` stay identifiers: glibc's <bits/floatn.h> typedefs them
    // for the compiler identity this preprocessor reports.
    C_AST_W("__float128", FLOAT128, ALWAYS),
    C_AST_W("__builtin_va_list", BUILTIN_VA_LIST, ALWAYS),
    C_AST_W("__auto_type", AUTO_TYPE, ALWAYS),
    C_AST_W("const", CONST, ALWAYS),
    C_AST_W("__const", CONST, ALWAYS),
    C_AST_W("__const__", CONST, ALWAYS),
    C_AST_W("volatile", VOLATILE, ALWAYS),
    C_AST_W("__volatile", VOLATILE, ALWAYS),
    C_AST_W("__volatile__", VOLATILE, ALWAYS),
    C_AST_W("restrict", RESTRICT, ALWAYS),
    C_AST_W("__restrict", RESTRICT, ALWAYS),
    C_AST_W("__restrict__", RESTRICT, ALWAYS),
    C_AST_W("_Atomic", ATOMIC, ALWAYS),
    C_AST_W("_Nonnull", NONNULL, ALWAYS),
    C_AST_W("_Nullable", NULLABLE, ALWAYS),
    C_AST_W("_Null_unspecified", NULL_UNSPECIFIED, ALWAYS),
    C_AST_W("typedef", TYPEDEF, ALWAYS),
    C_AST_W("extern", EXTERN, ALWAYS),
    C_AST_W("static", STATIC, ALWAYS),
    C_AST_W("auto", AUTO, ALWAYS),
    C_AST_W("register", REGISTER, ALWAYS),
    C_AST_W("_Thread_local", THREAD_LOCAL, ALWAYS),
    C_AST_W("__thread", THREAD_LOCAL, ALWAYS),
    C_AST_W("thread_local", THREAD_LOCAL, C23),
    C_AST_W("constexpr", CONSTEXPR, C23),
    C_AST_W("inline", INLINE, ALWAYS),
    C_AST_W("__inline", INLINE, ALWAYS),
    C_AST_W("__inline__", INLINE, ALWAYS),
    C_AST_W("_Noreturn", NORETURN, ALWAYS),
    C_AST_W("__extension__", EXTENSION, ALWAYS),
    C_AST_W("struct", STRUCT, ALWAYS),
    C_AST_W("union", UNION, ALWAYS),
    C_AST_W("enum", ENUM, ALWAYS),
    C_AST_W("typeof", TYPEOF, GNU_OR_C23),
    C_AST_W("__typeof", TYPEOF, ALWAYS),
    C_AST_W("__typeof__", TYPEOF, ALWAYS),
    C_AST_W("typeof_unqual", TYPEOF_UNQUAL, C23),
    C_AST_W("_Alignas", ALIGNAS, ALWAYS),
    C_AST_W("alignas", ALIGNAS, C23),
    C_AST_W("_BitInt", BITINT, GNU_OR_C23),
    C_AST_W("__attribute__", ATTRIBUTE, ALWAYS),
    C_AST_W("__attribute", ATTRIBUTE, ALWAYS),
    C_AST_W("__declspec", DECLSPEC, ALWAYS),
    C_AST_W("asm", ASM, GNU),
    C_AST_W("__asm", ASM, ALWAYS),
    C_AST_W("__asm__", ASM, ALWAYS),
    C_AST_W("if", IF, ALWAYS),
    C_AST_W("else", ELSE, ALWAYS),
    C_AST_W("switch", SWITCH, ALWAYS),
    C_AST_W("case", CASE, ALWAYS),
    C_AST_W("default", DEFAULT, ALWAYS),
    C_AST_W("while", WHILE, ALWAYS),
    C_AST_W("do", DO, ALWAYS),
    C_AST_W("for", FOR, ALWAYS),
    C_AST_W("goto", GOTO, ALWAYS),
    C_AST_W("continue", CONTINUE, ALWAYS),
    C_AST_W("break", BREAK, ALWAYS),
    C_AST_W("return", RETURN, ALWAYS),
    C_AST_W("_Static_assert", STATIC_ASSERT, ALWAYS),
    C_AST_W("static_assert", STATIC_ASSERT, C23),
    C_AST_W("sizeof", SIZEOF, ALWAYS),
    C_AST_W("_Alignof", ALIGNOF, ALWAYS),
    C_AST_W("__alignof", ALIGNOF, ALWAYS),
    C_AST_W("__alignof__", ALIGNOF, ALWAYS),
    C_AST_W("alignof", ALIGNOF, C23),
    C_AST_W("_Generic", GENERIC, ALWAYS),
    C_AST_W("__real__", REAL, ALWAYS),
    C_AST_W("__real", REAL, ALWAYS),
    C_AST_W("__imag__", IMAG, ALWAYS),
    C_AST_W("__imag", IMAG, ALWAYS),
    C_AST_W("true", TRUE, C23),
    C_AST_W("false", FALSE, C23),
    C_AST_W("nullptr", NULLPTR, C23),
};

// Word classes, indexed by the info byte.
enum
{
    // May begin a type-name inside parentheses: type words, qualifiers,
    // storage words (C23 compound literals), tags, typeof, alignas, attribute
    // groups and, by binding, typedef names. `__extension__` is decided by
    // c_ast_type_name_at, which looks past it.
    C_AST_FLAG_TYPE = 1,
    // May begin a declaration at a block item or after a declarator (K&R).
    // Not attributes and not __extension__: those have their own lookahead.
    C_AST_FLAG_DECL = 2,
    // A simple specifier word the specifier loop emits as a SPECIFIER_WORD.
    C_AST_FLAG_SPEC = 4,
    // Counts as the type specifier that ends typedef-name recognition.
    C_AST_FLAG_TYPE_WORD = 8,
};

#define C_AST_CLASS_TYPE_WORD (C_AST_FLAG_TYPE | C_AST_FLAG_DECL | C_AST_FLAG_SPEC | C_AST_FLAG_TYPE_WORD)
#define C_AST_CLASS_MODIFIER (C_AST_FLAG_TYPE | C_AST_FLAG_DECL | C_AST_FLAG_SPEC)
#define C_AST_CLASS_STRUCTURED (C_AST_FLAG_TYPE | C_AST_FLAG_DECL)

BUSTER_GLOBAL_LOCAL u8 const c_ast_info_flags[256] = {
    [C_AST_WORD_VOID] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_CHAR] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_SHORT] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_INT] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_LONG] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_FLOAT] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_DOUBLE] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_SIGNED] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_UNSIGNED] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_BOOL] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_COMPLEX] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_IMAGINARY] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_INT128] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_INT8] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_FLOAT16] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_BF16] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_FLOAT128] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_BUILTIN_VA_LIST] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_AUTO_TYPE] = C_AST_CLASS_TYPE_WORD,
    [C_AST_WORD_CONST] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_VOLATILE] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_RESTRICT] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_ATOMIC] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_NONNULL] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_NULLABLE] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_NULL_UNSPECIFIED] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_TYPEDEF] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_EXTERN] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_STATIC] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_AUTO] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_REGISTER] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_THREAD_LOCAL] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_CONSTEXPR] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_INLINE] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_NORETURN] = C_AST_CLASS_MODIFIER,
    [C_AST_WORD_EXTENSION] = C_AST_FLAG_SPEC,
    [C_AST_WORD_STRUCT] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_UNION] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_ENUM] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_TYPEOF] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_TYPEOF_UNQUAL] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_ALIGNAS] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_BITINT] = C_AST_CLASS_STRUCTURED,
    [C_AST_WORD_ATTRIBUTE] = C_AST_FLAG_TYPE,
    [C_AST_WORD_DECLSPEC] = C_AST_FLAG_TYPE,
    [C_AST_INFO_TYPEDEF] = C_AST_CLASS_STRUCTURED,
};

// Normalized spelling of each word (the dump of SPECIFIER_WORD): the C11
// spelling where the language has one, the GNU spelling otherwise.
BUSTER_GLOBAL_LOCAL String8 const c_ast_word_names[C_AST_WORD_COUNT] = {
    [C_AST_WORD_NONE] = S8_INITIALIZER("none"),
    [C_AST_WORD_VOID] = S8_INITIALIZER("void"),
    [C_AST_WORD_CHAR] = S8_INITIALIZER("char"),
    [C_AST_WORD_SHORT] = S8_INITIALIZER("short"),
    [C_AST_WORD_INT] = S8_INITIALIZER("int"),
    [C_AST_WORD_LONG] = S8_INITIALIZER("long"),
    [C_AST_WORD_FLOAT] = S8_INITIALIZER("float"),
    [C_AST_WORD_DOUBLE] = S8_INITIALIZER("double"),
    [C_AST_WORD_SIGNED] = S8_INITIALIZER("signed"),
    [C_AST_WORD_UNSIGNED] = S8_INITIALIZER("unsigned"),
    [C_AST_WORD_BOOL] = S8_INITIALIZER("_Bool"),
    [C_AST_WORD_COMPLEX] = S8_INITIALIZER("_Complex"),
    [C_AST_WORD_IMAGINARY] = S8_INITIALIZER("_Imaginary"),
    [C_AST_WORD_INT128] = S8_INITIALIZER("__int128"),
    [C_AST_WORD_INT8] = S8_INITIALIZER("__int8"),
    [C_AST_WORD_FLOAT16] = S8_INITIALIZER("_Float16"),
    [C_AST_WORD_BF16] = S8_INITIALIZER("__bf16"),
    [C_AST_WORD_FLOAT128] = S8_INITIALIZER("__float128"),
    [C_AST_WORD_BUILTIN_VA_LIST] = S8_INITIALIZER("__builtin_va_list"),
    [C_AST_WORD_AUTO_TYPE] = S8_INITIALIZER("__auto_type"),
    [C_AST_WORD_CONST] = S8_INITIALIZER("const"),
    [C_AST_WORD_VOLATILE] = S8_INITIALIZER("volatile"),
    [C_AST_WORD_RESTRICT] = S8_INITIALIZER("restrict"),
    [C_AST_WORD_ATOMIC] = S8_INITIALIZER("_Atomic"),
    [C_AST_WORD_NONNULL] = S8_INITIALIZER("_Nonnull"),
    [C_AST_WORD_NULLABLE] = S8_INITIALIZER("_Nullable"),
    [C_AST_WORD_NULL_UNSPECIFIED] = S8_INITIALIZER("_Null_unspecified"),
    [C_AST_WORD_TYPEDEF] = S8_INITIALIZER("typedef"),
    [C_AST_WORD_EXTERN] = S8_INITIALIZER("extern"),
    [C_AST_WORD_STATIC] = S8_INITIALIZER("static"),
    [C_AST_WORD_AUTO] = S8_INITIALIZER("auto"),
    [C_AST_WORD_REGISTER] = S8_INITIALIZER("register"),
    [C_AST_WORD_THREAD_LOCAL] = S8_INITIALIZER("_Thread_local"),
    [C_AST_WORD_CONSTEXPR] = S8_INITIALIZER("constexpr"),
    [C_AST_WORD_INLINE] = S8_INITIALIZER("inline"),
    [C_AST_WORD_NORETURN] = S8_INITIALIZER("_Noreturn"),
    [C_AST_WORD_EXTENSION] = S8_INITIALIZER("__extension__"),
    [C_AST_WORD_STRUCT] = S8_INITIALIZER("struct"),
    [C_AST_WORD_UNION] = S8_INITIALIZER("union"),
    [C_AST_WORD_ENUM] = S8_INITIALIZER("enum"),
    [C_AST_WORD_TYPEOF] = S8_INITIALIZER("typeof"),
    [C_AST_WORD_TYPEOF_UNQUAL] = S8_INITIALIZER("typeof_unqual"),
    [C_AST_WORD_ALIGNAS] = S8_INITIALIZER("_Alignas"),
    [C_AST_WORD_BITINT] = S8_INITIALIZER("_BitInt"),
    [C_AST_WORD_ATTRIBUTE] = S8_INITIALIZER("__attribute__"),
    [C_AST_WORD_DECLSPEC] = S8_INITIALIZER("__declspec"),
    [C_AST_WORD_ASM] = S8_INITIALIZER("asm"),
    [C_AST_WORD_IF] = S8_INITIALIZER("if"),
    [C_AST_WORD_ELSE] = S8_INITIALIZER("else"),
    [C_AST_WORD_SWITCH] = S8_INITIALIZER("switch"),
    [C_AST_WORD_CASE] = S8_INITIALIZER("case"),
    [C_AST_WORD_DEFAULT] = S8_INITIALIZER("default"),
    [C_AST_WORD_WHILE] = S8_INITIALIZER("while"),
    [C_AST_WORD_DO] = S8_INITIALIZER("do"),
    [C_AST_WORD_FOR] = S8_INITIALIZER("for"),
    [C_AST_WORD_GOTO] = S8_INITIALIZER("goto"),
    [C_AST_WORD_CONTINUE] = S8_INITIALIZER("continue"),
    [C_AST_WORD_BREAK] = S8_INITIALIZER("break"),
    [C_AST_WORD_RETURN] = S8_INITIALIZER("return"),
    [C_AST_WORD_STATIC_ASSERT] = S8_INITIALIZER("_Static_assert"),
    [C_AST_WORD_SIZEOF] = S8_INITIALIZER("sizeof"),
    [C_AST_WORD_ALIGNOF] = S8_INITIALIZER("_Alignof"),
    [C_AST_WORD_GENERIC] = S8_INITIALIZER("_Generic"),
    [C_AST_WORD_REAL] = S8_INITIALIZER("__real__"),
    [C_AST_WORD_IMAG] = S8_INITIALIZER("__imag__"),
    [C_AST_WORD_TRUE] = S8_INITIALIZER("true"),
    [C_AST_WORD_FALSE] = S8_INITIALIZER("false"),
    [C_AST_WORD_NULLPTR] = S8_INITIALIZER("nullptr"),
};

BUSTER_GLOBAL_LOCAL String8 const c_ast_kind_names[C_AST_KIND_COUNT] = {
    [C_AST_TRANSLATION_UNIT] = S8_INITIALIZER("translation_unit"),
    [C_AST_DECLARATION] = S8_INITIALIZER("declaration"),
    [C_AST_FUNCTION_DEFINITION] = S8_INITIALIZER("function_definition"),
    [C_AST_EMPTY_DECLARATION] = S8_INITIALIZER("empty_declaration"),
    [C_AST_STATIC_ASSERT] = S8_INITIALIZER("static_assert"),
    [C_AST_ASM_TOP_LEVEL] = S8_INITIALIZER("asm_top_level"),
    [C_AST_PRAGMA] = S8_INITIALIZER("pragma"),
    [C_AST_DECL_SPECIFIERS] = S8_INITIALIZER("decl_specifiers"),
    [C_AST_SPECIFIER_WORD] = S8_INITIALIZER("specifier_word"),
    [C_AST_TYPEDEF_NAME] = S8_INITIALIZER("typedef_name"),
    [C_AST_STRUCT_SPECIFIER] = S8_INITIALIZER("struct_specifier"),
    [C_AST_UNION_SPECIFIER] = S8_INITIALIZER("union_specifier"),
    [C_AST_ENUM_SPECIFIER] = S8_INITIALIZER("enum_specifier"),
    [C_AST_TAG_NAME] = S8_INITIALIZER("tag_name"),
    [C_AST_MEMBER_LIST] = S8_INITIALIZER("member_list"),
    [C_AST_MEMBER_DECLARATION] = S8_INITIALIZER("member_declaration"),
    [C_AST_MEMBER_DECLARATOR] = S8_INITIALIZER("member_declarator"),
    [C_AST_ENUMERATOR_LIST] = S8_INITIALIZER("enumerator_list"),
    [C_AST_ENUMERATOR] = S8_INITIALIZER("enumerator"),
    [C_AST_TYPEOF] = S8_INITIALIZER("typeof"),
    [C_AST_TYPEOF_UNQUAL] = S8_INITIALIZER("typeof_unqual"),
    [C_AST_ATOMIC_SPECIFIER] = S8_INITIALIZER("atomic_specifier"),
    [C_AST_ALIGNAS] = S8_INITIALIZER("alignas"),
    [C_AST_BITINT] = S8_INITIALIZER("bitint"),
    [C_AST_INIT_DECLARATOR] = S8_INITIALIZER("init_declarator"),
    [C_AST_ASM_LABEL] = S8_INITIALIZER("asm_label"),
    [C_AST_DECLARATOR_NAME] = S8_INITIALIZER("declarator_name"),
    [C_AST_DECLARATOR_POINTER] = S8_INITIALIZER("declarator_pointer"),
    [C_AST_DECLARATOR_ARRAY] = S8_INITIALIZER("declarator_array"),
    [C_AST_DECLARATOR_FUNCTION] = S8_INITIALIZER("declarator_function"),
    [C_AST_PARAMETER_LIST] = S8_INITIALIZER("parameter_list"),
    [C_AST_PARAMETER_LIST_VARIADIC] = S8_INITIALIZER("parameter_list_variadic"),
    [C_AST_IDENTIFIER_LIST] = S8_INITIALIZER("identifier_list"),
    [C_AST_PARAMETER] = S8_INITIALIZER("parameter"),
    [C_AST_TYPE_NAME] = S8_INITIALIZER("type_name"),
    [C_AST_ATTRIBUTE_LIST] = S8_INITIALIZER("attribute_list"),
    [C_AST_ATTRIBUTE_GNU] = S8_INITIALIZER("attribute_gnu"),
    [C_AST_ATTRIBUTE_STANDARD] = S8_INITIALIZER("attribute_standard"),
    [C_AST_ATTRIBUTE_DECLSPEC] = S8_INITIALIZER("attribute_declspec"),
    [C_AST_ATTRIBUTE] = S8_INITIALIZER("attribute"),
    [C_AST_ATTRIBUTE_SCOPED] = S8_INITIALIZER("attribute_scoped"),
    [C_AST_ATTRIBUTE_NAMESPACE] = S8_INITIALIZER("attribute_namespace"),
    [C_AST_COMPOUND_STATEMENT] = S8_INITIALIZER("compound_statement"),
    [C_AST_EXPRESSION_STATEMENT] = S8_INITIALIZER("expression_statement"),
    [C_AST_NULL_STATEMENT] = S8_INITIALIZER("null_statement"),
    [C_AST_IF] = S8_INITIALIZER("if"),
    [C_AST_IF_ELSE] = S8_INITIALIZER("if_else"),
    [C_AST_SWITCH] = S8_INITIALIZER("switch"),
    [C_AST_WHILE] = S8_INITIALIZER("while"),
    [C_AST_DO_WHILE] = S8_INITIALIZER("do_while"),
    [C_AST_FOR] = S8_INITIALIZER("for"),
    [C_AST_GOTO] = S8_INITIALIZER("goto"),
    [C_AST_GOTO_COMPUTED] = S8_INITIALIZER("goto_computed"),
    [C_AST_CONTINUE] = S8_INITIALIZER("continue"),
    [C_AST_BREAK] = S8_INITIALIZER("break"),
    [C_AST_RETURN] = S8_INITIALIZER("return"),
    [C_AST_LABELED] = S8_INITIALIZER("labeled"),
    [C_AST_CASE] = S8_INITIALIZER("case"),
    [C_AST_CASE_RANGE] = S8_INITIALIZER("case_range"),
    [C_AST_DEFAULT] = S8_INITIALIZER("default"),
    [C_AST_ATTRIBUTE_STATEMENT] = S8_INITIALIZER("attribute_statement"),
    [C_AST_ATTRIBUTED_STATEMENT] = S8_INITIALIZER("attributed_statement"),
    [C_AST_ASM] = S8_INITIALIZER("asm"),
    [C_AST_ASM_OUTPUTS] = S8_INITIALIZER("asm_outputs"),
    [C_AST_ASM_INPUTS] = S8_INITIALIZER("asm_inputs"),
    [C_AST_ASM_CLOBBERS] = S8_INITIALIZER("asm_clobbers"),
    [C_AST_ASM_LABELS] = S8_INITIALIZER("asm_labels"),
    [C_AST_ASM_OPERAND] = S8_INITIALIZER("asm_operand"),
    [C_AST_ASM_SYMBOLIC_NAME] = S8_INITIALIZER("asm_symbolic_name"),
    [C_AST_IDENTIFIER] = S8_INITIALIZER("identifier"),
    [C_AST_NUMBER] = S8_INITIALIZER("number"),
    [C_AST_CHARACTER] = S8_INITIALIZER("character"),
    [C_AST_STRING] = S8_INITIALIZER("string"),
    [C_AST_BOOLEAN_CONSTANT] = S8_INITIALIZER("boolean_constant"),
    [C_AST_NULLPTR] = S8_INITIALIZER("nullptr"),
    [C_AST_LABEL_ADDRESS] = S8_INITIALIZER("label_address"),
    [C_AST_STATEMENT_EXPRESSION] = S8_INITIALIZER("statement_expression"),
    [C_AST_GENERIC_SELECTION] = S8_INITIALIZER("generic_selection"),
    [C_AST_GENERIC_ASSOCIATION] = S8_INITIALIZER("generic_association"),
    [C_AST_GENERIC_DEFAULT] = S8_INITIALIZER("generic_default"),
    [C_AST_CALL] = S8_INITIALIZER("call"),
    [C_AST_INDEX] = S8_INITIALIZER("index"),
    [C_AST_MEMBER] = S8_INITIALIZER("member"),
    [C_AST_MEMBER_ARROW] = S8_INITIALIZER("member_arrow"),
    [C_AST_POST_INCREMENT] = S8_INITIALIZER("post_increment"),
    [C_AST_POST_DECREMENT] = S8_INITIALIZER("post_decrement"),
    [C_AST_COMPOUND_LITERAL] = S8_INITIALIZER("compound_literal"),
    [C_AST_PRE_INCREMENT] = S8_INITIALIZER("pre_increment"),
    [C_AST_PRE_DECREMENT] = S8_INITIALIZER("pre_decrement"),
    [C_AST_ADDRESS] = S8_INITIALIZER("address"),
    [C_AST_DEREFERENCE] = S8_INITIALIZER("dereference"),
    [C_AST_PLUS] = S8_INITIALIZER("plus"),
    [C_AST_NEGATE] = S8_INITIALIZER("negate"),
    [C_AST_BIT_NOT] = S8_INITIALIZER("bit_not"),
    [C_AST_LOGICAL_NOT] = S8_INITIALIZER("logical_not"),
    [C_AST_SIZEOF_EXPRESSION] = S8_INITIALIZER("sizeof_expression"),
    [C_AST_SIZEOF_TYPE] = S8_INITIALIZER("sizeof_type"),
    [C_AST_ALIGNOF_EXPRESSION] = S8_INITIALIZER("alignof_expression"),
    [C_AST_ALIGNOF_TYPE] = S8_INITIALIZER("alignof_type"),
    [C_AST_REAL] = S8_INITIALIZER("real"),
    [C_AST_IMAG] = S8_INITIALIZER("imag"),
    [C_AST_EXTENSION] = S8_INITIALIZER("extension"),
    [C_AST_CAST] = S8_INITIALIZER("cast"),
    [C_AST_MULTIPLY] = S8_INITIALIZER("multiply"),
    [C_AST_DIVIDE] = S8_INITIALIZER("divide"),
    [C_AST_REMAINDER] = S8_INITIALIZER("remainder"),
    [C_AST_ADD] = S8_INITIALIZER("add"),
    [C_AST_SUBTRACT] = S8_INITIALIZER("subtract"),
    [C_AST_SHIFT_LEFT] = S8_INITIALIZER("shift_left"),
    [C_AST_SHIFT_RIGHT] = S8_INITIALIZER("shift_right"),
    [C_AST_LESS] = S8_INITIALIZER("less"),
    [C_AST_GREATER] = S8_INITIALIZER("greater"),
    [C_AST_LESS_EQUAL] = S8_INITIALIZER("less_equal"),
    [C_AST_GREATER_EQUAL] = S8_INITIALIZER("greater_equal"),
    [C_AST_EQUAL] = S8_INITIALIZER("equal"),
    [C_AST_NOT_EQUAL] = S8_INITIALIZER("not_equal"),
    [C_AST_BIT_AND] = S8_INITIALIZER("bit_and"),
    [C_AST_BIT_XOR] = S8_INITIALIZER("bit_xor"),
    [C_AST_BIT_OR] = S8_INITIALIZER("bit_or"),
    [C_AST_LOGICAL_AND] = S8_INITIALIZER("logical_and"),
    [C_AST_LOGICAL_OR] = S8_INITIALIZER("logical_or"),
    [C_AST_CONDITIONAL] = S8_INITIALIZER("conditional"),
    [C_AST_CONDITIONAL_OMITTED] = S8_INITIALIZER("conditional_omitted"),
    [C_AST_ASSIGN] = S8_INITIALIZER("assign"),
    [C_AST_MULTIPLY_ASSIGN] = S8_INITIALIZER("multiply_assign"),
    [C_AST_DIVIDE_ASSIGN] = S8_INITIALIZER("divide_assign"),
    [C_AST_REMAINDER_ASSIGN] = S8_INITIALIZER("remainder_assign"),
    [C_AST_ADD_ASSIGN] = S8_INITIALIZER("add_assign"),
    [C_AST_SUBTRACT_ASSIGN] = S8_INITIALIZER("subtract_assign"),
    [C_AST_SHIFT_LEFT_ASSIGN] = S8_INITIALIZER("shift_left_assign"),
    [C_AST_SHIFT_RIGHT_ASSIGN] = S8_INITIALIZER("shift_right_assign"),
    [C_AST_BIT_AND_ASSIGN] = S8_INITIALIZER("bit_and_assign"),
    [C_AST_BIT_XOR_ASSIGN] = S8_INITIALIZER("bit_xor_assign"),
    [C_AST_BIT_OR_ASSIGN] = S8_INITIALIZER("bit_or_assign"),
    [C_AST_COMMA] = S8_INITIALIZER("comma"),
    [C_AST_INITIALIZER_LIST] = S8_INITIALIZER("initializer_list"),
    [C_AST_DESIGNATION] = S8_INITIALIZER("designation"),
    [C_AST_DESIGNATOR_MEMBER] = S8_INITIALIZER("designator_member"),
    [C_AST_DESIGNATOR_INDEX] = S8_INITIALIZER("designator_index"),
    [C_AST_DESIGNATOR_RANGE] = S8_INITIALIZER("designator_range"),
};

#define C_AST_CONTRACT_ENTRY(name, contract, a, b) (u8)C_AST_CONTRACT_##contract,
#define C_AST_ARITY_A_ENTRY(name, contract, a, b) (u8)(a),
#define C_AST_ARITY_B_ENTRY(name, contract, a, b) (u8)(b),
BUSTER_GLOBAL_LOCAL u8 const c_ast_kind_contracts[C_AST_KIND_COUNT] = {C_AST_KIND_LIST(C_AST_CONTRACT_ENTRY)};
BUSTER_GLOBAL_LOCAL u8 const c_ast_kind_arity_a[C_AST_KIND_COUNT] = {C_AST_KIND_LIST(C_AST_ARITY_A_ENTRY)};
BUSTER_GLOBAL_LOCAL u8 const c_ast_kind_arity_b[C_AST_KIND_COUNT] = {C_AST_KIND_LIST(C_AST_ARITY_B_ENTRY)};
#undef C_AST_CONTRACT_ENTRY
#undef C_AST_ARITY_A_ENTRY
#undef C_AST_ARITY_B_ENTRY

// Binary operators by punctuator: precedence in the low byte, node kind in
// the high byte; zero is not a binary operator. Assignment (2) and the
// conditional (3) associate to the right; every other level to the left.
#define C_AST_BINARY(precedence, kind) ((u16)(((u16)(kind) << 8) | (u16)(precedence)))
BUSTER_GLOBAL_LOCAL u16 const c_ast_binary_info[256] = {
    [C_PUNCTUATOR_STAR] = C_AST_BINARY(C_AST_PREC_MULTIPLICATIVE, C_AST_MULTIPLY),
    [C_PUNCTUATOR_SLASH] = C_AST_BINARY(C_AST_PREC_MULTIPLICATIVE, C_AST_DIVIDE),
    [C_PUNCTUATOR_PERCENT] = C_AST_BINARY(C_AST_PREC_MULTIPLICATIVE, C_AST_REMAINDER),
    [C_PUNCTUATOR_PLUS] = C_AST_BINARY(C_AST_PREC_ADDITIVE, C_AST_ADD),
    [C_PUNCTUATOR_MINUS] = C_AST_BINARY(C_AST_PREC_ADDITIVE, C_AST_SUBTRACT),
    [C_PUNCTUATOR_SHIFT_LEFT] = C_AST_BINARY(C_AST_PREC_SHIFT, C_AST_SHIFT_LEFT),
    [C_PUNCTUATOR_SHIFT_RIGHT] = C_AST_BINARY(C_AST_PREC_SHIFT, C_AST_SHIFT_RIGHT),
    [C_PUNCTUATOR_LESS] = C_AST_BINARY(C_AST_PREC_RELATIONAL, C_AST_LESS),
    [C_PUNCTUATOR_GREATER] = C_AST_BINARY(C_AST_PREC_RELATIONAL, C_AST_GREATER),
    [C_PUNCTUATOR_LESS_EQUAL] = C_AST_BINARY(C_AST_PREC_RELATIONAL, C_AST_LESS_EQUAL),
    [C_PUNCTUATOR_GREATER_EQUAL] = C_AST_BINARY(C_AST_PREC_RELATIONAL, C_AST_GREATER_EQUAL),
    [C_PUNCTUATOR_EQUAL] = C_AST_BINARY(C_AST_PREC_EQUALITY, C_AST_EQUAL),
    [C_PUNCTUATOR_NOT_EQUAL] = C_AST_BINARY(C_AST_PREC_EQUALITY, C_AST_NOT_EQUAL),
    [C_PUNCTUATOR_AMPERSAND] = C_AST_BINARY(C_AST_PREC_BIT_AND, C_AST_BIT_AND),
    [C_PUNCTUATOR_CARET] = C_AST_BINARY(C_AST_PREC_BIT_XOR, C_AST_BIT_XOR),
    [C_PUNCTUATOR_PIPE] = C_AST_BINARY(C_AST_PREC_BIT_OR, C_AST_BIT_OR),
    [C_PUNCTUATOR_AMPERSAND_AMPERSAND] = C_AST_BINARY(C_AST_PREC_LOGICAL_AND, C_AST_LOGICAL_AND),
    [C_PUNCTUATOR_PIPE_PIPE] = C_AST_BINARY(C_AST_PREC_LOGICAL_OR, C_AST_LOGICAL_OR),
    [C_PUNCTUATOR_QUESTION] = C_AST_BINARY(C_AST_PREC_CONDITIONAL, C_AST_CONDITIONAL),
    [C_PUNCTUATOR_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_ASSIGN),
    [C_PUNCTUATOR_STAR_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_MULTIPLY_ASSIGN),
    [C_PUNCTUATOR_SLASH_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_DIVIDE_ASSIGN),
    [C_PUNCTUATOR_PERCENT_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_REMAINDER_ASSIGN),
    [C_PUNCTUATOR_PLUS_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_ADD_ASSIGN),
    [C_PUNCTUATOR_MINUS_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_SUBTRACT_ASSIGN),
    [C_PUNCTUATOR_SHIFT_LEFT_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_SHIFT_LEFT_ASSIGN),
    [C_PUNCTUATOR_SHIFT_RIGHT_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_SHIFT_RIGHT_ASSIGN),
    [C_PUNCTUATOR_AMPERSAND_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_BIT_AND_ASSIGN),
    [C_PUNCTUATOR_CARET_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_BIT_XOR_ASSIGN),
    [C_PUNCTUATOR_PIPE_ASSIGN] = C_AST_BINARY(C_AST_PREC_ASSIGN, C_AST_BIT_OR_ASSIGN),
    [C_PUNCTUATOR_COMMA] = C_AST_BINARY(C_AST_PREC_COMMA, C_AST_COMMA),
};

// Prefix unary operators by punctuator; zero is not a prefix operator.
BUSTER_GLOBAL_LOCAL u8 const c_ast_prefix_kinds[256] = {
    [C_PUNCTUATOR_PLUS_PLUS] = C_AST_PRE_INCREMENT,
    [C_PUNCTUATOR_MINUS_MINUS] = C_AST_PRE_DECREMENT,
    [C_PUNCTUATOR_AMPERSAND] = C_AST_ADDRESS,
    [C_PUNCTUATOR_STAR] = C_AST_DEREFERENCE,
    [C_PUNCTUATOR_PLUS] = C_AST_PLUS,
    [C_PUNCTUATOR_MINUS] = C_AST_NEGATE,
    [C_PUNCTUATOR_TILDE] = C_AST_BIT_NOT,
    [C_PUNCTUATOR_EXCLAMATION] = C_AST_LOGICAL_NOT,
};

// ---- builder state --------------------------------------------------------

typedef enum CAstFrameKind
{
    C_AST_FRAME_TRANSLATION_UNIT,
    C_AST_FRAME_DECL,
    C_AST_FRAME_SPECS,
    C_AST_FRAME_KEYWORD_PAREN,
    C_AST_FRAME_TYPE_NAME,
    C_AST_FRAME_DECLARATOR,
    C_AST_FRAME_ARRAY_SUFFIX,
    C_AST_FRAME_FUNCTION_SUFFIX,
    C_AST_FRAME_PARAM_LIST,
    C_AST_FRAME_PARAM,
    C_AST_FRAME_INIT_DECL,
    C_AST_FRAME_MEMBER_LIST,
    C_AST_FRAME_MEMBER_DECL,
    C_AST_FRAME_MEMBER_DECLARATOR,
    C_AST_FRAME_STRUCT,
    C_AST_FRAME_ENUM,
    C_AST_FRAME_ENUMERATOR_LIST,
    C_AST_FRAME_ENUMERATOR,
    C_AST_FRAME_ATTR_LIST,
    C_AST_FRAME_ATTR_GROUP,
    C_AST_FRAME_EXPR,
    C_AST_FRAME_CALL,
    C_AST_FRAME_GENERIC,
    C_AST_FRAME_INIT_LIST,
    C_AST_FRAME_DESIGNATION,
    C_AST_FRAME_STATIC_ASSERT,
    C_AST_FRAME_ASM,
    C_AST_FRAME_BLOCK,
    C_AST_FRAME_IF,
    C_AST_FRAME_WHILE,
    C_AST_FRAME_DO,
    C_AST_FRAME_FOR,
    C_AST_FRAME_LABELED,
    C_AST_FRAME_CASE,
    C_AST_FRAME_DEFAULT,
    C_AST_FRAME_ATTR_STATEMENT,
    C_AST_FRAME_SIMPLE_STATEMENT,
    C_AST_FRAME_COUNT,
} CAstFrameKind;

// One open construct. `begin` is the append position at which its subtree
// began; a..f are the kind's registers (named per step function).
typedef struct CAstFrame CAstFrame;
struct CAstFrame
{
    u8 kind;
    u8 state;
    u16 flags;
    u32 begin;
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 e;
    u32 f;
};
BUSTER_CT_CHECK(sizeof(CAstFrame) == 32);

// A pending operator: the node it becomes (or a marker kind), its precedence,
// the append position at which its subtree begins, and its anchor token.
typedef struct CAstOperator CAstOperator;
struct CAstOperator
{
    u32 begin;
    u32 token;
    u8 kind;
    u8 precedence;
};

// A `*` of a declarator level awaiting emission after that level's suffixes.
typedef struct CAstPointer CAstPointer;
struct CAstPointer
{
    u32 begin;
    u32 token;
    u32 flags;
    u32 groups;
    u32 attribute_token;
};

typedef struct CAstUndo CAstUndo;
struct CAstUndo
{
    u32 symbol;
    u32 previous;
};

typedef struct CAstChunk CAstChunk;
struct CAstChunk
{
    u8* kinds;
    u32* extents;
    u32* tokens;
    u32* data;
};

typedef struct CAstBuilder CAstBuilder;
struct CAstBuilder
{
    // ---- token cursor. window[(position + k) & window_mask] while
    // position + k < filled; the stream's tokens in place when
    // refill_batch == 0, else a power-of-two ring refilled in batches.
    CToken const* window;
    CToken const* source;
    CToken* ring;
    u32 window_mask;
    u32 position;
    u32 filled;
    u32 token_count;
    u32 ring_capacity;
    u32 refill_batch;
    CToken eof_token;
    // ---- node columns: the open chunk and the chunk table.
    u32 node_count;
    u8* chunk_kinds;
    u32* chunk_extents;
    u32* chunk_tokens;
    u32* chunk_data;
    CAstChunk* chunks;
    u32 chunk_count;
    u32 chunk_capacity;
    // ---- explicit stacks.
    CAstFrame* frames;
    u32 frame_count;
    u32 frame_capacity;
    CAstOperator* operators;
    u32 operator_count;
    u32 operator_capacity;
    CAstPointer* pointers;
    u32 pointer_count;
    u32 pointer_capacity;
    CAstUndo* undo;
    u32 undo_count;
    u32 undo_capacity;
    u32* capture;
    u32 capture_count;
    u32 capture_capacity;
    // ---- symbols and bindings.
    CSymbolTable* symbols;
    u8* info;
    u32 info_count;
    u32 scope_depth;
    u32 parameter_depth;
    // ---- dialect.
    CPreprocessDialect dialect;
    bool c23;
    bool gnu;
    // ---- return registers: what a popped frame leaves for its parent.
    u32 ret_items;
    bool ret_typedef;
    bool ret_seen_type;
    bool ret_definition;
    u32 ret_name_token;
    u32 ret_name_symbol;
    u32 ret_first_derivation;
    // ---- failure: the first syntax error.
    bool failed;
    CDiagnosticKind fail_kind;
    u32 fail_token;
    String8 fail_message;
    // ---- environment.
    Arena* arena;
    Arena* phase;
    CPreprocessResult preprocess;
    CAstStatistics statistics;
};

// ---- stacks ---------------------------------------------------------------

// Doubles a phase-arena stack. The old block stays in the arena until the
// phase is released; the sum of a doubling sequence is bounded by twice the
// final size.
BUSTER_GLOBAL_LOCAL void* c_ast_grow(CAstBuilder* builder, void* old, u32 count, u32* capacity, u64 element_size)
{
    u32 new_capacity = *capacity ? *capacity * 2 : C_AST_INITIAL_STACK;
    void* result = arena_allocate_bytes(builder->phase, (u64)new_capacity * element_size, 16);
    if (count)
    {
        memcpy(result, old, (u64)count * element_size);
    }
    *capacity = new_capacity;
    return result;
}

BUSTER_GLOBAL_LOCAL CAstFrame* c_ast_push(CAstBuilder* builder, CAstFrameKind kind, u32 state, u32 flags, u32 begin)
{
    if (BUSTER_UNLIKELY(builder->frame_count == builder->frame_capacity))
    {
        builder->frames = (CAstFrame*)c_ast_grow(builder, builder->frames, builder->frame_count, &builder->frame_capacity, sizeof(CAstFrame));
    }
    CAstFrame* frame = &builder->frames[builder->frame_count];
    builder->frame_count += 1;
    frame->kind = (u8)kind;
    frame->state = (u8)state;
    frame->flags = (u16)flags;
    frame->begin = begin;
    frame->a = 0;
    frame->b = 0;
    frame->c = 0;
    frame->d = 0;
    frame->e = 0;
    frame->f = 0;
    C_AST_COUNT(builder, frames_pushed, 1);
#if C_AST_COUNTERS
    builder->statistics.frame_high_water = BUSTER_MAX(builder->statistics.frame_high_water, (u64)builder->frame_count);
#endif
    return frame;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_pop(CAstBuilder* builder)
{
    builder->frame_count -= 1;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_operator_push(CAstBuilder* builder, u32 kind, u32 precedence, u32 begin, u32 token)
{
    if (BUSTER_UNLIKELY(builder->operator_count == builder->operator_capacity))
    {
        builder->operators = (CAstOperator*)c_ast_grow(builder, builder->operators, builder->operator_count, &builder->operator_capacity,
                                                       sizeof(CAstOperator));
    }
    CAstOperator* entry = &builder->operators[builder->operator_count];
    builder->operator_count += 1;
    entry->begin = begin;
    entry->token = token;
    entry->kind = (u8)kind;
    entry->precedence = (u8)precedence;
#if C_AST_COUNTERS
    builder->statistics.operator_high_water = BUSTER_MAX(builder->statistics.operator_high_water, (u64)builder->operator_count);
#endif
}

BUSTER_GLOBAL_LOCAL CAstPointer* c_ast_pointer_push(CAstBuilder* builder, u32 begin, u32 token)
{
    if (BUSTER_UNLIKELY(builder->pointer_count == builder->pointer_capacity))
    {
        builder->pointers =
            (CAstPointer*)c_ast_grow(builder, builder->pointers, builder->pointer_count, &builder->pointer_capacity, sizeof(CAstPointer));
    }
    CAstPointer* entry = &builder->pointers[builder->pointer_count];
    builder->pointer_count += 1;
    entry->begin = begin;
    entry->token = token;
    entry->flags = 0;
    entry->groups = 0;
    entry->attribute_token = 0;
    return entry;
}

BUSTER_GLOBAL_LOCAL void c_ast_capture_push(CAstBuilder* builder, u32 symbol)
{
    if (BUSTER_UNLIKELY(builder->capture_count == builder->capture_capacity))
    {
        builder->capture = (u32*)c_ast_grow(builder, builder->capture, builder->capture_count, &builder->capture_capacity, sizeof(u32));
    }
    builder->capture[builder->capture_count] = symbol;
    builder->capture_count += 1;
}

// ---- diagnostics ----------------------------------------------------------

// Records the first syntax error and stops the pass. A later call is ignored:
// the earliest failure is the one worth reporting, and a step that failed
// returns to the loop, which stops.
BUSTER_GLOBAL_LOCAL void c_ast_fail(CAstBuilder* builder, u32 token, CDiagnosticKind kind, String8 message)
{
    if (!builder->failed)
    {
        builder->failed = true;
        builder->fail_kind = kind;
        builder->fail_token = token;
        builder->fail_message = message;
    }
}

// "found 'x'" text for the token at the cursor, with long spellings cut.
BUSTER_GLOBAL_LOCAL String8 c_ast_describe_token(CAstBuilder* builder, CToken token)
{
    String8 result;
    if (token.kind == C_TOKEN_END_OF_FILE)
    {
        result = S8("end of file");
    }
    else
    {
        String8 spelling = c_token_spelling(builder->preprocess.spelling_base, token);
        enum
        {
            SPELLING_LIMIT = 48,
        };
        if (spelling.length > SPELLING_LIMIT)
        {
            spelling.length = SPELLING_LIMIT;
            result = string_format(builder->arena, S8("'{S8}...'"), spelling);
        }
        else
        {
            result = string_format(builder->arena, S8("'{S8}'"), spelling);
        }
    }
    return result;
}

// ---- token cursor ---------------------------------------------------------

BUSTER_GLOBAL_LOCAL CToken c_ast_peek_slow(CAstBuilder* builder, u32 at)
{
    CToken result = builder->eof_token;
    if (builder->ring_capacity)
    {
        while (builder->filled <= at && builder->filled < builder->token_count)
        {
            u32 count = BUSTER_MIN(builder->refill_batch, builder->token_count - builder->filled);
            for (u32 offset = 0; offset < count; offset += 1)
            {
                builder->ring[(builder->filled + offset) & builder->window_mask] = builder->source[builder->filled + offset];
            }
            builder->filled += count;
            C_AST_COUNT(builder, refills, 1);
        }
        if (at < builder->filled)
        {
            result = builder->ring[at & builder->window_mask];
        }
    }
    return result;
}

// The token `ahead` positions past the cursor (ahead < C_AST_LOOKAHEAD). Past
// the end of the stream it is the end-of-file token. One compare decides
// whether the window holds it.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE CToken c_ast_peek(CAstBuilder* builder, u32 ahead)
{
    BUSTER_CHECK(ahead < C_AST_LOOKAHEAD);
    u32 at = builder->position + ahead;
    CToken result;
    if (BUSTER_LIKELY(at < builder->filled))
    {
        result = builder->window[at & builder->window_mask];
    }
    else
    {
        result = c_ast_peek_slow(builder, at);
    }
    C_AST_COUNT(builder, token_peeks, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_advance(CAstBuilder* builder)
{
    builder->position += 1;
    C_AST_COUNT(builder, tokens_consumed, 1);
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_punctuator(CToken token, CPunctuator punctuator)
{
    return token.punctuator == (u8)punctuator;
}

// "expected <what>, found <token>" at the cursor.
BUSTER_GLOBAL_LOCAL void c_ast_fail_expected(CAstBuilder* builder, CDiagnosticKind kind, String8 what)
{
    if (!builder->failed)
    {
        String8 found = c_ast_describe_token(builder, c_ast_peek(builder, 0));
        c_ast_fail(builder, builder->position, kind, string_format(builder->arena, S8("expected {S8}, found {S8}"), what, found));
    }
}

// ---- node columns ---------------------------------------------------------

BUSTER_GLOBAL_LOCAL void c_ast_chunk_next(CAstBuilder* builder)
{
    if (builder->node_count >= C_AST_NODE_LIMIT)
    {
        // Keep appending into the open chunk: the tree is discarded on
        // failure, and the dispatch loop stops at its next iteration.
        c_ast_fail(builder, builder->position, C_DIAGNOSTIC_SOURCE_TOO_LARGE, S8("the syntax tree has more nodes than a node index can address"));
    }
    else
    {
        if (builder->chunk_count == builder->chunk_capacity)
        {
            builder->chunks = (CAstChunk*)c_ast_grow(builder, builder->chunks, builder->chunk_count, &builder->chunk_capacity, sizeof(CAstChunk));
        }
        u8* block = (u8*)arena_allocate_bytes(builder->phase, C_AST_CHUNK_BYTES, 64);
        CAstChunk* chunk = &builder->chunks[builder->chunk_count];
        builder->chunk_count += 1;
        chunk->extents = (u32*)block;
        chunk->tokens = chunk->extents + C_AST_CHUNK_NODES;
        chunk->data = chunk->tokens + C_AST_CHUNK_NODES;
        chunk->kinds = (u8*)(chunk->data + C_AST_CHUNK_NODES);
        builder->chunk_kinds = chunk->kinds;
        builder->chunk_extents = chunk->extents;
        builder->chunk_tokens = chunk->tokens;
        builder->chunk_data = chunk->data;
    }
}

// Appends the node whose subtree began at `begin`. One predictable branch per
// node: the first slot of a chunk.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_append(CAstBuilder* builder, CAstKind kind, u32 begin, u32 token, u32 data)
{
    u32 node = builder->node_count;
    u32 slot = node & C_AST_CHUNK_MASK;
    if (BUSTER_UNLIKELY(slot == 0))
    {
        c_ast_chunk_next(builder);
    }
    builder->chunk_kinds[slot] = (u8)kind;
    builder->chunk_extents[slot] = node + 1 - begin;
    builder->chunk_tokens[slot] = token;
    builder->chunk_data[slot] = data;
    builder->node_count = node + 1;
}

// ---- bindings -------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void c_ast_info_ensure(CAstBuilder* builder, u32 symbol)
{
    if (symbol >= builder->info_count)
    {
        u32 new_count = BUSTER_MAX(symbol + 1, builder->info_count * 2);
        u8* grown = arena_allocate_zeroed(builder->phase, u8, new_count);
        memcpy(grown, builder->info, builder->info_count);
        builder->info = grown;
        builder->info_count = new_count;
    }
}

// The keyword a spelling names in this dialect (CAstWord), or 0. Used for
// tokens the intern pass never saw and to fill the symbol-indexed table.
BUSTER_GLOBAL_LOCAL u32 c_ast_word_for_spelling(CAstBuilder* builder, String8 spelling)
{
    u32 result = 0;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_word_spellings) && !result; index += 1)
    {
        CAstWordSpelling const* entry = &c_ast_word_spellings[index];
        bool enabled = entry->gate == C_AST_GATE_ALWAYS || (entry->gate == C_AST_GATE_GNU && builder->gnu) ||
                       (entry->gate == C_AST_GATE_C23 && builder->c23) ||
                       (entry->gate == C_AST_GATE_GNU_OR_C23 && (builder->gnu || builder->c23));
        if (enabled && string_equal(spelling, entry->spelling))
        {
            result = entry->word;
        }
    }
    return result;
}

// The symbol an identifier token stands for. Interned tokens carry theirs;
// a token the intern pass never saw is interned here, into a local value
// that is never written back into the stream.
BUSTER_GLOBAL_LOCAL u32 c_ast_symbol_slow(CAstBuilder* builder, CToken token)
{
    u32 symbol = c_symbol_intern(builder->symbols, c_token_spelling(builder->preprocess.spelling_base, token));
    c_ast_info_ensure(builder, symbol);
    return symbol;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_symbol_of(CAstBuilder* builder, CToken token)
{
    u32 result = token.symbol;
    if (BUSTER_UNLIKELY(result == 0 || result >= builder->info_count))
    {
        result = c_ast_symbol_slow(builder, token);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 c_ast_info_slow(CAstBuilder* builder, CToken token)
{
    u32 result = c_ast_word_for_spelling(builder, c_token_spelling(builder->preprocess.spelling_base, token));
    if (!result)
    {
        u32 symbol = c_ast_symbol_slow(builder, token);
        result = builder->info[symbol];
    }
    return result;
}

// What the info table says about a token: 0 for anything that is not an
// identifier or an unbound plain identifier, a CAstWord for a keyword,
// C_AST_INFO_TYPEDEF or C_AST_INFO_ORDINARY for a bound name. A symbol-indexed
// byte load for every interned identifier.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_info(CAstBuilder* builder, CToken token)
{
    u32 result = 0;
    if (token.kind == C_TOKEN_IDENTIFIER)
    {
        if (BUSTER_LIKELY(token.symbol != 0 && token.symbol < builder->info_count))
        {
            result = builder->info[token.symbol];
        }
        else
        {
            result = c_ast_info_slow(builder, token);
        }
    }
    return result;
}

// An identifier a declaration or use may name: not a keyword.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_name_info(u32 info)
{
    return info == 0 || info >= C_AST_WORD_COUNT;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_flags_of(CAstBuilder* builder, CToken token)
{
    return c_ast_info_flags[c_ast_info(builder, token)];
}

BUSTER_GLOBAL_LOCAL u32 c_ast_scope_push(CAstBuilder* builder)
{
    builder->scope_depth += 1;
    return builder->undo_count;
}

BUSTER_GLOBAL_LOCAL void c_ast_scope_pop(CAstBuilder* builder, u32 mark)
{
    while (builder->undo_count > mark)
    {
        builder->undo_count -= 1;
        CAstUndo entry = builder->undo[builder->undo_count];
        builder->info[entry.symbol] = (u8)entry.previous;
    }
    builder->scope_depth -= 1;
}

// Declares `symbol` as a typedef name or an ordinary identifier in the
// innermost scope. File-scope bindings are never undone and are not logged.
// A keyword is never rebound.
BUSTER_GLOBAL_LOCAL void c_ast_bind(CAstBuilder* builder, u32 symbol, u32 value)
{
    u32 previous = builder->info[symbol];
    if (c_ast_is_name_info(previous))
    {
        if (builder->scope_depth)
        {
            if (BUSTER_UNLIKELY(builder->undo_count == builder->undo_capacity))
            {
                builder->undo = (CAstUndo*)c_ast_grow(builder, builder->undo, builder->undo_count, &builder->undo_capacity, sizeof(CAstUndo));
            }
            builder->undo[builder->undo_count] = (CAstUndo){.symbol = symbol, .previous = previous};
            builder->undo_count += 1;
        }
        builder->info[symbol] = (u8)value;
        C_AST_COUNT(builder, bindings_published, 1);
    }
}

// ---- classification -------------------------------------------------------

// May this token begin a type-name after `(`?
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_type_start(CAstBuilder* builder, CToken token)
{
    C_AST_COUNT(builder, binding_lookups, token.kind == C_TOKEN_IDENTIFIER);
    return (c_ast_flags_of(builder, token) & C_AST_FLAG_TYPE) != 0;
}

// Does a type-name begin `offset` tokens ahead? `__extension__` may open a
// type-name (`(__extension__ int)`) or an expression (`__extension__ ({...})`),
// so the token after it decides.
BUSTER_GLOBAL_LOCAL bool c_ast_type_name_at(CAstBuilder* builder, u32 offset)
{
    CToken token = c_ast_peek(builder, offset);
    bool result;
    if (c_ast_info(builder, token) == C_AST_WORD_EXTENSION && offset + 1 < C_AST_LOOKAHEAD)
    {
        result = c_ast_is_type_start(builder, c_ast_peek(builder, offset + 1));
    }
    else
    {
        result = c_ast_is_type_start(builder, token);
    }
    return result;
}

// May this token begin a declaration at a block item (or K&R declaration)?
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_declaration_start(CAstBuilder* builder, CToken token)
{
    return (c_ast_flags_of(builder, token) & C_AST_FLAG_DECL) != 0;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_word(CAstBuilder* builder, CToken token, CAstWord word)
{
    return token.kind == C_TOKEN_IDENTIFIER && c_ast_info(builder, token) == (u32)word;
}

// `__attribute__`, `__declspec` or the `[[` of a standard attribute. Needs the
// second token for `[[`; the caller passes the first.
BUSTER_GLOBAL_LOCAL bool c_ast_is_attribute_start(CAstBuilder* builder, CToken token)
{
    bool result = false;
    if (token.kind == C_TOKEN_IDENTIFIER)
    {
        u32 info = c_ast_info(builder, token);
        result = info == C_AST_WORD_ATTRIBUTE || info == C_AST_WORD_DECLSPEC;
    }
    else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET))
    {
        result = c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_BRACKET);
    }
    return result;
}

// The qualifier flag a word contributes to a pointer or array declarator, or
// 0 when the word is not a qualifier.
BUSTER_GLOBAL_LOCAL u32 c_ast_qualifier_flag(u32 info)
{
    u32 result;
    switch (info)
    {
    case C_AST_WORD_CONST:
        result = C_AST_QUALIFIER_CONST;
        break;
    case C_AST_WORD_VOLATILE:
        result = C_AST_QUALIFIER_VOLATILE;
        break;
    case C_AST_WORD_RESTRICT:
        result = C_AST_QUALIFIER_RESTRICT;
        break;
    case C_AST_WORD_ATOMIC:
        result = C_AST_QUALIFIER_ATOMIC;
        break;
    case C_AST_WORD_NONNULL:
        result = C_AST_QUALIFIER_NONNULL;
        break;
    case C_AST_WORD_NULLABLE:
        result = C_AST_QUALIFIER_NULLABLE;
        break;
    case C_AST_WORD_NULL_UNSPECIFIED:
        result = C_AST_QUALIFIER_NULL_UNSPECIFIED;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_ast_is_offsetof(CAstBuilder* builder, CToken token)
{
    return c_token_is_well_known(builder->preprocess.spelling_base, token, C_SYMBOL_WELL_KNOWN_BUILTIN_OFFSETOF);
}

// Appends one STRING node for the run of adjacent string-literal tokens at
// the cursor (at least one) and returns the run length.
BUSTER_GLOBAL_LOCAL u32 c_ast_append_string_run(CAstBuilder* builder)
{
    u32 first = builder->position;
    u32 begin = builder->node_count;
    u32 count = 0;
    while (c_ast_peek(builder, 0).kind == C_TOKEN_STRING_LITERAL)
    {
        c_ast_advance(builder);
        count += 1;
    }
    c_ast_append(builder, C_AST_STRING, begin, first, count);
    return count;
}

// A list element that is one token -- a name, number, character or string --
// followed by `,` or `closer` is a leaf: it needs no expression frame. Appends
// it and returns true, or returns false and consumes nothing.
BUSTER_GLOBAL_LOCAL bool c_ast_append_simple_operand(CAstBuilder* builder, CToken token, CPunctuator closer, bool member_designator)
{
    bool result = false;
    CToken next = c_ast_peek(builder, 1);
    if (c_ast_is_punctuator(next, C_PUNCTUATOR_COMMA) || c_ast_is_punctuator(next, closer))
    {
        u32 node = builder->node_count;
        switch ((CTokenKind)token.kind)
        {
        case C_TOKEN_IDENTIFIER:
        {
            u32 info = c_ast_info(builder, token);
            result = c_ast_is_name_info(info) && (info != C_AST_INFO_TYPEDEF || member_designator);
            if (result)
            {
                c_ast_append(builder, C_AST_IDENTIFIER, node, builder->position, token.symbol);
            }
            break;
        }
        case C_TOKEN_PREPROCESSING_NUMBER:
        {
            c_ast_append(builder, C_AST_NUMBER, node, builder->position, 0);
            result = true;
            break;
        }
        case C_TOKEN_CHARACTER_LITERAL:
        {
            c_ast_append(builder, C_AST_CHARACTER, node, builder->position, 0);
            result = true;
            break;
        }
        case C_TOKEN_STRING_LITERAL:
        {
            c_ast_append(builder, C_AST_STRING, node, builder->position, 1);
            result = true;
            break;
        }
        default:
        {
            break;
        }
        }
        if (result)
        {
            c_ast_advance(builder);
        }
    }
    return result;
}

// ---- frame states ---------------------------------------------------------
// Each frame kind names its own states; a step reads f->state, so a frame
// that returns to the loop after pushing a child resumes in the named state.

enum
{
    C_AST_EXPR_OPERAND,
    C_AST_EXPR_POSTFIX,
    C_AST_EXPR_OPERATOR,
    C_AST_EXPR_AFTER_PRIMARY,
    C_AST_EXPR_AFTER_INDEX,
    C_AST_EXPR_AFTER_CALL,
    C_AST_EXPR_AFTER_CAST_TYPE,
    C_AST_EXPR_AFTER_COMPOUND_LITERAL,
    C_AST_EXPR_AFTER_SIZEOF_TYPE,
    C_AST_EXPR_AFTER_SIZEOF_COMPOUND,
    C_AST_EXPR_AFTER_STATEMENT,
};

enum
{
    // F_EXPR flags: the lowest precedence the frame accepts, in the low
    // nibble, and whether the pending `sizeof (type)` is `_Alignof`.
    C_AST_EXPR_PRECEDENCE_MASK = 0xf,
    C_AST_EXPR_ALIGNOF = 0x100,
    // The leading operand names a member (__builtin_offsetof's designator
    // arguments), so a typedef name is accepted there as a plain identifier.
    // Everywhere else a typedef name is never an operand.
    C_AST_EXPR_MEMBER_DESIGNATOR = 0x200,
    // F_CALL flag: the callee is __builtin_offsetof, whose later arguments
    // are member designators, never type-names.
    C_AST_CALL_OFFSETOF = 1,
};

enum
{
    C_AST_CALL_OPEN,
    C_AST_CALL_ARGUMENT,
    C_AST_CALL_AFTER_ARGUMENT,
};

enum
{
    C_AST_GENERIC_OPEN,
    C_AST_GENERIC_AFTER_CONTROL,
    C_AST_GENERIC_ITEM,
    C_AST_GENERIC_AFTER_TYPE,
    C_AST_GENERIC_AFTER_ASSOCIATION,
    C_AST_GENERIC_AFTER_DEFAULT,
    C_AST_GENERIC_AFTER_ITEM,
};

enum
{
    C_AST_INIT_LIST_OPEN,
    C_AST_INIT_LIST_ITEM,
    C_AST_INIT_LIST_AFTER_ITEM,
};

enum
{
    C_AST_DESIGNATION_NEXT,
    C_AST_DESIGNATION_AFTER_INDEX,
    C_AST_DESIGNATION_AFTER_RANGE,
    C_AST_DESIGNATION_AFTER_VALUE,
};

enum
{
    C_AST_TYPE_NAME_START,
    C_AST_TYPE_NAME_AFTER_SPECS,
    C_AST_TYPE_NAME_END,
};

enum
{
    // F_TYPE_NAME flag: a specifier-qualifier list only (an enum's fixed type).
    C_AST_TYPE_NAME_NO_DECLARATOR = 1,
};

enum
{
    C_AST_BLOCK_OPEN,
    C_AST_BLOCK_ITEMS,
};

enum
{
    // F_BLOCK flag: the scope belongs to the caller (a function body whose
    // parameters are already declared in it).
    C_AST_BLOCK_NO_SCOPE = 1,
};

// ---- frame constructors ---------------------------------------------------

BUSTER_GLOBAL_LOCAL void c_ast_push_type_name(CAstBuilder* builder, u32 flags)
{
    c_ast_push(builder, C_AST_FRAME_TYPE_NAME, C_AST_TYPE_NAME_START, flags, builder->node_count);
}

// An expression frame accepting operators of at least `minimum` precedence,
// seeded with `extension_run` consumed `__extension__` tokens that become
// prefix operators.
BUSTER_GLOBAL_LOCAL void c_ast_push_expr(CAstBuilder* builder, u32 minimum, u32 extension_run)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_EXPR, C_AST_EXPR_OPERAND, minimum, builder->node_count);
    frame->a = builder->operator_count;
    frame->b = builder->node_count;
    for (u32 index = 0; index < extension_run; index += 1)
    {
        c_ast_operator_push(builder, C_AST_EXTENSION, C_AST_PREC_PREFIX, builder->node_count, builder->position - extension_run + index);
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_push_block(CAstBuilder* builder, u32 flags)
{
    c_ast_push(builder, C_AST_FRAME_BLOCK, C_AST_BLOCK_OPEN, flags, builder->node_count);
}

// `{ ... }` or an assignment-expression: the value of an initializer.
BUSTER_GLOBAL_LOCAL void c_ast_push_initializer(CAstBuilder* builder)
{
    if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_BRACE))
    {
        c_ast_push(builder, C_AST_FRAME_INIT_LIST, C_AST_INIT_LIST_OPEN, 0, builder->node_count);
    }
    else
    {
        c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
    }
}

// ---- expressions ----------------------------------------------------------

// Pops every operator of at least `minimum` precedence above `base` into its
// node and returns the begin of the expression that results: the last
// operator's begin, or `begin` when none reduced. A marker (precedence 0)
// stops the loop.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_reduce(CAstBuilder* builder, u32 base, u32 minimum, u32 begin)
{
    u32 result = begin;
    while (builder->operator_count > base && builder->operators[builder->operator_count - 1].precedence >= minimum)
    {
        builder->operator_count -= 1;
        CAstOperator entry = builder->operators[builder->operator_count];
        c_ast_append(builder, (CAstKind)entry.kind, entry.begin, entry.token, 0);
        result = entry.begin;
    }
    return result;
}

// One expression, shunting-yard over the shared operator stack above this
// frame's base. Operands are appended the moment they are read; an operator
// becomes its node when a following operator of equal or lower precedence, a
// closer, or the end of the expression reduces it, with the begin it recorded
// when it was pushed. `begin` below is the subtree begin of the operand the
// next operator would take as its left side.
BUSTER_GLOBAL_LOCAL void c_ast_step_expr(CAstBuilder* builder, CAstFrame* frame)
{
    u32 base = frame->a;
    u32 minimum = frame->flags & C_AST_EXPR_PRECEDENCE_MASK;
    u32 nest = frame->d;
    u32 begin = frame->b;
    u32 state = frame->state;
    bool callee_offsetof = false;
    bool running = true;
    while (running && !builder->failed)
    {
        switch (state)
        {
        case C_AST_EXPR_OPERAND:
        {
            CToken token = c_ast_peek(builder, 0);
            u32 position = builder->position;
            switch ((CTokenKind)token.kind)
            {
            case C_TOKEN_IDENTIFIER:
            {
                u32 info = c_ast_info(builder, token);
                if (info == C_AST_INFO_TYPEDEF && !(frame->flags & C_AST_EXPR_MEMBER_DESIGNATOR))
                {
                    c_ast_fail(builder, position, C_DIAGNOSTIC_EXPECTED_DECLARATION,
                               string_format(builder->arena, S8("expected an expression, found the type name {S8}"),
                                             c_ast_describe_token(builder, token)));
                }
                else if (c_ast_is_name_info(info))
                {
                    frame->flags = (u16)(frame->flags & ~C_AST_EXPR_MEMBER_DESIGNATOR);
                    begin = builder->node_count;
                    c_ast_append(builder, C_AST_IDENTIFIER, begin, position, token.symbol);
                    callee_offsetof = c_ast_is_offsetof(builder, token);
                    c_ast_advance(builder);
                    state = C_AST_EXPR_POSTFIX;
                }
                else
                {
                    switch ((CAstWord)info)
                    {
                    case C_AST_WORD_SIZEOF:
                    case C_AST_WORD_ALIGNOF:
                    {
                        c_ast_advance(builder);
                        if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                            c_ast_type_name_at(builder, 1))
                        {
                            frame->flags = (u16)((frame->flags & ~C_AST_EXPR_ALIGNOF) | (info == C_AST_WORD_ALIGNOF ? C_AST_EXPR_ALIGNOF : 0));
                            frame->b = builder->node_count;
                            frame->c = position;
                            frame->d = nest;
                            frame->state = C_AST_EXPR_AFTER_SIZEOF_TYPE;
                            c_ast_advance(builder);
                            c_ast_push_type_name(builder, 0);
                            running = false;
                        }
                        else
                        {
                            c_ast_operator_push(builder, info == C_AST_WORD_SIZEOF ? C_AST_SIZEOF_EXPRESSION : C_AST_ALIGNOF_EXPRESSION,
                                                C_AST_PREC_PREFIX, builder->node_count, position);
                        }
                        break;
                    }
                    case C_AST_WORD_REAL:
                    case C_AST_WORD_IMAG:
                    case C_AST_WORD_EXTENSION:
                    {
                        c_ast_advance(builder);
                        c_ast_operator_push(builder, info == C_AST_WORD_REAL ? C_AST_REAL : info == C_AST_WORD_IMAG ? C_AST_IMAG : C_AST_EXTENSION,
                                            C_AST_PREC_PREFIX, builder->node_count, position);
                        break;
                    }
                    case C_AST_WORD_GENERIC:
                    {
                        frame->b = builder->node_count;
                        frame->d = nest;
                        frame->state = C_AST_EXPR_AFTER_PRIMARY;
                        c_ast_push(builder, C_AST_FRAME_GENERIC, C_AST_GENERIC_OPEN, 0, builder->node_count);
                        running = false;
                        break;
                    }
                    case C_AST_WORD_TRUE:
                    case C_AST_WORD_FALSE:
                    {
                        begin = builder->node_count;
                        c_ast_append(builder, C_AST_BOOLEAN_CONSTANT, begin, position, info == C_AST_WORD_TRUE);
                        c_ast_advance(builder);
                        state = C_AST_EXPR_POSTFIX;
                        break;
                    }
                    case C_AST_WORD_NULLPTR:
                    {
                        begin = builder->node_count;
                        c_ast_append(builder, C_AST_NULLPTR, begin, position, 0);
                        c_ast_advance(builder);
                        state = C_AST_EXPR_POSTFIX;
                        break;
                    }
                    default:
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an expression"));
                        break;
                    }
                    }
                }
                break;
            }
            case C_TOKEN_PREPROCESSING_NUMBER:
            {
                begin = builder->node_count;
                c_ast_append(builder, C_AST_NUMBER, begin, position, 0);
                c_ast_advance(builder);
                state = C_AST_EXPR_POSTFIX;
                break;
            }
            case C_TOKEN_CHARACTER_LITERAL:
            {
                begin = builder->node_count;
                c_ast_append(builder, C_AST_CHARACTER, begin, position, 0);
                c_ast_advance(builder);
                state = C_AST_EXPR_POSTFIX;
                break;
            }
            case C_TOKEN_STRING_LITERAL:
            {
                begin = builder->node_count;
                c_ast_append_string_run(builder);
                state = C_AST_EXPR_POSTFIX;
                break;
            }
            case C_TOKEN_PUNCTUATOR:
            {
                switch ((CPunctuator)token.punctuator)
                {
                case C_PUNCTUATOR_LEFT_PARENTHESIS:
                {
                    CToken next = c_ast_peek(builder, 1);
                    if (c_ast_is_punctuator(next, C_PUNCTUATOR_LEFT_BRACE))
                    {
                        frame->b = builder->node_count;
                        frame->c = position;
                        frame->d = nest;
                        frame->state = C_AST_EXPR_AFTER_STATEMENT;
                        c_ast_advance(builder);
                        c_ast_push_block(builder, 0);
                        running = false;
                    }
                    else if (c_ast_type_name_at(builder, 1))
                    {
                        frame->b = builder->node_count;
                        frame->c = position;
                        frame->d = nest;
                        frame->state = C_AST_EXPR_AFTER_CAST_TYPE;
                        c_ast_advance(builder);
                        c_ast_push_type_name(builder, 0);
                        running = false;
                    }
                    else
                    {
                        c_ast_operator_push(builder, C_AST_OP_PAREN, 0, builder->node_count, position);
                        nest += 1;
                        c_ast_advance(builder);
                    }
                    break;
                }
                case C_PUNCTUATOR_AMPERSAND_AMPERSAND:
                {
                    CToken label = c_ast_peek(builder, 1);
                    if (label.kind == C_TOKEN_IDENTIFIER)
                    {
                        begin = builder->node_count;
                        c_ast_append(builder, C_AST_LABEL_ADDRESS, begin, position + 1, label.symbol);
                        c_ast_advance(builder);
                        c_ast_advance(builder);
                        state = C_AST_EXPR_OPERATOR;
                    }
                    else
                    {
                        c_ast_fail(builder, position, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("expected a label name after '&&'"));
                    }
                    break;
                }
                default:
                {
                    u32 kind = c_ast_prefix_kinds[token.punctuator];
                    if (kind)
                    {
                        c_ast_operator_push(builder, kind, C_AST_PREC_PREFIX, builder->node_count, position);
                        c_ast_advance(builder);
                    }
                    else
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an expression"));
                    }
                    break;
                }
                }
                break;
            }
            default:
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an expression"));
                break;
            }
            }
            break;
        }
        case C_AST_EXPR_POSTFIX:
        {
            CToken token = c_ast_peek(builder, 0);
            u32 position = builder->position;
            switch ((CPunctuator)token.punctuator)
            {
            case C_PUNCTUATOR_LEFT_BRACKET:
            {
                frame->b = begin;
                frame->c = position;
                frame->d = nest;
                frame->state = C_AST_EXPR_AFTER_INDEX;
                c_ast_advance(builder);
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
                break;
            }
            case C_PUNCTUATOR_LEFT_PARENTHESIS:
            {
                frame->b = begin;
                frame->d = nest;
                frame->state = C_AST_EXPR_AFTER_CALL;
                c_ast_push(builder, C_AST_FRAME_CALL, C_AST_CALL_OPEN, callee_offsetof ? C_AST_CALL_OFFSETOF : 0, begin);
                running = false;
                break;
            }
            case C_PUNCTUATOR_DOT:
            case C_PUNCTUATOR_ARROW:
            {
                c_ast_advance(builder);
                CToken name = c_ast_peek(builder, 0);
                if (name.kind == C_TOKEN_IDENTIFIER)
                {
                    c_ast_append(builder, token.punctuator == C_PUNCTUATOR_DOT ? C_AST_MEMBER : C_AST_MEMBER_ARROW, begin, builder->position,
                                 name.symbol);
                    c_ast_advance(builder);
                    callee_offsetof = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a member name"));
                }
                break;
            }
            case C_PUNCTUATOR_PLUS_PLUS:
            {
                c_ast_append(builder, C_AST_POST_INCREMENT, begin, position, 0);
                c_ast_advance(builder);
                callee_offsetof = false;
                break;
            }
            case C_PUNCTUATOR_MINUS_MINUS:
            {
                c_ast_append(builder, C_AST_POST_DECREMENT, begin, position, 0);
                c_ast_advance(builder);
                callee_offsetof = false;
                break;
            }
            default:
            {
                state = C_AST_EXPR_OPERATOR;
                break;
            }
            }
            break;
        }
        case C_AST_EXPR_OPERATOR:
        {
            CToken token = c_ast_peek(builder, 0);
            u32 position = builder->position;
            u32 info = c_ast_binary_info[token.punctuator];
            u32 precedence = info & 0xffu;
            u32 floor = nest ? (u32)C_AST_PREC_COMMA : minimum;
            if (precedence && precedence >= floor)
            {
                if (precedence == C_AST_PREC_CONDITIONAL)
                {
                    begin = c_ast_reduce(builder, base, C_AST_PREC_CONDITIONAL + 1, begin);
                    c_ast_advance(builder);
                    if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
                    {
                        c_ast_advance(builder);
                        c_ast_operator_push(builder, C_AST_CONDITIONAL_OMITTED, C_AST_PREC_CONDITIONAL, begin, position);
                    }
                    else
                    {
                        c_ast_operator_push(builder, C_AST_OP_QUESTION, 0, begin, position);
                        nest += 1;
                    }
                }
                else
                {
                    // Assignment associates to the right: an equal-precedence
                    // operator below stays pending. Every other level reduces
                    // its equals first.
                    begin = c_ast_reduce(builder, base, precedence == C_AST_PREC_ASSIGN ? precedence + 1 : precedence, begin);
                    c_ast_operator_push(builder, info >> 8, precedence, begin, position);
                    c_ast_advance(builder);
                }
                state = C_AST_EXPR_OPERAND;
            }
            else if (nest && c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                begin = c_ast_reduce(builder, base, C_AST_PREC_COMMA, begin);
                CAstOperator marker = builder->operators[builder->operator_count - 1];
                if (marker.kind == C_AST_OP_PAREN)
                {
                    builder->operator_count -= 1;
                    begin = marker.begin;
                    nest -= 1;
                    c_ast_advance(builder);
                    callee_offsetof = false;
                    state = C_AST_EXPR_POSTFIX;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("':' to complete the conditional expression"));
                }
            }
            else if (nest && c_ast_is_punctuator(token, C_PUNCTUATOR_COLON))
            {
                begin = c_ast_reduce(builder, base, C_AST_PREC_COMMA, begin);
                CAstOperator marker = builder->operators[builder->operator_count - 1];
                if (marker.kind == C_AST_OP_QUESTION)
                {
                    builder->operator_count -= 1;
                    c_ast_operator_push(builder, C_AST_CONDITIONAL, C_AST_PREC_CONDITIONAL, marker.begin, marker.token);
                    nest -= 1;
                    c_ast_advance(builder);
                    state = C_AST_EXPR_OPERAND;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' to close the parenthesized expression"));
                }
            }
            else if (nest)
            {
                (void)c_ast_reduce(builder, base, C_AST_PREC_COMMA, begin);
                CAstOperator marker = builder->operators[builder->operator_count - 1];
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER,
                                    marker.kind == C_AST_OP_PAREN ? S8("')' to close the parenthesized expression")
                                                                  : S8("':' to complete the conditional expression"));
            }
            else
            {
                begin = c_ast_reduce(builder, base, C_AST_PREC_COMMA, begin);
                c_ast_pop(builder);
                running = false;
            }
            break;
        }
        case C_AST_EXPR_AFTER_PRIMARY:
        {
            state = C_AST_EXPR_POSTFIX;
            break;
        }
        case C_AST_EXPR_AFTER_CALL:
        {
            state = C_AST_EXPR_POSTFIX;
            break;
        }
        case C_AST_EXPR_AFTER_INDEX:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACKET))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_INDEX, begin, frame->c, 0);
                state = C_AST_EXPR_POSTFIX;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("']' after the subscript"));
            }
            break;
        }
        case C_AST_EXPR_AFTER_CAST_TYPE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_BRACE))
                {
                    frame->state = C_AST_EXPR_AFTER_COMPOUND_LITERAL;
                    c_ast_push(builder, C_AST_FRAME_INIT_LIST, C_AST_INIT_LIST_OPEN, 0, builder->node_count);
                    running = false;
                }
                else
                {
                    c_ast_operator_push(builder, C_AST_CAST, C_AST_PREC_PREFIX, begin, frame->c);
                    state = C_AST_EXPR_OPERAND;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the type name"));
            }
            break;
        }
        case C_AST_EXPR_AFTER_COMPOUND_LITERAL:
        {
            c_ast_append(builder, C_AST_COMPOUND_LITERAL, begin, frame->c, 0);
            state = C_AST_EXPR_POSTFIX;
            break;
        }
        case C_AST_EXPR_AFTER_SIZEOF_TYPE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                bool alignof_form = (frame->flags & C_AST_EXPR_ALIGNOF) != 0;
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_BRACE))
                {
                    // `sizeof (T){...}`: the operand is a compound literal.
                    c_ast_operator_push(builder, alignof_form ? C_AST_ALIGNOF_EXPRESSION : C_AST_SIZEOF_EXPRESSION, C_AST_PREC_PREFIX, begin, frame->c);
                    frame->state = C_AST_EXPR_AFTER_SIZEOF_COMPOUND;
                    c_ast_push(builder, C_AST_FRAME_INIT_LIST, C_AST_INIT_LIST_OPEN, 0, builder->node_count);
                    running = false;
                }
                else
                {
                    c_ast_append(builder, alignof_form ? C_AST_ALIGNOF_TYPE : C_AST_SIZEOF_TYPE, begin, frame->c, 0);
                    state = C_AST_EXPR_OPERATOR;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the type name"));
            }
            break;
        }
        case C_AST_EXPR_AFTER_SIZEOF_COMPOUND:
        {
            c_ast_append(builder, C_AST_COMPOUND_LITERAL, begin, frame->c + 1, 0);
            state = C_AST_EXPR_POSTFIX;
            break;
        }
        case C_AST_EXPR_AFTER_STATEMENT:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_STATEMENT_EXPRESSION, begin, frame->c, 0);
                state = C_AST_EXPR_POSTFIX;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the statement expression"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `callee ( arguments )`: the frame begins at the callee's begin; the cursor
// is on the `(`.
BUSTER_GLOBAL_LOCAL void c_ast_step_call(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_CALL_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_CALL, frame->begin, frame->a, 0);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                frame->state = C_AST_CALL_ARGUMENT;
            }
            break;
        }
        case C_AST_CALL_ARGUMENT:
        {
            // An argument that opens like a type-name is one (va_arg,
            // types_compatible_p, offsetof's first argument), except after
            // offsetof's first, where it names a member.
            bool type_argument =
                c_ast_type_name_at(builder, 0) && !((frame->flags & C_AST_CALL_OFFSETOF) && frame->b > 0);
            frame->b += 1;
            frame->state = C_AST_CALL_AFTER_ARGUMENT;
            if (type_argument)
            {
                c_ast_push_type_name(builder, 0);
                running = false;
            }
            else
            {
                bool member_designator = (frame->flags & C_AST_CALL_OFFSETOF) && frame->b > 1;
                if (!c_ast_append_simple_operand(builder, c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS, member_designator))
                {
                    c_ast_push_expr(builder, C_AST_PREC_ASSIGN | (member_designator ? C_AST_EXPR_MEMBER_DESIGNATOR : 0), 0);
                    running = false;
                }
            }
            break;
        }
        case C_AST_CALL_AFTER_ARGUMENT:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_CALL_ARGUMENT;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_CALL, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or ')' after the call arguments"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `_Generic ( control , association , ... )`.
BUSTER_GLOBAL_LOCAL void c_ast_step_generic(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_GENERIC_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_GENERIC_AFTER_CONTROL;
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after '_Generic'"));
            }
            break;
        }
        case C_AST_GENERIC_AFTER_CONTROL:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_GENERIC_ITEM;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("',' after the controlling expression"));
            }
            break;
        }
        case C_AST_GENERIC_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            frame->c = builder->node_count;
            frame->d = builder->position;
            if (c_ast_is_word(builder, token, C_AST_WORD_DEFAULT))
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
                {
                    c_ast_advance(builder);
                    frame->state = C_AST_GENERIC_AFTER_DEFAULT;
                    c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                    running = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("':' after 'default'"));
                }
            }
            else
            {
                frame->state = C_AST_GENERIC_AFTER_TYPE;
                c_ast_push_type_name(builder, 0);
                running = false;
            }
            break;
        }
        case C_AST_GENERIC_AFTER_TYPE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_GENERIC_AFTER_ASSOCIATION;
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("':' after the association type"));
            }
            break;
        }
        case C_AST_GENERIC_AFTER_ASSOCIATION:
        {
            c_ast_append(builder, C_AST_GENERIC_ASSOCIATION, frame->c, frame->d, 0);
            frame->b += 1;
            frame->state = C_AST_GENERIC_AFTER_ITEM;
            break;
        }
        case C_AST_GENERIC_AFTER_DEFAULT:
        {
            c_ast_append(builder, C_AST_GENERIC_DEFAULT, frame->c, frame->d, 0);
            frame->b += 1;
            frame->state = C_AST_GENERIC_AFTER_ITEM;
            break;
        }
        case C_AST_GENERIC_AFTER_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_GENERIC_ITEM;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_GENERIC_SELECTION, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or ')' in the generic selection"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- initializers ---------------------------------------------------------

// `{ item, item, ... }`: an item is an initializer or a designation.
BUSTER_GLOBAL_LOCAL void c_ast_step_init_list(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_INIT_LIST_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_INIT_LIST_ITEM;
            break;
        }
        case C_AST_INIT_LIST_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_INITIALIZER_LIST, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                frame->b += 1;
                frame->state = C_AST_INIT_LIST_AFTER_ITEM;
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_DOT) || c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    CAstFrame* designation = c_ast_push(builder, C_AST_FRAME_DESIGNATION, C_AST_DESIGNATION_NEXT, 0, builder->node_count);
                    designation->a = builder->position;
                    running = false;
                }
                else if (!c_ast_append_simple_operand(builder, token, C_PUNCTUATOR_RIGHT_BRACE, false))
                {
                    c_ast_push_initializer(builder);
                    running = false;
                }
            }
            break;
        }
        case C_AST_INIT_LIST_AFTER_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_INIT_LIST_ITEM;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                frame->state = C_AST_INIT_LIST_ITEM;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or '}' in the initializer list"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `.member`, `[index]` and GNU `[low ... high]` designators, then `=` and the
// value: one DESIGNATION.
BUSTER_GLOBAL_LOCAL void c_ast_step_designation(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_DESIGNATION_NEXT:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_DOT))
            {
                c_ast_advance(builder);
                CToken name = c_ast_peek(builder, 0);
                if (name.kind == C_TOKEN_IDENTIFIER)
                {
                    c_ast_append(builder, C_AST_DESIGNATOR_MEMBER, builder->node_count, builder->position, name.symbol);
                    c_ast_advance(builder);
                    frame->b += 1;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a member name after '.'"));
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET))
            {
                frame->c = builder->node_count;
                frame->d = builder->position;
                frame->state = C_AST_DESIGNATION_AFTER_INDEX;
                c_ast_advance(builder);
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            else
            {
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_ASSIGN))
                {
                    c_ast_advance(builder);
                }
                frame->state = C_AST_DESIGNATION_AFTER_VALUE;
                c_ast_push_initializer(builder);
                running = false;
            }
            break;
        }
        case C_AST_DESIGNATION_AFTER_INDEX:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_ELLIPSIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_DESIGNATION_AFTER_RANGE;
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACKET))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_DESIGNATOR_INDEX, frame->c, frame->d, 0);
                frame->b += 1;
                frame->state = C_AST_DESIGNATION_NEXT;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("']' after the array designator"));
            }
            break;
        }
        case C_AST_DESIGNATION_AFTER_RANGE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACKET))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_DESIGNATOR_RANGE, frame->c, frame->d, 0);
                frame->b += 1;
                frame->state = C_AST_DESIGNATION_NEXT;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("']' after the array range designator"));
            }
            break;
        }
        case C_AST_DESIGNATION_AFTER_VALUE:
        {
            c_ast_append(builder, C_AST_DESIGNATION, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- declaration states ---------------------------------------------------

enum
{
    C_AST_SPECS_LOOP,
};

enum
{
    // F_SPECS flags.
    C_AST_SPECS_SEEN_TYPE = 1,
    C_AST_SPECS_TYPEDEF = 2,
};

enum
{
    C_AST_KEYWORD_PAREN_OPEN,
    C_AST_KEYWORD_PAREN_CLOSE,
    // F_KEYWORD_PAREN flags: what the parentheses may hold.
    C_AST_KEYWORD_PAREN_TYPE = 1,
    C_AST_KEYWORD_PAREN_EXPR = 2,
    C_AST_KEYWORD_PAREN_COMMA = 4,
};

enum
{
    C_AST_DECLARATOR_PREFIX,
    C_AST_DECLARATOR_DIRECT,
    C_AST_DECLARATOR_AFTER_NESTED,
    C_AST_DECLARATOR_SUFFIX,
    C_AST_DECLARATOR_FINISH,
};

enum
{
    // F_DECLARATOR flags. The mode is what a declarator at this position may
    // be: it names an entity (NAMED), it is abstract (ABSTRACT), or either
    // (EITHER, a parameter).
    C_AST_DECLARATOR_NAMED = 1,
    C_AST_DECLARATOR_ABSTRACT = 2,
    C_AST_DECLARATOR_EITHER = 3,
    C_AST_DECLARATOR_MODE_MASK = 3,
    C_AST_DECLARATOR_NESTED = 4,
    C_AST_DECLARATOR_HAS_INNER = 8,
    C_AST_DECLARATOR_DECORATING = 16,
    C_AST_DECLARATOR_FIRST_SHIFT = 5,
    C_AST_DECLARATOR_FIRST_MASK = 3 << C_AST_DECLARATOR_FIRST_SHIFT,
    // The first derivation from the name outward.
    C_AST_DERIVATION_NONE = 0,
    C_AST_DERIVATION_POINTER = 1,
    C_AST_DERIVATION_ARRAY = 2,
    C_AST_DERIVATION_FUNCTION = 3,
};

enum
{
    C_AST_ARRAY_SUFFIX_OPEN,
    C_AST_ARRAY_SUFFIX_AFTER_SIZE,
    // F_ARRAY_SUFFIX flag: the suffix has an inner declarator.
    C_AST_ARRAY_SUFFIX_INNER = 1,
};

enum
{
    C_AST_FUNCTION_SUFFIX_OPEN,
    C_AST_FUNCTION_SUFFIX_AFTER_LIST,
    // F_FUNCTION_SUFFIX flags.
    C_AST_FUNCTION_SUFFIX_INNER = 1,
    C_AST_FUNCTION_SUFFIX_CAPTURE = 2,
};

enum
{
    C_AST_PARAM_LIST_OPEN,
    C_AST_PARAM_LIST_ITEM,
    C_AST_PARAM_LIST_AFTER_ITEM,
    // F_PARAM_LIST flags.
    C_AST_PARAM_LIST_CAPTURE = 1,
    C_AST_PARAM_LIST_VARIADIC = 2,
};

enum
{
    C_AST_PARAM_START,
    C_AST_PARAM_AFTER_SPECS,
    C_AST_PARAM_AFTER_DECLARATOR,
    C_AST_PARAM_FINISH,
};

enum
{
    C_AST_INIT_DECL_START,
    C_AST_INIT_DECL_AFTER_LEADING_ATTRIBUTES,
    C_AST_INIT_DECL_AFTER_DECLARATOR,
    C_AST_INIT_DECL_SUFFIXES,
    C_AST_INIT_DECL_FINISH,
    // F_INIT_DECL flags.
    C_AST_INIT_DECL_TYPEDEF = 1,
    C_AST_INIT_DECL_FILE_SCOPE = 2,
    C_AST_INIT_DECL_ALLOW_DEFINITION = 4,
};

enum
{
    C_AST_DECL_START,
    C_AST_DECL_AFTER_SPECS,
    C_AST_DECL_DECLARATOR,
    C_AST_DECL_AFTER_INIT_DECL,
    C_AST_DECL_BODY_START,
    C_AST_DECL_KNR,
    C_AST_DECL_AFTER_BODY,
    // F_DECL flags.
    C_AST_DECL_FILE_SCOPE = 1,
    C_AST_DECL_ALLOW_DEFINITION = 2,
    C_AST_DECL_TYPEDEF = 4,
};

// ---- declaration specifiers -----------------------------------------------

// Starts a specifier list. `pre_items` items already sit in the stream from
// `begin` (an attribute list read before the declaration was known to be
// one); `extension_run` consumed `__extension__` tokens become the leading
// words.
BUSTER_GLOBAL_LOCAL void c_ast_push_specs(CAstBuilder* builder, u32 begin, u32 pre_items, u32 extension_run, u32 first_token)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_SPECS, C_AST_SPECS_LOOP, 0, begin);
    frame->a = pre_items + extension_run;
    frame->b = first_token;
    for (u32 index = 0; index < extension_run; index += 1)
    {
        c_ast_append(builder, C_AST_SPECIFIER_WORD, builder->node_count, builder->position - extension_run + index, C_AST_WORD_EXTENSION);
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_push_attribute_list(CAstBuilder* builder)
{
    c_ast_push(builder, C_AST_FRAME_ATTR_LIST, 0, 0, builder->node_count);
}

BUSTER_GLOBAL_LOCAL void c_ast_push_keyword_paren(CAstBuilder* builder, CAstKind kind, u32 flags)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_KEYWORD_PAREN, C_AST_KEYWORD_PAREN_OPEN, flags, builder->node_count);
    frame->a = (u32)kind;
}

// The declaration specifiers of a declaration, parameter or type-name: simple
// words, tag specifiers, typeof, `_Atomic (`, alignment, `_BitInt`, attribute
// runs and a typedef name when no type specifier has been seen yet. The loop
// stops at the first token that is none of them; that token begins the
// declarator.
BUSTER_GLOBAL_LOCAL void c_ast_step_specs(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        CToken token = c_ast_peek(builder, 0);
        u32 position = builder->position;
        bool finished = false;
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            u32 info = c_ast_info(builder, token);
            u32 class_flags = c_ast_info_flags[info];
            if (info == C_AST_INFO_TYPEDEF)
            {
                if (frame->flags & C_AST_SPECS_SEEN_TYPE)
                {
                    finished = true;
                }
                else
                {
                    c_ast_append(builder, C_AST_TYPEDEF_NAME, builder->node_count, position, token.symbol);
                    c_ast_advance(builder);
                    frame->a += 1;
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                }
            }
            else if (class_flags & C_AST_FLAG_SPEC)
            {
                c_ast_append(builder, C_AST_SPECIFIER_WORD, builder->node_count, position, info);
                c_ast_advance(builder);
                frame->a += 1;
                if (class_flags & C_AST_FLAG_TYPE_WORD)
                {
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                }
                if (info == C_AST_WORD_TYPEDEF)
                {
                    frame->flags |= C_AST_SPECS_TYPEDEF;
                }
            }
            else
            {
                switch ((CAstWord)info)
                {
                case C_AST_WORD_ATOMIC:
                {
                    if (c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        frame->a += 1;
                        frame->flags |= C_AST_SPECS_SEEN_TYPE;
                        c_ast_push_keyword_paren(builder, C_AST_ATOMIC_SPECIFIER, C_AST_KEYWORD_PAREN_TYPE);
                        running = false;
                    }
                    else
                    {
                        c_ast_append(builder, C_AST_SPECIFIER_WORD, builder->node_count, position, info);
                        c_ast_advance(builder);
                        frame->a += 1;
                    }
                    break;
                }
                case C_AST_WORD_STRUCT:
                case C_AST_WORD_UNION:
                {
                    frame->a += 1;
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                    c_ast_push(builder, C_AST_FRAME_STRUCT, 0, info == C_AST_WORD_UNION ? 1 : 0, builder->node_count);
                    running = false;
                    break;
                }
                case C_AST_WORD_ENUM:
                {
                    frame->a += 1;
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                    c_ast_push(builder, C_AST_FRAME_ENUM, 0, 0, builder->node_count);
                    running = false;
                    break;
                }
                case C_AST_WORD_TYPEOF:
                case C_AST_WORD_TYPEOF_UNQUAL:
                {
                    frame->a += 1;
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                    c_ast_push_keyword_paren(builder, info == C_AST_WORD_TYPEOF ? C_AST_TYPEOF : C_AST_TYPEOF_UNQUAL,
                                             C_AST_KEYWORD_PAREN_TYPE | C_AST_KEYWORD_PAREN_EXPR);
                    running = false;
                    break;
                }
                case C_AST_WORD_ALIGNAS:
                {
                    frame->a += 1;
                    c_ast_push_keyword_paren(builder, C_AST_ALIGNAS, C_AST_KEYWORD_PAREN_TYPE | C_AST_KEYWORD_PAREN_EXPR);
                    running = false;
                    break;
                }
                case C_AST_WORD_BITINT:
                {
                    frame->a += 1;
                    frame->flags |= C_AST_SPECS_SEEN_TYPE;
                    c_ast_push_keyword_paren(builder, C_AST_BITINT, C_AST_KEYWORD_PAREN_EXPR);
                    running = false;
                    break;
                }
                case C_AST_WORD_ATTRIBUTE:
                case C_AST_WORD_DECLSPEC:
                {
                    frame->a += 1;
                    c_ast_push_attribute_list(builder);
                    running = false;
                    break;
                }
                default:
                {
                    finished = true;
                    break;
                }
                }
            }
        }
        else if (c_ast_is_attribute_start(builder, token))
        {
            frame->a += 1;
            c_ast_push_attribute_list(builder);
            running = false;
        }
        else
        {
            finished = true;
        }
        if (finished)
        {
            builder->ret_items = frame->a;
            builder->ret_typedef = (frame->flags & C_AST_SPECS_TYPEDEF) != 0;
            builder->ret_seen_type = (frame->flags & C_AST_SPECS_SEEN_TYPE) != 0;
            c_ast_append(builder, C_AST_DECL_SPECIFIERS, frame->begin, frame->b, frame->a);
            c_ast_pop(builder);
            running = false;
        }
    }
}

// `typeof ( x )`, `_Atomic ( T )`, `_Alignas ( x )`, `_BitInt ( n )`: a
// keyword, parentheses, and a type-name or an expression inside.
BUSTER_GLOBAL_LOCAL void c_ast_step_keyword_paren(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_KEYWORD_PAREN_OPEN:
        {
            frame->b = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                bool type = (frame->flags & C_AST_KEYWORD_PAREN_TYPE) &&
                            (c_ast_type_name_at(builder, 0) || !(frame->flags & C_AST_KEYWORD_PAREN_EXPR));
                frame->state = C_AST_KEYWORD_PAREN_CLOSE;
                if (type)
                {
                    c_ast_push_type_name(builder, 0);
                }
                else
                {
                    c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                }
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after the keyword"));
            }
            break;
        }
        case C_AST_KEYWORD_PAREN_CLOSE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, (CAstKind)frame->a, frame->begin, frame->b, 0);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')'"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_push_declarator(CAstBuilder* builder, u32 flags)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_DECLARATOR, C_AST_DECLARATOR_PREFIX, flags, builder->node_count);
    frame->a = builder->pointer_count;
    frame->c = C_AST_NODE_INVALID;
}

// A type-name: specifiers and an optional abstract declarator.
BUSTER_GLOBAL_LOCAL void c_ast_step_type_name(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_TYPE_NAME_START:
        {
            frame->a = builder->position;
            frame->state = C_AST_TYPE_NAME_AFTER_SPECS;
            c_ast_push_specs(builder, builder->node_count, 0, 0, builder->position);
            running = false;
            break;
        }
        case C_AST_TYPE_NAME_AFTER_SPECS:
        {
            if (builder->ret_items == 0)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a type name"));
            }
            else
            {
                CToken token = c_ast_peek(builder, 0);
                bool declarator = !(frame->flags & C_AST_TYPE_NAME_NO_DECLARATOR) &&
                                  (c_ast_is_punctuator(token, C_PUNCTUATOR_STAR) || c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS) ||
                                   (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET) &&
                                    !c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_BRACKET)));
                frame->state = C_AST_TYPE_NAME_END;
                if (declarator)
                {
                    frame->b = 1;
                    c_ast_push_declarator(builder, C_AST_DECLARATOR_ABSTRACT);
                    running = false;
                }
            }
            break;
        }
        case C_AST_TYPE_NAME_END:
        {
            c_ast_append(builder, C_AST_TYPE_NAME, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- declarators ----------------------------------------------------------

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_declarator_first(CAstFrame const* frame)
{
    return (frame->flags & C_AST_DECLARATOR_FIRST_MASK) >> C_AST_DECLARATOR_FIRST_SHIFT;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_declarator_note_derivation(CAstFrame* frame, u32 derivation)
{
    if (c_ast_declarator_first(frame) == C_AST_DERIVATION_NONE)
    {
        frame->flags = (u16)(frame->flags | (derivation << C_AST_DECLARATOR_FIRST_SHIFT));
    }
}

// pointer* direct-declarator suffix*. The pointers of a level are stacked as
// they are read and emitted after that level's suffixes, the `*` nearest the
// name first; a nested `( declarator )` is a child frame whose result is the
// inner declarator of this level's suffixes.
BUSTER_GLOBAL_LOCAL void c_ast_step_declarator(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_DECLARATOR_PREFIX:
        {
            CToken token = c_ast_peek(builder, 0);
            u32 position = builder->position;
            if (frame->flags & C_AST_DECLARATOR_DECORATING)
            {
                CAstPointer* pointer = &builder->pointers[builder->pointer_count - 1];
                u32 qualifier = c_ast_qualifier_flag(c_ast_info(builder, token));
                if (qualifier)
                {
                    pointer->flags |= qualifier;
                    c_ast_advance(builder);
                }
                else if (c_ast_is_attribute_start(builder, token))
                {
                    if (pointer->groups == 0)
                    {
                        pointer->attribute_token = position;
                    }
                    pointer->groups += 1;
                    c_ast_push(builder, C_AST_FRAME_ATTR_GROUP, 0, 0, builder->node_count);
                    running = false;
                }
                else if (pointer->token == C_AST_NODE_INVALID)
                {
                    // Attributes ahead of their `*`.
                    if (c_ast_is_punctuator(token, C_PUNCTUATOR_STAR))
                    {
                        pointer->token = position;
                        c_ast_advance(builder);
                    }
                    else
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'*' after the attributes"));
                    }
                }
                else
                {
                    if (pointer->groups)
                    {
                        c_ast_append(builder, C_AST_ATTRIBUTE_LIST, pointer->begin, pointer->attribute_token, pointer->groups);
                    }
                    frame->flags = (u16)(frame->flags & ~C_AST_DECLARATOR_DECORATING);
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_STAR))
            {
                c_ast_pointer_push(builder, builder->node_count, position);
                c_ast_advance(builder);
                frame->flags |= C_AST_DECLARATOR_DECORATING;
            }
            else if (c_ast_is_attribute_start(builder, token))
            {
                c_ast_pointer_push(builder, builder->node_count, C_AST_NODE_INVALID);
                frame->flags |= C_AST_DECLARATOR_DECORATING;
            }
            else
            {
                frame->state = C_AST_DECLARATOR_DIRECT;
            }
            break;
        }
        case C_AST_DECLARATOR_DIRECT:
        {
            CToken token = c_ast_peek(builder, 0);
            u32 mode = frame->flags & C_AST_DECLARATOR_MODE_MASK;
            frame->b = builder->node_count;
            frame->state = C_AST_DECLARATOR_SUFFIX;
            if (token.kind == C_TOKEN_IDENTIFIER)
            {
                if (c_ast_is_name_info(c_ast_info(builder, token)))
                {
                    if (mode == C_AST_DECLARATOR_ABSTRACT)
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an abstract declarator"));
                    }
                    else
                    {
                        frame->c = builder->position;
                        frame->d = c_ast_symbol_of(builder, token);
                        c_ast_append(builder, C_AST_DECLARATOR_NAME, builder->node_count, builder->position, token.symbol);
                        c_ast_advance(builder);
                        frame->flags |= C_AST_DECLARATOR_HAS_INNER;
                    }
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                // A parenthesis here is a nested declarator, except where an
                // abstract declarator is possible and what follows is `)` or
                // a type-name: then it is a function suffix with no inner.
                CToken next = c_ast_peek(builder, 1);
                bool nested = mode == C_AST_DECLARATOR_NAMED ||
                              !(c_ast_is_punctuator(next, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_ast_is_declaration_start(builder, next));
                if (nested)
                {
                    c_ast_advance(builder);
                    frame->state = C_AST_DECLARATOR_AFTER_NESTED;
                    c_ast_push_declarator(builder, C_AST_DECLARATOR_NESTED | mode);
                    running = false;
                }
            }
            break;
        }
        case C_AST_DECLARATOR_AFTER_NESTED:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->c = builder->ret_name_token;
                frame->d = builder->ret_name_symbol;
                c_ast_declarator_note_derivation(frame, builder->ret_first_derivation);
                frame->flags |= C_AST_DECLARATOR_HAS_INNER;
                frame->state = C_AST_DECLARATOR_SUFFIX;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the nested declarator"));
            }
            break;
        }
        case C_AST_DECLARATOR_SUFFIX:
        {
            CToken token = c_ast_peek(builder, 0);
            bool array = c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET) &&
                         !c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_BRACKET);
            bool function = c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            if (array || function)
            {
                bool inner = (frame->flags & C_AST_DECLARATOR_HAS_INNER) != 0;
                u32 suffix_begin = inner ? frame->b : builder->node_count;
                bool capture = function && c_ast_declarator_first(frame) == C_AST_DERIVATION_NONE && builder->parameter_depth == 0;
                c_ast_declarator_note_derivation(frame, array ? C_AST_DERIVATION_ARRAY : C_AST_DERIVATION_FUNCTION);
                frame->flags |= C_AST_DECLARATOR_HAS_INNER;
                c_ast_push(builder, array ? C_AST_FRAME_ARRAY_SUFFIX : C_AST_FRAME_FUNCTION_SUFFIX, 0,
                           (inner ? C_AST_FUNCTION_SUFFIX_INNER : 0) | (capture ? C_AST_FUNCTION_SUFFIX_CAPTURE : 0), suffix_begin);
                running = false;
            }
            else
            {
                frame->state = C_AST_DECLARATOR_FINISH;
            }
            break;
        }
        case C_AST_DECLARATOR_FINISH:
        {
            if ((frame->flags & C_AST_DECLARATOR_MODE_MASK) == C_AST_DECLARATOR_NAMED && frame->c == C_AST_NODE_INVALID)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a declarator name"));
            }
            else
            {
                while (builder->pointer_count > frame->a)
                {
                    builder->pointer_count -= 1;
                    CAstPointer pointer = builder->pointers[builder->pointer_count];
                    u32 data = pointer.flags | (pointer.groups ? 1u : 0u) | ((frame->flags & C_AST_DECLARATOR_HAS_INNER) ? 2u : 0u);
                    c_ast_append(builder, C_AST_DECLARATOR_POINTER, pointer.begin, pointer.token, data);
                    c_ast_declarator_note_derivation(frame, C_AST_DERIVATION_POINTER);
                    frame->flags |= C_AST_DECLARATOR_HAS_INNER;
                }
                builder->ret_name_token = frame->c;
                builder->ret_name_symbol = frame->d;
                builder->ret_first_derivation = c_ast_declarator_first(frame);
                c_ast_pop(builder);
                running = false;
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `[ static? qualifiers? size? ]` after a declarator's inner part. The frame
// begins at the inner declarator's begin (or its own first node when there
// is none).
BUSTER_GLOBAL_LOCAL void c_ast_step_array_suffix(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ARRAY_SUFFIX_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            bool qualifying = true;
            while (qualifying)
            {
                u32 info = c_ast_info(builder, c_ast_peek(builder, 0));
                u32 qualifier = c_ast_qualifier_flag(info);
                if (info == C_AST_WORD_STATIC)
                {
                    frame->b |= C_AST_ARRAY_STATIC;
                    c_ast_advance(builder);
                }
                else if (qualifier)
                {
                    frame->b |= qualifier;
                    c_ast_advance(builder);
                }
                else
                {
                    qualifying = false;
                }
            }
            CToken token = c_ast_peek(builder, 0);
            frame->state = C_AST_ARRAY_SUFFIX_AFTER_SIZE;
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_STAR) && c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_RIGHT_BRACKET))
            {
                frame->b |= C_AST_ARRAY_STAR;
                c_ast_advance(builder);
            }
            else if (!c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACKET))
            {
                frame->c = 2;
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            break;
        }
        case C_AST_ARRAY_SUFFIX_AFTER_SIZE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACKET))
            {
                c_ast_advance(builder);
                u32 data = frame->b | frame->c | ((frame->flags & C_AST_ARRAY_SUFFIX_INNER) ? 1u : 0u);
                c_ast_append(builder, C_AST_DECLARATOR_ARRAY, frame->begin, frame->a, data);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("']' after the array size"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `( parameters )`, `( )` or a K&R `( names )` after a declarator's inner
// part.
BUSTER_GLOBAL_LOCAL void c_ast_step_function_suffix(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_FUNCTION_SUFFIX_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            CToken token = c_ast_peek(builder, 0);
            u32 info = c_ast_info(builder, token);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_PARAMETER_LIST, builder->node_count, frame->a, 0);
                frame->state = C_AST_FUNCTION_SUFFIX_AFTER_LIST;
            }
            else if (token.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(info) && info != C_AST_INFO_TYPEDEF &&
                     (c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_COMMA) ||
                      c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_RIGHT_PARENTHESIS)))
            {
                u32 list_begin = builder->node_count;
                u32 count = 0;
                bool listing = true;
                while (listing && !builder->failed)
                {
                    CToken name = c_ast_peek(builder, 0);
                    if (name.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, name)))
                    {
                        c_ast_append(builder, C_AST_DECLARATOR_NAME, builder->node_count, builder->position, name.symbol);
                        if (frame->flags & C_AST_FUNCTION_SUFFIX_CAPTURE)
                        {
                            c_ast_capture_push(builder, c_ast_symbol_of(builder, name));
                        }
                        c_ast_advance(builder);
                        count += 1;
                        CToken separator = c_ast_peek(builder, 0);
                        if (c_ast_is_punctuator(separator, C_PUNCTUATOR_COMMA))
                        {
                            c_ast_advance(builder);
                        }
                        else if (c_ast_is_punctuator(separator, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                        {
                            c_ast_advance(builder);
                            listing = false;
                        }
                        else
                        {
                            c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or ')' in the identifier list"));
                        }
                    }
                    else
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an identifier in the identifier list"));
                    }
                }
                c_ast_append(builder, C_AST_IDENTIFIER_LIST, list_begin, frame->a, count);
                frame->state = C_AST_FUNCTION_SUFFIX_AFTER_LIST;
            }
            else
            {
                u32 open_token = frame->a;
                u32 list_flags = (frame->flags & C_AST_FUNCTION_SUFFIX_CAPTURE) ? C_AST_PARAM_LIST_CAPTURE : 0;
                frame->state = C_AST_FUNCTION_SUFFIX_AFTER_LIST;
                CAstFrame* list = c_ast_push(builder, C_AST_FRAME_PARAM_LIST, C_AST_PARAM_LIST_OPEN, list_flags, builder->node_count);
                list->a = open_token;
                running = false;
            }
            break;
        }
        case C_AST_FUNCTION_SUFFIX_AFTER_LIST:
        {
            c_ast_append(builder, C_AST_DECLARATOR_FUNCTION, frame->begin, frame->a, (frame->flags & C_AST_FUNCTION_SUFFIX_INNER) ? 1u : 0u);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// The parameter list of a function declarator, in its own prototype scope.
// When the declarator may be a function definition's, the names declared in
// that scope are captured before it pops so the body can redeclare them.
BUSTER_GLOBAL_LOCAL void c_ast_step_param_list(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        bool finished = false;
        switch (frame->state)
        {
        case C_AST_PARAM_LIST_OPEN:
        {
            frame->b = c_ast_scope_push(builder);
            builder->parameter_depth += 1;
            frame->state = C_AST_PARAM_LIST_ITEM;
            break;
        }
        case C_AST_PARAM_LIST_ITEM:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_ELLIPSIS))
            {
                c_ast_advance(builder);
                frame->flags |= C_AST_PARAM_LIST_VARIADIC;
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    c_ast_advance(builder);
                    finished = true;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after '...'"));
                }
            }
            else
            {
                frame->c += 1;
                frame->state = C_AST_PARAM_LIST_AFTER_ITEM;
                c_ast_push(builder, C_AST_FRAME_PARAM, C_AST_PARAM_START, 0, builder->node_count);
                running = false;
            }
            break;
        }
        case C_AST_PARAM_LIST_AFTER_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_PARAM_LIST_ITEM;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                finished = true;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or ')' in the parameter list"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
        if (finished)
        {
            if (frame->flags & C_AST_PARAM_LIST_CAPTURE)
            {
                for (u32 entry = frame->b; entry < builder->undo_count; entry += 1)
                {
                    c_ast_capture_push(builder, builder->undo[entry].symbol);
                }
            }
            c_ast_scope_pop(builder, frame->b);
            builder->parameter_depth -= 1;
            c_ast_append(builder, (frame->flags & C_AST_PARAM_LIST_VARIADIC) ? C_AST_PARAMETER_LIST_VARIADIC : C_AST_PARAMETER_LIST, frame->begin,
                         frame->a, frame->c);
            c_ast_pop(builder);
            running = false;
        }
    }
}

// One parameter: specifiers, an optional declarator (named or abstract) and
// optional trailing attributes. A named one is declared in the prototype
// scope when its declarator completes.
BUSTER_GLOBAL_LOCAL void c_ast_step_param(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_PARAM_START:
        {
            frame->a = builder->position;
            frame->state = C_AST_PARAM_AFTER_SPECS;
            c_ast_push_specs(builder, builder->node_count, 0, 0, builder->position);
            running = false;
            break;
        }
        case C_AST_PARAM_AFTER_SPECS:
        {
            if (builder->ret_items == 0)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a parameter declaration"));
            }
            else
            {
                CToken token = c_ast_peek(builder, 0);
                bool declarator = c_ast_is_punctuator(token, C_PUNCTUATOR_STAR) || c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_PARENTHESIS) ||
                                  (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET) &&
                                   !c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_BRACKET)) ||
                                  (token.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, token)));
                frame->state = C_AST_PARAM_AFTER_DECLARATOR;
                if (declarator)
                {
                    frame->b |= 1;
                    c_ast_push_declarator(builder, C_AST_DECLARATOR_EITHER);
                    running = false;
                }
            }
            break;
        }
        case C_AST_PARAM_AFTER_DECLARATOR:
        {
            if ((frame->b & 1) && builder->ret_name_token != C_AST_NODE_INVALID)
            {
                c_ast_bind(builder, builder->ret_name_symbol, C_AST_INFO_ORDINARY);
            }
            frame->state = C_AST_PARAM_FINISH;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 2;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_PARAM_FINISH:
        {
            c_ast_append(builder, C_AST_PARAMETER, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- init-declarators and declarations ------------------------------------

// `asm ( "name" )` after a declarator; the cursor is on the keyword and the
// `(` is known to follow.
BUSTER_GLOBAL_LOCAL void c_ast_parse_asm_label(CAstBuilder* builder)
{
    u32 begin = builder->node_count;
    u32 keyword = builder->position;
    c_ast_advance(builder);
    c_ast_advance(builder);
    if (c_ast_peek(builder, 0).kind == C_TOKEN_STRING_LITERAL)
    {
        c_ast_append_string_run(builder);
        if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            c_ast_advance(builder);
            c_ast_append(builder, C_AST_ASM_LABEL, begin, keyword, 0);
        }
        else
        {
            c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the assembler label"));
        }
    }
    else
    {
        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a string literal in the assembler label"));
    }
}

// One declarator with its optional assembler label, attributes and
// initializer. The name is declared when the declarator completes, before the
// initializer. At file scope (or, as a GNU nested function, in a block) a
// function declarator followed by `{` or a K&R declaration list is a function
// definition: the frame then leaves the declarator in place and pops without
// a node, setting ret_definition for the declaration frame.
BUSTER_GLOBAL_LOCAL void c_ast_step_init_decl(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_INIT_DECL_START:
        {
            if (builder->parameter_depth == 0)
            {
                // A new outermost declarator: the names a function definition
                // would redeclare in its body start over.
                builder->capture_count = 0;
            }
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                // `int a, __attribute__((x)) b;`: the list precedes its
                // declarator, so it is the declarator's earlier sibling.
                frame->a |= 2;
                frame->state = C_AST_INIT_DECL_AFTER_LEADING_ATTRIBUTES;
                c_ast_push_attribute_list(builder);
            }
            else
            {
                frame->state = C_AST_INIT_DECL_AFTER_DECLARATOR;
                c_ast_push_declarator(builder, C_AST_DECLARATOR_NAMED);
            }
            running = false;
            break;
        }
        case C_AST_INIT_DECL_AFTER_LEADING_ATTRIBUTES:
        {
            frame->state = C_AST_INIT_DECL_AFTER_DECLARATOR;
            c_ast_push_declarator(builder, C_AST_DECLARATOR_NAMED);
            running = false;
            break;
        }
        case C_AST_INIT_DECL_AFTER_DECLARATOR:
        {
            frame->b = builder->ret_name_token;
            frame->c = builder->ret_name_symbol;
            c_ast_bind(builder, frame->c, (frame->flags & C_AST_INIT_DECL_TYPEDEF) ? C_AST_INFO_TYPEDEF : C_AST_INFO_ORDINARY);
            frame->state = C_AST_INIT_DECL_SUFFIXES;
            if ((frame->flags & C_AST_INIT_DECL_ALLOW_DEFINITION) && builder->ret_first_derivation == C_AST_DERIVATION_FUNCTION)
            {
                CToken token = c_ast_peek(builder, 0);
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACE) ||
                    ((frame->flags & C_AST_INIT_DECL_FILE_SCOPE) && c_ast_is_declaration_start(builder, token)))
                {
                    builder->ret_definition = true;
                    c_ast_pop(builder);
                    running = false;
                }
            }
            break;
        }
        case C_AST_INIT_DECL_SUFFIXES:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_word(builder, token, C_AST_WORD_ASM) && c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                if (frame->a & 1)
                {
                    c_ast_fail(builder, builder->position, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a declarator has more than one assembler label"));
                }
                else
                {
                    frame->a |= 1;
                    c_ast_parse_asm_label(builder);
                }
            }
            else if (c_ast_is_attribute_start(builder, token))
            {
                if (frame->a & 2)
                {
                    c_ast_fail(builder, builder->position, C_DIAGNOSTIC_EXPECTED_DECLARATION,
                               S8("attributes of one declarator must be adjacent"));
                }
                else
                {
                    frame->a |= 2;
                    c_ast_push_attribute_list(builder);
                    running = false;
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_ASSIGN))
            {
                c_ast_advance(builder);
                frame->a |= 4;
                frame->state = C_AST_INIT_DECL_FINISH;
                c_ast_push_initializer(builder);
                running = false;
            }
            else
            {
                frame->state = C_AST_INIT_DECL_FINISH;
            }
            break;
        }
        case C_AST_INIT_DECL_FINISH:
        {
            c_ast_append(builder, C_AST_INIT_DECLARATOR, frame->begin, frame->b, frame->a);
            builder->ret_definition = false;
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// Starts a declaration. `begin` is where its first node (including `pre_items`
// already emitted) sits; `extension_run` consumed `__extension__` tokens
// precede the cursor.
BUSTER_GLOBAL_LOCAL void c_ast_push_declaration(CAstBuilder* builder, u32 flags, u32 begin, u32 pre_items, u32 extension_run, u32 first_token)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_DECL, C_AST_DECL_START, flags, begin);
    frame->b = pre_items;
    frame->c = first_token;
    frame->d = extension_run;
}

// A declaration or function definition: specifiers, then init-declarators
// separated by commas, then `;` -- or, for a first declarator that is a
// function, a body.
BUSTER_GLOBAL_LOCAL void c_ast_step_decl(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_DECL_START:
        {
            frame->state = C_AST_DECL_AFTER_SPECS;
            c_ast_push_specs(builder, frame->begin, frame->b, frame->d, frame->c);
            running = false;
            break;
        }
        case C_AST_DECL_AFTER_SPECS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (builder->ret_typedef)
            {
                frame->flags |= C_AST_DECL_TYPEDEF;
            }
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON) && builder->ret_items)
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_DECLARATION, frame->begin, frame->c, 0);
                c_ast_pop(builder);
                running = false;
            }
            else if (builder->ret_items == 0 && !((frame->flags & C_AST_DECL_FILE_SCOPE) && token.kind == C_TOKEN_IDENTIFIER &&
                                                  c_ast_is_name_info(c_ast_info(builder, token))))
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a declaration"));
            }
            else
            {
                frame->state = C_AST_DECL_DECLARATOR;
            }
            break;
        }
        case C_AST_DECL_DECLARATOR:
        {
            u32 flags = ((frame->flags & C_AST_DECL_TYPEDEF) ? C_AST_INIT_DECL_TYPEDEF : 0) |
                        ((frame->flags & C_AST_DECL_FILE_SCOPE) ? C_AST_INIT_DECL_FILE_SCOPE : 0) |
                        ((frame->flags & C_AST_DECL_ALLOW_DEFINITION) && !(frame->flags & C_AST_DECL_TYPEDEF) ? C_AST_INIT_DECL_ALLOW_DEFINITION : 0);
            frame->state = C_AST_DECL_AFTER_INIT_DECL;
            c_ast_push(builder, C_AST_FRAME_INIT_DECL, C_AST_INIT_DECL_START, flags, builder->node_count);
            running = false;
            break;
        }
        case C_AST_DECL_AFTER_INIT_DECL:
        {
            if (builder->ret_definition)
            {
                builder->ret_definition = false;
                frame->state = C_AST_DECL_BODY_START;
            }
            else
            {
                CToken token = c_ast_peek(builder, 0);
                frame->a += 1;
                frame->flags = (u16)(frame->flags & ~C_AST_DECL_ALLOW_DEFINITION);
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
                {
                    c_ast_advance(builder);
                    frame->state = C_AST_DECL_DECLARATOR;
                }
                else if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, C_AST_DECLARATION, frame->begin, frame->c, frame->a);
                    c_ast_pop(builder);
                    running = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("',' or ';' after the declarator"));
                }
            }
            break;
        }
        case C_AST_DECL_BODY_START:
        {
            // The function scope holds the parameters and the body's outermost
            // block; the names the declarator's first function suffix declared
            // are redeclared in it.
            frame->e = c_ast_scope_push(builder);
            for (u32 entry = 0; entry < builder->capture_count; entry += 1)
            {
                c_ast_bind(builder, builder->capture[entry], C_AST_INFO_ORDINARY);
            }
            builder->capture_count = 0;
            frame->a = 0;
            frame->state = C_AST_DECL_KNR;
            break;
        }
        case C_AST_DECL_KNR:
        {
            CToken token = c_ast_peek(builder, 0);
            if ((frame->flags & C_AST_DECL_FILE_SCOPE) && c_ast_is_declaration_start(builder, token))
            {
                frame->a += 1;
                c_ast_push_declaration(builder, 0, builder->node_count, 0, 0, builder->position);
                running = false;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACE))
            {
                frame->state = C_AST_DECL_AFTER_BODY;
                c_ast_push_block(builder, C_AST_BLOCK_NO_SCOPE);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a function body"));
            }
            break;
        }
        case C_AST_DECL_AFTER_BODY:
        {
            c_ast_scope_pop(builder, frame->e);
            c_ast_append(builder, C_AST_FUNCTION_DEFINITION, frame->begin, frame->c, frame->a);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- tags, members, enumerators, attributes, assembly ---------------------

enum
{
    C_AST_STRUCT_START,
    C_AST_STRUCT_AFTER_ATTRIBUTES,
    C_AST_STRUCT_AFTER_BODY,
    C_AST_STRUCT_FINISH,
    // F_STRUCT flag.
    C_AST_STRUCT_UNION = 1,
};

enum
{
    C_AST_MEMBER_LIST_OPEN,
    C_AST_MEMBER_LIST_ITEMS,
};

enum
{
    C_AST_MEMBER_DECL_START,
    C_AST_MEMBER_DECL_AFTER_SPECS,
    C_AST_MEMBER_DECL_AFTER_DECLARATOR,
};

enum
{
    C_AST_MEMBER_DECLARATOR_START,
    C_AST_MEMBER_DECLARATOR_AFTER_LEADING_ATTRIBUTES,
    C_AST_MEMBER_DECLARATOR_AFTER_DECLARATOR,
    C_AST_MEMBER_DECLARATOR_SUFFIXES,
    C_AST_MEMBER_DECLARATOR_AFTER_WIDTH,
    C_AST_MEMBER_DECLARATOR_FINISH,
};

enum
{
    C_AST_ENUM_START,
    C_AST_ENUM_AFTER_ATTRIBUTES,
    C_AST_ENUM_AFTER_FIXED_TYPE,
    C_AST_ENUM_AFTER_BODY,
    C_AST_ENUM_FINISH,
};

enum
{
    C_AST_ENUMERATOR_LIST_OPEN,
    C_AST_ENUMERATOR_LIST_ITEM,
    C_AST_ENUMERATOR_LIST_AFTER_ITEM,
};

enum
{
    C_AST_ENUMERATOR_START,
    C_AST_ENUMERATOR_AFTER_ATTRIBUTES,
    C_AST_ENUMERATOR_AFTER_VALUE,
};

enum
{
    C_AST_ATTRIBUTE_GROUP_OPEN,
    C_AST_ATTRIBUTE_GROUP_ITEM,
    C_AST_ATTRIBUTE_GROUP_ARGUMENT,
    C_AST_ATTRIBUTE_GROUP_AFTER_ARGUMENT,
    // F_ATTR_GROUP flag: the current item is `namespace::name`.
    C_AST_ATTRIBUTE_GROUP_SCOPED = 1,
};

enum
{
    C_AST_STATIC_ASSERT_OPEN,
    C_AST_STATIC_ASSERT_AFTER_CONDITION,
};

enum
{
    C_AST_ASM_STATE_OPEN,
    C_AST_ASM_STATE_NEXT,
    C_AST_ASM_STATE_OPERANDS,
    C_AST_ASM_STATE_AFTER_OPERAND,
    C_AST_ASM_STATE_CLOBBERS,
    C_AST_ASM_STATE_LABELS,
    // F_ASM flags below bit 8 (the C_AST_ASM_* qualifier flags own 8..10).
    C_AST_ASM_NAMED_OPERAND = 1,
    C_AST_ASM_SECTIONS_SHIFT = 1,
    C_AST_ASM_SECTIONS_MASK = 7 << C_AST_ASM_SECTIONS_SHIFT,
    C_AST_ASM_QUALIFIERS = C_AST_ASM_VOLATILE | C_AST_ASM_INLINE | C_AST_ASM_GOTO,
};

// `struct` or `union`: attributes, a tag, a member list, trailing attributes.
BUSTER_GLOBAL_LOCAL void c_ast_step_struct(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_STRUCT_START:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_STRUCT_AFTER_ATTRIBUTES;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 1;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_STRUCT_AFTER_ATTRIBUTES:
        {
            CToken token = c_ast_peek(builder, 0);
            if (token.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, token)))
            {
                c_ast_append(builder, C_AST_TAG_NAME, builder->node_count, builder->position, token.symbol);
                c_ast_advance(builder);
                frame->b |= 2;
                token = c_ast_peek(builder, 0);
            }
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACE))
            {
                frame->b |= 4;
                frame->state = C_AST_STRUCT_AFTER_BODY;
                c_ast_push(builder, C_AST_FRAME_MEMBER_LIST, C_AST_MEMBER_LIST_OPEN, 0, builder->node_count);
                running = false;
            }
            else if (frame->b & 2)
            {
                frame->state = C_AST_STRUCT_FINISH;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a tag name or '{'"));
            }
            break;
        }
        case C_AST_STRUCT_AFTER_BODY:
        {
            frame->state = C_AST_STRUCT_FINISH;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 8;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_STRUCT_FINISH:
        {
            c_ast_append(builder, (frame->flags & C_AST_STRUCT_UNION) ? C_AST_UNION_SPECIFIER : C_AST_STRUCT_SPECIFIER, frame->begin, frame->a,
                         frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `{ member-declarations }`.
BUSTER_GLOBAL_LOCAL void c_ast_step_member_list(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_MEMBER_LIST_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_MEMBER_LIST_ITEMS;
            break;
        }
        case C_AST_MEMBER_LIST_ITEMS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_MEMBER_LIST, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_append(builder, C_AST_EMPTY_DECLARATION, builder->node_count, builder->position, 0);
                c_ast_advance(builder);
                frame->b += 1;
            }
            else if (token.kind == C_TOKEN_PRAGMA)
            {
                c_ast_append(builder, C_AST_PRAGMA, builder->node_count, builder->position, 0);
                c_ast_advance(builder);
                frame->b += 1;
            }
            else if (token.kind == C_TOKEN_END_OF_FILE)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("'}' to close the member list"));
            }
            else
            {
                frame->b += 1;
                if (c_ast_is_word(builder, token, C_AST_WORD_STATIC_ASSERT))
                {
                    c_ast_push(builder, C_AST_FRAME_STATIC_ASSERT, C_AST_STATIC_ASSERT_OPEN, 0, builder->node_count);
                }
                else
                {
                    c_ast_push(builder, C_AST_FRAME_MEMBER_DECL, C_AST_MEMBER_DECL_START, 0, builder->node_count);
                }
                running = false;
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// Specifiers, then member declarators separated by commas, then `;`.
BUSTER_GLOBAL_LOCAL void c_ast_step_member_decl(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_MEMBER_DECL_START:
        {
            frame->b = builder->position;
            frame->state = C_AST_MEMBER_DECL_AFTER_SPECS;
            c_ast_push_specs(builder, builder->node_count, 0, 0, builder->position);
            running = false;
            break;
        }
        case C_AST_MEMBER_DECL_AFTER_SPECS:
        {
            if (builder->ret_items == 0)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a member declaration"));
            }
            else if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_MEMBER_DECLARATION, frame->begin, frame->b, 0);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                frame->a += 1;
                frame->state = C_AST_MEMBER_DECL_AFTER_DECLARATOR;
                c_ast_push(builder, C_AST_FRAME_MEMBER_DECLARATOR, C_AST_MEMBER_DECLARATOR_START, 0, builder->node_count);
                running = false;
            }
            break;
        }
        case C_AST_MEMBER_DECL_AFTER_DECLARATOR:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->a += 1;
                c_ast_push(builder, C_AST_FRAME_MEMBER_DECLARATOR, C_AST_MEMBER_DECLARATOR_START, 0, builder->node_count);
                running = false;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_MEMBER_DECLARATION, frame->begin, frame->b, frame->a);
                c_ast_pop(builder);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("',' or ';' after the member declarator"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// A declarator, a bit-field width and attributes, each optional.
BUSTER_GLOBAL_LOCAL void c_ast_step_member_declarator(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_MEMBER_DECLARATOR_START:
        {
            frame->a = C_AST_NODE_INVALID;
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
            {
                frame->state = C_AST_MEMBER_DECLARATOR_SUFFIXES;
            }
            else if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 4;
                frame->state = C_AST_MEMBER_DECLARATOR_AFTER_LEADING_ATTRIBUTES;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            else
            {
                frame->state = C_AST_MEMBER_DECLARATOR_AFTER_DECLARATOR;
                c_ast_push_declarator(builder, C_AST_DECLARATOR_NAMED);
                running = false;
            }
            break;
        }
        case C_AST_MEMBER_DECLARATOR_AFTER_LEADING_ATTRIBUTES:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
            {
                frame->state = C_AST_MEMBER_DECLARATOR_SUFFIXES;
            }
            else
            {
                frame->state = C_AST_MEMBER_DECLARATOR_AFTER_DECLARATOR;
                c_ast_push_declarator(builder, C_AST_DECLARATOR_NAMED);
                running = false;
            }
            break;
        }
        case C_AST_MEMBER_DECLARATOR_AFTER_DECLARATOR:
        {
            frame->a = builder->ret_name_token;
            frame->b |= 1;
            frame->state = C_AST_MEMBER_DECLARATOR_SUFFIXES;
            break;
        }
        case C_AST_MEMBER_DECLARATOR_SUFFIXES:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON) && !(frame->b & 2))
            {
                if (frame->a == C_AST_NODE_INVALID)
                {
                    frame->a = builder->position;
                }
                c_ast_advance(builder);
                frame->b |= 2;
                frame->state = C_AST_MEMBER_DECLARATOR_AFTER_WIDTH;
                c_ast_push_expr(builder, C_AST_PREC_CONDITIONAL, 0);
                running = false;
            }
            else if (c_ast_is_attribute_start(builder, token) && !(frame->b & 4))
            {
                frame->b |= 4;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            else
            {
                frame->state = C_AST_MEMBER_DECLARATOR_FINISH;
            }
            break;
        }
        case C_AST_MEMBER_DECLARATOR_AFTER_WIDTH:
        {
            frame->state = C_AST_MEMBER_DECLARATOR_SUFFIXES;
            break;
        }
        case C_AST_MEMBER_DECLARATOR_FINISH:
        {
            c_ast_append(builder, C_AST_MEMBER_DECLARATOR, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `enum`: attributes, a tag, a C23 fixed type, an enumerator list, trailing
// attributes.
BUSTER_GLOBAL_LOCAL void c_ast_step_enum(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ENUM_START:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_ENUM_AFTER_ATTRIBUTES;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 1;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_ENUM_AFTER_ATTRIBUTES:
        {
            CToken token = c_ast_peek(builder, 0);
            if (token.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, token)))
            {
                c_ast_append(builder, C_AST_TAG_NAME, builder->node_count, builder->position, token.symbol);
                c_ast_advance(builder);
                frame->b |= 2;
                token = c_ast_peek(builder, 0);
            }
            frame->state = C_AST_ENUM_AFTER_FIXED_TYPE;
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON) && c_ast_is_type_start(builder, c_ast_peek(builder, 1)))
            {
                c_ast_advance(builder);
                frame->b |= 4;
                c_ast_push_type_name(builder, C_AST_TYPE_NAME_NO_DECLARATOR);
                running = false;
            }
            break;
        }
        case C_AST_ENUM_AFTER_FIXED_TYPE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_BRACE))
            {
                frame->b |= 8;
                frame->state = C_AST_ENUM_AFTER_BODY;
                c_ast_push(builder, C_AST_FRAME_ENUMERATOR_LIST, C_AST_ENUMERATOR_LIST_OPEN, 0, builder->node_count);
                running = false;
            }
            else if (frame->b & 2)
            {
                frame->state = C_AST_ENUM_FINISH;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a tag name or '{'"));
            }
            break;
        }
        case C_AST_ENUM_AFTER_BODY:
        {
            frame->state = C_AST_ENUM_FINISH;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 16;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_ENUM_FINISH:
        {
            c_ast_append(builder, C_AST_ENUM_SPECIFIER, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `{ enumerator, enumerator, }`.
BUSTER_GLOBAL_LOCAL void c_ast_step_enumerator_list(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ENUMERATOR_LIST_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_ENUMERATOR_LIST_ITEM;
            break;
        }
        case C_AST_ENUMERATOR_LIST_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_ENUMERATOR_LIST, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else if (token.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, token)))
            {
                frame->b += 1;
                frame->state = C_AST_ENUMERATOR_LIST_AFTER_ITEM;
                c_ast_push(builder, C_AST_FRAME_ENUMERATOR, C_AST_ENUMERATOR_START, 0, builder->node_count);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an enumerator name"));
            }
            break;
        }
        case C_AST_ENUMERATOR_LIST_AFTER_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_ENUMERATOR_LIST_ITEM;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                frame->state = C_AST_ENUMERATOR_LIST_ITEM;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or '}' in the enumerator list"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `name attributes? (= value)?`; the name is declared after its value.
BUSTER_GLOBAL_LOCAL void c_ast_step_enumerator(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ENUMERATOR_START:
        {
            CToken token = c_ast_peek(builder, 0);
            frame->a = builder->position;
            frame->c = c_ast_symbol_of(builder, token);
            c_ast_advance(builder);
            frame->state = C_AST_ENUMERATOR_AFTER_ATTRIBUTES;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 1;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_ENUMERATOR_AFTER_ATTRIBUTES:
        {
            frame->state = C_AST_ENUMERATOR_AFTER_VALUE;
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_ASSIGN))
            {
                c_ast_advance(builder);
                frame->b |= 2;
                c_ast_push_expr(builder, C_AST_PREC_CONDITIONAL, 0);
                running = false;
            }
            break;
        }
        case C_AST_ENUMERATOR_AFTER_VALUE:
        {
            c_ast_bind(builder, frame->c, C_AST_INFO_ORDINARY);
            c_ast_append(builder, C_AST_ENUMERATOR, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- attributes -----------------------------------------------------------

// A maximal run of attribute groups: one ATTRIBUTE_LIST.
BUSTER_GLOBAL_LOCAL void c_ast_step_attr_list(CAstBuilder* builder, CAstFrame* frame)
{
    CToken token = c_ast_peek(builder, 0);
    if (c_ast_is_attribute_start(builder, token))
    {
        if (frame->a == 0)
        {
            frame->b = builder->position;
        }
        frame->a += 1;
        c_ast_push(builder, C_AST_FRAME_ATTR_GROUP, C_AST_ATTRIBUTE_GROUP_OPEN, 0, builder->node_count);
    }
    else
    {
        c_ast_append(builder, C_AST_ATTRIBUTE_LIST, frame->begin, frame->b, frame->a);
        c_ast_pop(builder);
    }
}

// Any identifier-kind token names an attribute, keywords included.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_is_attribute_name(CToken token)
{
    return token.kind == C_TOKEN_IDENTIFIER;
}

// One group: `__attribute__ (( items ))`, `[[ items ]]` or
// `__declspec ( items )`. An item is a name or `ns::name` with an optional
// parenthesized argument list of expressions and type-names.
BUSTER_GLOBAL_LOCAL void c_ast_step_attr_group(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        bool item_done = false;
        switch (frame->state)
        {
        case C_AST_ATTRIBUTE_GROUP_OPEN:
        {
            CToken token = c_ast_peek(builder, 0);
            frame->b = builder->position;
            c_ast_advance(builder);
            if (token.kind == C_TOKEN_PUNCTUATOR)
            {
                frame->a = C_AST_ATTRIBUTE_STANDARD;
                c_ast_advance(builder);
                frame->state = C_AST_ATTRIBUTE_GROUP_ITEM;
            }
            else if (c_ast_info(builder, token) == C_AST_WORD_ATTRIBUTE)
            {
                frame->a = C_AST_ATTRIBUTE_GNU;
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                    c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    c_ast_advance(builder);
                    c_ast_advance(builder);
                    frame->state = C_AST_ATTRIBUTE_GROUP_ITEM;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'((' after '__attribute__'"));
                }
            }
            else
            {
                frame->a = C_AST_ATTRIBUTE_DECLSPEC;
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    c_ast_advance(builder);
                    frame->state = C_AST_ATTRIBUTE_GROUP_ITEM;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after '__declspec'"));
                }
            }
            break;
        }
        case C_AST_ATTRIBUTE_GROUP_ITEM:
        {
            CToken token = c_ast_peek(builder, 0);
            bool standard = frame->a == C_AST_ATTRIBUTE_STANDARD;
            CPunctuator closer = standard ? C_PUNCTUATOR_RIGHT_BRACKET : C_PUNCTUATOR_RIGHT_PARENTHESIS;
            if (c_ast_is_punctuator(token, closer))
            {
                c_ast_advance(builder);
                if (frame->a == C_AST_ATTRIBUTE_DECLSPEC || c_ast_is_punctuator(c_ast_peek(builder, 0), closer))
                {
                    if (frame->a != C_AST_ATTRIBUTE_DECLSPEC)
                    {
                        c_ast_advance(builder);
                    }
                    c_ast_append(builder, (CAstKind)frame->a, frame->begin, frame->b, frame->c);
                    c_ast_pop(builder);
                    running = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, standard ? S8("']]' to close the attribute") : S8("'))' to close the attribute"));
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
            }
            else if (c_ast_is_attribute_name(token))
            {
                frame->e = builder->node_count;
                frame->d = builder->position;
                frame->f = 0;
                frame->flags = (u16)(frame->flags & ~C_AST_ATTRIBUTE_GROUP_SCOPED);
                c_ast_advance(builder);
                bool valid = true;
                if (standard && c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON) &&
                    c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_COLON))
                {
                    c_ast_append(builder, C_AST_ATTRIBUTE_NAMESPACE, builder->node_count, frame->d, 0);
                    c_ast_advance(builder);
                    c_ast_advance(builder);
                    CToken name = c_ast_peek(builder, 0);
                    if (c_ast_is_attribute_name(name))
                    {
                        frame->d = builder->position;
                        frame->flags |= C_AST_ATTRIBUTE_GROUP_SCOPED;
                        c_ast_advance(builder);
                    }
                    else
                    {
                        valid = false;
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an attribute name after '::'"));
                    }
                }
                if (valid)
                {
                    if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
                    {
                        c_ast_advance(builder);
                        frame->state = C_AST_ATTRIBUTE_GROUP_ARGUMENT;
                    }
                    else
                    {
                        item_done = true;
                    }
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an attribute"));
            }
            break;
        }
        case C_AST_ATTRIBUTE_GROUP_ARGUMENT:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                item_done = true;
            }
            else
            {
                bool type_argument = c_ast_type_name_at(builder, 0);
                frame->f += 1;
                frame->state = C_AST_ATTRIBUTE_GROUP_AFTER_ARGUMENT;
                if (type_argument)
                {
                    c_ast_push_type_name(builder, 0);
                }
                else
                {
                    c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                }
                running = false;
            }
            break;
        }
        case C_AST_ATTRIBUTE_GROUP_AFTER_ARGUMENT:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                frame->state = C_AST_ATTRIBUTE_GROUP_ARGUMENT;
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                item_done = true;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("',' or ')' in the attribute arguments"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
        if (item_done)
        {
            c_ast_append(builder, (frame->flags & C_AST_ATTRIBUTE_GROUP_SCOPED) ? C_AST_ATTRIBUTE_SCOPED : C_AST_ATTRIBUTE, frame->e, frame->d,
                         frame->f);
            frame->c += 1;
            frame->state = C_AST_ATTRIBUTE_GROUP_ITEM;
        }
    }
}

// ---- static assertions and assembly ---------------------------------------

// `_Static_assert ( condition (, message)? ) ;`.
BUSTER_GLOBAL_LOCAL void c_ast_step_static_assert(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_STATIC_ASSERT_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_STATIC_ASSERT_AFTER_CONDITION;
                c_ast_push_expr(builder, C_AST_PREC_ASSIGN, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after '_Static_assert'"));
            }
            break;
        }
        case C_AST_STATIC_ASSERT_AFTER_CONDITION:
        {
            bool valid = true;
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COMMA))
            {
                c_ast_advance(builder);
                if (c_ast_peek(builder, 0).kind == C_TOKEN_STRING_LITERAL)
                {
                    c_ast_append_string_run(builder);
                    frame->b |= 1;
                }
                else
                {
                    valid = false;
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a string literal message"));
                }
            }
            if (valid)
            {
                if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the static assertion"));
                }
                else
                {
                    c_ast_advance(builder);
                    if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the static assertion"));
                    }
                    else
                    {
                        c_ast_advance(builder);
                        c_ast_append(builder, C_AST_STATIC_ASSERT, frame->begin, frame->a, 0);
                        c_ast_pop(builder);
                        running = false;
                    }
                }
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// A file-scope `asm ( "text" ) ;`; the cursor is on the keyword.
BUSTER_GLOBAL_LOCAL void c_ast_parse_asm_top_level(CAstBuilder* builder)
{
    u32 begin = builder->node_count;
    u32 keyword = builder->position;
    c_ast_advance(builder);
    if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after 'asm'"));
    }
    else
    {
        c_ast_advance(builder);
        if (c_ast_peek(builder, 0).kind != C_TOKEN_STRING_LITERAL)
        {
            c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a string literal"));
        }
        else
        {
            c_ast_append_string_run(builder);
            if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the assembler text"));
            }
            else
            {
                c_ast_advance(builder);
                if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the assembler text"));
                }
                else
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, C_AST_ASM_TOP_LEVEL, begin, keyword, 0);
                }
            }
        }
    }
}

// An asm statement: qualifiers, the template, and up to four `:` sections.
BUSTER_GLOBAL_LOCAL void c_ast_step_asm(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ASM_STATE_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            bool qualifying = true;
            while (qualifying)
            {
                u32 info = c_ast_info(builder, c_ast_peek(builder, 0));
                if (info == C_AST_WORD_VOLATILE)
                {
                    frame->flags |= C_AST_ASM_VOLATILE;
                    c_ast_advance(builder);
                }
                else if (info == C_AST_WORD_INLINE)
                {
                    frame->flags |= C_AST_ASM_INLINE;
                    c_ast_advance(builder);
                }
                else if (info == C_AST_WORD_GOTO)
                {
                    frame->flags |= C_AST_ASM_GOTO;
                    c_ast_advance(builder);
                }
                else
                {
                    qualifying = false;
                }
            }
            if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after 'asm'"));
            }
            else
            {
                c_ast_advance(builder);
                if (c_ast_peek(builder, 0).kind != C_TOKEN_STRING_LITERAL)
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("the assembler template string"));
                }
                else
                {
                    c_ast_append_string_run(builder);
                    frame->state = C_AST_ASM_STATE_NEXT;
                }
            }
            break;
        }
        case C_AST_ASM_STATE_NEXT:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, C_AST_ASM, frame->begin, frame->a, frame->flags & C_AST_ASM_QUALIFIERS);
                    c_ast_pop(builder);
                    running = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the asm statement"));
                }
            }
            else if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON))
            {
                u32 written = (frame->flags & C_AST_ASM_SECTIONS_MASK) >> C_AST_ASM_SECTIONS_SHIFT;
                if (written >= 4)
                {
                    c_ast_fail(builder, builder->position, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an asm statement has at most four sections"));
                }
                else
                {
                    frame->d = builder->position;
                    frame->c = builder->node_count;
                    frame->b = 0;
                    c_ast_advance(builder);
                    frame->flags = (u16)((frame->flags & ~(u32)C_AST_ASM_SECTIONS_MASK) | ((written + 1) << C_AST_ASM_SECTIONS_SHIFT));
                    frame->state = written < 2 ? C_AST_ASM_STATE_OPERANDS : written == 2 ? C_AST_ASM_STATE_CLOBBERS : C_AST_ASM_STATE_LABELS;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("':' or ')' in the asm statement"));
            }
            break;
        }
        case C_AST_ASM_STATE_OPERANDS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON) || c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                u32 written = (frame->flags & C_AST_ASM_SECTIONS_MASK) >> C_AST_ASM_SECTIONS_SHIFT;
                c_ast_append(builder, written == 1 ? C_AST_ASM_OUTPUTS : C_AST_ASM_INPUTS, frame->c, frame->d, frame->b);
                frame->state = C_AST_ASM_STATE_NEXT;
            }
            else
            {
                bool valid = true;
                frame->e = builder->node_count;
                frame->flags = (u16)(frame->flags & ~C_AST_ASM_NAMED_OPERAND);
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET))
                {
                    c_ast_advance(builder);
                    CToken name = c_ast_peek(builder, 0);
                    if (name.kind == C_TOKEN_IDENTIFIER)
                    {
                        c_ast_append(builder, C_AST_ASM_SYMBOLIC_NAME, builder->node_count, builder->position, 0);
                        c_ast_advance(builder);
                        if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACKET))
                        {
                            c_ast_advance(builder);
                            frame->flags |= C_AST_ASM_NAMED_OPERAND;
                        }
                        else
                        {
                            valid = false;
                            c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("']' after the operand name"));
                        }
                    }
                    else
                    {
                        valid = false;
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an operand name"));
                    }
                }
                if (valid)
                {
                    if (c_ast_peek(builder, 0).kind != C_TOKEN_STRING_LITERAL)
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an operand constraint string"));
                    }
                    else
                    {
                        frame->f = builder->position;
                        c_ast_append_string_run(builder);
                        if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
                        {
                            c_ast_advance(builder);
                            frame->state = C_AST_ASM_STATE_AFTER_OPERAND;
                            c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                            running = false;
                        }
                        else
                        {
                            c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after the operand constraint"));
                        }
                    }
                }
            }
            break;
        }
        case C_AST_ASM_STATE_AFTER_OPERAND:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_append(builder, C_AST_ASM_OPERAND, frame->e, frame->f, frame->flags & C_AST_ASM_NAMED_OPERAND);
                frame->b += 1;
                CToken next = c_ast_peek(builder, 0);
                if (c_ast_is_punctuator(next, C_PUNCTUATOR_COMMA))
                {
                    c_ast_advance(builder);
                    CToken after = c_ast_peek(builder, 0);
                    if (c_ast_is_punctuator(after, C_PUNCTUATOR_COLON) || c_ast_is_punctuator(after, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("an asm operand after ','"));
                    }
                }
                else if (!c_ast_is_punctuator(next, C_PUNCTUATOR_COLON) && !c_ast_is_punctuator(next, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("',', ':' or ')' after an asm operand"));
                }
                frame->state = C_AST_ASM_STATE_OPERANDS;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the operand expression"));
            }
            break;
        }
        case C_AST_ASM_STATE_CLOBBERS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON) || c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_append(builder, C_AST_ASM_CLOBBERS, frame->c, frame->d, frame->b);
                frame->state = C_AST_ASM_STATE_NEXT;
            }
            else if (token.kind == C_TOKEN_STRING_LITERAL)
            {
                c_ast_append_string_run(builder);
                frame->b += 1;
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COMMA))
                {
                    c_ast_advance(builder);
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a clobber string"));
            }
            break;
        }
        case C_AST_ASM_STATE_LABELS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_COLON) || c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_append(builder, C_AST_ASM_LABELS, frame->c, frame->d, frame->b);
                frame->state = C_AST_ASM_STATE_NEXT;
            }
            else if (token.kind == C_TOKEN_IDENTIFIER)
            {
                c_ast_append(builder, C_AST_IDENTIFIER, builder->node_count, builder->position, token.symbol);
                c_ast_advance(builder);
                frame->b += 1;
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COMMA))
                {
                    c_ast_advance(builder);
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a label name"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// ---- statements -----------------------------------------------------------

enum
{
    C_AST_IF_OPEN,
    C_AST_IF_AFTER_CONDITION,
    C_AST_IF_AFTER_THEN,
    C_AST_IF_AFTER_ELSE,
};

enum
{
    C_AST_WHILE_OPEN,
    C_AST_WHILE_AFTER_CONDITION,
    C_AST_WHILE_AFTER_BODY,
    // F_WHILE flag: `switch` rather than `while`.
    C_AST_WHILE_SWITCH = 1,
};

enum
{
    C_AST_DO_OPEN,
    C_AST_DO_AFTER_BODY,
    C_AST_DO_AFTER_CONDITION,
};

enum
{
    C_AST_FOR_OPEN,
    C_AST_FOR_AFTER_INIT_DECLARATION,
    C_AST_FOR_AFTER_INIT_EXPRESSION,
    C_AST_FOR_CONDITION,
    C_AST_FOR_AFTER_CONDITION,
    C_AST_FOR_STEP,
    C_AST_FOR_AFTER_STEP,
    C_AST_FOR_END,
};

enum
{
    C_AST_LABELED_OPEN,
    C_AST_LABELED_AFTER_ATTRIBUTES,
    C_AST_LABELED_END,
};

enum
{
    C_AST_CASE_OPEN,
    C_AST_CASE_AFTER_VALUE,
    C_AST_CASE_AFTER_HIGH,
    C_AST_CASE_END,
    // F_CASE flag: `case low ... high`.
    C_AST_CASE_FLAG_RANGE = 1,
};

enum
{
    C_AST_DEFAULT_OPEN,
    C_AST_DEFAULT_END,
};

enum
{
    C_AST_ATTR_STATEMENT_OPEN,
    C_AST_ATTR_STATEMENT_AFTER_ATTRIBUTES,
    C_AST_ATTR_STATEMENT_END,
    // F_ATTR_STATEMENT flag: a declaration may follow the attributes.
    C_AST_ATTR_STATEMENT_DECLARATION = 1,
};

enum
{
    C_AST_SIMPLE_AFTER_EXPRESSION,
    // F_SIMPLE_STATEMENT flag: the node's token is the `;` rather than the
    // keyword in register b.
    C_AST_SIMPLE_SEMICOLON_TOKEN = 1,
};

enum
{
    C_AST_TRANSLATION_UNIT_ITEMS,
};

BUSTER_GLOBAL_LOCAL void c_ast_start_statement(CAstBuilder* builder, bool allow_declaration);

// Starts a statement and reports whether it left a frame for the caller to
// wait on; a statement that finished in place (`break;`, `;`) did not.
BUSTER_GLOBAL_LOCAL bool c_ast_start_child_statement(CAstBuilder* builder, bool allow_declaration)
{
    u32 depth = builder->frame_count;
    c_ast_start_statement(builder, allow_declaration);
    return builder->frame_count != depth;
}

// `statement ;` with a leading keyword already consumed: the frame sits
// under an expression frame and appends `kind` once the `;` is read.
BUSTER_GLOBAL_LOCAL void c_ast_push_simple_statement(CAstBuilder* builder, CAstKind kind, u32 token, u32 flags)
{
    CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_SIMPLE_STATEMENT, C_AST_SIMPLE_AFTER_EXPRESSION, flags, builder->node_count);
    frame->a = (u32)kind;
    frame->b = token;
}

BUSTER_GLOBAL_LOCAL void c_ast_push_expression_statement(CAstBuilder* builder, u32 extension_run)
{
    c_ast_push_simple_statement(builder, C_AST_EXPRESSION_STATEMENT, 0, C_AST_SIMPLE_SEMICOLON_TOKEN);
    c_ast_push_expr(builder, C_AST_PREC_COMMA, extension_run);
}

BUSTER_GLOBAL_LOCAL void c_ast_push_statement_declaration(CAstBuilder* builder, u32 extension_run, u32 first_token)
{
    c_ast_push_declaration(builder, builder->gnu ? C_AST_DECL_ALLOW_DEFINITION : 0, builder->node_count, 0, extension_run, first_token);
}

// One statement or, where a declaration is allowed, one declaration. Either
// finishes in place or pushes the frame that will append its single node.
BUSTER_GLOBAL_LOCAL void c_ast_start_statement(CAstBuilder* builder, bool allow_declaration)
{
    CToken token = c_ast_peek(builder, 0);
    u32 position = builder->position;
    if (token.kind == C_TOKEN_IDENTIFIER)
    {
        u32 info = c_ast_info(builder, token);
        if (c_ast_is_name_info(info))
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_COLON))
            {
                c_ast_push(builder, C_AST_FRAME_LABELED, C_AST_LABELED_OPEN, 0, builder->node_count);
            }
            else if (info == C_AST_INFO_TYPEDEF && allow_declaration)
            {
                c_ast_push_statement_declaration(builder, 0, position);
            }
            else
            {
                c_ast_push_expression_statement(builder, 0);
            }
        }
        else
        {
            switch ((CAstWord)info)
            {
            case C_AST_WORD_IF:
            {
                c_ast_push(builder, C_AST_FRAME_IF, C_AST_IF_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_SWITCH:
            case C_AST_WORD_WHILE:
            {
                c_ast_push(builder, C_AST_FRAME_WHILE, C_AST_WHILE_OPEN, info == C_AST_WORD_SWITCH ? C_AST_WHILE_SWITCH : 0, builder->node_count);
                break;
            }
            case C_AST_WORD_DO:
            {
                c_ast_push(builder, C_AST_FRAME_DO, C_AST_DO_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_FOR:
            {
                c_ast_push(builder, C_AST_FRAME_FOR, C_AST_FOR_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_CASE:
            {
                c_ast_push(builder, C_AST_FRAME_CASE, C_AST_CASE_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_DEFAULT:
            {
                c_ast_push(builder, C_AST_FRAME_DEFAULT, C_AST_DEFAULT_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_BREAK:
            case C_AST_WORD_CONTINUE:
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, info == C_AST_WORD_BREAK ? C_AST_BREAK : C_AST_CONTINUE, builder->node_count, position, 0);
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the jump statement"));
                }
                break;
            }
            case C_AST_WORD_RETURN:
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, C_AST_RETURN, builder->node_count, position, 0);
                }
                else
                {
                    c_ast_push_simple_statement(builder, C_AST_RETURN, position, 0);
                    c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                }
                break;
            }
            case C_AST_WORD_GOTO:
            {
                c_ast_advance(builder);
                CToken target = c_ast_peek(builder, 0);
                if (c_ast_is_punctuator(target, C_PUNCTUATOR_STAR))
                {
                    c_ast_advance(builder);
                    c_ast_push_simple_statement(builder, C_AST_GOTO_COMPUTED, position, 0);
                    c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                }
                else if (target.kind == C_TOKEN_IDENTIFIER && c_ast_is_name_info(c_ast_info(builder, target)))
                {
                    c_ast_append(builder, C_AST_GOTO, builder->node_count, builder->position, target.symbol);
                    c_ast_advance(builder);
                    if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                    {
                        c_ast_advance(builder);
                    }
                    else
                    {
                        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the goto statement"));
                    }
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("a label name or '*' after 'goto'"));
                }
                break;
            }
            case C_AST_WORD_ASM:
            {
                c_ast_push(builder, C_AST_FRAME_ASM, C_AST_ASM_STATE_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_STATIC_ASSERT:
            {
                c_ast_push(builder, C_AST_FRAME_STATIC_ASSERT, C_AST_STATIC_ASSERT_OPEN, 0, builder->node_count);
                break;
            }
            case C_AST_WORD_EXTENSION:
            {
                // `__extension__` opens either a declaration or an
                // expression; the decision needs the token after the run, so
                // the run is consumed first and replayed into the choice.
                u32 run = 0;
                while (c_ast_is_word(builder, c_ast_peek(builder, 0), C_AST_WORD_EXTENSION))
                {
                    c_ast_advance(builder);
                    run += 1;
                }
                CToken after = c_ast_peek(builder, 0);
                if (allow_declaration && (c_ast_is_declaration_start(builder, after) || c_ast_is_attribute_start(builder, after)))
                {
                    c_ast_push_statement_declaration(builder, run, position);
                }
                else
                {
                    c_ast_push_expression_statement(builder, run);
                }
                break;
            }
            case C_AST_WORD_ATTRIBUTE:
            case C_AST_WORD_DECLSPEC:
            {
                CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_ATTR_STATEMENT, C_AST_ATTR_STATEMENT_OPEN,
                                              allow_declaration ? C_AST_ATTR_STATEMENT_DECLARATION : 0, builder->node_count);
                frame->a = position;
                break;
            }
            default:
            {
                if (allow_declaration && (c_ast_info_flags[info] & C_AST_FLAG_DECL))
                {
                    c_ast_push_statement_declaration(builder, 0, position);
                }
                else
                {
                    c_ast_push_expression_statement(builder, 0);
                }
                break;
            }
            }
        }
    }
    else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACE))
    {
        c_ast_push_block(builder, 0);
    }
    else if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
    {
        c_ast_append(builder, C_AST_NULL_STATEMENT, builder->node_count, position, 0);
        c_ast_advance(builder);
    }
    else if (c_ast_is_punctuator(token, C_PUNCTUATOR_LEFT_BRACKET) && c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_BRACKET))
    {
        CAstFrame* frame = c_ast_push(builder, C_AST_FRAME_ATTR_STATEMENT, C_AST_ATTR_STATEMENT_OPEN,
                                      allow_declaration ? C_AST_ATTR_STATEMENT_DECLARATION : 0, builder->node_count);
        frame->a = position;
    }
    else if (token.kind == C_TOKEN_PRAGMA)
    {
        c_ast_append(builder, C_AST_PRAGMA, builder->node_count, position, 0);
        c_ast_advance(builder);
    }
    else
    {
        c_ast_push_expression_statement(builder, 0);
    }
}

// `{ block-items }` in its own scope (the scope of a function body belongs to
// its declaration frame).
BUSTER_GLOBAL_LOCAL void c_ast_step_block(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_BLOCK_OPEN:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_BRACE))
            {
                frame->a = builder->position;
                c_ast_advance(builder);
                if (!(frame->flags & C_AST_BLOCK_NO_SCOPE))
                {
                    frame->c = c_ast_scope_push(builder);
                }
                frame->state = C_AST_BLOCK_ITEMS;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'{'"));
            }
            break;
        }
        case C_AST_BLOCK_ITEMS:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                c_ast_advance(builder);
                if (!(frame->flags & C_AST_BLOCK_NO_SCOPE))
                {
                    c_ast_scope_pop(builder, frame->c);
                }
                c_ast_append(builder, C_AST_COMPOUND_STATEMENT, frame->begin, frame->a, frame->b);
                c_ast_pop(builder);
                running = false;
            }
            else if (token.kind == C_TOKEN_END_OF_FILE)
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("'}' to close the block"));
            }
            else
            {
                frame->b += 1;
                if (c_ast_start_child_statement(builder, true))
                {
                    running = false;
                }
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `if ( condition ) statement (else statement)?`.
BUSTER_GLOBAL_LOCAL void c_ast_step_if(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_IF_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_IF_AFTER_CONDITION;
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after 'if'"));
            }
            break;
        }
        case C_AST_IF_AFTER_CONDITION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_IF_AFTER_THEN;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the condition"));
            }
            break;
        }
        case C_AST_IF_AFTER_THEN:
        {
            if (c_ast_is_word(builder, c_ast_peek(builder, 0), C_AST_WORD_ELSE))
            {
                c_ast_advance(builder);
                frame->state = C_AST_IF_AFTER_ELSE;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_append(builder, C_AST_IF, frame->begin, frame->a, 0);
                c_ast_pop(builder);
                running = false;
            }
            break;
        }
        case C_AST_IF_AFTER_ELSE:
        {
            c_ast_append(builder, C_AST_IF_ELSE, frame->begin, frame->a, 0);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `while ( condition ) statement` and `switch ( condition ) statement`.
BUSTER_GLOBAL_LOCAL void c_ast_step_while(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_WHILE_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_WHILE_AFTER_CONDITION;
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after the keyword"));
            }
            break;
        }
        case C_AST_WHILE_AFTER_CONDITION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_WHILE_AFTER_BODY;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the condition"));
            }
            break;
        }
        case C_AST_WHILE_AFTER_BODY:
        {
            c_ast_append(builder, (frame->flags & C_AST_WHILE_SWITCH) ? C_AST_SWITCH : C_AST_WHILE, frame->begin, frame->a, 0);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `do statement while ( condition ) ;`.
BUSTER_GLOBAL_LOCAL void c_ast_step_do(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_DO_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_DO_AFTER_BODY;
            if (c_ast_start_child_statement(builder, false))
            {
                running = false;
            }
            break;
        }
        case C_AST_DO_AFTER_BODY:
        {
            if (c_ast_is_word(builder, c_ast_peek(builder, 0), C_AST_WORD_WHILE) &&
                c_ast_is_punctuator(c_ast_peek(builder, 1), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                c_ast_advance(builder);
                frame->state = C_AST_DO_AFTER_CONDITION;
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'while (' after the do body"));
            }
            break;
        }
        case C_AST_DO_AFTER_CONDITION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    c_ast_append(builder, C_AST_DO_WHILE, frame->begin, frame->a, 0);
                    c_ast_pop(builder);
                    running = false;
                }
                else
                {
                    c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the do-while statement"));
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the condition"));
            }
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `for ( init ; condition ; step ) statement`, in its own scope. The init is
// a declaration or an expression.
BUSTER_GLOBAL_LOCAL void c_ast_step_for(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_FOR_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->c = c_ast_scope_push(builder);
                CToken token = c_ast_peek(builder, 0);
                if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
                {
                    c_ast_advance(builder);
                    frame->state = C_AST_FOR_CONDITION;
                }
                else
                {
                    u32 run = 0;
                    while (c_ast_is_word(builder, c_ast_peek(builder, 0), C_AST_WORD_EXTENSION))
                    {
                        c_ast_advance(builder);
                        run += 1;
                    }
                    CToken after = c_ast_peek(builder, 0);
                    frame->b |= 1;
                    if (c_ast_is_declaration_start(builder, after) || c_ast_is_attribute_start(builder, after))
                    {
                        frame->state = C_AST_FOR_AFTER_INIT_DECLARATION;
                        c_ast_push_declaration(builder, 0, builder->node_count, 0, run, builder->position - run);
                    }
                    else
                    {
                        frame->state = C_AST_FOR_AFTER_INIT_EXPRESSION;
                        c_ast_push_expr(builder, C_AST_PREC_COMMA, run);
                    }
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("'(' after 'for'"));
            }
            break;
        }
        case C_AST_FOR_AFTER_INIT_DECLARATION:
        {
            frame->state = C_AST_FOR_CONDITION;
            break;
        }
        case C_AST_FOR_AFTER_INIT_EXPRESSION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_FOR_CONDITION;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the for initializer"));
            }
            break;
        }
        case C_AST_FOR_CONDITION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_FOR_STEP;
            }
            else
            {
                frame->b |= 2;
                frame->state = C_AST_FOR_AFTER_CONDITION;
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
            }
            break;
        }
        case C_AST_FOR_AFTER_CONDITION:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_FOR_STEP;
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the for condition"));
            }
            break;
        }
        case C_AST_FOR_STEP:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_FOR_END;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            else
            {
                frame->b |= 4;
                frame->state = C_AST_FOR_AFTER_STEP;
                c_ast_push_expr(builder, C_AST_PREC_COMMA, 0);
                running = false;
            }
            break;
        }
        case C_AST_FOR_AFTER_STEP:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                c_ast_advance(builder);
                frame->state = C_AST_FOR_END;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_UNMATCHED_DELIMITER, S8("')' after the for clauses"));
            }
            break;
        }
        case C_AST_FOR_END:
        {
            c_ast_scope_pop(builder, frame->c);
            c_ast_append(builder, C_AST_FOR, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `label : attributes? statement?`. The statement is optional only before
// the closing brace (C23).
BUSTER_GLOBAL_LOCAL void c_ast_step_labeled(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_LABELED_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            c_ast_advance(builder);
            frame->state = C_AST_LABELED_AFTER_ATTRIBUTES;
            if (c_ast_is_attribute_start(builder, c_ast_peek(builder, 0)))
            {
                frame->b |= 1;
                c_ast_push_attribute_list(builder);
                running = false;
            }
            break;
        }
        case C_AST_LABELED_AFTER_ATTRIBUTES:
        {
            frame->state = C_AST_LABELED_END;
            if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACE))
            {
                frame->b |= 2;
                if (c_ast_start_child_statement(builder, true))
                {
                    running = false;
                }
            }
            break;
        }
        case C_AST_LABELED_END:
        {
            c_ast_append(builder, C_AST_LABELED, frame->begin, frame->a, frame->b);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `case value :` and GNU `case low ... high :`, then an optional statement.
BUSTER_GLOBAL_LOCAL void c_ast_step_case(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_CASE_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            frame->state = C_AST_CASE_AFTER_VALUE;
            c_ast_push_expr(builder, C_AST_PREC_CONDITIONAL, 0);
            running = false;
            break;
        }
        case C_AST_CASE_AFTER_VALUE:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_ELLIPSIS))
            {
                c_ast_advance(builder);
                frame->flags |= C_AST_CASE_FLAG_RANGE;
                frame->state = C_AST_CASE_AFTER_HIGH;
                c_ast_push_expr(builder, C_AST_PREC_CONDITIONAL, 0);
                running = false;
            }
            else
            {
                frame->state = C_AST_CASE_AFTER_HIGH;
            }
            break;
        }
        case C_AST_CASE_AFTER_HIGH:
        {
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_CASE_END;
                if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACE) && c_ast_start_child_statement(builder, true))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("':' after the case value"));
            }
            break;
        }
        case C_AST_CASE_END:
        {
            c_ast_append(builder, (frame->flags & C_AST_CASE_FLAG_RANGE) ? C_AST_CASE_RANGE : C_AST_CASE, frame->begin, frame->a, 0);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// `default :` and an optional statement.
BUSTER_GLOBAL_LOCAL void c_ast_step_default(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_DEFAULT_OPEN:
        {
            frame->a = builder->position;
            c_ast_advance(builder);
            if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_COLON))
            {
                c_ast_advance(builder);
                frame->state = C_AST_DEFAULT_END;
                if (!c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_RIGHT_BRACE) && c_ast_start_child_statement(builder, true))
                {
                    running = false;
                }
            }
            else
            {
                c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("':' after 'default'"));
            }
            break;
        }
        case C_AST_DEFAULT_END:
        {
            c_ast_append(builder, C_AST_DEFAULT, frame->begin, frame->a, 0);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// A statement that opens with an attribute run: `[[fallthrough]];`, an
// attributed statement, or a declaration whose specifiers open with it.
BUSTER_GLOBAL_LOCAL void c_ast_step_attr_statement(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        switch (frame->state)
        {
        case C_AST_ATTR_STATEMENT_OPEN:
        {
            frame->state = C_AST_ATTR_STATEMENT_AFTER_ATTRIBUTES;
            c_ast_push_attribute_list(builder);
            running = false;
            break;
        }
        case C_AST_ATTR_STATEMENT_AFTER_ATTRIBUTES:
        {
            CToken token = c_ast_peek(builder, 0);
            if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
            {
                c_ast_append(builder, C_AST_ATTRIBUTE_STATEMENT, frame->begin, builder->position, 0);
                c_ast_advance(builder);
                c_ast_pop(builder);
                running = false;
            }
            else if ((frame->flags & C_AST_ATTR_STATEMENT_DECLARATION) && c_ast_is_declaration_start(builder, token))
            {
                // The attribute list is the declaration's first specifier.
                u32 begin = frame->begin;
                u32 first = frame->a;
                c_ast_pop(builder);
                c_ast_push_declaration(builder, builder->gnu ? C_AST_DECL_ALLOW_DEFINITION : 0, begin, 1, 0, first);
                running = false;
            }
            else
            {
                frame->state = C_AST_ATTR_STATEMENT_END;
                if (c_ast_start_child_statement(builder, false))
                {
                    running = false;
                }
            }
            break;
        }
        case C_AST_ATTR_STATEMENT_END:
        {
            c_ast_append(builder, C_AST_ATTRIBUTED_STATEMENT, frame->begin, frame->a, 0);
            c_ast_pop(builder);
            running = false;
            break;
        }
        default:
        {
            BUSTER_TODO();
        }
        }
    }
}

// An expression (or keyword and expression) statement waiting for its `;`.
BUSTER_GLOBAL_LOCAL void c_ast_step_simple_statement(CAstBuilder* builder, CAstFrame* frame)
{
    if (c_ast_is_punctuator(c_ast_peek(builder, 0), C_PUNCTUATOR_SEMICOLON))
    {
        u32 token = (frame->flags & C_AST_SIMPLE_SEMICOLON_TOKEN) ? builder->position : frame->b;
        c_ast_advance(builder);
        c_ast_append(builder, (CAstKind)frame->a, frame->begin, token, 0);
        c_ast_pop(builder);
    }
    else
    {
        c_ast_fail_expected(builder, C_DIAGNOSTIC_EXPECTED_DECLARATION, S8("';' after the statement"));
    }
}

// ---- translation unit and dispatch ----------------------------------------

// The external declarations, up to the end-of-file token.
BUSTER_GLOBAL_LOCAL void c_ast_step_translation_unit(CAstBuilder* builder, CAstFrame* frame)
{
    bool running = true;
    while (running && !builder->failed)
    {
        CToken token = c_ast_peek(builder, 0);
        u32 position = builder->position;
        if (token.kind == C_TOKEN_END_OF_FILE || builder->position >= builder->token_count)
        {
            u32 eof_index = builder->token_count ? builder->token_count - 1 : 0;
            c_ast_append(builder, C_AST_TRANSLATION_UNIT, frame->begin, eof_index, frame->a);
            c_ast_pop(builder);
            running = false;
        }
        else if (c_ast_is_punctuator(token, C_PUNCTUATOR_SEMICOLON))
        {
            c_ast_append(builder, C_AST_EMPTY_DECLARATION, builder->node_count, position, 0);
            c_ast_advance(builder);
            frame->a += 1;
        }
        else if (token.kind == C_TOKEN_PRAGMA)
        {
            c_ast_append(builder, C_AST_PRAGMA, builder->node_count, position, 0);
            c_ast_advance(builder);
            frame->a += 1;
        }
        else
        {
            u32 info = c_ast_info(builder, token);
            frame->a += 1;
            if (info == C_AST_WORD_STATIC_ASSERT)
            {
                c_ast_push(builder, C_AST_FRAME_STATIC_ASSERT, C_AST_STATIC_ASSERT_OPEN, 0, builder->node_count);
                running = false;
            }
            else if (info == C_AST_WORD_ASM)
            {
                c_ast_parse_asm_top_level(builder);
            }
            else
            {
                c_ast_push_declaration(builder, C_AST_DECL_FILE_SCOPE | C_AST_DECL_ALLOW_DEFINITION, builder->node_count, 0, 0, position);
                running = false;
            }
        }
    }
}

// The dispatch loop: the top frame's step until the stack is empty or a step
// reports a syntax error. No step calls another; the C stack stays flat.
BUSTER_GLOBAL_LOCAL void c_ast_run(CAstBuilder* builder)
{
    c_ast_push(builder, C_AST_FRAME_TRANSLATION_UNIT, C_AST_TRANSLATION_UNIT_ITEMS, 0, 0);
    while (builder->frame_count && !builder->failed)
    {
        CAstFrame* frame = &builder->frames[builder->frame_count - 1];
        switch ((CAstFrameKind)frame->kind)
        {
        case C_AST_FRAME_TRANSLATION_UNIT:
            c_ast_step_translation_unit(builder, frame);
            break;
        case C_AST_FRAME_DECL:
            c_ast_step_decl(builder, frame);
            break;
        case C_AST_FRAME_SPECS:
            c_ast_step_specs(builder, frame);
            break;
        case C_AST_FRAME_KEYWORD_PAREN:
            c_ast_step_keyword_paren(builder, frame);
            break;
        case C_AST_FRAME_TYPE_NAME:
            c_ast_step_type_name(builder, frame);
            break;
        case C_AST_FRAME_DECLARATOR:
            c_ast_step_declarator(builder, frame);
            break;
        case C_AST_FRAME_ARRAY_SUFFIX:
            c_ast_step_array_suffix(builder, frame);
            break;
        case C_AST_FRAME_FUNCTION_SUFFIX:
            c_ast_step_function_suffix(builder, frame);
            break;
        case C_AST_FRAME_PARAM_LIST:
            c_ast_step_param_list(builder, frame);
            break;
        case C_AST_FRAME_PARAM:
            c_ast_step_param(builder, frame);
            break;
        case C_AST_FRAME_INIT_DECL:
            c_ast_step_init_decl(builder, frame);
            break;
        case C_AST_FRAME_MEMBER_LIST:
            c_ast_step_member_list(builder, frame);
            break;
        case C_AST_FRAME_MEMBER_DECL:
            c_ast_step_member_decl(builder, frame);
            break;
        case C_AST_FRAME_MEMBER_DECLARATOR:
            c_ast_step_member_declarator(builder, frame);
            break;
        case C_AST_FRAME_STRUCT:
            c_ast_step_struct(builder, frame);
            break;
        case C_AST_FRAME_ENUM:
            c_ast_step_enum(builder, frame);
            break;
        case C_AST_FRAME_ENUMERATOR_LIST:
            c_ast_step_enumerator_list(builder, frame);
            break;
        case C_AST_FRAME_ENUMERATOR:
            c_ast_step_enumerator(builder, frame);
            break;
        case C_AST_FRAME_ATTR_LIST:
            c_ast_step_attr_list(builder, frame);
            break;
        case C_AST_FRAME_ATTR_GROUP:
            c_ast_step_attr_group(builder, frame);
            break;
        case C_AST_FRAME_EXPR:
            c_ast_step_expr(builder, frame);
            break;
        case C_AST_FRAME_CALL:
            c_ast_step_call(builder, frame);
            break;
        case C_AST_FRAME_GENERIC:
            c_ast_step_generic(builder, frame);
            break;
        case C_AST_FRAME_INIT_LIST:
            c_ast_step_init_list(builder, frame);
            break;
        case C_AST_FRAME_DESIGNATION:
            c_ast_step_designation(builder, frame);
            break;
        case C_AST_FRAME_STATIC_ASSERT:
            c_ast_step_static_assert(builder, frame);
            break;
        case C_AST_FRAME_ASM:
            c_ast_step_asm(builder, frame);
            break;
        case C_AST_FRAME_BLOCK:
            c_ast_step_block(builder, frame);
            break;
        case C_AST_FRAME_IF:
            c_ast_step_if(builder, frame);
            break;
        case C_AST_FRAME_WHILE:
            c_ast_step_while(builder, frame);
            break;
        case C_AST_FRAME_DO:
            c_ast_step_do(builder, frame);
            break;
        case C_AST_FRAME_FOR:
            c_ast_step_for(builder, frame);
            break;
        case C_AST_FRAME_LABELED:
            c_ast_step_labeled(builder, frame);
            break;
        case C_AST_FRAME_CASE:
            c_ast_step_case(builder, frame);
            break;
        case C_AST_FRAME_DEFAULT:
            c_ast_step_default(builder, frame);
            break;
        case C_AST_FRAME_ATTR_STATEMENT:
            c_ast_step_attr_statement(builder, frame);
            break;
        case C_AST_FRAME_SIMPLE_STATEMENT:
            c_ast_step_simple_statement(builder, frame);
            break;
        case C_AST_FRAME_COUNT:
        default:
            BUSTER_TODO();
        }
    }
}

// ---- entry point: setup, seal, layouts ------------------------------------

BUSTER_GLOBAL_LOCAL u32 c_ast_power_of_two_at_least(u32 value)
{
    u32 result = 1;
    while (result < value)
    {
        result *= 2;
    }
    return result;
}

// Prepares the cursor, the symbol-indexed table and the dialect flags. A
// stream the pass cannot address marks the builder failed instead.
BUSTER_GLOBAL_LOCAL void c_ast_builder_init(CAstBuilder* builder, CPreprocessResult preprocess, CAstOptions options)
{
    builder->preprocess = preprocess;
    builder->dialect = preprocess.dialect;
    builder->gnu = c_preprocess_dialect_is_gnu(preprocess.dialect);
    builder->c23 = c_preprocess_dialect_is_c23(preprocess.dialect);
    builder->symbols = preprocess.symbols;
    builder->eof_token = (CToken){.kind = C_TOKEN_END_OF_FILE};
    builder->ret_name_token = C_AST_NODE_INVALID;
    if (preprocess.token_count >= C_AST_NODE_LIMIT)
    {
        c_ast_fail(builder, 0, C_DIAGNOSTIC_SOURCE_TOO_LARGE, S8("the token stream is too long for a syntax tree"));
    }
    else if (!preprocess.symbols)
    {
        c_ast_fail(builder, 0, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS, S8("the syntax tree builder needs the preprocessing symbol table"));
    }
    else
    {
        builder->token_count = (u32)preprocess.token_count;
        builder->source = preprocess.tokens;
        builder->refill_batch = options.refill_batch;
        if (options.refill_batch == 0)
        {
            builder->window = preprocess.tokens;
            builder->window_mask = UINT32_MAX;
            builder->filled = builder->token_count;
        }
        else
        {
            builder->ring_capacity = c_ast_power_of_two_at_least(options.refill_batch + C_AST_LOOKAHEAD);
            builder->ring = arena_allocate(builder->phase, CToken, builder->ring_capacity);
            builder->window = builder->ring;
            builder->window_mask = builder->ring_capacity - 1;
        }
        builder->info_count = preprocess.symbols->count + 64;
        builder->info = arena_allocate_zeroed(builder->phase, u8, builder->info_count);
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_word_spellings); index += 1)
        {
            CAstWordSpelling const* entry = &c_ast_word_spellings[index];
            bool enabled = entry->gate == C_AST_GATE_ALWAYS || (entry->gate == C_AST_GATE_GNU && builder->gnu) ||
                           (entry->gate == C_AST_GATE_C23 && builder->c23) ||
                           (entry->gate == C_AST_GATE_GNU_OR_C23 && (builder->gnu || builder->c23));
            if (enabled)
            {
                u32 symbol = c_symbol_find(preprocess.symbols, entry->spelling);
                if (symbol && symbol < builder->info_count)
                {
                    builder->info[symbol] = entry->word;
                }
            }
        }
    }
}

// Copies the chunked columns into exact-sized contiguous ones in the caller's
// arena.
BUSTER_GLOBAL_LOCAL void c_ast_seal(CAstBuilder* builder, Arena* arena, CAst* ast)
{
    u32 count = builder->node_count;
    ast->kinds = arena_allocate(arena, u8, count);
    ast->extents = arena_allocate(arena, u32, count);
    ast->tokens = arena_allocate(arena, u32, count);
    ast->data = arena_allocate(arena, u32, count);
    for (u32 chunk = 0; chunk < builder->chunk_count; chunk += 1)
    {
        u32 first = chunk << C_AST_CHUNK_SHIFT;
        u32 in_chunk = BUSTER_MIN(count - first, (u32)C_AST_CHUNK_NODES);
        memcpy(ast->kinds + first, builder->chunks[chunk].kinds, in_chunk);
        memcpy(ast->extents + first, builder->chunks[chunk].extents, (u64)in_chunk * sizeof(u32));
        memcpy(ast->tokens + first, builder->chunks[chunk].tokens, (u64)in_chunk * sizeof(u32));
        memcpy(ast->data + first, builder->chunks[chunk].data, (u64)in_chunk * sizeof(u32));
    }
    ast->node_count = count;
    ast->root = count ? count - 1 : C_AST_NODE_INVALID;
    ast->layout = C_AST_LAYOUT_IMPLICIT;
    builder->statistics.sealed_copy_bytes = (u64)count * C_AST_NODE_BYTES;
    builder->statistics.retained_bytes = (u64)count * C_AST_NODE_BYTES;
}

// Writes the children of `node` in source order into `roots[0..count)`; the
// walk is backward from the last child, one dependent load each.
BUSTER_GLOBAL_LOCAL void c_ast_fill_children(CAst const* ast, u32 node, u32* roots, u32 count)
{
    u32 child = node - 1;
    for (u32 index = count; index > 0; index -= 1)
    {
        roots[index - 1] = child;
        child -= ast->extents[child];
    }
}

BUSTER_GLOBAL_LOCAL u32 c_ast_count_children(CAst const* ast, u32 node)
{
    u32 count = 0;
    u32 begin = node + 1 - ast->extents[node];
    u32 cursor = node;
    while (cursor > begin)
    {
        u32 child = cursor - 1;
        u32 extent = ast->extents[child];
        if (extent == 0 || extent > cursor - begin)
        {
            break;
        }
        cursor = child + 1 - extent;
        count += 1;
    }
    return count;
}

// HYBRID gives every LIST node a child-root slice [count, roots...] and
// repoints its data at the slice; EXPLICIT gives every interior node one in
// `slices` and leaves data alone. The forward pass never needs either; this
// is the measurement variant.
BUSTER_GLOBAL_LOCAL void c_ast_finalize_layout(Arena* arena, CAst* ast, CAstLayout layout, CAstStatistics* statistics)
{
    u32 count = ast->node_count;
    u64 total = 0;
    if (layout == C_AST_LAYOUT_HYBRID)
    {
        for (u32 node = 0; node < count; node += 1)
        {
            if (c_ast_kind_contracts[ast->kinds[node]] == C_AST_CONTRACT_LIST)
            {
                total += 1 + c_ast_kind_arity_a[ast->kinds[node]] + (u64)ast->data[node] + c_ast_kind_arity_b[ast->kinds[node]];
            }
        }
        ast->children = arena_allocate(arena, u32, total);
        u32 cursor = 0;
        for (u32 node = 0; node < count; node += 1)
        {
            if (c_ast_kind_contracts[ast->kinds[node]] == C_AST_CONTRACT_LIST)
            {
                u32 children = c_ast_kind_arity_a[ast->kinds[node]] + ast->data[node] + c_ast_kind_arity_b[ast->kinds[node]];
                ast->children[cursor] = children;
                c_ast_fill_children(ast, node, ast->children + cursor + 1, children);
                ast->data[node] = cursor;
                cursor += 1 + children;
            }
        }
        ast->children_count = cursor;
        ast->layout = layout;
    }
    else if (layout == C_AST_LAYOUT_EXPLICIT)
    {
        ast->slices = arena_allocate(arena, u32, count);
        for (u32 node = 0; node < count; node += 1)
        {
            if (ast->extents[node] > 1)
            {
                u32 children = c_ast_count_children(ast, node);
                ast->slices[node] = children;
                total += 1 + (u64)children;
            }
            else
            {
                ast->slices[node] = C_AST_NODE_INVALID;
            }
        }
        ast->children = arena_allocate(arena, u32, total);
        u32 cursor = 0;
        for (u32 node = 0; node < count; node += 1)
        {
            if (ast->slices[node] != C_AST_NODE_INVALID)
            {
                u32 children = ast->slices[node];
                ast->children[cursor] = children;
                c_ast_fill_children(ast, node, ast->children + cursor + 1, children);
                ast->slices[node] = cursor;
                cursor += 1 + children;
            }
        }
        ast->children_count = cursor;
        ast->layout = layout;
        statistics->retained_bytes += (u64)count * sizeof(u32);
    }
    statistics->finalize_child_entries = total;
    statistics->retained_bytes += total * sizeof(u32);
}

CAstResult c_ast_build(Arena* arena, CPreprocessResult preprocess, CAstOptions options)
{
    CAstResult result = {0};
    result.ast.root = C_AST_NODE_INVALID;
    Arena* phase = options.phase_arena;
    bool phase_owned = !phase;
    if (phase_owned)
    {
        phase = c_frontend_arena_create((ArenaCreation){
            .reserved_size = C_PHASE_ARENA_RESERVED_SIZE,
            .flags = {.pool_reuse = 1},
        }, C_FRONTEND_RESERVATION_ANALYSIS);
    }
    if (!phase)
    {
        result.diagnostics = arena_allocate(arena, CDiagnostic, 1);
        result.diagnostics[0] = (CDiagnostic){
            .message = S8("could not reserve the syntax tree builder arena"),
            .kind = C_DIAGNOSTIC_SOURCE_TOO_LARGE,
            .severity = C_DIAGNOSTIC_ERROR,
        };
        result.diagnostic_count = 1;
    }
    else
    {
        u64 phase_start = phase->position;
        CAstBuilder builder = {0};
        builder.arena = arena;
        builder.phase = phase;
        c_ast_builder_init(&builder, preprocess, options);
        if (!builder.failed)
        {
            c_ast_run(&builder);
        }
        if (!builder.failed)
        {
            c_ast_seal(&builder, arena, &result.ast);
            if (options.layout != C_AST_LAYOUT_IMPLICIT)
            {
                c_ast_finalize_layout(arena, &result.ast, options.layout, &builder.statistics);
            }
            result.complete = true;
        }
        else
        {
            CDiagnostic diagnostic = {
                .message = builder.fail_message,
                .kind = builder.fail_kind,
                .severity = C_DIAGNOSTIC_ERROR,
            };
            if (preprocess.tokens && preprocess.token_count)
            {
                u64 index = BUSTER_MIN((u64)builder.fail_token, preprocess.token_count - 1);
                diagnostic.location = c_preprocess_token_location(&preprocess, preprocess.tokens[index]);
            }
            result.diagnostics = arena_allocate(arena, CDiagnostic, 1);
            result.diagnostics[0] = diagnostic;
            result.diagnostic_count = 1;
        }
        result.statistics = builder.statistics;
        result.statistics.node_count = result.ast.node_count;
        result.statistics.transient_high_water = phase->position - phase_start;
        if (phase_owned)
        {
            c_phase_arena_retire(phase);
        }
        else
        {
            arena_release_to_position(phase, phase_start);
        }
    }
    return result;
}

// ---- names, accessors and traversal ---------------------------------------

String8 c_ast_kind_name(CAstKind kind)
{
    return (u32)kind < C_AST_KIND_COUNT ? c_ast_kind_names[kind] : S8("invalid");
}

String8 c_ast_word_name(CAstWord word)
{
    return (u32)word < C_AST_WORD_COUNT ? c_ast_word_names[word] : S8("invalid");
}

CAstContract c_ast_kind_contract(CAstKind kind)
{
    return (u32)kind < C_AST_KIND_COUNT ? (CAstContract)c_ast_kind_contracts[kind] : C_AST_CONTRACT_LEAF;
}

// The slice [count, roots...] holding n's children, or C_AST_NODE_INVALID
// when the layout keeps none for it.
BUSTER_GLOBAL_LOCAL u32 c_ast_slice_of(CAst const* ast, u32 node)
{
    u32 result = C_AST_NODE_INVALID;
    if (ast->layout == C_AST_LAYOUT_HYBRID && c_ast_kind_contracts[ast->kinds[node]] == C_AST_CONTRACT_LIST)
    {
        result = ast->data[node];
    }
    else if (ast->layout == C_AST_LAYOUT_EXPLICIT && ast->slices)
    {
        result = ast->slices[node];
    }
    return result;
}

u32 c_ast_child_count(CAst const* ast, u32 node)
{
    u32 result = 0;
    if (ast->extents[node] > 1)
    {
        u32 slice = c_ast_slice_of(ast, node);
        result = slice != C_AST_NODE_INVALID ? ast->children[slice] : c_ast_count_children(ast, node);
    }
    return result;
}

u32 c_ast_list_count(CAst const* ast, u32 node)
{
    u32 result = 0;
    CAstKind kind = (CAstKind)ast->kinds[node];
    if (c_ast_kind_contracts[kind] == C_AST_CONTRACT_LIST)
    {
        result = ast->layout == C_AST_LAYOUT_HYBRID
                     ? ast->children[ast->data[node]] - c_ast_kind_arity_a[kind] - c_ast_kind_arity_b[kind]
                     : ast->data[node];
    }
    return result;
}

u32 c_ast_child_at(CAst const* ast, u32 node, u32 index)
{
    u32 result = C_AST_NODE_INVALID;
    u32 count = c_ast_child_count(ast, node);
    if (index < count)
    {
        u32 slice = c_ast_slice_of(ast, node);
        if (slice != C_AST_NODE_INVALID)
        {
            result = ast->children[slice + 1 + index];
        }
        else
        {
            u32 child = node - 1;
            for (u32 skip = count - 1; skip > index; skip -= 1)
            {
                child -= ast->extents[child];
            }
            result = child;
        }
    }
    return result;
}

u32 c_ast_children(CAst const* ast, u32 node, u32* roots, u32 capacity)
{
    u32 count = c_ast_child_count(ast, node);
    u32 slice = count ? c_ast_slice_of(ast, node) : C_AST_NODE_INVALID;
    if (slice != C_AST_NODE_INVALID)
    {
        for (u32 index = 0; index < count && index < capacity; index += 1)
        {
            roots[index] = ast->children[slice + 1 + index];
        }
    }
    else if (count)
    {
        u32 child = node - 1;
        for (u32 index = count; index > 0; index -= 1)
        {
            if (index - 1 < capacity)
            {
                roots[index - 1] = child;
            }
            child -= ast->extents[child];
        }
    }
    return count;
}

#define C_AST_WALK_EXIT_BIT 0x80000000u

CAstWalk c_ast_walk_begin(Arena* arena, CAst const* ast, u32 node)
{
    CAstWalk walk = {.ast = ast, .arena = arena};
    // Each node has at most one pending entry (its enter or its exit), so the
    // subtree's node count bounds the stack.
    walk.capacity = ast->extents[node];
    walk.stack = arena_allocate(arena, u32, walk.capacity);
    walk.stack[0] = node;
    walk.count = 1;
    return walk;
}

CAstWalkEvent c_ast_walk_next(CAstWalk* walk, u32* node_out)
{
    CAstWalkEvent result = C_AST_WALK_DONE;
    if (walk->count)
    {
        u32 entry = walk->stack[walk->count - 1];
        walk->count -= 1;
        walk->steps += 1;
        u32 node = entry & ~C_AST_WALK_EXIT_BIT;
        *node_out = node;
        if (entry & C_AST_WALK_EXIT_BIT)
        {
            result = C_AST_WALK_EXIT;
        }
        else
        {
            result = C_AST_WALK_ENTER;
            u32 extent = walk->ast->extents[node];
            if (extent > 1)
            {
                // The exit is pushed first so it surfaces after the children;
                // the children go on last-first so the first pops first.
                walk->stack[walk->count] = node | C_AST_WALK_EXIT_BIT;
                walk->count += 1;
                u32 begin = node + 1 - extent;
                u32 cursor = node;
                while (cursor > begin)
                {
                    u32 child = cursor - 1;
                    walk->stack[walk->count] = child;
                    walk->count += 1;
                    walk->steps += 1;
                    cursor = child + 1 - walk->ast->extents[child];
                }
            }
        }
    }
    return result;
}

// ---- validation -----------------------------------------------------------

u32 c_ast_validate(CAst const* ast)
{
    u32 result = C_AST_NODE_INVALID;
    u32 count = ast->node_count;
    if (count && (ast->root != count - 1 || ast->extents[count - 1] != count))
    {
        result = count - 1;
    }
    for (u32 node = 0; node < count && result == C_AST_NODE_INVALID; node += 1)
    {
        u32 kind = ast->kinds[node];
        u32 extent = ast->extents[node];
        if (kind >= C_AST_KIND_COUNT || extent == 0 || extent > node + 1)
        {
            result = node;
        }
        else
        {
            // The children are the maximal subtrees tiling [begin, node).
            u32 begin = node + 1 - extent;
            u32 cursor = node;
            u32 children = 0;
            bool tiled = true;
            while (cursor > begin && tiled)
            {
                u32 child = cursor - 1;
                u32 child_extent = ast->extents[child];
                if (child_extent == 0 || child_extent > cursor - begin)
                {
                    tiled = false;
                }
                else
                {
                    cursor = child + 1 - child_extent;
                    children += 1;
                }
            }
            u32 a = c_ast_kind_arity_a[kind];
            u32 b = c_ast_kind_arity_b[kind];
            bool contract_ok = tiled;
            u32 slice = C_AST_NODE_INVALID;
            if (contract_ok)
            {
                switch ((CAstContract)c_ast_kind_contracts[kind])
                {
                case C_AST_CONTRACT_LEAF:
                    contract_ok = children == 0;
                    break;
                case C_AST_CONTRACT_FIXED:
                    contract_ok = children == a;
                    break;
                case C_AST_CONTRACT_RANGE:
                    contract_ok = children >= a && children <= b;
                    break;
                case C_AST_CONTRACT_PRESENCE:
                {
                    u32 present = 0;
                    for (u32 bits = ast->data[node] & C_AST_PRESENCE_MASK; bits; bits &= bits - 1)
                    {
                        present += 1;
                    }
                    contract_ok = children == a + present;
                    break;
                }
                case C_AST_CONTRACT_LIST:
                {
                    if (ast->layout == C_AST_LAYOUT_HYBRID)
                    {
                        slice = ast->data[node];
                        contract_ok = ast->children && slice < ast->children_count && ast->children[slice] == children && children >= a + b;
                    }
                    else
                    {
                        contract_ok = (u64)children == (u64)a + ast->data[node] + b;
                    }
                    break;
                }
                default:
                    contract_ok = false;
                    break;
                }
            }
            if (contract_ok && ast->layout == C_AST_LAYOUT_EXPLICIT)
            {
                slice = ast->slices ? ast->slices[node] : C_AST_NODE_INVALID;
                contract_ok = extent > 1 ? slice != C_AST_NODE_INVALID : slice == C_AST_NODE_INVALID;
                if (contract_ok && slice != C_AST_NODE_INVALID)
                {
                    contract_ok = slice < ast->children_count && ast->children[slice] == children;
                }
            }
            if (contract_ok && slice != C_AST_NODE_INVALID)
            {
                // The slice must hold exactly the implicit topology's roots.
                contract_ok = (u64)slice + 1 + children <= ast->children_count;
                u32 child = node - 1;
                for (u32 index = children; index > 0 && contract_ok; index -= 1)
                {
                    contract_ok = ast->children[slice + index] == child;
                    child -= ast->extents[child];
                }
            }
            if (!contract_ok)
            {
                result = node;
            }
        }
    }
    return result;
}

// ---- dump -----------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void c_ast_dump_put(char8* out, u64* position, String8 text)
{
    if (out && text.length)
    {
        memcpy(out + *position, text.pointer, text.length);
    }
    *position += text.length;
}

BUSTER_GLOBAL_LOCAL void c_ast_dump_put_flags(char8* out, u64* position, u32 flags, bool pointer_flags, bool array_flags)
{
    struct
    {
        u32 bit;
        String8 name;
    } const names[] = {
        {C_AST_QUALIFIER_CONST, S8_INITIALIZER("const")},
        {C_AST_QUALIFIER_VOLATILE, S8_INITIALIZER("volatile")},
        {C_AST_QUALIFIER_RESTRICT, S8_INITIALIZER("restrict")},
        {C_AST_QUALIFIER_ATOMIC, S8_INITIALIZER("_Atomic")},
        {C_AST_QUALIFIER_NONNULL, S8_INITIALIZER("_Nonnull")},
        {C_AST_QUALIFIER_NULLABLE, S8_INITIALIZER("_Nullable")},
        {C_AST_QUALIFIER_NULL_UNSPECIFIED, S8_INITIALIZER("_Null_unspecified")},
        {C_AST_ARRAY_STATIC, S8_INITIALIZER("static")},
        {C_AST_ARRAY_STAR, S8_INITIALIZER("*")},
    };
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        bool eligible = names[index].bit < C_AST_ARRAY_STATIC ? (pointer_flags || array_flags) : array_flags;
        if (eligible && (flags & names[index].bit))
        {
            c_ast_dump_put(out, position, S8(" "));
            c_ast_dump_put(out, position, names[index].name);
        }
    }
}

// The annotation (with its leading space) a node prints after its kind.
BUSTER_GLOBAL_LOCAL void c_ast_dump_annotation(char8* out, u64* position, CAst const* ast, CPreprocessResult const* preprocess, u32 node)
{
    CAstKind kind = (CAstKind)ast->kinds[node];
    u32 token_index = ast->tokens[node];
    switch (kind)
    {
    case C_AST_IDENTIFIER:
    case C_AST_TYPEDEF_NAME:
    case C_AST_TAG_NAME:
    case C_AST_DECLARATOR_NAME:
    case C_AST_MEMBER:
    case C_AST_MEMBER_ARROW:
    case C_AST_GOTO:
    case C_AST_LABELED:
    case C_AST_ENUMERATOR:
    case C_AST_DESIGNATOR_MEMBER:
    case C_AST_LABEL_ADDRESS:
    case C_AST_ATTRIBUTE:
    case C_AST_ATTRIBUTE_SCOPED:
    case C_AST_ATTRIBUTE_NAMESPACE:
    case C_AST_ASM_SYMBOLIC_NAME:
    case C_AST_NUMBER:
    case C_AST_CHARACTER:
    case C_AST_BOOLEAN_CONSTANT:
    {
        c_ast_dump_put(out, position, S8(" "));
        c_ast_dump_put(out, position, c_token_spelling(preprocess->spelling_base, preprocess->tokens[token_index]));
        break;
    }
    case C_AST_STRING:
    {
        for (u32 index = 0; index < ast->data[node]; index += 1)
        {
            c_ast_dump_put(out, position, S8(" "));
            c_ast_dump_put(out, position, c_token_spelling(preprocess->spelling_base, preprocess->tokens[token_index + index]));
        }
        break;
    }
    case C_AST_SPECIFIER_WORD:
    {
        c_ast_dump_put(out, position, S8(" "));
        c_ast_dump_put(out, position, c_ast_word_name((CAstWord)ast->data[node]));
        break;
    }
    case C_AST_DECLARATOR_POINTER:
    {
        c_ast_dump_put_flags(out, position, ast->data[node], true, false);
        break;
    }
    case C_AST_DECLARATOR_ARRAY:
    {
        c_ast_dump_put_flags(out, position, ast->data[node], true, true);
        break;
    }
    case C_AST_ASM:
    {
        if (ast->data[node] & C_AST_ASM_VOLATILE)
        {
            c_ast_dump_put(out, position, S8(" volatile"));
        }
        if (ast->data[node] & C_AST_ASM_INLINE)
        {
            c_ast_dump_put(out, position, S8(" inline"));
        }
        if (ast->data[node] & C_AST_ASM_GOTO)
        {
            c_ast_dump_put(out, position, S8(" goto"));
        }
        break;
    }
    default:
        break;
    }
}

// One walk over the subtree, writing (out non-null) or measuring (out null).
BUSTER_GLOBAL_LOCAL u64 c_ast_dump_pass(Arena* scratch, CAst const* ast, CPreprocessResult const* preprocess, u32 root, char8* out)
{
    u64 position = 0;
    CAstWalk walk = c_ast_walk_begin(scratch, ast, root);
    bool first = true;
    u32 node = 0;
    CAstWalkEvent event = c_ast_walk_next(&walk, &node);
    while (event != C_AST_WALK_DONE)
    {
        if (event == C_AST_WALK_ENTER)
        {
            if (!first)
            {
                c_ast_dump_put(out, &position, S8(" "));
            }
            first = false;
            c_ast_dump_put(out, &position, S8("("));
            c_ast_dump_put(out, &position, c_ast_kind_name((CAstKind)ast->kinds[node]));
            c_ast_dump_annotation(out, &position, ast, preprocess, node);
            if (ast->extents[node] == 1)
            {
                c_ast_dump_put(out, &position, S8(")"));
            }
        }
        else
        {
            c_ast_dump_put(out, &position, S8(")"));
        }
        event = c_ast_walk_next(&walk, &node);
    }
    return position;
}

String8 c_ast_dump(Arena* arena, CAst const* ast, CPreprocessResult preprocess, u32 node)
{
    String8 result = {0};
    if (node < ast->node_count)
    {
        TemporalArena temporary = scratch_begin(&arena, 1);
        u64 length = c_ast_dump_pass(temporary.arena, ast, &preprocess, node, 0);
        scratch_end(temporary);
        temporary = scratch_begin(&arena, 1);
        char8* text = arena_allocate(arena, char8, length);
        c_ast_dump_pass(temporary.arena, ast, &preprocess, node, text);
        scratch_end(temporary);
        result = (String8){.pointer = text, .length = length};
    }
    return result;
}
