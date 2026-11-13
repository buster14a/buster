// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's step 0, the census (docs/node-machine.md, "The steps and their gates"): what the node machine's set and its
// silence tests would take, counted on the pipeline as it is. No machine runs: each file is prepared and lowered as -c
// does, with the tasks' log of pops (#1048's, c_check_lower_inventory.c's lower_inventory_file), and every full
// expression the tasks lowered is read again from the finished graph, stream and parser's tree, by the tests the machine
// would make before it takes it. Include after c_check_lower_inventory.c.
//
//   build/compiler measure-node-machine [--list N] [--shadow [--scalar] [--verify] [--passes N] [--repeat N]] [preprocessor options] file...
//
// A full expression is a tree of the tasks' pops under one root: a pop that a statement's control pushed (an expression
// statement's, a condition's, a return's, a switch's, an initializer's) or a nested control (a statement expression's).
// Its evaluated nodes are its pops, a node popped twice counted once; its tests read every node of its root's graph
// subtree, those no task evaluates among them (sizeof's operand, a constant condition's other arm).
//
// The set (docs/node-machine.md, "T: bind and type"): a root of a control the machine doesn't read, an operator without a
// code (the builtins, va_arg, statement expressions, variable-length arrays, _Generic and __builtin_choose_expr, __real__
// and __imag__), a complex, _BitInt, atomic, vector, half-precision or __float128 value, a bit-field, a struct's or a
// union's value copied, a compound literal, a name whose declaration has a flag the tables don't model (register,
// overloadable, deprecated, unavailable, error, warning, diagnose_if, enable_if, noderef), a callee with semantics of its
// own (a library builtin the catalogue folds, a printf-family name, format, format_arg, sentinel, returns_twice,
// diagnose_if, enable_if, error, warning, deprecated, unavailable, overloadable, pass_object_size, thread-safety or noderef
// attributes, no prototype, a musttail return), more than BUSTER_NODE_CENSUS_MOST nodes, a body that tracks its stack and
// a node the tasks popped twice. A brace initializer's parts are a control's the machine doesn't read.
//
// The silence tests (docs/node-machine.md, "Silence"), each only where a warning it stands for is enabled in the unit
// (check_common_enabled, with each warning's default as its producer gives it): the actions' own warning bits (what the
// baked rows, the folds and the pointer pairs' memo give); a warning event of the reader's or the graph's at a token of
// the expression (a literal's, a deprecated use's, a null's...); two enumeration types meeting, or one and a floating
// type; a sign comparison (an unsigned operand at least as wide as int against a signed one that is no non-negative
// constant); a pointer cast to a narrower integer or an enumeration, or a void pointer dereferenced; a string literal plus
// an integer; an address of a packed record's member; an operand of sizeof or alignof with an effect, an array decayed
// in sizeof's operand, or alignof of an expression; a narrowing conversion that changes a constant, a division by zero, a
// shift by a constant out of range or of a negative constant, a null pointer dereferenced, a constant subscript outside
// its array (its end allowed under `&`), a comparison with a constant outside its other operand's range in their common
// type or at a bound by an ordering, a non-literal null; parentheses (an assignment as a condition, a comparison in a
// bitwise operator, a `!` before a comparison, a shift of a sum, a chained comparison, a shift in a truth context); a
// discarded value without an effect, or a discarded call whose callee asks for its result (warn_unused_result, const,
// pure, or a const or pure builtin's name); a self-assignment; a return of a local's address or array; a for
// statement's clauses where -Wfor-loop-analysis is enabled; an object written and named again unsequenced (the design's
// test: written twice, or written by an operator below the root and named outside it, the two neither apart from a
// sequence point nor in a conditional's two arms); and a body with a diagnostic or floating-point pragma, or a node with a
// floating-point environment. The design's test of a system header's macro's tokens in a user's expression is counted
// apart, not as a refusal: in a system header's macro the pipeline only suppresses diagnostics, so its silence holds
// there without it, and on sqlite3.c it would refuse more nodes than the silence tests' whole budget, its asserts'
// `((void)0)` under NDEBUG.
//
// The report counts, per test, the expressions and evaluated nodes it refuses (any), those it alone refuses (only) and
// those it refuses first in the list's order (first); the step's gate (the set's share of the nodes, and the share the
// silence tests refuse of all the nodes); the expressions and nodes taken by root kind; the bodies taken whole; and the
// events' warnings found in expressions. --list N prints the first N expressions each test refuses, by file and line.
//
// --shadow is step 1 (below, node_shadow_expression): the node machine (c_check_node_machine.c) run on every expression
// the census takes, compared with the pipeline's records and entries, and timed.
//
// This is a measurement outside the compile path: it runs the scalar task loop and reads the finished columns, and has
// no vector path of its own.
#ifndef BUSTER_C_CHECK_NODE_CENSUS_INCLUDED
#define BUSTER_C_CHECK_NODE_CENSUS_INCLUDED

#include <linux/perf_event.h>
#include <sys/syscall.h>

// The most nodes an expression the machine takes has (docs/node-machine.md, "The records and the tapes").
#define BUSTER_NODE_CENSUS_MOST 4096u

typedef enum NodeCensusTest
{
    NODE_CENSUS_TRACKED,
    NODE_CENSUS_POPPED_AGAIN,
    NODE_CENSUS_CONTROL,
    NODE_CENSUS_OPERATOR,
    NODE_CENSUS_TYPE,
    NODE_CENSUS_BIT_FIELD,
    NODE_CENSUS_AGGREGATE,
    NODE_CENSUS_COMPOUND_LITERAL,
    NODE_CENSUS_DECLARATION,
    NODE_CENSUS_CALLEE,
    NODE_CENSUS_SIZE,
    // The silence tests, from here on.
    NODE_CENSUS_ACTION_WARNING,
    NODE_CENSUS_EVENT,
    NODE_CENSUS_ENUM,
    NODE_CENSUS_SIGN_COMPARE,
    NODE_CENSUS_POINTER_CAST,
    NODE_CENSUS_STRING_PLUS_INT,
    NODE_CENSUS_PACKED_MEMBER,
    NODE_CENSUS_UNEVALUATED,
    NODE_CENSUS_CONSTANT,
    NODE_CENSUS_PARENTHESES,
    NODE_CENSUS_UNUSED,
    NODE_CENSUS_SELF_ASSIGN,
    NODE_CENSUS_RETURN_ADDRESS,
    NODE_CENSUS_LOOP,
    NODE_CENSUS_UNSEQUENCED,
    NODE_CENSUS_REGION,
    NODE_CENSUS_UNMODELED,
    NODE_CENSUS_TESTS,
} NodeCensusTest;

#define NODE_CENSUS_FIRST_SILENCE NODE_CENSUS_ACTION_WARNING
#define NODE_CENSUS_SET_MASK ((UINT64_C(1) << NODE_CENSUS_FIRST_SILENCE) - 1)

_Static_assert(NODE_CENSUS_TESTS <= 64, "an expression's tests are one word's bits");

static const char* const node_census_test_names[NODE_CENSUS_TESTS] = {
    [NODE_CENSUS_TRACKED] = "tracked-body",
    [NODE_CENSUS_POPPED_AGAIN] = "popped-again",
    [NODE_CENSUS_CONTROL] = "control",
    [NODE_CENSUS_OPERATOR] = "operator",
    [NODE_CENSUS_TYPE] = "complex-bitint-atomic-vector-half",
    [NODE_CENSUS_BIT_FIELD] = "bit-field",
    [NODE_CENSUS_AGGREGATE] = "aggregate-value",
    [NODE_CENSUS_COMPOUND_LITERAL] = "compound-literal",
    [NODE_CENSUS_DECLARATION] = "declaration-flag",
    [NODE_CENSUS_CALLEE] = "callee",
    [NODE_CENSUS_SIZE] = "size",
    [NODE_CENSUS_ACTION_WARNING] = "action-warning",
    [NODE_CENSUS_EVENT] = "warning-event",
    [NODE_CENSUS_ENUM] = "enum-mix",
    [NODE_CENSUS_SIGN_COMPARE] = "sign-compare",
    [NODE_CENSUS_POINTER_CAST] = "pointer-cast-or-void-dereference",
    [NODE_CENSUS_STRING_PLUS_INT] = "string-plus-int",
    [NODE_CENSUS_PACKED_MEMBER] = "packed-member-address",
    [NODE_CENSUS_UNEVALUATED] = "unevaluated-operand",
    [NODE_CENSUS_CONSTANT] = "constant",
    [NODE_CENSUS_PARENTHESES] = "parentheses",
    [NODE_CENSUS_UNUSED] = "unused-value",
    [NODE_CENSUS_SELF_ASSIGN] = "self-assign",
    [NODE_CENSUS_RETURN_ADDRESS] = "return-address",
    [NODE_CENSUS_LOOP] = "for-loop-analysis",
    [NODE_CENSUS_UNSEQUENCED] = "unsequenced",
    [NODE_CENSUS_REGION] = "region",
    [NODE_CENSUS_UNMODELED] = "unmodeled-warning",
};

// The roots by the control that pushed them.
typedef enum NodeCensusRoot
{
    NODE_CENSUS_ROOT_STATEMENT,
    NODE_CENSUS_ROOT_CONDITION,
    NODE_CENSUS_ROOT_RETURN,
    NODE_CENSUS_ROOT_SWITCH,
    NODE_CENSUS_ROOT_INITIALIZER,
    NODE_CENSUS_ROOT_BRACE,
    NODE_CENSUS_ROOT_OTHER,
    NODE_CENSUS_ROOTS,
} NodeCensusRoot;

static const char* const node_census_root_names[NODE_CENSUS_ROOTS] = {
    [NODE_CENSUS_ROOT_STATEMENT] = "statement",
    [NODE_CENSUS_ROOT_CONDITION] = "condition",
    [NODE_CENSUS_ROOT_RETURN] = "return",
    [NODE_CENSUS_ROOT_SWITCH] = "switch",
    [NODE_CENSUS_ROOT_INITIALIZER] = "initializer",
    [NODE_CENSUS_ROOT_BRACE] = "brace-initializer",
    [NODE_CENSUS_ROOT_OTHER] = "other-control",
};

// What the census does with each warning (node_census_warning_classes): a test stands for it, by its own reading or by
// the set's refusal of what it fires on (TESTED); it fires on no expression the census reads (OUTSIDE: declarations,
// attributes, pragmas, statements, brace initializers); its scans read the events a taken expression records (EVENTS:
// the uninitialized and unused-but-set ones); or no test models it, and wherever it is enabled every expression is
// refused (WHOLE). A warning without a class, as one added since, is refused as WHOLE with its default taken to be on,
// and test-checker fails until it has one (c_check_node_census_test.c): a new diagnostic lands with its refusal.
typedef enum NodeCensusWarningClass
{
    NODE_CENSUS_WARNING_UNCLASSIFIED,
    NODE_CENSUS_WARNING_TESTED,
    NODE_CENSUS_WARNING_OUTSIDE,
    NODE_CENSUS_WARNING_EVENTS,
    NODE_CENSUS_WARNING_WHOLE,
} NodeCensusWarningClass;

static const uint8_t node_census_warning_classes[CHECK_WARNING_ID_COUNT] = {
    [CHECK_WARNING_ID_DISCARDED_QUALIFIERS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_GNU_CONSTANT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ENUM_TOO_LARGE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INTEGER_OVERFLOW] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SHIFT_COUNT_NEGATIVE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SHIFT_COUNT_OVERFLOW] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SHIFT_NEGATIVE_VALUE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SHIFT_OVERFLOW] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_EXCESS_INITIALIZERS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNKNOWN_PRAGMA] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_IGNORED_ATTRIBUTES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_POINTER_SIGN] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_POINTER_INTEGER_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_DISTINCT_POINTER_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CONDITIONAL_POINTER_INTEGER] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CONDITIONAL_POINTER_TYPES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ADDRESS_OF_PACKED_MEMBER] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ARGUMENT_OUTSIDE_RANGE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_RETURN_TYPE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_IMPLICIT_FUNCTION_DECLARATION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_VISIBILITY_PRAGMA] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_KR_DEFINITION] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_NON_PROTOTYPE_CALL] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_KR_TOO_FEW] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_KR_TOO_MANY] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_NON_LITERAL_NULL] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_LITERAL_RANGE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ORDERED_POINTER_INTEGER] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_MISSING_DECLARATIONS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_PLAIN_COMPLEX] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_STATIC_LOCAL_INLINE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_MANY_SCALAR_BRACES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INITIALIZER_OVERRIDES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNDEFINED_INTERNAL] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_C23_PARAMETER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNRECOGNIZED_PRAGMA] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_IGNORED_PRAGMA] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_SOURCE_OPENMP] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_LIBRARY_REDECLARATION] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_BUILTIN_HEADER] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DIVISION_BY_ZERO] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CONSTANT_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ARRAY_BOUNDS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SIGN_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNUSED_COMPARISON] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNUSED_VALUE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_LOGICAL_NOT_PARENTHESES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_PARENTHESES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_EMPTY_BODY] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_SELF_ASSIGN] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_RETURN_STACK_ADDRESS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FORMAT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FORMAT_SECURITY] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNUSED_VARIABLE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNUSED_FUNCTION] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNUSED_PARAMETER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNINITIALIZED] = NODE_CENSUS_WARNING_EVENTS,
    [CHECK_WARNING_ID_SWITCH] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DANGLING_ELSE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNUSED_BUT_SET_PARAMETER] = NODE_CENSUS_WARNING_EVENTS,
    [CHECK_WARNING_ID_UNUSED_CONST_VARIABLE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_FORMAT_EXTRA_ARGS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FORMAT_INSUFFICIENT_ARGS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FORMAT_INVALID_SPECIFIER] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_EMPTY_CASE_RANGE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_GCC_COMPAT] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_GNU_ALIGNOF_EXPRESSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_DEPRECATED_DECLARATIONS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CFSTRING_LITERAL] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNSUPPORTED_AVAILABILITY_GUARD] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_AVAILABILITY] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_COUNTED_BY_ELEMENT] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_EXTERN_INITIALIZER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNKNOWN_SANITIZERS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_OVERFLOW_BEHAVIOR] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_NSOBJECT_ATTRIBUTE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INDEPENDENT_CLASS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_ASSUME] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DUPLICATE_DECLSPEC] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INVALID_NORETURN] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_PRIVATE_EXTERN] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_LITERAL_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_WEAK_UNDECLARED] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_REDEFINE_EXTNAME] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_THREAD_SAFETY_ATTRIBUTES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_NODEREF] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_EXCESSIVE_REGSAVE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_TCB_ENFORCEMENT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_INVALID_VERSION_AVAILABILITY] = NODE_CENSUS_WARNING_OUTSIDE,
    // A label's warning, a statement's (c_check_control_input.c).
    [CHECK_WARNING_ID_UNUSED_LABEL] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNKNOWN_ESCAPE_SEQUENCE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_VOID_PTR_DEREFERENCE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_MULTICHAR] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FOUR_CHAR_CONSTANTS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CHARACTER_TOO_LONG] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_NULL_DEREFERENCE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_USER_DEFINED_WARNINGS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FUNCTION_MULTIVERSION] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNUSED_RESULT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_TAUTOLOGICAL_CONSTANT_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_BITFIELD_CONSTANT_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SINGLE_BIT_BITFIELD_CONSTANT_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ENUM_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ENUM_ENUM_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ENUM_COMPARE_CONDITIONAL] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ENUM_FLOAT_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ANON_ENUM_ENUM_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_FOR_LOOP_ANALYSIS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_IMPLICIT_ENUM_ENUM_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_VISIBILITY] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_ODR] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INVALID_UNEVALUATED_STRING] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_NONSTANDARD_ESCAPE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_DELIMITED_ESCAPE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_TARGET_CLONES_MIXED_SPECIFIERS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_INVALID_CPU_SUPPORTS] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ATTRIBUTE_ALIAS] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_TYPE_SAFETY] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNEVALUATED_EXPRESSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNDERLYING_CV_QUALIFIER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNDERLYING_ATOMIC_QUALIFIER] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_PRAGMA_PACK] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_PRAGMA_PACK_NOTICE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_POINTER_TO_INT_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_VOID_POINTER_TO_INT_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_POINTER_TO_ENUM_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_TENTATIVE_ARRAY] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_SWIFT_NAME_ATTRIBUTE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNREACHABLE_GENERIC_ASSOC] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_UNSEQUENCED] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR_UNSAFE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_FIELD_UNSAFE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_CXX_COMPAT] = NODE_CENSUS_WARNING_WHOLE,
    [CHECK_WARNING_ID_GNU_UNION_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_GNU_DESIGNATOR] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_GNU_ARRAY_RANGE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_STRING_PLUS_INT] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SHIFT_OP_PARENTHESES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_CHAINED_COMPARISON] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ASM_LABEL_ON_AUTO] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_ALIGNMENT_NO_OP] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ALIGNMENT_ALWAYS_TRUE] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_NONSCALAR_SAME_CAST] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_C23_ATTRIBUTES] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_ELABORATED_ENUM_BASE] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_PSABI] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_GNU_EMPTY_STRUCT] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_UNUSED_BUT_SET_VARIABLE] = NODE_CENSUS_WARNING_EVENTS,
    [CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_ASSIGN_ENUM] = NODE_CENSUS_WARNING_TESTED,
    // Its comparisons are the tautological test's: a constant outside its other operand's range (#2218).
    [CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE] = NODE_CENSUS_WARNING_TESTED,
    // A typedef is a declaration's, no expression's (#2264).
    [CHECK_WARNING_ID_UNUSED_LOCAL_TYPEDEF] = NODE_CENSUS_WARNING_OUTSIDE,
    // The default-error conversions fire only at relaxed tokens (#1963): a unit with any is refused whole.
    [CHECK_WARNING_ID_INT_CONVERSION] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_INCOMPATIBLE_POINTER_TYPES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_INCOMPATIBLE_FUNCTION_POINTER_TYPES] = NODE_CENSUS_WARNING_TESTED,
    [CHECK_WARNING_ID_RETURN_MISMATCH] = NODE_CENSUS_WARNING_TESTED,
    // An atomic record member is refused by the type test, including an arrow whose own result is scalar.
    [CHECK_WARNING_ID_ATOMIC_ACCESS] = NODE_CENSUS_WARNING_TESTED,
};

