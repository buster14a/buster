// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The parser-to-checker reader. It retains parser token identity, elaborates supported declarations in visibility
// order, resolves uses through c_bindings, and emits expression nodes in the parser's postorder. Parser depths are
// bracket depths and are never used as semantic dependency levels. Unsupported declarators and statements fail
// explicitly; no unresolved name or unrecognized syntax acquires an invented int type.

// Parser token indices are below 1 << 30. The high bit distinguishes a source-only selection retained by a
// declaration/type-query proof from an evaluated selection that may supply an expression graph operand.
#define CHECK_INPUT_SOURCE_SELECTION 0x80000000u

typedef enum CheckInputMessage
{
    CHECK_INPUT_UNSUPPORTED,
    CHECK_INPUT_DECLARATION,
    CHECK_INPUT_UNDECLARED,
    CHECK_INPUT_LITERAL,
    CHECK_INPUT_RESOURCE,
    CHECK_INPUT_REDECLARATION,
    CHECK_INPUT_RETURN,
    CHECK_INPUT_TARGET,
    CHECK_INPUT_INCOMPLETE_MEMBER,
    CHECK_INPUT_INCOMPLETE_OBJECT,
    CHECK_INPUT_LOOP_HINT,
    CHECK_INPUT_LOOP_PLACEMENT,
    // An unroll pragma's count that clang can't read: empty, starting with no expression, unclosed.
    CHECK_INPUT_LOOP_MISSING,
    CHECK_INPUT_LOOP_EXPRESSION,
    CHECK_INPUT_LOOP_PARENTHESIS,
    CHECK_INPUT_NAKED_STATEMENT,
    CHECK_INPUT_NAKED_PARAMETER,
} CheckInputMessage;

static const char* const check_input_messages[] =
{
    "semantic checking of this construct is not implemented",
    "invalid or unsupported declaration",
    "use of undeclared identifier",
    "invalid or unsupported literal",
    "out of memory during semantic checking",
    "conflicting or repeated declaration",
    "return statement does not match the function's result type",
    "unsupported checker language or target model",
    "record member has incomplete type",
    "object has incomplete type",
    "invalid loop hint pragma",
    "loop hint pragma not followed by a loop",
    "unroll pragma's count missing",
    "unroll pragma's count not an expression",
    "unroll pragma's count unclosed",
    "non-ASM statement in naked function is not supported",
    "parameter references not allowed in naked functions",
};

// The type/action tables currently describe one C LP64 model: 32-bit wchar_t, x87 long double stored in 16 bytes, and
// arithmetic evaluated in its source format, with plain char signed or unsigned as the options say
// (TypeTable.char_unsigned). Refuse preprocessing options that advertise a different model before applying those fixed
// tables to the source.
static bool check_input_target_supported(const PPOptions* options)
{
    const PredefinesOptions* target = &options->predefines;
    const bool source_evaluation = target->fp_eval_method == BUSTER_PREDEFINES_EVAL_SOURCE ||
                                   (target->fp_eval_method == BUSTER_PREDEFINES_EVAL_DEFAULT && predefines_eval_method(target) == 0);
    return !options->assembly && !target->short_wchar && target->long_double == BUSTER_PREDEFINES_LONG_DOUBLE_80 && source_evaluation;
}

// CheckInputReader.enum_fixed: an enum without a fixed underlying type (or another tag), one with it whose enumerators
// aren't listed yet, and one with them.
#define BUSTER_CHECK_ENUM_NONE 0
#define BUSTER_CHECK_ENUM_FIXED 1
#define BUSTER_CHECK_ENUM_LISTED 2

typedef struct CheckInputReport CheckInputReport;
struct CheckInputReport
{
    uint32_t token;
    CheckInputMessage message;
    uint32_t pragma;
};

static __attribute__((unused)) uint32_t check_input_report_source(const Parser* parser, const CheckInputReport* report)
{
    return report->pragma != 0 ? (report->pragma <= parser->tokens.pragma_count ? parser->tokens.pragma_sources[report->pragma - 1] + 1 : BUSTER_CHECK_NONE) :
           report->token < parser->tokens.count ? parser->tokens.sources[report->token] : BUSTER_CHECK_NONE;
}

typedef struct CheckInput CheckInput;
typedef struct CheckInputAttributes CheckInputAttributes;
typedef struct CheckInputRegion CheckInputRegion;
typedef struct CheckInputVla CheckInputVla;
typedef struct CheckInputAsm CheckInputAsm;
typedef struct CheckInputDiagnosticPragmas CheckInputDiagnosticPragmas;
typedef struct CheckInputNonnullPragmas CheckInputNonnullPragmas;
typedef struct CheckInputFpPragma CheckInputFpPragma;
typedef struct CheckInputPackPragmas CheckInputPackPragmas;
typedef struct CheckInputIntrinsicDeclaration CheckInputIntrinsicDeclaration;
typedef struct CheckInputIntrinsicCall CheckInputIntrinsicCall;
typedef struct CheckInputPackedMember CheckInputPackedMember;
struct CheckInputPackedMember
{
    uint32_t address;
    uint32_t token;
    uint32_t member;
    uint32_t record;
    String8 member_name;
    String8 record_name;
    uint8_t alignment_log2;
    bool implicit_conversions;
    bool suppressed;
};
typedef struct CheckInputLoopDiagnostic CheckInputLoopDiagnostic;
typedef struct CheckInputNakedDiagnostic CheckInputNakedDiagnostic;
struct CheckInputNakedDiagnostic
{
    uint32_t position;
    uint32_t token;
    uint32_t attribute;
};
#define CHECK_INPUT_FUNCTION_INLINE 0x01u
#define CHECK_INPUT_FUNCTION_EXTERN 0x02u
#define CHECK_INPUT_FUNCTION_NAKED 0x04u
struct CheckInputVla
{
    uint32_t declaration;
    TypeId type;
    uint32_t bound_begin;
    uint32_t bound_end;
    uint32_t bound_node;
    uint32_t body;
    uint32_t scope_begin;
    uint32_t scope_end;
    bool evaluated;
};

struct CheckInputRegion
{
    uint32_t opening;
    uint32_t closing;
    uint32_t block;
    uint32_t body;
    uint32_t result;
    uint32_t final_statement;
};

struct CheckInput
{
    CheckGraph graph;
    CheckEnvironment environment;
    CheckInputDiagnosticPragmas* diagnostic_pragmas;
    CheckInputNonnullPragmas* nonnull_pragmas;
    CheckInputPackPragmas* pack_pragmas;
    // Only a rejected loop-hint statement retains this cold diagnostic, with its completion and spelling apart.
    CheckInputLoopDiagnostic* loop_diagnostic;
    CheckInputNakedDiagnostic* naked_diagnostic;
    uint8_t* pack_log2;
    CheckInputFpPragma* fp_pragmas;
    uint32_t fp_pragma_count;
    uint8_t* fp_environments;
    uint8_t* cx_limited_ranges;
    const uint8_t* deferred_tokens;
    CheckInputIntrinsicDeclaration* intrinsic_declarations;
    uint32_t intrinsic_declaration_count;
    uint32_t intrinsic_declaration_capacity;
    CheckInputIntrinsicCall* intrinsic_calls;
    uint32_t intrinsic_call_count;
    uint32_t intrinsic_call_capacity;
    uint32_t* intrinsic_ids;
    uint32_t* overload_heads;
    CheckInputReport* reports;
    uint32_t report_count;
    uint32_t report_capacity;
    uint32_t* declarations;
    TypeId* declared_types;
    uint8_t* declaration_kinds;
    uint8_t* function_flags;
    uint8_t* explicit_abis;
    uint32_t* objects;
    uint8_t* static_objects;
    uint8_t* registers;
    uint8_t* thread_local;
    uint8_t* compound_objects;
    CheckInputAttributes** attributes;
    uint8_t* linkage;
    uint8_t* defined;
    uint8_t* tentative;
    uint32_t* initializer_nodes;
    uint32_t* initializer_begins;
    uint32_t* initializer_ends;
    uint32_t* token_nodes;
    // Sparse source diagnostics, including addresses in type queries that have no runtime graph node.
    CheckInputPackedMember* packed_members;
    uint32_t packed_member_count;
    uint8_t* constant_warnings;
    uint8_t* constant_warning_ignored;
    uint32_t* body_declarations;
    uint32_t* declaration_bodies;
    uint32_t* parameters;
    uint32_t* parameter_next;
    uint32_t* control_owners;
    uint32_t* label_targets;
    // Resolved &&label expressions, counted by check_input_controls; zero lets check_program omit its address table.
    uint32_t label_address_count;
    uint32_t* case_ranges;
    CheckInputRegion* regions;
    uint32_t* expression_regions;
    uint32_t region_count;
    CheckInputVla* vlas;
    uint32_t vla_count;
    uint32_t vla_capacity;
    CheckInputAsm* asm_statements;
    uint32_t asm_count;
    uint32_t asm_capacity;
    uint32_t* asm_ids;
    CheckInitString* strings;
    // Literal index by token, including every part of an adjacent literal sequence; NONE for other tokens.
    // Retained from the reader so initializer planning also resolves an array-valued literal by object - 1.
    uint32_t* string_ids;
    uint32_t string_count;
    uint32_t token_count;
    CBindingStatistics binding_statistics;
    uint32_t binding_event_count;
    CBindingStatistics parser_binding_statistics;
    uint32_t parser_binding_event_count;
    // The tokens the scalar path's walks act at that the vector path's would have skipped (#529): the walk in source
    // order's tokens that no mark made (check_input_visit), and the expressions' walk's that its classes left out
    // (check_input_expression_class). Always 0 on the vector path; the tests require 0 of the scalar path too.
    uint32_t visit_misses;
    // The tokens the walk in source order and the expressions' walk visited: every token of their ranges on the scalar
    // path, and on the vector path those they act at, which the tests require to be fewer.
    uint32_t resolve_visits;
    uint32_t expression_visits;
    bool diagnostic_graph;
    bool resource;
};

// What check_input_resolve_range tests a token for by its syntax alone, as CheckInputReader.syntax keeps it: bits of the
// tests it would make of the token's name and kind, each a function of the token (and its successor) alone.
// One of the names whose builtins the walk prepares: __builtin_va_arg, __builtin_convertvector, __builtin_bit_cast,
// _Generic, __builtin_choose_expr, __builtin_types_compatible_p and __builtin_offsetof.
#define BUSTER_CHECK_SYNTAX_NAMED 0x01u
// check_input_attribute_starts(parser, token, parser->tokens.count).
#define BUSTER_CHECK_SYNTAX_ATTRIBUTE 0x02u
// A string, or __func__, __FUNCTION__ or __PRETTY_FUNCTION__ (check_input_function_name_token).
#define BUSTER_CHECK_SYNTAX_STRING 0x04u

// Which tokens the two walks' vector paths visit (#529), and what they test there: CheckInputReader.visits, a byte a
// token. Before the walk in source order (check_input_resolve_range), the reasons it may act at the token, a bit each,
// where 0 says it does nothing there: its syntax (check_input_count_tokens), or a write ahead of the walk to one of
// the columns it tests at the token (check_input_visit), which it tests only where a bit says.
// The token's syntax: the walk's tests of its role, kind and syntax bits, and its successor's.
#define BUSTER_CHECK_RESOLVE_SYNTAX 0x01u
// Declarations activate at the token (CheckInputReader.activate).
#define BUSTER_CHECK_RESOLVE_ACTIVATE 0x02u
// A scope ends before the token (CheckInputReader.scope_ends), whose bindings the walk restores there.
#define BUSTER_CHECK_RESOLVE_SCOPE 0x04u
// Initializers finish at the token: their arrays' sizes are inferred there (initializer_finish_heads).
#define BUSTER_CHECK_RESOLVE_FINISH 0x08u
// A declarator continues or an initializer starts at the token (declaration_bases, initializers).
#define BUSTER_CHECK_RESOLVE_DECLARATION 0x10u
// A specifier's type query finishes at the token, or a designator's, a selection's type, a va_arg's or a builtin's type
// name starts there (specifier_finishes, designator_valid, selection_type_ends, vararg_types, builtin_type_ends).
#define BUSTER_CHECK_RESOLVE_QUERY 0x20u
// Before the expressions' walk (check_input_expressions), the token's classes (check_input_expression_classes):
// The expressions' walk may act at the token: it has children, a sparse column has an entry for it, or its role or
// kind is one the walk acts at.
#define BUSTER_CHECK_EXPRESSION_ACTS 0x01u
// One of the sparse columns the walk tests first has an entry for the token: a statement expression's group, a cached
// constant, a selection, a va_arg call, a designator, a variable array's bound or an asm statement.
#define BUSTER_CHECK_EXPRESSION_SPARSE 0x02u

// The walks' tests of a token's role and of its kind, a bit each, in tables that a pass looks 64 tokens up in at once
// (_mm512_permutex2var_epi8 of a 128-byte table: roles and kinds are below 128). The scalar twins of the passes that read
// them make the walks' own tests instead (check_input_visit_of, check_input_expression_class).
// The walk in source order acts at a token of the role (check_input_resolve_range).
#define BUSTER_CHECK_ROLE_RESOLVE 0x01u
// An identifier of the role is no name the walk in source order resolves: a member's, a tag's or an asm label's.
#define BUSTER_CHECK_ROLE_UNNAMED 0x02u
// A group's opening, a block's and a specifier: the tests of a group of a block (a statement expression) and of an
// __extension__ before a declaration.
#define BUSTER_CHECK_ROLE_GROUP 0x04u
#define BUSTER_CHECK_ROLE_BLOCK 0x08u
#define BUSTER_CHECK_ROLE_SPECIFIER 0x10u
// The expressions' walk acts at a token of the role that isn't ignored (check_input_expressions)...
#define BUSTER_CHECK_ROLE_EXPRESSION 0x20u
// ...or at one that is: the roles whose values it passes on as a list.
#define BUSTER_CHECK_ROLE_LIST 0x40u
// The walk in source order acts at a token of the kind.
#define BUSTER_CHECK_KIND_RESOLVE 0x01u
#define BUSTER_CHECK_KIND_IDENTIFIER 0x02u
#define BUSTER_CHECK_KIND_EXTENSION 0x04u
// struct and enum, after an __extension__ before a declaration.
#define BUSTER_CHECK_KIND_TAG 0x08u
// The expressions' walk acts at a token of the kind that isn't ignored.
#define BUSTER_CHECK_KIND_EXPRESSION 0x20u

_Static_assert(PARSE_ROLE_COUNT <= 128 && PARSE_KIND_COUNT <= 128, "the walks' role and kind tests are 128-entry tables");

static const uint8_t check_input_role_tests[128] __attribute__((aligned(64))) =
{
    [PARSE_ROLE_OPERAND] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_MEMBER] = BUSTER_CHECK_ROLE_UNNAMED,
    [PARSE_ROLE_LEAF_RIGHT] = BUSTER_CHECK_ROLE_UNNAMED,
    [PARSE_ROLE_SPECIFIER] = BUSTER_CHECK_ROLE_RESOLVE | BUSTER_CHECK_ROLE_SPECIFIER | BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_TAG] = BUSTER_CHECK_ROLE_UNNAMED | BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_PREFIX] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_POSTFIX] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_MEMBER_OPERATOR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_MULTIPLICATIVE] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_ADDITIVE] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_SHIFT] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_RELATIONAL] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_EQUALITY] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_BITWISE_AND] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_BITWISE_XOR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_BITWISE_OR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_LOGICAL_AND] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_LOGICAL_OR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_ASSIGN] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_RANGE] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_BITFIELD] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_COMMA] = BUSTER_CHECK_ROLE_EXPRESSION | BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_RETURN] = BUSTER_CHECK_ROLE_RESOLVE | BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_ASSOCIATION] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_SEPARATOR] = BUSTER_CHECK_ROLE_EXPRESSION | BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_TERMINATOR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_LABEL] = BUSTER_CHECK_ROLE_RESOLVE | BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_OPEN_GROUP] = BUSTER_CHECK_ROLE_GROUP,
    [PARSE_ROLE_OPEN_CAST] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_COMPOUND] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_SIZEOF] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_CONDITION] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_FOR] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_BLOCK] = BUSTER_CHECK_ROLE_RESOLVE | BUSTER_CHECK_ROLE_BLOCK,
    [PARSE_ROLE_OPEN_BODY] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_OPEN_TERNARY] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_OPEN_CASE] = BUSTER_CHECK_ROLE_RESOLVE,
    [PARSE_ROLE_CLOSE_GROUP] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_CALL] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_FUNCTION] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_CAST] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_SIZEOF] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_SPECIFIER] = BUSTER_CHECK_ROLE_EXPRESSION | BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_CLOSE_ATTRIBUTE] = BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER] = BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT] = BUSTER_CHECK_ROLE_LIST,
    [PARSE_ROLE_CLOSE_ASM] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_KEYWORD] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_CONDITION] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_FOR] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_SUBSCRIPT] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_STRUCT] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_ENUM] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_INITIALIZER] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_COMPOUND_BODY] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_TERNARY] = BUSTER_CHECK_ROLE_EXPRESSION,
    [PARSE_ROLE_CLOSE_CASE] = BUSTER_CHECK_ROLE_EXPRESSION,
};

static const uint8_t check_input_kind_tests[128] __attribute__((aligned(64))) =
{
    [PARSE_KIND_IDENTIFIER] = BUSTER_CHECK_KIND_IDENTIFIER,
    [PARSE_KIND_STRING] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_STRUCT] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_TAG | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_ENUM] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_TAG | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_ATOMIC] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_CALLING_CONVENTION] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_ATTRIBUTE] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_ASM] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_ALIGNAS] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_TYPEOF] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_TYPE_ARGUMENT] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_GENERIC] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_EXTENSION] = BUSTER_CHECK_KIND_EXTENSION | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_REAL_IMAG] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_LABEL] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_SWITCH] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_CASE] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_DEFAULT] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_GOTO] = BUSTER_CHECK_KIND_RESOLVE | BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_CONTINUE] = BUSTER_CHECK_KIND_EXPRESSION,
    [PARSE_KIND_BREAK] = BUSTER_CHECK_KIND_EXPRESSION,
};

typedef struct CheckInputReader CheckInputReader;
// #113's deferred bodies (c_check_deferred.c, included after this file): the top level's walk records where it
// skips each body, and the bodies are read and merged before the walk's later phases.
typedef struct CheckDeferred CheckDeferred;
static void check_deferred_cut(CheckInputReader* reader, uint32_t opening);
static void check_deferred_bodies(CheckDeferred* deferred, CheckInputReader* reader);
typedef struct CheckInputVarargs CheckInputVarargs;
typedef struct CheckInputSpecifierJob CheckInputSpecifierJob;
struct CheckInputSpecifierJob
{
    uint32_t first;
    uint32_t keyword;
    uint32_t closing;
    uint32_t qualifiers;
    uint32_t longs;
    uint32_t shorts;
    uint32_t signedness;
    uint32_t storage;
    uint32_t restrict_token;
    uint32_t vla_begin;
    TypeId base;
    TypeId result;
    CheckInputAttributes* attributes;
    uint8_t abi_origin;
    uint8_t result_abi_origin;
    bool integer_seen;
    bool saw;
    bool thread_local;
    bool complex;
    bool atomic;
    bool is_typedef;
    bool is_inline;
    bool ready;
};

struct CheckInputVarargs
{
    CheckVarargsContext context;
    uint32_t closing;
    uint32_t argument_count;
    uint32_t second_argument;
    TypeId requested_type;
};

struct CheckInputReader
{
    CheckInput* output;
    const Parser* parser;
    Preprocessor* pp;
    TypeTable* types;
    Arena* arena;
    Arena* scratch;
    CBindings bindings;
    TypeId* declared_types;
    TypeId* query_types;
    CheckInputSpecifierJob** specifier_jobs;
    CheckInputSpecifierJob** specifier_finishes;
    uint32_t specifier_owner;
    uint32_t specifier_query_depth;
    bool specifier_pending;
    uint32_t* function_specifiers;
    uint32_t function_name_ids[3];
    uint32_t file_function_name_ids[3];
    uint32_t function_name_body;
    TypeId* return_types;
    uint8_t* declaration_kinds;
    uint8_t* defined;
    uint8_t* definition_seen;
    uint8_t* static_objects;
    uint8_t* registers;
    uint8_t* linkage;
    uint32_t* objects;
    uint8_t* ignored;
    uint8_t* initializers;
    uint8_t* static_initializers;
    uint32_t* initializer_declarations;
    uint32_t* initializer_finish_heads;
    uint32_t* initializer_finish_next;
    TypeId* incomplete_array_types;
    uint32_t inference_end;
    uint32_t* string_ids;
    uint64_t* designator_indices;
    uint8_t* designator_valid;
    uint32_t* designator_tokens;
    uint32_t* designator_ranges;
    uint32_t* selected_tokens; // NONE, a runtime operand token, or SOURCE_SELECTION | a proof-only operand token.
    uint32_t* selection_type_ends;
    TypeId* selection_types;
    CheckValue* cached_constants;
    uint32_t* constant_present;
    uint32_t cached_constant_count;
    uint16_t* builtin_names;
    uint32_t* intrinsic_names;
    CheckOverloadCandidate* overload_candidates;
    uint32_t overload_capacity;
    CheckOverloadIndex overload_index;
    bool declarator_overloadable;
    uint16_t* builtins;
    uint32_t* builtin_type_ends;
    uint8_t* builtin_syntax_effects;
    uint32_t builtin_syntax_count;
    CheckInputVarargs* varargs;
    uint32_t* vararg_calls;
    uint32_t* vararg_types;
    uint32_t vararg_count;
    uint32_t vararg_capacity;
    uint32_t* vla_bounds;
    uint32_t* vla_ends;
    uint32_t* vla_scope_ends;
    uint32_t* vla_sizes;
    // Appended records of the next definition's header, retained separately from the body's later records.
    uint32_t parameter_vla_begin;
    uint32_t parameter_vla_end;
    uint8_t* expression_commas;
    uint8_t* conditions;
    uint32_t* region_roots;
    uint8_t* constant_contexts;
    uint8_t* warning_suppressed;
    uint16_t* warning_index;
    uint32_t warning_index_base;
    bool warning_index_ready;
    TypeId* declaration_bases;
    uint32_t* declaration_storage;
    uint8_t* declaration_flags;
    uint8_t* declaration_abis;
    CheckInputAttributes** attribute_bases;
    CheckInputAttributes* specifier_attributes;
    CheckInputAttributes* declarator_attributes;
    uint32_t declarator_declarations;
    // The prototypes open around the reader's position: a declarator's parameter lists, each a scope a level deeper than
    // the one around it (check_input_declaration_depth).
    uint32_t prototype_depth;
    uint8_t specifier_abi_origin;
    uint8_t declarator_abi_origin;
    uint8_t constant_abi_origin;
    bool specifier_thread_local;
    uint32_t* activate;
    uint32_t* activation_next;
    // Each token's tests by its syntax alone (BUSTER_CHECK_SYNTAX_*), from check_input_count_tokens' pass: the walk
    // tests a bit of a byte where it compared the token's name and kind with each name and kind it acts on.
    const uint8_t* syntax;
    // Which tokens the walks' vector paths visit, a byte each (count + 1): the walk in source order's marks
    // (BUSTER_CHECK_RESOLVE_*), then the expressions' walk's classes (BUSTER_CHECK_EXPRESSION_*). Lanes (#113) share it,
    // each writing its own body's.
    uint8_t* visits;
    uint32_t* parameters;
    uint32_t* parameter_next;
    uint32_t* parameter_seen;
    uint32_t* parameter_previous;
    uint8_t* parameter_shapes;
    // The tags and enumeration constants a definition's parameter list declared besides its parameters, which its body
    // declares again: the first at the body's opening (written with output->body_declarations there, which says whether
    // it was), and each one's next at its declaration token.
    uint32_t* parameter_declarations;
    uint32_t* parameter_declaration_next;
    uint32_t* member_seen;
    uint32_t* linked_declarations;
    TypeId* linked_types;
    TypeId* tag_types;
    uint32_t* tag_ends;
    unsigned __int128* enum_values;
    // The tree's traversal (#186, #544): the postorder and the tree's columns (Parser.positions, ends, first_children
    // and next_siblings: each token's position in the postorder, its subtree's last token, its first child and its next
    // sibling). The parser's where it derived them; otherwise the reader's own, which check_input_run derives into its
    // scratch: the C driver parses ParseOptions.tree_only, so that nothing derives them twice.
    const uint32_t* postorder;
    const uint32_t* postorder_positions;
    // Complete-subtree queries consume sizes by postorder position. Borrow the parser's optional column or derive
    // it with this reader's traversal; prefix/repair views and tree_only parses need no parser-owned copy.
    const uint32_t* subtree_sizes;
    uint32_t* tag_completions;
    // Per tag: whether an enum has a fixed underlying type (C23's `enum E : T`), which completes it at its base, and
    // whether its enumerators were listed yet (BUSTER_CHECK_ENUM_*).
    uint8_t* enum_fixed;
    uint32_t* transparent_unions;
    const uint32_t* ends;
    const uint32_t* first_child;
    const uint32_t* next_child;
    uint32_t* first_value;
    uint32_t* last_value;
    // The length of each token's chain of values, from first_value to last_value, written with them: a parent adds its
    // children's rather than walking their chains, which an initializer's separators, each a child of the next, made
    // quadratic in its elements. The scalar reader walks the chains, as the twin of these counts.
    uint32_t* value_counts;
    uint32_t* value_next;
    uint32_t* scope_marks;
    uint32_t* scope_ends;
    uint32_t* scope_openings;
    uint32_t scope_count;
    TypeId function_result;
    uint32_t function_end;
    uint32_t function_body;
    bool naked_functions;
    uint32_t resolve_limit;
    uint32_t resolve_pragma;
    uint32_t resolve_diagnostic_token;
    uint32_t resolve_position;
    CheckInputMessage resolve_message;
    uint32_t expression_limit;
    uint32_t fragment_end;
    // GNU C's computed gotos (goto *expression) in the unit, which check_input_controls counts: only a unit with one has
    // its functions' labels' scopes checked against them.
    uint32_t indirect_gotos;
    uint32_t predefined_names[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
    TypeId predefined_types[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
    uint32_t builtin_member_names[10];
    // #113: the walk's token, where each predefined type was made (BUSTER_CHECK_NONE while it isn't), and the
    // deferred reader's state. A lane reads one body (c_check_deferred.c): from its opening, whose activations and
    // initializers' finishes the top level made, to the token after its closing, where only its scopes end. lane_unsupported says it did what only
    // source order can merge, and the unit is read in source order instead; lane_reason says what, first.
    uint32_t position;
    uint32_t predefined_positions[BUSTER_PARSE_PREDECLARED_TYPEDEFS];
    CheckDeferred* deferred;
    const char* lane_reason;
    bool lane;
    bool lane_unsupported;
};

// A body read on a lane that does what only source order can merge (`refused`), the first time with `reason`.
static void check_input_lane_refuse(CheckInputReader* reader, bool refused, const char* reason)
{
    reader->lane_reason = refused && !reader->lane_unsupported ? reason : reader->lane_reason;
    reader->lane_unsupported |= refused;
}

// Marks `token` (at most the unit's count) for the walk in source order's vector path with `reason`
// (BUSTER_CHECK_RESOLVE_*): the path visits only the tokens marked (CheckInputReader.visits), and tests there the columns
// whose reasons the marks give. Each write ahead of the walk to a column the walk tests at a token makes one, as a
// token's syntax makes the rest (check_input_count_tokens). The scalar path, which visits every token and tests every
// column, counts the tokens it acts at for a reason no mark gave (CheckInput.visit_misses).
static void check_input_visit(CheckInputReader* reader, uint32_t token, uint8_t reason)
{
    reader->visits[token] |= reason;
}

static void check_input_report(CheckInputReader* reader, uint32_t token, CheckInputMessage message)
{
    CheckInput* input = reader->output;
    if (input->resource)
    {
        message = CHECK_INPUT_RESOURCE;
    }
    if (input->report_count < input->report_capacity)
    {
        input->reports[input->report_count++] = (CheckInputReport)
        {
            .token = token,
            .message = message,
        };
    }
    else
    {
        input->resource = true;
    }
}

static void check_input_pragma_report(CheckInputReader* reader)
{
    const uint32_t before = reader->output->report_count;
    const uint32_t token = reader->resolve_position != BUSTER_CHECK_NONE ? reader->resolve_position :
                           reader->resolve_diagnostic_token != BUSTER_CHECK_NONE ? reader->resolve_diagnostic_token : reader->resolve_limit;
    check_input_report(reader, token, reader->resolve_message);
    if (reader->output->report_count != before && reader->resolve_diagnostic_token == BUSTER_CHECK_NONE)
    {
        reader->output->reports[before].pragma = reader->resolve_pragma + 1;
    }
}

static TypeId check_input_type_result(CheckInputReader* reader, TypeId type)
{
    if (type == 0 && reader->types->last_error == TYPE_ERROR_FULL)
    {
        reader->output->resource = true;
    }
    return type;
}

// Scans of a token column for the reader's passes that act on few tokens (#529): each gives the tokens from `first`
// (of the next 64, 16 or 8 below `count`, by the column's width) it selects, a bit each, for its pass's vector path to
// visit in order. The pass's scalar path, its twin, visits every token instead (check_input_scan_all), and its body
// tests each one, so that both paths act on the same tokens.
static uint64_t check_input_scan_all(uint32_t first, uint32_t count)
{
    return count - first < 64 ? _bzhi_u64(UINT64_MAX, count - first) : UINT64_MAX;
}

// The tokens whose byte in `column` is `value`.
static uint64_t check_input_scan_equal(const uint8_t* column, uint32_t first, uint32_t count, uint8_t value)
{
    const __mmask64 lanes = check_input_scan_all(first, count);
    return _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, column + first), _mm512_set1_epi8((char)value));
}

// The tokens whose byte in `column` isn't 0.
static uint64_t check_input_scan_nonzero(const uint8_t* column, uint32_t first, uint32_t count)
{
    const __mmask64 lanes = check_input_scan_all(first, count);
    const __m512i bytes = _mm512_maskz_loadu_epi8(lanes, column + first);
    return _mm512_mask_test_epi8_mask(lanes, bytes, bytes);
}

// Of the next 16, the tokens whose 32-bit value in `column` isn't 0.
static uint32_t check_input_scan_nonzero32(const uint32_t* column, uint32_t first, uint32_t count)
{
    const __mmask16 lanes = (__mmask16)check_input_scan_all(first, count);
    const __m512i values = _mm512_maskz_loadu_epi32(lanes, column + first);
    return _mm512_mask_test_epi32_mask(lanes, values, values);
}

// Of the next 16, the tokens whose 32-bit value in `column` is below `bound`.
static uint32_t check_input_scan_below(const uint32_t* column, uint32_t first, uint32_t count, uint32_t bound)
{
    const __mmask16 lanes = (__mmask16)check_input_scan_all(first, count);
    return _mm512_mask_cmplt_epu32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, column + first), _mm512_set1_epi32((int32_t)bound));
}

// Of the next 8, the tokens whose pointer in `column` isn't null.
static uint32_t check_input_scan_pointers(const void* const* column, uint32_t first, uint32_t count)
{
    const __mmask8 lanes = (__mmask8)check_input_scan_all(first, count);
    const __m512i pointers = _mm512_maskz_loadu_epi64(lanes, column + first);
    return _mm512_mask_test_epi64_mask(lanes, pointers, pointers);
}

#include "c_check_attribute.c"
static const CheckInputAttribute* check_input_naked_attribute(const CheckInputAttributes* attributes);
static void check_input_naked_inherit(CheckInputReader* reader, uint32_t token, uint32_t previous);

static TypeId check_input_declarator_abi(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type, uint8_t* origin)
{
    TypeId function = type;
    uint8_t kind = type_kind(reader->types, function);
    while (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED)
    {
        function = reader->types->types.base[type_index(function)];
        kind = type_kind(reader->types, function);
    }
    if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        *origin = check_input_attribute_abi_origin(reader, attributes, *origin);
    }
    return check_input_attribute_abi(reader, attributes, type);
}

static bool check_input_read_attributes(CheckInputReader* reader, uint32_t* cursor, uint32_t end, CheckInputAttributes** attributes);
static TypeId check_input_constant_type(CheckInputReader* reader, uint32_t first, uint32_t end);
static bool check_input_constant(CheckInputReader* reader, uint32_t first, uint32_t end, int64_t* value);
static bool check_input_vla_query(CheckInputReader* reader, uint32_t first, uint32_t end, TypeId type);

static bool check_input_qualifier(uint32_t name, uint32_t* qualifiers)
{
    uint32_t bit = 0;
    switch (name)
    {
        case C_NAME_CONST: case C_NAME_GNU_CONST: case C_NAME_GNU_CONST2: bit = BUSTER_TYPE_CONST; break;
        case C_NAME_VOLATILE: case C_NAME_GNU_VOLATILE: case C_NAME_GNU_VOLATILE2: bit = BUSTER_TYPE_VOLATILE; break;
        case C_NAME_RESTRICT: case C_NAME_GNU_RESTRICT: case C_NAME_GNU_RESTRICT2: bit = BUSTER_TYPE_RESTRICT; break;
        default: break;
    }
    *qualifiers |= bit;
    return bit != 0;
}

static uint32_t check_input_skip_attributes(const Parser* parser, uint32_t token, uint32_t end)
{
    while (check_input_attribute_starts(parser, token, end))
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t open = kind == PARSE_KIND_L_SQUARE ? token : token + 1;
        if (kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ASM || kind == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = open < end ? parser->tree.parents[open] : end;
            token = close > open && close < end ? close + 1 : end;
        }
        else
        {
            token += 1;
        }
    }
    return token;
}