// Which of the warnings the silence tests stand for the unit enables anywhere (check_common_enabled): the actions' warning
// bits enabled where their default is to warn and where it is to ignore, and one flag a test.
typedef struct NodeCensusPolicy NodeCensusPolicy;
struct NodeCensusPolicy
{
    uint32_t action_bits;
    uint32_t action_ignored_bits;
    bool enums;
    bool sign_compare;
    bool pointer_casts;
    bool void_dereference;
    bool string_plus_int;
    bool packed_member;
    bool unevaluated;
    bool alignof_expression;
    bool sizeof_decay;
    bool constant_conversion;
    bool division;
    bool shifts;
    bool null_dereference;
    bool array_bounds;
    bool tautological;
    bool non_literal_null;
    bool parentheses;
    bool logical_not;
    bool shift_parentheses;
    bool chained;
    bool int_in_bool;
    bool unused_value;
    bool unused_comparison;
    bool unused_result;
    bool self_assign;
    bool return_address;
    bool loops;
    bool unsequenced;
    bool shift_sign_overflow;
    bool assign_enum;
    bool type_safety;
    // A warning enabled in the unit that no test stands for (NODE_CENSUS_WARNING_WHOLE, or without a class).
    bool unmodeled;
};

// One expression's result, for the census's tests (NodeCensus.records): its root's line, its root kind and its tests.
typedef struct NodeCensusRecord NodeCensusRecord;
struct NodeCensusRecord
{
    uint32_t line;
    NodeCensusRoot root;
    uint64_t tests;
};

// The totals over the files.
typedef struct NodeCensus NodeCensus;
struct NodeCensus
{
    // Optional: each expression's result, up to `record_capacity` of them (c_check_node_census_test.c's).
    NodeCensusRecord* records;
    uint32_t record_count;
    uint32_t record_capacity;
    uint64_t files;
    uint64_t failures;
    uint64_t bodies;
    uint64_t bodies_taken;
    uint64_t bodies_taken_nodes;
    uint64_t expressions;
    uint64_t nodes;
    uint64_t set_expressions;
    uint64_t set_nodes;
    uint64_t taken_expressions;
    uint64_t taken_nodes;
    uint64_t root_expressions[NODE_CENSUS_ROOTS];
    uint64_t root_nodes[NODE_CENSUS_ROOTS];
    uint64_t root_taken_nodes[NODE_CENSUS_ROOTS];
    uint64_t any_expressions[NODE_CENSUS_TESTS];
    uint64_t any_nodes[NODE_CENSUS_TESTS];
    uint64_t only_expressions[NODE_CENSUS_TESTS];
    uint64_t only_nodes[NODE_CENSUS_TESTS];
    uint64_t first_expressions[NODE_CENSUS_TESTS];
    uint64_t first_nodes[NODE_CENSUS_TESTS];
    uint64_t listed[NODE_CENSUS_TESTS];
    // The warning events found in expressions, by ID (NODE_CENSUS_EVENT's), all the IDs past the array's in its last.
    uint64_t event_ids[CHECK_WARNING_ID_COUNT + 1];
    uint64_t largest;
    // The expressions with a system header's macro's tokens in a user's expression, and their nodes, and of those the ones
    // taken otherwise: what the design's system-macro test would refuse, counted apart (see node_census_report).
    uint64_t system_expressions;
    uint64_t system_nodes;
    uint64_t system_taken_nodes;
    // --shadow: the node machine's step 1, run on every expression the census takes (NodeShadow), or null.
    struct NodeShadow* shadow;
};

// --list N: the first N expressions each test refuses.
static uint32_t node_census_list;
// The current unit's -Wassign-enum (NodeCensusPolicy.assign_enum), for node_census_constant_changes.
static bool node_census_assign_enum;

// One file's state: the program and the unit's, and per node and pop the walks' columns, valid for the current
// expression's stamp alone.
typedef struct NodeCensusScratch NodeCensusScratch;
struct NodeCensusScratch
{
    const CheckProgram* program;
    Preprocessor* pp;
    const char* file;
    NodeCensusPolicy policy;
    // Per token: whether a kept warning event is at it, as a running count (events before it), token_count + 1 entries.
    uint32_t* event_prefix;
    // Per token, the first event's ID there (UINT32_MAX for none), for the report's histogram.
    uint32_t* event_ids;
    // Per token, whether it is a `&` of a packed record's member that the reader found (CheckInput.packed_members) and
    // didn't suppress: the member lookup's answer, which the machine reads with the member's offset.
    uint8_t* packed;
    // Per node: the stamp of the expression that reached it, its parent there and its depth below the root.
    uint32_t* marks;
    uint32_t* parents;
    uint32_t* depths;
    // Per node, the control whose node it is in the current body (BUSTER_CHECK_NONE for none).
    uint32_t* node_controls;
    // The current expression's nodes, in the order its walk reached them, and the walk's stack.
    uint32_t* list;
    uint32_t* stack;
    uint32_t stamp;
    // The current expression's tokens' span: its nodes' least and greatest tokens; and whether a token of a system header's
    // macro is among its nodes' where its root's isn't (the design's system-macro test, counted apart: see the report).
    uint32_t span_low;
    uint32_t span_high;
    bool mixed_system;
    // Per pop of the current body: its node's first pop, its root pop, and the pops of each node (node_pop, by node).
    uint32_t* node_pop;
    uint32_t* root_pop;
    uint32_t* times;
    uint32_t* expression_nodes;
    uint64_t* expression_tests;
    // The current expression's least node and its greatest but its root (its walk's), for the shadow's range of records.
    uint32_t lowest_node;
    uint32_t inner_node;
    // --shadow's: the machine, the current body's function and its entries' owners, and per object and per local the
    // body's map between them (each stamped with the body's number plus one).
    NodeMachine* machine;
    const Tier0Function* function;
    const uint32_t* owners;
    // The log's labels' entries (CheckLowerOwners.labels), which a pop's `label` indexes.
    const uint32_t* labels;
    uint64_t label_count;
    uint32_t* object_locals;
    uint32_t* object_stamps;
    uint32_t* local_objects;
    uint32_t* local_stamps;
    uint32_t body_stamp;
    // The current expression's temporaries' locals, by their number on the tape, and how many it has met.
    uint32_t* temporary_locals;
    uint32_t temporary_count;
};

static double node_census_share(uint64_t part, uint64_t whole)
{
    return whole != 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

// The warnings the silence tests stand for, enabled anywhere in the unit.
static NodeCensusPolicy node_census_policy(const Preprocessor* pp, const CheckInput* input)
{
    NodeCensusPolicy policy = { 0 };
    for (uint32_t id = 0; id < 25; id += 1)
    {
        policy.action_bits |= check_common_enabled(pp, input, id, false) ? 1u << id : 0u;
        policy.action_ignored_bits |= check_common_enabled(pp, input, id, true) ? 1u << id : 0u;
    }
    // ENUM_TOO_LARGE, EXCESS_INITIALIZERS, UNKNOWN_PRAGMA and IGNORED_ATTRIBUTES are no expression's; KR_DEFINITION a
    // definition's.
    const uint32_t declarations = 1u << CHECK_WARNING_ID_ENUM_TOO_LARGE | 1u << CHECK_WARNING_ID_EXCESS_INITIALIZERS | 1u << CHECK_WARNING_ID_UNKNOWN_PRAGMA |
                                  1u << CHECK_WARNING_ID_IGNORED_ATTRIBUTES | 1u << CHECK_WARNING_ID_KR_DEFINITION;
    policy.action_bits &= ~declarations;
    policy.action_ignored_bits &= ~declarations;
    policy.enums = check_common_enabled(pp, input, CHECK_WARNING_ID_ENUM_COMPARE, false) || check_common_enabled(pp, input, CHECK_WARNING_ID_ENUM_ENUM_CONVERSION, false) ||
                   check_common_enabled(pp, input, CHECK_WARNING_ID_ENUM_COMPARE_CONDITIONAL, false) ||
                   check_common_enabled(pp, input, CHECK_WARNING_ID_ENUM_FLOAT_CONVERSION, false) ||
                   check_common_enabled(pp, input, CHECK_WARNING_ID_ANON_ENUM_ENUM_CONVERSION, false) ||
                   check_common_enabled(pp, input, CHECK_WARNING_ID_IMPLICIT_ENUM_ENUM_CAST, false);
    policy.sign_compare = check_common_enabled(pp, input, CHECK_WARNING_ID_SIGN_COMPARE, true);
    policy.pointer_casts = check_common_enabled(pp, input, CHECK_WARNING_ID_POINTER_TO_INT_CAST, false) ||
                           check_common_enabled(pp, input, CHECK_WARNING_ID_VOID_POINTER_TO_INT_CAST, false) ||
                           check_common_enabled(pp, input, CHECK_WARNING_ID_POINTER_TO_ENUM_CAST, false) ||
                           check_common_enabled(pp, input, CHECK_WARNING_ID_VOID_POINTER_TO_ENUM_CAST, false);
    policy.void_dereference = check_common_enabled(pp, input, CHECK_WARNING_ID_VOID_PTR_DEREFERENCE, false);
    policy.string_plus_int = check_common_enabled(pp, input, CHECK_WARNING_ID_STRING_PLUS_INT, false);
    policy.packed_member = check_common_enabled(pp, input, CHECK_WARNING_ID_ADDRESS_OF_PACKED_MEMBER, false);
    policy.unevaluated = check_common_enabled(pp, input, CHECK_WARNING_ID_UNEVALUATED_EXPRESSION, false);
    policy.alignof_expression = check_common_enabled(pp, input, CHECK_WARNING_ID_GNU_ALIGNOF_EXPRESSION, false);
    policy.sizeof_decay = check_common_enabled(pp, input, CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY, false);
    policy.constant_conversion = check_common_enabled(pp, input, CHECK_WARNING_ID_CONSTANT_CONVERSION, false) ||
                                 check_common_enabled(pp, input, CHECK_WARNING_ID_LITERAL_CONVERSION, false);
    policy.division = check_common_enabled(pp, input, CHECK_WARNING_ID_DIVISION_BY_ZERO, false);
    policy.shifts = check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_COUNT_NEGATIVE, false) ||
                    check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_COUNT_OVERFLOW, false) ||
                    check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_NEGATIVE_VALUE, false);
    policy.null_dereference = check_common_enabled(pp, input, CHECK_WARNING_ID_NULL_DEREFERENCE, false);
    policy.array_bounds = check_common_enabled(pp, input, CHECK_WARNING_ID_ARRAY_BOUNDS, false);
    policy.tautological = check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_CONSTANT_COMPARE, false) ||
                          check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_OUT_OF_RANGE_COMPARE, false);
    policy.non_literal_null = check_common_enabled(pp, input, CHECK_WARNING_ID_NON_LITERAL_NULL, false);
    policy.parentheses = check_common_enabled(pp, input, CHECK_WARNING_ID_PARENTHESES, false);
    policy.logical_not = check_common_enabled(pp, input, CHECK_WARNING_ID_LOGICAL_NOT_PARENTHESES, false);
    policy.shift_parentheses = check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_OP_PARENTHESES, false);
    policy.chained = check_common_enabled(pp, input, CHECK_WARNING_ID_CHAINED_COMPARISON, false);
    policy.int_in_bool = check_common_enabled(pp, input, CHECK_WARNING_ID_INT_IN_BOOL_CONTEXT, true);
    policy.unused_value = check_common_enabled(pp, input, CHECK_WARNING_ID_UNUSED_VALUE, false);
    policy.unused_comparison = check_common_enabled(pp, input, CHECK_WARNING_ID_UNUSED_COMPARISON, false);
    policy.unused_result = check_common_enabled(pp, input, CHECK_WARNING_ID_UNUSED_RESULT, false) || policy.unused_value;
    policy.self_assign = check_common_enabled(pp, input, CHECK_WARNING_ID_SELF_ASSIGN, true);
    policy.return_address = check_common_enabled(pp, input, CHECK_WARNING_ID_RETURN_STACK_ADDRESS, false);
    policy.loops = check_common_enabled(pp, input, CHECK_WARNING_ID_FOR_LOOP_ANALYSIS, true);
    policy.unsequenced = check_common_enabled(pp, input, CHECK_WARNING_ID_UNSEQUENCED, false);
    policy.shift_sign_overflow = check_common_enabled(pp, input, CHECK_WARNING_ID_SHIFT_SIGN_OVERFLOW, true);
    policy.assign_enum = check_common_enabled(pp, input, CHECK_WARNING_ID_ASSIGN_ENUM, true);
    policy.type_safety = check_common_enabled(pp, input, CHECK_WARNING_ID_TYPE_SAFETY, false) && input->type_tags != NULL &&
                         input->type_tags->declaration_count != 0;
    // A demoted or ignored default-error conversion is made only at a relaxed token, and warned of there (#1963).
    policy.unmodeled |= input->relaxed_tokens;
    for (uint32_t id = 0; id < CHECK_WARNING_ID_COUNT; id += 1)
    {
        const uint8_t class = node_census_warning_classes[id];
        policy.unmodeled |= (class == NODE_CENSUS_WARNING_WHOLE && check_common_enabled(pp, input, id, true)) ||
                            (class == NODE_CENSUS_WARNING_UNCLASSIFIED && check_common_enabled(pp, input, id, false));
    }
    return policy;
}

// Whether the unit keeps `event`: not suppressed, and its warning enabled somewhere in the unit.
static bool node_census_event_kept(const Preprocessor* pp, const CheckInput* input, const CheckWarningEvent* event)
{
    const bool result = (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) == 0 && event->id < CHECK_WARNING_ID_COUNT &&
                        check_common_enabled(pp, input, event->id, (event->flags & BUSTER_CHECK_WARNING_DEFAULT_IGNORED) != 0);
    return result;
}