// An enum's fixed underlying type: the parser's specifier `:` after its name, then the type's specifiers and
// qualifiers. Returns the token after them, or `colon` when it starts no base.
static uint32_t check_input_enum_base_end(const Parser* parser, uint32_t tag, uint32_t colon, uint32_t end)
{
    uint32_t position = colon;
    if (parser->tokens.kinds[tag] == PARSE_KIND_ENUM && colon < end && parser->tokens.kinds[colon] == PARSE_KIND_COLON &&
        parser->tree.roles[colon] == PARSE_ROLE_SPECIFIER)
    {
        // The base's specifiers, and GNU attributes among them (`enum E : int __attribute__((unused)) { ... }`), which clang
        // takes as the base type's.
        position = colon + 1;
        while (position < end && (parser->tree.roles[position] == PARSE_ROLE_SPECIFIER || parser->tokens.kinds[position] == PARSE_KIND_ATTRIBUTE))
        {
            const uint32_t close = position + 1 < end ? parser->tree.parents[position + 1] : end;
            position = parser->tokens.kinds[position] == PARSE_KIND_ATTRIBUTE && close < end ? close + 1 : position + 1;
        }
    }
    return position;
}

// The token after a tag's name and attributes, and an enum's fixed underlying type: its body's `{`, or what follows.
static uint32_t check_input_tag_body(const Parser* parser, uint32_t token, uint32_t end)
{
    uint32_t position = check_input_skip_attributes(parser, token + 1, end);
    position += position < end && parser->tokens.kinds[position] == PARSE_KIND_IDENTIFIER;
    position = check_input_skip_attributes(parser, position, end);
    return check_input_enum_base_end(parser, token, position, end);
}

// These are the target's implicit typedefs, already classified by the parser. They have no source declaration
// token. Construct their actual ABI types lazily, and let ordinary lexical typedefs shadow them in inner scopes.
static TypeId check_input_predefined_type(CheckInputReader* reader, uint32_t name)
{
    TypeId result = 0;
    for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        if (name == reader->predefined_names[i])
        {
            result = reader->predefined_types[i];
            if (result == 0)
            {
                // A deferred body's table marks the tags of its predefined types, which the replay keeps once.
                TypeChanges* changes = reader->types->changes;
                if (changes != 0)
                {
                    changes->predefined = i + 1;
                }
                if (i == 0 || i == 1)
                {
                    result = i == 0 ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128;
                }
                else if (i == 3)
                {
                    result = type_pointer(reader->types, BUSTER_TYPE_CHAR);
                }
                else if (i == 4)
                {
                    result = type_builtin_va_list(reader->types, reader->builtin_member_names);
                }
                else
                {
                    const TypeId record = type_tag(reader->types, BUSTER_TYPE_KIND_STRUCT, reader->builtin_member_names[5]);
                    const TypeId int_pointer = type_pointer(reader->types, type_qualify(reader->types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
                    const TypeId char_pointer = type_pointer(reader->types, type_qualify(reader->types, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST));
                    TypeMember members[4] = { 0 };
                    const TypeId types[4] = { int_pointer, BUSTER_TYPE_INT, char_pointer, BUSTER_TYPE_LONG };
                    for (uint32_t member = 0; member < 4; member += 1)
                    {
                        members[member].name = reader->builtin_member_names[member + 6];
                        members[member].type = types[member];
                        members[member].bit_width = BUSTER_TYPE_NOT_BIT_FIELD;
                    }
                    const TypeRecordAttributes attributes = { 0 };
                    result = record != 0 && int_pointer != 0 && char_pointer != 0 && type_complete_record(reader->types, record, members, 4, attributes) ? record : 0;
                }
                if (changes != 0)
                {
                    changes->predefined = 0;
                }
                reader->predefined_types[i] = check_input_type_result(reader, result);
                reader->predefined_positions[i] = reader->position;
                TypeId tag_type = result != 0 && type_kind_is_array(type_kind(reader->types, result)) ? type_element(reader->types, result) : result;
                if (tag_type != 0 && type_kind(reader->types, tag_type) == BUSTER_TYPE_KIND_STRUCT)
                {
                    reader->tag_completions[reader->types->types.index[type_index(tag_type)]] = 0;
                }
            }
        }
    }
    return result;
}

static bool check_input_specifier_combination(const TypeTable* types, TypeId base, uint32_t longs, uint32_t shorts,
                                              uint32_t signedness, bool integer_seen)
{
    bool valid = longs <= 2 && !(longs && shorts);
    if (base == BUSTER_TYPE_CHAR || base == BUSTER_TYPE_INT128 || type_kind(types, base) == BUSTER_TYPE_KIND_BIT_INT)
    {
        valid &= !longs && !shorts && !integer_seen;
    }
    else if (base == BUSTER_TYPE_DOUBLE)
    {
        valid &= longs <= 1 && !shorts && !signedness && !integer_seen;
    }
    else if (base != 0)
    {
        valid &= !longs && !shorts && !signedness && !integer_seen;
    }
    return valid;
}

// An enum's fixed underlying type (C23's `enum E : T`, which clang takes in every language mode): from `colon` + 1 to
// `end`, the parser's specifiers there, which are type keywords, qualifiers, typedef names and _Atomic. It is an
// integer type of any width (bool too, but not a _BitInt or an enum), its qualifiers dropped. Returns 0 for none,
// reported at its first token as clang reports it. (check_input_specifiers, which reads a tag, isn't called here.)
static TypeId check_input_enum_underlying(CheckInputReader* reader, uint32_t colon, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t qualifiers = 0;
    uint32_t longs = 0;
    uint32_t shorts = 0;
    uint32_t signedness = 0;
    bool integer_seen = false;
    TypeId base = 0;
    bool valid = colon + 1 < end;
    for (uint32_t i = colon + 1; valid && i < end; i += 1)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        if (check_input_qualifier(name, &qualifiers))
        {
            // const and volatile dropped, as clang drops them (with a warning); restrict, which needs a pointer, refused.
            valid = (qualifiers & BUSTER_TYPE_RESTRICT) == 0;
        }
        else if (kind == PARSE_KIND_ATTRIBUTE)
        {
            // GNU attributes among the specifiers, which clang ignores in a base (with a warning), but one that would make
            // the type a vector's, which isn't an integer type. Parsed, not evaluated: a vector's size doesn't decide that,
            // and an argument's evaluation would come back here through a tag of its own.
            uint32_t cursor = i;
            CheckInputAttributes parsed = { 0 };
            valid = check_input_attributes(reader, &cursor, end, &parsed) && cursor > i;
            for (uint32_t a = 0; valid && a < parsed.count; a += 1)
            {
                valid = parsed.records[a].kind != CHECK_ATTRIBUTE_VECTOR_SIZE && parsed.records[a].kind != CHECK_ATTRIBUTE_EXT_VECTOR_TYPE;
            }
            i = cursor - 1;
        }
        else if (name == C_NAME_LONG)
        {
            longs += 1;
        }
        else if (name == C_NAME_SHORT)
        {
            shorts += 1;
        }
        else if (name == C_NAME_SIGNED || name == C_NAME_GNU_SIGNED || name == C_NAME_GNU_SIGNED2 || name == C_NAME_UNSIGNED)
        {
            valid = signedness == 0 || signedness == (name == C_NAME_UNSIGNED ? 2u : 1u);
            signedness = name == C_NAME_UNSIGNED ? 2 : 1;
        }
        else if (name == C_NAME_INT)
        {
            valid = !integer_seen;
            integer_seen = true;
        }
        else if (kind == PARSE_KIND_TYPE && base == 0)
        {
            base = name == C_NAME_CHAR ? BUSTER_TYPE_CHAR : name == C_NAME_BOOL || name == C_NAME_BOOL_C99 ? BUSTER_TYPE_BOOL :
                   name == C_NAME_INT128 ? BUSTER_TYPE_INT128 : name == C_NAME_FLOAT ? BUSTER_TYPE_FLOAT : name == C_NAME_DOUBLE ? BUSTER_TYPE_DOUBLE : 0;
            valid = base != 0;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && base == 0)
        {
            const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const TypeId predefined = handle == 0 ? check_input_predefined_type(reader, name) : 0;
            valid = predefined != 0 || (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF);
            if (valid)
            {
                const uint32_t declaration = handle != 0 ? reader->bindings.declaration[handle - 1] : BUSTER_CHECK_NONE;
                base = predefined != 0 ? predefined : reader->declared_types[declaration];
                reader->output->declarations[i] = declaration != BUSTER_CHECK_NONE ? declaration : reader->output->declarations[i];
                reader->declared_types[i] = base;
            }
        }
        else
        {
            valid = false;
        }
        valid = valid && check_input_specifier_combination(reader->types, base, longs, shorts, signedness, integer_seen);
    }
    if (valid && base == 0)
    {
        base = shorts ? (signedness == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               longs == 2 ? (signedness == 2 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
               longs == 1 ? (signedness == 2 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                            (signedness == 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT);
    }
    else if (valid && base == BUSTER_TYPE_CHAR)
    {
        valid = !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UCHAR : signedness == 1 ? BUSTER_TYPE_SCHAR : BUSTER_TYPE_CHAR;
    }
    else if (valid && base == BUSTER_TYPE_INT128)
    {
        valid = !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UINT128 : base;
    }
    else if (valid)
    {
        valid = !longs && !shorts && !signedness && !integer_seen;
    }
    const TypeId integer = valid ? type_unqualified(type_strip_aligned(reader->types, base)) : 0;
    valid = valid && type_kind(reader->types, integer) == BUSTER_TYPE_KIND_INTEGER;
    if (!valid)
    {
        check_input_report(reader, colon + 1 < end ? colon + 1 : colon, CHECK_INPUT_DECLARATION);
    }
    for (uint32_t i = colon; valid && i < end; i += 1)
    {
        reader->ignored[i] = true;
    }
    return valid ? integer : 0;
}

// The depth of the scope a declaration at the reader's position belongs to: its block's, or, in a declarator's parameter
// list, the list's, which is a scope of its own (C11 6.2.1p4) a level deeper than the one around it. The list's
// parameters, and the tags and enumeration constants it declares, are declared at it.
static uint32_t check_input_declaration_depth(const CheckInputReader* reader)
{
    return reader->scope_count + reader->prototype_depth;
}

// Tags have their own namespace. A definition or stand-alone forward declaration hides an outer tag; an ordinary
// reference finds the visible tag. The declaration token remains the original tag-name token when a definition
// completes an earlier forward declaration. Body completion is handled separately by the iterative tag reader.
// An enum with a fixed underlying type is complete at its base, so that its body's enumerators read it (`sizeof`
// it), and it may be declared without its body only by a declaration of it alone (`enum E : T;`). Its redeclarations
// in its scope keep its base (a declaration alone, `enum E;`, too; a reference, `enum E x;`, has none), and only one
// lists its enumerators; one without a base can't list them, nor one with a base those of an enum without one, as clang
// has it. A parameter list's definition of a tag the list defined already is no error in clang, only a warning
// (Sema::ActOnTag's warn_redefinition_in_param_list): it makes a tag of its own, which its name doesn't name, so that
// the name goes on naming the first (`int f(struct S { int x; } a, struct S { int y; } b)`).
static TypeId check_input_tag_identity(CheckInputReader* reader, uint32_t token, uint32_t end, bool definition)
{
    const Parser* parser = reader->parser;
    TypeId type = reader->tag_types[token];
    if (type == 0)
    {
        uint32_t i = check_input_skip_attributes(parser, token + 1, end);
        bool valid = i < end;
        const uint8_t kind = parser->tokens.kinds[token] == PARSE_KIND_ENUM ? BUSTER_TYPE_KIND_ENUM :
                             parser->tokens.names[token] == C_NAME_UNION ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT;
        const uint32_t name = i < end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[i] : 0;
        const uint32_t declaration = name != 0 ? i++ : token;
        // Attributes after the name are the declaration's, not the tag's: before a `;` they make it a reference, as clang
        // has it (`struct S __attribute__((unused));` names the visible S), where a `;` right after the name declares one.
        const uint32_t after_name = i;
        i = check_input_skip_attributes(parser, i, end);
        const bool attributed = i != after_name;
        const uint32_t colon = i;
        i = check_input_enum_base_end(parser, token, colon, end);
        const bool fixed = i != colon;
        const bool body = i < end && parser->tokens.kinds[i] == PARSE_KIND_L_BRACE;
        const bool forward = !attributed && i < end && parser->tokens.kinds[i] == PARSE_KIND_SEMI;
        const uint32_t previous = name != 0 ? c_bindings_lookup(&reader->bindings, C_BINDING_TAG, name) : 0;
        const uint32_t depth = check_input_declaration_depth(reader);
        const bool same_scope = previous != 0 && reader->bindings.depth[previous - 1] == depth;
        valid &= name != 0 || body;
        valid &= !body || definition;
        if (valid && fixed && !body && !forward)
        {
            // `enum E : T x;`, which clang reports at the `:`.
            check_input_report(reader, colon, CHECK_INPUT_DECLARATION);
            valid = false;
        }
        const TypeId underlying = valid && fixed ? check_input_enum_underlying(reader, colon, i) : 0;
        valid &= !fixed || underlying != 0;
        // A parameter list's second definition of a tag of the same kind (above): one with its enumerators listed, or
        // complete, for an enum without a fixed underlying type and a record.
        const TypeId defined = same_scope && body && reader->prototype_depth != 0 ? reader->declared_types[reader->bindings.declaration[previous - 1]] : 0;
        const uint32_t defined_tag = defined != 0 && type_kind(reader->types, defined) == kind ? reader->types->types.index[type_index(defined)] : 0;
        const bool redefinition = defined_tag != 0 && (kind == BUSTER_TYPE_KIND_ENUM && reader->enum_fixed[defined_tag] != BUSTER_CHECK_ENUM_NONE ?
                                                       reader->enum_fixed[defined_tag] == BUSTER_CHECK_ENUM_LISTED : type_is_complete(reader->types, defined));
        if (valid && previous != 0 && !redefinition && (same_scope || (!body && !forward)))
        {
            const uint32_t old = reader->bindings.declaration[previous - 1];
            type = reader->declared_types[old];
            const uint32_t tag = type_kind(reader->types, type) == kind ? reader->types->types.index[type_index(type)] : 0;
            const uint8_t state = tag != 0 && kind == BUSTER_TYPE_KIND_ENUM ? reader->enum_fixed[tag] : BUSTER_CHECK_ENUM_NONE;
            valid = type_kind(reader->types, type) == kind &&
                    (state == BUSTER_CHECK_ENUM_NONE ? !fixed && (!body || !type_is_complete(reader->types, type)) :
                                                       (fixed || (!body && !forward)) && (!body || state == BUSTER_CHECK_ENUM_FIXED) &&
                                                       (!fixed || reader->types->types.base[type_index(type)] == underlying));
            if (!valid && type_kind(reader->types, type) == kind && kind == BUSTER_TYPE_KIND_ENUM)
            {
                // A base that differs from the enum's (or that it hadn't), clang reports at the name.
                check_input_report(reader, declaration, CHECK_INPUT_DECLARATION);
            }
            reader->output->declarations[declaration] = old;
            reader->declared_types[declaration] = type;
        }
        else if (valid)
        {
            type = check_input_type_result(reader, type_tag(reader->types, kind, redefinition ? 0 : name));
            valid = type != 0;
            if (valid)
            {
                const uint32_t tag = reader->types->types.index[type_index(type)];
                reader->tag_completions[tag] = BUSTER_CHECK_NONE;
                // A deferred body's tag takes an index that an earlier body on its lane may have had (c_check_deferred.c).
                reader->enum_fixed[tag] = BUSTER_CHECK_ENUM_NONE;
                if (fixed)
                {
                    // Complete here: at the `{` or the `;` after the base.
                    const CheckInputAttributes* attributes = reader->output->attributes[token];
                    valid = type_enum_complete(reader->types, type, underlying, attributes != 0 ? attributes->alignment_log2 : 0);
                    reader->output->resource |= !valid && reader->types->last_error == TYPE_ERROR_FULL;
                    reader->tag_completions[tag] = i;
                    reader->enum_fixed[tag] = BUSTER_CHECK_ENUM_FIXED;
                }
            }
            reader->declared_types[declaration] = type;
            reader->declaration_kinds[declaration] = C_BINDING_TAG_KIND;
            reader->output->declarations[declaration] = declaration;
            if (valid && name != 0 && !redefinition && c_bindings_declare(&reader->bindings, C_BINDING_TAG, name, C_BINDING_TAG_KIND, declaration, i, depth) == 0)
            {
                reader->output->resource = true;
                valid = false;
            }
        }
        if (valid)
        {
            reader->ignored[token] = true;
            reader->ignored[declaration] = true;
            reader->tag_types[token] = type;
            reader->tag_ends[token] = i;
        }
        else
        {
            type = 0;
        }
    }
    return type;
}

// Attribute operands may contain tag references themselves. Identity creation has no evaluator dependency;
// declaration-time expressions schedule their attribute work explicitly and use that acyclic entry point.
static TypeId check_input_tag(CheckInputReader* reader, uint32_t token, uint32_t end, bool definition)
{
    TypeId type = reader->tag_types[token];
    if (type == 0)
    {
        uint32_t cursor = token + 1;
        CheckInputAttributes* attributes = 0;
        bool valid = check_input_read_attributes(reader, &cursor, end, &attributes);
        cursor += cursor < end && reader->parser->tokens.kinds[cursor] == PARSE_KIND_IDENTIFIER;
        valid = valid && check_input_read_attributes(reader, &cursor, end, &attributes);
        // Before the identity, which completes an enum with a fixed underlying type and its alignment attribute.
        reader->output->attributes[token] = valid ? attributes : 0;
        type = valid ? check_input_tag_identity(reader, token, end, definition) : 0;
    }
    return type;
}

// Specifiers are retained in source order because their inherited base type precedes declarators semantically,
// although the parser's ordinary postorder puts their nodes after those declarators.
static TypeId check_input_specifiers(CheckInputReader* reader, uint32_t* cursor, uint32_t end, bool* is_typedef, uint32_t* storage_class, bool* is_inline)
{
    const Parser* parser = reader->parser;
    const uint32_t first = *cursor;
    CheckInputSpecifierJob* job = reader->specifier_jobs[first];
    uint32_t i = *cursor;
    uint32_t qualifiers = 0;
    uint32_t longs = 0;
    uint32_t shorts = 0;
    uint32_t signedness = 0;
    uint32_t storage = 0;
    uint32_t restrict_token = BUSTER_CHECK_NONE;
    TypeId base = 0;
    uint8_t abi_origin = 0;
    bool integer_seen = false;
    bool valid = true;
    bool saw = false;
    bool reading = true;
    bool thread_local = false;
    bool complex = false;
    bool atomic = false;
    bool reported = false;
    // Whether a _BitInt base is this list's own keyword's, which the list's signedness makes signed or unsigned below;
    // a typedef name's or a typeof's keeps the signedness it has.
    bool bit_int = false;
    CheckInputAttributes* attributes = 0;
    *is_typedef = false;
    *is_inline = false;
    if (job != 0 && job->ready)
    {
        i = job->keyword;
        qualifiers = job->qualifiers;
        longs = job->longs;
        shorts = job->shorts;
        signedness = job->signedness;
        storage = job->storage;
        restrict_token = job->restrict_token;
        base = job->base;
        abi_origin = job->abi_origin;
        attributes = job->attributes;
        integer_seen = job->integer_seen;
        saw = job->saw;
        thread_local = job->thread_local;
        complex = job->complex;
        atomic = job->atomic;
        *is_typedef = job->is_typedef;
        *is_inline = job->is_inline;
    }
    while (reading && i < end)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        TypeId selected = 0;
        bool consumed = true;
        if (check_input_attribute_starts(parser, i, end))
        {
            valid &= check_input_read_attributes(reader, &i, end, &attributes);
            if (!valid)
            {
                break;
            }
            saw = true;
            continue;
        }
        if (check_input_qualifier(name, &qualifiers))
        {
            restrict_token = restrict_token == BUSTER_CHECK_NONE && (qualifiers & BUSTER_TYPE_RESTRICT) != 0 ? i : restrict_token;
        }
        else if (kind == PARSE_KIND_EXTENSION)
        {
            // __extension__ is a keyword in every language mode, as clang has it.
        }
        else if (kind == PARSE_KIND_TYPEDEF)
        {
            *is_typedef = true;
        }
        else if (kind == PARSE_KIND_STORAGE)
        {
            const bool tls = name == C_NAME_THREAD_LOCAL || name == C_NAME_THREAD_LOCAL_C11 || name == C_NAME_GNU_THREAD;
            thread_local |= tls;
            *is_inline |= name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2;
            valid &= tls || name == C_NAME_STATIC || name == C_NAME_EXTERN || name == C_NAME_REGISTER || name == C_NAME_AUTO || name == C_NAME_INLINE ||
                     name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2;
            if (name == C_NAME_STATIC || name == C_NAME_EXTERN || name == C_NAME_REGISTER || name == C_NAME_AUTO)
            {
                valid &= storage == 0 || storage == name;
                storage = name;
            }
        }
        else if (name == C_NAME_COMPLEX_C99 || name == C_NAME_GNU_COMPLEX || name == C_NAME_GNU_COMPLEX2)
        {
            complex = true;
        }
        else if (kind == PARSE_KIND_ATOMIC && (i + 1 >= end || parser->tokens.kinds[i + 1] != PARSE_KIND_L_PAREN))
        {
            atomic = true;
        }
        else if ((kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC || name == C_NAME_BITINT || name == C_NAME_EXTINT) &&
                 i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[i + 1];
            bool region = false;
            for (uint32_t token = i + 2; kind == PARSE_KIND_TYPEOF && reader->specifier_owner == first &&
                 token < close && close < end && !region; token += 1)
            {
                region = parser->tree.roles[token] == PARSE_ROLE_OPEN_BLOCK;
            }
            if (region && job == 0)
            {
                // A type query may contain ordinary declarations. Suspend this specifier frame while the
                // ordered source loop elaborates that region, then resume from the saved state at its close.
                job = arena_push(reader->scratch, sizeof(*job), _Alignof(CheckInputSpecifierJob));
                reader->output->resource |= job == 0;
                if (job != 0)
                {
                    *job = (CheckInputSpecifierJob)
                    {
                        .first = first, .keyword = i, .closing = close, .qualifiers = qualifiers,
                        .longs = longs, .shorts = shorts, .signedness = signedness, .storage = storage, .restrict_token = restrict_token,
                        .base = base, .attributes = attributes, .abi_origin = abi_origin, .integer_seen = integer_seen, .saw = saw,
                        .thread_local = thread_local, .complex = complex, .atomic = atomic,
                        .is_typedef = *is_typedef, .is_inline = *is_inline,
                    };
                    reader->specifier_jobs[first] = job;
                    reader->specifier_finishes[close] = job;
                    check_input_visit(reader, close, BUSTER_CHECK_RESOLVE_QUERY);
                    reader->ignored[i] = true;
                    reader->ignored[i + 1] = true;
                    reader->specifier_pending = true;
                    reader->specifier_query_depth += 1;
                }
                return 0;
            }
            reader->constant_abi_origin = 0;
            if (job != 0 && job->ready && job->keyword == i)
            {
                selected = job->result;
            }
            else if (name == C_NAME_BITINT || name == C_NAME_EXTINT)
            {
                // A _BitInt's width alone, unsigned until the declaration's own signedness applies below: read as a
                // whole type name, `_BitInt(1)` is a signed one-bit _BitInt, which C refuses, before an `unsigned` counts.
                int64_t width = 0;
                const bool sized = close > i + 2 && close < end && check_input_constant(reader, i + 2, close, &width) && width > 0 && width <= UINT32_MAX;
                selected = sized ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, true)) : 0;
            }
            else
            {
                selected = close > i + 1 && close < end ? check_input_constant_type(reader, i, close + 1) : 0;
            }
            abi_origin = job != 0 && job->ready && job->keyword == i ? job->result_abi_origin : reader->constant_abi_origin;
            valid &= selected != 0 && base == 0;
            base = selected;
            bit_int = name == C_NAME_BITINT || name == C_NAME_EXTINT;
            reader->declared_types[i] = selected;
            for (uint32_t token = i; selected != 0 && token <= close; token += 1)
            {
                reader->ignored[token] |= job == 0 || job->keyword != i || token <= i + 1 || token == close;
            }
            if (selected != 0 && kind == PARSE_KIND_TYPEOF)
            {
                valid &= check_input_vla_query(reader, i + 2, close, selected);
            }
            i = selected != 0 ? close : i;
        }
        else if (name == C_NAME_LONG)
        {
            longs += 1;
        }
        else if (name == C_NAME_SHORT)
        {
            shorts = 1;
        }
        else if (name == C_NAME_SIGNED || name == C_NAME_GNU_SIGNED || name == C_NAME_GNU_SIGNED2 || name == C_NAME_UNSIGNED)
        {
            valid &= signedness == 0 || signedness == (name == C_NAME_UNSIGNED ? 2u : 1u);
            signedness = name == C_NAME_UNSIGNED ? 2 : 1;
        }
        else if (name == C_NAME_INT)
        {
            valid &= !integer_seen;
            integer_seen = true;
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
        {
            // A tag that reports its own error (an enum's base, at the token clang names) needs no report here.
            const uint32_t reports = reader->output->report_count;
            selected = check_input_tag(reader, i, end, false);
            reported = reader->output->report_count != reports;
            valid &= selected != 0 && base == 0;
            base = selected;
            i = selected != 0 ? reader->tag_ends[i] - 1 : i;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            switch (name)
            {
                case C_NAME_CHAR: selected = BUSTER_TYPE_CHAR; break;
                case C_NAME_VOID: selected = BUSTER_TYPE_VOID; break;
                case C_NAME_BOOL: case C_NAME_BOOL_C99: selected = BUSTER_TYPE_BOOL; break;
                case C_NAME_FLOAT: selected = BUSTER_TYPE_FLOAT; break;
                case C_NAME_DOUBLE: selected = BUSTER_TYPE_DOUBLE; break;
                case C_NAME_INT128: selected = BUSTER_TYPE_INT128; break;
                case C_NAME_FLOAT16: selected = BUSTER_TYPE_FLOAT16; break;
                case C_NAME_FP16: selected = BUSTER_TYPE_HALF; break;
                case C_NAME_BF16: selected = BUSTER_TYPE_BF16; break;
                case C_NAME_FLOAT128: selected = BUSTER_TYPE_FLOAT128; break;
                default: valid = false; break;
            }
            valid &= base == 0;
            base = selected;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_SPECIFIER && base == 0 && !integer_seen && !longs && !shorts && !signedness)
        {
            const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const TypeId predefined = handle == 0 ? check_input_predefined_type(reader, name) : 0;
            valid &= predefined != 0 || (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF);
            if (predefined != 0)
            {
                base = predefined;
                reader->declared_types[i] = base;
            }
            if (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                base = reader->declared_types[declaration];
                abi_origin = reader->output->explicit_abis[declaration];
                reader->output->declarations[i] = declaration;
                reader->declared_types[i] = base;
                reader->output->explicit_abis[i] = abi_origin;
            }
        }
        else
        {
            consumed = false;
            reading = false;
        }
        if (consumed)
        {
            valid &= check_input_specifier_combination(reader->types, base, longs, shorts, signedness, integer_seen);
            if (!valid)
            {
                if (!reported)
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
                break;
            }
            reader->ignored[i] = true;
            saw = true;
            i += 1;
        }
    }
    valid &= saw && (base != 0 || integer_seen || longs || shorts || signedness || complex) && longs <= 2 && shorts <= 1 && !(longs && shorts);
    valid &= !*is_typedef || (storage == 0 && !*is_inline && !thread_local);
    valid &= !thread_local || (storage == 0 || storage == C_NAME_STATIC || storage == C_NAME_EXTERN);
    if (base == 0)
    {
        base = complex && !integer_seen && !longs && !shorts && !signedness ? BUSTER_TYPE_DOUBLE :
               shorts ? (signedness == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               longs == 2 ? (signedness == 2 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
               longs == 1 ? (signedness == 2 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                            (signedness == 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT);
    }
    else if (base == BUSTER_TYPE_CHAR)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UCHAR : signedness == 1 ? BUSTER_TYPE_SCHAR : BUSTER_TYPE_CHAR;
    }
    else if (base == BUSTER_TYPE_DOUBLE)
    {
        valid &= longs <= 1 && !shorts && !signedness && !integer_seen;
        base = longs ? BUSTER_TYPE_LONG_DOUBLE : base;
    }
    else if (base == BUSTER_TYPE_INT128)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UINT128 : base;
    }
    else if (type_kind(reader->types, base) == BUSTER_TYPE_KIND_BIT_INT && bit_int)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)reader->types->types.extent[type_index(base)], signedness == 2)) : 0;
    }
    else
    {
        valid &= !longs && !shorts && !signedness && !integer_seen;
    }
    if ((qualifiers & BUSTER_TYPE_RESTRICT) != 0 && valid)
    {
        const bool pointer = type_kind(reader->types, base) == BUSTER_TYPE_KIND_POINTER;
        valid = pointer && type_kind(reader->types, reader->types->types.base[type_index(base)]) != BUSTER_TYPE_KIND_FUNCTION;
        if (!valid)
        {
            // clang reports restrict on a type other than a pointer to an object at the qualifier.
            check_input_report(reader, restrict_token, CHECK_INPUT_DECLARATION);
        }
    }
    if (complex && valid)
    {
        const uint32_t code = type_index(base);
        valid = code < 64 && ((BUSTER_TYPE_COMPLEX_SPELLABLE >> code) & 1) != 0;
        base = valid ? check_input_type_result(reader, type_complex(reader->types, base)) : 0;
    }
    if (atomic && valid)
    {
        base = check_input_type_result(reader, type_atomic(reader->types, base));
    }
    if (attributes != 0 && valid)
    {
        base = check_input_attribute_type(reader, attributes, base, false);
    }
    reader->specifier_attributes = attributes;
    reader->specifier_abi_origin = abi_origin;
    reader->specifier_thread_local = thread_local;
    *cursor = i;
    *storage_class = storage;
    return valid ? type_qualify(reader->types, base, qualifiers) : 0;
}

static bool check_input_spelling(CheckInputReader* reader, uint32_t token, const uint8_t** bytes, uint32_t* length)
{
    const uint32_t source = reader->parser->tokens.sources[token];
    PPTokens* output = &reader->pp->output;
    const uint8_t kind = output->kinds[source];
    const uint64_t capacity = kind == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)output->values[source] - output->locations[source] + 1;
    uint8_t* buffer = arena_push(reader->scratch, capacity, 1);
    if (buffer != 0)
    {
        *length = pp_spelling(reader->pp, kind, output->values[source], output->locations[source], buffer, bytes);
    }
    reader->output->resource |= buffer == 0;
    return buffer != 0;
}

#include "c_check_function_name_input.c"
#include "c_check_pragma_input.c"
#include "c_check_pack_input.c"
#include "c_check_fp_input.c"
#include "c_check_loop_input.c"

static const CheckInitString* check_input_string(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    if (check_input_function_name_token(parser, token))
    {
        return check_input_function_name(reader, token);
    }
    const CheckInitString* result = 0;
    if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_STRING)
    {
        const uint32_t cached = reader->string_ids[token];
        if (cached != BUSTER_CHECK_NONE)
        {
            result = &reader->output->strings[cached];
        }
        else
        {
            const uint64_t mark = reader->scratch->length;
            uint32_t end = token + 1;
            while (end < parser->tokens.count && parser->tokens.kinds[end] == PARSE_KIND_STRING)
            {
                end += 1;
            }
            const uint32_t count = end - token;
            CheckStringPart* parts = arena_push_array(reader->scratch, count, sizeof(CheckStringPart), 8);
            bool valid = parts != 0;
            reader->output->resource |= !valid;
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                valid = check_input_spelling(reader, token + i, &parts[i].bytes, &parts[i].length);
                reader->output->resource |= !valid;
            }
            CheckStringLiteral literal = { 0 };
            if (valid)
            {
                literal = check_literal_strings(parts, count, parser->language.standard >= 2023, reader->arena);
                reader->output->resource |= literal.resource;
                valid = literal.valid;
            }
            const TypeId type = valid ? check_input_type_result(reader, type_array(reader->types, literal.element_type, literal.unit_count)) : 0;
            valid &= type != 0;
            if (valid)
            {
                const uint32_t index = reader->output->string_count++;
                reader->output->strings[index] = (CheckInitString)
                {
                    .object = token + 1,
                    .token = token,
                    .type = type,
                    .bytes = literal.bytes,
                    .byte_count = literal.byte_count,
                };
                for (uint32_t i = token; i < end; i += 1)
                {
                    reader->string_ids[i] = index;
                    reader->ignored[i] |= i != token;
                }
                reader->declared_types[token] = type;
                result = &reader->output->strings[index];
            }
            else
            {
                check_input_report(reader, token, CHECK_INPUT_LITERAL);
            }
            arena_pop_to(reader->scratch, mark);
        }
    }
    return result;
}