// The kept warning events of the unit's views (the reader's, the stream's and the path's), as a running count by token
// and each token's first ID: whether there was room.
static bool node_census_events(NodeCensusScratch* s, const CheckCorpusPath* path, Arena* arena)
{
    const CheckProgram* program = s->program;
    const uint32_t count = program->parser->tokens.count;
    s->event_prefix = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    s->event_ids = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    s->packed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    const bool room = s->event_prefix != NULL && s->event_ids != NULL && s->packed != NULL;
    if (room)
    {
        memset(s->packed, 0, (uint64_t)count + 1);
        const CheckInput* input = program->input;
        for (uint32_t i = 0; i < input->packed_member_count; i += 1)
        {
            const CheckInputPackedMember* member = input->packed_members + i;
            if (member->address < count && !member->suppressed)
            {
                s->packed[member->address] = 1;
            }
        }
        memset(s->event_prefix, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(s->event_ids, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        const CheckWarningEvents views[3] = { path->input.warning_events, program->stream->warning_events, check_warning_view(&path->warning_events) };
        for (uint32_t v = 0; v < 3; v += 1)
        {
            for (uint32_t i = 0; i < views[v].count; i += 1)
            {
                const CheckWarningEvent* event = views[v].events + i;
                if (event->token < count && node_census_event_kept(s->pp, program->input, event))
                {
                    s->event_prefix[event->token + 1] = 1;
                    s->event_ids[event->token] = s->event_ids[event->token] == UINT32_MAX ? event->id : s->event_ids[event->token];
                }
            }
        }
        for (uint32_t t = 0; t < count; t += 1)
        {
            s->event_prefix[t + 1] += s->event_prefix[t];
        }
    }
    return room;
}

// Type predicates, each on a value's type as the stream holds it.
static TypeId node_census_base(const TypeTable* types, TypeId type)
{
    return type != 0 ? type_conversion_operand(types, type) : 0;
}

static uint8_t node_census_kind(const TypeTable* types, TypeId type)
{
    return type != 0 ? type_kind(types, node_census_base(types, type)) : BUSTER_TYPE_KIND_NONE;
}

// A complex, _BitInt, atomic, vector, half-precision or __float128 type: the values the machine has no kind for.
static bool node_census_special_type(const TypeTable* types, TypeId type)
{
    const bool atomic = type != 0 && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC;
    const TypeId base = node_census_base(types, type);
    const uint8_t kind = type != 0 ? type_kind(types, base) : BUSTER_TYPE_KIND_NONE;
    const bool special_kind = kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_VECTOR ||
                              kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    const bool special_code = base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT16) || base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_HALF) ||
                              base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BF16) || base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT128);
    return atomic || special_kind || special_code;
}

static bool node_census_aggregate(const TypeTable* types, TypeId type)
{
    const uint8_t kind = node_census_kind(types, type);
    return kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
}

static bool node_census_pointer(const TypeTable* types, TypeId type)
{
    const uint8_t kind = node_census_kind(types, type);
    return kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR;
}

static bool node_census_enum(const TypeTable* types, TypeId type)
{
    return node_census_kind(types, type) == BUSTER_TYPE_KIND_ENUM;
}

static bool node_census_floating(const TypeTable* types, TypeId type)
{
    return node_census_kind(types, type) == BUSTER_TYPE_KIND_FLOATING;
}

// An integer type's width and sign (an enumeration's underlying type's, bool's 1 bit unsigned): whether it is one.
static bool node_census_integer(const TypeTable* types, TypeId type, uint32_t* width, bool* sign)
{
    const TypeId base = node_census_base(types, type);
    *width = 0;
    *sign = false;
    const bool integer = type != 0 && check_action_integer_type(types, base, width, sign);
    *width = integer && base == BUSTER_TYPE_BOOL ? 1 : *width;
    return integer && *width != 0 && *width <= 64;
}

// The function type a callee's value calls: its own, or its pointer's pointee; 0 for neither.
static TypeId node_census_function_type(const TypeTable* types, TypeId type)
{
    TypeId result = 0;
    const TypeId base = node_census_base(types, type);
    const uint8_t kind = type != 0 ? type_kind(types, base) : BUSTER_TYPE_KIND_NONE;
    if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        result = type_strip_aligned(types, base);
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId pointee = type_unqualified(type_strip_aligned(types, type_element(types, base)));
        result = type_kind(types, pointee) == BUSTER_TYPE_KIND_FUNCTION ? pointee : 0;
    }
    return result;
}

// A node's value as an integer constant of at most 64 bits: whether it is one, and its value, exact, in `value`.
static bool node_census_integer_constant(const CheckStream* stream, const TypeTable* types, uint32_t node, __int128* value)
{
    bool result = false;
    *value = 0;
    if (node < stream->count && check_stream_constant_kind(stream, node) == CHECK_CONSTANT_INTEGER)
    {
        const CheckValue v = check_stream_value(stream, node);
        uint32_t width = 0;
        bool sign = false;
        result = v.fault == CHECK_FAULT_NONE && node_census_integer(types, v.type, &width, &sign);
        if (result)
        {
            const uint64_t raw = (uint64_t)v.integer;
            const uint32_t shift = 64 - width;
            const uint64_t bits = width == 64 ? raw : raw & ((UINT64_C(1) << width) - 1);
            *value = sign ? (__int128)((int64_t)(bits << shift) >> shift) : (__int128)bits;
        }
    }
    return result;
}

// An integer type's least and greatest values, of `width` bits and `sign`.
static void node_census_range(uint32_t width, bool sign, __int128* smallest, __int128* largest)
{
    *smallest = sign ? -((__int128)1 << (width - 1)) : 0;
    *largest = sign ? ((__int128)1 << (width - 1)) - 1 : ((__int128)1 << width) - 1;
}

// Whether `value` survives a conversion to an integer type of `width` and `sign`, as a value or as the same bits of the
// other signedness (a negative value to an unsigned type changes only its sign, which -Wsign-conversion would say).
static bool node_census_fits(__int128 value, uint32_t width, bool sign)
{
    __int128 smallest = 0;
    __int128 largest = 0;
    node_census_range(width, true, &smallest, &largest);
    __int128 unsigned_smallest = 0;
    __int128 unsigned_largest = 0;
    node_census_range(width, false, &unsigned_smallest, &unsigned_largest);
    const bool result = value >= smallest && value <= (sign ? largest : unsigned_largest);
    return result;
}

// An integer type's width, 128 bits at most (a _BitInt's past it, 0), and whether it is signed, before promotion.
static uint32_t node_census_width(const TypeTable* types, TypeId type, bool* sign)
{
    uint32_t width = 0;
    *sign = false;
    const bool integer = type != 0 && check_action_integer_type(types, node_census_base(types, type), &width, sign);
    return integer && width <= 128 ? width : 0;
}

// Whether the operator at token `token` has an unparenthesized operand whose own operator is at `inner`: the parser's tree
// gives a parenthesized operand a group as its parent.
static bool node_census_unparenthesized(const Parser* parser, uint32_t inner, uint32_t token)
{
    return inner < parser->tokens.count && token < parser->tokens.count && check_common_parent(parser, inner) == token;
}

// Whether a declaration token's attributes have a kind of `kinds`' (a bit a CheckInputAttributeKind), or its canonical
// declaration's.
static bool node_census_attributed(const CheckInput* input, uint32_t declaration, const CheckInputAttributeKind* kinds, uint32_t kind_count)
{
    bool found = false;
    const uint32_t canonical = input->declarations != NULL && declaration < input->token_count ? input->declarations[declaration] : BUSTER_CHECK_NONE;
    const uint32_t at[2] = { declaration, canonical };
    for (uint32_t d = 0; d < 2 && !found; d += 1)
    {
        const CheckInputAttributes* attributes = at[d] < input->token_count ? check_input_attributes_at(input, at[d]) : NULL;
        for (uint32_t k = 0; attributes != NULL && k < kind_count && !found; k += 1)
        {
            found = check_input_attribute_of(attributes, kinds[k]) != NULL;
        }
    }
    return found;
}

// The visible declaration of the name at `token`, or BUSTER_CHECK_NONE.
static uint32_t node_census_declaration(const CheckProgram* program, uint32_t token)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const bool name = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
    return name && input->declarations != NULL && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
}

static const CheckInputAttributeKind node_census_flagged_kinds[] = {
    CHECK_ATTRIBUTE_DEPRECATED, CHECK_ATTRIBUTE_UNAVAILABLE, CHECK_ATTRIBUTE_ERROR, CHECK_ATTRIBUTE_WARNING, CHECK_ATTRIBUTE_DIAGNOSE_IF,
    CHECK_ATTRIBUTE_ENABLE_IF, CHECK_ATTRIBUTE_NODEREF, CHECK_ATTRIBUTE_OVERLOADABLE,
};

static const CheckInputAttributeKind node_census_callee_kinds[] = {
    CHECK_ATTRIBUTE_FORMAT, CHECK_ATTRIBUTE_FORMAT_ARG, CHECK_ATTRIBUTE_SENTINEL, CHECK_ATTRIBUTE_RETURNS_TWICE, CHECK_ATTRIBUTE_DIAGNOSE_IF,
    CHECK_ATTRIBUTE_ENABLE_IF, CHECK_ATTRIBUTE_ERROR, CHECK_ATTRIBUTE_WARNING, CHECK_ATTRIBUTE_DEPRECATED, CHECK_ATTRIBUTE_UNAVAILABLE,
    CHECK_ATTRIBUTE_OVERLOADABLE, CHECK_ATTRIBUTE_PASS_OBJECT_SIZE, CHECK_ATTRIBUTE_PASS_DYNAMIC_OBJECT_SIZE, CHECK_ATTRIBUTE_THREAD_SAFETY,
    CHECK_ATTRIBUTE_NODEREF, CHECK_ATTRIBUTE_INTERRUPT,
};

static const CheckInputAttributeKind node_census_result_kinds[] = {
    CHECK_ATTRIBUTE_WARN_UNUSED_RESULT, CHECK_ATTRIBUTE_CONST, CHECK_ATTRIBUTE_PURE,
};

// Whether a call's callee has semantics of its own (the set's): a library builtin's fold, a printf-family name, an
// attribute of node_census_callee_kinds, or a function type without a prototype.
static bool node_census_callee(const NodeCensusScratch* s, uint32_t call)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const Parser* parser = program->parser;
    const uint32_t member = graph->members != NULL ? graph->members[call] : 0;
    const uint32_t callee = graph->operand0[call];
    bool result = (member & BUSTER_CHECK_LIBRARY_CALL) != 0 || callee >= graph->count;
    if (!result)
    {
        const TypeId function = node_census_function_type(types, check_stream_type(stream, callee));
        result = function == 0 || (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
    }
    if (!result && graph->ops[callee] == CHECK_OP_VALUE)
    {
        const uint32_t token = graph->tokens[callee];
        const uint32_t declaration = node_census_declaration(program, token);
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t name_id = parser->tokens.names[token];
            const String8 name = { .pointer = (char*)intern_bytes(s->pp->table, name_id), .length = intern_length(s->pp->table, name_id) };
            result = check_common_format_index(name, &s->pp->options) != UINT32_MAX ||
                     node_census_attributed(program->input, declaration, node_census_callee_kinds, BUSTER_ARRAY_LENGTH(node_census_callee_kinds));
        }
    }
    return result;
}

// Whether the name `name` is a builtin's whose row in clang's table is const or pure (c, g or U), as the ignored-result
// pass reads it (check_common_ignored_builtin): a plain library function's (f) only where the unit keeps it a builtin.
static bool node_census_builtin_result(const Preprocessor* pp, uint32_t name)
{
    const InternSpan span = pp->table->spans[name];
    const BuiltinIntrinsicName spelled = { (const char*)pp->table->records + span.offset, span.length };
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_filter_candidates(&spelled, 1, pp->options.scalar) != 0 ?
                                       builtin_intrinsic_lookup(spelled.name, spelled.length) : NULL;
    bool result = false;
    bool library = false;
    bool argument = false;
    for (uint32_t i = 0; info != NULL && i < info->attributes_length; i += 1)
    {
        // An attribute's argument (a format's `p:0:`) is between colons.
        const char letter = info->attributes[i];
        argument = letter == ':' ? !argument : argument;
        result |= !argument && letter != ':' && (letter == 'c' || letter == 'g' || letter == 'U');
        library |= !argument && letter == 'f';
    }
    return result && (!library || pp_library_builtin(&pp->options, pp->table->records + span.offset, span.length));
}

// Whether a discarded call's callee asks for its result: warn_unused_result, const or pure, or a const or pure builtin's
// name.
static bool node_census_wants_result(const NodeCensusScratch* s, uint32_t call)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    const uint32_t callee = graph->operand0[call];
    bool result = false;
    if (callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE)
    {
        const uint32_t token = graph->tokens[callee];
        const uint32_t declaration = node_census_declaration(program, token);
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t name_id = parser->tokens.names[token];
            result = node_census_builtin_result(s->pp, name_id) ||
                     node_census_attributed(program->input, declaration, node_census_result_kinds, BUSTER_ARRAY_LENGTH(node_census_result_kinds));
        }
    }
    return result;
}

// Whether node `node` is a string literal's value.
static bool node_census_string(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    return node < graph->count && graph->ops[node] == CHECK_OP_VALUE && token < program->parser->tokens.count &&
           program->parser->tokens.kinds[token] == PARSE_KIND_STRING;
}

// The object a node names for the unsequenced and self-assignment tests: a VALUE's object, or a member's of such a value
// (not through a pointer) by its offset, as clang's sequence checker follows them; 0 for none.
static uint64_t node_census_named_object(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint32_t at = node;
    uint64_t offset = 0;
    uint32_t members = 0;
    for (uint32_t steps = 0; at < graph->count && graph->ops[at] == CHECK_OP_MEMBER && steps < 64; steps += 1)
    {
        offset += stream->member_offsets != NULL ? stream->member_offsets[at] : 0;
        members += 1;
        at = graph->operand0[at];
    }
    const uint32_t object = at < graph->count && graph->ops[at] == CHECK_OP_VALUE ? check_stream_object(stream, at) : 0;
    return object != 0 ? (uint64_t)object << 32 ^ (offset << 6) ^ members : 0;
}

// The local object a node names through members and array subscripts (its address the node's), for the return test: 0
// for none, or a static or global one.
static uint32_t node_census_local_object(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    uint32_t at = node;
    bool walking = true;
    for (uint32_t steps = 0; walking && at < graph->count && steps < 64; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        const uint32_t base = graph->operand0[at];
        const bool subscript = op == CHECK_OP_SUBSCRIPT && base < graph->count && type_kind_is_array(node_census_kind(types, check_stream_type(stream, base)));
        walking = op == CHECK_OP_MEMBER || subscript;
        at = walking ? base : at;
    }
    const uint32_t object = at < graph->count && graph->ops[at] == CHECK_OP_VALUE ? check_stream_object(stream, at) : 0;
    const bool local = object != 0 && object < program->object_count && program->object_symbols[object] == 0 &&
                       !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC);
    return local ? object : 0;
}

// Whether the value a return gives is a local's address, followed through conversions, casts, a comma's value, a
// conditional's arms and pointer arithmetic: an address of a local, or a local array's value, which decays to it. The
// operands followed wait in a list of BUSTER_NODE_CENSUS_FOLLOWED (a deeper tree's further nodes are refused).
#define BUSTER_NODE_CENSUS_FOLLOWED 64u
static bool node_census_returns_address(const CheckProgram* program, uint32_t root)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    uint32_t pending[BUSTER_NODE_CENSUS_FOLLOWED];
    uint32_t count = 0;
    uint32_t visited = 0;
    bool result = false;
    pending[count++] = root;
    while (count != 0 && !result)
    {
        const uint32_t node = pending[--count];
        visited += 1;
        if (node < graph->count)
        {
            const CheckOp op = graph->ops[node];
            const bool through = op == CHECK_OP_CONVERT || op == CHECK_OP_CAST || op == CHECK_OP_INITIALIZE;
            const bool both = op == CHECK_OP_CONDITIONAL || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
            const uint32_t first = through ? graph->operand0[node] : op == CHECK_OP_COMMA ? graph->operand1[node] :
                                   op == CHECK_OP_CONDITIONAL ? graph->operand1[node] : both ? graph->operand0[node] : BUSTER_CHECK_NONE;
            const uint32_t second = op == CHECK_OP_CONDITIONAL ? graph->operand2[node] : both ? graph->operand1[node] : BUSTER_CHECK_NONE;
            if (op == CHECK_OP_ADDRESS)
            {
                result = node_census_local_object(program, graph->operand0[node]) != 0;
            }
            else if (first == BUSTER_CHECK_NONE)
            {
                result = type_kind_is_array(node_census_kind(types, check_stream_type(stream, node))) && node_census_local_object(program, node) != 0;
            }
            else
            {
                // A tree past the list's room, or past as many visits, is refused rather than followed.
                result = count + 2 > BUSTER_NODE_CENSUS_FOLLOWED || visited > BUSTER_NODE_CENSUS_FOLLOWED;
                pending[count] = first;
                count += !result;
                if (second != BUSTER_CHECK_NONE && !result)
                {
                    pending[count++] = second;
                }
            }
        }
    }
    return result;
}

static bool node_census_modifies(CheckOp op)
{
    return op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
}

static bool node_census_comparison(CheckOp op)
{
    return op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
}

// Whether `token` was written in a system header, as the common warnings find it (check_common_severity): the graph's
// mark, or the preprocessor's level at the token's place.
static bool node_census_system(const NodeCensusScratch* s, uint32_t token)
{
    const CheckProgram* program = s->program;
    const Parser* parser = program->parser;
    const Preprocessor* pp = s->pp;
    bool system = check_graph_system_token(&program->input->graph, token);
    if (!system && pp->output.locations != NULL && token < parser->tokens.count && parser->tokens.sources[token] < pp->output.count)
    {
        system = pp_system_level_at(pp, pp->output.locations[parser->tokens.sources[token]]) != BUSTER_PP_USER_FILE;
    }
    return system;
}

// Whether `op` has a code of the machine's (docs/node-machine.md, "T: bind and type").
static bool node_census_coded(CheckOp op)
{
    const bool result = op <= CHECK_OP_CONDITION || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    return result;
}

// Whether `inner` is in the subtree of `outer` within the current expression (its parents' chain reaches it).
static bool node_census_under(const NodeCensusScratch* s, uint32_t inner, uint32_t outer)
{
    uint32_t at = inner;
    while (at != BUSTER_CHECK_NONE && at != outer)
    {
        at = s->parents[at];
    }
    return at == outer;
}

// Whether two nodes of the current expression are apart from a sequence point, or never both evaluated: their lowest
// common ancestor is a comma, a logical operator or a conditional.
static bool node_census_sequenced(const NodeCensusScratch* s, const CheckGraph* graph, uint32_t a, uint32_t b)
{
    uint32_t x = a;
    uint32_t y = b;
    uint32_t below_x = BUSTER_CHECK_NONE;
    uint32_t below_y = BUSTER_CHECK_NONE;
    while (s->depths[x] > s->depths[y])
    {
        below_x = x;
        x = s->parents[x];
    }
    while (s->depths[y] > s->depths[x])
    {
        below_y = y;
        y = s->parents[y];
    }
    while (x != y)
    {
        below_x = x;
        below_y = y;
        x = s->parents[x];
        y = s->parents[y];
    }
    // A conditional's condition is sequenced before its arms, and of its arms one alone is evaluated.
    const CheckOp op = graph->ops[x];
    const bool result = op == CHECK_OP_COMMA || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL;
    (void)below_x;
    (void)below_y;
    return result;
}

// The unsequenced test over the current expression's `count` nodes: an object written twice, or written by an operator
// below the root and named outside its subtree, the two not apart from a sequence point.
static bool node_census_unsequenced(const NodeCensusScratch* s, uint32_t count, uint32_t root)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    bool found = false;
    for (uint32_t i = 0; i < count && !found; i += 1)
    {
        const uint32_t write = s->list[i];
        const uint64_t object = node_census_modifies(graph->ops[write]) ? node_census_named_object(program, graph->operand0[write]) : 0;
        for (uint32_t j = 0; object != 0 && j < count && !found; j += 1)
        {
            const uint32_t other = s->list[j];
            const bool named = other != write && other != graph->operand0[write] && node_census_named_object(program, other) == object &&
                               (graph->ops[other] == CHECK_OP_VALUE || graph->ops[other] == CHECK_OP_MEMBER) &&
                               !(s->parents[other] != BUSTER_CHECK_NONE && graph->ops[s->parents[other]] == CHECK_OP_MEMBER);
            const bool another_write = other != write && node_census_modifies(graph->ops[other]) && node_census_named_object(program, graph->operand0[other]) == object;
            const bool inside = named && node_census_under(s, other, write);
            const bool below_root = write != root;
            found = (another_write && !node_census_sequenced(s, graph, write, other)) ||
                    (named && !inside && below_root && !node_census_sequenced(s, graph, write, other));
        }
    }
    return found;
}

// Whether a discarded node gives -Wunused-value or its kin, following the discard down a comma's and a conditional's
// operands: a value without an effect, or a call whose callee asks for its result. A cast to void is explicit. The
// operands followed wait in a list of BUSTER_NODE_CENSUS_FOLLOWED (a deeper tree is refused).
static bool node_census_discarded(const NodeCensusScratch* s, uint32_t root)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    uint32_t pending[BUSTER_NODE_CENSUS_FOLLOWED];
    uint32_t count = 0;
    uint32_t visited = 0;
    bool result = false;
    pending[count++] = root;
    while (count != 0 && !result)
    {
        const uint32_t node = pending[--count];
        visited += 1;
        if (node < graph->count)
        {
            const CheckOp op = graph->ops[node];
            const TypeId type = check_stream_type(stream, node);
            const bool to_void = type != 0 && node_census_kind(types, type) == BUSTER_TYPE_KIND_VOID;
            if (op == CHECK_OP_COMMA || op == CHECK_OP_CONDITIONAL)
            {
                result = count + 2 > BUSTER_NODE_CENSUS_FOLLOWED || visited > BUSTER_NODE_CENSUS_FOLLOWED;
                pending[count] = op == CHECK_OP_COMMA ? graph->operand0[node] : graph->operand1[node];
                count += !result;
                pending[count] = op == CHECK_OP_COMMA ? graph->operand1[node] : graph->operand2[node];
                count += !result;
            }
            else if (op == CHECK_OP_CALL)
            {
                result = s->policy.unused_result && node_census_wants_result(s, node);
            }
            else if (!(op == CHECK_OP_CAST && to_void) && !node_census_modifies(op) && !to_void)
            {
                result = (s->policy.unused_value || s->policy.unused_comparison) && (check_stream_flags(stream, node) & BUSTER_CHECK_SIDE_EFFECT) == 0;
            }
        }
    }
    return result;
}

// Whether a node of the current expression is in a truth context: a `!`'s, a logical operator's or a conditional's
// condition's operand, or the root of a condition.
static bool node_census_truth(const NodeCensusScratch* s, uint32_t node, bool condition_root)
{
    const CheckGraph* graph = &s->program->input->graph;
    const uint32_t parent = s->parents[node];
    const CheckOp op = parent != BUSTER_CHECK_NONE ? graph->ops[parent] : CHECK_OP_COUNT;
    const bool result = (parent == BUSTER_CHECK_NONE && condition_root) || op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR ||
                        op == CHECK_OP_CONDITION || (op == CHECK_OP_CONDITIONAL && graph->operand0[parent] == node);
    return result;
}

// Whether a constant operand's conversion to `target` changes it: an integer constant that doesn't fit, a floating
// constant converted to an integer, or an integer one to a floating type that can't hold it exactly.
static bool node_census_constant_changes(const CheckStream* stream, const TypeTable* types, uint32_t operand, TypeId target)
{
    bool result = false;
    uint32_t width = 0;
    bool sign = false;
    __int128 value = 0;
    const CheckConstantKind kind = operand < stream->count ? check_stream_constant_kind(stream, operand) : CHECK_CONSTANT_NONE;
    // -Wassign-enum: an integer constant converted to an enumeration, whatever its value (node_census_constants' callers
    // ask only where it or -Wconstant-conversion is enabled).
    result = node_census_assign_enum && kind == CHECK_CONSTANT_INTEGER && target != 0 && node_census_enum(types, target);
    if (!result && target != 0 && kind == CHECK_CONSTANT_INTEGER && node_census_integer(types, target, &width, &sign))
    {
        // A conversion to a type at least as wide changes no value but its sign, which -Wsign-conversion says, not
        // -Wconstant-conversion: only a narrower one is tested.
        bool source_sign = false;
        const uint32_t source_width = node_census_width(types, check_stream_type(stream, operand), &source_sign);
        const bool narrows = source_width == 0 || source_width > width;
        result = narrows && (!node_census_integer_constant(stream, types, operand, &value) || !node_census_fits(value, width, sign));
    }
    else if (!result && target != 0 && kind == CHECK_CONSTANT_FLOATING && node_census_integer(types, target, &width, &sign))
    {
        result = true;
    }
    else if (!result && target != 0 && kind == CHECK_CONSTANT_INTEGER && node_census_floating(types, target))
    {
        // Exact where its significant bits fit the type's significand: float's 24, double's 53, long double's 64.
        const TypeId base = node_census_base(types, target);
        const uint32_t precision = base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT) ? 24 : base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_DOUBLE) ? 53 : 64;
        const bool known = node_census_integer_constant(stream, types, operand, &value);
        const uint64_t magnitude = value < 0 ? (uint64_t)(-value) : (uint64_t)value;
        const uint32_t span = magnitude != 0 ? 64 - (uint32_t)__builtin_clzll(magnitude) - (uint32_t)__builtin_ctzll(magnitude) : 0;
        result = !known || span > precision;
    }
    return result;
}

// The silence test of constants at node `node`: its operands' conversions, a division by zero, a shift's constant count or
// negative operand, a null dereference, a constant subscript outside its array, a comparison with a constant outside or at
// the bound of its other operand's type, a non-literal null converted to a pointer.
static bool node_census_constants(const NodeCensusScratch* s, uint32_t node)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const NodeCensusPolicy* policy = &s->policy;
    const CheckOp op = graph->ops[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    bool result = false;
    // The operands' implicit conversions, and a call's arguments'.
    const bool conversions = policy->constant_conversion || policy->assign_enum;
    for (uint32_t i = 0; i < 3 && op != CHECK_OP_CAST && op != CHECK_OP_VALUE && stream->conversions != NULL && conversions && !result; i += 1)
    {
        const uint32_t operand = i == 0 ? a : i == 1 ? b : graph->operand2[node];
        const CheckConversion conversion = stream->conversions[node].operands[i];
        result = operand < graph->count && node_census_constant_changes(stream, types, operand, conversion.type);
    }
    if (op == CHECK_OP_CALL && stream->argument_conversions != NULL && conversions && !result)
    {
        const uint32_t first = graph->argument_offsets[node];
        for (uint32_t i = 0; i < graph->argument_counts[node] && !result; i += 1)
        {
            result = node_census_constant_changes(stream, types, graph->argument_nodes[first + i], stream->argument_conversions[first + i].type);
        }
    }
    __int128 value = 0;
    const bool divide = op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN;
    result |= policy->division && divide && node_census_integer_constant(stream, types, b, &value) && value == 0;
    const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
    if (policy->shifts && shift && !result)
    {
        bool sign = false;
        const uint32_t width = a < graph->count ? node_census_width(types, check_stream_type(stream, a), &sign) : 0;
        const uint32_t promoted = width < 32 ? 32 : width;
        result = node_census_integer_constant(stream, types, b, &value) && (value < 0 || width == 0 || value >= promoted);
        const bool left_constant = node_census_integer_constant(stream, types, a, &value);
        result |= (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_LEFT_ASSIGN) && left_constant && value < 0;
    }
    // -Wshift-sign-overflow: a signed left shift by a constant (whose fold may set the sign bit).
    if (policy->shift_sign_overflow && (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_LEFT_ASSIGN) && !result)
    {
        bool sign = false;
        const uint32_t width = a < graph->count ? node_census_width(types, check_stream_type(stream, a), &sign) : 0;
        result = (sign || width < 32) && node_census_integer_constant(stream, types, b, &value);
    }
    const bool through = op == CHECK_OP_DEREFERENCE || op == CHECK_OP_ARROW || op == CHECK_OP_SUBSCRIPT;
    if (policy->null_dereference && through && !result)
    {
        const uint32_t pointer = op == CHECK_OP_SUBSCRIPT && b < graph->count && node_census_pointer(types, check_stream_type(stream, b)) ? b : a;
        const CheckConstantKind kind = pointer < graph->count ? check_stream_constant_kind(stream, pointer) : CHECK_CONSTANT_NONE;
        const CheckValue v = kind != CHECK_CONSTANT_NONE ? check_stream_value(stream, pointer) : (CheckValue){ 0 };
        result = (kind == CHECK_CONSTANT_INTEGER && v.integer == 0) || (kind == CHECK_CONSTANT_ADDRESS && v.object == 0);
    }
    if (policy->array_bounds && op == CHECK_OP_SUBSCRIPT && !result)
    {
        const bool a_array = a < graph->count && type_kind_is_array(node_census_kind(types, check_stream_type(stream, a)));
        const uint32_t array = a_array ? a : b;
        const uint32_t index = a_array ? b : a;
        const TypeId array_type = array < graph->count ? node_census_base(types, check_stream_type(stream, array)) : 0;
        const bool sized = array_type != 0 && type_kind(types, array_type) == BUSTER_TYPE_KIND_ARRAY;
        const uint64_t element = sized ? type_size(types, type_element(types, array_type)) : 0;
        const uint64_t length = element != 0 ? type_size(types, array_type) / element : 0;
        // `&a[N]` is the array's end, which C allows: an address of a subscript may be one past its last element.
        const bool addressed = s->parents[node] != BUSTER_CHECK_NONE && graph->ops[s->parents[node]] == CHECK_OP_ADDRESS;
        result = sized && node_census_integer_constant(stream, types, index, &value) && (value < 0 || value > (__int128)length || (value == (__int128)length && !addressed));
    }
    if (policy->tautological && node_census_comparison(op) && !result)
    {
        for (uint32_t side = 0; side < 2 && !result; side += 1)
        {
            const uint32_t constant = side == 0 ? a : b;
            const uint32_t other = side == 0 ? b : a;
            uint32_t width = 0;
            bool sign = false;
            bool constant_sign = false;
            const uint32_t constant_width = constant < graph->count ? node_census_width(types, check_stream_type(stream, constant), &constant_sign) : 0;
            bool known = node_census_integer_constant(stream, types, constant, &value) && other < graph->count &&
                         check_stream_constant_kind(stream, other) != CHECK_CONSTANT_INTEGER &&
                         node_census_integer(types, check_stream_type(stream, other), &width, &sign) && constant_width != 0;
            // The comparison's common type, by the usual arithmetic conversions of the promoted operands, and the
            // constant converted to it. A signed operand compared in an unsigned type has no interval of values there.
            const uint32_t promoted = width < 32 ? 32 : width;
            const bool promoted_sign = width < 32 ? true : sign;
            const uint32_t constant_promoted = constant_width < 32 ? 32 : constant_width;
            const bool constant_promoted_sign = constant_width < 32 ? true : constant_sign;
            const uint32_t common = promoted > constant_promoted ? promoted : constant_promoted;
            const bool common_sign = promoted_sign == constant_promoted_sign ? promoted_sign :
                                     promoted_sign ? (promoted > constant_promoted) : (constant_promoted > promoted);
            if (known && common < 128)
            {
                const unsigned __int128 mask = ((unsigned __int128)1 << common) - 1;
                const unsigned __int128 wrapped = (unsigned __int128)value & mask;
                const bool negative = common_sign && (wrapped >> (common - 1)) != 0;
                value = negative ? (__int128)wrapped - ((__int128)1 << common) : (__int128)wrapped;
            }
            known = known && common < 128 && !(sign && !common_sign);
            __int128 smallest = 0;
            __int128 largest = 0;
            node_census_range(known ? width : 1, sign, &smallest, &largest);
            // At a bound, the orderings that always hold or always fail: x < min, x >= min, x > max, x <= max, and their
            // mirrors with the constant on the left.
            const CheckOp mirrored = side == 1 ? op : op == CHECK_OP_LESS ? CHECK_OP_GREATER : op == CHECK_OP_GREATER ? CHECK_OP_LESS :
                                     op == CHECK_OP_LESS_EQUAL ? CHECK_OP_GREATER_EQUAL : op == CHECK_OP_GREATER_EQUAL ? CHECK_OP_LESS_EQUAL : op;
            const bool at_least = value == smallest && (mirrored == CHECK_OP_LESS || mirrored == CHECK_OP_GREATER_EQUAL);
            const bool at_most = value == largest && (mirrored == CHECK_OP_GREATER || mirrored == CHECK_OP_LESS_EQUAL);
            result = known && (value < smallest || value > largest || at_least || at_most);
        }
    }
    if (policy->non_literal_null && (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT) && !result)
    {
        const uint32_t source = op == CHECK_OP_ASSIGN ? b : a;
        const bool to_pointer = node_census_pointer(types, check_stream_type(stream, node));
        result = to_pointer && source < graph->count && graph->ops[source] != CHECK_OP_VALUE && graph->ops[source] != CHECK_OP_CAST &&
                 node_census_integer_constant(stream, types, source, &value) && value == 0;
    }
    return result;
}