// A literal token's type and value, into `result`, written into the caller's rather than returned (#510: see
// check_literal_integer).
static void check_input_literal(CheckInputReader* reader, uint32_t token, CheckLiteral* result)
{
    const uint64_t mark = reader->scratch->length;
    const uint8_t* bytes = 0;
    uint32_t length = 0;
    *result = (CheckLiteral){ 0 };
    const uint32_t name = reader->parser->tokens.names[token];
    if (name == C_NAME_TRUE || name == C_NAME_FALSE)
    {
        result->type = BUSTER_TYPE_BOOL;
        result->integer = name == C_NAME_TRUE;
        result->valid = true;
    }
    else if (name == C_NAME_NULLPTR || name == C_NAME_GNU_NULLPTR)
    {
        result->type = BUSTER_TYPE_NULLPTR;
        result->valid = true;
    }
    else if (name == 0 && check_input_spelling(reader, token, &bytes, &length))
    {
        if (reader->parser->tokens.kinds[token] == PARSE_KIND_CHARACTER)
        {
            *result = check_literal_character(bytes, length, reader->pp->options.language.standard >= 2023, reader->types->char_unsigned);
        }
        else
        {
            check_literal_number(bytes, length, reader->scratch, result);
        }
    }
    reader->output->resource |= result->resource;
    arena_pop_to(reader->scratch, mark);
}

static CheckOp check_input_binary(uint8_t kind);

// A subtree is contiguous in token order and postorder. Its first token's own subtree starts the same postorder
// run; extending that run to the requested length locates its root. The size and last-token checks reject slices
// that cut a subtree or contain several roots. This shares the reader's retained traversal and subtree sizes;
// neither selections nor constant evaluation needs another tree or a range scan.
static uint32_t check_input_range_root(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t root = BUSTER_CHECK_NONE;
    if (first < end && end <= parser->tokens.count)
    {
        const uint32_t position = reader->postorder_positions[first];
        const uint32_t size = reader->subtree_sizes[position];
        const uint32_t length = end - first;
        if (size != 0 && size <= length && size <= position + 1 && length - size < parser->tokens.count - position)
        {
            const uint32_t after = position + length - size;
            const uint32_t candidate = reader->postorder[after];
            if (reader->subtree_sizes[after] == length && candidate >= first && candidate < end && reader->ends[candidate] == end - 1)
            {
                root = candidate;
            }
        }
    }
    return root;
}

// Declaration-time ICEs and type queries use the same semantic actions as expression graphs. The helper uses
// bounded explicit stacks for casts, size/alignment queries, selections, operators and supported builtin calls.
// Resolve names in source order before visiting the expression's postorder, preserving binding-event ordering.
#include "c_check_builtin_evidence.c"
#include "c_check_generic.c"
#include "c_check_constant.c"
#include "c_check_select.c"
#include "c_check_vla_input.c"
#include "c_check_overload_input.c"

static bool check_input_read_attributes(CheckInputReader* reader, uint32_t* cursor, uint32_t end, CheckInputAttributes** attributes)
{
    if (!check_input_attribute_starts(reader->parser, *cursor, end))
    {
        return true;
    }
    CheckInputAttributes parsed = { 0 };
    bool valid = check_input_attributes(reader, cursor, end, &parsed);
    for (uint32_t i = 0; valid && i < parsed.count; i += 1)
    {
        CheckInputAttribute* attribute = &parsed.records[i];
        if (!attribute->resolved && attribute->schema == CHECK_ATTRIBUTE_INTEGER)
        {
            int64_t value = 0;
            const bool type_argument = attribute->kind == CHECK_ATTRIBUTE_ALIGNAS &&
                                       check_constant_type_start(reader->parser, attribute->argument_first);
            bool evaluated = !type_argument && check_input_constant_ice(reader, attribute->argument_first, attribute->argument_end, &value);
            if (type_argument)
            {
                const TypeId type = check_input_constant_type(reader, attribute->argument_first, attribute->argument_end);
                evaluated = type != 0 && type_is_complete(reader->types, type);
                value = evaluated ? (int64_t)1 << type_alignment_log2(reader->types, type) : 0;
            }
            valid = evaluated && check_input_attribute_value(reader, &parsed, i, value);
        }
        else if (!attribute->resolved && (attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST || attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS))
        {
            for (uint32_t argument = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS; valid && argument < attribute->argument_count; argument += 1)
            {
                uint32_t first;
                uint32_t last;
                int64_t value;
                valid = check_input_attribute_argument(attribute, argument, &first, &last) &&
                        check_input_constant_ice(reader, first, last, &value) && check_input_attribute_argument_value(reader, &parsed, i, argument, value);
            }
        }
    }
    CheckInputAttributes* combined = valid ? arena_push(reader->arena, sizeof(CheckInputAttributes), 8) : 0;
    reader->output->resource |= valid && combined == 0;
    valid &= combined != 0;
    if (valid)
    {
        const CheckInputAttributes empty = { 0 };
        valid = check_input_attribute_merge(reader, *attributes != 0 ? *attributes : &empty, &parsed, combined);
        *attributes = combined;
    }
    return valid;
}

static CheckInputAttributes* check_input_merge_attributes(CheckInputReader* reader, CheckInputAttributes* common, CheckInputAttributes* suffix)
{
    CheckInputAttributes* result = suffix != 0 ? suffix : common;
    if (common != 0 && suffix != 0)
    {
        result = arena_push(reader->arena, sizeof(CheckInputAttributes), 8);
        reader->output->resource |= result == 0;
        if (result != 0 && !check_input_attribute_merge(reader, common, suffix, result))
        {
            result = 0;
        }
    }
    return result;
}

// A transparent-union annotation takes effect where its complete declaration ends. Keeping that activation
// separate from nominal type flags prevents a later typedef attribute from changing an earlier call's rules.
static void check_input_transparent_union(CheckInputReader* reader, TypeId type, CheckInputAttributes* attributes, uint32_t point)
{
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION && !attribute->ignored)
        {
            const TypeTransparentUnionResult marked = type_mark_transparent_union(reader->types, type);
            attribute->ignored = marked.status != TYPE_TRANSPARENT_UNION_APPLIED;
            // A block's typedef may make a file-scope union transparent for what follows: source order's.
            check_input_lane_refuse(reader, reader->lane && !attribute->ignored, "a body makes a union transparent");
            if (!attribute->ignored)
            {
                const uint32_t tag = reader->types->types.index[type_index(type_strip_aligned(reader->types, type))];
                reader->output->resource |= reader->transparent_unions == 0;
                if (reader->transparent_unions != 0 && reader->transparent_unions[tag] == 0)
                {
                    reader->transparent_unions[tag] = point;
                }
            }
        }
    }
}

typedef struct CheckDeclaratorPart CheckDeclaratorPart;
struct CheckDeclaratorPart
{
    uint32_t next;
    uint32_t token;
    uint32_t qualifiers;
    uint32_t parameter_first;
    uint32_t parameter_last;
    uint32_t parameter_count;
    uint32_t names;
    uint32_t last_name;
    // A function's: the tags and enumeration constants its list declared besides its parameters (names).
    uint32_t declarations;
    uint32_t last_declaration;
    uint64_t extent;
    uint8_t kind;
    uint8_t flags;
};

typedef struct CheckDeclaratorParameter CheckDeclaratorParameter;
struct CheckDeclaratorParameter
{
    TypeId type;
    uint32_t next;
};

typedef struct CheckDeclaratorFrame CheckDeclaratorFrame;
struct CheckDeclaratorFrame
{
    TypeId base;
    uint32_t cursor;
    uint32_t end;
    uint32_t name;
    uint32_t first;
    uint32_t last;
    uint32_t pointers;
    uint32_t function;
    uint32_t binding_mark;
    uint32_t storage;
    uint32_t array_qualifiers;
    CheckInputAttributes* attributes;
    CheckInputAttributes* common_attributes;
    uint8_t abi_origin;
    uint8_t state;
    bool group;
    bool parameter;
    bool parameter_context;
    bool star_array;
};

static CheckDeclaratorFrame check_input_declarator_frame(TypeId base, uint32_t first, uint32_t end, bool group, bool parameter, uint32_t storage, uint8_t abi_origin)
{
    return (CheckDeclaratorFrame)
    {
        .base = base,
        .cursor = first,
        .end = end,
        .name = BUSTER_CHECK_NONE,
        .first = BUSTER_CHECK_NONE,
        .last = BUSTER_CHECK_NONE,
        .pointers = BUSTER_CHECK_NONE,
        .function = BUSTER_CHECK_NONE,
        .storage = storage,
        .abi_origin = abi_origin,
        .group = group,
        .parameter = parameter,
        .parameter_context = parameter,
    };
}

// The struct, union or enum keyword among the specifiers from `first`, before `end`, whose body follows it: a definition
// that the reader hasn't read yet, which a declaration's, a member's or a parameter's reader reads before the specifiers
// (check_input_tag_definition), and BUSTER_CHECK_NONE when there's none.
static uint32_t check_input_specifier_definition(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t tag = first;
    while (tag < end && (parser->tree.roles[tag] == PARSE_ROLE_SPECIFIER || parser->tokens.kinds[tag] == PARSE_KIND_QUALIFIER ||
           parser->tokens.kinds[tag] == PARSE_KIND_EXTENSION || check_input_attribute_starts(parser, tag, end)))
    {
        tag = check_input_attribute_starts(parser, tag, end) ? check_input_skip_attributes(parser, tag, end) : tag + 1;
    }
    const bool keyword = tag < end && (parser->tokens.kinds[tag] == PARSE_KIND_STRUCT || parser->tokens.kinds[tag] == PARSE_KIND_ENUM);
    const uint32_t body = keyword ? check_input_tag_body(parser, tag, end) : end;
    return body < end && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE && reader->tag_types[tag] == 0 ? tag : BUSTER_CHECK_NONE;
}

// A declarator being read (check_input_declarator), with its frames, parts and parameters in scratch after itself:
// what the run has made so far, and what it waits for when it stops before its end.
typedef struct CheckDeclaratorRun CheckDeclaratorRun;
struct CheckDeclaratorRun
{
    CheckDeclaratorFrame* frames;
    CheckDeclaratorPart* parts;
    CheckDeclaratorParameter* parameters;
    // An array bound's evaluation that waits for a definition in it (check_input_vla_bound), or 0.
    CheckConstantInput* bound;
    CheckInputAttributes* attributes;
    TypeId result;
    // Scratch's length before the run.
    uint64_t mark;
    uint32_t stop;
    uint32_t binding_mark;
    // The reader's prototype depth at the run's start, which its end gives back.
    uint32_t prototype_depth;
    uint32_t depth;
    uint32_t part_count;
    uint32_t parameter_count;
    // What a complete run gives check_input_declarator's caller: where it ended, its name, a definition's parameters and
    // the tags and enumeration constants its parameter list declared besides them.
    uint32_t cursor;
    uint32_t name;
    uint32_t names;
    uint32_t declarations;
    // The struct, union or enum keyword of a definition the run waits for, which ends before definition_end (a
    // parameter list's or an array bound's closing token), or BUSTER_CHECK_NONE.
    uint32_t definition;
    uint32_t definition_end;
    uint8_t abi_origin;
    bool overloadable;
    bool valid;
};

// Declarator operators are collected outwards from the identifier, then applied in reverse to the base type.
// Group and prototype-parameter frames share one explicit stack, so neither parentheses nor nested callbacks use
// the C call stack. A prototype's bindings are restored at its closing parenthesis; its own names, and the tags and
// enumeration constants it declared (CheckDeclaratorRun.declarations: not a nested prototype's, which its own
// parenthesis ended), are retained only for a possible function body, whose scope is theirs too (C11 6.2.1p4).
// Parameter-array qualifiers belong to the adjusted pointer object; the function signature contains its unqualified
// adjusted type. A prototype-only [*] remains distinct until body activation.
//
// A declarator runs in steps, since a struct, union or enum may be defined in it, in a parameter's specifiers or in an
// array bound (`int f(struct S { int x; } s, int a[sizeof(enum E { K })])`): a step stops at each such definition
// (CheckDeclaratorRun.definition) for its caller to read it, in source order, before the next step goes on from there.
// A definition reads its members' declarators in runs of their own, so this needs neither recursion nor a cycle in
// the call graph: check_input_declarator reads definitions with check_input_tag_definition, whose own stack of frames
// reads those its members' declarators stop at. A complete run's prototypes restored their bindings at their closing
// parentheses, and what it declared outside them, a tag that an array bound names or defines, is its scope's, as it is
// in C (`int a[sizeof(struct V { int x; })]; struct V v;`); a run that fails restores everything it declared.
//
// The run begins (check_input_declarator_begin) after the caller sets reader->specifier_abi_origin and
// reader->declarator_overloadable for it, which it takes. Returns 0, with the output's resource set, when there's no
// room for it in scratch.
static CheckDeclaratorRun* check_input_declarator_begin(CheckInputReader* reader, TypeId base, uint32_t cursor, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t stop = cursor;
    while (stop < end)
    {
        const uint8_t kind = parser->tokens.kinds[stop];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[stop];
            if (close <= stop || close >= end)
            {
                break;
            }
            stop = close + 1;
        }
        else if (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE ||
                 kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_COLON)
        {
            break;
        }
        else
        {
            stop += 1;
        }
    }
    const uint32_t capacity = stop - cursor + 1;
    const uint64_t mark = reader->scratch->length;
    CheckDeclaratorRun* run = arena_push(reader->scratch, sizeof(CheckDeclaratorRun), _Alignof(CheckDeclaratorRun));
    CheckDeclaratorFrame* frames = run != 0 ? arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorFrame), 8) : 0;
    CheckDeclaratorPart* parts = frames != 0 ? arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorPart), 8) : 0;
    CheckDeclaratorParameter* parameters = parts != 0 ? arena_push_array(reader->scratch, capacity, sizeof(CheckDeclaratorParameter), 4) : 0;
    reader->output->resource |= parameters == 0;
    if (run != 0)
    {
        *run = (CheckDeclaratorRun)
        {
            .frames = frames,
            .parts = parts,
            .parameters = parameters,
            .mark = mark,
            .stop = stop,
            .binding_mark = c_bindings_mark(&reader->bindings),
            .prototype_depth = reader->prototype_depth,
            .cursor = cursor,
            .name = BUSTER_CHECK_NONE,
            .names = BUSTER_CHECK_NONE,
            .declarations = BUSTER_CHECK_NONE,
            .definition = BUSTER_CHECK_NONE,
            .definition_end = BUSTER_CHECK_NONE,
            .overloadable = reader->declarator_overloadable,
            .valid = parameters != 0 && base != 0,
        };
        if (run->valid)
        {
            frames[run->depth++] = check_input_declarator_frame(base, cursor, stop, false, false, 0, reader->specifier_abi_origin);
        }
    }
    reader->declarator_overloadable = false;
    return run;
}

// Reads the run's declarator on, until it ends or fails, or stops at a definition (CheckDeclaratorRun.definition) for
// its caller to read before the next step, which goes on from there.
static void check_input_declarator_step(CheckInputReader* reader, CheckDeclaratorRun* run)
{
    const Parser* parser = reader->parser;
    CheckDeclaratorFrame* frames = run->frames;
    CheckDeclaratorPart* parts = run->parts;
    CheckDeclaratorParameter* parameters = run->parameters;
    bool valid = run->valid;
    uint32_t depth = run->depth;
    uint32_t part_count = run->part_count;
    uint32_t parameter_count = run->parameter_count;
    run->definition = BUSTER_CHECK_NONE;
    while (valid && depth != 0 && run->definition == BUSTER_CHECK_NONE)
    {
        CheckDeclaratorFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        if (frame->state == 0)
        {
            while (i < frame->end && check_input_attribute_starts(parser, i, frame->end))
            {
                valid &= check_input_read_attributes(reader, &i, frame->end, &frame->attributes);
                if (!valid)
                {
                    break;
                }
            }
            while (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_STAR)
            {
                const uint32_t token = i;
                reader->ignored[i++] = true;
                uint32_t qualifiers = 0;
                while (valid && i < frame->end)
                {
                    if (check_input_qualifier(parser->tokens.names[i], &qualifiers))
                    {
                        reader->ignored[i++] = true;
                    }
                    else if (check_input_attribute_starts(parser, i, frame->end))
                    {
                        valid = check_input_read_attributes(reader, &i, frame->end, &frame->attributes);
                    }
                    else
                    {
                        break;
                    }
                }
                parts[part_count] = (CheckDeclaratorPart)
                {
                    .next = frame->pointers,
                    .token = token,
                    .qualifiers = qualifiers,
                    .kind = BUSTER_TYPE_KIND_POINTER,
                };
                frame->pointers = part_count++;
            }
            frame->state = 1;
            if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_OPERAND)
            {
                frame->name = i;
                reader->ignored[i++] = true;
            }
            else if (i < frame->end && (parser->tree.roles[i] == PARSE_ROLE_OPEN_GROUP ||
                     (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN && parser->tree.parents[i] < frame->end &&
                      check_input_skip_attributes(parser, i + 1, parser->tree.parents[i]) < parser->tree.parents[i] &&
                      parser->tokens.kinds[check_input_skip_attributes(parser, i + 1, parser->tree.parents[i])] == PARSE_KIND_STAR)))
            {
                const uint32_t close = parser->tree.parents[i];
                valid = close > i && close < frame->end;
                if (valid)
                {
                    reader->ignored[i] = true;
                    reader->ignored[close] = true;
                    frame->cursor = close + 1;
                    frames[depth++] = check_input_declarator_frame(frame->base, i + 1, close, true, false, 0, frame->abi_origin);
                    frames[depth - 1].parameter_context = frame->parameter_context;
                }
            }
            frame->cursor = depth != 0 && frame == &frames[depth - 1] ? i : frame->cursor;
        }
        else if (frame->state == 2)
        {
            CheckDeclaratorPart* function = &parts[frame->function];
            const uint32_t close = parser->tree.parents[function->token];
            if (i == close)
            {
                // The tags and enumeration constants the list declared, in its order: those still active, since a
                // nested prototype's ended at its own parenthesis. A struct's, union's or enum's nested in a parameter's
                // definition are the list's too, as C has no scope of a record's.
                for (uint32_t active = frame->binding_mark; active < reader->bindings.active_count; active += 1)
                {
                    const uint32_t event = reader->bindings.active[active];
                    if (reader->bindings.kind[event] == C_BINDING_TAG_KIND || reader->bindings.kind[event] == C_BINDING_ENUMERATOR)
                    {
                        const uint32_t declaration = reader->bindings.declaration[event];
                        reader->parameter_declaration_next[declaration] = BUSTER_CHECK_NONE;
                        if (function->last_declaration != BUSTER_CHECK_NONE)
                        {
                            reader->parameter_declaration_next[function->last_declaration] = declaration;
                        }
                        else
                        {
                            function->declarations = declaration;
                        }
                        function->last_declaration = declaration;
                    }
                }
                valid = c_bindings_restore(&reader->bindings, frame->binding_mark, close);
                reader->output->resource |= !valid;
                reader->prototype_depth -= 1;
                for (uint32_t parameter = function->names; parameter != BUSTER_CHECK_NONE; parameter = reader->parameter_next[parameter])
                {
                    reader->parameter_seen[parser->tokens.names[parameter]] = reader->parameter_previous[parameter];
                }
                reader->ignored[close] = true;
                frame->cursor = close + 1;
                frame->state = 1;
            }
            else if (i < close && parser->tokens.kinds[i] == PARSE_KIND_ELLIPSIS)
            {
                valid = i + 1 == close;
                function->flags |= BUSTER_TYPE_FLAG_VARIADIC;
                reader->ignored[i] = true;
                frame->cursor = i + 1;
            }
            else if (i < close)
            {
                // A parameter whose specifiers define a struct, union or enum: the caller reads the definition first, as a
                // declaration's own is read before its specifiers (check_input_declaration), then the parameter from here.
                const uint32_t definition = check_input_specifier_definition(reader, i, close);
                if (definition != BUSTER_CHECK_NONE)
                {
                    run->definition = definition;
                    run->definition_end = close;
                }
                else
                {
                    bool is_typedef;
                    bool is_inline;
                    uint32_t storage;
                    const TypeId parameter = check_input_specifiers(reader, &i, close, &is_typedef, &storage, &is_inline);
                    valid = parameter != 0 && !is_typedef && !is_inline && (storage == 0 || storage == C_NAME_REGISTER);
                    if (valid)
                    {
                        frames[depth++] = check_input_declarator_frame(parameter, i, close, false, true, storage, reader->specifier_abi_origin);
                        frames[depth - 1].common_attributes = reader->specifier_attributes;
                        valid &= !reader->specifier_thread_local;
                    }
                }
            }
            else
            {
                valid = false;
            }
        }
        else if (i < frame->end && check_input_attribute_starts(parser, i, frame->end))
        {
            valid = check_input_read_attributes(reader, &i, frame->end, &frame->attributes);
            frame->cursor = i;
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            uint32_t bound = i + 1;
            uint32_t qualifiers = 0;
            bool minimum = false;
            while (valid && bound < close)
            {
                if (parser->tokens.names[bound] == C_NAME_STATIC)
                {
                    valid &= !minimum;
                    minimum = true;
                }
                else if (!check_input_qualifier(parser->tokens.names[bound], &qualifiers))
                {
                    break;
                }
                bound += 1;
            }
            valid &= (qualifiers == 0 && !minimum) || (frame->parameter_context && frame->first == BUSTER_CHECK_NONE);
            const bool star = bound + 1 == close && parser->tokens.kinds[bound] == PARSE_KIND_STAR;
            valid &= !star || (frame->parameter_context && !minimum);
            valid &= !minimum || bound < close;
            CheckDeclaratorPart part =
            {
                .next = BUSTER_CHECK_NONE,
                .token = i,
                .kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
            };
            if (valid)
            {
                // A bound that defines a struct, union or enum stops the run at each definition, with its evaluation
                // kept for the next step, which reads the bound again from here.
                frame->star_array |= star;
                valid = check_input_vla_bound(reader, i, bound, close, frame->parameter_context, star, &run->bound, &run->definition, &part.kind, &part.extent);
                run->definition_end = run->definition != BUSTER_CHECK_NONE ? close : run->definition_end;
            }
            if (valid && run->definition == BUSTER_CHECK_NONE)
            {
                frame->array_qualifiers |= qualifiers;
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = part;
                frame->last = part_count++;
                frame->cursor = close + 1;
            }
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_PAREN &&
                 (parser->tree.roles[i] == PARSE_ROLE_OPEN_FUNCTION || parser->tree.roles[i] == PARSE_ROLE_OPEN_CALL))
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            if (valid)
            {
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = (CheckDeclaratorPart)
                {
                    .next = BUSTER_CHECK_NONE,
                    .token = i,
                    .parameter_first = BUSTER_CHECK_NONE,
                    .parameter_last = BUSTER_CHECK_NONE,
                    .names = BUSTER_CHECK_NONE,
                    .last_name = BUSTER_CHECK_NONE,
                    .declarations = BUSTER_CHECK_NONE,
                    .last_declaration = BUSTER_CHECK_NONE,
                    .kind = BUSTER_TYPE_KIND_FUNCTION,
                    .flags = i + 1 != close || parser->language.standard >= 2023 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0,
                };
                frame->function = part_count;
                frame->last = part_count++;
                frame->binding_mark = c_bindings_mark(&reader->bindings);
                frame->state = 2;
                frame->cursor = i + 1;
                reader->ignored[i] = true;
                // The list's scope, which its closing parenthesis ends.
                reader->prototype_depth += 1;
            }
        }
        else
        {
            valid = frame->parameter ? i <= frame->end && (i == frame->end || parser->tokens.kinds[i] == PARSE_KIND_COMMA) : i == frame->end;
            if (frame->last != BUSTER_CHECK_NONE)
            {
                parts[frame->last].next = frame->pointers;
            }
            else
            {
                frame->first = frame->pointers;
            }
            if (frame->pointers != BUSTER_CHECK_NONE)
            {
                frame->last = frame->pointers;
                while (parts[frame->last].next != BUSTER_CHECK_NONE)
                {
                    frame->last = parts[frame->last].next;
                }
            }
            if (valid && frame->group)
            {
                CheckDeclaratorFrame* parent = &frames[depth - 2];
                parent->name = frame->name;
                parent->first = frame->first;
                parent->last = frame->last;
                parent->attributes = check_input_merge_attributes(reader, parent->attributes, frame->attributes);
                parent->array_qualifiers |= frame->array_qualifiers;
                parent->star_array |= frame->star_array;
                depth -= 1;
            }
            else if (valid)
            {
                const uint32_t first = frame->first;
                uint32_t reverse = BUSTER_CHECK_NONE;
                for (uint32_t part = first; part != BUSTER_CHECK_NONE;)
                {
                    const uint32_t next = parts[part].next;
                    parts[part].next = reverse;
                    reverse = part;
                    part = next;
                }
                TypeId type = frame->base;
                uint8_t abi_origin = frame->abi_origin;
                for (uint32_t p = reverse; type != 0 && p != BUSTER_CHECK_NONE; p = parts[p].next)
                {
                    const CheckDeclaratorPart* part = &parts[p];
                    if (part->kind == BUSTER_TYPE_KIND_POINTER)
                    {
                        const bool restrict_function = (part->qualifiers & BUSTER_TYPE_RESTRICT) != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                        if (restrict_function)
                        {
                            // clang reports a restrict pointer to a function at the pointer's *.
                            check_input_report(reader, part->token, CHECK_INPUT_DECLARATION);
                        }
                        type = restrict_function ? 0 : check_input_type_result(reader, type_pointer(reader->types, type));
                        type = type != 0 ? type_qualify(reader->types, type, part->qualifiers) : 0;
                    }
                    else if (part->kind == BUSTER_TYPE_KIND_FUNCTION)
                    {
                        abi_origin = 0;
                        const bool bare = part->parameter_count == 0 && (part->flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 && parser->language.standard < 2023;
                        const bool overloadable = run->overloadable || (check_input_overload_flags(frame->attributes) & BUSTER_CHECK_OVERLOADABLE) != 0;
                        valid &= !bare || (!frame->parameter && p == first && overloadable);
                        const uint64_t parameter_mark = reader->scratch->length;
                        TypeId* parameter_types = arena_push_array(reader->scratch, (uint64_t)part->parameter_count + 1, sizeof(TypeId), 4);
                        reader->output->resource |= parameter_types == 0;
                        uint32_t count = 0;
                        for (uint32_t parameter = part->parameter_first; parameter_types != 0 && parameter != BUSTER_CHECK_NONE; parameter = parameters[parameter].next)
                        {
                            parameter_types[count++] = parameters[parameter].type;
                        }
                        type = valid && parameter_types != 0 ? check_input_type_result(reader, type_function(reader->types, type, parameter_types, count, part->flags)) : 0;
                        arena_pop_to(reader->scratch, parameter_mark);
                    }
                    else
                    {
                        type = check_input_vla_type(reader, part->token, type, frame->name, frame->parameter_context, part->kind, part->extent);
                    }
                }
                if (type != 0 && frame->attributes != 0)
                {
                    type = check_input_attribute_type(reader, frame->attributes, type, false);
                }
                if (type != 0)
                {
                    type = check_input_declarator_abi(reader, frame->common_attributes, type, &abi_origin);
                    type = check_input_declarator_abi(reader, frame->attributes, type, &abi_origin);
                }
                valid = type != 0;
                valid &= !frame->parameter || ((frame->common_attributes == 0 || !frame->common_attributes->has_alignas) &&
                         (frame->attributes == 0 || !frame->attributes->has_alignas));
                valid &= !frame->parameter || ((check_input_overload_flags(frame->common_attributes) |
                          check_input_overload_flags(frame->attributes)) & BUSTER_CHECK_OVERLOADABLE) == 0;
                if (valid && frame->parameter)
                {
                    CheckDeclaratorFrame* parent = &frames[depth - 2];
                    CheckDeclaratorPart* function = &parts[parent->function];
                    const bool only_void = type == BUSTER_TYPE_VOID && frame->name == BUSTER_CHECK_NONE && function->parameter_count == 0 && i == frame->end;
                    if (!only_void)
                    {
                        const uint32_t qualifiers = type_qualifiers(type);
                        const uint8_t kind = type_kind(reader->types, type);
                        const TypeId adjusted = check_input_type_result(reader, type_adjust_parameter(reader->types, type));
                        valid = adjusted != 0 && type_kind(reader->types, adjusted) != BUSTER_TYPE_KIND_VOID;
                        if (valid)
                        {
                            parameters[parameter_count] = (CheckDeclaratorParameter)
                            {
                                .type = adjusted,
                                .next = BUSTER_CHECK_NONE,
                            };
                            if (function->parameter_last != BUSTER_CHECK_NONE)
                            {
                                parameters[function->parameter_last].next = parameter_count;
                            }
                            else
                            {
                                function->parameter_first = parameter_count;
                            }
                            function->parameter_last = parameter_count++;
                            function->parameter_count += 1;
                            if (frame->name != BUSTER_CHECK_NONE)
                            {
                                const uint32_t token = frame->name;
                                const uint32_t identifier = parser->tokens.names[token];
                                // Another parameter's name, or an enumeration constant's that the list declared, which
                                // clang reports at the parameter (`int f(enum E { K } e, int K)`).
                                const uint32_t visible = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier);
                                if (reader->parameter_seen[identifier] == function->token + 1 ||
                                    (visible != 0 && reader->bindings.depth[visible - 1] == check_input_declaration_depth(reader)))
                                {
                                    check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
                                    valid = false;
                                }
                                reader->parameter_previous[token] = reader->parameter_seen[identifier];
                                reader->parameter_seen[identifier] = function->token + 1;
                                reader->declared_types[token] = type_qualify(reader->types, adjusted, type_kind_is_array(kind) ? frame->array_qualifiers : qualifiers);
                                reader->parameter_shapes[token] = (type_kind_is_array(kind) ? 1 : kind == BUSTER_TYPE_KIND_FUNCTION ? 4 : 0) |
                                                                  (frame->star_array ? 2 : 0);
                                reader->declaration_kinds[token] = C_BINDING_OBJECT;
                                reader->registers[token] = frame->storage == C_NAME_REGISTER;
                                reader->objects[token] = token + 1;
                                reader->output->declarations[token] = token;
                                reader->output->explicit_abis[token] = abi_origin;
                                reader->output->attributes[token] = check_input_merge_attributes(reader, frame->common_attributes, frame->attributes);
                                if (reader->output->attributes[token] != 0)
                                {
                                    valid &= check_input_attribute_function(reader, reader->output->attributes[token], adjusted);
                                }
                                reader->parameter_next[token] = BUSTER_CHECK_NONE;
                                if (function->last_name != BUSTER_CHECK_NONE)
                                {
                                    reader->parameter_next[function->last_name] = token;
                                }
                                else
                                {
                                    function->names = token;
                                }
                                function->last_name = token;
                                if (c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, identifier, C_BINDING_OBJECT, token, i, check_input_declaration_depth(reader)) == 0)
                                {
                                    reader->output->resource = true;
                                    valid = false;
                                }
                            }
                        }
                    }
                    parent->cursor = i;
                    if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                    {
                        reader->ignored[i] = true;
                        parent->cursor = i + 1;
                        valid &= i + 1 < frame->end;
                    }
                    depth -= 1;
                }
                else if (valid)
                {
                    const bool function = first != BUSTER_CHECK_NONE && parts[first].kind == BUSTER_TYPE_KIND_FUNCTION;
                    run->result = type;
                    run->cursor = i;
                    run->name = frame->name;
                    run->names = function ? parts[first].names : BUSTER_CHECK_NONE;
                    run->attributes = frame->attributes;
                    run->declarations = function ? parts[first].declarations : BUSTER_CHECK_NONE;
                    run->abi_origin = abi_origin;
                    depth -= 1;
                }
            }
        }
    }
    run->valid = valid;
    run->depth = depth;
    run->part_count = part_count;
    run->parameter_count = parameter_count;
}

// The run's type, 0 when it failed, and as a complete run, where it ended (`cursor`), its name and a definition's
// parameters (`names`), its attributes, the tags and enumeration constants its parameter list declared and its ABI's
// origin (reader->declarator_attributes, reader->declarator_declarations and reader->declarator_abi_origin). A run
// that failed restores what it declared, and the run's memory is taken back.
static TypeId check_input_declarator_end(CheckInputReader* reader, CheckDeclaratorRun* run, uint32_t* cursor, uint32_t* name, uint32_t* names)
{
    const bool complete = run->valid && run->depth == 0;
    if (!complete && c_bindings_mark(&reader->bindings) != run->binding_mark && !c_bindings_restore(&reader->bindings, run->binding_mark, run->stop))
    {
        reader->output->resource = true;
    }
    reader->prototype_depth = run->prototype_depth;
    *cursor = complete ? run->cursor : *cursor;
    *name = complete ? run->name : BUSTER_CHECK_NONE;
    *names = complete ? run->names : BUSTER_CHECK_NONE;
    reader->declarator_attributes = complete ? run->attributes : 0;
    reader->declarator_declarations = complete ? run->declarations : BUSTER_CHECK_NONE;
    reader->declarator_abi_origin = complete ? run->abi_origin : 0;
    const TypeId result = complete ? run->result : 0;
    arena_pop_to(reader->scratch, run->mark);
    return result;
}

typedef struct CheckTagFrame CheckTagFrame;
struct CheckTagFrame
{
    TypeId type;
    uint32_t token;
    uint32_t cursor;
    uint32_t close;
    uint32_t first;
    uint32_t last;
    uint32_t count;
    unsigned __int128 enumerator;
    TypeId enumerator_type;
    bool wide_enumerators;
    uint32_t negative_bits;
    uint32_t positive_bits;
    uint8_t pack_log2;
    CheckInputAttributes* attributes;
    CheckConstantInput* enum_job;
    CheckInputAttributes* enumerator_attributes;
    // A record's member declaration being read: its specifiers' type (0 between declarations), their ABI origin and
    // attributes, and its declarator's run while that waits for a frame above to read a definition it stopped at, which
    // starts at member_declarator.
    TypeId member_base;
    CheckInputAttributes* member_attributes;
    CheckDeclaratorRun* member_run;
    uint32_t member_declarator;
    uint8_t member_abi_origin;
};