// The tests of node `node` of the current expression (`root` its root, `context` its root's kind), as bits of
// NodeCensusTest.
static uint64_t node_census_node(NodeCensusScratch* s, uint32_t node, uint32_t root, NodeCensusRoot context)
{
    const CheckProgram* program = s->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const Parser* parser = program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    const CheckOp op = graph->ops[node];
    const CheckValue v = check_stream_value(stream, node);
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const TypeId a_type = a < graph->count && op != CHECK_OP_VALUE ? check_stream_type(stream, a) : 0;
    const TypeId b_type = b < graph->count && op != CHECK_OP_VALUE ? check_stream_type(stream, b) : 0;
    const uint32_t token = graph->tokens[node];
    uint64_t tests = 0;
    // The set.
    tests |= !node_census_coded(op) ? UINT64_C(1) << NODE_CENSUS_OPERATOR : 0;
    tests |= node_census_special_type(types, v.type) ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= types->atomic_records && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && check_common_atomic_member(graph, stream, types, node) ?
             UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= (v.flags & BUSTER_CHECK_BIT_FIELD) != 0 ? UINT64_C(1) << NODE_CENSUS_BIT_FIELD : 0;
    const bool copies = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                        op == CHECK_OP_CAST || op == CHECK_OP_CALL;
    bool aggregate = copies && node_census_aggregate(types, v.type);
    for (uint32_t i = 0; op == CHECK_OP_CALL && !aggregate && i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
        aggregate = argument < graph->count && node_census_aggregate(types, check_stream_type(stream, argument));
    }
    tests |= aggregate ? UINT64_C(1) << NODE_CENSUS_AGGREGATE : 0;
    if (op == CHECK_OP_VALUE)
    {
        const uint32_t object = v.object;
        const bool known = object != 0 && object < program->object_count;
        const bool compound = known && check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND);
        const bool register_object = known && check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_REGISTER);
        tests |= compound ? UINT64_C(1) << NODE_CENSUS_COMPOUND_LITERAL : 0;
        const uint32_t declaration = node_census_declaration(program, token);
        const bool flagged = v.overload_head != 0 || register_object || (v.flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) != 0 ||
                             (declaration != BUSTER_CHECK_NONE &&
                              node_census_attributed(input, declaration, node_census_flagged_kinds, BUSTER_ARRAY_LENGTH(node_census_flagged_kinds)));
        tests |= flagged ? UINT64_C(1) << NODE_CENSUS_DECLARATION : 0;
    }
    // -Wtype-safety reads a call's arguments against its callee's type tags: in a unit that declares any, every call.
    tests |= op == CHECK_OP_CALL && (node_census_callee(s, node) || policy->type_safety) ? UINT64_C(1) << NODE_CENSUS_CALLEE : 0;
    // The silence tests.
    const uint32_t warnings = stream->warnings != NULL ? stream->warnings[node] & 0x01ffffffu : 0;
    const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
    const uint32_t kept = (warnings & ~ignored & policy->action_bits) | (warnings & ignored & policy->action_ignored_bits);
    tests |= kept != 0 ? UINT64_C(1) << NODE_CENSUS_ACTION_WARNING : 0;
    const bool binary = op != CHECK_OP_VALUE && a < graph->count && b < graph->count;
    if (policy->enums && op != CHECK_OP_VALUE)
    {
        const TypeId left = op == CHECK_OP_CONDITIONAL ? b_type : a_type;
        const TypeId right = op == CHECK_OP_CONDITIONAL ? (graph->operand2[node] < graph->count ? check_stream_type(stream, graph->operand2[node]) : 0) : b_type;
        const bool both = left != 0 && right != 0 && node_census_enum(types, left) && node_census_enum(types, right) &&
                          node_census_base(types, left) != node_census_base(types, right);
        const bool floating = left != 0 && right != 0 && ((node_census_enum(types, left) && node_census_floating(types, right)) ||
                                                         (node_census_enum(types, right) && node_census_floating(types, left)));
        const bool converts = (op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE) && a_type != 0 && node_census_enum(types, a_type) &&
                              (node_census_enum(types, v.type) || node_census_floating(types, v.type)) && node_census_base(types, a_type) != node_census_base(types, v.type);
        tests |= both || floating || converts ? UINT64_C(1) << NODE_CENSUS_ENUM : 0;
    }
    if (policy->sign_compare && node_census_comparison(op) && binary)
    {
        uint32_t aw = 0;
        uint32_t bw = 0;
        bool as = false;
        bool bs = false;
        __int128 value = 0;
        // The comparison is unsigned where an operand is unsigned and as wide as int or wider (a narrower one promotes to
        // int); the other operand's sign then matters where it is a signed type's, but for a non-negative constant.
        const bool integers = node_census_integer(types, a_type, &aw, &as) && node_census_integer(types, b_type, &bw, &bs);
        const bool a_wide = !as && aw >= 32;
        const bool b_wide = !bs && bw >= 32;
        const bool a_safe = node_census_integer_constant(stream, types, a, &value) && value >= 0;
        const bool b_safe = node_census_integer_constant(stream, types, b, &value) && value >= 0;
        const bool mixed = (a_wide && bs && !b_safe) || (b_wide && as && !a_safe);
        tests |= integers && mixed ? UINT64_C(1) << NODE_CENSUS_SIGN_COMPARE : 0;
    }
    if (op == CHECK_OP_CAST && policy->pointer_casts && a_type != 0 && node_census_pointer(types, a_type))
    {
        uint32_t width = 0;
        bool sign = false;
        const bool narrow = node_census_integer(types, v.type, &width, &sign) && width < 64;
        tests |= narrow || node_census_enum(types, v.type) ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
    }
    if ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference && a_type != 0 && node_census_pointer(types, a_type))
    {
        const TypeId pointee = type_unqualified(type_strip_aligned(types, type_element(types, node_census_base(types, a_type))));
        tests |= type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
    }
    if (op == CHECK_OP_ADD && policy->string_plus_int && binary && (node_census_string(program, a) || node_census_string(program, b)))
    {
        tests |= UINT64_C(1) << NODE_CENSUS_STRING_PLUS_INT;
    }
    tests |= op == CHECK_OP_ADDRESS && policy->packed_member && token < parser->tokens.count && s->packed[token] != 0 ? UINT64_C(1) << NODE_CENSUS_PACKED_MEMBER : 0;
    if ((op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && a < graph->count)
    {
        const bool effect = policy->unevaluated && (check_stream_flags(stream, a) & BUSTER_CHECK_SIDE_EFFECT) != 0;
        const bool expression_alignof = op == CHECK_OP_ALIGNOF && policy->alignof_expression;
        // -Wsizeof-array-decay: sizeof of a pointer an operator made of an array (`sizeof(a + 1)`).
        const CheckOp inner = graph->ops[a];
        const bool operator = inner == CHECK_OP_ADD || inner == CHECK_OP_SUBTRACT || inner == CHECK_OP_CONDITIONAL || inner == CHECK_OP_COMMA;
        bool decays = false;
        for (uint32_t i = 0; policy->sizeof_decay && operator && i < 3 && !decays; i += 1)
        {
            const uint32_t operand = i == 0 ? graph->operand0[a] : i == 1 ? graph->operand1[a] : graph->operand2[a];
            decays = operand < graph->count && type_kind_is_array(node_census_kind(types, check_stream_type(stream, operand)));
        }
        tests |= effect || expression_alignof || decays ? UINT64_C(1) << NODE_CENSUS_UNEVALUATED : 0;
    }
    tests |= op != CHECK_OP_VALUE && node_census_constants(s, node) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    // The parser's tree: parentheses.
    const uint32_t count = parser->tokens.count;
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const uint32_t parent = token < count ? check_common_parent(parser, token) : BUSTER_CHECK_NONE;
    const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    bool parentheses = false;
    if (policy->parentheses && op == CHECK_OP_ASSIGN && role == PARSE_ROLE_ASSIGN && token < count && parser->tokens.kinds[token] == PARSE_KIND_EQUAL)
    {
        const bool for_condition = check_common_for_condition(parser, token, parent);
        const uint32_t opener = parent_role == PARSE_ROLE_CLOSE_CONDITION ? check_common_opener(parser, parent) : BUSTER_CHECK_NONE;
        const bool switch_condition = opener != BUSTER_CHECK_NONE && opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_SWITCH;
        parentheses = (parent_role == PARSE_ROLE_CLOSE_CONDITION || for_condition) && !switch_condition;
    }
    if (policy->parentheses && check_common_comparison_role(role) &&
        (parent_role == PARSE_ROLE_BITWISE_AND || parent_role == PARSE_ROLE_BITWISE_OR || parent_role == PARSE_ROLE_BITWISE_XOR))
    {
        parentheses |= check_common_bitwise_precedence(parser, token, parent);
    }
    parentheses |= policy->logical_not && op == CHECK_OP_NOT && check_common_comparison_role(parent_role) && token < parent;
    const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
    for (uint32_t i = 0; policy->shift_parentheses && shift && i < 2; i += 1)
    {
        const uint32_t operand = i == 0 ? a : b;
        const CheckOp inner = operand < graph->count ? graph->ops[operand] : CHECK_OP_COUNT;
        parentheses |= (inner == CHECK_OP_ADD || inner == CHECK_OP_SUBTRACT) && node_census_unparenthesized(parser, graph->tokens[operand], token);
    }
    for (uint32_t i = 0; policy->chained && node_census_comparison(op) && i < 2; i += 1)
    {
        const uint32_t operand = i == 0 ? a : b;
        parentheses |= operand < graph->count && node_census_comparison(graph->ops[operand]) && node_census_unparenthesized(parser, graph->tokens[operand], token);
    }
    const bool truth = node_census_truth(s, node, context == NODE_CENSUS_ROOT_CONDITION);
    __int128 value = 0;
    const bool constant_shift = op == CHECK_OP_SHIFT_LEFT && node_census_integer_constant(stream, types, a, &value);
    parentheses |= truth && ((policy->int_in_bool && (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_MULTIPLY)) || (policy->tautological && constant_shift));
    tests |= parentheses ? UINT64_C(1) << NODE_CENSUS_PARENTHESES : 0;
    bool unused = policy->unused_comparison && check_common_comparison_role(role) && parent < count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR &&
                  check_common_statement_terminator(parser, parent);
    unused |= op == CHECK_OP_COMMA && node_census_discarded(s, a);
    tests |= unused ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0;
    if (policy->self_assign && op == CHECK_OP_ASSIGN && binary)
    {
        const uint64_t left = node_census_named_object(program, a);
        const bool same = left != 0 && left == node_census_named_object(program, b);
        tests |= same ? UINT64_C(1) << NODE_CENSUS_SELF_ASSIGN : 0;
    }
    s->mixed_system |= node_census_system(s, token) && !node_census_system(s, graph->tokens[root]);
    tests |= input->fp_environments != NULL && graph->fp_environments != NULL && graph->fp_environments[node] != 0 ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
    return tests;
}

// Whether a root's token is a for statement's clause's: its `)` (the increment's) or a `;` of its head.
static bool node_census_for_clause(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const uint32_t parent = token < count ? parser->tree.parents[token] : count;
    const bool result = role == PARSE_ROLE_CLOSE_FOR || (role == PARSE_ROLE_TERMINATOR && parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FOR);
    return result;
}

// The declared type of the object whose initializer's whole value is node `root`, by its INITIALIZE control.
static TypeId node_census_initialized_type(const NodeCensusScratch* s, uint32_t root)
{
    const CheckProgram* program = s->program;
    const uint32_t control = s->node_controls[root];
    const uint32_t declaration = control != BUSTER_CHECK_NONE ? program->controls[control].token : BUSTER_CHECK_NONE;
    return declaration < program->input->token_count ? program->input->declared_types[declaration] : 0;
}

// The tests of the expression whose root is node `root`, popped with `flags`, of root kind `context`: its graph subtree
// walked into the scratch's list, then each node's tests and the expression's own.
static uint64_t node_census_expression(NodeCensusScratch* s, uint32_t root, uint8_t flags, NodeCensusRoot context, uint32_t* walked)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const Parser* parser = program->parser;
    s->stamp += 1;
    s->mixed_system = false;
    uint32_t count = 0;
    uint32_t top = 0;
    uint32_t lowest = UINT32_MAX;
    uint32_t highest = 0;
    s->marks[root] = s->stamp;
    s->parents[root] = BUSTER_CHECK_NONE;
    s->depths[root] = 0;
    s->stack[top++] = root;
    while (top != 0)
    {
        const uint32_t node = s->stack[--top];
        s->list[count++] = node;
        const uint32_t token = graph->tokens[node];
        lowest = token < parser->tokens.count && token < lowest ? token : lowest;
        highest = token < parser->tokens.count && token > highest ? token : highest;
        const CheckOp op = graph->ops[node];
        uint32_t children[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        if (op != CHECK_OP_VALUE && op != CHECK_OP_STATEMENT_VALUE)
        {
            children[0] = graph->operand0[node];
            children[1] = op != CHECK_OP_CALL ? graph->operand1[node] : BUSTER_CHECK_NONE;
            children[2] = op != CHECK_OP_CALL ? graph->operand2[node] : BUSTER_CHECK_NONE;
        }
        const uint32_t arguments = op == CHECK_OP_CALL ? graph->argument_counts[node] : 0;
        for (uint32_t i = 0; i < 3 + arguments; i += 1)
        {
            const uint32_t child = i < 3 ? children[i] : graph->argument_nodes[graph->argument_offsets[node] + i - 3];
            if (child < graph->count && s->marks[child] != s->stamp)
            {
                s->marks[child] = s->stamp;
                s->parents[child] = node;
                s->depths[child] = s->depths[node] + 1;
                s->stack[top++] = child;
            }
        }
    }
    uint64_t tests = 0;
    uint32_t lowest_node = root;
    uint32_t inner_node = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t node = s->list[i];
        tests |= node_census_node(s, node, root, context);
        lowest_node = node < lowest_node ? node : lowest_node;
        inner_node = node != root && node > inner_node ? node : inner_node;
    }
    s->lowest_node = lowest_node;
    s->inner_node = count > 1 ? inner_node : root;
    tests |= count > BUSTER_NODE_CENSUS_MOST ? UINT64_C(1) << NODE_CENSUS_SIZE : 0;
    tests |= context == NODE_CENSUS_ROOT_OTHER || context == NODE_CENSUS_ROOT_BRACE ? UINT64_C(1) << NODE_CENSUS_CONTROL : 0;
    tests |= (flags & BUSTER_CHECK_LOWER_OWNER_TRACKED) != 0 ? UINT64_C(1) << NODE_CENSUS_TRACKED : 0;
    // An expression statement's root is the reader's cast to void at its `;` (or a for clause's `)`): what it discards is
    // its operand, where an explicit cast's token is its own `)`.
    const uint32_t root_token = graph->tokens[root];
    const bool wrapper = graph->ops[root] == CHECK_OP_CAST && root_token < parser->tokens.count && parser->tree.roles[root_token] != PARSE_ROLE_CLOSE_CAST &&
                         parser->tree.roles[root_token] != PARSE_ROLE_OPEN_CAST;
    const uint32_t discarded_node = wrapper ? graph->operand0[root] : root;
    const bool discarded = context == NODE_CENSUS_ROOT_STATEMENT || (flags & 4u) != 0;
    tests |= discarded && node_census_discarded(s, discarded_node) ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0;
    tests |= s->policy.loops && node_census_for_clause(parser, graph->tokens[root]) ? UINT64_C(1) << NODE_CENSUS_LOOP : 0;
    tests |= s->policy.unmodeled ? UINT64_C(1) << NODE_CENSUS_UNMODELED : 0;
    // A scalar object's initializer converts its value to the object's type in its plan, not at a node.
    const TypeId declared = context == NODE_CENSUS_ROOT_INITIALIZER ? node_census_initialized_type(s, root) : 0;
    tests |= (s->policy.constant_conversion || s->policy.assign_enum) && declared != 0 && node_census_constant_changes(stream, program->types, root, declared) ?
             UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    tests |= declared != 0 && node_census_special_type(program->types, declared) ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= s->policy.return_address && context == NODE_CENSUS_ROOT_RETURN && node_census_returns_address(program, root) ?
             UINT64_C(1) << NODE_CENSUS_RETURN_ADDRESS : 0;
    tests |= s->policy.unsequenced && node_census_unsequenced(s, count, root) ? UINT64_C(1) << NODE_CENSUS_UNSEQUENCED : 0;
    const bool events = lowest <= highest && s->event_prefix[highest + 1] != s->event_prefix[lowest];
    tests |= events ? UINT64_C(1) << NODE_CENSUS_EVENT : 0;
    s->span_low = lowest;
    s->span_high = highest;
    *walked = count;
    return tests;
}