typedef struct CheckTagMember CheckTagMember;
struct CheckTagMember
{
    TypeMember member;
    uint32_t token;
    uint32_t next;
    uint32_t previous;
};

#include "c_check_record_input.c"
#include "c_check_enum_input.c"

// Pushes the frame that reads the definition at `token`, its struct, union or enum keyword, which ends before `end`: the
// tag's identity, unless a job of the constant evaluator made it already (check_constant_tag_finish), and then its body.
static bool check_input_tag_frame(CheckInputReader* reader, CheckTagFrame* frames, uint32_t* depth, uint32_t capacity, uint32_t token, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t body = check_input_tag_body(parser, token, end);
    const uint32_t close = body < end ? parser->tree.parents[body] : end;
    const TypeId type = check_input_tag(reader, token, end, true);
    const bool valid = type != 0 && body < end && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE && close > body && close < end && *depth < capacity;
    if (valid)
    {
        frames[*depth] = (CheckTagFrame)
        {
            .type = type,
            .token = token,
            .cursor = body + 1,
            .close = close,
            .first = BUSTER_CHECK_NONE,
            .last = BUSTER_CHECK_NONE,
            .pack_log2 = reader->output->pack_log2 != 0 ? reader->output->pack_log2[body] : 0,
            .attributes = reader->output->attributes[token],
        };
        *depth += 1;
        reader->ignored[body] = true;
    }
    return valid;
}

// Complete nested record definitions with explicit frames. Their tags have the enclosing declaration scope;
// members use an independent per-record namespace. Each frame retains a linked slice because nested records may
// append their members between two outer members; only completion packs that slice for the type-table consumer.
// So do the definitions in its members' declarators, each a frame above the member's, whose declarator's run waits in
// its frame (CheckTagFrame.member_run) while that frame reads the definition (check_input_declarator_begin).
static bool check_input_tag_definition(CheckInputReader* reader, uint32_t token, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t prototype_depth = reader->prototype_depth;
    uint32_t open = check_input_tag_body(parser, token, end);
    bool valid = open < end && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE;
    const uint32_t close = valid ? parser->tree.parents[open] : open;
    valid &= close > open && close < end;
    const uint32_t capacity = valid ? close - token + 1 : 1;
    CheckTagFrame* frames = valid ? arena_push_array(reader->scratch, capacity, sizeof(CheckTagFrame), _Alignof(CheckTagFrame)) : 0;
    CheckTagMember* members = valid ? arena_push_array(reader->scratch, capacity, sizeof(CheckTagMember), 8) : 0;
    reader->output->resource |= valid && (frames == 0 || members == 0);
    valid &= frames != 0 && members != 0;
    uint32_t depth = 0;
    uint32_t member_count = 0;
    uint32_t failed_token = token;
    if (valid)
    {
        const TypeId type = check_input_tag(reader, token, end, true);
        valid = type != 0;
        frames[depth++] = (CheckTagFrame)
        {
            .type = type,
            .token = token,
            .cursor = open + 1,
            .close = close,
            .first = BUSTER_CHECK_NONE,
            .last = BUSTER_CHECK_NONE,
            .pack_log2 = reader->output->pack_log2 != 0 ? reader->output->pack_log2[open] : 0,
            .attributes = reader->output->attributes[token],
        };
        reader->ignored[open] = true;
    }
    while (valid && depth != 0)
    {
        CheckTagFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        failed_token = i;
        const uint8_t kind = type_kind(reader->types, frame->type);
        if (i == frame->close)
        {
            uint32_t after = i + 1;
            valid = check_input_read_attributes(reader, &after, end, &frame->attributes);
            reader->output->attributes[frame->token] = frame->attributes;
            for (uint32_t attribute = 0; valid && frame->attributes != 0 && attribute < frame->attributes->count; attribute += 1)
            {
                valid = frame->attributes->records[attribute].kind != CHECK_ATTRIBUTE_FALLTHROUGH &&
                        frame->attributes->records[attribute].kind != CHECK_ATTRIBUTE_OVERLOADABLE;
            }
            if (valid && kind == BUSTER_TYPE_KIND_ENUM)
            {
                valid = check_input_enum_complete(reader, frame, members);
            }
            else if (valid)
            {
                const uint64_t member_mark = reader->scratch->length;
                TypeMember* packed = arena_push_array(reader->scratch, (uint64_t)frame->count + 1, sizeof(TypeMember), 8);
                valid = packed != 0;
                reader->output->resource |= !valid;
                uint32_t written = 0;
                bool anonymous_names = false;
                for (uint32_t member = frame->first; valid && member != BUSTER_CHECK_NONE; member = members[member].next)
                {
                    const TypeMember value = members[member].member;
                    packed[written++] = value;
                    anonymous_names |= value.name == 0 && value.bit_width == BUSTER_TYPE_NOT_BIT_FIELD;
                    if (value.name != 0)
                    {
                        reader->member_seen[value.name] = members[member].previous;
                    }
                    // A flexible array member ends a struct. One that is the struct's only named member (unnamed
                    // bit-fields before it or not) is a GNU extension, which clang takes, and so does this.
                    if (type_kind(reader->types, value.type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
                    {
                        valid = kind == BUSTER_TYPE_KIND_STRUCT && written == frame->count;
                        if (!valid)
                        {
                            failed_token = members[member].token;
                        }
                    }
                }
                uint32_t failed_member = BUSTER_CHECK_NONE;
                // Direct names were checked by member_seen while reading and their previous stamps are restored
                // above. Only an anonymous non-bit-field can add a namespace conflict or an invalid nonrecord.
                valid = valid && (!anonymous_names || check_input_record_names(reader, packed, frame->count, frame->close - frame->token + 1, &failed_member));
                for (uint32_t member = frame->first; !valid && member != BUSTER_CHECK_NONE && failed_member != BUSTER_CHECK_NONE; member = members[member].next)
                {
                    if (failed_member == 0)
                    {
                        failed_token = members[member].token;
                        break;
                    }
                    failed_member -= 1;
                }
                const TypeRecordAttributes attributes =
                {
                    .pack_log2 = frame->pack_log2,
                    .packed = frame->attributes != 0 && frame->attributes->packed,
                    .alignment_log2 = frame->attributes != 0 ? frame->attributes->alignment_log2 : 0,
                };
                valid = valid && type_complete_record(reader->types, frame->type, packed, frame->count, attributes);
                arena_pop_to(reader->scratch, member_mark);
            }
            reader->output->resource |= !valid && reader->types->last_error == TYPE_ERROR_FULL;
            if (valid)
            {
                // An enum with a fixed underlying type was complete at its base already, and stays so from there.
                const uint32_t tag = reader->types->types.index[type_index(frame->type)];
                reader->tag_completions[tag] = reader->enum_fixed[tag] == BUSTER_CHECK_ENUM_NONE ? i + 1 : reader->tag_completions[tag];
                check_input_transparent_union(reader, frame->type, frame->attributes, after);
            }
            reader->ignored[i] = true;
            reader->tag_ends[frame->token] = after;
            depth -= 1;
        }
        else if (i > frame->close)
        {
            valid = false;
        }
        else if (kind != BUSTER_TYPE_KIND_ENUM && parser->tokens.kinds[i] == PARSE_KIND_SEMI)
        {
            reader->ignored[i] = true;
            frame->cursor = i + 1;
        }
        else if (kind == BUSTER_TYPE_KIND_ENUM)
        {
            uint32_t nested = BUSTER_CHECK_NONE;
            valid = check_input_enum_read(reader, frame, members, &member_count, &nested);
            if (valid && nested != BUSTER_CHECK_NONE)
            {
                valid = check_input_tag_frame(reader, frames, &depth, capacity, nested, frame->close);
            }
        }
        else
        {
            // A member declaration: a definition in its specifiers, in a frame of its own, then the specifiers, then its
            // declarators one at a time, each a run that stops at each definition in it (a parameter's or an array
            // bound's) for a frame above to read: this frame keeps the run, which goes on when that frame ends.
            const uint32_t nested = frame->member_base == 0 ? check_input_specifier_definition(reader, i, frame->close) : BUSTER_CHECK_NONE;
            if (nested != BUSTER_CHECK_NONE)
            {
                valid = check_input_tag_frame(reader, frames, &depth, capacity, nested, frame->close);
            }
            else
            {
                if (frame->member_base == 0)
                {
                    bool is_typedef;
                    bool is_inline;
                    uint32_t storage;
                    const TypeId base = check_input_specifiers(reader, &i, frame->close, &is_typedef, &storage, &is_inline);
                    valid = base != 0 && !is_typedef && !is_inline && storage == 0 && !reader->specifier_thread_local;
                    frame->member_base = valid ? base : 0;
                    frame->member_abi_origin = reader->specifier_abi_origin;
                    frame->member_attributes = reader->specifier_attributes;
                }
                if (valid && frame->member_run == 0)
                {
                    frame->member_declarator = i;
                    reader->specifier_abi_origin = frame->member_abi_origin;
                    frame->member_run = check_input_declarator_begin(reader, frame->member_base, i, frame->close);
                    valid = frame->member_run != 0;
                }
                CheckDeclaratorRun* run = valid ? frame->member_run : 0;
                if (run != 0)
                {
                    check_input_declarator_step(reader, run);
                }
                if (run != 0 && run->valid && run->definition != BUSTER_CHECK_NONE)
                {
                    valid = check_input_tag_frame(reader, frames, &depth, capacity, run->definition, run->definition_end);
                }
                else if (run != 0)
                {
                    const uint32_t declarator = frame->member_declarator;
                    CheckInputAttributes* common_attributes = frame->member_attributes;
                    uint32_t name;
                    uint32_t parameters;
                    TypeId type = check_input_declarator_end(reader, run, &i, &name, &parameters);
                    frame->member_run = 0;
                    failed_token = name != BUSTER_CHECK_NONE ? name : declarator;
                    CheckInputAttributes* attributes = check_input_merge_attributes(reader, common_attributes, reader->declarator_attributes);
                    uint8_t abi_origin = reader->declarator_abi_origin;
                    type = check_input_declarator_abi(reader, attributes, type, &abi_origin);
                    if (name != BUSTER_CHECK_NONE)
                    {
                        reader->output->explicit_abis[name] = abi_origin;
                    }
                    valid &= (check_input_overload_flags(attributes) & BUSTER_CHECK_OVERLOADABLE) == 0;
                    uint32_t width = BUSTER_TYPE_NOT_BIT_FIELD;
                    if (i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_COLON)
                    {
                        const uint32_t colon = i++;
                        const uint32_t begin = i;
                        while (i < frame->close && parser->tokens.kinds[i] != PARSE_KIND_COMMA && parser->tokens.kinds[i] != PARSE_KIND_SEMI &&
                               parser->tokens.kinds[i] != PARSE_KIND_ATTRIBUTE)
                        {
                            if (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN)
                            {
                                i = parser->tree.parents[i];
                            }
                            i += 1;
                        }
                        // GNU attributes after the width, the member's, which the parser hangs on the member's end, outside
                        // the width's tree.
                        const bool attributed = i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_ATTRIBUTE;
                        int64_t bits = 0;
                        valid &= check_input_constant(reader, begin, i, &bits) && bits >= 0 && bits <= UINT32_MAX;
                        width = (uint32_t)bits;
                        valid = valid && (!attributed || check_input_read_attributes(reader, &i, frame->close, &attributes));
                        for (uint32_t ignored = colon; ignored < i; ignored += 1)
                        {
                            reader->ignored[ignored] = true;
                        }
                    }
                    // The parser accepts a final member without a semicolon, matching the diagnostic-only
                    // GNU extension. Its closing brace terminates the declarator without consuming the record.
                    valid &= type != 0 && (i == frame->close || (i < frame->close &&
                             (parser->tokens.kinds[i] == PARSE_KIND_COMMA || parser->tokens.kinds[i] == PARSE_KIND_SEMI)));
                    valid &= attributes == 0 || !attributes->has_alignas || width == BUSTER_TYPE_NOT_BIT_FIELD;
                    bool tag_only = false;
                    if (valid && name == BUSTER_CHECK_NONE && width == BUSTER_TYPE_NOT_BIT_FIELD)
                    {
                        const TypeId record = type_strip_aligned(reader->types, type);
                        const uint8_t kind = type_kind(reader->types, record);
                        valid = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
                        const uint32_t tag = valid ? reader->types->types.index[type_index(record)] : 0;
                        tag_only = valid && reader->types->tags.name[tag] != 0;
                        valid &= tag_only || (kind != BUSTER_TYPE_KIND_ENUM && type_is_complete(reader->types, record));
                    }
                    if (valid && !tag_only)
                    {
                        const uint32_t identifier = name != BUSTER_CHECK_NONE ? parser->tokens.names[name] : 0;
                        const TypeMember member =
                        {
                            .name = identifier,
                            .type = type,
                            .bit_width = width,
                            .packed = attributes != 0 && attributes->packed,
                            .alignment_log2 = attributes != 0 ? attributes->alignment_log2 : 0,
                        };
                        bool flexible = false;
                        valid = type_check_member(reader->types, &member, true, &flexible) == TYPE_ERROR_NONE &&
                                (!flexible || kind == BUSTER_TYPE_KIND_STRUCT);
                        if (!valid && !type_is_complete(reader->types, type) && type_kind(reader->types, type_strip_aligned(reader->types, type)) != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY &&
                            type_kind(reader->types, type) != BUSTER_TYPE_KIND_VOID && type_kind(reader->types, type) != BUSTER_TYPE_KIND_FUNCTION)
                        {
                            if (name != BUSTER_CHECK_NONE)
                            {
                                reader->declared_types[name] = type;
                            }
                            check_input_report(reader, failed_token, CHECK_INPUT_INCOMPLETE_MEMBER);
                        }
                        if (valid && identifier != 0 && reader->member_seen[identifier] == frame->token + 1)
                        {
                            check_input_report(reader, name, CHECK_INPUT_REDECLARATION);
                            valid = false;
                        }
                        if (valid)
                        {
                            members[member_count] = (CheckTagMember)
                            {
                                .member = member,
                                .token = failed_token,
                                .next = BUSTER_CHECK_NONE,
                                .previous = reader->member_seen[identifier],
                            };
                            if (identifier != 0)
                            {
                                reader->member_seen[identifier] = frame->token + 1;
                                reader->declared_types[name] = type;
                                reader->output->declarations[name] = name;
                            }
                            if (frame->last != BUSTER_CHECK_NONE)
                            {
                                members[frame->last].next = member_count;
                            }
                            else
                            {
                                frame->first = member_count;
                            }
                            frame->last = member_count++;
                            frame->count += 1;
                        }
                    }
                    // After a comma, the declaration's next declarator.
                    const bool more = valid && i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_COMMA;
                    if (valid && i < frame->close)
                    {
                        reader->ignored[i++] = true;
                    }
                    frame->member_base = more ? frame->member_base : 0;
                    frame->cursor = i;
                }
            }
        }
    }
    // A definition that failed: its frames' runs end, the innermost first, each restoring what it declared, and the
    // prototype lists an enumerator's value left open (check_constant_prototype_open) close with it.
    for (uint32_t level = depth; !valid && level-- > 0;)
    {
        if (frames[level].member_run != 0)
        {
            uint32_t cursor = 0;
            uint32_t name;
            uint32_t parameters;
            frames[level].member_run->valid = false;
            check_input_declarator_end(reader, frames[level].member_run, &cursor, &name, &parameters);
        }
    }
    reader->prototype_depth = valid ? reader->prototype_depth : prototype_depth;
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, failed_token, CHECK_INPUT_DECLARATION);
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

// Reads the declarator from `cursor`, before `end`, for a declaration or a type name, and the definitions in it
// (check_input_declarator_begin), each before the declarator goes on past it: the declarator's type, or 0 when it or a
// definition in it fails, with check_input_declarator_end's results.
static TypeId check_input_declarator(CheckInputReader* reader, TypeId base, uint32_t* cursor, uint32_t end, uint32_t* name, uint32_t* names)
{
    CheckDeclaratorRun* run = check_input_declarator_begin(reader, base, *cursor, end);
    TypeId result = 0;
    if (run != 0)
    {
        check_input_declarator_step(reader, run);
        while (run->valid && run->definition != BUSTER_CHECK_NONE)
        {
            run->valid = check_input_tag_definition(reader, run->definition, run->definition_end);
            if (run->valid)
            {
                check_input_declarator_step(reader, run);
            }
        }
        result = check_input_declarator_end(reader, run, cursor, name, names);
    }
    else
    {
        *name = BUSTER_CHECK_NONE;
        *names = BUSTER_CHECK_NONE;
        reader->declarator_attributes = 0;
        reader->declarator_declarations = BUSTER_CHECK_NONE;
        reader->declarator_abi_origin = 0;
    }
    return result;
}

static void check_input_schedule(CheckInputReader* reader, uint32_t token, uint32_t point, TypeId type, CBindingKind kind)
{
    reader->declared_types[token] = type;
    reader->declaration_kinds[token] = (uint8_t)kind;
    reader->activation_next[token] = reader->activate[point];
    reader->activate[point] = token;
    check_input_visit(reader, point, BUSTER_CHECK_RESOLVE_ACTIVATE);
    reader->ignored[token] = true;
    reader->output->declarations[token] = token;
    reader->objects[token] = token + 1;
    reader->output->declaration_bodies[token] = reader->function_body;
}

// Whether a parameter of main other than the first has clang's type for it: char ** with any qualifiers on the
// parameter itself, or const (and only const) on the pointer it points to or on the char, as clang extends it.
static bool check_input_main_arguments(const TypeTable* types, TypeId type)
{
    const TypeId outer = type_strip_aligned(types, type_unqualified(type));
    bool valid = type_kind(types, outer) == BUSTER_TYPE_KIND_POINTER;
    const TypeId middle = valid ? type_strip_aligned(types, types->types.base[type_index(outer)]) : 0;
    valid = valid && (type_qualifiers(middle) & ~BUSTER_TYPE_CONST) == 0 && type_kind(types, middle) == BUSTER_TYPE_KIND_POINTER;
    const TypeId inner = valid ? type_strip_aligned(types, types->types.base[type_index(middle)]) : 0;
    return valid && (type_qualifiers(inner) & ~BUSTER_TYPE_CONST) == 0 && type_unqualified(inner) == BUSTER_TYPE_CHAR;
}

// The token clang reports a declaration of a hosted program's main at, as Sema::CheckMain checks it, or
// BUSTER_CHECK_NONE: a result other than int outside GNU's modes, at the first type specifier (clang only warns in
// GNU's), and with a prototype more than three parameters, or a first one other than int and others other than char **,
// at the name. Its warnings (static, inline, _Noreturn, variadic, one parameter) don't change the verdict.
static uint32_t check_input_main(CheckInputReader* reader, TypeId type, uint32_t first, uint32_t name)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const TypeId function = type_strip_aligned(types, type);
    uint32_t result = BUSTER_CHECK_NONE;
    if (!parser->language.gnu && type_strip_aligned(types, type_return(types, function)) != BUSTER_TYPE_INT)
    {
        uint32_t token = first;
        uint32_t qualifiers = 0;
        while (token < name && (check_input_attribute_starts(parser, token, name) || parser->tokens.kinds[token] == PARSE_KIND_STORAGE ||
               parser->tokens.kinds[token] == PARSE_KIND_EXTENSION || check_input_qualifier(parser->tokens.names[token], &qualifiers)))
        {
            token = check_input_attribute_starts(parser, token, name) ? check_input_skip_attributes(parser, token, name) : token + 1;
        }
        result = token < name ? token : first;
    }
    if (result == BUSTER_CHECK_NONE && (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
    {
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(types, function, &count);
        bool valid = count <= 3;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            valid = i == 0 ? type_strip_aligned(types, type_unqualified(parameters[i])) == BUSTER_TYPE_INT : check_input_main_arguments(types, parameters[i]);
        }
        result = valid ? result : name;
    }
    return result;
}

// Read a declaration's specifiers and its declarators, using parser group boundaries and subtree ends to pass
// over each initializer. Identifier uses within them are resolved later at their own source positions.
static void check_input_declaration(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    if (reader->resolve_limit < count)
    {
        // A retained, unsupported pragma can alter a declaration's type or layout. Prepare only declarations
        // whose specifiers and declarator finish before it; a function body or initializer is visited later.
        uint32_t cursor = first + (reader->declaration_bases[first] != 0);
        while (cursor < reader->resolve_limit)
        {
            const uint8_t kind = parser->tokens.kinds[cursor];
            if (kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || parser->tree.roles[cursor] == PARSE_ROLE_OPEN_BODY)
            {
                break;
            }
            if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
            {
                const uint32_t closing = parser->tree.parents[cursor];
                if (closing >= reader->resolve_limit)
                {
                    cursor = reader->resolve_limit;
                    break;
                }
                cursor = closing > cursor ? closing : cursor;
            }
            cursor += 1;
        }
        if (cursor >= reader->resolve_limit)
        {
            reader->expression_limit = first < reader->expression_limit ? first : reader->expression_limit;
            check_input_pragma_report(reader);
            return;
        }
    }
    const bool continuation = reader->declaration_bases[first] != 0;
    const CheckInputSpecifierJob* specifier_job = reader->specifier_jobs[first];
    const uint32_t vla_begin = specifier_job != 0 ? specifier_job->vla_begin : reader->output->vla_count;
    const uint32_t definition = continuation ? BUSTER_CHECK_NONE : check_input_specifier_definition(reader, first, count);
    const bool prepared = definition == BUSTER_CHECK_NONE || check_input_tag_definition(reader, definition, count);
    uint32_t i = first + continuation;
    bool is_typedef = (reader->declaration_flags[first] & 1) != 0;
    bool is_inline = (reader->declaration_flags[first] & 2) != 0;
    uint32_t storage = reader->declaration_storage[first];
    reader->specifier_owner = first;
    reader->specifier_pending = false;
    const TypeId base = !prepared ? 0 : continuation ? reader->declaration_bases[first] : check_input_specifiers(reader, &i, count, &is_typedef, &storage, &is_inline);
    reader->specifier_owner = BUSTER_CHECK_NONE;
    if (reader->specifier_pending)
    {
        reader->specifier_jobs[first]->vla_begin = vla_begin;
        reader->specifier_pending = false;
        return;
    }
    CheckInputAttributes* common_attributes = continuation ? reader->attribute_bases[first] : reader->specifier_attributes;
    const uint8_t base_abi = continuation ? reader->declaration_abis[first] : reader->specifier_abi_origin;
    const bool thread_local = continuation ? (reader->declaration_flags[first] & 4) != 0 : reader->specifier_thread_local;
    const bool is_static = storage == C_NAME_STATIC;
    bool valid = base != 0;
    const bool tag_only = valid && i < count && parser->tokens.kinds[i] == PARSE_KIND_SEMI && (type_kind(reader->types, base) == BUSTER_TYPE_KIND_STRUCT ||
                          type_kind(reader->types, base) == BUSTER_TYPE_KIND_UNION || type_kind(reader->types, base) == BUSTER_TYPE_KIND_ENUM);
    valid &= !tag_only || (!is_typedef && storage == 0 && !is_inline);
    bool more = valid && !tag_only;
    while (more && i < count)
    {
        uint32_t name;
        uint32_t parameters = BUSTER_CHECK_NONE;
        reader->specifier_abi_origin = base_abi;
        reader->declarator_overloadable = (check_input_overload_flags(common_attributes) & BUSTER_CHECK_OVERLOADABLE) != 0;
        TypeId type = check_input_declarator(reader, base, &i, count, &name, &parameters);
        reader->declarator_overloadable = false;
        const uint32_t parameter_declarations = reader->declarator_declarations;
        CheckInputAttributes* attributes = check_input_merge_attributes(reader, common_attributes, reader->declarator_attributes);
        valid &= (common_attributes == 0 && reader->declarator_attributes == 0) || attributes != 0;
        uint8_t abi_origin = reader->declarator_abi_origin;
        type = check_input_declarator_abi(reader, attributes, type, &abi_origin);
        if (is_typedef && type != 0 && attributes != 0 && attributes->alignment_log2 != 0)
        {
            type = check_input_type_result(reader, type_aligned(reader->types, type, attributes->alignment_log2 - 1));
        }
        valid &= type != 0 && name != BUSTER_CHECK_NONE && i < count;
        if (valid && attributes != 0)
        {
            valid = check_input_attribute_function(reader, attributes, type);
            valid &= (check_input_overload_flags(attributes) & BUSTER_CHECK_OVERLOADABLE) == 0 ||
                     (!is_typedef && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION);
            valid &= !attributes->has_alignas || (!is_typedef && storage != C_NAME_REGISTER);
            valid &= attributes->required_alignment_log2 == 0 || (!is_typedef && type_kind(reader->types, type) != BUSTER_TYPE_KIND_FUNCTION &&
                     attributes->required_alignment_log2 - 1 >= type_alignment_log2(reader->types, type));
            if (valid && is_typedef)
            {
                check_input_transparent_union(reader, type, attributes, i);
            }
        }
        if (valid)
        {
            const uint8_t kind = parser->tokens.kinds[i];
            const bool function = type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
            const bool file = reader->function_result == 0;
            valid &= is_typedef || function || type_kind(reader->types, type) != BUSTER_TYPE_KIND_VOID;
            valid &= !is_inline || function;
            valid &= !thread_local || (!function && (file || is_static || storage == C_NAME_EXTERN));
            valid &= !file || (storage != C_NAME_REGISTER && storage != C_NAME_AUTO);
            valid &= !function || (storage != C_NAME_REGISTER && storage != C_NAME_AUTO && (file || !is_static));
            // A file scope's or an extern's declaration may leave its type to be completed later, but not a definition
            // with an initializer: clang reports that one's incomplete type at its name, wherever it is.
            const bool complete_object = is_typedef || function || ((file || storage == C_NAME_EXTERN) && kind != PARSE_KIND_EQUAL) ||
                                         type_is_complete(reader->types, type) ||
                                         (kind == PARSE_KIND_EQUAL && type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY);
            if (valid && !complete_object)
            {
                reader->declared_types[name] = type;
                check_input_report(reader, name, CHECK_INPUT_INCOMPLETE_OBJECT);
            }
            valid &= complete_object;
            valid &= kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || (function && kind == PARSE_KIND_L_BRACE);
            valid = valid && check_input_attribute_definition(reader, attributes, function && !is_typedef, function && !is_typedef && kind == PARSE_KIND_L_BRACE);
            valid &= file || storage != C_NAME_EXTERN || kind != PARSE_KIND_EQUAL;
            valid &= check_input_vla_declaration(reader, first, vla_begin, name, type, i, storage, is_typedef, kind == PARSE_KIND_EQUAL, thread_local);
            if (valid && function && !is_typedef && !reader->pp->options.freestanding && intern_length(reader->pp->table, parser->tokens.names[name]) == 4 &&
                memcmp(intern_bytes(reader->pp->table, parser->tokens.names[name]), "main", 4) == 0)
            {
                // A hosted program's main, at file scope or in a block, as clang checks it there too.
                const uint32_t failed = check_input_main(reader, type, first, name);
                if (failed != BUSTER_CHECK_NONE)
                {
                    check_input_report(reader, failed, CHECK_INPUT_DECLARATION);
                    valid = false;
                }
            }
            if (valid)
            {
                check_input_schedule(reader, name, i, type, is_typedef ? C_BINDING_TYPEDEF : function ? C_BINDING_FUNCTION : C_BINDING_OBJECT);
                reader->output->explicit_abis[name] = abi_origin;
                reader->defined[name] = kind == PARSE_KIND_EQUAL || (function && kind == PARSE_KIND_L_BRACE);
                reader->defined[name] |= !is_typedef && !function && !file && is_static;
                reader->definition_seen[name] = reader->defined[name];
                reader->output->tentative[name] = !is_typedef && !function && file && storage != C_NAME_EXTERN && kind != PARSE_KIND_EQUAL;
                reader->static_objects[name] = !is_typedef && (is_static || function || storage == C_NAME_EXTERN || reader->function_result == 0);
                reader->registers[name] = storage == C_NAME_REGISTER;
                reader->output->thread_local[name] = thread_local;
                reader->output->attributes[name] = attributes;
                reader->output->function_flags[name] = function ? (is_inline ? CHECK_INPUT_FUNCTION_INLINE : 0) |
                                                      (storage == C_NAME_EXTERN ? CHECK_INPUT_FUNCTION_EXTERN : 0) : 0;
                if (function && !is_typedef && check_input_naked_attribute(attributes) != NULL)
                {
                    reader->output->function_flags[name] |= CHECK_INPUT_FUNCTION_NAKED;
                    reader->naked_functions = true;
                }
                reader->linkage[name] = is_typedef || (!file && !function && storage != C_NAME_EXTERN) ? 0 : is_static ? 1 : 2;
                if ((storage == C_NAME_EXTERN || (function && storage == 0)) && reader->linkage[name] != 0)
                {
                    const uint32_t previous = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[name]);
                    if (previous != 0)
                    {
                        const uint32_t declaration = reader->bindings.declaration[previous - 1];
                        reader->linkage[name] = reader->linkage[declaration] != 0 ? reader->linkage[declaration] : reader->linkage[name];
                    }
                }
                if (function && kind == PARSE_KIND_L_BRACE)
                {
                    reader->parameter_vla_begin = vla_begin;
                    reader->parameter_vla_end = reader->output->vla_count;
                    reader->output->body_declarations[i] = name;
                    reader->function_specifiers[i] = first;
                    reader->parameters[i] = parameters;
                    reader->parameter_declarations[i] = parameter_declarations;
                    reader->query_types[i] = type_return(reader->types, type);
                    // A qualified void (const void, which clang only warns about) is void.
                    valid &= check_action_kind(reader->types, reader->query_types[i]) == BUSTER_TYPE_KIND_VOID || type_is_complete(reader->types, reader->query_types[i]);
                    if (!valid)
                    {
                        check_input_report(reader, name, CHECK_INPUT_DECLARATION);
                    }
                    for (uint32_t parameter = parameters; valid && parameter != BUSTER_CHECK_NONE; parameter = reader->parameter_next[parameter])
                    {
                        valid = type_is_complete(reader->types, reader->declared_types[parameter]);
                        if (!valid)
                        {
                            check_input_report(reader, parameter, CHECK_INPUT_DECLARATION);
                        }
                    }
                }
                if (kind == PARSE_KIND_EQUAL)
                {
                    valid &= !is_typedef && !function;
                    reader->initializers[i] = i + 1 < count && (parser->tokens.kinds[i + 1] == PARSE_KIND_L_BRACE || check_init_aggregate(reader->types, type)) ? 2 : 1;
                    reader->initializer_declarations[i] = name;
                    check_input_visit(reader, i, BUSTER_CHECK_RESOLVE_DECLARATION);
                    reader->query_types[i] = type;
                    reader->static_initializers[i] = is_static || reader->function_result == 0;
                    uint32_t right = reader->first_child[i];
                    while (right != BUSTER_CHECK_NONE && right < i)
                    {
                        right = reader->next_child[right];
                    }
                    valid &= right != BUSTER_CHECK_NONE;
                    reader->output->initializer_begins[name] = i + 1;
                    reader->output->initializer_ends[name] = valid ? reader->ends[right] + 1 : i + 1;
                    i = valid ? reader->ends[right] + 1 : i;
                }
                more = valid && i < count && parser->tokens.kinds[i] == PARSE_KIND_COMMA;
                if (more)
                {
                    // A later declarator sees earlier names and initializer uses in their exact source order.
                    // In particular, its prototype events must not precede the preceding declarator's activation.
                    reader->ignored[i] = true;
                    reader->declaration_bases[i] = base;
                    check_input_visit(reader, i, BUSTER_CHECK_RESOLVE_DECLARATION);
                    // Written only where they change, as most declarations have no storage class, flags, calling
                    // convention or attributes: their columns, zero where nothing is written, then take no pages (#572).
                    const uint8_t flags = (uint8_t)(is_typedef | (is_inline << 1) | (thread_local << 2));
                    if (reader->declaration_storage[i] != storage)
                    {
                        reader->declaration_storage[i] = storage;
                    }
                    if (reader->declaration_flags[i] != flags)
                    {
                        reader->declaration_flags[i] = flags;
                    }
                    if (reader->declaration_abis[i] != base_abi)
                    {
                        reader->declaration_abis[i] = base_abi;
                    }
                    if (reader->attribute_bases[i] != common_attributes)
                    {
                        reader->attribute_bases[i] = common_attributes;
                    }
                    more = false;
                }
            }
        }
        if (!valid)
        {
            more = false;
        }
    }
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, i < count ? i : first, CHECK_INPUT_DECLARATION);
    }
}