// The first event's ID in the current expression's span of tokens, for the report.
static uint32_t node_census_event_id(const NodeCensusScratch* s)
{
    uint32_t id = UINT32_MAX;
    for (uint32_t t = s->span_low; t <= s->span_high && id == UINT32_MAX; t += 1)
    {
        id = s->event_ids[t];
    }
    return id;
}

// The line of token `token`, for --list.
static uint32_t node_census_line(Preprocessor* pp, const Parser* parser, uint32_t token)
{
    uint32_t line = 0;
    if (token < parser->tokens.count && parser->tokens.sources[token] < pp->output.count && pp->output.locations != NULL)
    {
        const char* name = NULL;
        uint32_t length = 0;
        line = pp_presumed(pp, pp->output.locations[parser->tokens.sources[token]], &name, &length);
    }
    return line;
}

// One expression's counts, its tests `tests` and its `nodes` evaluated nodes, into `census`.
static void node_census_count(NodeCensus* census, NodeCensusScratch* s, uint64_t tests, uint64_t nodes, NodeCensusRoot context, uint32_t root)
{
    census->expressions += 1;
    census->nodes += nodes;
    census->root_expressions[context] += 1;
    census->root_nodes[context] += nodes;
    const bool set = (tests & NODE_CENSUS_SET_MASK) == 0;
    census->set_expressions += set;
    census->set_nodes += set ? nodes : 0;
    census->taken_expressions += tests == 0;
    census->taken_nodes += tests == 0 ? nodes : 0;
    census->root_taken_nodes[context] += tests == 0 ? nodes : 0;
    const bool single = tests != 0 && (tests & (tests - 1)) == 0;
    const uint32_t first = tests != 0 ? (uint32_t)__builtin_ctzll(tests) : NODE_CENSUS_TESTS;
    for (uint32_t t = 0; t < NODE_CENSUS_TESTS; t += 1)
    {
        const bool fired = (tests >> t & 1) != 0;
        census->any_expressions[t] += fired;
        census->any_nodes[t] += fired ? nodes : 0;
        census->only_expressions[t] += fired && single;
        census->only_nodes[t] += fired && single ? nodes : 0;
        census->first_expressions[t] += t == first;
        census->first_nodes[t] += t == first ? nodes : 0;
        if (fired && census->listed[t] < node_census_list)
        {
            const uint32_t token = s->program->input->graph.tokens[root];
            census->listed[t] += 1;
            printf("node-census: list test=%s file=%s line=%u root=%s nodes=%" PRIu64 " tests=%#" PRIx64 "\n", node_census_test_names[t], s->file,
                   node_census_line(s->pp, s->program->parser, token), node_census_root_names[context], nodes, tests);
        }
    }
    census->system_expressions += s->mixed_system;
    census->system_nodes += s->mixed_system ? nodes : 0;
    census->system_taken_nodes += s->mixed_system && tests == 0 ? nodes : 0;
    if (census->records != NULL && census->record_count < census->record_capacity)
    {
        const uint32_t token = s->program->input->graph.tokens[root];
        census->records[census->record_count++] = (NodeCensusRecord){ .line = node_census_line(s->pp, s->program->parser, token), .root = context, .tests = tests };
    }
    if ((tests >> NODE_CENSUS_EVENT & 1) != 0)
    {
        const uint32_t id = node_census_event_id(s);
        census->event_ids[id < CHECK_WARNING_ID_COUNT ? id : CHECK_WARNING_ID_COUNT] += 1;
    }
}

// The root kind of root pop `pop`: by the control whose node it is, or whose initializer's plan holds it as a part (a
// scalar object's whole value, or a brace initializer's or an aggregate's part, which the machine doesn't read). A root
// pushed under a nested control, a statement expression's, is another control's.
static NodeCensusRoot node_census_root(const NodeCensusScratch* s, const CheckLowerOwnerPop* pop)
{
    const CheckProgram* program = s->program;
    const uint32_t control_index = s->node_controls[pop->node];
    const CheckControl* control = control_index != BUSTER_CHECK_NONE ? program->controls + control_index : NULL;
    const CheckControlKind kind = control != NULL ? control->kind : CHECK_CONTROL_END;
    NodeCensusRoot root = NODE_CENSUS_ROOT_OTHER;
    if (control != NULL && (pop->pusher & BUSTER_CHECK_LOWER_OWNER_TOKEN) == BUSTER_CHECK_LOWER_OWNER_TOKEN && kind != CHECK_CONTROL_INITIALIZE)
    {
        root = kind == CHECK_CONTROL_EVALUATE ? NODE_CENSUS_ROOT_STATEMENT :
               kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE ? NODE_CENSUS_ROOT_CONDITION :
               kind == CHECK_CONTROL_RETURN && control->target != BUSTER_CHECK_CONTROL_TAIL ? NODE_CENSUS_ROOT_RETURN :
               kind == CHECK_CONTROL_SWITCH ? NODE_CENSUS_ROOT_SWITCH : NODE_CENSUS_ROOT_OTHER;
    }
    else if (control != NULL && (pop->pusher & BUSTER_CHECK_LOWER_OWNER_TOKEN) == BUSTER_CHECK_LOWER_OWNER_TOKEN)
    {
        const CheckInitializer* plan = program->initializers->plans + control->target;
        const CheckInitPart* part = plan->count == 1 ? plan->parts : NULL;
        const TypeTable* types = program->types;
        const TypeId declared = control->token < program->input->token_count ? program->input->declared_types[control->token] : 0;
        const uint8_t declared_kind = node_census_kind(types, declared);
        const bool scalar = declared != 0 && !node_census_aggregate(types, declared) && !type_kind_is_array(declared_kind);
        const Parser* parser = program->parser;
        bool brace = false;
        for (uint32_t t = control->token + 1; part != NULL && t < part->token && t < parser->tokens.count && !brace; t += 1)
        {
            brace = parser->tokens.kinds[t] == PARSE_KIND_L_BRACE;
        }
        const bool whole = part != NULL && part->kind == CHECK_INIT_VALUE && part->bit_offset == 0 && part->bit_width == 0 && part->node == pop->node;
        root = whole && scalar && !brace ? NODE_CENSUS_ROOT_INITIALIZER : NODE_CENSUS_ROOT_BRACE;
    }
    return root;
}

// Whether a body holds a diagnostic or floating-point pragma between its opening and its closing brace.
static bool node_census_region(const CheckProgram* program, const CheckProgramBody* body)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const uint32_t opening = body->opening;
    const uint32_t closing = opening < parser->tokens.count && parser->tree.parents[opening] < parser->tokens.count && parser->tree.parents[opening] > opening ?
                             parser->tree.parents[opening] : parser->tokens.count;
    bool found = false;
    const CheckInputDiagnosticPragmas* pragmas = input->diagnostic_pragmas;
    for (uint32_t i = 0; pragmas != NULL && i < pragmas->count && !found; i += 1)
    {
        found = pragmas->records[i].token > opening && pragmas->records[i].token < closing;
    }
    for (uint32_t i = 0; input->fp_pragmas != NULL && i < input->fp_pragma_count && !found; i += 1)
    {
        found = input->fp_pragmas[i].token > opening && input->fp_pragmas[i].token < closing;
    }
    return found;
}

// Marks (or clears) each control's node of `body` with the control's index, and each part's node of its initializers'
// plans with its INITIALIZE control's.
static void node_census_mark_controls(NodeCensusScratch* s, const CheckProgramBody* body, bool mark)
{
    const CheckProgram* program = s->program;
    const CheckInitializers* initializers = program->initializers;
    const uint32_t count = program->stream->count;
    for (uint32_t c = body->first; c < body->first + body->count; c += 1)
    {
        const CheckControl* control = program->controls + c;
        const uint32_t value = mark ? c : BUSTER_CHECK_NONE;
        if (control->node < count)
        {
            s->node_controls[control->node] = value;
        }
        if (control->kind == CHECK_CONTROL_INITIALIZE && initializers != NULL && control->target < initializers->count)
        {
            const CheckInitializer* plan = initializers->plans + control->target;
            for (uint32_t p = 0; p < plan->count; p += 1)
            {
                if (plan->parts[p].node < count)
                {
                    s->node_controls[plan->parts[p].node] = value;
                }
            }
        }
    }
}

// #2524's step 1, the shadow (--shadow): the node machine (c_check_node_machine.c) run on every expression the census
// takes, after the pipeline, its records compared with the stream's values and conversions node by node and its tape
// with the lowering's entries entry by entry, then timed. --scalar types every node by the action, --verify checks T's
// rows and rules against the action at every node they type, --passes N times each file's taken expressions N times a
// sweep set, and --repeat N runs each expression's sweeps N times in a row within a pass (the runs after the first find
// their inputs in the caches).

// One expression the machine took, for the timing passes.
typedef struct NodeShadowExpression NodeShadowExpression;
struct NodeShadowExpression
{
    uint32_t first;
    uint32_t root;
    uint32_t count;
    uint32_t object;
    TypeId target;
    uint8_t context;
};

// The sweeps timed: T alone, T and C, and all three.
#define BUSTER_NODE_SHADOW_SWEEPS 3u

// One expression's result, for the shadow's tests (NodeShadow.records_out): its root's line and the machine's refusal.
typedef struct NodeShadowRecord NodeShadowRecord;
struct NodeShadowRecord
{
    uint32_t line;
    NodeMachineRefusal refusal;
};

typedef struct NodeShadow NodeShadow;
struct NodeShadow
{
    // Optional: each expression's result, up to `record_capacity` of them (c_check_node_census_test.c's).
    NodeShadowRecord* records_out;
    uint32_t record_count;
    uint32_t record_capacity;
    bool scalar;
    bool verify;
    uint32_t passes;
    // --repeat N: each expression's sweeps N times in a row in the timing passes (1 by default).
    uint32_t repeat;
    // The expressions the census took and their evaluated nodes, and of them those the machine took.
    uint64_t expressions;
    uint64_t nodes;
    uint64_t records;
    uint64_t taken_expressions;
    uint64_t taken_nodes;
    uint64_t taken_records;
    uint64_t entries;
    // The taken expressions' calls, each compared with its row of the function's calls table, and their arguments.
    uint64_t calls;
    uint64_t arguments;
    // The argument memo's entries T filled and the arguments it took by the rule instead, over every expression's T.
    uint64_t memo_fills;
    uint64_t memo_rules;
    // Each refusal's expressions and their evaluated nodes, and the operators the code refusal stopped at.
    uint64_t refused_expressions[NODE_MACHINE_REFUSALS];
    uint64_t refused_nodes[NODE_MACHINE_REFUSALS];
    uint64_t refused_codes[CHECK_OP_COUNT];
    uint64_t refused_templates[CHECK_OP_COUNT];
    // The taken expressions with a record whose value, conversions or member offset differ from the stream's, and those
    // whose tape differs from the lowering's entries.
    uint64_t value_differences;
    uint64_t entry_differences;
    uint64_t listed;
    // The taken expressions with labels (a logical operator's or a conditional's), and their labels, each compared with the
    // lowering's by its number from the expression's first and by its entry (node_shadow_same_label).
    uint64_t labeled_expressions;
    uint64_t labels;
    // T's nodes by row and by action, and the row-typed nodes the action types otherwise (--verify).
    uint64_t row_nodes;
    uint64_t action_nodes;
    uint64_t twin_differences;
    uint64_t action_ops[CHECK_OP_COUNT];
    // The timing: each file's least cycles, instructions and TSC ticks over the passes for each sweep set, summed, and
    // the evaluated nodes and records they took. `counted`: whether the cycle counter could be opened.
    uint64_t cycles[BUSTER_NODE_SHADOW_SWEEPS];
    uint64_t instructions[BUSTER_NODE_SHADOW_SWEEPS];
    uint64_t ticks[BUSTER_NODE_SHADOW_SWEEPS];
    uint64_t timed_nodes;
    bool counted;
    // The current file's taken expressions, and their evaluated nodes.
    NodeShadowExpression* timed;
    uint64_t file_nodes;
    uint32_t timed_count;
    uint32_t timed_capacity;
};

// Whether a local's entry: its value is a local's number in the stream and its object on the tape.
static bool node_shadow_local_kind(uint8_t kind)
{
    return kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
}

// Whether the tape's local `object` is the stream's local `local` in the current body: the first time either is met it is
// the other's, and every later time it must be. A temporary of the expression's (BUSTER_NODE_MACHINE_TEMPORARY, numbered in
// the order the tape first names them) is a local the body hasn't met, the first time, and that local every later time.
static bool node_shadow_same_local(NodeCensusScratch* s, uint64_t object, uint64_t local)
{
    const uint32_t stamp = s->body_stamp;
    const bool temporary = (object & BUSTER_NODE_MACHINE_TEMPORARY) != 0;
    const uint64_t number = object & ~BUSTER_NODE_MACHINE_TEMPORARY;
    const bool fits = (temporary ? number <= s->temporary_count && number < BUSTER_NODE_MACHINE_MOST : object < s->program->object_count) && local < s->function->local_count;
    bool same = fits;
    if (fits && temporary && number == s->temporary_count)
    {
        same = s->local_stamps[local] != stamp;
        s->local_stamps[local] = stamp;
        s->local_objects[local] = UINT32_MAX;
        s->temporary_locals[number] = (uint32_t)local;
        s->temporary_count += 1;
    }
    else if (fits && temporary)
    {
        same = s->temporary_locals[number] == local;
    }
    else if (fits && s->object_stamps[object] != stamp && s->local_stamps[local] != stamp)
    {
        s->object_stamps[object] = stamp;
        s->object_locals[object] = (uint32_t)local;
        s->local_stamps[local] = stamp;
        s->local_objects[local] = (uint32_t)object;
    }
    else if (fits)
    {
        same = s->object_stamps[object] == stamp && s->local_stamps[local] == stamp && s->object_locals[object] == local && s->local_objects[local] == object;
    }
    return same;
}

// Whether record `r`'s value, conversions and member offset are the stream's for its node.
static bool node_shadow_same_value(const NodeMachine* machine, uint32_t r)
{
    const CheckStream* stream = machine->program->stream;
    const uint32_t node = node_machine_node(machine, r);
    const CheckValue expected = check_stream_value(stream, node);
    CheckValue value;
    node_machine_record_value(machine, r, &value);
    bool same = expected.type == value.type && expected.category == value.category && expected.flags == value.flags &&
                expected.constant_kind == value.constant_kind && expected.object == value.object && expected.offset == value.offset &&
                expected.fault == value.fault && expected.overload_head == value.overload_head && expected.bit_width == value.bit_width;
    same = same && (expected.constant_kind == CHECK_CONSTANT_NONE || expected.integer == value.integer);
    same = same && (expected.constant_kind != CHECK_CONSTANT_FLOATING || memcmp(expected.floating_storage, value.floating_storage, 10) == 0);
    for (uint32_t k = 0; k < 2; k += 1)
    {
        same = same && stream->conversions[node].operands[k].type == node_machine_record_converted(machine, r, k) &&
               stream->conversions[node].operands[k].flags == node_machine_record_conversion_flags(machine, r, k);
    }
    same = same && stream->member_offsets[node] == node_machine_record_member_offset(machine, r);
    // A call's arguments' conversions, the stream's in its argument list.
    const CheckGraph* graph = &machine->program->input->graph;
    const bool call = node_machine_record_op(machine, r) == CHECK_OP_CALL;
    const uint32_t count = call ? node_machine_record_argument_count(machine, r) : 0;
    same = same && (!call || count == graph->argument_counts[node]);
    for (uint32_t i = 0; same && i < count; i += 1)
    {
        const CheckConversion* expected_conversion = stream->argument_conversions + graph->argument_offsets[node] + i;
        same = expected_conversion->type == node_machine_record_argument_converted(machine, r, i) &&
               expected_conversion->flags == node_machine_record_argument_conversion_flags(machine, r, i);
    }
    return same;
}