// An unannotated redeclaration inherits the linked function's calling convention. Explicit sysv_abi shares
// the canonical default TypeId, so its retained spelling origin must prevent this inheritance just as ms_abi does.
static void check_input_inherit_abi(CheckInputReader* reader, uint32_t token, uint32_t old, bool overload)
{
    const TypeId type = reader->declared_types[token];
    if (reader->declaration_kinds[token] == C_BINDING_FUNCTION && (reader->output->explicit_abis == NULL || reader->output->explicit_abis[token] == 0) &&
        type_function_abi_kind(reader->types, type) == TYPE_FUNCTION_ABI_SYSV)
    {
        const uint32_t name = reader->parser->tokens.names[token];
        TypeId previous = 0;
        if (overload)
        {
            uint64_t signature_hash = 0;
            const bool hashed = check_overload_signature_hash(&reader->overload_index, reader->types, type, reader->scratch, &signature_hash);
            reader->output->resource |= !hashed;
            const uint64_t key = check_overload_index_key(signature_hash, name, UINT32_MAX);
            const uint32_t slot = hashed ? check_overload_index_find(&reader->overload_index, key, name, UINT32_MAX, reader->parser->options.scalar) : UINT32_MAX;
            for (uint32_t linked = check_overload_index_head(&reader->overload_index, slot); previous == 0 && linked != UINT32_MAX; linked = reader->overload_index.linked_next[linked])
            {
                const TypeId candidate = reader->overload_candidates[linked].type;
                const TypeAnswer signature = check_overload_signature(reader->types, candidate, type);
                reader->output->resource |= signature == TYPE_ANSWER_FULL;
                previous = signature == TYPE_ANSWER_YES ? candidate : 0;
            }
        }
        else if (old != 0 && reader->bindings.kind[old - 1] == C_BINDING_FUNCTION)
        {
            previous = reader->declared_types[reader->bindings.declaration[old - 1]];
        }
        else if (reader->linked_declarations[name] != 0 && reader->declaration_kinds[reader->linked_declarations[name] - 1] == C_BINDING_FUNCTION)
        {
            previous = reader->linked_types[name];
        }
        const TypeFunctionABI abi = previous != 0 ? type_function_abi_kind(reader->types, previous) : TYPE_FUNCTION_ABI_SYSV;
        if (abi != TYPE_FUNCTION_ABI_SYSV && abi != TYPE_FUNCTION_ABI_INVALID)
        {
            uint32_t count = 0;
            const TypeId* parameters = type_parameters(reader->types, type, &count);
            reader->declared_types[token] = check_input_type_result(reader, type_function_abi(reader->types, type_return(reader->types, type),
                                                    parameters, count, reader->types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_KEY, abi));
        }
    }
}

static void check_input_bind(CheckInputReader* reader, uint32_t token, uint32_t point)
{
    const uint32_t name = reader->parser->tokens.names[token];
    const uint32_t old = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
    const CBindingKind kind = (CBindingKind)reader->declaration_kinds[token];
    const uint32_t old_declaration = old != 0 ? reader->bindings.declaration[old - 1] : BUSTER_CHECK_NONE;
    const bool linked_visible = old != 0 && reader->linkage[old_declaration] != 0 && reader->linkage[token] != 0;
    const bool overload = check_input_overload_active(reader, token);
    check_input_inherit_abi(reader, token, old, overload);
    if (old == 0 && reader->scope_count == 0)
    {
        const TypeId predefined = check_input_predefined_type(reader, name);
        if (predefined != 0)
        {
            const TypeAnswer compatible = type_same_canonical(reader->types, predefined, reader->declared_types[token]);
            reader->output->resource |= compatible == TYPE_ANSWER_FULL;
            if (kind != C_BINDING_TYPEDEF || compatible != TYPE_ANSWER_YES)
            {
                check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
            }
        }
    }
    if (!overload && old != 0 && (reader->bindings.depth[old - 1] == reader->scope_count || linked_visible))
    {
        const uint32_t previous = reader->bindings.declaration[old - 1];
        const TypeAnswer compatible = kind == C_BINDING_TYPEDEF ? type_same_canonical(reader->types, reader->declared_types[previous], reader->declared_types[token]) :
                                      type_compatible(reader->types, reader->declared_types[previous], reader->declared_types[token]);
        reader->output->resource |= compatible == TYPE_ANSWER_FULL;
        const bool repeatable = kind == C_BINDING_TYPEDEF || kind == C_BINDING_FUNCTION || reader->scope_count == 0 ||
                                (reader->linkage[token] != 0 && reader->linkage[previous] != 0);
        bool parameter_conflict = false;
        if (kind == C_BINDING_FUNCTION && reader->bindings.kind[old - 1] == kind && reader->defined[token])
        {
            uint32_t previous_count = 0;
            type_parameters(reader->types, reader->declared_types[previous], &previous_count);
            parameter_conflict = previous_count != 0 && (reader->types->types.flags[type_index(reader->declared_types[token])] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
        }
        if (!repeatable || compatible != TYPE_ANSWER_YES || reader->bindings.kind[old - 1] != kind || reader->linkage[token] != reader->linkage[previous] ||
            reader->output->thread_local[token] != reader->output->thread_local[previous] || parameter_conflict)
        {
            check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
        }
        else
        {
            reader->objects[token] = reader->objects[previous];
            check_input_naked_inherit(reader, token, previous);
            const TypeId composite = check_input_type_result(reader, type_composite(reader->types, reader->declared_types[previous], reader->declared_types[token]));
            if (composite == 0)
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
            else
            {
                reader->declared_types[token] = composite;
            }
        }
    }
    // A body's declaration with linkage is the file-scope entity's, which later declarations see: source order's.
    check_input_lane_refuse(reader, reader->lane && reader->linkage[token] != 0, "a body declares something with linkage");
    if (!overload && reader->linkage[token] != 0 && reader->output->report_count == 0)
    {
        // Lexical scope restoration does not create a new externally linked entity. Keep its compatibility and
        // object identity for the translation unit, while only a visible declaration contributes to the source
        // type above. A prototype in a closed block must not change a later unprototyped declaration's call type.
        const uint32_t linked = reader->linked_declarations[name];
        TypeId composite = reader->declared_types[token];
        if (linked != 0)
        {
            const uint32_t previous = linked - 1;
            const TypeAnswer compatible = type_compatible(reader->types, reader->linked_types[name], reader->declared_types[token]);
            reader->output->resource |= compatible == TYPE_ANSWER_FULL;
            bool parameter_conflict = false;
            if (kind == C_BINDING_FUNCTION && reader->declaration_kinds[previous] == kind)
            {
                uint32_t previous_count = 0;
                type_parameters(reader->types, reader->linked_types[name], &previous_count);
                parameter_conflict = reader->defined[token] && previous_count != 0 &&
                                     (reader->types->types.flags[type_index(reader->declared_types[token])] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
            }
            if (reader->declaration_kinds[previous] != kind || reader->linkage[previous] != reader->linkage[token] || compatible != TYPE_ANSWER_YES ||
                (reader->defined[token] && reader->definition_seen[previous]) ||
                reader->output->thread_local[token] != reader->output->thread_local[previous] || parameter_conflict)
            {
                check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
            }
            else
            {
                reader->definition_seen[token] |= reader->definition_seen[previous];
                reader->objects[token] = reader->objects[previous];
                check_input_naked_inherit(reader, token, previous);
                composite = check_input_type_result(reader, type_composite(reader->types, reader->linked_types[name], reader->declared_types[token]));
                if (composite == 0)
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
        }
        if (linked == 0 || !reader->defined[linked - 1])
        {
            reader->linked_declarations[name] = token + 1;
        }
        reader->linked_types[name] = composite;
    }
    check_input_overload_declare(reader, token, point, old, overload);
    if (c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, name, kind, token, point, reader->scope_count) == 0)
    {
        reader->output->resource = true;
    }
}

#include "c_check_input_initializer.c"
#include "c_check_control_input.c"
#include "c_check_builtin_input.c"
#include "c_check_asm_input.c"

// A GNU attribute's items are never expressions of the graph: the attribute reader takes them by range. Marking
// them before any declaration is read keeps them out of the diagnostic graph of a declaration that fails before its
// reader reaches them: an aligned(8) taken for a call, or a visibility("hidden") before a struct definition that
// fails, which the declaration defines before it reads its specifiers. The keyword and its parentheses stay as they
// are, for the readers of attribute statements.
static void check_input_attribute_item(CheckInputReader* reader, uint32_t i)
{
    const Parser* parser = reader->parser;
    const uint32_t close = parser->tree.parents[i + 2];
    if (parser->tokens.kinds[i] == PARSE_KIND_ATTRIBUTE && parser->tree.roles[i + 1] == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER &&
        parser->tree.roles[i + 2] == PARSE_ROLE_OPEN_ATTRIBUTE_LIST && close < parser->tokens.count && close > i + 2)
    {
        memset(reader->ignored + i + 3, 1, close - (i + 3));
    }
}

// The vector path visits the `__attribute__` keywords alone (check_input_scan_equal), the scalar path every token.
static void check_input_attribute_items(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count < 2 ? 0 : parser->tokens.count - 2;
    for (uint32_t first = 0; first < count; first += 64)
    {
        uint64_t selected = BUSTER_SCALAR_PATH(parser->options.scalar) ? check_input_scan_all(first, count) : check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_ATTRIBUTE);
        for (; selected != 0; selected &= selected - 1)
        {
            check_input_attribute_item(reader, first + (uint32_t)__builtin_ctzll(selected));
        }
    }
}

// Whether the walk in source order may act at `token` by its syntax alone, as the walk's own tests of its role, kind and
// syntax bits (`syntax`, check_input_syntax_of's) and its successor's give it: the scalar path's mark, the twin of the
// vector path's (check_input_count_tokens). A deferred body's tokens, which the walk skips from the body's opening, need
// none: a deferred body starts at an OPEN_BODY (check_lazy_mask_valid), which is marked. A label's, a goto's and an
// address of a label's names, which the walk ignores, are identifiers, the only operands the parser takes after a goto
// or an && and before a label's colon.
static bool check_input_visit_of(const Parser* parser, uint8_t syntax, uint32_t token)
{
    const uint8_t role = parser->tree.roles[token];
    const uint8_t kind = parser->tokens.kinds[token];
    const bool successor = token + 1 < parser->tokens.count;
    const uint8_t next_role = successor ? parser->tree.roles[token + 1] : PARSE_ROLE_NONE;
    const uint8_t next_kind = successor ? parser->tokens.kinds[token + 1] : PARSE_KIND_END;
    const bool declaration = role == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_TYPEOF ||
                             kind == PARSE_KIND_ATOMIC ||
                             (kind == PARSE_KIND_EXTENSION && (next_role == PARSE_ROLE_SPECIFIER || next_kind == PARSE_KIND_STRUCT || next_kind == PARSE_KIND_ENUM));
    const bool statement = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION ||
                           role == PARSE_ROLE_RETURN || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_OPEN_CASE || kind == PARSE_KIND_GOTO ||
                           kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_ASM || (role == PARSE_ROLE_OPEN_GROUP && next_role == PARSE_ROLE_OPEN_BLOCK);
    const bool query = role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF || role == PARSE_ROLE_OPEN_COMPOUND;
    const bool name = kind == PARSE_KIND_IDENTIFIER && role != PARSE_ROLE_MEMBER && role != PARSE_ROLE_TAG && role != PARSE_ROLE_LEAF_RIGHT;
    return syntax != 0 || declaration || statement || query || name;
}

// The token the walk in source order visits after `token`, at most `end`: the next one on the scalar path, which visits
// every token, and on the vector path the next one marked (CheckInputReader.visits), 64 at a time, or the pragma's
// position the walk stops at (CheckInputReader.resolve_limit), or `end`. The tokens between are those at which the walk
// does nothing: its own writes mark each token at which they make it act (check_input_visit).
static uint32_t check_input_resolve_next(const CheckInputReader* reader, uint32_t token, uint32_t end)
{
    const uint32_t stop = reader->resolve_limit > token && reader->resolve_limit < end ? reader->resolve_limit : end;
    uint32_t next = token + 1;
    if (!reader->parser->options.scalar && next < stop)
    {
        uint64_t marked = check_input_scan_nonzero(reader->visits, next, stop);
        while (marked == 0 && stop - next > 64)
        {
            next += 64;
            marked = check_input_scan_nonzero(reader->visits, next, stop);
        }
        next = marked != 0 ? next + (uint32_t)__builtin_ctzll(marked) : stop;
    }
    return next;
}

// Whether the walk in source order tests at `token` the columns a write marks with `reason` (BUSTER_CHECK_RESOLVE_*), or
// the token's syntax for BUSTER_CHECK_RESOLVE_SYNTAX: at every token on the scalar path, and on the vector path where a
// mark gives the reason, read when the walk tests the columns, after its earlier steps at the token, which may mark it.
static bool check_input_resolve_tests(const CheckInputReader* reader, uint32_t token, uint8_t reason)
{
    return reader->parser->options.scalar || (reader->visits[token] & reason) != 0;
}

// Whether the walk in source order arrives at `token` where it acts for a reason that no mark gives the token
// (CheckInputReader.visits): the vector path would skip the token or the test. The scalar path, which visits every
// token and makes every test, counts such a token (CheckInput.visit_misses), by the walk's tests of the token's syntax
// (check_input_visit_of) and of the columns it writes ahead of itself, each against its mark's reason.
static bool check_input_resolve_missed(const CheckInputReader* reader, uint32_t token)
{
    bool missed = false;
    if (token != reader->resolve_limit)
    {
        const uint8_t marks = reader->visits[token];
        // The unit's count, a whole unit's last token, has no syntax.
        const bool syntax = token < reader->parser->tokens.count && check_input_visit_of(reader->parser, reader->syntax[token], token);
        const bool scope = reader->scope_count != 0 && reader->scope_ends[reader->scope_count - 1] < token;
        const bool finish = reader->initializer_finish_heads[token] != BUSTER_CHECK_NONE;
        const bool declaration = (reader->initializers[token] != 0 && reader->initializer_declarations[token] != BUSTER_CHECK_NONE) ||
                                 reader->declaration_bases[token] != 0;
        const bool query = reader->designator_valid[token] == 2 || reader->specifier_finishes[token] != 0 || reader->selection_type_ends[token] != 0 ||
                           reader->vararg_types[token] != 0 || reader->builtin_type_ends[token] != 0;
        missed = (syntax && (marks & BUSTER_CHECK_RESOLVE_SYNTAX) == 0) || (reader->activate[token] != BUSTER_CHECK_NONE && (marks & BUSTER_CHECK_RESOLVE_ACTIVATE) == 0) ||
                 (scope && (marks & BUSTER_CHECK_RESOLVE_SCOPE) == 0) || (finish && (marks & BUSTER_CHECK_RESOLVE_FINISH) == 0) ||
                 (declaration && (marks & BUSTER_CHECK_RESOLVE_DECLARATION) == 0) || (query && (marks & BUSTER_CHECK_RESOLVE_QUERY) == 0);
    }
    return missed;
}

// The walk from token `begin` to `end`, where it stops (the unit's count, for the whole unit). A lane's walk of a body
// (CheckInputReader.lane) starts at its opening brace, whose activations the top level made, and ends at the token
// after its closing brace, where its scopes end and nothing else of the token is its. The vector path visits only the
// tokens marked (check_input_resolve_next): about a third of sqlite3.c's. At each token it visits, the walk first ends the
// function it has left (function_end), which only a visit reads; and it makes only the tests the token's marks give
// reasons for (check_input_resolve_tests): those of its syntax, and of the columns it writes ahead of itself.
static void check_input_resolve_range(CheckInputReader* reader, uint32_t begin, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t visits = 0;
    for (uint32_t i = begin; i <= end && reader->output->report_count == 0 && !reader->output->resource; i = check_input_resolve_next(reader, i, end))
    {
        visits += 1;
        if (parser->options.scalar)
        {
            reader->output->visit_misses += check_input_resolve_missed(reader, i);
        }
        if (i == reader->resolve_limit)
        {
            check_input_pragma_report(reader);
            break;
        }
        reader->position = i;
        type_changes_at(reader->types, i);
        // A lane's opening and the token after its closing are the top level's, which finished their initializers.
        const bool finishes = !(reader->lane && (i == begin || i == end)) && check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_FINISH);
        for (uint32_t declaration = finishes ? reader->initializer_finish_heads[i] : BUSTER_CHECK_NONE; declaration != BUSTER_CHECK_NONE;
             declaration = reader->initializer_finish_next[declaration])
        {
            if (!check_input_initializer_finish(reader, declaration))
            {
                break;
            }
        }
        if (reader->output->report_count != 0 || reader->output->resource)
        {
            break;
        }
        const bool scopes = check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_SCOPE);
        while (scopes && reader->scope_count != 0 && reader->scope_ends[reader->scope_count - 1] < i)
        {
            reader->scope_count -= 1;
            // A scope that declared nothing has nothing to end, and its restore is left out where the stream, which
            // takes its events in source order, has one past the scope's end already: a definition in a bound after
            // a statement expression, which the declarator read before the walk reached the expression's block
            // (`[({ n; }) + sizeof(struct S { int x; })]`, check_input_vla_bound).
            const bool empty = c_bindings_mark(&reader->bindings) == reader->scope_marks[reader->scope_count];
            const bool ahead = reader->bindings.count != reader->bindings.base && reader->bindings.point[reader->bindings.count - 1] > i;
            if (!(empty && ahead) && !c_bindings_restore(&reader->bindings, reader->scope_marks[reader->scope_count], i))
            {
                reader->output->resource = true;
            }
        }
        if (i > reader->function_end)
        {
            reader->function_result = 0;
            reader->function_body = BUSTER_CHECK_NONE;
        }
        if (i == end)
        {
            break;
        }
        if (reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[i] != 0)
        {
            // The definition's declaration is visible at its ordinary source point. Only its selected-out
            // body is deferred; parameters and the full function declarator were checked before this opening.
            for (uint32_t token = reader->activate[i]; token != BUSTER_CHECK_NONE; token = reader->activation_next[token])
            {
                check_input_bind(reader, token, i);
            }
            if (reader->deferred != 0)
            {
                check_deferred_cut(reader, i);
            }
            i = parser->tree.parents[i];
            continue;
        }
        if (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_QUERY) && reader->designator_valid[i] == 2 && !check_input_initializer_designator(reader, i))
        {
            break;
        }
        CheckInputSpecifierJob* job = check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_QUERY) ? reader->specifier_finishes[i] : 0;
        if (job != 0 && !job->ready)
        {
            CheckValue value = { 0 };
            uint32_t failed_token = BUSTER_CHECK_NONE;
            reader->constant_abi_origin = 0;
            const bool evaluated = check_input_evaluate_cached_at(reader, job->keyword + 2, job->closing, &value, &failed_token);
            check_input_warning_suppress(reader, job->keyword + 2, job->closing, check_constant_unevaluated_warnings());
            job->result = evaluated ? value.type : 0;
            job->result_abi_origin = evaluated ? reader->constant_abi_origin : 0;
            if (!evaluated && failed_token < parser->tokens.count && !reader->output->resource)
            {
                check_input_report(reader, failed_token, CHECK_INPUT_DECLARATION);
            }
            const uint32_t name = parser->tokens.names[job->keyword];
            if (name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2)
            {
                job->result = type_unqualified(job->result);
                job->result = type_kind(reader->types, job->result) == BUSTER_TYPE_KIND_ATOMIC ? type_element(reader->types, job->result) : job->result;
            }
            job->ready = true;
            reader->specifier_query_depth -= 1;
            if (job->result != 0)
            {
                check_input_declaration(reader, job->first);
            }
            else if (reader->output->report_count == 0 && !reader->output->resource)
            {
                check_input_report(reader, job->keyword, CHECK_INPUT_DECLARATION);
            }
        }
        const uint8_t role = parser->tree.roles[i];
        const uint8_t kind = parser->tokens.kinds[i];
        const uint32_t parent = parser->tree.parents[i];
        const uint8_t syntax = reader->syntax[i];
        // The tests of the token's syntax, of a token its syntax marks on the vector path.
        const bool syntactic = check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_SYNTAX);
        if (syntactic)
        {
            // A label's name, a goto's, and an address of a label's (GNU C's &&label): no identifier of the ordinary scopes.
            const bool label_name = role == PARSE_ROLE_OPERAND && parent < parser->tokens.count &&
                                    (parser->tree.roles[parent] == PARSE_ROLE_LABEL || parser->tokens.kinds[parent] == PARSE_KIND_GOTO ||
                                     (parser->tree.roles[parent] == PARSE_ROLE_PREFIX && parser->tokens.kinds[parent] == PARSE_KIND_AMPAMP));
            reader->ignored[i] |= label_name || role == PARSE_ROLE_LABEL || kind == PARSE_KIND_GOTO || kind == PARSE_KIND_SWITCH || role == PARSE_ROLE_OPEN_CASE;
        }
        if (syntactic && !reader->ignored[i] && role == PARSE_ROLE_OPEN_GROUP && i + 1 < parser->tokens.count && parser->tree.roles[i + 1] == PARSE_ROLE_OPEN_BLOCK)
        {
            check_input_region_prepare(reader, i);
        }
        if (syntactic && !reader->ignored[i] && kind == PARSE_KIND_ASM)
        {
            check_input_asm_prepare(reader, i);
        }
        // The names the walk prepares builtins of, whose tests a token without one (most) skips.
        if (syntactic && (syntax & BUSTER_CHECK_SYNTAX_NAMED) != 0)
        {
            if (!reader->ignored[i] && parser->tokens.names[i] == C_NAME_BUILTIN_VA_ARG)
            {
                check_input_vararg_prepare(reader, i, CBUILTIN_VA_ARG);
            }
            if (!reader->ignored[i] && (parser->tokens.names[i] == C_NAME_BUILTIN_CONVERTVECTOR || parser->tokens.names[i] == C_NAME_BUILTIN_BIT_CAST))
            {
                check_input_builtin_type_prepare(reader, i, parser->tokens.names[i] == C_NAME_BUILTIN_BIT_CAST ? CBUILTIN_BIT_CAST : CBUILTIN_CONVERTVECTOR);
            }
            if (!reader->ignored[i] && (parser->tokens.names[i] == C_NAME_GENERIC || parser->tokens.names[i] == C_NAME_BUILTIN_CHOOSE_EXPR))
            {
                check_input_selection_prepare(reader, i);
            }
            if (!reader->ignored[i] && (parser->tokens.names[i] == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || parser->tokens.names[i] == C_NAME_BUILTIN_OFFSETOF))
            {
                const uint32_t close = i + 1 < parser->tokens.count ? parser->tree.parents[i + 1] : parser->tokens.count;
                const uint32_t end = close < parser->tokens.count ? check_constant_punctuation(parser, i + 2, close, PARSE_KIND_COMMA) : close;
                const uint32_t definition = check_input_specifier_definition(reader, i + 2, end);
                if (definition != BUSTER_CHECK_NONE)
                {
                    check_input_tag_definition(reader, definition, end);
                }
                if (reader->output->report_count == 0 && !reader->output->resource)
                {
                    check_input_builtin_constant_prepare(reader, i);
                }
            }
        }
        if (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_QUERY) && reader->selection_type_ends[i] != 0)
        {
            check_input_selection_type(reader, i);
        }
        const bool activations = !(reader->lane && i == begin) && check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_ACTIVATE);
        for (uint32_t token = activations ? reader->activate[i] : BUSTER_CHECK_NONE; token != BUSTER_CHECK_NONE; token = reader->activation_next[token])
        {
            check_input_bind(reader, token, i);
        }
        if (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_QUERY) && reader->vararg_types[i] != 0)
        {
            check_input_vararg_type(reader, i);
        }
        if (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_QUERY) && reader->builtin_type_ends[i] != 0)
        {
            check_input_builtin_type(reader, i);
        }
        if (syntactic && !reader->ignored[i] && (syntax & BUSTER_CHECK_SYNTAX_ATTRIBUTE) != 0)
        {
            check_input_statement_attributes(reader, i);
        }
        if (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_DECLARATION) && reader->initializers[i] != 0 && reader->initializer_declarations[i] != BUSTER_CHECK_NONE)
        {
            const uint32_t declaration = reader->initializer_declarations[i];
            if (reader->output->initializer_ends[declaration] > reader->resolve_limit)
            {
                reader->expression_limit = i < reader->expression_limit ? i : reader->expression_limit;
                check_input_pragma_report(reader);
                break;
            }
            check_input_initializer_prepare(reader, i);
        }
        if ((syntactic && !reader->ignored[i] && (role == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
             kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC || (syntax & BUSTER_CHECK_SYNTAX_ATTRIBUTE) != 0 ||
             (kind == PARSE_KIND_EXTENSION && i + 1 < parser->tokens.count && (parser->tree.roles[i + 1] == PARSE_ROLE_SPECIFIER ||
              parser->tokens.kinds[i + 1] == PARSE_KIND_STRUCT || parser->tokens.kinds[i + 1] == PARSE_KIND_ENUM)))) ||
            (check_input_resolve_tests(reader, i, BUSTER_CHECK_RESOLVE_DECLARATION) && reader->declaration_bases[i] != 0))
        {
            check_input_declaration(reader, i);
        }
        if (syntactic && !reader->ignored[i] && (role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION))
        {
            const uint32_t parent = parser->tree.parents[i];
            reader->scope_marks[reader->scope_count] = c_bindings_mark(&reader->bindings);
            reader->scope_openings[reader->scope_count] = i;
            reader->scope_ends[reader->scope_count] = parent < parser->tokens.count ? reader->ends[parent] : parser->tokens.count;
            if (reader->scope_ends[reader->scope_count] < parser->tokens.count)
            {
                // The scope ends at the token after its last, which the walk visits to restore the bindings.
                check_input_visit(reader, reader->scope_ends[reader->scope_count] + 1, BUSTER_CHECK_RESOLVE_SCOPE);
            }
            reader->scope_count += 1;
            if (role == PARSE_ROLE_OPEN_BODY)
            {
                reader->function_result = reader->query_types[i];
                reader->function_end = reader->scope_ends[reader->scope_count - 1];
                reader->function_body = i;
                // The tags and enumeration constants the parameter list declared are in the body's scope too, as its
                // parameters are (C11 6.2.1p4): declared again in it, in their order and with their declarations'
                // tokens, as clang does (Sema::ActOnStartOfFunctionDef).
                const uint32_t first_declaration = reader->output->body_declarations[i] != BUSTER_CHECK_NONE ? reader->parameter_declarations[i] : BUSTER_CHECK_NONE;
                for (uint32_t declaration = first_declaration; declaration != BUSTER_CHECK_NONE; declaration = reader->parameter_declaration_next[declaration])
                {
                    const bool tag = reader->declaration_kinds[declaration] == C_BINDING_TAG_KIND;
                    if (c_bindings_declare(&reader->bindings, tag ? C_BINDING_TAG : C_BINDING_ORDINARY, parser->tokens.names[declaration],
                                           tag ? C_BINDING_TAG_KIND : C_BINDING_ENUMERATOR, declaration, i, reader->scope_count) == 0)
                    {
                        reader->output->resource = true;
                    }
                }
                for (uint32_t p = reader->parameters[i]; p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
                {
                    if ((reader->parameter_shapes[p] & 2) != 0)
                    {
                        check_input_report(reader, p, CHECK_INPUT_DECLARATION);
                    }
                    reader->output->declaration_bodies[p] = i;
                    check_input_bind(reader, p, i);
                }
                check_input_vla_parameters(reader, i);
            }
        }
        if (syntactic && role == PARSE_ROLE_RETURN)
        {
            if (kind == PARSE_KIND_RETURN)
            {
                reader->return_types[i] = reader->function_result;
            }
            else if (kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE)
            {
                bool loop = false;
                for (uint32_t ancestor = parser->tree.parents[i]; ancestor < parser->tokens.count && !loop; ancestor = parser->tree.parents[ancestor])
                {
                    const uint8_t ancestor_role = parser->tree.roles[ancestor];
                    const uint32_t keyword = reader->first_child[ancestor];
                    loop = (i > ancestor || ancestor_role == PARSE_ROLE_DO) && (ancestor_role == PARSE_ROLE_CLOSE_FOR || ancestor_role == PARSE_ROLE_DO ||
                           (ancestor_role == PARSE_ROLE_CLOSE_CONDITION && keyword < parser->tokens.count &&
                            (parser->tokens.kinds[keyword] == PARSE_KIND_WHILE || (kind == PARSE_KIND_BREAK && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH))));
                }
                if (!loop)
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
                reader->ignored[i] = true;
            }
        }
        if (syntactic && role == PARSE_ROLE_OPEN_FOR)
        {
            const uint32_t close = parser->tree.parents[i];
            uint32_t separators = 0;
            for (uint32_t child = reader->first_child[close]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                if (child > i && child < close && parser->tokens.kinds[child] == PARSE_KIND_SEMI)
                {
                    separators += 1;
                    reader->conditions[child] = separators == 2;
                }
            }
        }
        if (syntactic && !reader->ignored[i] && (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF || role == PARSE_ROLE_OPEN_COMPOUND))
        {
            const uint32_t close = parser->tree.parents[i];
            const uint32_t next = i + 1;
            if (close < parser->tokens.count && next < close && (parser->tree.roles[next] == PARSE_ROLE_SPECIFIER ||
                parser->tokens.kinds[next] == PARSE_KIND_STRUCT || parser->tokens.kinds[next] == PARSE_KIND_ENUM ||
                parser->tokens.kinds[next] == PARSE_KIND_TYPEOF || parser->tokens.kinds[next] == PARSE_KIND_ATOMIC ||
                check_input_attribute_starts(parser, next, close)))
            {
                uint32_t cursor = next;
                // A definition among the type name's specifiers, unless a declarator read it already: one in a variable
                // array's bound, which the walk reaches after the bound's declaration.
                const uint32_t definition = check_input_specifier_definition(reader, next, close);
                if (definition != BUSTER_CHECK_NONE)
                {
                    check_input_tag_definition(reader, definition, close);
                }
                bool is_typedef;
                bool is_inline;
                uint32_t storage;
                TypeId type = check_input_specifiers(reader, &cursor, close, &is_typedef, &storage, &is_inline);
                uint32_t name;
                uint32_t parameters;
                type = check_input_declarator(reader, type, &cursor, close, &name, &parameters);
                if (type == 0 || cursor != close || name != BUSTER_CHECK_NONE || is_typedef || storage != 0 || is_inline)
                {
                    check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                }
                reader->query_types[close] = type;
                if (role == PARSE_ROLE_OPEN_COMPOUND && type != 0)
                {
                    const uint32_t body = close + 1;
                    const uint32_t body_end = body < parser->tokens.count ? parser->tree.parents[body] : parser->tokens.count;
                    const uint8_t type_kind_value = type_kind(reader->types, type);
                    if (body_end < parser->tokens.count && parser->tree.roles[body] == PARSE_ROLE_OPEN_COMPOUND_BODY &&
                        type_kind_value != BUSTER_TYPE_KIND_FUNCTION && type_kind_value != BUSTER_TYPE_KIND_VOID)
                    {
                        reader->output->declarations[i] = i;
                        reader->declared_types[i] = type;
                        reader->declaration_kinds[i] = C_BINDING_OBJECT;
                        reader->objects[i] = i + 1;
                        reader->static_objects[i] = reader->function_result == 0;
                        reader->defined[i] = true;
                        reader->output->compound_objects[i] = true;
                        reader->output->declaration_bodies[i] = reader->function_body;
                        reader->output->initializer_begins[i] = body;
                        reader->output->initializer_ends[i] = body_end + 1;
                        reader->initializer_declarations[i] = i;
                        reader->initializer_declarations[body_end] = i;
                        reader->query_types[i] = type;
                        check_input_initializer_prepare(reader, i);
                        reader->ignored[close] = true;
                    }
                    else
                    {
                        check_input_report(reader, i, CHECK_INPUT_DECLARATION);
                    }
                }
            }
        }
        if (syntactic && !reader->ignored[i] && kind == PARSE_KIND_IDENTIFIER && role != PARSE_ROLE_MEMBER && role != PARSE_ROLE_TAG && role != PARSE_ROLE_LEAF_RIGHT)
        {
            const uint32_t identifier = parser->tokens.names[i];
            if (reader->output->overload_heads != 0)
            {
                const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier);
                reader->output->overload_heads[i] = handle != 0 ? reader->output->overload_heads[reader->bindings.declaration[handle - 1]] : 0;
            }
            const uint16_t builtin = check_input_builtin_id(reader, i);
            if (builtin != CBUILTIN_NONE && role == PARSE_ROLE_OPERAND && c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier) == 0)
            {
                const uint32_t close = parser->tree.parents[i];
                const bool call = close < parser->tokens.count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL &&
                                  i + 1 < close && parser->tree.roles[i + 1] == PARSE_ROLE_OPEN_CALL;
                if (call)
                {
                    reader->builtins[close] = builtin;
                    reader->ignored[i] = true;
                    if (check_input_vararg_id(builtin))
                    {
                        check_input_vararg_prepare(reader, i, builtin);
                    }
                }
                else
                {
                    check_input_report(reader, i, CHECK_INPUT_UNSUPPORTED);
                }
            }
            else if (reader->output->declarations[i] == BUSTER_CHECK_NONE)
            {
                const CheckInputIntrinsicStatus intrinsic = check_input_intrinsic_prepare(reader, i);
                if (intrinsic == CHECK_INPUT_INTRINSIC_PREPARED)
                {
                    const uint32_t close = parser->tree.parents[i];
                    const CheckInputIntrinsicCall* call = reader->output->intrinsic_calls + reader->output->intrinsic_ids[close] - 1;
                    const CheckInputIntrinsicDeclaration* declaration = reader->output->intrinsic_declarations + call->declaration;
                    reader->ignored[i] = true;
                    reader->query_types[close] = declaration->types.function_type;
                    const uint64_t required = declaration->types.ice_parameters | check_graph_x86_ice_mask(declaration->info, false);
                    for (uint32_t argument = 0; argument < call->argument_count && argument < 64; argument += 1)
                    {
                        if ((required & (UINT64_C(1) << argument)) != 0)
                        {
                            for (uint32_t token = call->arguments[argument].first; token < call->arguments[argument].end; token += 1)
                            {
                                reader->constant_contexts[token] |= 1;
                            }
                        }
                    }
                }
                else if (intrinsic == CHECK_INPUT_INTRINSIC_NOT_FOUND && c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, identifier, i, i) == 0)
                {
                    reader->output->resource = true;
                }
                if (intrinsic == CHECK_INPUT_INTRINSIC_NOT_FOUND && (reader->specifier_query_depth != 0 || i < reader->inference_end))
                {
                    // The suspended type query consumes this expression before the final batch replay. Capture
                    // its declaration while the source scope is active; the replay still verifies the same event.
                    const uint32_t handle = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier);
                    if (handle != 0)
                    {
                        const uint32_t declaration = reader->bindings.declaration[handle - 1];
                        reader->output->declarations[i] = declaration;
                        reader->declared_types[i] = reader->declared_types[declaration];
                    }
                    else if (reader->specifier_query_depth != 0)
                    {
                        check_input_report(reader, i, CHECK_INPUT_UNDECLARED);
                    }
                }
            }
        }
        if (syntactic && !reader->ignored[i] && (syntax & BUSTER_CHECK_SYNTAX_STRING) != 0)
        {
            check_input_string(reader, i);
        }
    }
    reader->output->resolve_visits += visits;
}

static uint32_t check_input_expression_end(const CheckInputReader* reader, uint32_t token)
{
    // The condition's parser subtree also owns its statement body. Its semantic action depends only on the
    // expression ending at this parenthesis, so later statements must not extend its visibility or diagnostic cut.
    return reader->parser->tree.roles[token] == PARSE_ROLE_CLOSE_CONDITION ? token + 1 : reader->ends[token] + 1;
}

static uint32_t check_input_add(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_graph_add(&reader->output->graph, op, token, a, b, c);
    if (node == BUSTER_CHECK_NONE)
    {
        reader->output->resource = true;
    }
    else
    {
        reader->value_next[node] = BUSTER_CHECK_NONE;
        reader->output->graph.source_positions[node] = check_input_expression_end(reader, token);
        if (reader->output->graph.warning_suppressed != 0)
        {
            reader->output->graph.warning_suppressed[node] = reader->warning_suppressed != 0 ? reader->warning_suppressed[token] : 0;
        }
        if (reader->output->graph.fp_environments != 0)
        {
            reader->output->graph.fp_environments[node] = reader->constant_contexts[token] == 0 ? reader->output->fp_environments[token] : 0;
        }
    }
    return node;
}

static void check_input_vector_member(CheckInputReader* reader, uint32_t node, uint32_t token)
{
    CheckGraph* graph = &reader->output->graph;
    const uint32_t name = reader->parser->tokens.names[token];
    const uint8_t* bytes = intern_bytes(reader->pp->table, name);
    const uint32_t length = intern_length(reader->pp->table, name);
    CheckVectorMember member;
    if (!check_vector_member_decode(bytes, length, 0, 0, &member))
    {
        uint8_t* lanes = arena_push_array(reader->arena, length, sizeof(uint8_t), 1);
        reader->output->resource |= lanes == 0;
        if (lanes != 0)
        {
            reader->output->resource |= !check_vector_member_decode(bytes, length, lanes, length, &member);
        }
    }
    if (!reader->output->resource && member.kind != CHECK_VECTOR_MEMBER_INVALID)
    {
        if (graph->vector_member_count < graph->vector_member_capacity)
        {
            graph->vector_members[graph->vector_member_count++] = (CheckVectorCall){ .node = node, .member = member };
        }
        else
        {
            reader->output->resource = true;
        }
    }
}

static CheckOp check_input_binary(uint8_t kind)
{
    CheckOp result = CHECK_OP_COUNT;
    switch (kind)
    {
        case PARSE_KIND_PLUS: result = CHECK_OP_ADD; break;
        case PARSE_KIND_MINUS: result = CHECK_OP_SUBTRACT; break;
        case PARSE_KIND_STAR: result = CHECK_OP_MULTIPLY; break;
        case PARSE_KIND_SLASH: result = CHECK_OP_DIVIDE; break;
        case PARSE_KIND_PERCENT: result = CHECK_OP_REMAINDER; break;
        case PARSE_KIND_LESSLESS: result = CHECK_OP_SHIFT_LEFT; break;
        case PARSE_KIND_GREATERGREATER: result = CHECK_OP_SHIFT_RIGHT; break;
        case PARSE_KIND_LESS: result = CHECK_OP_LESS; break;
        case PARSE_KIND_LESSEQUAL: result = CHECK_OP_LESS_EQUAL; break;
        case PARSE_KIND_GREATER: result = CHECK_OP_GREATER; break;
        case PARSE_KIND_GREATEREQUAL: result = CHECK_OP_GREATER_EQUAL; break;
        case PARSE_KIND_EQUALEQUAL: result = CHECK_OP_EQUAL; break;
        case PARSE_KIND_EXCLAIMEQUAL: result = CHECK_OP_NOT_EQUAL; break;
        case PARSE_KIND_AMP: result = CHECK_OP_BIT_AND; break;
        case PARSE_KIND_PIPE: result = CHECK_OP_BIT_OR; break;
        case PARSE_KIND_CARET: result = CHECK_OP_BIT_XOR; break;
        case PARSE_KIND_AMPAMP: result = CHECK_OP_LOGICAL_AND; break;
        case PARSE_KIND_PIPEPIPE: result = CHECK_OP_LOGICAL_OR; break;
        case PARSE_KIND_EQUAL: result = CHECK_OP_ASSIGN; break;
        case PARSE_KIND_PLUSEQUAL: result = CHECK_OP_ADD_ASSIGN; break;
        case PARSE_KIND_MINUSEQUAL: result = CHECK_OP_SUBTRACT_ASSIGN; break;
        case PARSE_KIND_STAREQUAL: result = CHECK_OP_MULTIPLY_ASSIGN; break;
        case PARSE_KIND_SLASHEQUAL: result = CHECK_OP_DIVIDE_ASSIGN; break;
        case PARSE_KIND_PERCENTEQUAL: result = CHECK_OP_REMAINDER_ASSIGN; break;
        case PARSE_KIND_LESSLESSEQUAL: result = CHECK_OP_SHIFT_LEFT_ASSIGN; break;
        case PARSE_KIND_GREATERGREATEREQUAL: result = CHECK_OP_SHIFT_RIGHT_ASSIGN; break;
        case PARSE_KIND_AMPEQUAL: result = CHECK_OP_BIT_AND_ASSIGN; break;
        case PARSE_KIND_PIPEEQUAL: result = CHECK_OP_BIT_OR_ASSIGN; break;
        case PARSE_KIND_CARETEQUAL: result = CHECK_OP_BIT_XOR_ASSIGN; break;
        case PARSE_KIND_COMMA: result = CHECK_OP_COMMA; break;
        default: break;
    }
    return result;
}

// The first token of the expression whose root is `token`. Every subtree is a contiguous run of tokens and a node's
// children are in token order, so its first token is down the chain of first children that precede their parents.
// The chains of a call's arguments stop at nested callees, so each token is on at most one argument's chain.
static uint32_t check_input_first_token(const CheckInputReader* reader, uint32_t token)
{
    uint32_t first = token;
    while (first < reader->parser->tokens.count && reader->first_child[first] < first)
    {
        first = reader->first_child[first];
    }
    return first;
}

static uint32_t check_input_intrinsic_arguments(CheckInputReader* reader, const CheckInputIntrinsicCall* call,
                                               uint32_t first, uint64_t* ice_arguments)
{
    CheckGraph* graph = &reader->output->graph;
    const CheckInputIntrinsicDeclaration* declaration = reader->output->intrinsic_declarations + call->declaration;
    const bool constant_context = reader->constant_contexts[call->closing] != 0;
    const uint64_t required = declaration->types.ice_parameters | check_graph_x86_ice_mask(declaration->info, constant_context);
    const uint32_t offset = graph->argument_count;
    uint32_t argument = first;
    for (uint32_t index = 0; index < call->argument_count && argument != BUSTER_CHECK_NONE; index += 1)
    {
        const uint32_t next = reader->value_next[argument];
        CheckValue value = { 0 };
        const CheckInputIntrinsicArgument span = call->arguments[index];
        const bool ice = index < 64 && check_input_evaluate_cached(reader, span.first, span.end, &value) && check_input_integer_ice(reader, &value);
        if (ice)
        {
            *ice_arguments |= UINT64_C(1) << index;
        }
        if (ice && (required & (UINT64_C(1) << index)) != 0)
        {
            // Keep the original subtree checked, but an immediate operand is the value proved in its source
            // constant context. Runtime-only effects in discarded constant-evaluator paths are not call inputs.
            const uint32_t proven = check_input_add(reader, CHECK_OP_VALUE, graph->tokens[argument], BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (proven != BUSTER_CHECK_NONE)
            {
                *check_graph_leaf_of(graph, proven) = value;
                argument = proven;
            }
        }
        if (graph->argument_count < graph->argument_capacity)
        {
            graph->argument_tokens[graph->argument_count] = span.first;
            graph->argument_nodes[graph->argument_count++] = argument;
        }
        else
        {
            reader->output->resource = true;
        }
        argument = next;
    }
    return offset;
}

#include "c_check_region_input.c"

// Whether one of the sparse columns the expressions' walk tests first has an entry for `token`: a statement expression's
// group, a cached constant, a runtime selection, a va_arg call, a designator, a variable array's bound or an asm statement.
static bool check_input_expression_sparse(const CheckInputReader* reader, uint32_t token)
{
    const CheckInput* input = reader->output;
    return input->expression_regions[token] != 0 || reader->constant_present[token] != 0 || reader->selected_tokens[token] < reader->parser->tokens.count ||
           reader->vararg_calls[token] != 0 || reader->designator_tokens[token] != 0 || reader->vla_ends[token] != 0 || input->asm_ids[token] != 0;
}

// The expressions' walk's classes of `token` (BUSTER_CHECK_EXPRESSION_*), by the walk's own tests: the scalar twin of
// check_input_expression_classes' vector path. The walk acts at a token with children, at one a sparse column has an
// entry for, and at one whose role or kind its other tests take: for an ignored token, a role whose values it passes on
// as a list. At any other token it writes the token's first value, NONE, and nothing else.
static uint8_t check_input_expression_class(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint8_t role = parser->tree.roles[token];
    const uint8_t kind = parser->tokens.kinds[token];
    const bool sparse = check_input_expression_sparse(reader, token);
    const bool list = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_CLOSE_ATTRIBUTE ||
                      role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT || role == PARSE_ROLE_CLOSE_SPECIFIER;
    const bool operation = role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_RETURN || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX ||
                           role == PARSE_ROLE_MEMBER_OPERATOR || (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA ||
                           role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_ASSOCIATION || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_CLOSE_TERNARY ||
                           role == PARSE_ROLE_RANGE;
    const bool closing = role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_SUBSCRIPT ||
                         role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_CONDITION ||
                         role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
                         role == PARSE_ROLE_CLOSE_CASE;
    // The roles and kinds the walk refuses where it meets them, which a valid unit's reader ignores.
    const bool refused = role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_ASM ||
                         role == PARSE_ROLE_LABEL || role == PARSE_ROLE_TAG || role == PARSE_ROLE_BITFIELD || kind == PARSE_KIND_STRING || kind == PARSE_KIND_GOTO ||
                         kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_LABEL || kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_CASE ||
                         kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_ASM || kind == PARSE_KIND_ATTRIBUTE ||
                         kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_CALLING_CONVENTION || kind == PARSE_KIND_TYPEOF ||
                         kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT || kind == PARSE_KIND_REAL_IMAG || kind == PARSE_KIND_EXTENSION;
    const bool acts = reader->first_child[token] != BUSTER_CHECK_NONE || sparse || (reader->ignored[token] ? list : operation || closing || refused);
    return (uint8_t)((acts ? BUSTER_CHECK_EXPRESSION_ACTS : 0) | (sparse ? BUSTER_CHECK_EXPRESSION_SPARSE : 0));
}

// The expressions' walk's classes of every token, into CheckInputReader.visits, as check_input_expression_class gives
// them: on the vector path, 64 tokens' roles and kinds looked up in the tables (check_input_role_tests) and 16 tokens'
// sparse columns and first children at a time; on the scalar path, its twin's, a token at a time. Each token the walk
// doesn't act at gets its first value, NONE, here, which the walk writes when it reaches it, and the walk's vector path
// skips it. Nothing the walk does writes a column the classes read at a token it hasn't reached: the scalar path, which
// reaches every token, counts each token it acts at, or tests a sparse column of, that its class left out
// (CheckInput.visit_misses). Returns the number of tokens the walk acts at.
static uint32_t check_input_expression_classes(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    const __m512i role_tests_low = _mm512_load_si512(check_input_role_tests);
    const __m512i role_tests_high = _mm512_load_si512(check_input_role_tests + 64);
    const __m512i kind_tests_low = _mm512_load_si512(check_input_kind_tests);
    const __m512i kind_tests_high = _mm512_load_si512(check_input_kind_tests + 64);
    const __m512i none = _mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE);
    uint32_t result = 0;
    uint32_t token = 0;
    for (; !parser->options.scalar && token < count; token += 64)
    {
        const uint32_t length = count - token < 64 ? count - token : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        const __m512i role = _mm512_maskz_loadu_epi8(lanes, parser->tree.roles + token);
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, parser->tokens.kinds + token);
        const __m512i ignored = _mm512_maskz_loadu_epi8(lanes, reader->ignored + token);
        const __m512i role_tests = _mm512_permutex2var_epi8(role_tests_low, role, role_tests_high);
        const __m512i kind_tests = _mm512_permutex2var_epi8(kind_tests_low, kind, kind_tests_high);
        const uint64_t skipped = _mm512_test_epi8_mask(ignored, ignored);
        const uint64_t taken = (skipped & _mm512_test_epi8_mask(role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_LIST))) |
                               (~skipped & (_mm512_test_epi8_mask(role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_EXPRESSION)) |
                                            _mm512_test_epi8_mask(kind_tests, _mm512_set1_epi8(BUSTER_CHECK_KIND_EXPRESSION))));
        uint64_t acts = 0;
        uint64_t sparse = 0;
        for (uint32_t part = 0; part < length; part += 16)
        {
            const __mmask16 parts = (__mmask16)_bzhi_u32(UINT32_MAX, length - part < 16 ? length - part : 16);
            const uint32_t at = token + part;
            const __m512i regions = _mm512_maskz_loadu_epi32(parts, input->expression_regions + at);
            const __m512i constants = _mm512_maskz_loadu_epi32(parts, reader->constant_present + at);
            const __m512i varargs = _mm512_maskz_loadu_epi32(parts, reader->vararg_calls + at);
            const __m512i designators = _mm512_maskz_loadu_epi32(parts, reader->designator_tokens + at);
            const __m512i bounds = _mm512_maskz_loadu_epi32(parts, reader->vla_ends + at);
            const __m512i statements = _mm512_maskz_loadu_epi32(parts, input->asm_ids + at);
            const __m512i entries = _mm512_or_si512(_mm512_or_si512(_mm512_or_si512(regions, constants), _mm512_or_si512(varargs, designators)),
                                                    _mm512_or_si512(bounds, statements));
            const __mmask16 selected = _mm512_mask_cmplt_epu32_mask(parts, _mm512_maskz_loadu_epi32(parts, reader->selected_tokens + at), _mm512_set1_epi32((int32_t)count));
            const __mmask16 part_sparse = _mm512_mask_test_epi32_mask(parts, entries, entries) | selected;
            const __mmask16 children = _mm512_mask_cmpneq_epu32_mask(parts, _mm512_maskz_loadu_epi32(parts, reader->first_child + at), none);
            const __mmask16 part_acts = children | part_sparse | (__mmask16)(taken >> part);
            _mm512_mask_storeu_epi32(reader->first_value + at, parts & (__mmask16)~part_acts, none);
            acts |= (uint64_t)part_acts << part;
            sparse |= (uint64_t)part_sparse << part;
        }
        const __m512i classes = _mm512_or_si512(_mm512_maskz_mov_epi8(acts, _mm512_set1_epi8(BUSTER_CHECK_EXPRESSION_ACTS)),
                                                _mm512_maskz_mov_epi8(sparse, _mm512_set1_epi8(BUSTER_CHECK_EXPRESSION_SPARSE)));
        _mm512_mask_storeu_epi8(reader->visits + token, lanes, classes);
        result += (uint32_t)__builtin_popcountll(acts);
    }
    for (; token < count; token += 1)
    {
        reader->visits[token] = check_input_expression_class(reader, token);
        if ((reader->visits[token] & BUSTER_CHECK_EXPRESSION_ACTS) == 0)
        {
            reader->first_value[token] = BUSTER_CHECK_NONE;
        }
        result += (reader->visits[token] & BUSTER_CHECK_EXPRESSION_ACTS) != 0;
    }
    return result;
}