// Whether the machine's call rows are the function's from row `base` on (Tier0Function's calls table): each one's callee's
// symbol, its arguments' count, classes and sizes, whether it is variadic, and no result object.
static bool node_shadow_same_calls(const NodeMachine* machine, const Tier0Function* function, uint32_t base)
{
    bool same = base <= function->call_count && machine->call_count <= function->call_count - base;
    for (uint32_t k = 0; same && k < machine->call_count; k += 1)
    {
        const uint32_t r = machine->call_records[k];
        const uint32_t row = base + k;
        const uint32_t count = node_machine_record_argument_count(machine, r);
        const uint32_t at = function->call_first[row];
        same = function->call_targets[row] == node_machine_call_target(machine, r) && function->call_argument_counts[row] == count &&
               (function->call_variadic[row] != 0) == node_machine_call_variadic(machine, r) &&
               (function->call_result_objects == NULL || function->call_result_objects[row] == 0) && at <= function->argument_count && count <= function->argument_count - at;
        for (uint32_t i = 0; same && i < count; i += 1)
        {
            const TypeId argument = node_machine_record_argument_converted(machine, r, i);
            same = function->argument_classes[at + i] == node_machine_class(machine, argument) && (function->argument_sizes == NULL || function->argument_sizes[at + i] == 0);
        }
    }
    return same;
}

// Whether a branch's entry, a target in the stream (an entry's index, check_lower_resolve_labels'), is the tape's: its label
// `label`, counted from the expression's first (whose pop is `pop`), is the lowering's label of the same number counted
// from the label its pop logged (the numbering, relative to the expression), and both labels' entries are the stream's
// target (their places).
static bool node_shadow_same_label(const NodeCensusScratch* s, const NodeMachine* machine, const CheckLowerOwnerPop* pop, uint64_t label, uint64_t target)
{
    const uint64_t logged = (uint64_t)pop->label + label;
    return label < machine->label_count && logged < s->label_count && s->labels[logged] == target && pop->entry + (uint64_t)machine->label_entries[label] == target;
}

// Whether the tape is the lowering's entries for the expression whose root pop is `pop`: the entries its nodes' tasks and
// their conversions own, from the root's pop on; whether each of its labels is the lowering's of the same number,
// counted from the expression's first, at the same entry; and its calls. A call's row is relative on the tape: the
// stream's rows are the tape's plus one base, the row its first call has, and their contents the machine's
// (node_shadow_same_calls).
static bool node_shadow_same_entries(NodeCensusScratch* s, const NodeMachine* machine, const CheckLowerOwnerPop* pop, uint32_t* compared)
{
    const Tier0Function* function = s->function;
    const uint32_t first = machine->first;
    const uint32_t last = first + machine->count - 2;
    const uint32_t root = machine->root;
    uint32_t e = pop->entry;
    uint32_t t = 0;
    bool same = true;
    uint64_t base = UINT64_MAX;
    bool inside = e < function->count;
    s->temporary_count = 0;
    while (inside)
    {
        const uint32_t owner = s->owners[e];
        const uint32_t node = owner & ~BUSTER_CHECK_LOWER_OWNER_CONVERSION;
        inside = (owner & BUSTER_CHECK_LOWER_OWNER_CONTROL) == 0 && ((node >= first && node <= last && machine->count > 1) || node == root);
        if (inside)
        {
            const uint8_t kind = function->kinds[e];
            const bool branch = kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE;
            // A row of the tape's calls (so that the rows the tape names, distinct as the stream's are, are all its own).
            const bool call = kind == BUSTER_TYPED_CALL && t < machine->entry_count && machine->tape[t] < machine->call_count && function->values[e] >= machine->tape[t];
            base = call && base == UINT64_MAX ? function->values[e] - machine->tape[t] : base;
            same = same && t < machine->entry_count && machine->kinds[t] == kind && machine->classes[t] == function->classes[e] &&
                   (node_shadow_local_kind(kind) ? node_shadow_same_local(s, machine->tape[t], function->values[e]) :
                    branch                       ? node_shadow_same_label(s, machine, pop, machine->tape[t], function->values[e]) :
                    kind == BUSTER_TYPED_CALL    ? call && machine->tape[t] + base == function->values[e] :
                                                   machine->tape[t] == function->values[e]);
            t += 1;
            e += 1;
            inside = e < function->count;
        }
    }
    // Every label, a branch's or not, at the lowering's label's entry.
    for (uint32_t label = 0; same && label < machine->label_count; label += 1)
    {
        const uint64_t logged = (uint64_t)pop->label + label;
        same = logged < s->label_count && s->labels[logged] == pop->entry + machine->label_entries[label];
    }
    same = same && (machine->call_count == 0 || (base < UINT32_MAX && node_shadow_same_calls(machine, function, (uint32_t)base)));
    *compared = t;
    return same && t == machine->entry_count;
}

// The shadow of the census's taken expression whose root pop is `p` of the body's `pops`, of root kind `context`, whose
// walk reached `walked` nodes, `nodes` of them evaluated.
static void node_shadow_expression(NodeShadow* shadow, NodeCensusScratch* s, const CheckLowerOwnerPop* pops, uint32_t p, NodeCensusRoot context, uint32_t walked,
                                   uint32_t nodes)
{
    NodeMachine* machine = s->machine;
    const CheckProgram* program = s->program;
    const CheckLowerOwnerPop* pop = pops + p;
    const uint32_t root = pop->node;
    const uint32_t first = s->lowest_node;
    // Its records: one range of the graph's nodes, and its root, the next node or a later one.
    const uint32_t records = walked == 1 ? 1 : s->inner_node - first + 2;
    uint32_t object = 0;
    if (context == NODE_CENSUS_ROOT_INITIALIZER)
    {
        const uint32_t control = s->node_controls[root];
        const uint32_t declaration = control != BUSTER_CHECK_NONE ? program->controls[control].token : BUSTER_CHECK_NONE;
        object = declaration < program->input->token_count ? program->input->objects[declaration] : 0;
    }
    // The root's context, as its control's task gives it: an address, a discard, or neither.
    const uint8_t flags = pop->flags & (uint8_t)~BUSTER_CHECK_LOWER_OWNER_TRACKED;
    const uint8_t root_context = flags & (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS | BUSTER_CHECK_LOWER_TEMPLATE_DISCARD);
    NodeMachineRefusal refusal = walked != records ? NODE_MACHINE_REFUSED_SHAPE : (flags & ~root_context) != 0 ? NODE_MACHINE_REFUSED_ROOT : NODE_MACHINE_TAKEN;
    const uint64_t row_nodes = machine->row_nodes;
    const uint64_t action_nodes = machine->action_nodes;
    const uint64_t twins = machine->twin_differences;
    const uint64_t memo_fills = machine->memo_fills;
    const uint64_t memo_rules = machine->memo_rules;
    refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_run(machine, first, root, records, root_context, pop->target, object) : refusal;
    shadow->memo_fills += machine->memo_fills - memo_fills;
    shadow->memo_rules += machine->memo_rules - memo_rules;
    shadow->expressions += 1;
    shadow->nodes += nodes;
    shadow->records += walked;
    if (shadow->records_out != NULL && shadow->record_count < shadow->record_capacity)
    {
        shadow->records_out[shadow->record_count++] = (NodeShadowRecord){
            .line = node_census_line(s->pp, program->parser, program->input->graph.tokens[root]),
            .refusal = refusal,
        };
    }
    shadow->refused_expressions[refusal] += 1;
    shadow->refused_nodes[refusal] += nodes;
    const CheckOp stopped = machine->stopped < machine->count ? program->input->graph.ops[node_machine_node(machine, machine->stopped)] : CHECK_OP_COUNT;
    shadow->refused_codes[refusal == NODE_MACHINE_REFUSED_CODE && stopped < CHECK_OP_COUNT ? stopped : 0] += refusal == NODE_MACHINE_REFUSED_CODE;
    shadow->refused_templates[refusal == NODE_MACHINE_REFUSED_TEMPLATE && stopped < CHECK_OP_COUNT ? stopped : 0] += refusal == NODE_MACHINE_REFUSED_TEMPLATE;
    if (refusal == NODE_MACHINE_TAKEN)
    {
        bool values = true;
        uint32_t different = UINT32_MAX;
        for (uint32_t r = 0; r < machine->count; r += 1)
        {
            const bool same = node_shadow_same_value(machine, r);
            different = !same && different == UINT32_MAX ? r : different;
            values = values && same;
        }
        uint32_t compared = 0;
        const bool entries = node_shadow_same_entries(s, machine, pop, &compared);
        shadow->taken_expressions += 1;
        shadow->taken_nodes += nodes;
        shadow->taken_records += machine->count;
        shadow->entries += machine->entry_count;
        shadow->calls += machine->call_count;
        for (uint32_t c = 0; c < machine->call_count; c += 1)
        {
            shadow->arguments += node_machine_record_argument_count(machine, machine->call_records[c]);
        }
        shadow->value_differences += !values;
        shadow->entry_differences += !entries;
        shadow->labeled_expressions += machine->label_count != 0;
        shadow->labels += machine->label_count;
        shadow->row_nodes += machine->row_nodes - row_nodes;
        shadow->action_nodes += machine->action_nodes - action_nodes;
        shadow->twin_differences += machine->twin_differences - twins;
        if ((!values || !entries || machine->twin_differences != twins) && shadow->listed < node_census_list)
        {
            const uint32_t at = different != UINT32_MAX ? node_machine_node(machine, different) : root;
            shadow->listed += 1;
            printf("node-shadow: difference file=%s line=%u root=%s values=%s entries=%s (tape %u, stream %u) twin=%s node=%u op=%s\n", s->file,
                   node_census_line(s->pp, program->parser, program->input->graph.tokens[at]), node_census_root_names[context], values ? "same" : "DIFFERENT",
                   entries ? "same" : "DIFFERENT", machine->entry_count, compared, machine->twin_differences != twins ? "DIFFERENT" : "same", at,
                   lower_inventory_op_names[program->input->graph.ops[at]] != NULL ? lower_inventory_op_names[program->input->graph.ops[at]] : "?");
        }
        if (shadow->timed_count < shadow->timed_capacity)
        {
            shadow->file_nodes += nodes;
            shadow->timed[shadow->timed_count++] = (NodeShadowExpression){
                .first = first,
                .root = root,
                .count = records,
                .object = object,
                .target = pop->target,
                .context = root_context,
            };
        }
    }
}

// A cycle or instruction counter of this thread's user code (`config`, PERF_COUNT_HW_*), or -1.
static int node_shadow_counter(uint64_t config)
{
    struct perf_event_attr attribute;
    memset(&attribute, 0, sizeof(attribute));
    attribute.type = PERF_TYPE_HARDWARE;
    attribute.size = sizeof(attribute);
    attribute.config = config;
    attribute.exclude_kernel = 1;
    attribute.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, 0);
}

static uint64_t node_shadow_read(int counter)
{
    uint64_t value = 0;
    if (counter >= 0 && read(counter, &value, sizeof(value)) != (ssize_t)sizeof(value))
    {
        value = 0;
    }
    return value;
}

// Each of the file's taken expressions through the sweeps of set `sweeps` (0: T, 1: T and C, 2: all three).
static void node_shadow_sweep(NodeMachine* machine, const NodeShadow* shadow, uint32_t sweeps)
{
    for (uint32_t i = 0; i < shadow->timed_count; i += 1)
    {
        const NodeShadowExpression* expression = shadow->timed + i;
        // --repeat: each expression's sweeps again at once, its inputs in the caches as the reader leaves them in step 2.
        for (uint32_t time = 0; time < shadow->repeat; time += 1)
        {
            NodeMachineRefusal refusal = node_machine_type(machine, expression->first, expression->root, expression->count);
            if (sweeps >= 1 && refusal == NODE_MACHINE_TAKEN)
            {
                refusal = node_machine_contexts(machine, expression->context, expression->target, expression->object);
            }
            if (sweeps >= 2 && refusal == NODE_MACHINE_TAKEN)
            {
                node_machine_expand(machine, expression->object);
            }
        }
    }
}

// The timing passes over the file's taken expressions (`nodes` evaluated nodes in them): each sweep set `passes` times,
// interleaved, its least cycles, instructions and ticks added to the totals. Each sweep set starts without the unit's call
// answers (node_machine_forget), so that it pays their fills as one compile does; the machine's counts are the comparison's.
static void node_shadow_time(NodeShadow* shadow, NodeMachine* machine, uint64_t nodes)
{
    const int cycles = node_shadow_counter(PERF_COUNT_HW_CPU_CYCLES);
    const int instructions = node_shadow_counter(PERF_COUNT_HW_INSTRUCTIONS);
    const bool verify = machine->verify;
    const uint64_t memo_fills = machine->memo_fills;
    const uint64_t memo_rules = machine->memo_rules;
    machine->verify = false;
    uint64_t least[BUSTER_NODE_SHADOW_SWEEPS][3];
    memset(least, 0xff, sizeof(least));
    for (uint32_t pass = 0; pass < shadow->passes; pass += 1)
    {
        for (uint32_t sweeps = 0; sweeps < BUSTER_NODE_SHADOW_SWEEPS; sweeps += 1)
        {
            node_machine_forget(machine);
            const uint64_t c0 = node_shadow_read(cycles);
            const uint64_t i0 = node_shadow_read(instructions);
            const uint64_t t0 = __rdtsc();
            node_shadow_sweep(machine, shadow, sweeps);
            const uint64_t t1 = __rdtsc();
            const uint64_t i1 = node_shadow_read(instructions);
            const uint64_t c1 = node_shadow_read(cycles);
            const uint64_t measured[3] = { c1 - c0, i1 - i0, t1 - t0 };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                least[sweeps][k] = measured[k] < least[sweeps][k] ? measured[k] : least[sweeps][k];
            }
        }
    }
    for (uint32_t sweeps = 0; shadow->passes != 0 && sweeps < BUSTER_NODE_SHADOW_SWEEPS; sweeps += 1)
    {
        shadow->cycles[sweeps] += least[sweeps][0];
        shadow->instructions[sweeps] += least[sweeps][1];
        shadow->ticks[sweeps] += least[sweeps][2];
    }
    shadow->timed_nodes += shadow->passes != 0 ? nodes : 0;
    shadow->counted = cycles >= 0;
    machine->verify = verify;
    machine->memo_fills = memo_fills;
    machine->memo_rules = memo_rules;
    if (cycles >= 0)
    {
        close(cycles);
    }
    if (instructions >= 0)
    {
        close(instructions);
    }
}

static void node_shadow_report(const NodeShadow* shadow, const uint64_t* action_ops)
{
    printf("node-shadow: census-taken expressions=%" PRIu64 " evaluated-nodes=%" PRIu64 " records=%" PRIu64 "\n", shadow->expressions, shadow->nodes, shadow->records);
    printf("node-shadow: machine-taken expressions=%" PRIu64 " (%.2f%%) evaluated-nodes=%" PRIu64 " (%.2f%%) records=%" PRIu64 " entries=%" PRIu64 " calls=%" PRIu64 "\n",
           shadow->taken_expressions, node_census_share(shadow->taken_expressions, shadow->expressions), shadow->taken_nodes,
           node_census_share(shadow->taken_nodes, shadow->nodes), shadow->taken_records, shadow->entries, shadow->calls);
    printf("node-shadow: differences values=%" PRIu64 " entries=%" PRIu64 " twin=%" PRIu64 " (row-typed nodes %" PRIu64 ", action-typed %" PRIu64 "%s)\n",
           shadow->value_differences, shadow->entry_differences, shadow->twin_differences, shadow->row_nodes, shadow->action_nodes,
           shadow->verify ? ", verified" : "");
    printf("node-shadow: labels=%" PRIu64 " in taken expressions=%" PRIu64 "\n", shadow->labels, shadow->labeled_expressions);
    printf("node-shadow: argument-memo filled=%" PRIu64 " by-rule=%" PRIu64 " (taken calls' arguments %" PRIu64 ")\n", shadow->memo_fills, shadow->memo_rules,
           shadow->arguments);
    for (uint32_t r = 1; r < NODE_MACHINE_REFUSALS; r += 1)
    {
        printf("node-shadow: refused=%s expressions=%" PRIu64 " evaluated-nodes=%" PRIu64 " (%.2f%%)\n", node_machine_refusal_names[r], shadow->refused_expressions[r],
               shadow->refused_nodes[r], node_census_share(shadow->refused_nodes[r], shadow->nodes));
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        if (shadow->refused_codes[op] != 0 || shadow->refused_templates[op] != 0)
        {
            printf("node-shadow: refused-at op=%s code=%" PRIu64 " template=%" PRIu64 "\n", lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?",
                   shadow->refused_codes[op], shadow->refused_templates[op]);
        }
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        if (action_ops[op] != 0)
        {
            printf("node-shadow: action-typed op=%s nodes=%" PRIu64 "\n", lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", action_ops[op]);
        }
    }
    static const char* const sweep_names[BUSTER_NODE_SHADOW_SWEEPS] = { "T", "T+C", "T+C+X" };
    for (uint32_t k = 0; shadow->timed_nodes != 0 && k < BUSTER_NODE_SHADOW_SWEEPS; k += 1)
    {
        printf("node-shadow: time sweeps=%s passes=%u repeat=%u cycles=%" PRIu64 " instructions=%" PRIu64 " ticks=%" PRIu64 " per-evaluated-node: cycles=%.1f instructions=%.1f "
               "ticks=%.1f%s\n",
               sweep_names[k], shadow->passes, shadow->repeat, shadow->cycles[k], shadow->instructions[k], shadow->ticks[k], (double)shadow->cycles[k] / (double)shadow->timed_nodes,
               (double)shadow->instructions[k] / (double)shadow->timed_nodes, (double)shadow->ticks[k] / (double)shadow->timed_nodes,
               shadow->counted ? "" : " (no cycle counter)");
    }
}

// Body `b`'s pops (`pops`, `count` of them), counted into `census`.
static void node_census_body(NodeCensus* census, NodeCensusScratch* s, uint32_t b, const CheckLowerOwnerPop* pops, uint32_t count)
{
    const CheckProgram* program = s->program;
    const CheckProgramBody* body = program->bodies + b;
    const uint32_t none = BUSTER_CHECK_NONE;
    node_census_mark_controls(s, body, true);
    // Each pop's root pop and each first pop's times; a pop again of a node is its first's.
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint32_t node = pops[p].node;
        s->times[p] = 0;
        s->expression_nodes[p] = 0;
        s->expression_tests[p] = 0;
        if (s->node_pop[node] == none)
        {
            s->node_pop[node] = p;
        }
        s->times[s->node_pop[node]] += 1;
        const uint32_t pusher = pops[p].pusher;
        const uint32_t parent = (pusher & BUSTER_CHECK_LOWER_OWNER_CONTROL) == 0 ? s->node_pop[pusher & ~BUSTER_CHECK_LOWER_OWNER_CONVERSION] : none;
        s->root_pop[p] = parent != none && parent < p ? s->root_pop[parent] : p;
    }
    const bool region = node_census_region(program, body);
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint32_t root = s->root_pop[p];
        const bool first = s->node_pop[pops[p].node] == p;
        s->expression_nodes[root] += first;
        if (first && s->times[p] > 1)
        {
            s->expression_tests[root] |= UINT64_C(1) << NODE_CENSUS_POPPED_AGAIN;
        }
    }
    bool whole = true;
    uint64_t body_nodes = 0;
    for (uint32_t p = 0; p < count; p += 1)
    {
        if (s->root_pop[p] == p)
        {
            const NodeCensusRoot context = node_census_root(s, pops + p);
            uint32_t walked = 0;
            uint64_t tests = s->expression_tests[p] | node_census_expression(s, pops[p].node, pops[p].flags, context, &walked);
            tests |= region ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
            census->largest = walked > census->largest ? walked : census->largest;
            node_census_count(census, s, tests, s->expression_nodes[p], context, pops[p].node);
            if (census->shadow != NULL && tests == 0)
            {
                node_shadow_expression(census->shadow, s, pops, p, context, walked, s->expression_nodes[p]);
            }
            whole = whole && tests == 0;
            body_nodes += s->expression_nodes[p];
        }
    }
    for (uint32_t p = 0; p < count; p += 1)
    {
        s->node_pop[pops[p].node] = none;
    }
    node_census_mark_controls(s, body, false);
    census->bodies += 1;
    census->bodies_taken += whole;
    census->bodies_taken_nodes += whole ? body_nodes : 0;
}

// File `file`, read with `options`, prepared and lowered as -c lowers it, counted into `census`: whether its options could
// be read.
static bool node_census_file(NodeCensus* census, const char* file, char** options, uint32_t option_count)
{
    static LowerSource source;
    const bool valid = lower_source_open(&source, file, options, option_count);
    const bool prepared = source.prepared;
    CheckCorpusPath* path = &source.path;
    CheckProgramSelection selected = source.selected;
    const uint64_t log_capacity = prepared ? 4 * (uint64_t)selected.program.stream->count + 65536 : 0;
    // Each body's labels: its controls' and the ones its tasks number (the shadow's branches are read there).
    const uint64_t label_capacity = prepared ? 2 * (uint64_t)selected.program.control_count + selected.program.body_count + 4 * (uint64_t)selected.program.stream->count + 65536 : 0;
    Arena log_arena = arena_reserve(log_capacity * sizeof(CheckLowerOwnerPop) + label_capacity * sizeof(uint32_t) + 8192);
    CheckLowerOwners owners = { .capacity = log_capacity, .label_capacity = label_capacity };
    owners.pops = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, CheckLowerOwnerPop, owners.capacity) : 0;
    owners.labels = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, uint32_t, owners.label_capacity) : 0;
    CheckProgramLowered lowered = { 0 };
    const bool lowering = prepared && owners.pops != 0 && owners.labels != 0 &&
                          check_program_lower_owners(&lowered, &selected.program, &owners, &path->output_arena, &path->scratch);
    const bool complete = lowering && !owners.overflow;
    if (complete)
    {
        const uint32_t nodes = selected.program.stream->count;
        Arena scratch = arena_reserve(UINT64_C(1) << 34);
        uint32_t widest = 0;
        uint64_t first = 0;
        for (uint64_t i = 0; i <= owners.count; i += 1)
        {
            if (i == owners.count || owners.pops[i].body != owners.pops[first].body)
            {
                widest = (uint32_t)(i - first) > widest ? (uint32_t)(i - first) : widest;
                first = i;
            }
        }
        NodeCensusScratch s = { .program = &selected.program, .pp = &source.pp, .file = file };
        s.policy = node_census_policy(&source.pp, selected.program.input);
        node_census_assign_enum = s.policy.assign_enum;
        uint32_t** node_columns[] = { &s.marks, &s.parents, &s.depths, &s.node_controls, &s.list, &s.stack, &s.node_pop };
        bool room = node_census_events(&s, path, &scratch);
        for (uint32_t c = 0; room && c < BUSTER_ARRAY_LENGTH(node_columns); c += 1)
        {
            *node_columns[c] = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)nodes + 1);
            room = *node_columns[c] != 0;
        }
        uint32_t** pop_columns[] = { &s.root_pop, &s.times, &s.expression_nodes };
        for (uint32_t c = 0; room && c < BUSTER_ARRAY_LENGTH(pop_columns); c += 1)
        {
            *pop_columns[c] = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)widest + 1);
            room = *pop_columns[c] != 0;
        }
        s.expression_tests = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, (uint64_t)widest + 1) : 0;
        room = room && s.expression_tests != 0;
        NodeShadow* shadow = census->shadow;
        NodeMachine machine;
        if (room && shadow != NULL)
        {
            uint32_t locals = 1;
            for (uint32_t b = 0; b < selected.program.body_count; b += 1)
            {
                locals = lowered.entry_tokens[b] != 0 && lowered.functions[b].local_count > locals ? lowered.functions[b].local_count : locals;
            }
            const uint32_t objects = selected.program.object_count + 1;
            s.object_locals = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, objects);
            s.object_stamps = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, objects);
            s.local_objects = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, locals);
            s.local_stamps = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, locals);
            s.temporary_locals = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, BUSTER_NODE_MACHINE_MOST);
            shadow->timed_capacity = (uint32_t)(owners.count < UINT32_MAX ? owners.count : UINT32_MAX);
            shadow->timed = BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeShadowExpression, (uint64_t)shadow->timed_capacity + 1);
            shadow->timed_count = 0;
            room = s.object_locals != NULL && s.object_stamps != NULL && s.local_objects != NULL && s.local_stamps != NULL && s.temporary_locals != NULL &&
                   shadow->timed != NULL &&
                   node_machine_begin(&machine, &selected.program, shadow->scalar, shadow->verify, &scratch);
            if (room)
            {
                memset(s.object_stamps, 0, (uint64_t)objects * sizeof(uint32_t));
                memset(s.local_stamps, 0, (uint64_t)locals * sizeof(uint32_t));
                s.machine = &machine;
            }
        }
        if (room)
        {
            memset(s.marks, 0, ((uint64_t)nodes + 1) * sizeof(uint32_t));
            memset(s.node_controls, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
            memset(s.node_pop, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
            first = 0;
            for (uint32_t b = 0; b < selected.program.body_count; b += 1)
            {
                uint64_t end = first;
                while (end < owners.count && owners.pops[end].body == b)
                {
                    end += 1;
                }
                if (lowered.entry_tokens[b] != 0)
                {
                    s.function = lowered.functions + b;
                    s.owners = lowered.entry_tokens[b];
                    s.labels = owners.labels;
                    s.label_count = owners.label_count;
                    s.body_stamp = b + 1;
                    node_census_body(census, &s, b, owners.pops + first, (uint32_t)(end - first));
                }
                first = end;
            }
            if (shadow != NULL)
            {
                for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
                {
                    shadow->action_ops[op] += machine.action_ops[op];
                }
                node_shadow_time(shadow, &machine, shadow->file_nodes);
                shadow->file_nodes = 0;
            }
            census->files += 1;
        }
        else
        {
            census->failures += 1;
            printf("node-census: file=%s failed=\"no memory for the census's columns\"\n", file);
        }
        arena_release(&scratch);
    }
    else if (valid)
    {
        census->failures += 1;
        printf("node-census: file=%s failed=\"%s\"\n", file, !prepared ? lower_source_failure(&source) : owners.overflow ? "the log of pops filled" : "it couldn't be lowered");
    }
    arena_release(&log_arena);
    lower_source_close(&source);
    return valid;
}

static void node_census_report(const NodeCensus* census)
{
    const uint64_t nodes = census->nodes;
    printf("node-census: files=%" PRIu64 " bodies=%" PRIu64 " expressions=%" PRIu64 " evaluated-nodes=%" PRIu64 " nodes-per-expression=%.2f largest=%" PRIu64
           " failures=%" PRIu64 "\n",
           census->files, census->bodies, census->expressions, nodes, census->expressions != 0 ? (double)nodes / (double)census->expressions : 0.0,
           census->largest, census->failures);
    const uint64_t silence_nodes = census->set_nodes - census->taken_nodes;
    const uint64_t silence_expressions = census->set_expressions - census->taken_expressions;
    printf("node-census: set expressions=%" PRIu64 " (%.2f%%) nodes=%" PRIu64 " (%.2f%%)\n", census->set_expressions,
           node_census_share(census->set_expressions, census->expressions), census->set_nodes, node_census_share(census->set_nodes, nodes));
    printf("node-census: silence refuses of the set's expressions=%" PRIu64 " (%.2f%%) nodes=%" PRIu64 " (%.2f%% of all nodes)\n", silence_expressions,
           node_census_share(silence_expressions, census->expressions), silence_nodes, node_census_share(silence_nodes, nodes));
    printf("node-census: taken expressions=%" PRIu64 " (%.2f%%) nodes=%" PRIu64 " (%.2f%%)\n", census->taken_expressions,
           node_census_share(census->taken_expressions, census->expressions), census->taken_nodes, node_census_share(census->taken_nodes, nodes));
    printf("node-census: bodies taken whole=%" PRIu64 " (%.2f%%) their nodes=%" PRIu64 " (%.2f%%)\n", census->bodies_taken,
           node_census_share(census->bodies_taken, census->bodies), census->bodies_taken_nodes, node_census_share(census->bodies_taken_nodes, nodes));
    for (uint32_t r = 0; r < NODE_CENSUS_ROOTS; r += 1)
    {
        printf("node-census: root=%s expressions=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%) taken-nodes=%" PRIu64 " (%.2f%% of its)\n", node_census_root_names[r],
               census->root_expressions[r], census->root_nodes[r], node_census_share(census->root_nodes[r], nodes), census->root_taken_nodes[r],
               node_census_share(census->root_taken_nodes[r], census->root_nodes[r]));
    }
    for (uint32_t t = 0; t < NODE_CENSUS_TESTS; t += 1)
    {
        printf("node-census: test=%s %s any: expressions=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%) only: expressions=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%) first: "
               "expressions=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%)\n",
               node_census_test_names[t], t < NODE_CENSUS_FIRST_SILENCE ? "set" : "silence", census->any_expressions[t], census->any_nodes[t],
               node_census_share(census->any_nodes[t], nodes), census->only_expressions[t], census->only_nodes[t], node_census_share(census->only_nodes[t], nodes),
               census->first_expressions[t], census->first_nodes[t], node_census_share(census->first_nodes[t], nodes));
    }
    // The design's system-macro test (docs/node-machine.md, "Silence") isn't one of the census's: in a system header's
    // macro the pipeline only suppresses diagnostics, so a taken expression is as silent there as its tests make it.
    printf("node-census: system-macro (not a refusal) expressions=%" PRIu64 " nodes=%" PRIu64 " (%.2f%%) of them taken=%" PRIu64 " (%.2f%%)\n",
           census->system_expressions, census->system_nodes, node_census_share(census->system_nodes, nodes), census->system_taken_nodes,
           node_census_share(census->system_taken_nodes, nodes));
    for (uint32_t id = 0; id <= CHECK_WARNING_ID_COUNT; id += 1)
    {
        if (census->event_ids[id] != 0)
        {
            const char* option = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[id].option : NULL;
            printf("node-census: event id=%u option=%s expressions=%" PRIu64 "\n", id, option != NULL ? option : "-", census->event_ids[id]);
        }
    }
}

static int run_node_census(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    bool shadowed = false;
    NodeShadow shadow = { .passes = 5, .repeat = 1 };
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--list") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number <= UINT32_MAX;
            node_census_list = (uint32_t)number;
        }
        else if (strcmp(arguments[i], "--shadow") == 0)
        {
            shadowed = true;
        }
        else if (strcmp(arguments[i], "--scalar") == 0)
        {
            shadow.scalar = true;
        }
        else if (strcmp(arguments[i], "--verify") == 0)
        {
            shadow.verify = true;
        }
        else if (strcmp(arguments[i], "--passes") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number <= 1000;
            shadow.passes = (uint32_t)number;
        }
        else if (strcmp(arguments[i], "--repeat") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number >= 1 && number <= 16;
            shadow.repeat = (uint32_t)number;
        }
        else if (strcmp(arguments[i], "--no-default-config") == 0)
        {
            // Clang's option, which the corpora's command lines take: nothing for pp_parse_options to read.
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else
        {
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0 && (shadowed || (!shadow.scalar && !shadow.verify));
    NodeCensus census = { .shadow = shadowed ? &shadow : NULL };
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = node_census_file(&census, files[i], options, option_count);
        fflush(stdout);
    }
    if (valid)
    {
        node_census_report(&census);
        if (shadowed)
        {
            node_shadow_report(&shadow, shadow.action_ops);
        }
    }
    else
    {
        fprintf(stderr, "usage: compiler measure-node-machine [--list N] [--shadow [--scalar] [--verify] [--passes N] [--repeat N]] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    const bool differences = shadowed && (shadow.value_differences != 0 || shadow.entry_differences != 0 || shadow.twin_differences != 0);
    return valid && census.failures == 0 && census.files != 0 && !differences ? 0 : 1;
}

#endif