// The walk over the tokens in postorder that builds the expression graph. The vector path skips the tokens it doesn't
// act at, whose first values their classes wrote (check_input_expression_classes), and tests the sparse columns only
// of tokens their classes say have an entry; the scalar path, its twin, visits every token and tests every column.
static void check_input_expressions(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    CheckGraph* graph = &reader->output->graph;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    // The tokens the walk visits: every token on the scalar path, and those it acts at on the vector path.
    const uint32_t acts = check_input_expression_classes(reader);
    reader->output->expression_visits = scalar ? parser->tokens.count : acts;
    for (uint32_t order = 0; order < parser->tokens.count && !reader->output->resource; order += 1)
    {
        const uint32_t token = reader->postorder[order];
        const uint8_t classes = reader->visits[token];
        if (!scalar && (classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0)
        {
            continue;
        }
        // Every token's first value is written when the walk reaches it, NONE for one without values, before its parent
        // reads it: a column written before it is read.
        reader->first_value[token] = BUSTER_CHECK_NONE;
        if (check_input_expression_end(reader, token) > reader->expression_limit ||
            (reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[token] != 0))
        {
            continue;
        }
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t last = BUSTER_CHECK_NONE;
        uint32_t count = 0;
        for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (reader->specifier_finishes[token] != 0 && child < token)
            {
                // Keep query-region nodes checked, but do not make their value a runtime declaration operand.
                continue;
            }
            const uint32_t begin = reader->first_value[child];
            if (begin != BUSTER_CHECK_NONE)
            {
                if (last != BUSTER_CHECK_NONE)
                {
                    reader->value_next[last] = begin;
                }
                else
                {
                    first = begin;
                }
                last = reader->last_value[child];
                if (scalar)
                {
                    for (uint32_t n = begin; n != BUSTER_CHECK_NONE; n = reader->value_next[n])
                    {
                        count += 1;
                    }
                }
                else
                {
                    count += reader->value_counts[child];
                }
            }
        }
        const uint32_t a = first;
        const uint32_t b = a != BUSTER_CHECK_NONE ? reader->value_next[a] : BUSTER_CHECK_NONE;
        const uint32_t c = b != BUSTER_CHECK_NONE ? reader->value_next[b] : BUSTER_CHECK_NONE;
        uint32_t node = BUSTER_CHECK_NONE;
        bool list = false;
        bool supported = true;
        CheckOp op = CHECK_OP_COUNT;
        // The sparse columns' tests, of a token whose classes say one has an entry for it on the vector path.
        const bool sparse = scalar || (classes & BUSTER_CHECK_EXPRESSION_SPARSE) != 0;
        if (sparse && role == PARSE_ROLE_CLOSE_GROUP && reader->output->expression_regions[token] != 0)
        {
            CheckInputRegion* region = &reader->output->regions[reader->output->expression_regions[token] - 1];
            if (region->result == BUSTER_CHECK_NONE)
            {
                region->result = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (region->result != BUSTER_CHECK_NONE)
                {
                    *check_graph_leaf_of(graph, region->result) = (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
                }
            }
            if (region->result != BUSTER_CHECK_NONE)
            {
                const uint32_t proof = check_input_region_proof(reader, region);
                node = check_input_add(reader, CHECK_OP_STATEMENT_VALUE, token, region->result, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE && proof != BUSTER_CHECK_NONE)
                {
                    graph->region_proofs[proof].node = node;
                }
            }
        }
        else if (sparse && reader->constant_present[token] != 0)
        {
            node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                *check_graph_leaf_of(graph, node) = reader->cached_constants[reader->constant_present[token] - 1];
            }
        }
        else if (sparse && reader->selected_tokens[token] < parser->tokens.count)
        {
            const uint32_t selected = reader->output->token_nodes[reader->selected_tokens[token]];
            supported = selected != BUSTER_CHECK_NONE;
            if (supported)
            {
                node = check_input_add(reader, CHECK_OP_SELECT, token, selected, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            }
        }
        else if (sparse && reader->vararg_calls[token] != 0)
        {
            op = CHECK_OP_VARARGS;
        }
        else if (sparse && reader->designator_tokens[token] != 0)
        {
            const uint32_t opener = reader->designator_tokens[token] - 1;
            node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                *check_graph_leaf_of(graph, node) = (CheckValue)
                {
                    .type = BUSTER_TYPE_ULONG,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_ICE,
                    .constant_kind = CHECK_CONSTANT_INTEGER,
                    .integer = reader->designator_indices[opener],
                };
            }
        }
        else if (sparse && reader->vla_ends[token] != 0)
        {
            node = check_input_vla_capture(reader, token);
        }
        else if (sparse && reader->output->asm_ids[token] != 0)
        {
            if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION)
            {
                supported = count == 1;
                reader->output->token_nodes[token] = count == 1 ? first : BUSTER_CHECK_NONE;
            }
        }
        else if (reader->ignored[token])
        {
            // Specifier chains carry initializer nodes to their enclosing statement without creating expressions.
            list = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_SEPARATOR ||
                   role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ||
                   role == PARSE_ROLE_CLOSE_SPECIFIER;
        }
        else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER)
        {
            if (reader->declared_types[token] != 0)
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    const TypeId type = reader->declared_types[token];
                    const uint32_t declaration = reader->output->declarations[token];
                    const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                    *check_graph_leaf_of(graph, node) = (CheckValue)
                    {
                        .type = type,
                        .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                        .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) | (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                        .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                        .integer = enumeration ? reader->enum_values[declaration] : 0,
                        .object = reader->objects[declaration],
                        .overload_head = reader->output->overload_heads != 0 ? reader->output->overload_heads[token] : 0,
                    };
                }
            }
        }
        else if (role == PARSE_ROLE_OPERAND && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER) && !check_input_function_name_token(parser, token))
        {
            CheckLiteral literal;
            check_input_literal(reader, token, &literal);
            if (!literal.valid)
            {
                check_input_report(reader, token, CHECK_INPUT_LITERAL);
            }
            else
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    *check_graph_leaf_of(graph, node) = check_literal_value(literal);
                }
            }
        }
        else if ((reader->syntax[token] & BUSTER_CHECK_SYNTAX_STRING) != 0 && role == PARSE_ROLE_OPERAND)
        {
            const CheckInitString* string = check_input_string(reader, token);
            if (string != 0)
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    *check_graph_leaf_of(graph, node) = (CheckValue)
                    {
                        .type = string->type,
                        .category = CHECK_LVALUE,
                        .flags = BUSTER_CHECK_STATIC_OBJECT,
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .object = string->object,
                    };
                }
            }
        }
        else if (role == PARSE_ROLE_RETURN && kind == PARSE_KIND_STAR && parser->tree.parents[token] < parser->tokens.count &&
                 check_input_indirect_goto(parser, parser->tree.parents[token]))
        {
            // goto *expression: the whole expression after the `*` converted to const void * as if by assignment (clang's).
            supported = count == 1;
            op = CHECK_OP_CONVERT;
            reader->query_types[token] = check_input_type_result(reader, type_pointer(reader->types, type_qualify(reader->types, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST)));
            supported &= reader->query_types[token] != 0;
        }
        else if (role == PARSE_ROLE_PREFIX)
        {
            supported = count == 1;
            switch (kind)
            {
                case PARSE_KIND_AMPAMP:
                {
                    // GNU C's address of a label (check_input_controls found it): a void * constant, the address of the
                    // label's own object, its token plus one, which no declaration has.
                    const uint32_t label = reader->output->label_targets[token];
                    const TypeId pointer = check_input_type_result(reader, type_pointer(reader->types, BUSTER_TYPE_VOID));
                    supported = label < parser->tokens.count && pointer != 0 && count == 0;
                    node = supported ? check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE) : BUSTER_CHECK_NONE;
                    if (node != BUSTER_CHECK_NONE)
                    {
                        *check_graph_leaf_of(graph, node) = (CheckValue)
                        {
                            .type = pointer,
                            .category = CHECK_RVALUE,
                            .flags = BUSTER_CHECK_STATIC_OBJECT,
                            .constant_kind = CHECK_CONSTANT_ADDRESS,
                            .object = label + 1,
                        };
                    }
                    break;
                }
                case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
                case PARSE_KIND_EXTENSION: op = CHECK_OP_SELECT; break;
                case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
                case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
                case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
                case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
                case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
                case PARSE_KIND_PLUSPLUS: op = CHECK_OP_PRE_INCREMENT; break;
                case PARSE_KIND_MINUSMINUS: op = CHECK_OP_PRE_DECREMENT; break;
                case PARSE_KIND_REAL_IMAG:
                    op = parser->tokens.names[token] == C_NAME_GNU_IMAG || parser->tokens.names[token] == C_NAME_GNU_IMAG2 ? CHECK_OP_IMAGINARY : CHECK_OP_REAL;
                    break;
                case PARSE_KIND_SIZEOF:
                    op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF;
                    supported = count == 1 || reader->query_types[token] != 0;
                    break;
                default: supported = false; break;
            }
        }
        else if ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA || (role == PARSE_ROLE_SEPARATOR && reader->expression_commas[token]))
        {
            op = check_input_binary(kind);
            supported = count == 2 && op != CHECK_OP_COUNT;
            if (reader->initializers[token])
            {
                op = reader->initializers[token] == 1 ? CHECK_OP_INITIALIZE : CHECK_OP_COUNT;
                supported = reader->initializers[token] == 2 || count == 1;
                list = reader->initializers[token] == 2;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP)
        {
            supported = count == 1;
            list = true;
        }
        else if (role == PARSE_ROLE_CLOSE_CAST)
        {
            supported = count == 1 && reader->query_types[token] != 0;
            op = CHECK_OP_CAST;
        }
        else if (role == PARSE_ROLE_CLOSE_SIZEOF)
        {
            list = count == 1;
            const uint32_t parent = parser->tree.parents[token];
            if (reader->query_types[token] != 0 && parent < parser->tokens.count)
            {
                reader->query_types[parent] = reader->query_types[token];
            }
        }
        else if (role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_ASSOCIATION)
        {
            list = true;
        }
        else if (role == PARSE_ROLE_CLOSE_TERNARY)
        {
            supported = count == 3 || count == 2;
            op = CHECK_OP_CONDITIONAL;
        }
        else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            supported = count == 2;
            op = CHECK_OP_SUBSCRIPT;
        }
        else if (role == PARSE_ROLE_MEMBER_OPERATOR)
        {
            supported = count == 1 && token + 1 < parser->tokens.count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
            op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
        }
        else if (role == PARSE_ROLE_POSTFIX)
        {
            supported = count == 1 && (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
            op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT;
        }
        else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION)
        {
            const uint16_t builtin = reader->builtins[token];
            const bool intrinsic = reader->output->intrinsic_ids != 0 && reader->output->intrinsic_ids[token] != 0;
            const bool overflow = builtin >= CBUILTIN_ADD_OVERFLOW && builtin <= CBUILTIN_MUL_OVERFLOW;
            const CheckOp special = check_graph_builtin_op((CBuiltinId)builtin);
            const bool component = check_input_complex_builtin((CBuiltinId)builtin);
            supported = intrinsic || (component ? count == 1 : special == CHECK_OP_BUILTIN_OVERFLOW ? count == 3 :
                        builtin != 0 ? special != CHECK_OP_COUNT || (builtin < CBUILTIN_COUNT && count == (overflow ? 3 : 1)) : count != 0);
            op = intrinsic ? CHECK_OP_BUILTIN_X86 : component ? (builtin >= CBUILTIN_CIMAG ? CHECK_OP_IMAGINARY : CHECK_OP_REAL) :
                 special != CHECK_OP_COUNT ? special : builtin != 0 ? (overflow ? CHECK_OP_BUILTIN_OVERFLOW : CHECK_OP_BUILTIN_UNARY) : CHECK_OP_CALL;
        }
        else if (role == PARSE_ROLE_RETURN)
        {
            supported = reader->return_types[token] != 0 && count <= 1;
            if (count == 1)
            {
                op = CHECK_OP_CONVERT;
                reader->query_types[token] = reader->return_types[token];
            }
            else if (check_action_kind(reader->types, reader->return_types[token]) != BUSTER_TYPE_KIND_VOID &&
                     !check_graph_system_token(graph, token))
            {
                // No value for a non-void function: -Wreturn-mismatch, which clang suppresses in a system header.
                check_input_report(reader, token, CHECK_INPUT_RETURN);
            }
        }
        else if (role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            const uint32_t keyword = reader->first_child[token];
            const bool builtin = reader->builtins[token] != 0;
            op = builtin ? check_graph_builtin_op((CBuiltinId)reader->builtins[token]) : CHECK_OP_COUNT;
            supported = builtin ? op != CHECK_OP_COUNT :
                        keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_STATIC_ASSERT && count == 1;
            if (supported && !builtin)
            {
                graph->contexts[a] |= BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT;
                graph->contexts[a] |= BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CONDITION)
        {
            // The condition itself is the first value; child statements have already been checked and dropped.
            supported = count == 1;
            const uint32_t keyword = reader->first_child[token];
            op = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION;
        }
        else if (role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_FOR)
        {
            // Discarding an expression still performs its lvalue/array/function conversions. In particular an
            // unused register array cannot decay, and a volatile lvalue remains an observable load.
            supported = !reader->conditions[token] || count <= 1;
            const uint32_t parent = parser->tree.parents[token];
            const uint32_t selected = parent < parser->tokens.count ? reader->output->expression_regions[parent] : 0;
            if (selected != 0 && reader->output->regions[selected - 1].final_statement == token)
            {
                supported &= count == 1;
                reader->output->regions[selected - 1].result = count == 1 ? first : BUSTER_CHECK_NONE;
            }
            uint32_t last_discard = BUSTER_CHECK_NONE;
            for (uint32_t value = first; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
            {
                const uint32_t discard = check_input_add(reader, reader->conditions[token] ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, value, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (discard != BUSTER_CHECK_NONE)
                {
                    graph->types[discard] = BUSTER_TYPE_VOID;
                    if (reader->region_roots != NULL)
                    {
                        if (last_discard == BUSTER_CHECK_NONE)
                        {
                            reader->region_roots[token] = discard;
                        }
                        else
                        {
                            reader->value_next[last_discard] = discard;
                        }
                        last_discard = discard;
                    }
                }
            }
        }
        else if (role == PARSE_ROLE_CLOSE_INITIALIZER)
        {
            list = true;
        }
        else if (role == PARSE_ROLE_CLOSE_COMPOUND_BODY)
        {
            const uint32_t declaration = reader->initializer_declarations[token];
            supported = declaration != BUSTER_CHECK_NONE && reader->output->compound_objects[declaration];
            if (supported)
            {
                node = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (node != BUSTER_CHECK_NONE)
                {
                    *check_graph_leaf_of(graph, node) = (CheckValue)
                    {
                        .type = reader->declared_types[declaration],
                        .category = CHECK_LVALUE,
                        .flags = reader->static_objects[declaration] ? BUSTER_CHECK_STATIC_OBJECT : BUSTER_CHECK_SIDE_EFFECT,
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .object = reader->objects[declaration],
                    };
                }
            }
        }
        else if (role == PARSE_ROLE_RANGE)
        {
            supported = count == 2 && parser->tree.parents[token] < parser->tokens.count && parser->tree.roles[parser->tree.parents[token]] == PARSE_ROLE_CLOSE_CASE;
            list = supported;
        }
        else if (role == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t keyword = reader->first_child[token];
            const bool fallback = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_DEFAULT;
            supported = reader->output->control_owners[token] != BUSTER_CHECK_NONE && (fallback ? count == 0 : count == 1 || count == 2);
            if (supported && !fallback)
            {
                reader->output->token_nodes[token] = a;
                reader->output->case_ranges[token] = count == 2 ? b : BUSTER_CHECK_NONE;
                graph->contexts[a] |= BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
                if (count == 2)
                {
                    graph->contexts[b] |= BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
                }
            }
        }
        else if (role == PARSE_ROLE_CLOSE_STRUCT ||
                 role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_SPECIFIER ||
                 role == PARSE_ROLE_CLOSE_ASM || role == PARSE_ROLE_LABEL || role == PARSE_ROLE_TAG || role == PARSE_ROLE_BITFIELD)
        {
            supported = false;
        }
        else if (kind == PARSE_KIND_STRING)
        {
            // Static-assert messages and concatenated tails are already ignored. Other literals need an operand.
            supported = false;
        }
        else if (kind == PARSE_KIND_GOTO || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_LABEL || kind == PARSE_KIND_SWITCH ||
                 kind == PARSE_KIND_CASE || kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                 kind == PARSE_KIND_ASM || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATOMIC ||
                 kind == PARSE_KIND_CALLING_CONVENTION || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT ||
                 kind == PARSE_KIND_REAL_IMAG || kind == PARSE_KIND_EXTENSION)
        {
            supported = false;
        }
        if (op == CHECK_OP_SIZEOF && supported)
        {
            TypeId requested = reader->query_types[token];
            if (requested == 0)
            {
                CheckValue value = { 0 };
                if (check_input_evaluate_cached(reader, token + 1, reader->ends[token] + 1, &value))
                {
                    requested = value.type;
                }
            }
            if (check_input_vla_runtime_size(reader->types, requested))
            {
                node = check_input_vla_size(reader, token, requested, reader->query_types[token] == 0 ? a : BUSTER_CHECK_NONE);
                op = CHECK_OP_COUNT;
                supported = node != BUSTER_CHECK_NONE;
            }
        }
        if (op != CHECK_OP_COUNT && supported)
        {
            if (op == CHECK_OP_ALIGNOF && reader->query_types[token] == 0 && a != BUSTER_CHECK_NONE)
            {
                const uint32_t operand = graph->tokens[a];
                const uint32_t declaration = operand < parser->tokens.count ? reader->output->declarations[operand] : BUSTER_CHECK_NONE;
                CheckInputAttributes* attributes = declaration < parser->tokens.count ? reader->output->attributes[declaration] : 0;
                if (attributes != 0 && attributes->alignment_log2 != 0)
                {
                    const TypeId type = reader->declared_types[declaration];
                    const uint32_t requested = attributes->alignment_log2 - 1;
                    const uint32_t natural = type_alignment_log2(reader->types, type);
                    reader->query_types[token] = check_input_type_result(reader, type_aligned(reader->types, type, requested > natural ? requested : natural));
                }
            }
            const bool arguments = check_graph_has_arguments(op) && op != CHECK_OP_CALL;
            uint32_t left = arguments ? BUSTER_CHECK_NONE : op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF ?
                            (reader->query_types[token] == 0 && count == 1 ? a : BUSTER_CHECK_NONE) : a;
            if (check_input_complex_builtin((CBuiltinId)reader->builtins[token]))
            {
                left = check_input_add(reader, CHECK_OP_CONVERT, token, left, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                if (left != BUSTER_CHECK_NONE)
                {
                    graph->types[left] = check_input_type_result(reader, type_complex(reader->types, check_input_complex_format((CBuiltinId)reader->builtins[token])));
                }
            }
            const bool one = op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CALL || op == CHECK_OP_CAST || op == CHECK_OP_CONDITION ||
                             op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || role == PARSE_ROLE_PREFIX || role == PARSE_ROLE_POSTFIX;
            const bool omitted = op == CHECK_OP_CONDITIONAL && count == 2;
            uint64_t intrinsic_ice = 0;
            uint32_t intrinsic_offset = 0;
            if (op == CHECK_OP_BUILTIN_X86)
            {
                const CheckInputIntrinsicCall* call = reader->output->intrinsic_calls + reader->output->intrinsic_ids[token] - 1;
                intrinsic_offset = check_input_intrinsic_arguments(reader, call, a, &intrinsic_ice);
            }
            node = check_input_add(reader, op, token, left, one || arguments ? BUSTER_CHECK_NONE : omitted ? a : b,
                                   omitted ? b : op == CHECK_OP_CONDITIONAL || op == CHECK_OP_BUILTIN_OVERFLOW ? c : BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                graph->types[node] = reader->query_types[token];
                if (reader->builtins[token] != 0)
                {
                    graph->members[node] = reader->builtins[token];
                }
                if (op == CHECK_OP_VARARGS)
                {
                    const CheckInputVarargs* call = &reader->varargs[reader->vararg_calls[token] - 1];
                    uint32_t second = b;
                    while (second < graph->count && (graph->ops[second] == CHECK_OP_CAST || graph->ops[second] == CHECK_OP_SELECT))
                    {
                        second = graph->operand0[second];
                    }
                    const uint32_t operand = second < graph->count ? graph->tokens[second] : BUSTER_CHECK_NONE;
                    const uint32_t declaration = operand < parser->tokens.count ? reader->output->declarations[operand] : BUSTER_CHECK_NONE;
                    if (graph->varargs_count < graph->varargs_capacity)
                    {
                        graph->argument_offsets[node] = graph->varargs_count;
                        graph->argument_counts[node] = call->argument_count;
                        graph->varargs_calls[graph->varargs_count++] = (CheckVarargsCall)
                        {
                            .node = node,
                            .second_argument_declaration = declaration,
                            .context = call->context,
                        };
                    }
                    else
                    {
                        reader->output->resource = true;
                    }
                }
                if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
                {
                    graph->members[node] = parser->tokens.names[token + 1];
                    if (op == CHECK_OP_MEMBER)
                    {
                        check_input_vector_member(reader, node, token + 1);
                    }
                }
                if (check_graph_has_arguments(op))
                {
                    if (op == CHECK_OP_CALL && reader->output->overload_heads != 0 && a != BUSTER_CHECK_NONE)
                    {
                        const uint32_t callee = graph->tokens[a];
                        const uint32_t head = callee < parser->tokens.count ? reader->output->overload_heads[callee] : 0;
                        if (head != 0)
                        {
                            if (graph->overload_count < graph->overload_capacity)
                            {
                                graph->overload_calls[graph->overload_count++] = (CheckOverloadCall){ .node = node, .head = head - 1 };
                            }
                            else
                            {
                                reader->output->resource = true;
                            }
                        }
                    }
                    graph->argument_offsets[node] = op == CHECK_OP_BUILTIN_X86 ? intrinsic_offset : graph->argument_count;
                    graph->argument_counts[node] = count - (op == CHECK_OP_CALL);
                    for (uint32_t argument = op == CHECK_OP_BUILTIN_X86 ? BUSTER_CHECK_NONE : op == CHECK_OP_CALL ? b : a;
                         argument != BUSTER_CHECK_NONE; argument = reader->value_next[argument])
                    {
                        if (graph->argument_count < graph->argument_capacity)
                        {
                            graph->argument_tokens[graph->argument_count] = check_input_first_token(reader, graph->tokens[argument]);
                            graph->argument_nodes[graph->argument_count++] = argument;
                        }
                        else
                        {
                            reader->output->resource = true;
                        }
                    }
                    if (op == CHECK_OP_BUILTIN_X86)
                    {
                        const CheckInputIntrinsicCall* call = reader->output->intrinsic_calls + reader->output->intrinsic_ids[token] - 1;
                        const CheckInputIntrinsicDeclaration* declaration = reader->output->intrinsic_declarations + call->declaration;
                        if (graph->x86_count < graph->x86_capacity)
                        {
                            graph->x86_calls[graph->x86_count++] = (CheckX86Call)
                            {
                                .node = node,
                                .info = declaration->info,
                                .required_ice_arguments = declaration->types.ice_parameters,
                                .ice_arguments = intrinsic_ice,
                                .reachability = CHECK_BUILTIN_X86_UNKNOWN,
                                .constant_evaluated_context = reader->constant_contexts[token] != 0,
                            };
                        }
                        else
                        {
                            reader->output->resource = true;
                        }
                    }
                    if (op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
                        (op == CHECK_OP_BUILTIN_VECTOR && reader->builtins[token] == CBUILTIN_SHUFFLEVECTOR))
                    {
                        if (graph->builtin_count < graph->builtin_capacity)
                        {
                            const uint32_t keyword = reader->first_child[token];
                            const uint32_t begin = keyword < token ? keyword + 2 : token;
                            const uint32_t end = check_constant_punctuation(parser, begin, token, PARSE_KIND_COMMA);
                            CheckBuiltinCall* evidence = &graph->builtin_calls[graph->builtin_count++];
                            *evidence = (CheckBuiltinCall){ .node = node };
                            if (op == CHECK_OP_BUILTIN_VECTOR)
                            {
                                check_input_shuffle_evidence(reader, keyword, token, count, evidence);
                            }
                            else
                            {
                                check_input_builtin_evidence(reader, begin, end, evidence);
                            }
                            if (reader->builtins[token] == CBUILTIN_CONSTANT_P && count == 1)
                            {
                                evidence->constant_p_side_effects = check_input_builtin_syntax_effects(reader, a);
                            }
                        }
                        else
                        {
                            reader->output->resource = true;
                        }
                    }
                }
                if (reader->static_initializers[token] && op != CHECK_OP_INITIALIZE)
                {
                    graph->contexts[node] |= BUSTER_CHECK_CONTEXT_STATIC;
                }
                if (op == CHECK_OP_INITIALIZE)
                {
                    reader->output->initializer_nodes[reader->initializer_declarations[token]] = node;
                }
            }
        }
        if (scalar)
        {
            // What the vector path's classes of the token, made before the walk, would have missed: the walk acts at a
            // token they say it doesn't, or a sparse column has an entry for one they say has none.
            const bool acted = node != BUSTER_CHECK_NONE || list || !supported || op != CHECK_OP_COUNT || count != 0;
            reader->output->visit_misses += ((classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0 && acted) ||
                                            ((classes & BUSTER_CHECK_EXPRESSION_SPARSE) == 0 && check_input_expression_sparse(reader, token));
        }
        if (!supported && reader->output->report_count == 0)
        {
            check_input_report(reader, token, CHECK_INPUT_UNSUPPORTED);
        }
        if (node != BUSTER_CHECK_NONE)
        {
            if (reader->output->overload_heads != 0)
            {
                const CheckOp actual = (CheckOp)graph->ops[node];
                const uint32_t child = graph->operand0[node];
                const uint32_t child_token = child != BUSTER_CHECK_NONE ? graph->tokens[child] : parser->tokens.count;
                reader->output->overload_heads[token] = actual == CHECK_OP_VALUE ? check_graph_leaf_of(graph, node)->overload_head :
                    (actual == CHECK_OP_ADDRESS || actual == CHECK_OP_SELECT) && child_token < parser->tokens.count ? reader->output->overload_heads[child_token] : 0;
            }
            if (reader->constant_contexts[token] && graph->ops[node] == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_EXPECT)
            {
                const uint32_t keyword = reader->first_child[token];
                CheckValue constant = { 0 };
                if (keyword < token && check_input_evaluate_cached(reader, keyword, token + 1, &constant) &&
                    constant.constant_kind != CHECK_CONSTANT_NONE && constant.fault == CHECK_FAULT_NONE && (constant.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                {
                    // The original call and both operands remain checked. Only a constant-required consumer
                    // receives the independent source-evaluator result; runtime calls retain both evaluations.
                    const uint32_t proven = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                    if (proven != BUSTER_CHECK_NONE)
                    {
                        *check_graph_leaf_of(graph, proven) = constant;
                        node = proven;
                    }
                }
            }
            reader->output->token_nodes[token] = node;
            if (role != PARSE_ROLE_CLOSE_CONDITION && role != PARSE_ROLE_RETURN)
            {
                reader->first_value[token] = node;
                reader->last_value[token] = node;
                reader->value_counts[token] = 1;
            }
        }
        else if (list)
        {
            reader->output->token_nodes[token] = count == 1 ? first : BUSTER_CHECK_NONE;
            reader->first_value[token] = first;
            reader->last_value[token] = last;
            reader->value_counts[token] = count;
        }
    }
}

// At translation-unit end a tentative object must have acquired a complete type, except an incomplete array
// which the static-data finalizer completes to one element. Expression completeness is checked by each action
// using the retained source-position history, so a pointer to an opaque type is itself a valid complete object.
static void check_input_complete_use(CheckInputReader* reader, uint32_t token)
{
    if (reader->output->tentative[token] && !type_is_complete(reader->types, reader->declared_types[token]) &&
        type_kind(reader->types, reader->declared_types[token]) != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
    {
        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
    }
}

// The vector path visits the tentative definitions alone (check_input_scan_nonzero), the scalar path every token: both
// report in token order.
static void check_input_complete_uses(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t first = 0; first < count; first += 64)
    {
        uint64_t selected = BUSTER_SCALAR_PATH(parser->options.scalar) ? check_input_scan_all(first, count) : check_input_scan_nonzero(reader->output->tentative, first, count);
        for (; selected != 0; selected &= selected - 1)
        {
            check_input_complete_use(reader, first + (uint32_t)__builtin_ctzll(selected));
        }
    }
}

#include "c_check_binding.c"
#include "c_check_packed_input.c"
#include "c_check_x86_input.c"
#include "c_check_naked_input.c"

// Which tokens are written in a system header, where clang suppresses its warnings that default to errors
// (BUSTER_CHECK_EXTENSION_SYSTEM_HEADER): the system level of each output token's expansion location, its file and the
// line notes before it. The preprocessor gives every output token's level from the runs its main loop kept
// (pp_system_levels), or by the expansion history: for --scalar, its twin, and for a unit whose runs can't say
// (Preprocessor.system_runs_inexact), recording the history first if the unit put it off (pp_history_ensure).
static void check_input_system_tokens(CheckInputReader* reader, Preprocessor* pp, Arena* arena)
{
    const Parser* parser = reader->parser;
    CheckGraph* graph = &reader->output->graph;
    const uint32_t count = parser->tokens.count;
    bool system = false;
    for (uint32_t i = 0; !system && i < pp->range_count; i += 1)
    {
        system = pp->ranges[i].system != BUSTER_PP_USER_FILE;
    }
    for (uint32_t i = 0; !system && i < pp->line_directive_count; i += 1)
    {
        system = pp->line_directives[i].system != BUSTER_PP_USER_FILE;
    }
    const uint64_t mark = reader->scratch->length;
    uint8_t* tokens = system && pp->output.locations != NULL ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint8_t), 64) : NULL;
    uint8_t* levels = tokens != NULL ? arena_push_array(reader->scratch, (uint64_t)pp->output.count + 1, sizeof(uint8_t), 64) : NULL;
    reader->output->resource |= system && pp->output.locations != NULL && levels == NULL;
    if (levels != NULL)
    {
        if (BUSTER_SCALAR_PATH(parser->options.scalar) || pp->system_runs_inexact)
        {
            pp_history_ensure(pp);
            pp_system_levels_history(pp, levels);
        }
        else
        {
            pp_system_levels(pp, levels);
        }
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t source = parser->tokens.sources[token];
            const uint8_t level = source < pp->output.count ? levels[source] : BUSTER_PP_USER_FILE;
            tokens[token] = level == BUSTER_PP_SYSTEM_FILE || level == BUSTER_PP_EXTERN_C_FILE;
        }
        tokens[count] = 0;
        graph->system_tokens = tokens;
        graph->system_token_count = count;
    }
    arena_pop_to(reader->scratch, mark);
}

// Whether a builtin's calls take one of the graph's builtin call records: a floating, query or atomic one's, or
// __builtin_shufflevector's.
static uint32_t check_input_builtin_recorded(uint16_t builtin)
{
    const CheckOp op = check_graph_builtin_op((CBuiltinId)builtin);
    return op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
           (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_SHUFFLEVECTOR);
}

// The graph's builtin call records the reader's `builtins` (a builtin per token of `count`, 0 for none) need. Nearly
// every token has none, so the vector path tests 32 tokens at once and reads only those with a builtin; the scalar path,
// its twin, reads every token's.
static uint32_t check_input_builtin_capacity(const uint16_t* builtins, uint32_t count, bool scalar)
{
    (void)scalar;

    uint32_t result = 0;
    uint32_t token = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && token < count; token += 32)
    {
        const __mmask32 lanes = (__mmask32)_bzhi_u32(UINT32_MAX, count - token < 32 ? count - token : 32);
        const __m512i ids = _mm512_maskz_loadu_epi16(lanes, builtins + token);
        uint32_t present = (uint32_t)_mm512_test_epi16_mask(ids, ids);
        while (present != 0)
        {
            result += check_input_builtin_recorded(builtins[token + (uint32_t)__builtin_ctz(present)]);
            present &= present - 1;
        }
    }
    for (; token < count; token += 1)
    {
        result += check_input_builtin_recorded(builtins[token]);
    }
    return result;
}

// The constant context a token opens, if any: a static object's initializer, a static assertion's condition or a case
// label's value.
static void check_input_constant_context(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    const uint32_t begin = input->initializer_begins[token];
    const uint32_t end = input->initializer_ends[token];
    if (input->static_objects[token] && begin < end && end <= count)
    {
        memset(reader->constant_contexts + begin, 1, end - begin);
    }
    if (parser->tokens.kinds[token] == PARSE_KIND_STATIC_ASSERT && token + 1 < count)
    {
        const uint32_t close = parser->tree.parents[token + 1];
        if (close > token + 1 && close < count)
        {
            const uint32_t separator = check_constant_punctuation(parser, token + 2, close, PARSE_KIND_COMMA);
            memset(reader->constant_contexts + token + 2, 1, separator - token - 2);
            if (separator < close)
            {
                // A message contributes no expression value. Mark its span once instead of walking the parser
                // ancestors of every string, which is quadratic in a long initializer's separator chain (#593).
                memset(reader->ignored + separator + 1, 1, close - separator - 1);
            }
        }
    }
    if (parser->tokens.kinds[token] == PARSE_KIND_CASE)
    {
        const uint32_t close = parser->tree.parents[token];
        if (close > token && close < count)
        {
            memset(reader->constant_contexts + token + 1, 1, close - token - 1);
        }
    }
}

// The constant contexts of every token, and the static-assert message spans that contribute no expression value.
// Only static objects, static assertions and case labels open one, so the vector path visits those alone, the scalar
// path every token. Neither the context writes nor the ignored message spans affect the selection scans.
static void check_input_constant_contexts(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t first = 0; first < count; first += 64)
    {
        uint64_t selected = BUSTER_SCALAR_PATH(parser->options.scalar) ? check_input_scan_all(first, count) :
                            check_input_scan_nonzero(reader->output->static_objects, first, count) |
                            check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_STATIC_ASSERT) |
                            check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_CASE);
        for (; selected != 0; selected &= selected - 1)
        {
            check_input_constant_context(reader, first + (uint32_t)__builtin_ctzll(selected));
        }
    }
    if (reader->warning_index_ready)
    {
        // Static contexts may replace the earlier independent-bound bit. Reopen the compact index before later
        // intrinsic constant evaluation, which must see the current contexts and any newly clearable warnings.
        memset(reader->warning_index, 0xff, (uint64_t)reader->warning_index_base * 2 * sizeof(uint16_t));
    }
}

// The groups of names check_input_count_tokens counts the tokens of.
typedef enum CheckInputCountGroup
{
    // __builtin_types_compatible_p and __builtin_offsetof, whose values the reader caches.
    CHECK_INPUT_COUNT_BUILTIN_CONSTANTS,
    // The va_start, va_arg, va_copy and va_end builtins'.
    CHECK_INPUT_COUNT_VARARGS,
    // transparent_union's two spellings.
    CHECK_INPUT_COUNT_TRANSPARENT_UNIONS,
    // overloadable's two spellings.
    CHECK_INPUT_COUNT_OVERLOADABLE,
    // _Generic and __builtin_choose_expr, which check_input_selections reads.
    CHECK_INPUT_COUNT_SELECTIONS,
    CHECK_INPUT_COUNT_GROUPS,
} CheckInputCountGroup;

#define BUSTER_CHECK_INPUT_COUNT_NAMES 16u

// What check_input_run sizes its tables by: the tokens of a few kinds and roles, and of the names it is given, each
// name (0 for one the unit hasn't) in one of CHECK_INPUT_COUNT_GROUPS.
typedef struct CheckInputTokenCounts CheckInputTokenCounts;
struct CheckInputTokenCounts
{
    // `[`, `asm` and `sizeof` (with _Alignof's, of the same kind) tokens, and `.` as a member operator.
    uint32_t subscripts;
    uint32_t asms;
    uint32_t size_queries;
    uint32_t vector_members;
    // The operands, and the closing parentheses of calls and function declarators: the overloads' room.
    uint32_t operands;
    uint32_t calls;
    uint32_t groups[CHECK_INPUT_COUNT_GROUPS];
};

// A token's syntax bits as the walk's own tests give them: the scalar path's, the vector path's twin.
static uint8_t check_input_syntax_of(const Parser* parser, uint32_t token)
{
    const uint32_t name = parser->tokens.names[token];
    const bool named = name == C_NAME_BUILTIN_VA_ARG || name == C_NAME_BUILTIN_CONVERTVECTOR || name == C_NAME_BUILTIN_BIT_CAST || name == C_NAME_GENERIC ||
                       name == C_NAME_BUILTIN_CHOOSE_EXPR || name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || name == C_NAME_BUILTIN_OFFSETOF;
    const bool string = parser->tokens.kinds[token] == PARSE_KIND_STRING || check_input_function_name_token(parser, token);
    return (uint8_t)((named ? BUSTER_CHECK_SYNTAX_NAMED : 0) | (check_input_attribute_starts(parser, token, parser->tokens.count) ? BUSTER_CHECK_SYNTAX_ATTRIBUTE : 0) |
                     (string ? BUSTER_CHECK_SYNTAX_STRING : 0));
}

// One pass over the tokens' kinds, roles and names for every count check_input_run needs before it reads (#529), where
// it made one pass for each, and for the walk's syntax bits of each token, which `syntax` (count + 1 bytes, or null for
// none) receives, and its marks (check_input_visit_of), which `visits` (count + 1 bytes, or null for none) receives.
// The vector path tests 64 tokens' kinds and roles and 16 tokens' names at once, and looks up 64 roles' and kinds'
// tests in tables (check_input_role_tests); the scalar path, its twin, tests a token at a time. A name of 0 counts no
// token: tokens without a name have 0. Without roles, every token is marked.
static __attribute__((noinline)) CheckInputTokenCounts check_input_count_tokens(const Parser* parser, const uint32_t* names, const uint8_t* groups,
                                                                              uint32_t name_count, uint8_t* syntax, uint8_t* visits, bool scalar)
{
    (void)scalar;

    // The names of the syntax bits: BUSTER_CHECK_SYNTAX_NAMED's, then _ATTRIBUTE's, then _STRING's.
    static const uint32_t syntax_names[] =
    {
        C_NAME_BUILTIN_VA_ARG,
        C_NAME_BUILTIN_CONVERTVECTOR,
        C_NAME_BUILTIN_BIT_CAST,
        C_NAME_GENERIC,
        C_NAME_BUILTIN_CHOOSE_EXPR,
        C_NAME_BUILTIN_TYPES_COMPATIBLE_P,
        C_NAME_BUILTIN_OFFSETOF,
        C_NAME_NORETURN,
        C_NAME_NONNULL,
        C_NAME_NULLABLE,
        C_NAME_NULLABLE_RESULT,
        C_NAME_NULL_UNSPECIFIED,
        C_NAME_FUNC,
        C_NAME_FUNCTION,
        C_NAME_PRETTY_FUNCTION,
    };
    CheckInputTokenCounts result = { 0 };
    const uint32_t count = parser->tokens.count;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* token_names = parser->tokens.names;
    const __m512i role_tests_low = _mm512_load_si512(check_input_role_tests);
    const __m512i role_tests_high = _mm512_load_si512(check_input_role_tests + 64);
    const __m512i kind_tests_low = _mm512_load_si512(check_input_kind_tests);
    const __m512i kind_tests_high = _mm512_load_si512(check_input_kind_tests + 64);
    uint32_t token = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && token < count; token += 64)
    {
        const uint32_t length = count - token < 64 ? count - token : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + token);
        const __m512i role = roles != 0 ? _mm512_maskz_loadu_epi8(lanes, roles + token) : _mm512_setzero_si512();
        result.subscripts += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_L_SQUARE)));
        result.asms += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_ASM)));
        result.size_queries += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_SIZEOF)));
        if (roles != 0)
        {
            const __mmask64 periods = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_PERIOD));
            result.vector_members += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(periods, role, _mm512_set1_epi8(PARSE_ROLE_MEMBER_OPERATOR)));
            result.operands += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, role, _mm512_set1_epi8(PARSE_ROLE_OPERAND)));
            result.calls += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(lanes, role, _mm512_set1_epi8(PARSE_ROLE_CLOSE_CALL)) |
                                                           _mm512_mask_cmpeq_epi8_mask(lanes, role, _mm512_set1_epi8(PARSE_ROLE_CLOSE_FUNCTION)));
        }
        // An attribute's `[[` is a `[` whose successor, the next lane or the next block's first token, is one too.
        const uint32_t following = count - token - 1 < 64 ? count - token - 1 : 64;
        const __mmask64 successors = _bzhi_u64(UINT64_MAX, following);
        const __m512i next = _mm512_maskz_loadu_epi8(successors, kinds + token + 1);
        uint64_t attributes = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_ATTRIBUTE)) |
                              _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_ALIGNAS)) |
                              _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_ASM)) |
                              _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_CALLING_CONVENTION)) |
                              (_mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_L_SQUARE)) &
                               _mm512_mask_cmpeq_epi8_mask(successors, next, _mm512_set1_epi8(PARSE_KIND_L_SQUARE)));
        uint64_t strings = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_STRING));
        uint64_t named = 0;
        for (uint32_t part = 0; part < length; part += 16)
        {
            const __mmask16 parts = (__mmask16)_bzhi_u32(UINT32_MAX, length - part < 16 ? length - part : 16);
            const __m512i name = _mm512_maskz_loadu_epi32(parts, token_names + token + part);
            const __mmask16 present = _mm512_mask_test_epi32_mask(parts, name, name);
            __mmask16 matched[CHECK_INPUT_COUNT_GROUPS] = { 0 };
            for (uint32_t i = 0; i < name_count; i += 1)
            {
                matched[groups[i]] |= _mm512_mask_cmpeq_epi32_mask(present, name, _mm512_set1_epi32((int32_t)names[i]));
            }
            for (uint32_t group = 0; group < CHECK_INPUT_COUNT_GROUPS; group += 1)
            {
                result.groups[group] += (uint32_t)__builtin_popcount(matched[group]);
            }
            __mmask16 syntax_matched[3] = { 0 };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(syntax_names); i += 1)
            {
                syntax_matched[i < 7 ? 0 : i < 12 ? 1 : 2] |= _mm512_mask_cmpeq_epi32_mask(present, name, _mm512_set1_epi32((int32_t)syntax_names[i]));
            }
            named |= (uint64_t)syntax_matched[0] << part;
            attributes |= (uint64_t)syntax_matched[1] << part;
            strings |= (uint64_t)syntax_matched[2] << part;
        }
        if (syntax != 0)
        {
            const __m512i bits = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi8(named, _mm512_set1_epi8(BUSTER_CHECK_SYNTAX_NAMED)),
                                                                 _mm512_maskz_mov_epi8(attributes, _mm512_set1_epi8(BUSTER_CHECK_SYNTAX_ATTRIBUTE))),
                                                 _mm512_maskz_mov_epi8(strings, _mm512_set1_epi8(BUSTER_CHECK_SYNTAX_STRING)));
            _mm512_mask_storeu_epi8(syntax + token, lanes, bits);
        }
        if (visits != 0)
        {
            // The walk in source order's marks (check_input_visit_of): the tables' tests of each token's role and kind,
            // and of its successor's, past the block's last lane too, which a token without one takes as no role and no
            // kind; and the syntax bits.
            uint64_t visit = lanes;
            if (roles != 0)
            {
                const __m512i next_role = _mm512_maskz_loadu_epi8(successors, roles + token + 1);
                const __m512i role_tests = _mm512_permutex2var_epi8(role_tests_low, role, role_tests_high);
                const __m512i kind_tests = _mm512_permutex2var_epi8(kind_tests_low, kind, kind_tests_high);
                const __m512i next_role_tests = _mm512_permutex2var_epi8(role_tests_low, next_role, role_tests_high);
                const __m512i next_kind_tests = _mm512_permutex2var_epi8(kind_tests_low, next, kind_tests_high);
                const uint64_t extension = _mm512_test_epi8_mask(kind_tests, _mm512_set1_epi8(BUSTER_CHECK_KIND_EXTENSION)) &
                                           (_mm512_test_epi8_mask(next_role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_SPECIFIER)) |
                                            _mm512_test_epi8_mask(next_kind_tests, _mm512_set1_epi8(BUSTER_CHECK_KIND_TAG)));
                const uint64_t group = _mm512_test_epi8_mask(role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_GROUP)) &
                                       _mm512_test_epi8_mask(next_role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_BLOCK));
                const uint64_t name = _mm512_test_epi8_mask(kind_tests, _mm512_set1_epi8(BUSTER_CHECK_KIND_IDENTIFIER)) &
                                      ~_mm512_test_epi8_mask(role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_UNNAMED));
                visit = lanes & (_mm512_test_epi8_mask(role_tests, _mm512_set1_epi8(BUSTER_CHECK_ROLE_RESOLVE)) |
                                 _mm512_test_epi8_mask(kind_tests, _mm512_set1_epi8(BUSTER_CHECK_KIND_RESOLVE)) | extension | group | name |
                                 named | attributes | strings);
            }
            _mm512_mask_storeu_epi8(visits + token, lanes, _mm512_maskz_mov_epi8(visit, _mm512_set1_epi8(BUSTER_CHECK_RESOLVE_SYNTAX)));
        }
    }
    for (; token < count; token += 1)
    {
        result.subscripts += kinds[token] == PARSE_KIND_L_SQUARE;
        result.asms += kinds[token] == PARSE_KIND_ASM;
        result.size_queries += kinds[token] == PARSE_KIND_SIZEOF;
        result.vector_members += roles != 0 && roles[token] == PARSE_ROLE_MEMBER_OPERATOR && kinds[token] == PARSE_KIND_PERIOD;
        result.operands += roles != 0 && roles[token] == PARSE_ROLE_OPERAND;
        result.calls += roles != 0 && (roles[token] == PARSE_ROLE_CLOSE_CALL || roles[token] == PARSE_ROLE_CLOSE_FUNCTION);
        const uint32_t name = token_names[token];
        bool matched[CHECK_INPUT_COUNT_GROUPS] = { 0 };
        for (uint32_t i = 0; name != 0 && i < name_count; i += 1)
        {
            matched[groups[i]] |= name == names[i];
        }
        for (uint32_t group = 0; group < CHECK_INPUT_COUNT_GROUPS; group += 1)
        {
            result.groups[group] += matched[group];
        }
        const uint8_t bits = check_input_syntax_of(parser, token);
        if (syntax != 0)
        {
            syntax[token] = bits;
        }
        if (visits != 0)
        {
            visits[token] = roles == 0 || check_input_visit_of(parser, bits, token) ? BUSTER_CHECK_RESOLVE_SYNTAX : 0;
        }
    }
    if (syntax != 0)
    {
        syntax[count] = 0;
    }
    if (visits != 0)
    {
        visits[count] = 0;
    }
    return result;
}

// `environment` publishes #113's environment of the unit (CheckInput.environment, check_environment_publish), which the
// deferred reader's tests and the corpus's comparisons read; a compile, which reads its unit in source order, doesn't.
static bool check_input_run(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                            bool complete_translation_unit, uint32_t original_end, const uint8_t* deferred_tokens, CheckInputBindingMode binding_mode,
                            CheckDeferred* deferred, bool environment)
{
    *input = (CheckInput){ 0 };
    // Plain char's signedness, which every phase after the reader reads from the table too.
    types->char_unsigned = pp->options.predefines.char_unsigned;
    const uint32_t count = parser->tokens.count;
    input->token_count = count;
    const char* const vararg_spellings[] =
    {
        "__builtin_va_start",
        "__builtin_c23_va_start",
        "__builtin_va_arg",
        "__builtin_va_copy",
        "__builtin_va_end",
    };
    const char* const spellings[] =
    {
        "transparent_union",
        "__transparent_union__",
        "overloadable",
        "__overloadable__",
    };
    uint32_t names[BUSTER_CHECK_INPUT_COUNT_NAMES] = { C_NAME_BUILTIN_TYPES_COMPATIBLE_P, C_NAME_BUILTIN_OFFSETOF, C_NAME_GENERIC, C_NAME_BUILTIN_CHOOSE_EXPR };
    uint8_t groups[BUSTER_CHECK_INPUT_COUNT_NAMES] =
    {
        CHECK_INPUT_COUNT_BUILTIN_CONSTANTS,
        CHECK_INPUT_COUNT_BUILTIN_CONSTANTS,
        CHECK_INPUT_COUNT_SELECTIONS,
        CHECK_INPUT_COUNT_SELECTIONS,
    };
    uint32_t name_count = 4;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(vararg_spellings); i += 1)
    {
        names[name_count] = intern_find(pp->table, (const uint8_t*)vararg_spellings[i], (uint32_t)strlen(vararg_spellings[i]));
        groups[name_count++] = CHECK_INPUT_COUNT_VARARGS;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
    {
        names[name_count] = intern_find(pp->table, (const uint8_t*)spellings[i], (uint32_t)strlen(spellings[i]));
        groups[name_count++] = i < 2 ? CHECK_INPUT_COUNT_TRANSPARENT_UNIONS : CHECK_INPUT_COUNT_OVERLOADABLE;
    }
    const uint64_t mark = scratch->length;
    uint8_t* syntax = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint8_t), 64);
    uint8_t* visits = syntax != 0 ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint8_t), 64) : 0;
    const CheckInputTokenCounts counts = check_input_count_tokens(parser, names, groups, name_count, syntax, visits, BUSTER_SCALAR_PATH(parser->options.scalar));
    input->vla_capacity = counts.subscripts;
    input->asm_capacity = counts.asms;
    const uint32_t vector_members = counts.vector_members;
    const bool transparent_unions = counts.groups[CHECK_INPUT_COUNT_TRANSPARENT_UNIONS] != 0;
    const uint64_t graph_capacity = (uint64_t)count + (uint64_t)input->vla_capacity * 8 + (uint64_t)counts.size_queries * 4 + 1;
    CheckInputReader reader =
    {
        .output = input,
        .parser = parser,
        .pp = pp,
        .types = types,
        .arena = arena,
        .scratch = scratch,
        .function_body = BUSTER_CHECK_NONE,
        .function_name_body = BUSTER_CHECK_NONE,
        .function_name_ids = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
        .file_function_name_ids = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
        .specifier_owner = BUSTER_CHECK_NONE,
        .resolve_limit = count + 1,
        .resolve_pragma = BUSTER_CHECK_NONE,
        .resolve_diagnostic_token = BUSTER_CHECK_NONE,
        .resolve_position = BUSTER_CHECK_NONE,
        .resolve_message = CHECK_INPUT_UNSUPPORTED,
        .expression_limit = count,
        .fragment_end = original_end,
        .deferred = deferred,
        .syntax = syntax,
        .visits = visits,
    };
    for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        reader.predefined_positions[i] = BUSTER_CHECK_NONE;
    }
    bool valid = visits != 0 && arena != scratch && parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE && (parser->tree.parents != 0 || count == 0) &&
                 count < UINT32_MAX / 4 && graph_capacity <= UINT32_MAX && original_end <= count;
    const char* const builtin_members[] =
    {
        "__va_list_tag",
        "gp_offset",
        "fp_offset",
        "overflow_arg_area",
        "reg_save_area",
        "__NSConstantString_tag",
        "isa",
        "flags",
        "str",
        "length",
    };
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(builtin_members); i += 1)
    {
        reader.builtin_member_names[i] = intern_find_or_add(pp->table, (const uint8_t*)builtin_members[i], (uint32_t)strlen(builtin_members[i]));
        valid &= reader.builtin_member_names[i] != 0;
    }
    for (uint32_t i = 0; valid && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
    {
        reader.predefined_names[i] = intern_find(pp->table, (const uint8_t*)parse_predeclared_typedefs[i], (uint32_t)strlen(parse_predeclared_typedefs[i]));
    }
    input->report_capacity = count + 1;
    input->reports = valid ? arena_push_array(arena, input->report_capacity, sizeof(CheckInputReport), 4) : 0;
    input->declarations = input->reports != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    input->declared_types = input->declarations != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(TypeId), 4) : 0;
    input->declaration_kinds = input->declared_types != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint8_t), 1) : 0;
    input->objects = input->declaration_kinds != 0 ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    valid &= input->objects != 0;
#define BUSTER_CHECK_OUTPUT_ARRAY(field, type) input->field = valid ? arena_push_array(arena, (uint64_t)count + 1, sizeof(type), 8) : 0; valid &= input->field != 0
    BUSTER_CHECK_OUTPUT_ARRAY(static_objects, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(function_flags, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(explicit_abis, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(registers, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(thread_local, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(compound_objects, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(attributes, CheckInputAttributes*);
    BUSTER_CHECK_OUTPUT_ARRAY(linkage, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(defined, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(tentative, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(initializer_nodes, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(initializer_begins, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(initializer_ends, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(token_nodes, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(constant_warnings, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(constant_warning_ignored, uint8_t);
    BUSTER_CHECK_OUTPUT_ARRAY(body_declarations, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(declaration_bodies, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(parameters, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(parameter_next, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(control_owners, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(label_targets, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(case_ranges, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(regions, CheckInputRegion);
    BUSTER_CHECK_OUTPUT_ARRAY(expression_regions, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(asm_ids, uint32_t);
    BUSTER_CHECK_OUTPUT_ARRAY(strings, CheckInitString);
    BUSTER_CHECK_OUTPUT_ARRAY(string_ids, uint32_t);
#undef BUSTER_CHECK_OUTPUT_ARRAY
    if (valid && deferred_tokens != 0)
    {
        uint8_t* retained = arena_push_array(arena, (uint64_t)count + 1, sizeof(uint8_t), 1);
        valid = retained != 0;
        if (valid)
        {
            memcpy(retained, deferred_tokens, (uint64_t)count + 1);
            input->deferred_tokens = retained;
        }
    }
    reader.declared_types = input->declared_types;
    reader.declaration_kinds = input->declaration_kinds;
    reader.objects = input->objects;
    reader.static_objects = input->static_objects;
    reader.registers = input->registers;
    reader.linkage = input->linkage;
    reader.defined = input->defined;
    reader.parameters = input->parameters;
    reader.parameter_next = input->parameter_next;
    valid &= check_graph_initialize(&input->graph, arena, (uint32_t)graph_capacity, count + 1);
    input->graph.extensions = check_input_expression_extensions(parser);
    input->graph.warning_suppressed = valid ? arena_push_array(arena, graph_capacity, sizeof(uint8_t), 1) : 0;
    valid &= input->graph.warning_suppressed != 0;
    input->graph.argument_tokens = valid ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
    valid &= input->graph.argument_tokens != 0;
    if (valid && vector_members != 0)
    {
        input->graph.vector_member_capacity = vector_members;
        input->graph.vector_members = arena_push_array(arena, vector_members, sizeof(CheckVectorCall), _Alignof(CheckVectorCall));
        valid = input->graph.vector_members != 0;
    }
    // The columns that most tokens have an entry in, or that the walk writes at most of its pages, in the scratch, whose
    // pages the parse's temporaries took first (ParseOptions.scratch) and the later stages take again.
#define BUSTER_CHECK_INPUT_ARRAY(field, type) reader.field = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(type), 64) : 0; valid &= reader.field != 0
    BUSTER_CHECK_INPUT_ARRAY(query_types, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(function_specifiers, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(return_types, TypeId);
    BUSTER_CHECK_INPUT_ARRAY(definition_seen, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(ignored, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(initializers, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(initializer_declarations, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(initializer_finish_heads, uint32_t);
    reader.string_ids = input->string_ids;

    BUSTER_CHECK_INPUT_ARRAY(selected_tokens, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(expression_commas, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(conditions, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(constant_contexts, uint8_t);
    BUSTER_CHECK_INPUT_ARRAY(activate, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(activation_next, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(parameter_previous, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(parameter_declarations, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(first_value, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(last_value, uint32_t);
    BUSTER_CHECK_INPUT_ARRAY(value_counts, uint32_t);
#undef BUSTER_CHECK_INPUT_ARRAY
    reader.warning_index_base = 1;
    while ((uint64_t)reader.warning_index_base * BUSTER_CHECK_WARNING_BLOCK < count)
    {
        reader.warning_index_base *= 2;
    }
    reader.warning_index = valid ? arena_push_array(scratch, (uint64_t)reader.warning_index_base * 2, sizeof(uint16_t), 64) : 0;
    valid &= reader.warning_index != 0;
    // The tree's traversal: the parser's where it derived it (parse_tree_columns), else the reader's own, in its scratch,
    // derived below once the columns are reserved: five columns with room for parse_tree_columns' sentinel, and the
    // passes' two temporaries, pushed last.
    const bool derived = parser->postorder != 0 && parser->positions != 0 && parser->ends != 0 && parser->first_children != 0 && parser->next_siblings != 0;
    const uint64_t slots = (uint64_t)count + 1;
    uint32_t* traversal = derived || !valid ? 0 : arena_push_array(scratch, 5 * slots, sizeof(uint32_t), 64);
    valid &= derived || traversal != 0;
    reader.postorder = derived ? parser->postorder : parser->postorder != 0 ? parser->postorder : traversal;
    reader.postorder_positions = derived ? parser->positions : traversal != 0 ? traversal + slots : 0;
    reader.ends = derived ? parser->ends : traversal != 0 ? traversal + 2 * slots : 0;
    reader.first_child = derived ? parser->first_children : traversal != 0 ? traversal + 3 * slots : 0;
    reader.next_child = derived ? parser->next_siblings : traversal != 0 ? traversal + 4 * slots : 0;
    uint32_t* sizes = parser->sizes != 0 || !valid ? 0 : arena_push_array(scratch, slots, sizeof(uint32_t), 64);
    valid &= parser->sizes != 0 || sizes != 0;
    reader.subtree_sizes = parser->sizes != 0 ? parser->sizes : sizes;
    reader.value_next = valid ? arena_push_array(scratch, graph_capacity, sizeof(uint32_t), 64) : 0;
    valid &= reader.value_next != 0;
    // The columns few tokens have an entry in, and the binding table, which has room for four events a token and fills
    // a few at its start, in an arena of their own that the reader unmaps when it returns (#572). Its pages are new and
    // zero, so a column that is zero where nothing is written needs no clearing, and takes memory only where the walk
    // writes it: where the walk only reads it, the kernel maps its zero page. In the scratch, which the later stages take
    // again, they made the reader's pages the most any stage touched (186 MB of sqlite3.c's, against 72 MB). The pages
    // are huge, since the walk reads most of these columns at every token, which in base pages would fault each one;
    // but the scope stacks and the enumeration constants' values, which only the tokens that write them read, are in
    // base pages at the arena's start.
#define BUSTER_CHECK_SPARSE_SMALL_COLUMNS(X) \
    X(scope_marks, uint32_t) \
    X(scope_ends, uint32_t) \
    X(scope_openings, uint32_t) \
    X(enum_values, unsigned __int128)
#define BUSTER_CHECK_SPARSE_COLUMNS(X) \
    X(specifier_jobs, CheckInputSpecifierJob*) \
    X(specifier_finishes, CheckInputSpecifierJob*) \
    X(attribute_bases, CheckInputAttributes*) \
    X(declaration_bases, TypeId) \
    X(declaration_storage, uint32_t) \
    X(declaration_flags, uint8_t) \
    X(declaration_abis, uint8_t) \
    X(static_initializers, uint8_t) \
    X(initializer_finish_next, uint32_t) \
    X(incomplete_array_types, TypeId) \
    X(designator_indices, uint64_t) \
    X(designator_valid, uint8_t) \
    X(designator_tokens, uint32_t) \
    X(designator_ranges, uint32_t) \
    X(selection_type_ends, uint32_t) \
    X(selection_types, TypeId) \
    X(constant_present, uint32_t) \
    X(builtins, uint16_t) \
    X(builtin_type_ends, uint32_t) \
    X(vararg_calls, uint32_t) \
    X(vararg_types, uint32_t) \
    X(vla_bounds, uint32_t) \
    X(vla_ends, uint32_t) \
    X(vla_scope_ends, uint32_t) \
    X(warning_suppressed, uint8_t) \
    X(parameter_shapes, uint8_t) \
    X(parameter_declaration_next, uint32_t) \
    X(tag_types, TypeId) \
    X(tag_ends, uint32_t)
#define BUSTER_CHECK_SPARSE_BYTES(field, type) + ((uint64_t)count + 1) * sizeof(type) + 63
    const uint64_t sparse_small = ((0 BUSTER_CHECK_SPARSE_SMALL_COLUMNS(BUSTER_CHECK_SPARSE_BYTES)) + BUSTER_ARENA_PAGE_BYTES - 1) & ~(BUSTER_ARENA_PAGE_BYTES - 1);
    const uint64_t sparse_bytes = sparse_small + (0 BUSTER_CHECK_SPARSE_COLUMNS(BUSTER_CHECK_SPARSE_BYTES)) + c_bindings_bytes(pp->table->count, count * 4 + 1);
#undef BUSTER_CHECK_SPARSE_BYTES
    Arena sparse = valid ? arena_reserve_small(sparse_bytes, sparse_small > BUSTER_ARENA_SMALL_BYTES ? sparse_small : BUSTER_ARENA_SMALL_BYTES) : (Arena){ 0 };
#define BUSTER_CHECK_SPARSE_ARRAY(field, type) reader.field = valid ? arena_push_array(&sparse, (uint64_t)count + 1, sizeof(type), 64) : 0; valid &= reader.field != 0;
    BUSTER_CHECK_SPARSE_SMALL_COLUMNS(BUSTER_CHECK_SPARSE_ARRAY)
    BUSTER_CHECK_SPARSE_COLUMNS(BUSTER_CHECK_SPARSE_ARRAY)
#undef BUSTER_CHECK_SPARSE_ARRAY
#undef BUSTER_CHECK_SPARSE_COLUMNS
#undef BUSTER_CHECK_SPARSE_SMALL_COLUMNS
    const uint32_t builtin_constants = counts.groups[CHECK_INPUT_COUNT_BUILTIN_CONSTANTS];
    if (valid && input->vla_capacity != 0)
    {
        input->vlas = arena_push_array(arena, input->vla_capacity, sizeof(CheckInputVla), _Alignof(CheckInputVla));
        reader.vla_sizes = arena_push_array(scratch, input->vla_capacity, sizeof(uint32_t), 4);
        valid = input->vlas != 0 && reader.vla_sizes != 0;
        if (valid)
        {
            memset(reader.vla_sizes, 0xff, (uint64_t)input->vla_capacity * sizeof(uint32_t));
        }
    }
    if (valid && input->asm_capacity != 0)
    {
        input->asm_statements = arena_push_array(arena, input->asm_capacity, sizeof(CheckInputAsm), _Alignof(CheckInputAsm));
        valid = input->asm_statements != 0;
    }
    reader.cached_constants = valid ? arena_push_array(scratch, (uint64_t)builtin_constants + 1, sizeof(CheckValue), _Alignof(CheckValue)) : 0;
    valid &= reader.cached_constants != 0;
    reader.parameter_seen = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint32_t), 64) : 0;
    valid &= reader.parameter_seen != 0;
    reader.builtin_names = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint16_t), 64) : 0;
    valid &= reader.builtin_names != 0;
    reader.vararg_capacity = counts.groups[CHECK_INPUT_COUNT_VARARGS];
    if (valid && reader.vararg_capacity != 0)
    {
        reader.varargs = arena_push_array(scratch, reader.vararg_capacity, sizeof(CheckInputVarargs), _Alignof(CheckInputVarargs));
        input->graph.varargs_calls = arena_push_array(arena, reader.vararg_capacity, sizeof(CheckVarargsCall), _Alignof(CheckVarargsCall));
        input->graph.varargs_capacity = reader.vararg_capacity;
        valid = reader.varargs != 0 && input->graph.varargs_calls != 0;
    }
    reader.member_seen = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint32_t), 64) : 0;
    valid &= reader.member_seen != 0;
    reader.linked_declarations = valid ? arena_push_array(scratch, pp->table->count, sizeof(uint32_t), 64) : 0;
    valid &= reader.linked_declarations != 0;
    reader.linked_types = valid ? arena_push_array(scratch, pp->table->count, sizeof(TypeId), 64) : 0;
    valid &= reader.linked_types != 0;
    reader.tag_completions = valid ? arena_push_array(scratch, types->tags.capacity, sizeof(uint32_t), 4) : 0;
    valid &= reader.tag_completions != 0;
    reader.enum_fixed = valid ? arena_push_array(scratch, types->tags.capacity, sizeof(uint8_t), 4) : 0;
    valid &= reader.enum_fixed != 0;
    if (valid && transparent_unions)
    {
        reader.transparent_unions = arena_push_array(scratch, types->tags.capacity, sizeof(uint32_t), 4);
        valid = reader.transparent_unions != 0;
        if (valid)
        {
            memset(reader.transparent_unions, 0, (uint64_t)types->tags.capacity * sizeof(uint32_t));
        }
    }
    valid &= c_bindings_initialize(&reader.bindings, &sparse, pp->table->count, count * 4 + 1);
    // The traversal's temporaries, last on the scratch and popped once it is derived, so that the passes after it take
    // their pages.
    const uint64_t traversal_mark = scratch->length;
    const uint64_t temporary_columns = (traversal != 0 ? 2u : 0u) + (sizes != 0 ? 1u : 0u);
    uint32_t* const temporaries = temporary_columns != 0 && valid ? arena_push_array(scratch, temporary_columns * slots, sizeof(uint32_t), 64) : 0;
    uint32_t* const starts = sizes != 0 && temporaries != 0 ? temporaries + (traversal != 0 ? 2 * slots : 0) : 0;
    valid &= temporary_columns == 0 || temporaries != 0;

    if (valid)
    {
        memset(reader.declared_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.query_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.function_specifiers, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(reader.return_types, 0, ((uint64_t)count + 1) * sizeof(TypeId));
        memset(reader.declaration_kinds, 0, (uint64_t)count + 1);
        memset(reader.conditions, 0, (uint64_t)count + 1);
        memset(reader.constant_contexts, 0, (uint64_t)count + 1);
        memset(input->graph.warning_suppressed, 0, graph_capacity);
        memset(input->constant_warnings, 0, (uint64_t)count + 1);
        memset(input->constant_warning_ignored, 0, (uint64_t)count + 1);
        if (input->deferred_tokens != 0 && deferred == 0)
        {
            // A lazily deferred body is ignored by every phase; #113's deferred bodies are read later instead.
            memcpy(reader.ignored, input->deferred_tokens, (uint64_t)count + 1);
        }
        else
        {
            memset(reader.ignored, 0, (uint64_t)count + 1);
        }
        memset(reader.defined, 0, (uint64_t)count + 1);
        memset(reader.definition_seen, 0, (uint64_t)count + 1);
        memset(input->tentative, 0, (uint64_t)count + 1);
        memset(reader.static_objects, 0, (uint64_t)count + 1);
        memset(reader.registers, 0, (uint64_t)count + 1);
        memset(input->thread_local, 0, (uint64_t)count + 1);
        memset(input->compound_objects, 0, (uint64_t)count + 1);
        memset(input->function_flags, 0, (uint64_t)count + 1);
        memset(input->explicit_abis, 0, (uint64_t)count + 1);
        memset(input->expression_regions, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(input->asm_ids, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(input->attributes, 0, ((uint64_t)count + 1) * sizeof(CheckInputAttributes*));
        memset(reader.linkage, 0, (uint64_t)count + 1);
        memset(reader.objects, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(reader.parameter_seen, 0, (uint64_t)pp->table->count * sizeof(uint32_t));
        memset(reader.member_seen, 0, (uint64_t)pp->table->count * sizeof(uint32_t));
        memset(reader.linked_declarations, 0, (uint64_t)pp->table->count * sizeof(uint32_t));
        memset(reader.linked_types, 0, (uint64_t)pp->table->count * sizeof(TypeId));
        memset(reader.tag_completions, 0xff, (uint64_t)types->tags.capacity * sizeof(uint32_t));
        memset(reader.enum_fixed, 0, types->tags.capacity);
        memset(reader.initializers, 0, (uint64_t)count + 1);
        memset(reader.builtin_names, 0xff, (uint64_t)pp->table->count * sizeof(uint16_t));
        for (uint32_t i = 0; i <= count; i += 1)
        {
            input->declarations[i] = BUSTER_CHECK_NONE;
            input->initializer_nodes[i] = BUSTER_CHECK_NONE;
            input->initializer_begins[i] = BUSTER_CHECK_NONE;
            input->initializer_ends[i] = BUSTER_CHECK_NONE;
            input->token_nodes[i] = BUSTER_CHECK_NONE;
            input->body_declarations[i] = BUSTER_CHECK_NONE;
            input->declaration_bodies[i] = BUSTER_CHECK_NONE;
            input->parameter_next[i] = BUSTER_CHECK_NONE;
            input->control_owners[i] = BUSTER_CHECK_NONE;
            input->label_targets[i] = BUSTER_CHECK_NONE;
            input->case_ranges[i] = BUSTER_CHECK_NONE;
            reader.string_ids[i] = BUSTER_CHECK_NONE;
            reader.initializer_declarations[i] = BUSTER_CHECK_NONE;
            reader.initializer_finish_heads[i] = BUSTER_CHECK_NONE;
            reader.selected_tokens[i] = BUSTER_CHECK_NONE;
            reader.activate[i] = BUSTER_CHECK_NONE;
            reader.parameters[i] = BUSTER_CHECK_NONE;
            reader.first_value[i] = BUSTER_CHECK_NONE;
            reader.last_value[i] = BUSTER_CHECK_NONE;
        }
        if (traversal != 0)
        {
            // The views of c_check_prefix.c and c_check_repair.c bring their postorder, the rest is derived from it, as
            // with --scalar from the walk's (parse_tree_columns' twins); otherwise, the C driver's tree_only parse, all
            // of it in parse_tree_columns' three passes.
            if (parser->postorder != 0 || parser->options.scalar)
            {
                if (parser->postorder == 0)
                {
                    parse_postorder(parser->tree.parents, count, temporaries, traversal);
                }
                parse_tree_links(parser->tree.parents, reader.postorder, count, traversal + slots, traversal + 2 * slots, traversal + 3 * slots, traversal + 4 * slots);
            }
            else
            {
                parse_tree_columns(parser->tree.parents, count, temporaries, temporaries + slots, traversal, traversal + slots, traversal + 2 * slots,
                                   traversal + 3 * slots, traversal + 4 * slots, starts, sizes);
            }
        }
        if (sizes != 0 && (traversal == 0 || parser->postorder != 0 || parser->options.scalar))
        {
            // The scalar/prefix/repair traversal already exists. Source order gives an earlier first child's
            // start before its parent, exactly as parse_tree_columns' size publication does on the vector path.
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t first = reader.first_child[token];
                starts[token] = first < token ? starts[first] : token;
                sizes[reader.postorder_positions[token]] = reader.ends[token] - starts[token] + 1;
            }
        }
        if (sizes != 0)
        {
            sizes[count] = 0;
        }
        arena_pop_to(scratch, traversal_mark);
        // The parser uses SEPARATOR for commas inside any parentheses. The enclosing delimiter distinguishes
        // expression commas from call/declaration separators; reverse postorder passes that fact down once.
        for (uint32_t k = count; k-- > 0;)
        {
            const uint32_t token = reader.postorder[k];
            const uint32_t parent = parser->tree.parents[token];
            const uint8_t role = parser->tree.roles[token];
            reader.expression_commas[token] = role == PARSE_ROLE_SEPARATOR && parent < count ? reader.expression_commas[parent] :
                                              role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_TERNARY ||
                                              role == PARSE_ROLE_CLOSE_FOR || (role == PARSE_ROLE_TERMINATOR && parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR);
        }
        if (parser->tokens.pragma_count != 0)
        {
            uint8_t* handled = arena_push_array(scratch, parser->tokens.pragma_count, sizeof(uint8_t), 1);
            // Argument errors stop at the pragma. Placement/conflict errors stop after the annotated statement.
            uint8_t* loop_messages = handled != 0 ? arena_push_array(scratch, parser->tokens.pragma_count, sizeof(uint8_t), 1) : 0;
            input->resource |= loop_messages == 0;
            if (loop_messages != 0)
            {
                memset(handled, 0, parser->tokens.pragma_count);
                memset(loop_messages, 0, parser->tokens.pragma_count);
                CheckInputLoopDiagnostic delayed = { 0 };
                bool prepared = check_input_diagnostic_pragmas(&reader, handled);
                prepared = prepared && check_input_nonnull_pragmas(&reader, handled);
                prepared = prepared && check_input_pack_pragmas(&reader, handled);
                prepared = prepared && check_input_fp_pragmas(&reader, handled);
                prepared = prepared && check_input_loop_pragmas(&reader, handled, loop_messages, &delayed);
                input->resource |= !prepared;
                uint32_t pack = 0;
                bool handler_error = false;
                for (uint32_t pragma = 0; prepared && pragma < parser->tokens.pragma_count; pragma += 1)
                {
                    while (input->pack_pragmas != 0 && pack < input->pack_pragmas->count && input->pack_pragmas->records[pack].pragma < pragma)
                    {
                        pack += 1;
                    }
                    const uint32_t position = parser->tokens.pragma_positions[pragma];
                    if ((handled[pragma] == 0 || handled[pragma] >= 3) &&
                        (input->deferred_tokens == 0 || deferred != 0 || input->deferred_tokens[position] == 0))
                    {
                        reader.resolve_limit = parser->tokens.pragma_positions[pragma];
                        reader.resolve_pragma = pragma;
                        reader.resolve_message = handled[pragma] < 3 ? CHECK_INPUT_UNSUPPORTED :
                                                 loop_messages[pragma] != 0 ? (CheckInputMessage)loop_messages[pragma] : CHECK_INPUT_DECLARATION;
                        handler_error = handled[pragma] == BUSTER_CHECK_LOOP_HANDLER_ERROR;
                        if (handler_error && position != 0)
                        {
                            const uint8_t previous = parser->tree.roles[position - 1];
                            const bool completed = previous == PARSE_ROLE_TERMINATOR || previous == PARSE_ROLE_CLOSE_BLOCK || previous == PARSE_ROLE_CLOSE_BODY;
                            reader.resolve_position = completed ? position - 1 : BUSTER_CHECK_NONE;
                        }
                        if (input->pack_pragmas != 0 && pack < input->pack_pragmas->count && input->pack_pragmas->records[pack].pragma == pragma)
                        {
                            reader.resolve_diagnostic_token = input->pack_pragmas->records[pack].diagnostic_token;
                        }
                        break;
                    }
                }
                // The next pragma's handler can run in the current statement's lookahead. Its argument parser runs
                // only with the next statement; those errors follow a conflict at the same insertion position.
                if (prepared && delayed.pragma != 0 && (delayed.position < reader.resolve_limit ||
                    (delayed.position == reader.resolve_limit && !handler_error)))
                {
                    input->loop_diagnostic = BUSTER_ARENA_PUSH_ARRAY(arena, CheckInputLoopDiagnostic, 1);
                    input->resource |= input->loop_diagnostic == 0;
                    if (input->loop_diagnostic != 0)
                    {
                        *input->loop_diagnostic = delayed;
                        reader.resolve_limit = delayed.position;
                        reader.resolve_pragma = delayed.pragma - 1;
                        reader.resolve_diagnostic_token = BUSTER_CHECK_NONE;
                        reader.resolve_position = BUSTER_CHECK_NONE;
                        reader.resolve_message = delayed.message;
                    }
                }
            }
        }
        valid &= check_input_intrinsic_initialize(&reader);
        valid &= check_input_overload_initialize(&reader, counts.groups[CHECK_INPUT_COUNT_OVERLOADABLE] != 0, counts.operands, counts.calls);
        if (!check_input_target_supported(&pp->options))
        {
            check_input_report(&reader, 0, CHECK_INPUT_TARGET);
        }
        else if (!input->resource)
        {
            check_input_system_tokens(&reader, pp, arena);
            check_input_attribute_items(&reader);
            check_input_resolve_range(&reader, 0, count);
            if (deferred != 0)
            {
                // The bodies, read apart and merged into the reader's state, where source order would have read them;
                // or the unit is for source order (check_input_deferred), and nothing more is done here. The phases
                // after the walk run on the merge's table, which source order's walk would have made.
                check_deferred_bodies(deferred, &reader);
                types = reader.types;
                // Lanes may have appended warnings after a top-level range last summarized their shared columns.
                reader.warning_index_ready = false;
            }
            if (input->report_count == 0 && !input->resource)
            {
                check_input_controls(&reader);
            }
            else if (!input->resource && input->report_count == 1 && input->reports[0].pragma != 0)
            {
                const uint32_t position = input->reports[0].token < reader.expression_limit ? input->reports[0].token : reader.expression_limit;
                check_input_controls_prefix(&reader, position);
            }
        }
        if (!input->resource && reader.bindings.valid)
        {
            check_input_bindings(&reader, binding_mode);
            for (uint32_t report = 0; report < input->report_count; report += 1)
            {
                const uint32_t token = input->reports[report].token;
                reader.expression_limit = token < reader.expression_limit ? token : reader.expression_limit;
            }
            if (!input->resource)
            {
                // Without a _Generic or __builtin_choose_expr token there is nothing to select: the scalar reader
                // walks every token for them, the twin of this count.
                if (counts.groups[CHECK_INPUT_COUNT_SELECTIONS] != 0 || BUSTER_SCALAR_PATH(parser->options.scalar))
                {
                    check_input_selections(&reader);
                }
                if (complete_translation_unit && input->report_count == 0)
                {
                    check_input_complete_uses(&reader);
                }
                for (uint32_t report = 0; report < input->report_count; report += 1)
                {
                    const uint32_t token = input->reports[report].token;
                    reader.expression_limit = token < reader.expression_limit ? token : reader.expression_limit;
                }
                const uint32_t tags = types->tags.count;
                uint32_t* completions = tags != 0 ? arena_push_array(arena, tags, sizeof(uint32_t), 4) : 0;
                input->resource |= tags != 0 && completions == 0;
                if (completions != 0)
                {
                    memcpy(completions, reader.tag_completions, (uint64_t)tags * sizeof(uint32_t));
                    input->graph.tag_completions = completions;
                    input->graph.tag_completion_count = tags;
                }
                if (tags != 0 && reader.transparent_unions != 0)
                {
                    uint32_t* activations = arena_push_array(arena, tags, sizeof(uint32_t), 4);
                    input->resource |= activations == 0;
                    if (activations != 0)
                    {
                        memcpy(activations, reader.transparent_unions, (uint64_t)tags * sizeof(uint32_t));
                        input->graph.transparent_unions = activations;
                    }
                }
                input->graph.builtin_capacity += check_input_builtin_capacity(reader.builtins, count, BUSTER_SCALAR_PATH(parser->options.scalar));
                if (input->graph.builtin_capacity != 0)
                {
                    input->graph.builtin_calls = arena_push_array(arena, input->graph.builtin_capacity, sizeof(CheckBuiltinCall), _Alignof(CheckBuiltinCall));
                    input->resource |= input->graph.builtin_calls == 0;
                }
                if (input->intrinsic_call_count != 0)
                {
                    input->graph.x86_capacity = input->intrinsic_call_count;
                    input->graph.x86_calls = arena_push_array(arena, input->graph.x86_capacity, sizeof(CheckX86Call), _Alignof(CheckX86Call));
                    input->resource |= input->graph.x86_calls == 0;
                }
                check_input_constant_contexts(&reader);
                if (check_input_region_initialize(&reader))
                {
                    check_input_expressions(&reader);
                }
                if (!input->resource && input->graph.x86_count != 0 && counts.groups[CHECK_INPUT_COUNT_SELECTIONS] != 0)
                {
                    check_input_x86_contexts(&reader);
                }
                if (!input->resource)
                {
                    check_input_asm_finish(&reader);
                    check_input_naked_finish(&reader);
                }
                if (input->report_count == 0 && !input->resource)
                {
                    check_input_vla_finish(&reader);
                }
                if (!input->resource)
                {
                    check_input_packed_members(&reader);
                }
                input->diagnostic_graph = input->report_count != 0 && !input->resource;
            }
        }
        valid = !input->resource && input->report_count == 0 && reader.bindings.valid;
        if (valid && environment)
        {
            valid = check_environment_publish(&input->environment, parser, &reader.bindings, input->declared_types, types, reader.tag_completions, arena, scratch);
            input->resource |= !valid;
        }
    }
    else
    {
        input->resource = true;
    }
    arena_pop_to(scratch, mark);
    arena_release(&sparse);
    return valid;
}

static bool check_input(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch)
{
    return check_input_run(input, parser, pp, types, arena, scratch, true, parser->tokens.count, 0, CHECK_INPUT_BINDINGS_DEFAULT, 0, true);
}

// check_input with another binding handoff (#336), for the tests and benchmark-checker-bindings: every mode must
// publish the same columns, reports and history as the default one.
static __attribute__((unused)) bool check_input_bindings_mode(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types,
                                                              Arena* arena, Arena* scratch, CheckInputBindingMode binding_mode)
{
    return check_input_run(input, parser, pp, types, arena, scratch, true, parser->tokens.count, 0, binding_mode, 0, true);
}

// The selection owner validates nonoverlapping complete [OPEN_BODY, CLOSE_BODY + 1) masks. All source syntax,
// function declarations and file-scope state still belong to this translation unit. The retained copy outlives
// the caller's selection scratch; an absent mask takes exactly the ordinary all-body path.
static __attribute__((unused)) bool check_input_selected(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types,
                                                       Arena* arena, Arena* scratch, const uint8_t* deferred_tokens)
{
    return check_input_run(input, parser, pp, types, arena, scratch, true, parser->tokens.count, deferred_tokens, CHECK_INPUT_BINDINGS_DEFAULT, 0, true);
}

// A complete syntactic prefix may still acquire later tentative definitions. This entry point is only for
// selecting an earlier semantic diagnostic when the full parser has already rejected the translation unit.
static bool check_input_prefix(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch)
{
    return check_input_run(input, parser, pp, types, arena, scratch, false, parser->tokens.count, 0, CHECK_INPUT_BINDINGS_DEFAULT, 0, true);
}

// The C driver's reader (run_check_command): check_input, check_input_prefix, check_input_fragment or check_input_selected
// by its arguments, without #113's environment, which nothing in a compile reads: publishing it copied the unit's
// binding events and built their index for nothing (#572).
static bool check_input_compile(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                                bool complete_translation_unit, uint32_t original_end, const uint8_t* deferred_tokens)
{
    return check_input_run(input, parser, pp, types, arena, scratch, complete_translation_unit, original_end, deferred_tokens, CHECK_INPUT_BINDINGS_DEFAULT, 0,
                           false);
}

// Diagnostic repairs append synthetic closing tokens. Labels may still occur after the retained fragment in
// such a body, while every source-complete body keeps ordinary function-scope label constraints.
static __attribute__((unused)) bool check_input_fragment(CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                                 uint32_t original_end)
{
    return check_input_run(input, parser, pp, types, arena, scratch, false, original_end, 0, CHECK_INPUT_BINDINGS_DEFAULT, 0, true);
}
