// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's step 0, the census (docs/node-machine.md, "The steps and their gates"): what the node machine's set and its
// silence tests would take, counted on the pipeline as it is. No machine runs: each file is prepared and lowered as -c
// does, with the tasks' log of pops (#1048's, c_check_lower_inventory.c's lower_inventory_file), and every full
// expression the tasks lowered is read again from the finished graph, stream and parser's tree, by the tests the machine
// would make before it takes it. Include after c_check_lower_inventory.c.
//
//   build/compiler measure-node-machine [--list N] [--shadow [--scalar] [--verify] [--fused] [--interleave N] [--facts | --facts-lean | --facts-loads]
//                                       [--facts-operators] [--facts-reads] [--facts-packed] [--facts-window N] [--front | --front-first] [--front-lean]
//                                       [--front-warm] [--front-types] [--front-sweeps] [--front-tests | --front-own-tests] [--front-tests-last] [--passes N]
//                                       [--repeat N]] [preprocessor options] file...
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
// The tests read what the graph run computes of a node through a view (node_view_value and its kin): the stream's here,
// and with --shadow --front-tests T's records at reader time, where the same tests run before the pipeline
// (node_census_reader_expression; docs/node-machine.md, "The tests where they sit") and the two verdicts are compared.
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
    [CHECK_WARNING_ID_CXX_ZERO_SIZE_RECORD] = NODE_CENSUS_WARNING_OUTSIDE,
    [CHECK_WARNING_ID_ZERO_LENGTH_ARRAY] = NODE_CENSUS_WARNING_OUTSIDE,
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

// One file's state: the program and the unit's, and per node and pop the walks' columns, valid for the current
// expression's stamp alone.
typedef struct NodeCensusScratch NodeCensusScratch;
struct NodeCensusScratch
{
    const CheckProgram* program;
    Preprocessor* pp;
    const char* file;
    NodeCensusPolicy policy;
    // The tests' view of a node's value (node_view_value and its kin): the stream's after the pipeline, and at reader time
    // ("The tests where they sit"), where `records` is the machine there, T's records of its current expression. `missed`
    // counts the nodes of an expression's walk that the records don't hold.
    const NodeMachine* records;
    uint32_t missed;
    // At reader time, whether the unit has a packed record or member at all: the packed members' witnesses
    // (CheckInput.packed_members) are a later pass's, so any address taken in such a unit stands for one.
    bool packed_unit;
    // What the tests ask of a type, made once a type and kept by its index (node_census_type_bits), `type_bits_count` of
    // them; and of a name's visible declaration, kept by its token (node_census_declaration_facts), an entry a token.
    // Both start cleared: a type past the first's room is asked each time.
    uint8_t* type_bits;
    uint32_t type_bits_count;
    uint8_t* declaration_facts;
    // The tests in the machine's own form (node_tests_expression): a type's widths as one word, kept as its byte is
    // (node_tests_ranges), and each record's type's byte for the operators above it, BUSTER_NODE_MACHINE_MOST of them.
    uint32_t* type_ranges;
    uint8_t* own_types;
    // What each record is as an operand (BUSTER_NODE_TESTS_OPERAND_*), with a zero byte before the first for an operand
    // that is none; the pass's three lists of records, BUSTER_NODE_MACHINE_MOST each; and the unit's tables by operator
    // (node_tests_tables).
    uint8_t* own_properties;
    uint16_t* own_lists;
    uint8_t own_classes[128];
    uint8_t own_operand_bits[128];
    uint8_t own_copies[128];
    uint8_t own_always[128];
    uint8_t own_sends[128];
    // The declared type of the object whose initializer the current expression is, where the caller gives it (0
    // otherwise): at reader time, and after the pipeline for a root the lowering never popped, which no control names.
    TypeId given_declared;
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
    // --facts: the file's fact words and windows (c_check_node_machine_facts.c), or none.
    NodeFacts* facts;
    // --facts-reads: the file's census of what reads T's records (c_check_node_machine_reads.c), or none.
    NodeReads* reads;
    const Tier0Function* function;
    const uint32_t* owners;
    // The log's labels' entries (CheckLowerOwners.labels), which a pop's `label` indexes.
    const uint32_t* labels;
    uint64_t label_count;
    // The body's named result's local and its hidden pointer's (CheckLowerOwners.named), or BUSTER_CHECK_NONE twice.
    uint32_t named_local;
    uint32_t hidden_local;
    uint32_t* object_locals;
    uint32_t* object_stamps;
    uint32_t* local_objects;
    uint32_t* local_stamps;
    uint32_t body_stamp;
    // The current expression's temporaries' locals, by their number on the tape, and how many it has met.
    uint32_t* temporary_locals;
    uint32_t temporary_count;
    // The current expression's records' consumers, for the fused sweep's census (node_shadow_fusion).
    uint32_t* consumers;
    // --fused: the three sweeps' tape, labels' entries and calls' records, kept for the comparison with the one sweep's
    // (node_shadow_fused).
    uint8_t* fused_kinds;
    uint8_t* fused_classes;
    uint64_t* fused_tape;
    uint32_t* fused_labels;
    uint32_t* fused_calls;
};

static double node_census_share(uint64_t part, uint64_t whole)
{
    return whole != 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

// The warnings the silence tests stand for, enabled anywhere in the unit: by the command's options and the unit's
// diagnostic pragmas, which the reader has whole before its coded walk.
static NodeCensusPolicy node_census_policy(const Preprocessor* pp, const CheckInput* input)
{
    NodeCensusPolicy policy;
    // Every byte, its padding too: the shadow compares reader time's with the pipeline's as bytes.
    memset(&policy, 0, sizeof(policy));
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

// The tests' view: what they read of graph node `node` that the graph run computes, from the stream after the pipeline
// and, at reader time, from T's record of the node (NodeCensusScratch.records): its value, its operands' conversions, a
// member's offset. Reader time has T's records of the machine's current expression alone, which hold every node its
// root's subtree reaches (T refuses the shape otherwise): a node outside them reads as none.
static inline uint32_t node_view_record(const NodeCensusScratch* s, uint32_t node)
{
    const NodeMachine* machine = s->records;
    const uint32_t record = node - machine->first;
    return node == machine->root ? machine->count - 1 : node >= machine->first && record + 1 < machine->count ? record : UINT32_MAX;
}

// Whether the view has node `node`'s value.
static inline bool node_view_has(const NodeCensusScratch* s, uint32_t node)
{
    return s->records == NULL ? node < s->program->stream->count : node_view_record(s, node) != UINT32_MAX;
}

static inline CheckValue node_view_value(const NodeCensusScratch* s, uint32_t node)
{
    CheckValue value;
    if (s->records == NULL)
    {
        value = check_stream_value(s->program->stream, node);
    }
    else
    {
        const uint32_t record = node_view_record(s, node);
        memset(&value, 0, sizeof(value));
        if (record != UINT32_MAX)
        {
            node_machine_record_value(s->records, record, &value);
        }
    }
    return value;
}

static inline TypeId node_view_type(const NodeCensusScratch* s, uint32_t node)
{
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? check_stream_type(s->program->stream, node) : record != UINT32_MAX ? node_machine_record_type(s->records, record) : 0;
}

static inline uint32_t node_view_flags(const NodeCensusScratch* s, uint32_t node)
{
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? check_stream_flags(s->program->stream, node) : record != UINT32_MAX ? node_machine_record_flags(s->records, record) : 0;
}

static inline CheckConstantKind node_view_constant_kind(const NodeCensusScratch* s, uint32_t node)
{
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? check_stream_constant_kind(s->program->stream, node) :
           record != UINT32_MAX ? node_machine_record_constant_kind(s->records, record) : CHECK_CONSTANT_NONE;
}

static inline uint32_t node_view_object(const NodeCensusScratch* s, uint32_t node)
{
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? check_stream_object(s->program->stream, node) : record != UINT32_MAX ? node_machine_record_object(s->records, record) : 0;
}

// Whether a leaf's value is an overload set's.
static inline bool node_view_overloaded(const NodeCensusScratch* s, uint32_t node)
{
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? check_stream_value(s->program->stream, node).overload_head != 0 : record != UINT32_MAX && node_machine_record_overloaded(s->records, record);
}

// A member's offset in its record, as the action gave it.
static inline uint64_t node_view_member_offset(const NodeCensusScratch* s, uint32_t node)
{
    const CheckStream* stream = s->program->stream;
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? (stream->member_offsets != NULL ? stream->member_offsets[node] : 0) :
           record != UINT32_MAX ? node_machine_record_member_offset(s->records, record) : 0;
}

// The type node `node` converts its operand `k` (0 to 2) to, 0 for none; and a call's argument `i`'s.
static inline TypeId node_view_converted(const NodeCensusScratch* s, uint32_t node, uint32_t k)
{
    const CheckStream* stream = s->program->stream;
    const uint32_t record = s->records != NULL ? node_view_record(s, node) : UINT32_MAX;
    return s->records == NULL ? (stream->conversions != NULL ? stream->conversions[node].operands[k].type : 0) :
           record == UINT32_MAX ? 0 : k < 2 ? node_machine_record_converted(s->records, record, k) : node_machine_record_converted_third(s->records, record);
}

static inline TypeId node_view_argument_converted(const NodeCensusScratch* s, uint32_t call, uint32_t i)
{
    const CheckStream* stream = s->program->stream;
    const uint32_t record = s->records != NULL ? node_view_record(s, call) : UINT32_MAX;
    return s->records == NULL ? (stream->argument_conversions != NULL ? stream->argument_conversions[s->program->input->graph.argument_offsets[call] + i].type : 0) :
           record != UINT32_MAX && i < node_machine_record_argument_count(s->records, record) ? node_machine_record_argument_converted(s->records, record, i) : 0;
}

// The warnings the graph run kept at node `node`, and those of them its context ignores, into `ignored`. Reader time has
// none a node: T keeps its actions' warnings an expression (NodeMachine.action_warnings), which node_census_expression
// reads.
static inline uint32_t node_view_warnings(const NodeCensusScratch* s, uint32_t node, uint32_t* ignored)
{
    const CheckStream* stream = s->program->stream;
    *ignored = s->records == NULL && stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
    return s->records == NULL && stream->warnings != NULL ? stream->warnings[node] & 0x01ffffffu : 0;
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

// What the tests ask of a value's type at every node, as one byte a type: whether it is one the machine has no kind for
// (node_census_special_type), a struct or a union, an enumeration, a floating type, a pointer (or nullptr_t). Made once
// a type by the predicates above and kept by the type's index (they read a type past its qualifiers).
#define BUSTER_NODE_CENSUS_TYPE_KNOWN 0x01u
#define BUSTER_NODE_CENSUS_TYPE_SPECIAL 0x02u
#define BUSTER_NODE_CENSUS_TYPE_AGGREGATE 0x04u
#define BUSTER_NODE_CENSUS_TYPE_ENUM 0x08u
#define BUSTER_NODE_CENSUS_TYPE_FLOATING 0x10u
#define BUSTER_NODE_CENSUS_TYPE_POINTER 0x20u
// For the tests in the machine's own form: a pointer whose pointee may be void (a void dereference's operand; nullptr_t
// reads as one, for the census's code to decide), and an array.
#define BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE 0x40u
#define BUSTER_NODE_CENSUS_TYPE_ARRAY 0x80u

static __attribute__((noinline)) uint8_t node_census_type_bits_make(const TypeTable* types, TypeId type)
{
    const uint8_t kind = node_census_kind(types, type);
    const TypeId pointee = kind == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_strip_aligned(types, type_element(types, node_census_base(types, type)))) : 0;
    const bool void_pointee = kind == BUSTER_TYPE_KIND_NULLPTR || (kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID);
    const uint8_t bits = (uint8_t)(BUSTER_NODE_CENSUS_TYPE_KNOWN | (node_census_special_type(types, type) ? BUSTER_NODE_CENSUS_TYPE_SPECIAL : 0) |
                                   (node_census_aggregate(types, type) ? BUSTER_NODE_CENSUS_TYPE_AGGREGATE : 0) |
                                   (node_census_enum(types, type) ? BUSTER_NODE_CENSUS_TYPE_ENUM : 0) |
                                   (node_census_floating(types, type) ? BUSTER_NODE_CENSUS_TYPE_FLOATING : 0) |
                                   (node_census_pointer(types, type) ? BUSTER_NODE_CENSUS_TYPE_POINTER : 0) |
                                   (void_pointee ? BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE : 0) | (type_kind_is_array(kind) ? BUSTER_NODE_CENSUS_TYPE_ARRAY : 0));
    return bits;
}

static inline uint8_t node_census_type_bits(const NodeCensusScratch* s, TypeId type)
{
    const uint32_t index = type_index(type);
    const bool kept = type != 0 && s->type_bits != NULL && index < s->type_bits_count;
    uint8_t bits = kept ? s->type_bits[index] : 0;
    if (type != 0 && bits == 0)
    {
        bits = node_census_type_bits_make(s->program->types, type);
        if (kept)
        {
            s->type_bits[index] = bits;
        }
    }
    return bits;
}

// A node's value as an integer constant of at most 64 bits: whether it is one, and its value, exact, in `value`.
static bool node_census_integer_constant(const NodeCensusScratch* s, uint32_t node, __int128* value)
{
    const TypeTable* types = s->program->types;
    bool result = false;
    *value = 0;
    if (node_view_has(s, node) && node_view_constant_kind(s, node) == CHECK_CONSTANT_INTEGER)
    {
        const CheckValue v = node_view_value(s, node);
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

static bool node_census_builtin_result(const Preprocessor* pp, uint32_t name);

// What the tests ask of the visible declaration `declaration` of a name `name` (the identifier at a leaf's token), as one
// byte kept by the declaration's token: whether it has an attribute the tables don't model (node_census_flagged_kinds);
// whether a call of it has semantics of its own by its name or its attributes (a printf-family name,
// node_census_callee_kinds); and whether it asks for its result (node_census_result_kinds, or a const or pure builtin's
// name). Each is its declaration's alone: every leaf of one declaration has its name. Each is made when it is first
// asked (its KNOWN bit): a leaf asks the first, a call's callee the second, a discarded call the third.
#define BUSTER_NODE_CENSUS_DECLARATION_FLAGGED_KNOWN 0x01u
#define BUSTER_NODE_CENSUS_DECLARATION_FLAGGED 0x02u
#define BUSTER_NODE_CENSUS_DECLARATION_CALLEE 0x04u
#define BUSTER_NODE_CENSUS_DECLARATION_RESULT 0x08u
#define BUSTER_NODE_CENSUS_DECLARATION_CALLEE_KNOWN 0x10u
#define BUSTER_NODE_CENSUS_DECLARATION_RESULT_KNOWN 0x20u

static __attribute__((noinline)) uint8_t node_census_declaration_facts_make(const NodeCensusScratch* s, uint32_t declaration, uint32_t name, uint8_t asked)
{
    const CheckInput* input = s->program->input;
    uint8_t facts = asked;
    if (asked == BUSTER_NODE_CENSUS_DECLARATION_FLAGGED_KNOWN)
    {
        const bool flagged = node_census_attributed(input, declaration, node_census_flagged_kinds, BUSTER_ARRAY_LENGTH(node_census_flagged_kinds));
        facts |= flagged ? BUSTER_NODE_CENSUS_DECLARATION_FLAGGED : 0;
    }
    else if (asked == BUSTER_NODE_CENSUS_DECLARATION_CALLEE_KNOWN)
    {
        const String8 spelled = { .pointer = (char*)intern_bytes(s->pp->table, name), .length = intern_length(s->pp->table, name) };
        const bool callee = check_common_format_index(spelled, &s->pp->options) != UINT32_MAX ||
                            node_census_attributed(input, declaration, node_census_callee_kinds, BUSTER_ARRAY_LENGTH(node_census_callee_kinds));
        facts |= callee ? BUSTER_NODE_CENSUS_DECLARATION_CALLEE : 0;
    }
    else
    {
        const bool result = node_census_builtin_result(s->pp, name) ||
                            node_census_attributed(input, declaration, node_census_result_kinds, BUSTER_ARRAY_LENGTH(node_census_result_kinds));
        facts |= result ? BUSTER_NODE_CENSUS_DECLARATION_RESULT : 0;
    }
    return facts;
}

// The declaration's byte with the fact `asked` (one of its KNOWN bits) made.
static inline uint8_t node_census_declaration_facts(const NodeCensusScratch* s, uint32_t declaration, uint32_t name, uint8_t asked)
{
    const bool kept = s->declaration_facts != NULL && declaration < s->program->parser->tokens.count;
    uint8_t facts = kept ? s->declaration_facts[declaration] : 0;
    if ((facts & asked) == 0)
    {
        facts |= node_census_declaration_facts_make(s, declaration, name, asked);
        if (kept)
        {
            s->declaration_facts[declaration] = facts;
        }
    }
    return facts;
}

// Whether a call's callee has semantics of its own (the set's): a library builtin's fold or computation
// (check_program_call_library), a printf-family name, an attribute of node_census_callee_kinds, or a function type
// without a prototype.
static bool node_census_callee(const NodeCensusScratch* s, uint32_t call)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    const Parser* parser = program->parser;
    const uint32_t member = graph->members != NULL ? graph->members[call] : 0;
    const uint32_t callee = graph->operand0[call];
    // At reader time T has asked the same of each call (node_machine_reader_call_library) and refused the expression.
    bool result = ((member & BUSTER_CHECK_LIBRARY_CALL) != 0 && s->records == NULL && check_program_call_library(program, call)) || callee >= graph->count;
    if (!result)
    {
        const TypeId function = node_census_function_type(types, node_view_type(s, callee));
        result = function == 0 || (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
    }
    if (!result && graph->ops[callee] == CHECK_OP_VALUE)
    {
        const uint32_t token = graph->tokens[callee];
        const uint32_t declaration = node_census_declaration(program, token);
        if (declaration != BUSTER_CHECK_NONE)
        {
            result = (node_census_declaration_facts(s, declaration, parser->tokens.names[token], BUSTER_NODE_CENSUS_DECLARATION_CALLEE_KNOWN) & BUSTER_NODE_CENSUS_DECLARATION_CALLEE) != 0;
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
            result = (node_census_declaration_facts(s, declaration, parser->tokens.names[token], BUSTER_NODE_CENSUS_DECLARATION_RESULT_KNOWN) & BUSTER_NODE_CENSUS_DECLARATION_RESULT) != 0;
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
static uint64_t node_census_named_object(const NodeCensusScratch* s, uint32_t node)
{
    const CheckGraph* graph = &s->program->input->graph;
    uint32_t at = node;
    uint64_t offset = 0;
    uint32_t members = 0;
    for (uint32_t steps = 0; at < graph->count && graph->ops[at] == CHECK_OP_MEMBER && steps < 64; steps += 1)
    {
        offset += node_view_member_offset(s, at);
        members += 1;
        at = graph->operand0[at];
    }
    const uint32_t object = at < graph->count && graph->ops[at] == CHECK_OP_VALUE ? node_view_object(s, at) : 0;
    return object != 0 ? (uint64_t)object << 32 ^ (offset << 6) ^ members : 0;
}

// The local object a node names through members and array subscripts (its address the node's), for the return test: 0
// for none, or a static or global one.
static uint32_t node_census_local_object(const NodeCensusScratch* s, uint32_t node)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    uint32_t at = node;
    bool walking = true;
    for (uint32_t steps = 0; walking && at < graph->count && steps < 64; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        const uint32_t base = graph->operand0[at];
        const bool subscript = op == CHECK_OP_SUBSCRIPT && base < graph->count && type_kind_is_array(node_census_kind(types, node_view_type(s, base)));
        walking = op == CHECK_OP_MEMBER || subscript;
        at = walking ? base : at;
    }
    const uint32_t object = at < graph->count && graph->ops[at] == CHECK_OP_VALUE ? node_view_object(s, at) : 0;
    // A static object is no local: its symbol says so, or its declaration, or its value's own flag. A string's object has
    // no declaration, and no symbol either in a body the selection leaves out, which the tests at reader time are
    // compared in too (--front-tests): there the flag alone says it.
    const bool local = object != 0 && object < program->object_count && program->object_symbols[object] == 0 &&
                       !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC) &&
                       (node_view_flags(s, at) & BUSTER_CHECK_STATIC_OBJECT) == 0;
    return local ? object : 0;
}

// Whether the value a return gives is a local's address, followed through conversions, casts, a comma's value, a
// conditional's arms and pointer arithmetic: an address of a local, or a local array's value, which decays to it. The
// operands followed wait in a list of BUSTER_NODE_CENSUS_FOLLOWED (a deeper tree's further nodes are refused).
#define BUSTER_NODE_CENSUS_FOLLOWED 64u
static bool node_census_returns_address(const NodeCensusScratch* s, uint32_t root)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
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
                result = node_census_local_object(s, graph->operand0[node]) != 0;
            }
            else if (first == BUSTER_CHECK_NONE)
            {
                result = type_kind_is_array(node_census_kind(types, node_view_type(s, node))) && node_census_local_object(s, node) != 0;
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
        const uint64_t object = node_census_modifies(graph->ops[write]) ? node_census_named_object(s, graph->operand0[write]) : 0;
        for (uint32_t j = 0; object != 0 && j < count && !found; j += 1)
        {
            const uint32_t other = s->list[j];
            const bool named = other != write && other != graph->operand0[write] && node_census_named_object(s, other) == object &&
                               (graph->ops[other] == CHECK_OP_VALUE || graph->ops[other] == CHECK_OP_MEMBER) &&
                               !(s->parents[other] != BUSTER_CHECK_NONE && graph->ops[s->parents[other]] == CHECK_OP_MEMBER);
            const bool another_write = other != write && node_census_modifies(graph->ops[other]) && node_census_named_object(s, graph->operand0[other]) == object;
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
            const TypeId type = node_view_type(s, node);
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
                result = (s->policy.unused_value || s->policy.unused_comparison) && (node_view_flags(s, node) & BUSTER_CHECK_SIDE_EFFECT) == 0;
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
static bool node_census_constant_changes(const NodeCensusScratch* s, uint32_t operand, TypeId target)
{
    const TypeTable* types = s->program->types;
    bool result = false;
    uint32_t width = 0;
    bool sign = false;
    __int128 value = 0;
    const CheckConstantKind kind = node_view_has(s, operand) ? node_view_constant_kind(s, operand) : CHECK_CONSTANT_NONE;
    // -Wassign-enum: an integer constant converted to an enumeration, whatever its value (node_census_constants' callers
    // ask only where it or -Wconstant-conversion is enabled).
    result = s->policy.assign_enum && kind == CHECK_CONSTANT_INTEGER && target != 0 && node_census_enum(types, target);
    if (!result && target != 0 && kind == CHECK_CONSTANT_INTEGER && node_census_integer(types, target, &width, &sign))
    {
        // A conversion to a type at least as wide changes no value but its sign, which -Wsign-conversion says, not
        // -Wconstant-conversion: only a narrower one is tested.
        bool source_sign = false;
        const uint32_t source_width = node_census_width(types, node_view_type(s, operand), &source_sign);
        const bool narrows = source_width == 0 || source_width > width;
        result = narrows && (!node_census_integer_constant(s, operand, &value) || !node_census_fits(value, width, sign));
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
        const bool known = node_census_integer_constant(s, operand, &value);
        const uint64_t magnitude = value < 0 ? (uint64_t)(-value) : (uint64_t)value;
        const uint32_t span = magnitude != 0 ? 64 - (uint32_t)__builtin_clzll(magnitude) - (uint32_t)__builtin_ctzll(magnitude) : 0;
        result = !known || span > precision;
    }
    return result;
}

// The silence test of constants at node `node`: its operands' conversions, a division by zero, a shift's constant count or
// negative operand, a null dereference, a constant subscript outside its array, a comparison with a constant outside or at
// the bound of its other operand's type, a non-literal null converted to a pointer.
static bool node_census_constant_tests(const NodeCensusScratch* s, uint32_t node)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    const NodeCensusPolicy* policy = &s->policy;
    const CheckOp op = graph->ops[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    bool result = false;
    // The operands' implicit conversions, and a call's arguments'.
    const bool conversions = policy->constant_conversion || policy->assign_enum;
    for (uint32_t i = 0; i < 3 && op != CHECK_OP_CAST && op != CHECK_OP_VALUE && conversions && !result; i += 1)
    {
        const uint32_t operand = i == 0 ? a : i == 1 ? b : graph->operand2[node];
        result = operand < graph->count && node_census_constant_changes(s, operand, node_view_converted(s, node, i));
    }
    if (op == CHECK_OP_CALL && conversions && !result)
    {
        const uint32_t first = graph->argument_offsets[node];
        for (uint32_t i = 0; i < graph->argument_counts[node] && !result; i += 1)
        {
            result = node_census_constant_changes(s, graph->argument_nodes[first + i], node_view_argument_converted(s, node, i));
        }
    }
    __int128 value = 0;
    const bool divide = op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN;
    result |= policy->division && divide && node_census_integer_constant(s, b, &value) && value == 0;
    const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
    if (policy->shifts && shift && !result)
    {
        bool sign = false;
        const uint32_t width = a < graph->count ? node_census_width(types, node_view_type(s, a), &sign) : 0;
        const uint32_t promoted = width < 32 ? 32 : width;
        result = node_census_integer_constant(s, b, &value) && (value < 0 || width == 0 || value >= promoted);
        const bool left_constant = node_census_integer_constant(s, a, &value);
        result |= (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_LEFT_ASSIGN) && left_constant && value < 0;
    }
    // -Wshift-sign-overflow: a signed left shift by a constant (whose fold may set the sign bit).
    if (policy->shift_sign_overflow && (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_LEFT_ASSIGN) && !result)
    {
        bool sign = false;
        const uint32_t width = a < graph->count ? node_census_width(types, node_view_type(s, a), &sign) : 0;
        result = (sign || width < 32) && node_census_integer_constant(s, b, &value);
    }
    const bool through = op == CHECK_OP_DEREFERENCE || op == CHECK_OP_ARROW || op == CHECK_OP_SUBSCRIPT;
    if (policy->null_dereference && through && !result)
    {
        const uint32_t pointer = op == CHECK_OP_SUBSCRIPT && b < graph->count && node_census_pointer(types, node_view_type(s, b)) ? b : a;
        const CheckConstantKind kind = pointer < graph->count ? node_view_constant_kind(s, pointer) : CHECK_CONSTANT_NONE;
        const CheckValue v = kind != CHECK_CONSTANT_NONE ? node_view_value(s, pointer) : (CheckValue){ 0 };
        result = (kind == CHECK_CONSTANT_INTEGER && v.integer == 0) || (kind == CHECK_CONSTANT_ADDRESS && v.object == 0);
    }
    if (policy->array_bounds && op == CHECK_OP_SUBSCRIPT && !result)
    {
        const bool a_array = a < graph->count && type_kind_is_array(node_census_kind(types, node_view_type(s, a)));
        const uint32_t array = a_array ? a : b;
        const uint32_t index = a_array ? b : a;
        const TypeId array_type = array < graph->count ? node_census_base(types, node_view_type(s, array)) : 0;
        const bool sized = array_type != 0 && type_kind(types, array_type) == BUSTER_TYPE_KIND_ARRAY;
        const uint64_t element = sized ? type_size(types, type_element(types, array_type)) : 0;
        const uint64_t length = element != 0 ? type_size(types, array_type) / element : 0;
        // `&a[N]` is the array's end, which C allows: an address of a subscript may be one past its last element.
        const bool addressed = s->parents[node] != BUSTER_CHECK_NONE && graph->ops[s->parents[node]] == CHECK_OP_ADDRESS;
        result = sized && node_census_integer_constant(s, index, &value) && (value < 0 || value > (__int128)length || (value == (__int128)length && !addressed));
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
            const uint32_t constant_width = constant < graph->count ? node_census_width(types, node_view_type(s, constant), &constant_sign) : 0;
            bool known = node_census_integer_constant(s, constant, &value) && other < graph->count &&
                         node_view_constant_kind(s, other) != CHECK_CONSTANT_INTEGER &&
                         node_census_integer(types, node_view_type(s, other), &width, &sign) && constant_width != 0;
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
        const bool to_pointer = node_census_pointer(types, node_view_type(s, node));
        result = to_pointer && source < graph->count && graph->ops[source] != CHECK_OP_VALUE && graph->ops[source] != CHECK_OP_CAST &&
                 node_census_integer_constant(s, source, &value) && value == 0;
    }
    return result;
}

// The silence test of constants at node `node` (node_census_constant_tests). Each of its tests reads a constant among
// the node's operands or a call's arguments: where none is one, none can fire, and none is asked.
static bool node_census_constants(const NodeCensusScratch* s, uint32_t node)
{
    const CheckGraph* graph = &s->program->input->graph;
    const CheckOp op = graph->ops[node];
    bool constant = false;
    for (uint32_t i = 0; i < 3 && !constant; i += 1)
    {
        const uint32_t operand = i == 0 ? graph->operand0[node] : i == 1 ? graph->operand1[node] : graph->operand2[node];
        constant = operand < graph->count && node_view_has(s, operand) && node_view_constant_kind(s, operand) != CHECK_CONSTANT_NONE;
    }
    for (uint32_t i = 0; op == CHECK_OP_CALL && !constant && i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
        constant = argument < graph->count && node_view_has(s, argument) && node_view_constant_kind(s, argument) != CHECK_CONSTANT_NONE;
    }
    return constant && node_census_constant_tests(s, node);
}

// check_common_atomic_member's test of member access `node`, with its operand's type read through the view: whether
// the operand is an atomic struct or union (an arrow's pointer after its lvalue conversion).
static bool node_view_atomic_member(const NodeCensusScratch* s, uint32_t node)
{
    const CheckGraph* graph = &s->program->input->graph;
    const TypeTable* types = s->program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t base = graph->operand0[node];
    TypeId record = base < graph->count && node_view_has(s, base) ? node_view_type(s, base) : 0;
    if (op == CHECK_OP_ARROW && record != 0)
    {
        record = type_strip_aligned(types, type_conversion_operand(types, record));
        const uint8_t kind = type_kind(types, record);
        record = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? type_element(types, record) : 0;
    }
    record = record != 0 ? type_strip_aligned(types, record) : 0;
    const TypeId element = record != 0 && type_kind(types, record) == BUSTER_TYPE_KIND_ATOMIC ? type_strip_aligned(types, type_element(types, record)) : 0;
    const uint8_t kind = element != 0 ? type_kind(types, element) : BUSTER_TYPE_KIND_NONE;
    const bool result = (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION);
    return result;
}

// A leaf's tests of the set, as bits of NodeCensusTest: a compound literal's object; and an object or a name the tables
// don't model (an overload set's, a register object, a declaration with a flagged attribute). `token` is the leaf's and
// `flags` its value's.
static inline uint64_t node_census_leaf(const NodeCensusScratch* s, uint32_t node, uint32_t token, uint32_t flags)
{
    const CheckProgram* program = s->program;
    const CheckInput* input = program->input;
    const uint32_t object = node_view_object(s, node);
    const bool known = object != 0 && object < program->object_count;
    const bool compound = known && check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND);
    const bool register_object = known && check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_REGISTER);
    const uint32_t declaration = node_census_declaration(program, token);
    const bool flagged = node_view_overloaded(s, node) || register_object || (flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) != 0 ||
                         (declaration != BUSTER_CHECK_NONE &&
                          (node_census_declaration_facts(s, declaration, program->parser->tokens.names[token], BUSTER_NODE_CENSUS_DECLARATION_FLAGGED_KNOWN) & BUSTER_NODE_CENSUS_DECLARATION_FLAGGED) != 0);
    return (compound ? UINT64_C(1) << NODE_CENSUS_COMPOUND_LITERAL : 0) | (flagged ? UINT64_C(1) << NODE_CENSUS_DECLARATION : 0);
}

// The tests of node `node` of the current expression (`root` its root, `context` its root's kind), as bits of
// NodeCensusTest.
static uint64_t node_census_node(NodeCensusScratch* s, uint32_t node, uint32_t root, NodeCensusRoot context)
{
    const CheckProgram* program = s->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const TypeTable* types = program->types;
    const Parser* parser = program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    const CheckOp op = graph->ops[node];
    // The node's value's type and flags: the tests read no more of it but at a leaf (its object, its overload set). What
    // they ask of a type at every node is a byte kept a type (node_census_type_bits).
    const TypeId type = node_view_type(s, node);
    const uint32_t flags = node_view_flags(s, node);
    const uint8_t type_bits = node_census_type_bits(s, type);
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const TypeId a_type = a < graph->count && op != CHECK_OP_VALUE ? node_view_type(s, a) : 0;
    const TypeId b_type = b < graph->count && op != CHECK_OP_VALUE ? node_view_type(s, b) : 0;
    const uint32_t token = graph->tokens[node];
    uint64_t tests = 0;
    // The set.
    tests |= !node_census_coded(op) ? UINT64_C(1) << NODE_CENSUS_OPERATOR : 0;
    tests |= (type_bits & BUSTER_NODE_CENSUS_TYPE_SPECIAL) != 0 ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= types->atomic_records && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && node_view_atomic_member(s, node) ?
             UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= (flags & BUSTER_CHECK_BIT_FIELD) != 0 ? UINT64_C(1) << NODE_CENSUS_BIT_FIELD : 0;
    const bool copies = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                        op == CHECK_OP_CAST || op == CHECK_OP_CALL;
    bool aggregate = copies && (type_bits & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0;
    for (uint32_t i = 0; op == CHECK_OP_CALL && !aggregate && i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + i];
        aggregate = argument < graph->count && (node_census_type_bits(s, node_view_type(s, argument)) & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0;
    }
    tests |= aggregate ? UINT64_C(1) << NODE_CENSUS_AGGREGATE : 0;
    tests |= op == CHECK_OP_VALUE ? node_census_leaf(s, node, token, flags) : 0;
    // -Wtype-safety reads a call's arguments against its callee's type tags: in a unit that declares any, every call.
    tests |= op == CHECK_OP_CALL && (node_census_callee(s, node) || policy->type_safety) ? UINT64_C(1) << NODE_CENSUS_CALLEE : 0;
    // The silence tests.
    uint32_t ignored = 0;
    const uint32_t warnings = node_view_warnings(s, node, &ignored);
    const uint32_t kept = (warnings & ~ignored & policy->action_bits) | (warnings & ignored & policy->action_ignored_bits);
    tests |= kept != 0 ? UINT64_C(1) << NODE_CENSUS_ACTION_WARNING : 0;
    const bool binary = op != CHECK_OP_VALUE && a < graph->count && b < graph->count;
    if (policy->enums && op != CHECK_OP_VALUE)
    {
        const TypeId left = op == CHECK_OP_CONDITIONAL ? b_type : a_type;
        const TypeId right = op == CHECK_OP_CONDITIONAL ? (graph->operand2[node] < graph->count ? node_view_type(s, graph->operand2[node]) : 0) : b_type;
        const uint8_t left_bits = node_census_type_bits(s, left);
        const uint8_t right_bits = node_census_type_bits(s, right);
        const uint8_t a_bits = node_census_type_bits(s, a_type);
        const bool both = (left_bits & right_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 && node_census_base(types, left) != node_census_base(types, right);
        const bool floating = ((left_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 && (right_bits & BUSTER_NODE_CENSUS_TYPE_FLOATING) != 0) ||
                              ((right_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 && (left_bits & BUSTER_NODE_CENSUS_TYPE_FLOATING) != 0);
        const bool converts = (op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE) && (a_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 &&
                              (type_bits & (BUSTER_NODE_CENSUS_TYPE_ENUM | BUSTER_NODE_CENSUS_TYPE_FLOATING)) != 0 &&
                              node_census_base(types, a_type) != node_census_base(types, type);
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
        const bool a_safe = node_census_integer_constant(s, a, &value) && value >= 0;
        const bool b_safe = node_census_integer_constant(s, b, &value) && value >= 0;
        const bool mixed = (a_wide && bs && !b_safe) || (b_wide && as && !a_safe);
        tests |= integers && mixed ? UINT64_C(1) << NODE_CENSUS_SIGN_COMPARE : 0;
    }
    if (op == CHECK_OP_CAST && policy->pointer_casts && (node_census_type_bits(s, a_type) & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0)
    {
        uint32_t width = 0;
        bool sign = false;
        const bool narrow = node_census_integer(types, type, &width, &sign) && width < 64;
        tests |= narrow || (type_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
    }
    if ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference && (node_census_type_bits(s, a_type) & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0)
    {
        const TypeId pointee = type_unqualified(type_strip_aligned(types, type_element(types, node_census_base(types, a_type))));
        tests |= type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
    }
    if (op == CHECK_OP_ADD && policy->string_plus_int && binary && (node_census_string(program, a) || node_census_string(program, b)))
    {
        tests |= UINT64_C(1) << NODE_CENSUS_STRING_PLUS_INT;
    }
    const bool packed = s->records != NULL ? s->packed_unit : token < parser->tokens.count && s->packed[token] != 0;
    tests |= op == CHECK_OP_ADDRESS && policy->packed_member && packed ? UINT64_C(1) << NODE_CENSUS_PACKED_MEMBER : 0;
    if ((op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && a < graph->count)
    {
        const bool effect = policy->unevaluated && (node_view_flags(s, a) & BUSTER_CHECK_SIDE_EFFECT) != 0;
        const bool expression_alignof = op == CHECK_OP_ALIGNOF && policy->alignof_expression;
        // -Wsizeof-array-decay: sizeof of a pointer an operator made of an array (`sizeof(a + 1)`).
        const CheckOp inner = graph->ops[a];
        const bool operator = inner == CHECK_OP_ADD || inner == CHECK_OP_SUBTRACT || inner == CHECK_OP_CONDITIONAL || inner == CHECK_OP_COMMA;
        bool decays = false;
        for (uint32_t i = 0; policy->sizeof_decay && operator && i < 3 && !decays; i += 1)
        {
            const uint32_t operand = i == 0 ? graph->operand0[a] : i == 1 ? graph->operand1[a] : graph->operand2[a];
            decays = operand < graph->count && type_kind_is_array(node_census_kind(types, node_view_type(s, operand)));
        }
        tests |= effect || expression_alignof || decays ? UINT64_C(1) << NODE_CENSUS_UNEVALUATED : 0;
    }
    tests |= op != CHECK_OP_VALUE && node_census_constants(s, node) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    // The parser's tree: parentheses.
    const uint32_t count = parser->tokens.count;
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    // Its token's parent in the tree, which only an assignment's, a `!`'s and a comparison's tests below read.
    const bool climbs = op == CHECK_OP_ASSIGN || op == CHECK_OP_NOT || check_common_comparison_role(role);
    const uint32_t parent = climbs && token < count ? check_common_parent(parser, token) : BUSTER_CHECK_NONE;
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
    // A truth context matters to a left shift and a product alone.
    const bool truth = (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_MULTIPLY) && node_census_truth(s, node, context == NODE_CENSUS_ROOT_CONDITION);
    __int128 value = 0;
    const bool constant_shift = op == CHECK_OP_SHIFT_LEFT && node_census_integer_constant(s, a, &value);
    parentheses |= truth && ((policy->int_in_bool && (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_MULTIPLY)) || (policy->tautological && constant_shift));
    tests |= parentheses ? UINT64_C(1) << NODE_CENSUS_PARENTHESES : 0;
    bool unused = policy->unused_comparison && check_common_comparison_role(role) && parent < count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR &&
                  check_common_statement_terminator(parser, parent);
    unused |= op == CHECK_OP_COMMA && node_census_discarded(s, a);
    tests |= unused ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0;
    if (policy->self_assign && op == CHECK_OP_ASSIGN && binary)
    {
        const uint64_t left = node_census_named_object(s, a);
        const bool same = left != 0 && left == node_census_named_object(s, b);
        tests |= same ? UINT64_C(1) << NODE_CENSUS_SELF_ASSIGN : 0;
    }
    // The design's system-macro test, counted apart after the pipeline (the report's): no test of reader time's.
    s->mixed_system |= s->records == NULL && node_census_system(s, token) && !node_census_system(s, graph->tokens[root]);
    tests |= input->fp_environments != NULL && graph->fp_environments != NULL &&
             (graph->fp_environments[node] & (BUSTER_CHECK_FP_ENV_ACCESS | BUSTER_CHECK_FP_EVAL_MASK)) != 0 ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
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

// The walk of the expression whose root is node `root`: its graph subtree's nodes into the scratch's list, each with its
// parent and its depth, and their count; into `lowest_token` and `highest_token` the least and the greatest of their
// tokens.
static uint32_t node_census_walk(NodeCensusScratch* s, uint32_t root, uint32_t* lowest_token, uint32_t* highest_token)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    s->stamp += 1;
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
        s->missed += s->records != NULL && node_view_record(s, node) == UINT32_MAX;
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
    *lowest_token = lowest;
    *highest_token = highest;
    return count;
}

// The tests of an expression that are its own and no node's, as bits of NodeCensusTest: the expression's root is node
// `root`, popped with `flags`, of root kind `context`; it has `count` nodes, whose tokens span `lowest` to `highest`.
// The unsequenced test reads the walk's list of `walked` nodes, and is asked where `unsequenced` says.
static uint64_t node_census_root_tests(NodeCensusScratch* s, uint32_t root, uint8_t flags, NodeCensusRoot context, uint32_t count, uint32_t lowest, uint32_t highest,
                                       bool unsequenced, uint32_t walked)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    uint64_t tests = 0;
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
    const TypeId declared = context != NODE_CENSUS_ROOT_INITIALIZER ? 0 : s->given_declared != 0 || s->records != NULL ? s->given_declared : node_census_initialized_type(s, root);
    tests |= (s->policy.constant_conversion || s->policy.assign_enum) && declared != 0 && node_census_constant_changes(s, root, declared) ?
             UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    tests |= declared != 0 && node_census_special_type(program->types, declared) ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= s->policy.return_address && context == NODE_CENSUS_ROOT_RETURN && node_census_returns_address(s, root) ?
             UINT64_C(1) << NODE_CENSUS_RETURN_ADDRESS : 0;
    tests |= s->policy.unsequenced && unsequenced && node_census_unsequenced(s, walked, root) ? UINT64_C(1) << NODE_CENSUS_UNSEQUENCED : 0;
    const bool events = s->event_prefix != NULL && lowest <= highest && s->event_prefix[highest + 1] != s->event_prefix[lowest];
    tests |= events ? UINT64_C(1) << NODE_CENSUS_EVENT : 0;
    // T's actions' warnings for the expression's nodes, those the unit's nodes suppress aside: at least the ones the
    // graph run keeps a node (it drops an unevaluated operand's folds').
    const uint32_t actions = s->records != NULL ? s->records->action_warnings & 0x01ffffffu : 0;
    tests |= (actions & s->policy.action_bits) != 0 ? UINT64_C(1) << NODE_CENSUS_ACTION_WARNING : 0;
    s->span_low = lowest;
    s->span_high = highest;
    return tests;
}

// The tests of the expression whose root is node `root`, popped with `flags`, of root kind `context`: its graph subtree
// walked into the scratch's list, then each node's tests and the expression's own. At reader time (the view's records)
// the lowering's flags are none, an initializer's object's type is the caller's (NodeCensusScratch.given_declared), the
// actions' warnings are T's for the whole expression, and the warning events are the caller's to test.
static uint64_t node_census_expression(NodeCensusScratch* s, uint32_t root, uint8_t flags, NodeCensusRoot context, uint32_t* walked)
{
    s->mixed_system = false;
    uint32_t lowest = UINT32_MAX;
    uint32_t highest = 0;
    const uint32_t count = node_census_walk(s, root, &lowest, &highest);
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
    tests |= node_census_root_tests(s, root, flags, context, count, lowest, highest, true, count);
    *walked = count;
    return tests;
}

// The tests in the machine's own form (docs/node-machine.md, "The tests in the machine's own form"): the same bits as
// node_census_expression's at reader time, from one pass over T's records in their order. There is no walk of the
// graph and no step from a node to its record: a record's operands are records below it, whose types' bytes the pass
// has kept. What every record pays is its operator's place in the set, its type's byte, its flags and its token; a
// leaf pays its declaration's byte. An operator's silence tests are asked only where a condition
// that each of them needs holds (a trigger, by the operator and its operands' bytes and constants); the record is then
// sent to the census's own code (node_census_node), after a walk of the expression for the tests that read a node's
// parent. So the pass's own conditions are never the verdict of a silence test: they only say where none can fire.
//
// It runs where it sits: on an expression the front made whole and T took. What those decide first it doesn't ask again
// (each is the census's code's condition still, which runs beside it with --verify, and whose fixtures' lines would show
// the form's lack the day one of these stops holding):
// - T refuses a record in a floating-point pragma's region or a constant context (its context refusal, by the same bits
//   as the region test's), a call whose callee's type has no prototype (node_machine_callee_refused), and a leaf with an
//   overload set (its leaf refusal). So no record has a region's bit, no call's callee lacks a prototype, and a wide
//   leaf's bits are its columns', as any leaf's.
// - The front leaves sizeof, _Alignof and a compound literal to the walk, so no record is one, and no leaf's object is a
//   compound literal's.
// - A register object's leaf has BUSTER_CHECK_REGISTER in its flags (the reader's, from the same declaration's bit as
//   the object's): the flags alone say it, before its declaration's attributes are asked. (The census's code never
//   asks a register object's: its own register test comes first. So here such a leaf always went to that code, and
//   neither of the form's two register conditions behind it could fire.)
// - A root is a condition's only where it is the CHECK_OP_CONDITION wrapper (node_census_reader_expression), whose
//   operand, a shift or a product read as a truth, the wrapper's own send takes.
typedef enum NodeTestsTrigger
{
    NODE_TESTS_TRIGGER_ATOMIC_MEMBER,
    NODE_TESTS_TRIGGER_ENUM,
    NODE_TESTS_TRIGGER_SIGN_COMPARE,
    NODE_TESTS_TRIGGER_VOID_DEREFERENCE,
    NODE_TESTS_TRIGGER_CONVERSION,
    NODE_TESTS_TRIGGER_DIVISION,
    NODE_TESTS_TRIGGER_SHIFT_COUNT,
    NODE_TESTS_TRIGGER_NULL,
    NODE_TESTS_TRIGGER_BOUNDS,
    NODE_TESTS_TRIGGER_COMPARISON,
    NODE_TESTS_TRIGGER_NULL_CONSTANT,
    NODE_TESTS_TRIGGER_SHIFT_OPERAND,
    NODE_TESTS_TRIGGER_CHAINED,
    NODE_TESTS_TRIGGER_TRUTH,
    NODE_TESTS_TRIGGER_SELF_ASSIGN,
    NODE_TESTS_TRIGGER_UNSEQUENCED,
    NODE_TESTS_TRIGGERS,
} NodeTestsTrigger;

static const char* const node_tests_trigger_names[NODE_TESTS_TRIGGERS] = {
    [NODE_TESTS_TRIGGER_ATOMIC_MEMBER] = "atomic-member",
    [NODE_TESTS_TRIGGER_ENUM] = "enum-operand",
    [NODE_TESTS_TRIGGER_SIGN_COMPARE] = "sign-compare",
    [NODE_TESTS_TRIGGER_VOID_DEREFERENCE] = "void-dereference",
    [NODE_TESTS_TRIGGER_CONVERSION] = "constant-conversion",
    [NODE_TESTS_TRIGGER_DIVISION] = "constant-divisor",
    [NODE_TESTS_TRIGGER_SHIFT_COUNT] = "constant-shift",
    [NODE_TESTS_TRIGGER_NULL] = "constant-pointer",
    [NODE_TESTS_TRIGGER_BOUNDS] = "array-subscript",
    [NODE_TESTS_TRIGGER_COMPARISON] = "constant-comparison",
    [NODE_TESTS_TRIGGER_NULL_CONSTANT] = "constant-to-pointer",
    [NODE_TESTS_TRIGGER_SHIFT_OPERAND] = "shift-of-sum",
    [NODE_TESTS_TRIGGER_CHAINED] = "chained-comparison",
    [NODE_TESTS_TRIGGER_TRUTH] = "shift-or-product-as-truth",
    [NODE_TESTS_TRIGGER_SELF_ASSIGN] = "assignment-of-a-name",
    [NODE_TESTS_TRIGGER_UNSEQUENCED] = "writes",
};

// What the own form asks of a type's widths, as one word kept by the type's index: its width as an integer of at most 64
// bits (node_census_integer's: bits 0 to 6, 0 for none) and whether that is signed (bit 7); its width before promotion of
// at most 128 (node_census_width's: bits 8 to 15) and whether that is signed (bit 16); and bit 31 once made.
#define BUSTER_NODE_TESTS_RANGE_SIGNED 0x80u
#define BUSTER_NODE_TESTS_RANGE_WIDE_SIGNED 0x10000u
#define BUSTER_NODE_TESTS_RANGE_KNOWN 0x80000000u

static __attribute__((noinline)) uint32_t node_tests_ranges_make(const TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    const bool integer = node_census_integer(types, type, &width, &sign);
    bool wide_sign = false;
    const uint32_t wide = node_census_width(types, type, &wide_sign);
    return BUSTER_NODE_TESTS_RANGE_KNOWN | (integer ? width : 0) | (integer && sign ? BUSTER_NODE_TESTS_RANGE_SIGNED : 0) | wide << 8 |
           (wide != 0 && wide_sign ? BUSTER_NODE_TESTS_RANGE_WIDE_SIGNED : 0);
}

static inline uint32_t node_tests_ranges(const NodeCensusScratch* s, TypeId type)
{
    const uint32_t index = type_index(type);
    const bool kept = type != 0 && s->type_ranges != NULL && index < s->type_bits_count;
    uint32_t ranges = kept ? s->type_ranges[index] : 0;
    if (type != 0 && ranges == 0)
    {
        ranges = node_tests_ranges_make(s->program->types, type);
        if (kept)
        {
            s->type_ranges[index] = ranges;
        }
    }
    return ranges;
}

// An operand's record among T's records of the current expression (node_view_record's, an operand being no root):
// UINT32_MAX for a node that is none or outside them.
static inline uint32_t node_tests_record(const NodeMachine* machine, uint32_t node)
{
    const uint32_t record = node - machine->first;
    return node >= machine->first && record + 1 < machine->count ? record : UINT32_MAX;
}

// Record `r`'s value as an integer constant the way node_census_integer_constant reads it, where its columns hold it
// whole: whether it is one, and its value into `value`. A wide record reads as none here: `wide` says so, for the
// caller to send its operator to the census's code.
static inline bool node_tests_integer(const NodeCensusScratch* s, uint32_t r, int64_t* value, bool* wide)
{
    const NodeMachineColumns* columns = s->records->columns;
    const uint8_t standing = columns->standings[r];
    const bool integer = ((standing >> 2) & 0x07u) == CHECK_CONSTANT_INTEGER;
    const uint32_t ranges = integer ? node_tests_ranges(s, columns->value_types[r]) : 0;
    const uint32_t width = ranges & 0x7fu;
    const bool plain = (standing & (BUSTER_NODE_MACHINE_WIDE | BUSTER_NODE_MACHINE_FIELD)) == 0;
    const uint64_t raw = columns->payloads[r];
    const uint32_t shift = 64 - (width != 0 ? width : 64);
    const uint64_t bits = raw << shift;
    const bool sign = (ranges & BUSTER_NODE_TESTS_RANGE_SIGNED) != 0;
    // An unsigned value past INT64_MAX has no place in `value`: it reads as wide, for the census's code.
    const bool huge = !sign && width == 64 && (raw >> 63) != 0;
    *wide = integer && (!plain || huge);
    *value = sign ? (int64_t)bits >> shift : (int64_t)(bits >> shift);
    return integer && plain && width != 0 && !huge;
}

// What a constant operand's conversion to `target` does (node_census_constant_changes'): nothing, a change, or one the
// census's code is asked about. Operand record `o` has constant kind `kind`. An integer constant changes where it
// doesn't fit a narrower integer type, and under -Wassign-enum where the target is an enumeration; a floating constant
// changes to an integer type; an integer constant to a floating type, and one whose value the columns don't hold, are
// the census's code's.
#define BUSTER_NODE_TESTS_CONVERSION_NONE 0u
#define BUSTER_NODE_TESTS_CONVERSION_CHANGES 1u
#define BUSTER_NODE_TESTS_CONVERSION_ASKED 2u

static inline __attribute__((always_inline)) uint32_t node_tests_conversion(const NodeCensusScratch* s, uint32_t o, uint32_t kind, TypeId target)
{
    uint32_t result = BUSTER_NODE_TESTS_CONVERSION_NONE;
    if (target != 0 && (kind == CHECK_CONSTANT_INTEGER || kind == CHECK_CONSTANT_FLOATING))
    {
        const uint8_t target_bits = node_census_type_bits(s, target);
        const uint32_t target_ranges = node_tests_ranges(s, target);
        const uint32_t target_width = target_ranges & 0x7fu;
        const bool target_sign = (target_ranges & BUSTER_NODE_TESTS_RANGE_SIGNED) != 0;
        const uint32_t source_width = (node_tests_ranges(s, s->records->columns->value_types[o]) >> 8) & 0xffu;
        const bool narrows = source_width == 0 || source_width > target_width;
        if (kind == CHECK_CONSTANT_FLOATING)
        {
            result = target_width != 0 ? BUSTER_NODE_TESTS_CONVERSION_CHANGES : BUSTER_NODE_TESTS_CONVERSION_NONE;
        }
        else if (s->policy.assign_enum && (target_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0)
        {
            result = BUSTER_NODE_TESTS_CONVERSION_CHANGES;
        }
        else if (target_width != 0 && narrows)
        {
            // Whether the value survives, as a value or as the same bits of the other signedness (node_census_fits).
            int64_t value = 0;
            bool wide = false;
            const bool known = node_tests_integer(s, o, &value, &wide);
            const int64_t smallest = target_width == 64 ? INT64_MIN : -(INT64_C(1) << (target_width - 1));
            const int64_t largest = target_width == 64 ? INT64_MAX : (INT64_C(1) << (target_width - 1)) - 1;
            const uint64_t unsigned_largest = target_width == 64 ? UINT64_MAX : (UINT64_C(1) << target_width) - 1;
            const bool fits = value >= smallest && (target_sign ? value <= largest : value < 0 || (uint64_t)value <= unsigned_largest);
            result = wide ? BUSTER_NODE_TESTS_CONVERSION_ASKED : !known || !fits ? BUSTER_NODE_TESTS_CONVERSION_CHANGES : BUSTER_NODE_TESTS_CONVERSION_NONE;
        }
        else if (target_width == 0 && (target_bits & BUSTER_NODE_CENSUS_TYPE_FLOATING) != 0)
        {
            result = BUSTER_NODE_TESTS_CONVERSION_ASKED;
        }
    }
    return result;
}

// The constants' tests (node_census_constant_tests) at record `r`, operator `op` at node `node`, whose operands'
// records are `o` (UINT32_MAX for none), one of them a constant: whether one fires, into `fires`, where the columns
// hold what it reads; and otherwise the trigger for the census's code, or NODE_TESTS_TRIGGERS for none. A comparison's
// bound, a constant's precision as a floating value, a subscript past its array's length and any constant the columns
// don't hold whole are the census's code's.
static inline NodeTestsTrigger node_tests_constants(const NodeCensusScratch* s, uint32_t r, uint32_t node, CheckOp op, const uint32_t* o, bool* fires)
{
    const NodeMachineColumns* columns = s->records->columns;
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    const NodeCensusPolicy* policy = &s->policy;
    const uint8_t* kept = s->own_types;
    const uint32_t kinds[3] = { o[0] != UINT32_MAX ? (columns->standings[o[0]] >> 2) & 0x07u : 0, o[1] != UINT32_MAX ? (columns->standings[o[1]] >> 2) & 0x07u : 0,
                                o[2] != UINT32_MAX ? (columns->standings[o[2]] >> 2) & 0x07u : 0 };
    NodeTestsTrigger trigger = NODE_TESTS_TRIGGERS;
    bool fired = false;
    // The operands' implicit conversions: a first two's targets are the record's columns.
    if ((policy->constant_conversion || policy->assign_enum) && op != CHECK_OP_CAST)
    {
        const uint32_t first = kinds[0] != CHECK_CONSTANT_NONE ? node_tests_conversion(s, o[0], kinds[0], columns->converted[2 * r]) : BUSTER_NODE_TESTS_CONVERSION_NONE;
        const uint32_t second = kinds[1] != CHECK_CONSTANT_NONE ? node_tests_conversion(s, o[1], kinds[1], columns->converted[2 * r + 1]) : BUSTER_NODE_TESTS_CONVERSION_NONE;
        const uint32_t third = kinds[2] != CHECK_CONSTANT_NONE ? node_tests_conversion(s, o[2], kinds[2], node_view_converted(s, node, 2)) : BUSTER_NODE_TESTS_CONVERSION_NONE;
        fired = ((first | second | third) & BUSTER_NODE_TESTS_CONVERSION_CHANGES) != 0;
        trigger = ((first | second | third) & BUSTER_NODE_TESTS_CONVERSION_ASKED) != 0 ? NODE_TESTS_TRIGGER_CONVERSION : trigger;
    }
    int64_t value = 0;
    bool wide = false;
    if (op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN)
    {
        // A division by a constant zero.
        const bool known = policy->division && kinds[1] == CHECK_CONSTANT_INTEGER && node_tests_integer(s, o[1], &value, &wide);
        fired |= known && value == 0;
        trigger = policy->division && kinds[1] == CHECK_CONSTANT_INTEGER && wide ? NODE_TESTS_TRIGGER_DIVISION : trigger;
    }
    else if (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN)
    {
        // A shift whose constant count is negative or reaches its operand's promoted width, a left shift of a negative
        // constant, and under -Wshift-sign-overflow a left shift of a signed or narrow operand by any constant.
        const bool left = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_LEFT_ASSIGN;
        const uint32_t operand_ranges = o[0] != UINT32_MAX ? node_tests_ranges(s, columns->value_types[o[0]]) : 0;
        const uint32_t operand_width = (operand_ranges >> 8) & 0xffu;
        const bool operand_sign = (operand_ranges & BUSTER_NODE_TESTS_RANGE_WIDE_SIGNED) != 0;
        const uint32_t promoted = operand_width < 32 ? 32 : operand_width;
        bool count_wide = false;
        const bool counted = kinds[1] == CHECK_CONSTANT_INTEGER && node_tests_integer(s, o[1], &value, &count_wide);
        const bool out_of_range = counted && (value < 0 || operand_width == 0 || value >= (int64_t)promoted);
        int64_t shifted = 0;
        bool shifted_wide = false;
        const bool negative = left && kinds[0] == CHECK_CONSTANT_INTEGER && node_tests_integer(s, o[0], &shifted, &shifted_wide) && shifted < 0;
        const bool overflow = policy->shift_sign_overflow && left && (operand_sign || operand_width < 32) && counted;
        fired |= (policy->shifts && (out_of_range || negative)) || overflow;
        trigger = (policy->shifts || policy->shift_sign_overflow) && (count_wide || shifted_wide) ? NODE_TESTS_TRIGGER_SHIFT_COUNT : trigger;
    }
    else if (op == CHECK_OP_DEREFERENCE || op == CHECK_OP_ARROW || op == CHECK_OP_SUBSCRIPT)
    {
        // A dereference of a null constant: the pointer is a subscript's second operand where that is one; null is an
        // integer constant of 0 or an address constant of no object.
        const uint8_t second_bits = o[1] != UINT32_MAX ? kept[o[1]] : 0;
        const uint8_t first_bits = o[0] != UINT32_MAX ? kept[o[0]] : 0;
        const uint32_t side = op == CHECK_OP_SUBSCRIPT && graph->operand1[node] < graph->count && (second_bits & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0 ? 1 : 0;
        const uint32_t pointer = o[side];
        const uint8_t standing = pointer != UINT32_MAX ? columns->standings[pointer] : 0;
        const bool whole = (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
        const bool zero = (standing & BUSTER_NODE_MACHINE_FIELD) != 0 || columns->payloads[pointer != UINT32_MAX ? pointer : 0] == 0;
        const bool null = (kinds[side] == CHECK_CONSTANT_INTEGER && zero) || (kinds[side] == CHECK_CONSTANT_ADDRESS && columns->objects[pointer != UINT32_MAX ? pointer : 0] == 0);
        const bool pointed = kinds[side] == CHECK_CONSTANT_INTEGER || kinds[side] == CHECK_CONSTANT_ADDRESS;
        fired |= policy->null_dereference && pointed && whole && null;
        trigger = policy->null_dereference && pointed && !whole ? NODE_TESTS_TRIGGER_NULL : trigger;
        // A constant subscript of an array of a known length: inside it, none; at its end or outside, the census's code
        // (an address of the end is allowed, which its parent says).
        if (policy->array_bounds && op == CHECK_OP_SUBSCRIPT && ((first_bits | second_bits) & BUSTER_NODE_CENSUS_TYPE_ARRAY) != 0)
        {
            const bool first_array = (first_bits & BUSTER_NODE_CENSUS_TYPE_ARRAY) != 0;
            const uint32_t array = first_array ? o[0] : o[1];
            const uint32_t index = first_array ? o[1] : o[0];
            const uint32_t index_kind = first_array ? kinds[1] : kinds[0];
            const TypeId array_type = array != UINT32_MAX ? node_census_base(types, columns->value_types[array]) : 0;
            const bool sized = array_type != 0 && type_kind(types, array_type) == BUSTER_TYPE_KIND_ARRAY;
            const uint64_t element = sized ? type_size(types, type_element(types, array_type)) : 0;
            const uint64_t length = element != 0 ? type_size(types, array_type) / element : 0;
            bool index_wide = false;
            const bool known = sized && index_kind == CHECK_CONSTANT_INTEGER && node_tests_integer(s, index, &value, &index_wide);
            const bool inside = known && value >= 0 && (uint64_t)value < length;
            trigger = sized && index_kind == CHECK_CONSTANT_INTEGER && (index_wide || (known && !inside)) ? NODE_TESTS_TRIGGER_BOUNDS : trigger;
        }
    }
    else if (node_census_comparison(op))
    {
        // A comparison with a constant that may be outside or at a bound of its other operand's type: none where the
        // other operand is no integer or is a constant integer too, nor where the constant is a small value strictly
        // inside every integer type of 8 bits or more that can hold its sign (an equality has no bound: zero is inside
        // there).
        for (uint32_t side = 0; policy->tautological && side < 2 && trigger == NODE_TESTS_TRIGGERS; side += 1)
        {
            const uint32_t constant = o[side];
            const uint32_t other = o[1 - side];
            const uint32_t other_ranges = other != UINT32_MAX ? node_tests_ranges(s, columns->value_types[other]) : 0;
            const uint32_t other_width = other_ranges & 0x7fu;
            const bool other_sign = (other_ranges & BUSTER_NODE_TESTS_RANGE_SIGNED) != 0;
            const bool asked = kinds[side] == CHECK_CONSTANT_INTEGER && other != UINT32_MAX && kinds[1 - side] != CHECK_CONSTANT_INTEGER && other_width != 0;
            const bool known = asked && node_tests_integer(s, constant, &value, &wide);
            const bool equality = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
            const bool inside = known && other_width >= 8 &&
                                ((value >= 1 && value <= 126) || (value >= -127 && value <= 0 && other_sign) || (value == 0 && equality));
            // A constant whose own type is no integer of 64 bits at most is none for the test, as there.
            const bool none = asked && !known && !wide;
            trigger = asked && !inside && !none ? NODE_TESTS_TRIGGER_COMPARISON : trigger;
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT)
    {
        // A constant zero that is no literal converted to a pointer.
        const uint32_t side = op == CHECK_OP_ASSIGN ? 1 : 0;
        const uint32_t source = side == 1 ? graph->operand1[node] : graph->operand0[node];
        const bool computed = source < graph->count && graph->ops[source] != CHECK_OP_VALUE && graph->ops[source] != CHECK_OP_CAST;
        const bool asked = policy->non_literal_null && (kept[r] & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0 && kinds[side] == CHECK_CONSTANT_INTEGER && computed;
        const bool known = asked && node_tests_integer(s, o[side], &value, &wide);
        fired |= known && value == 0;
        trigger = asked && wide ? NODE_TESTS_TRIGGER_NULL_CONSTANT : trigger;
    }
    *fires = fired;
    return trigger;
}

// An operator's classes for the own form's pass (NodeCensusScratch.own_classes, by CheckOp): outside the set, a leaf, a
// write, an address, one whose token's parent the tree's tests read (`!`, `=`).
#define BUSTER_NODE_TESTS_CLASS_UNCODED 0x01u
#define BUSTER_NODE_TESTS_CLASS_LEAF 0x02u
#define BUSTER_NODE_TESTS_CLASS_WRITE 0x04u
#define BUSTER_NODE_TESTS_CLASS_ADDRESS 0x08u
#define BUSTER_NODE_TESTS_CLASS_TREE 0x10u
// What a record is as an operand (NodeCensusScratch.own_properties, a byte a record): a constant; of an enumeration's
// type, a pointer's, a pointer's whose pointee may be void, an array's; an addition or a subtraction; a comparison; a
// left shift or a product. An operator's `own_sends` says which of its operands' properties send it to its own tests.
#define BUSTER_NODE_TESTS_OPERAND_CONSTANT 0x01u
#define BUSTER_NODE_TESTS_OPERAND_ENUM 0x02u
#define BUSTER_NODE_TESTS_OPERAND_POINTER 0x04u
#define BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE 0x08u
#define BUSTER_NODE_TESTS_OPERAND_ARRAY 0x10u
#define BUSTER_NODE_TESTS_OPERAND_SUM 0x20u
#define BUSTER_NODE_TESTS_OPERAND_COMPARISON 0x40u
#define BUSTER_NODE_TESTS_OPERAND_TRUTH 0x80u
_Static_assert(CHECK_OP_COUNT <= 128, "the own form's tables have an entry an operator");

// The own form's tables for a unit, by its enabled warnings: each operator's classes, what it is as an operand, the
// mask of its type's byte where it copies its value (an aggregate's), whether it always has tests of its own, and the
// operand properties that give it some.
static void node_tests_tables(NodeCensusScratch* s, const TypeTable* types)
{
    const NodeCensusPolicy* policy = &s->policy;
    const bool truths = policy->int_in_bool || policy->tautological;
    for (uint32_t code = 0; code < 128; code += 1)
    {
        const CheckOp op = (CheckOp)code;
        const bool leaf = op == CHECK_OP_VALUE;
        const bool comparison = node_census_comparison(op);
        const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
        const bool copies = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                            op == CHECK_OP_CAST;
        const bool truth_parent = op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL;
        s->own_classes[code] = (uint8_t)((!node_census_coded(op) ? BUSTER_NODE_TESTS_CLASS_UNCODED : 0) | (leaf ? BUSTER_NODE_TESTS_CLASS_LEAF : 0) |
                                         (node_census_modifies(op) ? BUSTER_NODE_TESTS_CLASS_WRITE : 0) | (op == CHECK_OP_ADDRESS ? BUSTER_NODE_TESTS_CLASS_ADDRESS : 0) |
                                         (op == CHECK_OP_NOT || op == CHECK_OP_ASSIGN ? BUSTER_NODE_TESTS_CLASS_TREE : 0));
        s->own_operand_bits[code] = (uint8_t)((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT ? BUSTER_NODE_TESTS_OPERAND_SUM : 0) |
                                              (comparison ? BUSTER_NODE_TESTS_OPERAND_COMPARISON : 0) |
                                              (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_MULTIPLY ? BUSTER_NODE_TESTS_OPERAND_TRUTH : 0));
        s->own_copies[code] = copies ? UINT8_MAX : 0;
        // A call's callee and arguments and a comma's discard are read at each, and the third operand of a conditional
        // (or of an operator outside the set); a member's record only in a unit with an atomic one, a comparison's
        // operands' signs and an assignment's names only under their warnings.
        const bool always = op == CHECK_OP_CALL || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                            !node_census_coded(op) || (types->atomic_records && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)) ||
                            (policy->sign_compare && comparison) || (policy->self_assign && op == CHECK_OP_ASSIGN);
        s->own_always[code] = !leaf && always ? 1 : 0;
        // A constant operand's tests are no cast's (it converts nothing implicitly, and no other constant's test is a
        // cast's).
        s->own_sends[code] = leaf ? 0 :
                             (uint8_t)((op != CHECK_OP_CAST ? BUSTER_NODE_TESTS_OPERAND_CONSTANT : 0) | (policy->enums ? BUSTER_NODE_TESTS_OPERAND_ENUM : 0) |
                                       (op == CHECK_OP_CAST && policy->pointer_casts ? BUSTER_NODE_TESTS_OPERAND_POINTER : 0) |
                                       ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference ? BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE : 0) |
                                       (op == CHECK_OP_ADD && policy->string_plus_int ? BUSTER_NODE_TESTS_OPERAND_ARRAY : 0) |
                                       (shift && policy->shift_parentheses ? BUSTER_NODE_TESTS_OPERAND_SUM : 0) |
                                       (comparison && policy->chained ? BUSTER_NODE_TESTS_OPERAND_COMPARISON : 0) |
                                       (truth_parent && truths ? BUSTER_NODE_TESTS_OPERAND_TRUTH : 0));
    }
}

// The parser's tree's tests of record `r` (node `node`, operator `op`, at `token` of role `role`), as node_census_node
// reads them: a comparison's token under a bitwise operator or an expression statement's terminator, a `!`'s under a
// comparison, an assignment's under a condition's `)` or in a for statement's head. Each is asked only where the
// token's parent can be one of those.
static __attribute__((noinline)) uint64_t node_tests_tree(const NodeCensusScratch* s, CheckOp op, uint32_t token, uint8_t role)
{
    const Parser* parser = s->program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    const uint32_t token_count = parser->tokens.count;
    const uint32_t parent = check_common_parent(parser, token);
    const uint8_t parent_role = parent < token_count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint32_t grandparent = parent_role == PARSE_ROLE_TERMINATOR ? check_common_parent(parser, parent) : BUSTER_CHECK_NONE;
    const bool bitwise = parent_role == PARSE_ROLE_BITWISE_AND || parent_role == PARSE_ROLE_BITWISE_OR || parent_role == PARSE_ROLE_BITWISE_XOR;
    const bool head = parent_role == PARSE_ROLE_CLOSE_CONDITION || parent_role == PARSE_ROLE_CLOSE_FOR ||
                      (grandparent < token_count && parser->tree.roles[grandparent] == PARSE_ROLE_CLOSE_FOR);
    bool parentheses = check_common_comparison_role(role) && policy->parentheses && bitwise && check_common_bitwise_precedence(parser, token, parent);
    parentheses |= op == CHECK_OP_NOT && policy->logical_not && check_common_comparison_role(parent_role) && token < parent;
    if (op == CHECK_OP_ASSIGN && policy->parentheses && head && role == PARSE_ROLE_ASSIGN && parser->tokens.kinds[token] == PARSE_KIND_EQUAL)
    {
        const bool for_condition = check_common_for_condition(parser, token, parent);
        const uint32_t opener = parent_role == PARSE_ROLE_CLOSE_CONDITION ? check_common_opener(parser, parent) : BUSTER_CHECK_NONE;
        const bool switch_condition = opener != BUSTER_CHECK_NONE && opener > 0 && parser->tokens.kinds[opener - 1] == PARSE_KIND_SWITCH;
        parentheses |= (parent_role == PARSE_ROLE_CLOSE_CONDITION || for_condition) && !switch_condition;
    }
    const bool unused = policy->unused_comparison && check_common_comparison_role(role) && parent_role == PARSE_ROLE_TERMINATOR &&
                        check_common_statement_terminator(parser, parent);
    return (parentheses ? UINT64_C(1) << NODE_CENSUS_PARENTHESES : 0) | (unused ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0);
}

// A record sent to the census's own code: its bit in `visit`, counted once with its trigger.
static inline void node_tests_send(uint32_t r, NodeTestsTrigger trigger, uint64_t* visit, uint32_t* visits, NodeFrontTypes* counts)
{
    const bool fresh = (visit[r >> 6] >> (r & 63) & 1) == 0;
    counts->own_triggers[trigger] += fresh;
    *visits += fresh;
    visit[r >> 6] |= UINT64_C(1) << (r & 63);
}

// The tests of record `r` at node `node`, an operator that always has some or that its operands' bytes `operands` (or a
// third operand) sent here: the bits that are exact in place (a pointer's cast, a string's sum, a call's aggregates and
// callee), and into `visit` the records for the census's own code: this one, or an operand that is a shift or a product
// read as a truth.
static __attribute__((noinline)) uint64_t node_tests_operator(const NodeCensusScratch* s, uint32_t r, uint32_t node, uint8_t operands, uint64_t* visit, uint32_t* visits,
                                                              NodeFrontTypes* counts)
{
    const NodeMachine* machine = s->records;
    const NodeMachineColumns* columns = machine->columns;
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const NodeCensusPolicy* policy = &s->policy;
    const uint8_t* kept = s->own_types;
    const uint8_t* properties = s->own_properties;
    const CheckOp op = (CheckOp)columns->codes[r];
    const uint8_t own = kept[r];
    const uint32_t third_node = graph->operand2[node];
    const uint32_t o[3] = { columns->left[r], columns->right[r], third_node < graph->count ? node_tests_record(machine, third_node) : UINT32_MAX };
    // The third operand's byte with the two others': a conditional's second arm.
    const uint8_t all = (uint8_t)(operands | properties[(int32_t)o[2]]);
    uint64_t tests = 0;
    NodeTestsTrigger trigger = NODE_TESTS_TRIGGERS;
    counts->own_calls += op == CHECK_OP_CALL;
    counts->own_constants += op != CHECK_OP_CALL && (all & BUSTER_NODE_TESTS_OPERAND_CONSTANT) != 0;
    counts->own_thirds += o[2] != UINT32_MAX;
    // What sent it: a constant operand alone has the constants' tests and no other.
    const uint8_t sent = (uint8_t)(all & s->own_sends[op]);
    bool fires = false;
    if (s->own_always[op] == 0 && sent == BUSTER_NODE_TESTS_OPERAND_CONSTANT)
    {
        trigger = node_tests_constants(s, r, node, op, o, &fires);
    }
    else if ((all & BUSTER_NODE_TESTS_OPERAND_TRUTH) != 0 && (s->own_sends[op] & BUSTER_NODE_TESTS_OPERAND_TRUTH) != 0)
    {
        for (uint32_t i = 0; i < (op == CHECK_OP_CONDITIONAL ? 1u : 3u); i += 1)
        {
            if (o[i] != UINT32_MAX && (properties[o[i]] & BUSTER_NODE_TESTS_OPERAND_TRUTH) != 0)
            {
                node_tests_send(o[i], NODE_TESTS_TRIGGER_TRUTH, visit, visits, counts);
            }
        }
    }
    if (s->own_always[op] == 0 && sent == BUSTER_NODE_TESTS_OPERAND_CONSTANT)
    {
        // Done above.
    }
    else if (op == CHECK_OP_CALL)
    {
        // A call in place: an aggregate result or argument, its callee's own semantics, and its arguments' constants;
        // the census's code only where one of those may be changed by its parameter's type.
        const bool conversions = policy->constant_conversion || policy->assign_enum;
        bool aggregate = (own & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0;
        uint32_t changes = BUSTER_NODE_TESTS_CONVERSION_NONE;
        const uint32_t first_argument = graph->argument_offsets[node];
        for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
        {
            const uint32_t argument = node_tests_record(machine, graph->argument_nodes[first_argument + i]);
            const uint32_t kind = argument != UINT32_MAX ? (columns->standings[argument] >> 2) & 0x07u : 0;
            aggregate |= argument != UINT32_MAX && (kept[argument] & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0;
            changes |= conversions && kind != CHECK_CONSTANT_NONE ? node_tests_conversion(s, argument, kind, node_view_argument_converted(s, node, i)) : 0;
        }
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const uint32_t kind = o[i] != UINT32_MAX ? (columns->standings[o[i]] >> 2) & 0x07u : 0;
            changes |= conversions && kind != CHECK_CONSTANT_NONE ? node_tests_conversion(s, o[i], kind, node_view_converted(s, node, i)) : 0;
        }
        fires = (changes & BUSTER_NODE_TESTS_CONVERSION_CHANGES) != 0;
        tests |= aggregate ? UINT64_C(1) << NODE_CENSUS_AGGREGATE : 0;
        // Its callee's own semantics (node_census_callee's at reader time, where T has refused a callee whose type has
        // no prototype): a named callee whose declaration has some.
        const uint32_t callee_node = graph->operand0[node];
        bool callee = callee_node >= graph->count;
        if (!callee && graph->ops[callee_node] == CHECK_OP_VALUE)
        {
            const uint32_t token = graph->tokens[callee_node];
            const uint32_t declaration = node_census_declaration(program, token);
            callee = declaration != BUSTER_CHECK_NONE &&
                     (node_census_declaration_facts(s, declaration, program->parser->tokens.names[token], BUSTER_NODE_CENSUS_DECLARATION_CALLEE_KNOWN) & BUSTER_NODE_CENSUS_DECLARATION_CALLEE) != 0;
        }
        tests |= callee || policy->type_safety ? UINT64_C(1) << NODE_CENSUS_CALLEE : 0;
        const bool enums = policy->enums && (all & BUSTER_NODE_TESTS_OPERAND_ENUM) != 0;
        trigger = (changes & BUSTER_NODE_TESTS_CONVERSION_ASKED) != 0 ? NODE_TESTS_TRIGGER_CONVERSION : enums ? NODE_TESTS_TRIGGER_ENUM : trigger;
    }
    else
    {
        const bool comparison = node_census_comparison(op);
        const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
        const bool binary = graph->operand0[node] < graph->count && graph->operand1[node] < graph->count;
        const uint8_t first_bits = o[0] != UINT32_MAX ? kept[o[0]] : 0;
        const uint8_t first_property = properties[(int32_t)o[0]];
        const uint8_t second_property = properties[(int32_t)o[1]];
        tests |= op == CHECK_OP_ADD && policy->string_plus_int && binary && (node_census_string(program, graph->operand0[node]) || node_census_string(program, graph->operand1[node])) ?
                 UINT64_C(1) << NODE_CENSUS_STRING_PLUS_INT : 0;
        // A pointer cast to an integer narrower than it, or to an enumeration.
        if (op == CHECK_OP_CAST && policy->pointer_casts && (first_bits & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0)
        {
            const uint32_t width = node_tests_ranges(s, columns->value_types[r]) & 0x7fu;
            tests |= (width != 0 && width < 64) || (own & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
        }
        if (program->types->atomic_records && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW))
        {
            trigger = NODE_TESTS_TRIGGER_ATOMIC_MEMBER;
        }
        else if (policy->enums && (all & BUSTER_NODE_TESTS_OPERAND_ENUM) != 0)
        {
            trigger = NODE_TESTS_TRIGGER_ENUM;
        }
        else if (policy->sign_compare && comparison && binary)
        {
            trigger = NODE_TESTS_TRIGGER_SIGN_COMPARE;
        }
        else if ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference && (first_bits & BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE) != 0)
        {
            trigger = NODE_TESTS_TRIGGER_VOID_DEREFERENCE;
        }
        else if (policy->shift_parentheses && shift && ((first_property | second_property) & BUSTER_NODE_TESTS_OPERAND_SUM) != 0)
        {
            trigger = NODE_TESTS_TRIGGER_SHIFT_OPERAND;
        }
        else if (policy->chained && comparison && ((first_property | second_property) & BUSTER_NODE_TESTS_OPERAND_COMPARISON) != 0)
        {
            trigger = NODE_TESTS_TRIGGER_CHAINED;
        }
        else if (policy->self_assign && op == CHECK_OP_ASSIGN && binary)
        {
            // An assignment of a name to itself: both operands a value or a member's chain.
            const CheckOp inner0 = graph->ops[graph->operand0[node]];
            const CheckOp inner1 = graph->ops[graph->operand1[node]];
            trigger = (inner0 == CHECK_OP_VALUE || inner0 == CHECK_OP_MEMBER) && (inner1 == CHECK_OP_VALUE || inner1 == CHECK_OP_MEMBER) ? NODE_TESTS_TRIGGER_SELF_ASSIGN : trigger;
        }
        // A comma's discarded operand: a value without an effect.
        tests |= op == CHECK_OP_COMMA && node_census_discarded(s, graph->operand0[node]) ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0;
        if (trigger == NODE_TESTS_TRIGGERS && (all & BUSTER_NODE_TESTS_OPERAND_CONSTANT) != 0)
        {
            trigger = node_tests_constants(s, r, node, op, o, &fires);
        }
    }
    tests |= fires ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    if (trigger != NODE_TESTS_TRIGGERS)
    {
        node_tests_send(r, trigger, visit, visits, counts);
    }
    return tests;
}

// The expression's own tests at reader time (node_census_root_tests' there), each asked only where it can fire: a
// statement's discard where its top doesn't write, an initializer's constant where its root is one that its object's
// type may change, a return's address where the expression takes an address or has an array (`class_bits` and
// `type_bits` are the pass's, over all records). The unsequenced test reads the walk's `walked` nodes where
// `unsequenced` says.
static inline uint64_t node_tests_root(NodeCensusScratch* s, uint32_t root, NodeCensusRoot context, uint32_t count, uint32_t lowest, uint32_t highest, bool unsequenced,
                                       uint32_t walked, uint32_t class_bits, uint32_t type_bits)
{
    const NodeMachine* machine = s->records;
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    uint64_t tests = 0;
    tests |= count > BUSTER_NODE_CENSUS_MOST ? UINT64_C(1) << NODE_CENSUS_SIZE : 0;
    tests |= context == NODE_CENSUS_ROOT_OTHER || context == NODE_CENSUS_ROOT_BRACE ? UINT64_C(1) << NODE_CENSUS_CONTROL : 0;
    if (context == NODE_CENSUS_ROOT_STATEMENT)
    {
        // An expression statement's root is the reader's cast to void at its `;`: what it discards is its operand, where
        // an explicit cast's token is its own `)`. A discarded write is no value without an effect.
        const uint32_t root_token = graph->tokens[root];
        const bool wrapper = graph->ops[root] == CHECK_OP_CAST && root_token < parser->tokens.count && parser->tree.roles[root_token] != PARSE_ROLE_CLOSE_CAST &&
                             parser->tree.roles[root_token] != PARSE_ROLE_OPEN_CAST;
        const uint32_t discarded_node = wrapper ? graph->operand0[root] : root;
        const bool writes = discarded_node < graph->count && node_census_modifies(graph->ops[discarded_node]);
        tests |= !writes && node_census_discarded(s, discarded_node) ? UINT64_C(1) << NODE_CENSUS_UNUSED : 0;
    }
    tests |= policy->loops && node_census_for_clause(parser, graph->tokens[root]) ? UINT64_C(1) << NODE_CENSUS_LOOP : 0;
    tests |= policy->unmodeled ? UINT64_C(1) << NODE_CENSUS_UNMODELED : 0;
    const TypeId declared = context == NODE_CENSUS_ROOT_INITIALIZER ? s->given_declared : 0;
    if (declared != 0)
    {
        // A scalar object's initializer converts its value to the object's type in its plan: the root's record is the
        // last.
        const uint32_t kind = count != 0 ? (machine->columns->standings[count - 1] >> 2) & 0x07u : 0;
        const uint32_t changes = (policy->constant_conversion || policy->assign_enum) && kind != CHECK_CONSTANT_NONE ? node_tests_conversion(s, count - 1, kind, declared) :
                                 BUSTER_NODE_TESTS_CONVERSION_NONE;
        const bool changed = changes == BUSTER_NODE_TESTS_CONVERSION_CHANGES || (changes == BUSTER_NODE_TESTS_CONVERSION_ASKED && node_census_constant_changes(s, root, declared));
        tests |= changed ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
        tests |= (node_census_type_bits(s, declared) & BUSTER_NODE_CENSUS_TYPE_SPECIAL) != 0 ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    }
    const bool addresses = (class_bits & BUSTER_NODE_TESTS_CLASS_ADDRESS) != 0 || (type_bits & BUSTER_NODE_CENSUS_TYPE_ARRAY) != 0;
    tests |= policy->return_address && context == NODE_CENSUS_ROOT_RETURN && addresses && node_census_returns_address(s, root) ? UINT64_C(1) << NODE_CENSUS_RETURN_ADDRESS : 0;
    tests |= policy->unsequenced && unsequenced && node_census_unsequenced(s, walked, root) ? UINT64_C(1) << NODE_CENSUS_UNSEQUENCED : 0;
    const bool events = s->event_prefix != NULL && lowest <= highest && s->event_prefix[highest + 1] != s->event_prefix[lowest];
    tests |= events ? UINT64_C(1) << NODE_CENSUS_EVENT : 0;
    tests |= (machine->action_warnings & 0x01ffffffu & policy->action_bits) != 0 ? UINT64_C(1) << NODE_CENSUS_ACTION_WARNING : 0;
    s->span_low = lowest;
    s->span_high = highest;
    return tests;
}

// The tests of the machine's current expression at reader time, as bits of NodeCensusTest: node_census_expression's, by
// the own form. `root` is its root's node, `context` its root's kind; `walked` receives its records' count, and
// `counts` what was sent to the census's code.
//
// One loop reads every record once, without a branch on what it is: its operator's classes, its type's byte, its
// standing, its flags and its token. It keeps what the record is as an operand (a byte), gathers the set's tests as
// bits over all records, and lists three kinds of record: the leaves, for their flags and their declarations' bytes; the
// ones whose token's parent the tree's tests read; and the operators that have tests of their own, by their operands'
// bytes. Each list's records then have their own tests, and the few that a silence test may refuse go to the census's
// code after a walk.
static uint64_t node_tests_expression(NodeCensusScratch* s, uint32_t root, NodeCensusRoot context, uint32_t* walked, NodeFrontTypes* counts)
{
    const NodeMachine* machine = s->records;
    const NodeMachineColumns* columns = machine->columns;
    const CheckProgram* program = s->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    const uint32_t count = machine->count;
    const uint32_t first = machine->first;
    const uint32_t token_count = parser->tokens.count;
    const uint32_t* tokens = graph->tokens;
    const uint8_t* roles = parser->tree.roles;
    const uint8_t* codes = columns->codes;
    const uint8_t* standings = columns->standings;
    const TypeId* value_types = columns->value_types;
    const uint32_t* value_flags = columns->value_flags;
    const uint32_t* left = columns->left;
    const uint32_t* right = columns->right;
    const uint8_t* classes = s->own_classes;
    const uint8_t* operand_bits = s->own_operand_bits;
    const uint8_t* copies = s->own_copies;
    const uint8_t* always = s->own_always;
    const uint8_t* sends = s->own_sends;
    uint8_t* kept = s->own_types;
    uint8_t* properties = s->own_properties;
    uint16_t* leaf_list = s->own_lists;
    uint16_t* tree_list = s->own_lists + BUSTER_NODE_MACHINE_MOST;
    uint16_t* operator_list = s->own_lists + 2 * BUSTER_NODE_MACHINE_MOST;
    const uint32_t* objects = columns->objects;
    uint32_t leaves = 0;
    uint32_t trees = 0;
    uint32_t operators = 0;
    uint32_t lowest = UINT32_MAX;
    uint32_t highest = 0;
    uint32_t writes = 0;
    uint32_t last_write = UINT32_MAX;
    uint32_t class_bits = 0;
    uint32_t type_bits = 0;
    uint32_t copied_bits = 0;
    uint32_t flag_bits = 0;
    s->mixed_system = false;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t node = r + 1 == count ? root : first + r;
        const uint32_t code = codes[r] & 0x7fu;
        const uint8_t own = node_census_type_bits(s, value_types[r]);
        const uint8_t standing = standings[r];
        const uint32_t token = tokens[node];
        const bool placed = token < token_count;
        const uint8_t role = placed ? roles[token] : PARSE_ROLE_NONE;
        const uint8_t class = classes[code];
        // What it is as an operand: a constant a test may read (no object's address: the tests read an address constant
        // only for a null one), its type's four bits, its operator's three.
        const uint32_t kind = (standing >> 2) & 0x07u;
        const bool constant = kind != CHECK_CONSTANT_NONE && (kind != CHECK_CONSTANT_ADDRESS || (standing & BUSTER_NODE_MACHINE_WIDE) != 0 || objects[r] == 0);
        const uint8_t property = (uint8_t)((constant ? BUSTER_NODE_TESTS_OPERAND_CONSTANT : 0) | ((own >> 2) & BUSTER_NODE_TESTS_OPERAND_ENUM) |
                                           ((own >> 3) & (BUSTER_NODE_TESTS_OPERAND_POINTER | BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE | BUSTER_NODE_TESTS_OPERAND_ARRAY)) |
                                           operand_bits[code]);
        // Its operands' bytes: no operand reads the zero before the first record's.
        const uint8_t operands = properties[(int32_t)left[r]] | properties[(int32_t)right[r]];
        kept[r] = own;
        properties[r] = property;
        class_bits |= class;
        type_bits |= own;
        copied_bits |= own & copies[code];
        flag_bits |= value_flags[r];
        lowest = placed && token < lowest ? token : lowest;
        highest = placed && token > highest ? token : highest;
        writes += (class & BUSTER_NODE_TESTS_CLASS_WRITE) != 0;
        last_write = (class & BUSTER_NODE_TESTS_CLASS_WRITE) != 0 ? r : last_write;
        leaf_list[leaves] = (uint16_t)r;
        leaves += (class & BUSTER_NODE_TESTS_CLASS_LEAF) != 0;
        tree_list[trees] = (uint16_t)r;
        trees += role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL || (class & BUSTER_NODE_TESTS_CLASS_TREE) != 0;
        operator_list[operators] = (uint16_t)r;
        operators += (always[code] | (sends[code] & operands)) != 0;
    }
    // The set, from the bits of all records.
    uint64_t tests = 0;
    tests |= (class_bits & BUSTER_NODE_TESTS_CLASS_UNCODED) != 0 ? UINT64_C(1) << NODE_CENSUS_OPERATOR : 0;
    tests |= (type_bits & BUSTER_NODE_CENSUS_TYPE_SPECIAL) != 0 ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= (flag_bits & BUSTER_CHECK_BIT_FIELD) != 0 ? UINT64_C(1) << NODE_CENSUS_BIT_FIELD : 0;
    tests |= (copied_bits & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0 ? UINT64_C(1) << NODE_CENSUS_AGGREGATE : 0;
    tests |= (class_bits & BUSTER_NODE_TESTS_CLASS_ADDRESS) != 0 && policy->packed_member && s->packed_unit ? UINT64_C(1) << NODE_CENSUS_PACKED_MEMBER : 0;
    // A leaf's flags and its name's declaration's byte, by T's columns (node_census_leaf's tests, of which a register
    // object's is its flags' and a compound literal's is no leaf's here): the census's code for a declaration whose
    // attributes no leaf has asked yet. A register object's leaf never asks them, there or here.
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t* declarations = input->declarations;
    const uint8_t* facts = s->declaration_facts;
    for (uint32_t i = 0; i < leaves; i += 1)
    {
        const uint32_t r = leaf_list[i];
        const uint32_t node = r + 1 == count ? root : first + r;
        const uint32_t token = tokens[node];
        const bool named = token < token_count && kinds[token] == PARSE_KIND_IDENTIFIER && declarations != NULL && token < input->token_count;
        const uint32_t declaration = named ? declarations[token] : BUSTER_CHECK_NONE;
        const uint8_t known = declaration < token_count ? facts[declaration] : 0;
        if ((value_flags[r] & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) != 0)
        {
            // A register object's: its flags say it, whatever its declaration's attributes are.
            tests |= UINT64_C(1) << NODE_CENSUS_DECLARATION;
        }
        else if (declaration != BUSTER_CHECK_NONE && (known & BUSTER_NODE_CENSUS_DECLARATION_FLAGGED_KNOWN) == 0)
        {
            tests |= node_census_leaf(s, node, token, value_flags[r]);
        }
        else
        {
            tests |= (known & BUSTER_NODE_CENSUS_DECLARATION_FLAGGED) != 0 ? UINT64_C(1) << NODE_CENSUS_DECLARATION : 0;
        }
    }
    for (uint32_t i = 0; i < trees; i += 1)
    {
        const uint32_t r = tree_list[i];
        const uint32_t node = r + 1 == count ? root : first + r;
        const uint32_t token = tokens[node];
        tests |= node_tests_tree(s, (CheckOp)codes[r], token, token < token_count ? roles[token] : PARSE_ROLE_NONE);
    }
    uint64_t visit[BUSTER_NODE_MACHINE_MOST / 64];
    uint32_t visits = 0;
    if (operators != 0)
    {
        for (uint32_t word = 0; word < (count + 63) / 64; word += 1)
        {
            visit[word] = 0;
        }
        for (uint32_t i = 0; i < operators; i += 1)
        {
            const uint32_t r = operator_list[i];
            const uint8_t operands = properties[(int32_t)left[r]] | properties[(int32_t)right[r]];
            tests |= node_tests_operator(s, r, r + 1 == count ? root : first + r, operands, visit, &visits, counts);
        }
    }
    // The unsequenced test needs two writes, or one below the expression's top: a lone write that is the root, or the
    // root's operand, has every other node but the root in its subtree.
    const uint32_t top = count >= 2 ? node_tests_record(machine, graph->operand0[root]) : UINT32_MAX;
    const bool sequenced = writes == 0 || (writes == 1 && (last_write + 1 == count || last_write == top));
    const bool unsequenced = policy->unsequenced && !sequenced;
    counts->own_triggers[NODE_TESTS_TRIGGER_UNSEQUENCED] += unsequenced;
    uint32_t walk_count = count;
    if (visits != 0 || unsequenced)
    {
        // The census's code reads a node's parent for a shift's or a product's truth and for a subscript's address, and
        // the unsequenced test reads the walk's list: the walk only for those.
        bool parents = unsequenced;
        for (uint32_t word = 0; visits != 0 && word < (count + 63) / 64; word += 1)
        {
            for (uint64_t pending = visit[word]; pending != 0; pending &= pending - 1)
            {
                const CheckOp sent = (CheckOp)codes[word * 64 + (uint32_t)__builtin_ctzll(pending)];
                parents |= sent == CHECK_OP_SHIFT_LEFT || sent == CHECK_OP_MULTIPLY || sent == CHECK_OP_SUBSCRIPT;
            }
        }
        if (parents)
        {
            uint32_t walk_lowest = UINT32_MAX;
            uint32_t walk_highest = 0;
            walk_count = node_census_walk(s, root, &walk_lowest, &walk_highest);
            counts->own_walks += 1;
            counts->own_count_differences += walk_count != count;
        }
        for (uint32_t word = 0; visits != 0 && word < (count + 63) / 64; word += 1)
        {
            uint64_t pending = visit[word];
            while (pending != 0)
            {
                const uint32_t r = word * 64 + (uint32_t)__builtin_ctzll(pending);
                pending &= pending - 1;
                tests |= node_census_node(s, r + 1 == count ? root : first + r, root, context);
            }
        }
    }
    counts->own_expressions += 1;
    counts->own_records += count;
    counts->own_leaves += leaves;
    counts->own_trees += trees;
    counts->own_operators += operators;
    counts->own_visited += visits != 0;
    counts->own_visits += visits;
    tests |= node_tests_root(s, root, context, count, lowest, highest, unsequenced, walk_count, class_bits, type_bits);
    *walked = count;
    return tests;
}

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

// The tests where they sit (docs/node-machine.md): the tests above at reader time, for the machine there
// (c_check_node_machine_reader.c, PPOptions.node_reader's BUSTER_NODE_READER_TESTS), on each expression T has just taken
// and before C and X run on it. The test code is the census's own, with a view that reads T's record where the census
// reads the stream's node. What the census reads besides, and what stands for it before the pipeline:
//
// - The program: its input, parser and types are the reader's, and its objects' columns the machine's there
//   (NodeMachine.object_symbols: a static declaration stands for a symbol).
// - A root's kind, the control's that pushed it: the front's root's operator (a discard's cast, a condition, a return's
//   conversion, a switch's promotion, an initialization, which the front makes for a scalar object's unbraced
//   initializer alone).
// - An initializer's object's type: its declaration's (the reader's declared_types).
// - The enabled warnings: the options' and the diagnostic pragmas', which the reader has whole before its coded walk.
// - A body's diagnostic and floating-point pragmas: the same records, by the outermost braces around each.
// - The warning events at the expression's tokens: the reader's own so far, a chunk at a time. The events the later
//   passes make aren't there yet: each has a test above that stands for it, or the shadow's comparison finds it.
// - The actions' warnings a node: T's for the whole expression (NodeMachine.action_warnings).
// - A packed member's address: its witness is a later pass's of the reader (c_check_packed_input.c), so in a unit with a
//   packed record or member any address taken stands for one (NodeCensusScratch.packed_unit).
//
// The lowering's own facts have nothing at reader time: a body that tracks its stack, a node popped twice.
typedef struct NodeCensusReader NodeCensusReader;
struct NodeCensusReader
{
    NodeCensusScratch scratch;
    // What stands for the unit's program in the tests: the reader's input, parser and types, and the machine's objects.
    CheckProgram program;
    NodeFrontTypes* types;
    // A bit a token: a kept warning event of the reader's at it; and the events taken in so far.
    uint64_t* event_bits;
    uint32_t events_seen;
    // The outermost brace pairs that hold a diagnostic or floating-point pragma (node_census_region's bodies), each its
    // opening's and its closing's tokens.
    uint32_t* pragma_openings;
    uint32_t* pragma_closings;
    uint32_t pragma_count;
    // A bit a token: inside a statement expression (CheckInput.regions, the reader's): a root there is a nested control's.
    uint64_t* nested_bits;
    // BUSTER_NODE_READER_OWN and BUSTER_NODE_READER_BOTH of the unit's options: the tests by the machine's own form
    // (node_tests_expression), and the census's code run too and compared with it.
    uint8_t mode;
};

// The outermost brace pair around token `token` into `opening` and `closing`: whether there is one. The parser's tree
// gives an opening brace its closing one as its parent.
static bool node_census_reader_braces(const Parser* parser, uint32_t token, uint32_t* opening, uint32_t* closing)
{
    const uint32_t count = parser->tokens.count;
    bool found = false;
    for (uint32_t t = 0; t < count && t < token && !found;)
    {
        const uint32_t parent = parser->tokens.kinds[t] == PARSE_KIND_L_BRACE ? parser->tree.parents[t] : count;
        const bool pair = parent < count && parent > t;
        found = pair && token < parent;
        *opening = found ? t : *opening;
        *closing = found ? parent : *closing;
        t = pair ? parent + 1 : t + 1;
    }
    return found;
}

static NodeCensusReader* node_census_reader_begin(const CheckInputReader* reader, const NodeMachine* machine, NodeFrontTypes* types, uint32_t node_capacity)
{
    Arena* arena = reader->scratch;
    const CheckInput* input = reader->output;
    const uint32_t tokens = reader->parser->tokens.count;
    NodeCensusReader* tests = arena_push_array(arena, 1, sizeof(NodeCensusReader), _Alignof(NodeCensusReader));
    NodeCensusPolicy* policy = arena_push_array(reader->arena, 1, sizeof(NodeCensusPolicy), _Alignof(NodeCensusPolicy));
    const uint32_t pragmas = (input->diagnostic_pragmas != NULL ? input->diagnostic_pragmas->count : 0) + (input->fp_pragmas != NULL ? input->fp_pragma_count : 0);
    bool room = tests != NULL && policy != NULL;
    if (room)
    {
        memset(tests, 0, sizeof(*tests));
        NodeCensusScratch* s = &tests->scratch;
        tests->types = types;
        tests->program.input = input;
        tests->program.parser = reader->parser;
        tests->program.types = reader->types;
        tests->program.object_symbols = (SymbolId*)machine->object_symbols;
        tests->program.object_count = machine->object_count;
        s->program = &tests->program;
        s->pp = reader->pp;
        s->records = machine;
        s->policy = node_census_policy(reader->pp, input);
        *policy = s->policy;
        types->policy = policy;
        uint32_t** node_columns[] = { &s->marks, &s->parents, &s->depths, &s->list, &s->stack };
        for (uint32_t c = 0; room && c < BUSTER_ARRAY_LENGTH(node_columns); c += 1)
        {
            *node_columns[c] = arena_push_array(arena, (uint64_t)node_capacity + 1, sizeof(uint32_t), _Alignof(uint32_t));
            room = *node_columns[c] != NULL;
        }
        tests->event_bits = arena_push_array(arena, (uint64_t)tokens / 64 + 1, sizeof(uint64_t), _Alignof(uint64_t));
        tests->pragma_openings = arena_push_array(arena, (uint64_t)pragmas + 1, sizeof(uint32_t), _Alignof(uint32_t));
        tests->pragma_closings = arena_push_array(arena, (uint64_t)pragmas + 1, sizeof(uint32_t), _Alignof(uint32_t));
        tests->nested_bits = arena_push_array(arena, (uint64_t)tokens / 64 + 1, sizeof(uint64_t), _Alignof(uint64_t));
        // The types' bytes have room for the types T's derivations and the walk make later, as the machine's words do.
        s->type_bits_count = machine->type_capacity;
        s->type_bits = arena_push_array(arena, (uint64_t)s->type_bits_count + 1, 1, 1);
        s->declaration_facts = arena_push_array(arena, (uint64_t)tokens + 1, 1, 1);
        s->type_ranges = arena_push_array(arena, (uint64_t)s->type_bits_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
        s->own_types = arena_push_array(arena, (uint64_t)BUSTER_NODE_MACHINE_MOST + 64, 1, 64);
        uint8_t* properties = arena_push_array(arena, (uint64_t)BUSTER_NODE_MACHINE_MOST + 128, 1, 64);
        s->own_properties = properties != NULL ? properties + 64 : NULL;
        s->own_lists = arena_push_array(arena, 3 * (uint64_t)BUSTER_NODE_MACHINE_MOST + 64, sizeof(uint16_t), 64);
        tests->mode = reader->pp->options.node_reader & (BUSTER_NODE_READER_OWN | BUSTER_NODE_READER_BOTH);
        room = room && s->type_bits != NULL && s->declaration_facts != NULL && tests->event_bits != NULL && tests->pragma_openings != NULL && tests->pragma_closings != NULL &&
               tests->nested_bits != NULL && s->type_ranges != NULL && s->own_types != NULL && s->own_properties != NULL && s->own_lists != NULL;
    }
    if (room)
    {
        NodeCensusScratch* s = &tests->scratch;
        memset(s->marks, 0, ((uint64_t)node_capacity + 1) * sizeof(uint32_t));
        memset(tests->event_bits, 0, ((uint64_t)tokens / 64 + 1) * sizeof(uint64_t));
        const TypeTable* table = reader->types;
        for (uint32_t tag = 0; tag < table->tags.count && !s->packed_unit; tag += 1)
        {
            s->packed_unit = table->tags.packed[tag] != 0;
        }
        for (uint32_t member = 0; member < table->members.count && !s->packed_unit; member += 1)
        {
            s->packed_unit = (table->members.flags[member] & BUSTER_TYPE_MEMBER_PACKED) != 0;
        }
        memset(tests->nested_bits, 0, ((uint64_t)tokens / 64 + 1) * sizeof(uint64_t));
        memset(s->type_bits, 0, (uint64_t)s->type_bits_count + 1);
        memset(s->declaration_facts, 0, (uint64_t)tokens + 1);
        memset(s->type_ranges, 0, ((uint64_t)s->type_bits_count + 1) * sizeof(uint32_t));
        memset(s->own_properties - 64, 0, 64);
        node_tests_tables(s, reader->types);
        for (uint32_t r = 0; r < input->region_count; r += 1)
        {
            for (uint32_t t = input->regions[r].opening; t <= input->regions[r].closing && t < tokens; t += 1)
            {
                tests->nested_bits[t >> 6] |= UINT64_C(1) << (t & 63);
            }
        }
        for (uint32_t i = 0; i < pragmas; i += 1)
        {
            const uint32_t diagnostics = input->diagnostic_pragmas != NULL ? input->diagnostic_pragmas->count : 0;
            const uint32_t token = i < diagnostics ? input->diagnostic_pragmas->records[i].token : input->fp_pragmas[i - diagnostics].token;
            uint32_t opening = 0;
            uint32_t closing = 0;
            if (node_census_reader_braces(reader->parser, token, &opening, &closing))
            {
                tests->pragma_openings[tests->pragma_count] = opening;
                tests->pragma_closings[tests->pragma_count] = closing;
                tests->pragma_count += 1;
            }
        }
    }
    return room ? tests : NULL;
}

static void node_census_reader_chunk(NodeCensusReader* tests, const CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const uint32_t tokens = reader->parser->tokens.count;
    for (; tests->events_seen < reader->warning_events.count; tests->events_seen += 1)
    {
        const CheckWarningEvent* event = reader->warning_events.events + tests->events_seen;
        if (event->token < tokens && node_census_event_kept(reader->pp, input, event))
        {
            tests->event_bits[event->token >> 6] |= UINT64_C(1) << (event->token & 63);
        }
    }
}

static uint64_t node_census_reader_expression(NodeCensusReader* tests, uint32_t root, CheckOp op, uint32_t token, uint32_t declaration, TypeId declared, uint8_t* kind)
{
    NodeCensusScratch* s = &tests->scratch;
    const CheckProgram* program = s->program;
    const Parser* parser = program->parser;
    const CheckGraph* graph = &program->input->graph;
    // The root's kind, node_census_root's by the control that pushed it. An initializer's whole value is a scalar
    // object's without a brace before it, as there; and a root inside a statement expression is a nested control's.
    const bool nested = token < parser->tokens.count && (tests->nested_bits[token >> 6] >> (token & 63) & 1) != 0;
    NodeCensusRoot context = nested ? NODE_CENSUS_ROOT_OTHER : op == CHECK_OP_CAST ? NODE_CENSUS_ROOT_STATEMENT : op == CHECK_OP_CONDITION ? NODE_CENSUS_ROOT_CONDITION :
                             op == CHECK_OP_CONVERT ? NODE_CENSUS_ROOT_RETURN : op == CHECK_OP_POSITIVE ? NODE_CENSUS_ROOT_SWITCH : NODE_CENSUS_ROOT_OTHER;
    if (op == CHECK_OP_INITIALIZE && !nested)
    {
        // A scalar's type: no struct, union or array, by its byte.
        const uint8_t declared_bits = declared != 0 ? node_census_type_bits(s, declared) : 0;
        const bool scalar = declared != 0 && (declared_bits & (BUSTER_NODE_CENSUS_TYPE_AGGREGATE | BUSTER_NODE_CENSUS_TYPE_ARRAY)) == 0;
        const uint32_t value_token = root < graph->count ? graph->tokens[root] : 0;
        bool brace = declaration >= parser->tokens.count;
        for (uint32_t t = declaration + 1; !brace && t < value_token && t < parser->tokens.count; t += 1)
        {
            brace = parser->tokens.kinds[t] == PARSE_KIND_L_BRACE;
        }
        context = scalar && !brace ? NODE_CENSUS_ROOT_INITIALIZER : NODE_CENSUS_ROOT_BRACE;
    }
    s->given_declared = context == NODE_CENSUS_ROOT_INITIALIZER ? declared : 0;
    uint32_t walked = 0;
    const bool own = (tests->mode & BUSTER_NODE_READER_OWN) != 0;
    uint64_t bits = own ? node_tests_expression(s, root, context, &walked, tests->types) : node_census_expression(s, root, 0, context, &walked);
    if (own && (tests->mode & BUSTER_NODE_READER_BOTH) != 0)
    {
        // The census's code on the view, the own form's oracle: the same bits and the same span of tokens.
        const uint32_t low = s->span_low;
        const uint32_t high = s->span_high;
        const uint32_t missed = s->missed;
        uint32_t census_walked = 0;
        const uint64_t census = node_census_expression(s, root, 0, context, &census_walked);
        const bool same = census == bits && low == s->span_low && high == s->span_high && census_walked == walked;
        tests->types->own_differences += !same;
        if (!same && tests->types->own_listed < node_census_list)
        {
            tests->types->own_listed += 1;
            printf("node-tests: difference line=%u root=%s nodes=%u/%u span=%u..%u/%u..%u own=%#" PRIx64 " census=%#" PRIx64 "\n",
                   node_census_line(s->pp, parser, root < graph->count ? graph->tokens[root] : 0), node_census_root_names[context], walked, census_walked, low, high, s->span_low, s->span_high, bits, census);
        }
        s->missed = missed;
    }
    s->given_declared = 0;
    // A body with a pragma (node_census_region's), by the root's token.
    const uint32_t root_token = root < graph->count ? graph->tokens[root] : 0;
    bool region = false;
    for (uint32_t i = 0; i < tests->pragma_count && !region; i += 1)
    {
        region = root_token > tests->pragma_openings[i] && root_token < tests->pragma_closings[i];
    }
    bits |= region ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
    // The reader's own warning events so far at the expression's tokens.
    bool events = false;
    for (uint32_t word = s->span_low >> 6; s->span_low <= s->span_high && word <= s->span_high >> 6 && !events; word += 1)
    {
        const uint64_t low = word == s->span_low >> 6 ? ~UINT64_C(0) << (s->span_low & 63) : ~UINT64_C(0);
        const uint64_t high = word == s->span_high >> 6 ? ~UINT64_C(0) >> (63 - (s->span_high & 63)) : ~UINT64_C(0);
        events = (tests->event_bits[word] & low & high) != 0;
    }
    bits |= events ? UINT64_C(1) << NODE_CENSUS_EVENT : 0;
    // A node the records don't hold refuses the expression (none does: T takes one range of nodes, whole).
    bits |= s->missed != 0 ? UINT64_C(1) << NODE_CENSUS_SIZE : 0;
    tests->types->tested_missed += s->missed;
    s->missed = 0;
    *kind = (uint8_t)context;
    return bits;
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
    // Its body's stamp (NodeCensusScratch.body_stamp), its root pop's flags and its root's kind (NodeCensusRoot): the
    // interleaved sets' chunks by bodies and the census's walk again (--interleave).
    uint32_t body;
    uint8_t context;
    uint8_t flags;
    uint8_t kind;
};

// The sets timed: T alone, T and C, and all three; with --fused, T and then C and X as one sweep (node_machine_lower);
// and with --interleave N, the census's own walk of each expression (node_census_expression: other code over the same
// nodes, as the reader's is where a compile runs the machine) alone, then before the three sweeps, and before T and the
// one sweep with --fused. The interleaved sets take the expressions by chunks of N (a body's for 0): the walk of each of a
// chunk's expressions, then the sweeps of each, so that a set less the walk alone is what the sweeps cost where other
// code runs between two of the machine's runs, and N is how many expressions a run takes.
#define BUSTER_NODE_SHADOW_SWEEPS 7u
#define BUSTER_NODE_SHADOW_SET_FUSED 3u
#define BUSTER_NODE_SHADOW_SET_WALK 4u
#define BUSTER_NODE_SHADOW_SET_WALK_SWEEPS 5u
#define BUSTER_NODE_SHADOW_SET_WALK_FUSED 6u

// One expression's result, for the shadow's tests (NodeShadow.records_out): its root's line and the machine's refusal.
typedef struct NodeShadowRecord NodeShadowRecord;
struct NodeShadowRecord
{
    uint32_t line;
    NodeMachineRefusal refusal;
    // A taken expression's, where the front ran (`front`): whether the front made its every record.
    bool fronted;
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
    // The fused sweep's census (node_shadow_fusion): the taken expressions' records evaluated and not; of these, those
    // whose consumer isn't evaluated either; the evaluated records whose context their consumer's own context gives (a
    // SELECT's operand, a comma's second operand), and of those, the ones not evaluated as plain values; the evaluated
    // calls, and those below another; the records followed by a segment or an update's first row; the update plans; and
    // the expressions with anything such a sweep settles after the records' visits.
    uint64_t fusion_evaluated;
    uint64_t fusion_unevaluated;
    uint64_t fusion_wasted;
    uint64_t fusion_inherited;
    uint64_t fusion_inherited_other;
    uint64_t fusion_calls;
    uint64_t fusion_nested_calls;
    uint64_t fusion_hooked;
    uint64_t fusion_updates;
    uint64_t fusion_settled_expressions;
    // --fused: C and X as one sweep (c_check_node_machine_lower.c), run after the three sweeps on every expression T
    // typed: those whose refusal, tape, labels or calls differ from C's and X's, which must be none.
    bool fused;
    uint64_t fused_expressions;
    uint64_t fused_differences;
    // --front, --front-first: the machine's front in the reader (c_check_node_machine_front.c, PPOptions.node_front 1 and
    // 2), its records compared there with the walk's nodes: the files' reports summed (NodeFrontShadow's), the walk's own
    // parts' ticks beside the front's, and the taken expressions whose every node agreed, with their evaluated nodes.
    uint8_t front;
    uint64_t front_ticks;
    uint64_t front_tokens;
    uint64_t front_records;
    uint64_t front_checked;
    uint64_t front_differences;
    uint64_t front_resets;
    uint64_t front_waiting;
    // The units the front ran in, and those read without it though it was asked for: it runs beside the reader's coded
    // walk, the vector path's, over a whole unit, so a unit the scalar walk read (a build without the vector paths, the
    // parser's scalar option) or that was read by parts has none.
    uint64_t front_units;
    uint64_t front_unread;
    uint64_t front_acting[BUSTER_CHECK_EXPRESSION_CODES];
    uint64_t front_left[BUSTER_CHECK_EXPRESSION_CODES];
    uint64_t front_left_queries;
    uint64_t walk_ticks[BUSTER_CHECK_EXPRESSION_PARTS];
    uint64_t front_expressions;
    uint64_t front_nodes;
    // The front's records' operands by their distance back in the postorder (NodeFrontShadow's), and of the taken
    // expressions the front made, those with an operand further back than BUSTER_NODE_FRONT_NEAR, with their nodes.
    uint64_t front_operands;
    uint64_t front_operands_near;
    uint64_t front_operands_byte;
    uint64_t front_operands_far;
    uint64_t front_root_operands;
    uint64_t front_root_operands_past;
    uint64_t front_arguments;
    uint64_t front_arguments_past;
    // What #2850's byte columns cap (NodeFrontShadow's), and the walk's preparation by its parts where the front ran.
    uint64_t front_counts_past;
    uint64_t front_places_past;
    uint64_t front_compact_ticks;
    uint64_t front_identifier_ticks;
    uint64_t front_literal_ticks;
    uint64_t front_past_expressions;
    uint64_t front_past_nodes;
    // --list N: the taken expressions the front didn't make, listed up to N.
    uint32_t front_missed_listed;
    uint32_t front_listed;
    // --front-types: T run at reader time on the front's expressions (c_check_node_machine_reader.c). The files' reports
    // summed (NodeFrontTypes'): the expressions it ran on, their records, its refusals, its ticks, its state's making and
    // the types its derivations added. And after the pipeline: of the expressions it ran on, those the lowering popped
    // (a selected body's roots) and those of the census's taken set, each compared with the shadow's own T by its
    // records' digest (node_machine_digest): the ones whose range, refusal or records differ, which must be none; and
    // the taken expressions it didn't run on: those the front left (`typed_missing`), and those whose nodes the front
    // made without a root for them (`typed_unrun`: an initializer beside a variable-length array's bound). `typed_unpopped`:
    // the expressions it ran on that the lowering never popped, by T's refusal, and of them `typed_unpopped_inside` those
    // in a body the lowering lowered (a constant's context, a condition the program folds), the others being in a body
    // the selection dropped or in none: what a machine in the reader types for nothing. `typed_discards`: the front's
    // roots that are a declaration's discard, which T isn't run on (NodeFrontTypes.discards).
    uint64_t typed_expressions;
    uint64_t typed_records;
    uint64_t typed_typed;
    uint64_t typed_refused[NODE_MACHINE_REFUSALS];
    uint64_t typed_ticks;
    uint64_t typed_setup_ticks;
    uint64_t typed_late_types;
    uint64_t typed_skipped;
    uint64_t typed_popped;
    uint64_t typed_unpopped[NODE_MACHINE_REFUSALS];
    uint64_t typed_unpopped_inside[NODE_MACHINE_REFUSALS];
    uint64_t typed_discards;
    uint64_t typed_compared;
    uint64_t typed_compared_records;
    uint64_t typed_differences;
    uint64_t typed_range_differences;
    uint64_t typed_refusal_differences;
    uint64_t typed_missing;
    uint64_t typed_unrun;
    // What reader time made in place of the program's columns, against them: the objects whose last static declaration,
    // returns_twice or asm label differs (of `typed_objects` with a static declaration), and the calls whose "a library
    // builtin's semantics of its own?" differs (of `typed_library_calls` T asked it of there). Both must be none.
    uint64_t typed_objects;
    uint64_t typed_object_differences;
    uint64_t typed_library_calls;
    uint64_t typed_library_differences;
    uint32_t typed_listed;
    // --front-sweeps: C and X run at reader time after T, on the expressions it took there. The files' reports summed
    // (NodeFrontTypes'): the expressions they ran on, by their refusal, their entries, and the tapes the store had no room
    // for. And after the pipeline, for each expression of the census's taken set that T took both times: reader time's
    // root (its conversion's target and its object against the lowering's pop's), its refusal and, where both took the
    // expression, its tape, patched with the program's symbols, and its labels, calls and temporaries by their digest,
    // against the shadow's own C's and X's. `swept_differences` counts the expressions that differ in any, which must be
    // none. Of the entries compared, `swept_references` are the globals' the patch changed and `swept_call_targets` the
    // compared expressions' direct calls, whose rows' targets a splice patches too.
    uint64_t swept_expressions;
    uint64_t swept_refused[NODE_MACHINE_REFUSALS];
    uint64_t swept_entries;
    uint64_t swept_unkept;
    uint64_t swept_compared;
    uint64_t swept_compared_entries;
    uint64_t swept_references;
    uint64_t swept_call_targets;
    uint64_t swept_differences;
    uint64_t swept_root_differences;
    uint64_t swept_refusal_differences;
    uint64_t swept_tape_differences;
    uint64_t swept_side_differences;
    uint32_t swept_listed;
    // --front-tests: the census's tests at reader time after T (PPOptions.node_reader, `reader` its bits). The files'
    // reports summed (NodeFrontTypes'): the expressions they ran on and refused, by test, and the nodes they asked of
    // T's records that weren't there. And after the pipeline, the census's own verdict on each such root against reader
    // time's, test by test: the roots the lowering popped (`tested_compared`), and those it never did
    // (`tested_compared_unpopped`), which the census reads again from the stream for it. `tested_unsound` counts the
    // expressions reader time takes (no test refuses, and C and X take it) that a test of the census's refuses, which
    // must be none; `tested_lowering` those it takes that the census refuses for the lowering's own facts alone (a body
    // that tracks its stack, a node popped twice), which a splice reads where it pops the root; `tested_differences`
    // those where a test differs otherwise than reader time may (its actions' warnings are at least the graph run's, a
    // packed unit's addresses at least the witnesses', and its events the reader's own alone), which must be none too;
    // `tested_kind_differences` the popped roots whose kind the front's root gives otherwise than the control.
    // By test: the expressions reader time alone refuses by it, and the census alone.
    uint8_t reader;
    uint64_t tested_expressions;
    uint64_t tested_refused;
    uint64_t tested_any[NODE_CENSUS_TESTS];
    uint64_t tested_first[NODE_CENSUS_TESTS];
    uint64_t tested_missed;
    uint64_t tested_compared;
    uint64_t tested_compared_unpopped;
    uint64_t tested_taken;
    uint64_t tested_taken_nodes;
    uint64_t tested_unsound;
    uint64_t tested_lowering;
    uint64_t tested_differences;
    uint64_t tested_kind_differences;
    uint64_t tested_policy_differences;
    uint64_t tested_reader_alone[NODE_CENSUS_TESTS];
    uint64_t tested_census_alone[NODE_CENSUS_TESTS];
    uint64_t tested_census_refused;
    uint64_t tested_census_taken_refused;
    uint64_t tested_census_taken_refused_nodes;
    uint32_t tested_listed;
    // Optional: each compared root's result, up to `tested_out_capacity` of them (c_check_node_census_test.c's).
    struct NodeShadowTested* tested_out;
    uint32_t tested_out_count;
    uint32_t tested_out_capacity;
    // --verify with T at reader time: the nodes a row or a rule typed there whose value isn't the action's, and those
    // whose action gives a warning; and the latter after the pipeline, of the shadow's own T.
    uint64_t reader_twin_differences;
    uint64_t reader_twin_warnings;
    uint64_t twin_warnings;
    // --front-own-tests: the tests at reader time in the machine's own form (NodeFrontTypes' counts, summed).
    uint64_t own_expressions;
    uint64_t own_records;
    uint64_t own_visited;
    uint64_t own_visits;
    uint64_t own_walks;
    uint64_t own_triggers[NODE_TESTS_TRIGGERS];
    uint64_t own_leaves;
    uint64_t own_trees;
    uint64_t own_operators;
    uint64_t own_calls;
    uint64_t own_constants;
    uint64_t own_thirds;
    uint64_t own_differences;
    uint64_t own_count_differences;
    // --interleave N: the interleaved sets' chunk (0: a body's expressions), the current file's scratch for the census's
    // walk in the timing passes, the walks' tests (so that the walks aren't dead code), and the sweeps' own TSC ticks in
    // an interleaved set: the last pass's, and each file's least over the passes, summed.
    bool interleave;
    uint32_t chunk;
    NodeCensusScratch* scratch;
    uint64_t walk_tests;
    uint64_t inner_ticks;
    uint64_t inner[BUSTER_NODE_SHADOW_SWEEPS];
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
    // --facts (#2850's step 1, c_check_node_machine_facts.c): the machine-taken expressions' leaves and lookups from the
    // fact words, compared with T's records and timed. The files' counts summed (NodeFacts'), the records the kernel's
    // rules made (`facts_taken_leaves`, `facts_taken_lookups`), those that differ from T's, the words made, and the
    // kernel's time over each file's windows: its least cycles, instructions and TSC ticks over the passes, the ticks of
    // its two loops, and those of each window's second run of two in a row (`facts_warm`).
    // With --facts-operators (#2850's step 2's first piece), the operators that are no lookup too, and a literal's leaf
    // from a line: each operator's records, those the kernel made (`facts_operator_taken`) and those a class's loop
    // made (`facts_operator_short`), and the literals' lines.
    bool facts;
    bool facts_packed;
    bool facts_operators;
    // --facts-reads (#2850's step 2's second piece, c_check_node_machine_reads.c): what a later rule of T, C and X read of
    // each record's wide fields, and where those come from; its counts over every file.
    bool facts_reads;
    NodeReadsCounts reads;
    uint64_t reads_listed;
    uint32_t facts_window;
    uint32_t facts_mode;
    uint64_t facts_operator_records[CHECK_OP_COUNT];
    uint64_t facts_operator_taken[CHECK_OP_COUNT];
    uint64_t facts_operator_short[CHECK_OP_COUNT];
    uint64_t facts_literal_lines;
    uint64_t facts_taken_lines;
    uint64_t facts_pairs;
    uint64_t facts_records;
    uint64_t facts_literals;
    uint64_t facts_identifiers;
    uint64_t facts_named[4];
    uint64_t facts_left_identifiers;
    uint64_t facts_lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t facts_left_lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t facts_given[CHECK_OP_COUNT];
    uint64_t facts_taken_leaves;
    uint64_t facts_taken_lookups;
    uint64_t facts_differences;
    uint64_t facts_decoy_differences;
    uint64_t facts_list_differences;
    uint64_t facts_listed;
    uint64_t facts_short_lookups;
    uint64_t facts_conversions;
    uint64_t facts_conversions_derived;
    uint64_t facts_discards;
    uint64_t facts_discards_derived;
    uint64_t facts_loads;
    uint64_t facts_failures;
    uint64_t facts_rows;
    uint64_t facts_lines;
    uint64_t facts_line_versions;
    uint64_t facts_members;
    uint64_t facts_windows;
    uint64_t facts_widest;
    uint64_t facts_row_ticks;
    uint64_t facts_line_ticks;
    uint64_t facts_cycles;
    uint64_t facts_instructions;
    uint64_t facts_given_cycles;
    uint64_t facts_given_instructions;
    uint64_t facts_given_ticks;
    uint64_t facts_mispredicts;
    uint64_t facts_given_mispredicts;
    uint64_t facts_ticks;
    uint64_t facts_loop_ticks[2];
    uint64_t facts_warm[2];
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
            // A named result's address (#1375, #2882): the body builds that local where its hidden pointer points, and
            // check_lower_named_rewrite made each LOCAL_ADDRESS of it a LOCAL of the hidden pointer after the tasks wrote
            // them. The tape has the address, as the tasks had: the two are the same entry where the tape's object is
            // the named local's, the body's one object there.
            const bool named = s->named_local != BUSTER_CHECK_NONE && kind == BUSTER_TYPED_LOCAL && function->values[e] == s->hidden_local &&
                               t < machine->entry_count && machine->kinds[t] == BUSTER_TYPED_LOCAL_ADDRESS;
            same = same && t < machine->entry_count && machine->classes[t] == function->classes[e] &&
                   (named                        ? node_shadow_same_local(s, machine->tape[t], s->named_local) :
                    machine->kinds[t] != kind    ? false :
                    node_shadow_local_kind(kind) ? node_shadow_same_local(s, machine->tape[t], function->values[e]) :
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

// The fused sweep's census (docs/node-machine.md, "A fused sweep: what it would settle later"): for a taken expression,
// what a T that classified each record's operands, as C does, at the record's own visit, up the postorder, would leave to
// a later step. A record's consumer decides its context and target, but where they are the consumer's own context (a
// SELECT's operand and a comma's second operand, check_lower_template_classify's), which its consumer's consumer gives
// later; a record its consumer doesn't evaluate costs that visit nothing, but one whose consumer isn't evaluated either
// was classified for nothing; and a call's depth among the evaluated calls, which numbers its row, is its ancestors'.
static void node_shadow_fusion(NodeShadow* shadow, NodeCensusScratch* s, const NodeMachine* machine)
{
    const uint32_t count = machine->count;
    uint32_t* consumers = s->consumers;
    for (uint32_t r = 0; r < count; r += 1)
    {
        consumers[r] = UINT32_MAX;
    }
    for (uint32_t r = 0; r < count; r += 1)
    {
        const CheckOp op = node_machine_record_op(machine, r);
        const uint32_t operands[3] = {
            node_machine_record_left(machine, r),
            node_machine_record_right(machine, r),
            op == CHECK_OP_CONDITIONAL ? node_machine_record_third(machine, r) : UINT32_MAX,
        };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            if (operands[k] < r)
            {
                consumers[operands[k]] = r;
            }
        }
        for (uint32_t i = 0; op == CHECK_OP_CALL && i < node_machine_record_argument_count(machine, r); i += 1)
        {
            const uint32_t argument = node_machine_record_argument(machine, r, i);
            if (argument < r)
            {
                consumers[argument] = r;
            }
        }
    }
    uint64_t settled = 0;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint8_t state = node_machine_record_state(machine, r);
        const bool evaluated = (state & BUSTER_NODE_MACHINE_EVALUATED) != 0;
        const uint32_t consumer = consumers[r];
        const bool consumer_evaluated = consumer < count && (node_machine_record_state(machine, consumer) & BUSTER_NODE_MACHINE_EVALUATED) != 0;
        const CheckOp consumer_op = consumer < count ? node_machine_record_op(machine, consumer) : CHECK_OP_COUNT;
        const bool inherited = evaluated && (consumer_op == CHECK_OP_SELECT || (consumer_op == CHECK_OP_COMMA && r == node_machine_record_right(machine, consumer)));
        const bool wasted = !evaluated && consumer < count && !consumer_evaluated;
        const bool nested = evaluated && node_machine_record_op(machine, r) == CHECK_OP_CALL && node_machine_record_call_depth(machine, r) != 0;
        shadow->fusion_evaluated += evaluated;
        shadow->fusion_unevaluated += !evaluated;
        shadow->fusion_wasted += wasted;
        shadow->fusion_inherited += inherited;
        shadow->fusion_inherited_other += inherited && node_machine_record_context(machine, r) != 0;
        shadow->fusion_calls += evaluated && node_machine_record_op(machine, r) == CHECK_OP_CALL;
        shadow->fusion_nested_calls += nested;
        shadow->fusion_hooked += evaluated && state > BUSTER_NODE_MACHINE_EVALUATED;
        settled += inherited || wasted || nested;
    }
    shadow->fusion_updates += machine->update_count;
    shadow->fusion_settled_expressions += settled != 0;
}

// --fused, after the three sweeps ran on the expression with `refusal` (C's or X's, or none): C and X as one sweep on the
// same records, its refusal, tape, labels and calls compared with theirs, which are its exact twin.
static void node_shadow_fused(NodeShadow* shadow, NodeCensusScratch* s, NodeMachineRefusal refusal, uint8_t context, TypeId target, uint32_t object)
{
    NodeMachine* machine = s->machine;
    const uint32_t entries = machine->entry_count;
    const uint32_t labels = machine->label_count;
    const uint32_t calls = machine->call_count;
    const uint32_t temporaries = machine->temporary_count;
    const bool taken = refusal == NODE_MACHINE_TAKEN;
    if (taken)
    {
        memcpy(s->fused_kinds, machine->kinds, entries);
        memcpy(s->fused_classes, machine->classes, entries);
        memcpy(s->fused_tape, machine->tape, (uint64_t)entries * sizeof(uint64_t));
        memcpy(s->fused_labels, machine->label_entries, (uint64_t)labels * sizeof(uint32_t));
        memcpy(s->fused_calls, machine->call_records, (uint64_t)calls * sizeof(uint32_t));
    }
    const NodeMachineRefusal fused = node_machine_lower(machine, context, target, object);
    bool same = fused == refusal;
    if (same && taken)
    {
        const uint32_t at = machine->entry_first;
        same = machine->entry_count == entries && machine->label_count == labels && machine->call_count == calls && machine->temporary_count == temporaries &&
               memcmp(s->fused_kinds, machine->kinds + at, entries) == 0 && memcmp(s->fused_classes, machine->classes + at, entries) == 0 &&
               memcmp(s->fused_tape, machine->tape + at, (uint64_t)entries * sizeof(uint64_t)) == 0 &&
               memcmp(s->fused_labels, machine->label_entries, (uint64_t)labels * sizeof(uint32_t)) == 0 &&
               memcmp(s->fused_calls, machine->call_records, (uint64_t)calls * sizeof(uint32_t)) == 0;
    }
    shadow->fused_expressions += 1;
    shadow->fused_differences += !same;
    if (!same && shadow->listed < node_census_list)
    {
        shadow->listed += 1;
        printf("node-shadow: fused difference file=%s line=%u refusal=%s (fused %s) entries=%u (fused %u) labels=%u (%u) calls=%u (%u)\n", s->file,
               node_census_line(s->pp, s->program->parser, s->program->input->graph.tokens[node_machine_node(machine, machine->count - 1)]),
               node_machine_refusal_names[refusal], node_machine_refusal_names[fused], entries, machine->entry_count, labels, machine->label_count, calls, machine->call_count);
    }
}

// T's records of the machine's current expression (`records` of them from node `first`, and `root`; its refusal
// `refusal`) against what T made of the same root at reader time (`typed`, --front-types): the same range, the same
// refusal and the same records, by their digest. A root reader time didn't run on is counted.
static void node_shadow_typed(NodeShadow* shadow, const NodeCensusScratch* s, NodeFrontTypes* typed, uint32_t first, uint32_t root, uint32_t records, NodeMachineRefusal refusal)
{
    const CheckProgram* program = s->program;
    if (root < typed->node_capacity && typed->counts[root] != 0)
    {
        const bool range = typed->firsts[root] == first && typed->counts[root] == records;
        const NodeMachineRefusal earlier = (NodeMachineRefusal)(typed->refusals[root] & BUSTER_NODE_FRONT_TYPED_REFUSAL);
        const bool same = range && earlier == refusal && typed->digests[root] == node_machine_digest(s->machine, refusal);
        typed->refusals[root] |= BUSTER_NODE_FRONT_TYPED_COMPARED;
        shadow->typed_compared += 1;
        shadow->typed_compared_records += records;
        shadow->typed_differences += !same;
        shadow->typed_range_differences += !range;
        shadow->typed_refusal_differences += range && earlier != refusal;
        if (!same && shadow->typed_listed < node_census_list)
        {
            shadow->typed_listed += 1;
            printf("node-shadow: reader-time T difference file=%s line=%u range=%s (reader first=%u records=%u, shadow first=%u records=%u) refusal=%s (reader %s) stopped=%u\n",
                   s->file, node_census_line(s->pp, program->parser, program->input->graph.tokens[root]), range ? "same" : "DIFFERENT", typed->firsts[root], typed->counts[root],
                   first, records, node_machine_refusal_names[refusal], node_machine_refusal_names[earlier < NODE_MACHINE_REFUSALS ? earlier : NODE_MACHINE_TAKEN],
                   s->machine->stopped);
        }
    }
    else
    {
        // Reader time ran on no such root: one the front left, or one whose every node the front made but whose root it
        // left (an initializer's value beside a variable-length array's bound, whose INITIALIZE takes two values).
        const NodeFrontShadow* front = program->input->node_front;
        bool made = root < front->node_capacity && (front->nodes[root >> 6] >> (root & 63) & 1) != 0;
        for (uint32_t node = first; made && node + 1 < first + records; node += 1)
        {
            made = (front->nodes[node >> 6] >> (node & 63) & 1) != 0;
        }
        shadow->typed_missing += !made;
        shadow->typed_unrun += made;
        if (made && shadow->typed_listed < node_census_list)
        {
            const CheckOp op = program->input->graph.ops[root];
            shadow->typed_listed += 1;
            printf("node-shadow: reader-time T not run on an expression the front made file=%s line=%u root-op=%s records=%u\n", s->file,
                   node_census_line(s->pp, program->parser, program->input->graph.tokens[root]), lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", records);
        }
    }
}

// Whether an entry of kind `kind` holds a global's symbol: at reader time its object, which stands for it.
static bool node_shadow_global_kind(uint8_t kind)
{
    return kind == BUSTER_TYPED_GLOBAL || kind == BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL;
}

// C's and X's results on the machine's current expression (root `root`, their refusal `refusal`, and what its pop gave
// it: the conversion's target `target` and the object `object`) against reader time's on the same root (`typed`,
// --front-sweeps): the same target and object, the same refusal and, where both took it, the same tape once reader
// time's is patched with the program's symbols (a global's entry holds its object there), and the same labels, calls
// and temporaries by their digest. A root reader time didn't sweep (T refused it there, or didn't run) isn't compared.
static void node_shadow_swept(NodeShadow* shadow, const NodeCensusScratch* s, const NodeFrontTypes* typed, uint32_t root, NodeMachineRefusal refusal, TypeId target,
                              uint32_t object)
{
    const NodeMachine* machine = s->machine;
    const CheckProgram* program = s->program;
    if (typed->sweep_refusals != NULL && root < typed->node_capacity && (typed->refusals[root] & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0)
    {
        const NodeMachineRefusal earlier = (NodeMachineRefusal)typed->sweep_refusals[root];
        const bool rooted = typed->targets[root] == target && typed->objects[root] == object;
        const bool refused = earlier == refusal;
        const bool taken = refused && refusal == NODE_MACHINE_TAKEN && typed->tape_counts[root] != UINT32_MAX;
        const uint32_t entries = taken ? typed->tape_counts[root] : 0;
        const uint64_t first = typed->tape_firsts[root];
        bool tape = !taken || entries == machine->entry_count;
        uint32_t at = UINT32_MAX;
        uint64_t references = 0;
        for (uint32_t e = 0; taken && tape && e < entries; e += 1)
        {
            const uint8_t kind = typed->tape_kinds[first + e];
            const uint64_t value = typed->tape_values[first + e];
            const bool global = node_shadow_global_kind(kind);
            // The splice's patch: a global's entry holds its object at reader time, and its symbol is the program's.
            const uint64_t patched = !global ? value : value < program->object_count ? program->object_symbols[value] : 0;
            tape = kind == machine->kinds[e] && typed->tape_classes[first + e] == machine->classes[e] && patched == machine->tape[e];
            references += global;
            at = tape ? at : e;
        }
        const bool side = !taken || typed->side_digests[root] == node_machine_side_digest(machine);
        uint64_t targets = 0;
        for (uint32_t call = 0; taken && call < machine->call_count; call += 1)
        {
            targets += node_machine_call_target(machine, machine->call_records[call]) != 0;
        }
        const bool same = rooted && refused && tape && side;
        shadow->swept_compared += 1;
        shadow->swept_compared_entries += entries;
        shadow->swept_references += references;
        shadow->swept_call_targets += targets;
        shadow->swept_differences += !same;
        shadow->swept_root_differences += !rooted;
        shadow->swept_refusal_differences += rooted && !refused;
        shadow->swept_tape_differences += rooted && refused && !tape;
        shadow->swept_side_differences += rooted && refused && tape && !side;
        if (!same && shadow->swept_listed < node_census_list)
        {
            const uint32_t e = at != UINT32_MAX ? at : 0;
            const bool entry = at != UINT32_MAX && at < machine->entry_count;
            shadow->swept_listed += 1;
            printf("node-shadow: reader-time C and X difference file=%s line=%u target=%u (pop %u) object=%u (pop %u) refusal=%s (reader %s) entries=%u (reader %u) "
                   "entry=%u kind=%u (reader %u) class=%u (reader %u) value=%" PRIu64 " (reader %" PRIu64 ") side=%s\n",
                   s->file, node_census_line(s->pp, program->parser, program->input->graph.tokens[root]), typed->targets[root], target, typed->objects[root], object,
                   node_machine_refusal_names[refusal], node_machine_refusal_names[earlier < NODE_MACHINE_REFUSALS ? earlier : NODE_MACHINE_TAKEN], machine->entry_count,
                   typed->tape_counts[root], at, entry ? machine->kinds[e] : 0, entry ? typed->tape_kinds[first + e] : 0, entry ? machine->classes[e] : 0,
                   entry ? typed->tape_classes[first + e] : 0, entry ? machine->tape[e] : 0, entry ? typed->tape_values[first + e] : 0, side ? "same" : "DIFFERENT");
        }
    }
}

// One compared root's result, for the tests (NodeShadow.tested_out): its line, reader time's tests and the census's, and
// whether reader time takes the expression (no test refuses it, and C and X take it).
typedef struct NodeShadowTested NodeShadowTested;
struct NodeShadowTested
{
    uint32_t line;
    uint64_t reader;
    uint64_t census;
    bool taken;
    bool popped;
};

// The census's verdict on the expression whose root is node `root` (its tests `census`, and for a root the lowering
// popped its kind `context` and its `nodes` evaluated nodes) against reader time's (`typed`, --front-tests), test by test.
// The lowering's own facts aren't reader time's to know; its actions' warnings are at least the graph run's; and its
// events are the reader's own so far. A root the tests didn't run on there (T refused it, or didn't run) isn't compared.
static void node_shadow_tested(NodeShadow* shadow, const NodeCensusScratch* s, const NodeFrontTypes* typed, uint32_t root, uint64_t census, NodeCensusRoot context,
                               uint64_t nodes, bool popped)
{
    if (typed->tests != NULL && root < typed->node_capacity && (typed->refusals[root] & BUSTER_NODE_FRONT_TYPED_TESTED) != 0)
    {
        const uint64_t reader = typed->tests[root];
        const uint64_t lowering = UINT64_C(1) << NODE_CENSUS_TRACKED | UINT64_C(1) << NODE_CENSUS_POPPED_AGAIN;
        const uint64_t reader_alone = reader & ~census & ~lowering;
        const uint64_t census_alone = census & ~reader & ~lowering;
        const bool kind = !popped || typed->kinds[root] == (uint8_t)context;
        const bool swept = typed->sweep_refusals == NULL ||
                           ((typed->refusals[root] & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 && typed->sweep_refusals[root] == NODE_MACHINE_TAKEN);
        const bool taken = reader == 0 && swept;
        // The lowering's own facts are a splice's to read where it pops the root: no test of reader time's.
        const bool unsound = taken && (census & ~lowering) != 0;
        const uint64_t more = UINT64_C(1) << NODE_CENSUS_ACTION_WARNING | UINT64_C(1) << NODE_CENSUS_PACKED_MEMBER;
        const bool differs = (reader_alone & ~more) != 0 || (census_alone & ~(UINT64_C(1) << NODE_CENSUS_EVENT)) != 0;
        shadow->tested_lowering += taken && census != 0 && (census & ~lowering) == 0;
        shadow->tested_compared += popped;
        shadow->tested_compared_unpopped += !popped;
        shadow->tested_taken += taken;
        shadow->tested_taken_nodes += taken ? nodes : 0;
        shadow->tested_unsound += unsound;
        shadow->tested_differences += differs;
        shadow->tested_kind_differences += !kind;
        shadow->tested_census_refused += census != 0;
        // What the tests add to the machine's own refusals: the expressions C and X take, or would, that the census refuses.
        shadow->tested_census_taken_refused += swept && census != 0;
        shadow->tested_census_taken_refused_nodes += swept && census != 0 ? nodes : 0;
        for (uint64_t bits = reader_alone; bits != 0; bits &= bits - 1)
        {
            shadow->tested_reader_alone[__builtin_ctzll(bits)] += 1;
        }
        for (uint64_t bits = census_alone | (census & lowering); bits != 0; bits &= bits - 1)
        {
            shadow->tested_census_alone[__builtin_ctzll(bits)] += 1;
        }
        if (shadow->tested_out != NULL && shadow->tested_out_count < shadow->tested_out_capacity)
        {
            const CheckProgram* program = s->program;
            shadow->tested_out[shadow->tested_out_count++] = (NodeShadowTested){
                .line = node_census_line(s->pp, program->parser, program->input->graph.tokens[root]),
                .reader = reader,
                .census = census,
                .taken = taken,
                .popped = popped,
            };
        }
        if ((unsound || differs || !kind) && shadow->tested_listed < node_census_list)
        {
            const CheckProgram* program = s->program;
            shadow->tested_listed += 1;
            printf("node-shadow: reader-time tests difference file=%s line=%u %s tests=%#" PRIx64 " (reader %#" PRIx64 ") kind=%s (reader %s) reader-takes=%s%s\n", s->file,
                   node_census_line(s->pp, program->parser, program->input->graph.tokens[root]), popped ? "popped" : "never-lowered", census, reader,
                   node_census_root_names[context], node_census_root_names[typed->kinds[root] < NODE_CENSUS_ROOTS ? typed->kinds[root] : NODE_CENSUS_ROOT_OTHER],
                   taken ? "yes" : "no", unsound ? " UNSOUND" : "");
        }
    }
}

// Whether token `token` is in a body with a diagnostic or floating-point pragma, by the outermost braces around each
// pragma: node_census_region's answer for a root no lowered body holds.
static bool node_census_region_at(const CheckProgram* program, uint32_t token)
{
    const CheckInput* input = program->input;
    const uint32_t diagnostics = input->diagnostic_pragmas != NULL ? input->diagnostic_pragmas->count : 0;
    const uint32_t pragmas = diagnostics + (input->fp_pragmas != NULL ? input->fp_pragma_count : 0);
    bool found = false;
    for (uint32_t i = 0; i < pragmas && !found; i += 1)
    {
        const uint32_t at = i < diagnostics ? input->diagnostic_pragmas->records[i].token : input->fp_pragmas[i - diagnostics].token;
        uint32_t opening = 0;
        uint32_t closing = 0;
        found = node_census_reader_braces(program->parser, at, &opening, &closing) && token > opening && token < closing;
    }
    return found;
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
    const bool ran = refusal == NODE_MACHINE_TAKEN;
    NodeFrontTypes* typed = program->input->node_front != NULL ? program->input->node_front->types : NULL;
    if (ran && typed != NULL)
    {
        // The sweeps one at a time, so that T's records are compared with reader time's before C writes its own.
        refusal = node_machine_type(machine, first, root, records);
        node_shadow_typed(shadow, s, typed, first, root, records, refusal);
        const bool swept = refusal == NODE_MACHINE_TAKEN;
        refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_contexts(machine, root_context, pop->target, object) : refusal;
        refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_expand(machine, object) : refusal;
        if (swept)
        {
            node_shadow_swept(shadow, s, typed, root, refusal, pop->target, object);
        }
    }
    else if (ran)
    {
        refusal = node_machine_run(machine, first, root, records, root_context, pop->target, object);
    }
    shadow->memo_fills += machine->memo_fills - memo_fills;
    shadow->memo_rules += machine->memo_rules - memo_rules;
    shadow->expressions += 1;
    shadow->nodes += nodes;
    shadow->records += walked;
    NodeShadowRecord* out = shadow->records_out != NULL && shadow->record_count < shadow->record_capacity ? shadow->records_out + shadow->record_count : NULL;
    if (out != NULL)
    {
        shadow->record_count += 1;
        *out = (NodeShadowRecord){
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
        const NodeFrontShadow* front = program->input->node_front;
        if (front != NULL)
        {
            // The front made the expression where each of its nodes' records agreed with the walk's.
            bool made = root < front->node_capacity && (front->nodes[root >> 6] >> (root & 63) & 1) != 0;
            bool past = made && (front->past[root >> 6] >> (root & 63) & 1) != 0;
            for (uint32_t node = first; made && node + 1 < first + records; node += 1)
            {
                made = (front->nodes[node >> 6] >> (node & 63) & 1) != 0;
                past = past || (front->past[node >> 6] >> (node & 63) & 1) != 0;
            }
            shadow->front_expressions += made;
            shadow->front_nodes += made ? nodes : 0;
            shadow->front_past_expressions += made && past;
            shadow->front_past_nodes += made && past ? nodes : 0;
            if (out != NULL)
            {
                out->fronted = made;
            }
            if (!made && shadow->front_missed_listed < node_census_list)
            {
                shadow->front_missed_listed += 1;
                printf("node-shadow: front missed file=%s line=%u nodes=%u\n", s->file, node_census_line(s->pp, program->parser, program->input->graph.tokens[root]), nodes);
            }
        }
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
        node_shadow_fusion(shadow, s, machine);
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
                .body = s->body_stamp,
                .context = root_context,
                .flags = pop->flags,
                .kind = (uint8_t)context,
            };
        }
        if (s->facts != NULL)
        {
            node_facts_append(s->facts, s->body_stamp);
        }
    }
    // The one sweep, for every expression T typed: C and X took it or refused it.
    if (shadow->fused && ran && (refusal == NODE_MACHINE_TAKEN || refusal == NODE_MACHINE_REFUSED_TEMPLATE || refusal == NODE_MACHINE_REFUSED_ROW))
    {
        node_shadow_fused(shadow, s, refusal, root_context, pop->target, object);
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

// One expression through the sweeps of set `sweeps`: T alone, T and C, or all three; or T and the one sweep of C and X.
static inline __attribute__((always_inline)) void node_shadow_sweep_one(NodeMachine* machine, const NodeShadowExpression* expression, uint32_t sweeps)
{
    const bool fused = sweeps == BUSTER_NODE_SHADOW_SET_FUSED || sweeps == BUSTER_NODE_SHADOW_SET_WALK_FUSED;
    NodeMachineRefusal refusal = node_machine_type(machine, expression->first, expression->root, expression->count);
    if (fused)
    {
        if (refusal == NODE_MACHINE_TAKEN)
        {
            node_machine_lower(machine, expression->context, expression->target, expression->object);
        }
    }
    else
    {
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

// Each of the file's taken expressions through set `sweeps` (the sets' comment). An interleaved set takes them by chunks:
// the census's walk of each of a chunk's expressions, then the sweeps of each, whose TSC ticks alone are the pass's
// `inner_ticks`.
static void node_shadow_sweep(NodeMachine* machine, NodeShadow* shadow, uint32_t sweeps)
{
    shadow->inner_ticks = 0;
    if (sweeps >= BUSTER_NODE_SHADOW_SET_WALK)
    {
        uint64_t inner = 0;
        uint32_t base = 0;
        while (base < shadow->timed_count)
        {
            uint32_t end = base + 1;
            while (end < shadow->timed_count && (shadow->chunk == 0 ? shadow->timed[end].body == shadow->timed[base].body : end - base < shadow->chunk))
            {
                end += 1;
            }
            for (uint32_t i = base; i < end; i += 1)
            {
                const NodeShadowExpression* expression = shadow->timed + i;
                uint32_t walked = 0;
                shadow->walk_tests += node_census_expression(shadow->scratch, expression->root, expression->flags, (NodeCensusRoot)expression->kind, &walked);
            }
            if (sweeps != BUSTER_NODE_SHADOW_SET_WALK)
            {
                const uint64_t t0 = __rdtsc();
                for (uint32_t i = base; i < end; i += 1)
                {
                    node_shadow_sweep_one(machine, shadow->timed + i, sweeps);
                }
                inner += __rdtsc() - t0;
            }
            base = end;
        }
        shadow->inner_ticks = inner;
    }
    else
    {
        for (uint32_t i = 0; i < shadow->timed_count; i += 1)
        {
            // --repeat: each expression's sweeps again at once, its inputs in the caches as the reader leaves them in step 2.
            for (uint32_t time = 0; time < shadow->repeat; time += 1)
            {
                node_shadow_sweep_one(machine, shadow->timed + i, sweeps);
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
    const bool enabled[BUSTER_NODE_SHADOW_SWEEPS] = { true, true, true, shadow->fused, shadow->interleave, shadow->interleave, shadow->interleave && shadow->fused };
    machine->verify = false;
    uint64_t least[BUSTER_NODE_SHADOW_SWEEPS][4];
    memset(least, 0xff, sizeof(least));
    // A set's passes run one after another, so that its least follows its own code, whichever other sets are timed: with
    // the sets taken in turn inside a pass, a file's T ran after the walk's sets and read 7 cycles a node more on Lua.
    for (uint32_t sweeps = 0; sweeps < BUSTER_NODE_SHADOW_SWEEPS; sweeps += 1)
    {
        for (uint32_t pass = 0; pass < shadow->passes; pass += 1)
        {
            if (enabled[sweeps])
            {
                node_machine_forget(machine);
                const uint64_t c0 = node_shadow_read(cycles);
                const uint64_t i0 = node_shadow_read(instructions);
                const uint64_t t0 = __rdtsc();
                node_shadow_sweep(machine, shadow, sweeps);
                const uint64_t t1 = __rdtsc();
                const uint64_t i1 = node_shadow_read(instructions);
                const uint64_t c1 = node_shadow_read(cycles);
                const uint64_t measured[4] = { c1 - c0, i1 - i0, t1 - t0, shadow->inner_ticks };
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    least[sweeps][k] = measured[k] < least[sweeps][k] ? measured[k] : least[sweeps][k];
                }
            }
        }
    }
    for (uint32_t sweeps = 0; shadow->passes != 0 && sweeps < BUSTER_NODE_SHADOW_SWEEPS; sweeps += 1)
    {
        if (enabled[sweeps])
        {
            shadow->cycles[sweeps] += least[sweeps][0];
            shadow->instructions[sweeps] += least[sweeps][1];
            shadow->ticks[sweeps] += least[sweeps][2];
            shadow->inner[sweeps] += least[sweeps][3];
        }
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

// --facts, a file's: the kernel (node_facts_window) over each window of the file's machine-taken expressions. First a
// pass over records no rule has written, whose every identifier's leaf and lookup is compared with T's record
// (node_facts_same), and a second over decoys; then the timing: `passes` passes over the windows, and as many with each window run twice in a row,
// whose second run finds its inputs in the caches.
static void node_shadow_facts(NodeShadow* shadow, NodeCensusScratch* s)
{
    NodeFacts* facts = s->facts;
    const CheckProgram* program = s->program;
    const uint32_t windows = facts->record_count != 0 ? facts->window_count + 1 : 0;
    if (!facts->room)
    {
        shadow->facts_failures += 1;
        printf("node-facts: file=%s failed=\"no room for its windows\"\n", s->file);
    }
    else
    {
        // The checked pass. The loads' kernel takes the lean one's answer for whether a lookup's operand is fit, so the lean
        // kernel is checked first for it and the loads' own records after.
        const uint32_t mode = shadow->facts_mode;
        const uint32_t checked = mode == BUSTER_NODE_FACTS_LOADS ? BUSTER_NODE_FACTS_LEAN : mode;
        // The members' slots are a memo, filled by an access's rule the first time: a first pass over the unit fills them,
        // as declarations would have, so that the checked pass and the timed ones find the same words and the short
        // paths take what they take in every pass.
        for (uint32_t window = 0; window < windows; window += 1)
        {
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
            node_facts_window(facts, window, checked, false);
        }
        for (uint32_t window = 0; window < windows; window += 1)
        {
            const uint32_t first = facts->windows[window];
            const uint32_t count = facts->windows[window + 1] - first;
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_RECORDS);
            node_facts_window(facts, window, checked, false);
            shadow->facts_list_differences += !node_facts_lists_same(facts, window);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t w = first + i;
                const uint8_t kind = facts->kinds[w];
                // What the kernel makes: a leaf with a line (an identifier's, a literal's) and a made operator (a lookup,
                // another operator).
                const bool leaf = kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LITERAL_LINE;
                const bool lookup = kind == BUSTER_NODE_FACTS_LOOKUP;
                const bool operation = kind == BUSTER_NODE_FACTS_OPERATOR;
                if (leaf || lookup || operation)
                {
                    const CheckOp op = (CheckOp)facts->ops[w];
                    const bool line = kind == BUSTER_NODE_FACTS_LITERAL_LINE;
                    const bool brief = facts->records[i].taken == BUSTER_NODE_FACTS_SHORT;
                    const bool taken = brief || facts->records[i].taken == BUSTER_NODE_FACTS_RULED;
                    const bool same = node_facts_same(facts, first, i, checked);
                    shadow->facts_taken_leaves += taken && leaf && !line;
                    shadow->facts_taken_lines += taken && line;
                    shadow->facts_taken_lookups += taken && lookup;
                    shadow->facts_short_lookups += brief && lookup;
                    shadow->facts_left_identifiers += !taken && leaf && !line;
                    shadow->facts_left_lookups[lookup ? node_facts_lookup(op) : 0] += !taken && lookup;
                    shadow->facts_operator_taken[op] += taken && operation;
                    shadow->facts_operator_short[op] += brief && operation;
                    shadow->facts_differences += !same;
                    facts->fits[w] = brief && lookup;
                    if (!same && shadow->facts_listed < node_census_list)
                    {
                        shadow->facts_listed += 1;
                        printf("node-facts: difference file=%s line=%u op=%s taken=%u type=%u/%u flags=%#x/%#x standing=%#x/%#x object=%u/%u payload=%" PRIu64 "/%" PRIu64
                               " converted=%u,%u/%u,%u conversion-flags=%#x,%#x/%#x,%#x member-offset=%" PRIu64 "/%" PRIu64 "\n",
                               s->file, node_census_line(s->pp, program->parser, program->input->graph.tokens[facts->nodes[w]]),
                               lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", facts->records[i].taken, facts->records[i].type, facts->expected_types[w],
                               facts->records[i].flags, facts->expected_flags[w], facts->records[i].standing, facts->expected_standings[w], facts->records[i].object, facts->expected_objects[w],
                               facts->records[i].payload, facts->expected_payloads[w], facts->records[i].converted[0], facts->records[i].converted[1],
                               facts->expected_converted[2 * (uint64_t)w], facts->expected_converted[2 * (uint64_t)w + 1], facts->records[i].conversion_flags[0],
                               facts->records[i].conversion_flags[1], facts->expected_conversion_flags[2 * (uint64_t)w],
                               facts->expected_conversion_flags[2 * (uint64_t)w + 1], facts->records[i].member_offset, facts->expected_member_offsets[w]);
                    }
                    // What the lean kernel left to C: the operator's conversions' entries must be its rule's on these records.
                    if (checked == BUSTER_NODE_FACTS_LEAN && !leaf && taken)
                    {
                        shadow->facts_conversions += 1;
                        shadow->facts_conversions_derived += node_facts_derived(facts, first, i, false);
                    }
                }
                else if (kind == BUSTER_NODE_FACTS_DISCARD)
                {
                    // And a discard it didn't make: T's record must be the discard's rule on its operand's record.
                    shadow->facts_discards += 1;
                    shadow->facts_discards_derived += node_facts_derived(facts, first, i, true);
                }
            }
            if (mode == BUSTER_NODE_FACTS_LOADS)
            {
                node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_LOADS);
                node_facts_window(facts, window, BUSTER_NODE_FACTS_LOADS, false);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t w = first + i;
                    const uint8_t kind = facts->kinds[w];
                    if (kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LOOKUP)
                    {
                        const bool same = node_facts_same(facts, first, i, BUSTER_NODE_FACTS_LOADS);
                        shadow->facts_differences += !same;
                        shadow->facts_loads += kind == BUSTER_NODE_FACTS_LOOKUP && facts->records[i].taken == BUSTER_NODE_FACTS_SHORT;
                        if (!same && shadow->facts_listed < node_census_list)
                        {
                            shadow->facts_listed += 1;
                            printf("node-facts: loads difference file=%s line=%u taken=%u type=%u/%u word=%#x/%#x member-offset=%" PRIu64 "/%" PRIu64 "\n", s->file,
                                   node_census_line(s->pp, program->parser, program->input->graph.tokens[facts->nodes[w]]), facts->records[i].taken, facts->records[i].type,
                                   facts->expected_types[w], facts->records[i].word, facts->expected_words[w], facts->records[i].member_offset, facts->expected_member_offsets[w]);
                        }
                    }
                }
            }
        }
        // The checked pass again over decoys: T's record of each node the kernel is to make, with one inherited flag the
        // other way. An operator made before its operand takes the decoy for it, as it would take T's record in a
        // timed pass, and differs here; over poison it would only be left to its rule.
        for (uint32_t window = 0; window < windows; window += 1)
        {
            const uint32_t first = facts->windows[window];
            const uint32_t count = facts->windows[window + 1] - first;
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_DECOY);
            node_facts_window(facts, window, checked, false);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t w = first + i;
                const uint8_t kind = facts->kinds[w];
                if ((kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LITERAL_LINE || kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR) &&
                    !node_facts_same(facts, first, i, checked))
                {
                    shadow->facts_differences += 1;
                    shadow->facts_decoy_differences += 1;
                    if (shadow->facts_listed < node_census_list)
                    {
                        const CheckOp op = (CheckOp)facts->ops[w];
                        shadow->facts_listed += 1;
                        printf("node-facts: difference over decoys file=%s line=%u op=%s taken=%u type=%u/%u flags=%#x/%#x standing=%#x/%#x\n", s->file,
                               node_census_line(s->pp, program->parser, program->input->graph.tokens[facts->nodes[w]]),
                               lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", facts->records[i].taken, facts->records[i].type, facts->expected_types[w],
                               facts->records[i].flags, facts->expected_flags[w], facts->records[i].standing, facts->expected_standings[w]);
                    }
                }
            }
        }
        const int cycles = node_shadow_counter(PERF_COUNT_HW_CPU_CYCLES);
        const int instructions = node_shadow_counter(PERF_COUNT_HW_INSTRUCTIONS);
        const int mispredicts = node_shadow_counter(PERF_COUNT_HW_BRANCH_MISSES);
        uint64_t least[12];
        memset(least, 0xff, sizeof(least));
        for (uint32_t pass = 0; pass < shadow->passes; pass += 1)
        {
            const uint64_t c0 = node_shadow_read(cycles);
            const uint64_t i0 = node_shadow_read(instructions);
            const uint64_t m0 = node_shadow_read(mispredicts);
            const uint64_t t0 = __rdtsc();
            for (uint32_t window = 0; window < windows; window += 1)
            {
                node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
                node_facts_window(facts, window, mode, false);
            }
            const uint64_t t1 = __rdtsc();
            const uint64_t m1 = node_shadow_read(mispredicts);
            const uint64_t i1 = node_shadow_read(instructions);
            const uint64_t c1 = node_shadow_read(cycles);
            const uint64_t measured[3] = { c1 - c0, i1 - i0, t1 - t0 };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                least[k] = measured[k] < least[k] ? measured[k] : least[k];
            }
            least[10] = m1 - m0 < least[10] ? m1 - m0 : least[10];
        }
        // The windows' given records put in place and no kernel: what of a pass above is the harness's copying.
        for (uint32_t pass = 0; pass < shadow->passes; pass += 1)
        {
            const uint64_t c0 = node_shadow_read(cycles);
            const uint64_t i0 = node_shadow_read(instructions);
            const uint64_t m0 = node_shadow_read(mispredicts);
            const uint64_t t0 = __rdtsc();
            for (uint32_t window = 0; window < windows; window += 1)
            {
                node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
            }
            const uint64_t t1 = __rdtsc();
            const uint64_t m1 = node_shadow_read(mispredicts);
            const uint64_t i1 = node_shadow_read(instructions);
            const uint64_t c1 = node_shadow_read(cycles);
            const uint64_t measured[3] = { c1 - c0, i1 - i0, t1 - t0 };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                least[7 + k] = measured[k] < least[7 + k] ? measured[k] : least[7 + k];
            }
            least[11] = m1 - m0 < least[11] ? m1 - m0 : least[11];
        }
        // The two loops apart, by the TSC: each window's run after its given records were put in place, and its run again
        // at once, when everything it touches is where the first run left it.
        for (uint32_t pass = 0; pass < shadow->passes; pass += 1)
        {
            uint64_t ticks[4] = { 0, 0, 0, 0 };
            for (uint32_t window = 0; window < windows; window += 1)
            {
                node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
                facts->leaf_ticks = 0;
                facts->lookup_ticks = 0;
                node_facts_window(facts, window, mode, true);
                ticks[0] += facts->leaf_ticks;
                ticks[1] += facts->lookup_ticks;
                facts->leaf_ticks = 0;
                facts->lookup_ticks = 0;
                node_facts_window(facts, window, mode, true);
                ticks[2] += facts->leaf_ticks;
                ticks[3] += facts->lookup_ticks;
            }
            for (uint32_t k = 0; k < 4; k += 1)
            {
                least[3 + k] = ticks[k] < least[3 + k] ? ticks[k] : least[3 + k];
            }
        }
        if (shadow->passes != 0)
        {
            shadow->facts_cycles += least[0];
            shadow->facts_instructions += least[1];
            shadow->facts_ticks += least[2];
            shadow->facts_loop_ticks[0] += least[3];
            shadow->facts_loop_ticks[1] += least[4];
            shadow->facts_warm[0] += least[5];
            shadow->facts_warm[1] += least[6];
            shadow->facts_given_cycles += least[7];
            shadow->facts_given_instructions += least[8];
            shadow->facts_given_ticks += least[9];
            shadow->facts_mispredicts += least[10];
            shadow->facts_given_mispredicts += least[11];
        }
        if (cycles >= 0)
        {
            close(cycles);
        }
        if (instructions >= 0)
        {
            close(instructions);
        }
        if (mispredicts >= 0)
        {
            close(mispredicts);
        }
        shadow->facts_records += facts->record_total;
        shadow->facts_literals += facts->literals;
        shadow->facts_identifiers += facts->identifiers;
        shadow->facts_named[0] += facts->enumerators;
        shadow->facts_named[1] += facts->functions;
        shadow->facts_named[2] += facts->static_objects;
        shadow->facts_named[3] += facts->automatic_objects;
        for (uint32_t lookup = 0; lookup < BUSTER_NODE_FACTS_LOOKUPS; lookup += 1)
        {
            shadow->facts_lookups[lookup] += facts->lookups[lookup];
        }
        for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
        {
            shadow->facts_given[op] += facts->given[op];
            shadow->facts_operator_records[op] += facts->operator_records[op];
        }
        shadow->facts_literal_lines += facts->literal_lines;
        shadow->facts_pairs += facts->pair_count;
        shadow->facts_rows += facts->row_count;
        shadow->facts_lines += facts->line_count;
        shadow->facts_line_versions += facts->line_versions;
        shadow->facts_members += facts->member_count;
        shadow->facts_windows += windows;
        shadow->facts_widest = facts->widest > shadow->facts_widest ? facts->widest : shadow->facts_widest;
        shadow->facts_row_ticks += facts->row_ticks;
        shadow->facts_line_ticks += facts->line_ticks;
        // --facts-reads, last: its rules' runs fill the members' and the pointer conversions' slots with keys no pass
        // above asked for. A later rule's reads by the kernel's windows; then C's and X's by the machine's expressions,
        // typed again one at a time, whose records are the windows' in their order.
        if (s->reads != NULL)
        {
            NodeMachine* machine = s->machine;
            for (uint32_t window = 0; window < windows; window += 1)
            {
                node_reads_consumers(s->reads, facts, window);
            }
            uint64_t base = 0;
            for (uint32_t e = 0; e < shadow->timed_count; e += 1)
            {
                const NodeShadowExpression* expression = shadow->timed + e;
                if (node_machine_type(machine, expression->first, expression->root, expression->count) == NODE_MACHINE_TAKEN)
                {
                    node_reads_sweeps(s->reads, facts, machine, base, expression->context, expression->target, expression->object);
                }
                base += expression->count;
            }
            node_reads_classify(s->reads, facts);
            // --list N: the first N records C or X decide on an identity of, by file and line, with each reader's bits
            // of it (BUSTER_NODE_READS_TYPE_DECIDES, _OBJECT_DECIDES, _CONVERTED_DECIDES).
            for (uint64_t at = 0; at < facts->record_count && shadow->reads_listed < node_census_list; at += 1)
            {
                const uint64_t mask = s->reads->masks[at];
                const uint32_t contexts = (uint32_t)(mask >> (16 * BUSTER_NODE_READS_C)) & BUSTER_NODE_READS_IDENTITY_DECIDES;
                const uint32_t expanded = (uint32_t)(mask >> (16 * BUSTER_NODE_READS_X)) & BUSTER_NODE_READS_IDENTITY_DECIDES;
                if ((contexts | expanded) != 0 && s->reads->ran[at] == (BUSTER_NODE_READS_RAN_T | BUSTER_NODE_READS_RAN_SWEEPS))
                {
                    // Its place as the preprocessor presumes it (a header's line is the header's), its consumer's
                    // operator, its type's index and the index of the type it was changed to: one under
                    // BUSTER_TYPE_CODE_COUNT is a builtin type's code.
                    const uint32_t op = node_reads_op(facts, at);
                    const uint32_t ahead = s->reads->ahead[at];
                    const uint32_t consumer = ahead != 0 && at + ahead < facts->record_count ? node_reads_op(facts, at + ahead) : BUSTER_NODE_READS_OPS;
                    const Parser* parser = s->program->parser;
                    const uint32_t token = s->program->input->graph.tokens[facts->nodes[at]];
                    const char* name = s->file;
                    uint32_t length = (uint32_t)strlen(s->file);
                    uint32_t line = 0;
                    if (token < parser->tokens.count && parser->tokens.sources[token] < s->pp->output.count && s->pp->output.locations != NULL)
                    {
                        line = pp_presumed(s->pp, s->pp->output.locations[parser->tokens.sources[token]], &name, &length);
                    }
                    const uint32_t index = type_index(facts->expected_types[at]);
                    shadow->reads_listed += 1;
                    printf("node-reads: an identity decides: file=%.*s line=%u op=%s consumer=%s C=0x%x X=0x%x type=%u other=%u\n", (int)length, name, line,
                           op == BUSTER_NODE_READS_LITERAL ? "literal" : op == BUSTER_NODE_READS_DISCARD ? "discard" : op == CHECK_OP_VALUE ? "identifier" :
                           lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?",
                           consumer == BUSTER_NODE_READS_OPS ? "none" : consumer == BUSTER_NODE_READS_LITERAL ? "literal" : consumer == BUSTER_NODE_READS_DISCARD ? "discard" :
                           consumer == CHECK_OP_VALUE ? "identifier" : lower_inventory_op_names[consumer] != NULL ? lower_inventory_op_names[consumer] : "?",
                           contexts, expanded, index, index < facts->row_count ? s->reads->other_types[index] : index);
                }
            }
        }
    }
}

// --facts-reads' report (c_check_node_machine_reads.c): the runs; by kind and by operator the records every reader was
// run again for, by what they need made at them, and by kind which wide fields are read, which of those are new at
// the record, and which reader reads one; then, for each operator, each reader's reads of each field, the flags' bits
// read, the payloads that decided, and each read wide field's source. Only what is not zero is printed.
static void node_shadow_reads_report(const NodeShadow* shadow)
{
    const NodeReadsCounts* counts = &shadow->reads;
    static const char* const readers[BUSTER_NODE_READS_READERS] = { "T", "C", "X" };
    static const char* const kinds[BUSTER_NODE_READS_KINDS] = { "leaf", "lookup", "operator" };
    static const char* const bits[BUSTER_NODE_READS_BITS] = {
        "type-word",      "type-decides",    "type-carried",   "object-kind",      "object-decides",    "object-carried", "payload-decides", "payload-carried",
        "offset-decides", "offset-carried",  "converted-word", "converted-decides", "converted-carried", "flags",          "conversion-flags",
    };
    static const char* const flags[BUSTER_NODE_READS_FLAG_BITS] = {
        "ice",           "float-literal",    "bit-field",       "register",        "side-effect",         "ice-unevaluated",        "null-pointer",
        "static-object", "volatile-part",    "gnu-constant",    "gnu-ice-form",    "fp-environment",      "vector-element",         "vector-duplicate",
        "gnu-division",  "gnu-float-range",  "gnu-object-read", "literal-zero",    "inactive-union-read", "inactive-union-address", "const-read-restricted",
        "discarded-unevaluable", "modification", "nonnull-address", "register-name",
    };
    static const char* const conversions[BUSTER_NODE_READS_CONVERSION_BITS] = {
        "lvalue", "array", "function", "promotion", "arithmetic", "assignment", "cast", "null", "transparent-union", "atomic", "to-union", "failed",
    };
    static const char* const payloads[BUSTER_NODE_READS_PAYLOADS] = { "flipped", "zero", "three", "eight", "seventy-thousand", "large" };
    static const char* const fields[BUSTER_NODE_READS_FIELDS] = { "type", "object", "payload", "conversion-0", "conversion-1", "offset" };
    static const char* const sources[BUSTER_NODE_READS_SOURCES] = { "none", "leaf", "builtin", "operand", "row", "input", "new" };
    uint64_t records = 0;
    uint64_t measured = 0;
    for (uint32_t kind = 0; kind < BUSTER_NODE_READS_KINDS; kind += 1)
    {
        records += counts->kind_records[kind];
        measured += counts->kind_measured[kind];
    }
    printf("node-reads: expressions=%" PRIu64 " records=%" PRIu64 " measured=%" PRIu64 " (%.2f%%); expressions past %u records=%" PRIu64 " (records %" PRIu64
           "); consumers left to T=%" PRIu64 "; misplaced=%" PRIu64 "\n",
           counts->expressions, records, measured, node_census_share(measured, records), BUSTER_NODE_READS_MOST, counts->long_expressions, counts->long_records,
           counts->left_consumers, counts->misplaced);
    printf("node-reads: runs with a field changed: rules=%" PRIu64 " sweeps=%" PRIu64 "; with none changed=%" PRIu64 " differences=%" PRIu64 "\n", counts->rule_runs,
           counts->sweep_runs, counts->same_runs, counts->same_differences);
    for (uint32_t kind = 0; kind < BUSTER_NODE_READS_KINDS; kind += 1)
    {
        const uint64_t* by = counts->kind_classes[kind];
        printf("node-reads: kind=%s records=%" PRIu64 " measured=%" PRIu64 " none=%" PRIu64 " (%.2f%%) named=%" PRIu64 " (%.2f%%) copied=%" PRIu64 " (%.2f%%) new=%" PRIu64
               " (%.2f%%); C or X decide on an identity=%" PRIu64 " (%.2f%%); of a type with no other like it=%" PRIu64 "\n",
               kinds[kind], counts->kind_records[kind], counts->kind_measured[kind], by[0], node_census_share(by[0], counts->kind_measured[kind]), by[1],
               node_census_share(by[1], counts->kind_measured[kind]), by[2], node_census_share(by[2], counts->kind_measured[kind]), by[3],
               node_census_share(by[3], counts->kind_measured[kind]), counts->kind_identities[kind], node_census_share(counts->kind_identities[kind], counts->kind_measured[kind]),
               counts->kind_lone[kind]);
        const uint64_t* read = counts->kind_fields[kind];
        const uint64_t* fresh = counts->kind_new[kind];
        const uint64_t* by_reader = counts->kind_readers[kind];
        printf("node-reads: kind=%s a wide field read: type=%" PRIu64 " object=%" PRIu64 " payload=%" PRIu64 " offset=%" PRIu64 " conversions=%" PRIu64
               "; of them new at the record: type=%" PRIu64 " object=%" PRIu64 " payload=%" PRIu64 " (an operand's where it fits: %" PRIu64
               "; an operand's, its rule not run again: %" PRIu64 ") offset=%" PRIu64 " conversions=%" PRIu64 "; read by T=%" PRIu64 " C=%" PRIu64 " X=%" PRIu64 "\n",
               kinds[kind], read[0], read[1], read[2], read[3], read[4], fresh[0], fresh[1], fresh[2], counts->kind_fits[kind], counts->kind_unseen[kind], fresh[3],
               fresh[4], by_reader[BUSTER_NODE_READS_T], by_reader[BUSTER_NODE_READS_C], by_reader[BUSTER_NODE_READS_X]);
    }
    for (uint32_t op = 0; op < BUSTER_NODE_READS_OPS; op += 1)
    {
        const char* name = op == BUSTER_NODE_READS_LITERAL ? "literal" : op == BUSTER_NODE_READS_DISCARD ? "discard" : op == CHECK_OP_VALUE ? "identifier" :
                           lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?";
        if (counts->records[op] != 0)
        {
            const uint64_t* by = counts->classes[op];
            printf("node-reads: op=%s records=%" PRIu64 " measured=%" PRIu64 " none=%" PRIu64 " named=%" PRIu64 " copied=%" PRIu64 " new=%" PRIu64 " (%.2f%%)\n", name,
                   counts->records[op], counts->measured[op], by[0], by[1], by[2], by[3], node_census_share(by[3], counts->measured[op]));
            for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
            {
                bool any = false;
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_BITS; bit += 1)
                {
                    any = any || counts->bits[reader][op][bit] != 0;
                }
                if (any)
                {
                    printf("node-reads: op=%s reader=%s", name, readers[reader]);
                    for (uint32_t bit = 0; bit < BUSTER_NODE_READS_BITS; bit += 1)
                    {
                        if (counts->bits[reader][op][bit] != 0)
                        {
                            printf(" %s=%" PRIu64, bits[bit], counts->bits[reader][op][bit]);
                        }
                    }
                    printf("\n");
                }
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_FLAG_BITS; bit += 1)
                {
                    if (counts->flag_bits[reader][op][bit] != 0)
                    {
                        printf("node-reads: op=%s reader=%s flag=%s records=%" PRIu64 "\n", name, readers[reader], flags[bit], counts->flag_bits[reader][op][bit]);
                    }
                }
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_CONVERSION_BITS; bit += 1)
                {
                    if (counts->conversion_bits[reader][op][bit] != 0)
                    {
                        printf("node-reads: op=%s reader=%s conversion-flag=%s records=%" PRIu64 "\n", name, readers[reader], conversions[bit],
                               counts->conversion_bits[reader][op][bit]);
                    }
                }
            }
            for (uint32_t k = 0; k < BUSTER_NODE_READS_PAYLOADS; k += 1)
            {
                if (counts->payload_values[op][k] != 0)
                {
                    printf("node-reads: op=%s payload=%s decides for C or X: records=%" PRIu64 "\n", name, payloads[k], counts->payload_values[op][k]);
                }
            }
            for (uint32_t field = 0; field < BUSTER_NODE_READS_FIELDS; field += 1)
            {
                for (uint32_t source = 0; source < BUSTER_NODE_READS_SOURCES; source += 1)
                {
                    if (counts->sources[field][op][source] != 0)
                    {
                        printf("node-reads: op=%s field=%s read, its source=%s: records=%" PRIu64 "\n", name, fields[field], sources[source],
                               counts->sources[field][op][source]);
                    }
                }
            }
        }
    }
}

// --facts' report: the records by kind, what the kernel's rules made of them and left, the differences, the words, and
// the kernel's time a record its rules made.
static void node_shadow_facts_report(const NodeShadow* shadow)
{
    uint64_t lookups = 0;
    uint64_t given = 0;
    for (uint32_t lookup = 0; lookup < BUSTER_NODE_FACTS_LOOKUPS; lookup += 1)
    {
        lookups += shadow->facts_lookups[lookup];
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        given += shadow->facts_given[op];
    }
    // A lean kernel makes no discard: its lookups are the others.
    static const char* const variants[BUSTER_NODE_FACTS_MODES] = { "whole", "lean", "loads" };
    const bool lean = shadow->facts_mode != BUSTER_NODE_FACTS_WHOLE;
    const uint64_t made = lookups - (lean ? shadow->facts_lookups[BUSTER_NODE_FACTS_LOOKUPS - 1] : 0);
    const uint64_t taken = shadow->facts_taken_leaves + shadow->facts_taken_lookups;
    printf("node-facts: variant=%s records=%" PRIu64 " identifiers=%" PRIu64 " (enumerators=%" PRIu64 " functions=%" PRIu64 " static-objects=%" PRIu64
           " automatic-objects=%" PRIu64 ") literals=%" PRIu64 " lookups=%" PRIu64 " given=%" PRIu64 "\n",
           variants[shadow->facts_mode < BUSTER_NODE_FACTS_MODES ? shadow->facts_mode : 0], shadow->facts_records, shadow->facts_identifiers, shadow->facts_named[0],
           shadow->facts_named[1], shadow->facts_named[2], shadow->facts_named[3], shadow->facts_literals, made, given + (lookups - made));
    printf("node-facts: taken identifiers=%" PRIu64 " (%.2f%%) lookups=%" PRIu64 " (%.2f%%) differences=%" PRIu64 " list-differences=%" PRIu64 " failures=%" PRIu64
           " (differences over decoys %" PRIu64 ")\n",
           shadow->facts_taken_leaves, node_census_share(shadow->facts_taken_leaves, shadow->facts_identifiers), shadow->facts_taken_lookups,
           node_census_share(shadow->facts_taken_lookups, made), shadow->facts_differences, shadow->facts_list_differences, shadow->facts_failures,
           shadow->facts_decoy_differences);
    printf("node-facts: lookups by a short path=%" PRIu64 " (%.2f%%) by their rule=%" PRIu64 "\n", shadow->facts_short_lookups,
           node_census_share(shadow->facts_short_lookups, made), shadow->facts_taken_lookups - shadow->facts_short_lookups);
    if (lean)
    {
        printf("node-facts: left out, and what the rule makes of each from the kernel's records: conversions=%" PRIu64 " derived=%" PRIu64 "; discards=%" PRIu64
               " derived=%" PRIu64 "\n",
               shadow->facts_conversions, shadow->facts_conversions_derived, shadow->facts_discards, shadow->facts_discards_derived);
    }
    if (shadow->facts_mode == BUSTER_NODE_FACTS_LOADS)
    {
        printf("node-facts: loads: lookups by their load alone=%" PRIu64 " (%.2f%%)\n", shadow->facts_loads, node_census_share(shadow->facts_loads, made));
    }
    for (uint32_t lookup = 0; lookup < BUSTER_NODE_FACTS_LOOKUPS - (lean ? 1u : 0u); lookup += 1)
    {
        printf("node-facts: lookup=%s records=%" PRIu64 " left=%" PRIu64 " (%.2f%%)\n", node_facts_lookup_names[lookup], shadow->facts_lookups[lookup],
               shadow->facts_left_lookups[lookup], node_census_share(shadow->facts_left_lookups[lookup], shadow->facts_lookups[lookup]));
    }
    uint64_t operators = 0;
    uint64_t operators_taken = 0;
    uint64_t operators_short = 0;
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        operators += shadow->facts_operator_records[op];
        operators_taken += shadow->facts_operator_taken[op];
        operators_short += shadow->facts_operator_short[op];
    }
    if (shadow->facts_operators)
    {
        // The operators' kernel's: the operators that are no lookup, a literal's leaf from its line, and of the records
        // that are no literal's leaf, those the kernel made.
        const uint64_t named = shadow->facts_records - shadow->facts_literals;
        const uint64_t kernel = taken + operators_taken;
        printf("node-facts: operators records=%" PRIu64 " taken=%" PRIu64 " (%.2f%%) by a class's loop=%" PRIu64 " (%.2f%%) by their rules=%" PRIu64
               "; literals' lines=%" PRIu64 " taken=%" PRIu64 "; pointer-conversion slots=%" PRIu64 "\n",
               operators, operators_taken, node_census_share(operators_taken, operators), operators_short, node_census_share(operators_short, operators),
               operators_taken - operators_short, shadow->facts_literal_lines, shadow->facts_taken_lines, shadow->facts_pairs);
        printf("node-facts: records that are no literal=%" PRIu64 " made by the kernel=%" PRIu64 " (%.2f%%)\n", named, kernel, node_census_share(kernel, named));
        for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
        {
            if (shadow->facts_operator_records[op] != 0)
            {
                printf("node-facts: operator op=%s records=%" PRIu64 " left=%" PRIu64 " (%.2f%%) by-rule=%" PRIu64 "\n",
                       lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", shadow->facts_operator_records[op],
                       shadow->facts_operator_records[op] - shadow->facts_operator_taken[op],
                       node_census_share(shadow->facts_operator_records[op] - shadow->facts_operator_taken[op], shadow->facts_operator_records[op]),
                       shadow->facts_operator_taken[op] - shadow->facts_operator_short[op]);
            }
        }
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        if (shadow->facts_given[op] != 0)
        {
            printf("node-facts: given op=%s records=%" PRIu64 " (%.2f%%)\n", lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", shadow->facts_given[op],
                   node_census_share(shadow->facts_given[op], shadow->facts_records));
        }
    }
    printf("node-facts: words rows=%" PRIu64 " (%.1f ticks each) lines=%" PRIu64 " (%.1f ticks each; %" PRIu64 " a declaration's line made again) member-slots=%" PRIu64
           " windows=%" PRIu64 " widest=%" PRIu64 "\n",
           shadow->facts_rows, shadow->facts_rows != 0 ? (double)shadow->facts_row_ticks / (double)shadow->facts_rows : 0.0, shadow->facts_lines,
           shadow->facts_lines != 0 ? (double)shadow->facts_line_ticks / (double)shadow->facts_lines : 0.0, shadow->facts_line_versions, shadow->facts_members,
           shadow->facts_windows, shadow->facts_widest);
    if (shadow->passes != 0 && taken != 0)
    {
        // The kernel's own: a pass less the same windows' given records put in place alone. A tick's cycles are the pass's.
        const uint64_t cycles = shadow->facts_cycles > shadow->facts_given_cycles ? shadow->facts_cycles - shadow->facts_given_cycles : 0;
        const uint64_t instructions = shadow->facts_instructions > shadow->facts_given_instructions ? shadow->facts_instructions - shadow->facts_given_instructions : 0;
        const uint64_t mispredicts = shadow->facts_mispredicts > shadow->facts_given_mispredicts ? shadow->facts_mispredicts - shadow->facts_given_mispredicts : 0;
        const double tick = shadow->facts_ticks != 0 ? (double)shadow->facts_cycles / (double)shadow->facts_ticks : 0.0;
        printf("node-facts: the kernel a taken leaf or lookup: cycles=%.2f instructions=%.2f mispredicted-branches=%.3f (a pass less its windows' given records put in "
               "place: %.2f cycles and %.2f instructions a record of the window)\n",
               (double)cycles / (double)taken, (double)instructions / (double)taken, (double)mispredicts / (double)taken,
               (double)shadow->facts_given_cycles / (double)shadow->facts_records, (double)shadow->facts_given_instructions / (double)shadow->facts_records);
        // The two passes apart (#2872). The figure above is their difference, and the pass that is taken off carries a
        // constant a unit that the build fixes: on units of a hundred records it moved the difference by a tenth to a
        // third between two builds of one kernel. The first pass's own figure is what a change of the kernel moves.
        printf("node-facts: the two passes, a record of the windows: the given records put in place and the kernel: cycles=%.2f instructions=%.2f ticks=%.2f; the given "
               "records alone: cycles=%.2f instructions=%.2f ticks=%.2f\n",
               (double)shadow->facts_cycles / (double)shadow->facts_records, (double)shadow->facts_instructions / (double)shadow->facts_records,
               (double)shadow->facts_ticks / (double)shadow->facts_records, (double)shadow->facts_given_cycles / (double)shadow->facts_records,
               (double)shadow->facts_given_instructions / (double)shadow->facts_records, (double)shadow->facts_given_ticks / (double)shadow->facts_records);
        if (shadow->facts_operators)
        {
            // T whole: the kernel's time over the unit, a record it made, a record of the windows, and an evaluated
            // node as the sweeps' own lines count them.
            const uint64_t all = taken + operators_taken + shadow->facts_taken_lines;
            printf("node-facts: the operators' kernel a unit: cycles=%" PRIu64 " instructions=%" PRIu64 " mispredicted-branches=%" PRIu64
                   "; a record it made: cycles=%.2f; a record of the windows: cycles=%.2f instructions=%.2f mispredicted-branches=%.3f; an evaluated node: cycles=%.2f "
                   "instructions=%.2f\n",
                   cycles, instructions, mispredicts, all != 0 ? (double)cycles / (double)all : 0.0, (double)cycles / (double)shadow->facts_records,
                   (double)instructions / (double)shadow->facts_records, (double)mispredicts / (double)shadow->facts_records,
                   shadow->timed_nodes != 0 ? (double)cycles / (double)shadow->timed_nodes : 0.0,
                   shadow->timed_nodes != 0 ? (double)instructions / (double)shadow->timed_nodes : 0.0);
        }
        printf("node-facts: the kernel's loops, TSC ticks (%.3f cycles a tick): a leaf=%.2f a lookup=%.2f; a window's second run: a leaf=%.2f a lookup=%.2f\n", tick,
               shadow->facts_taken_leaves != 0 ? (double)shadow->facts_loop_ticks[0] / (double)shadow->facts_taken_leaves : 0.0,
               shadow->facts_taken_lookups != 0 ? (double)shadow->facts_loop_ticks[1] / (double)shadow->facts_taken_lookups : 0.0,
               shadow->facts_taken_leaves != 0 ? (double)shadow->facts_warm[0] / (double)shadow->facts_taken_leaves : 0.0,
               shadow->facts_taken_lookups != 0 ? (double)shadow->facts_warm[1] / (double)shadow->facts_taken_lookups : 0.0);
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
    printf("node-shadow: fusion evaluated=%" PRIu64 " unevaluated=%" PRIu64 " (under an unevaluated consumer %" PRIu64 ") inherited-context=%" PRIu64 " (not a value %" PRIu64
           ") calls=%" PRIu64 " (below a call %" PRIu64 ") segments-or-splits=%" PRIu64 " updates=%" PRIu64 " labeled-expressions=%" PRIu64
           " expressions-settled-later=%" PRIu64 " of %" PRIu64 "\n",
           shadow->fusion_evaluated, shadow->fusion_unevaluated, shadow->fusion_wasted, shadow->fusion_inherited, shadow->fusion_inherited_other, shadow->fusion_calls,
           shadow->fusion_nested_calls, shadow->fusion_hooked, shadow->fusion_updates, shadow->labeled_expressions, shadow->fusion_settled_expressions,
           shadow->taken_expressions);
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
    if (shadow->fused)
    {
        printf("node-shadow: fused expressions=%" PRIu64 " differences=%" PRIu64 "\n", shadow->fused_expressions, shadow->fused_differences);
    }
    if (shadow->front != 0)
    {
        if (shadow->front_unread != 0)
        {
            printf("node-shadow: front not run in %" PRIu64 " of %" PRIu64 " units: the reader's coded walk didn't read them whole%s\n", shadow->front_unread,
                   shadow->front_unread + shadow->front_units, BUSTER_HOST_VECTOR ? "" : " (this build has no vector paths, and reads by the scalar walk)");
        }
    }
    if (shadow->front != 0 && shadow->front_units != 0)
    {
        uint64_t left = 0;
        for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
        {
            left += shadow->front_left[code];
        }
        printf("node-shadow: front %s%s%s tokens=%" PRIu64 " left=%" PRIu64 " records=%" PRIu64 " compared=%" PRIu64 " differences=%" PRIu64 " resets=%" PRIu64
               " waiting-at-the-end=%" PRIu64 "\n",
               (shadow->front & BUSTER_NODE_FRONT_ORDER) == BUSTER_NODE_FRONT_BEFORE ? "before-the-walk" : "after-the-walk",
               (shadow->front & BUSTER_NODE_FRONT_LEAN) != 0 ? " lean" : "", (shadow->front & BUSTER_NODE_FRONT_WARM) != 0 ? " warm" : "", shadow->front_tokens, left,
               shadow->front_records, shadow->front_checked, shadow->front_differences, shadow->front_resets, shadow->front_waiting);
        for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
        {
            if (shadow->front_acting[code] != 0)
            {
                printf("node-shadow: front class=%s tokens=%" PRIu64 " left=%" PRIu64 "\n",
                       check_input_expression_code_names[code] != NULL ? check_input_expression_code_names[code] : "?", shadow->front_acting[code], shadow->front_left[code]);
            }
        }
        printf("node-shadow: front left sizeof-and-alignof tokens=%" PRIu64 "\n", shadow->front_left_queries);
        printf("node-shadow: front taken-expressions=%" PRIu64 " (%.2f%%) evaluated-nodes=%" PRIu64 " (%.2f%%)\n", shadow->front_expressions,
               node_census_share(shadow->front_expressions, shadow->taken_expressions), shadow->front_nodes, node_census_share(shadow->front_nodes, shadow->taken_nodes));
        printf("node-shadow: front operands=%" PRIu64 " within-%u=%" PRIu64 " (%.3f%%) in-a-byte=%" PRIu64 " further=%" PRIu64 " arguments=%" PRIu64 " (past %u: %" PRIu64
               ") roots'=%" PRIu64 " (past %u: %" PRIu64 ")\n",
               shadow->front_operands, BUSTER_NODE_FRONT_NEAR, shadow->front_operands_near, node_census_share(shadow->front_operands_near, shadow->front_operands),
               shadow->front_operands_byte, shadow->front_operands_far, shadow->front_arguments, BUSTER_NODE_FRONT_NEAR, shadow->front_arguments_past,
               shadow->front_root_operands, BUSTER_NODE_FRONT_NEAR, shadow->front_root_operands_past);
        printf("node-shadow: front taken-expressions with an operand past %u records back=%" PRIu64 " (%.3f%%) evaluated-nodes=%" PRIu64 " (%.3f%%)\n", BUSTER_NODE_FRONT_NEAR,
               shadow->front_past_expressions, node_census_share(shadow->front_past_expressions, shadow->taken_expressions), shadow->front_past_nodes,
               node_census_share(shadow->front_past_nodes, shadow->taken_nodes));
        printf("node-shadow: front structure records-with-more-than-%u-operands=%" PRIu64 " operands-at-a-place-past-%u=%" PRIu64 "\n", BUSTER_NODE_FRONT_COUNT_MOST,
               shadow->front_counts_past, BUSTER_NODE_FRONT_PLACE_MOST, shadow->front_places_past);
        printf("node-shadow: front ticks=%" PRIu64 "\n", shadow->front_ticks);
        printf("node-shadow: walk prepare ticks compact=%" PRIu64 " identifiers=%" PRIu64 " literals=%" PRIu64 "\n", shadow->front_compact_ticks,
               shadow->front_identifier_ticks, shadow->front_literal_ticks);
    }
    if ((shadow->front & BUSTER_NODE_FRONT_TYPES) != 0 && shadow->front_units != 0)
    {
        // T at reader time: what it ran on and made, then how it compares with the shadow's T after the pipeline.
        printf("node-shadow: reader-time T expressions=%" PRIu64 " records=%" PRIu64 " typed=%" PRIu64 " taken=%" PRIu64 " roots-not-run=%" PRIu64 " late-types=%" PRIu64
               " ticks=%" PRIu64 " setup-ticks=%" PRIu64 "\n",
               shadow->typed_expressions, shadow->typed_records, shadow->typed_typed, shadow->typed_refused[NODE_MACHINE_TAKEN], shadow->typed_skipped,
               shadow->typed_late_types, shadow->typed_ticks, shadow->typed_setup_ticks);
        for (uint32_t refusal = NODE_MACHINE_TAKEN + 1; refusal < NODE_MACHINE_REFUSALS; refusal += 1)
        {
            if (shadow->typed_refused[refusal] != 0)
            {
                printf("node-shadow: reader-time T refused=%s expressions=%" PRIu64 "\n", node_machine_refusal_names[refusal], shadow->typed_refused[refusal]);
            }
        }
        printf("node-shadow: reader-time T lowered=%" PRIu64 " (%.2f%%) never-lowered=%" PRIu64 " (taken %" PRIu64 ", context %" PRIu64 "; of them in a lowered body: taken %"
               PRIu64 ", context %" PRIu64 ") declarations'-discards-not-run=%" PRIu64 " compared=%" PRIu64
               " (records %" PRIu64 ") differences=%" PRIu64 " (range %" PRIu64 ", refusal %" PRIu64 ") taken-expressions-the-front-left=%" PRIu64
               " the-front's-not-run=%" PRIu64 "\n",
               shadow->typed_popped, node_census_share(shadow->typed_popped, shadow->typed_expressions), shadow->typed_expressions - shadow->typed_popped,
               shadow->typed_unpopped[NODE_MACHINE_TAKEN], shadow->typed_unpopped[NODE_MACHINE_REFUSED_CONTEXT], shadow->typed_unpopped_inside[NODE_MACHINE_TAKEN],
               shadow->typed_unpopped_inside[NODE_MACHINE_REFUSED_CONTEXT], shadow->typed_discards, shadow->typed_compared, shadow->typed_compared_records,
               shadow->typed_differences, shadow->typed_range_differences, shadow->typed_refusal_differences, shadow->typed_missing, shadow->typed_unrun);
        printf("node-shadow: reader-time T objects=%" PRIu64 " differences=%" PRIu64 " library-calls=%" PRIu64 " differences=%" PRIu64 "\n", shadow->typed_objects,
               shadow->typed_object_differences, shadow->typed_library_calls, shadow->typed_library_differences);
    }
    if ((shadow->front & BUSTER_NODE_FRONT_SWEEPS) != 0 && shadow->front_units != 0)
    {
        // C and X at reader time: what they ran on and made (the ticks above are then T's, C's and X's together), then how
        // they compare with the shadow's own after the pipeline.
        if ((shadow->reader & BUSTER_NODE_READER_TESTS) != 0)
        {
            printf("node-shadow: reader-time tests expressions=%" PRIu64 " refused=%" PRIu64 " nodes-outside-the-records=%" PRIu64 " units-whose-enabled-warnings-differ=%" PRIu64 "\n",
                   shadow->tested_expressions, shadow->tested_refused, shadow->tested_missed, shadow->tested_policy_differences);
            for (uint32_t t = 0; t < NODE_CENSUS_TESTS; t += 1)
            {
                if (shadow->tested_any[t] != 0)
                {
                    printf("node-shadow: reader-time tests refused test=%s expressions=%" PRIu64 " first=%" PRIu64 "\n", node_census_test_names[t], shadow->tested_any[t],
                           shadow->tested_first[t]);
                }
            }
            printf("node-shadow: reader-time tests compared=%" PRIu64 " never-lowered=%" PRIu64 " taken-there=%" PRIu64 " (nodes %" PRIu64 ") taken-there-and-refused-by-the-census=%" PRIu64
                   " (for the lowering's facts alone %" PRIu64 ") differences=%" PRIu64 " root-kind-differences=%" PRIu64 " refused-by-the-census=%" PRIu64 " (of them past C and X %"
                   PRIu64 ", nodes %" PRIu64 ")\n",
                   shadow->tested_compared, shadow->tested_compared_unpopped, shadow->tested_taken, shadow->tested_taken_nodes, shadow->tested_unsound, shadow->tested_lowering,
                   shadow->tested_differences, shadow->tested_kind_differences, shadow->tested_census_refused, shadow->tested_census_taken_refused,
                   shadow->tested_census_taken_refused_nodes);
            for (uint32_t t = 0; t < NODE_CENSUS_TESTS; t += 1)
            {
                if (shadow->tested_reader_alone[t] != 0 || shadow->tested_census_alone[t] != 0)
                {
                    printf("node-shadow: reader-time tests test=%s reader-alone=%" PRIu64 " census-alone=%" PRIu64 "\n", node_census_test_names[t], shadow->tested_reader_alone[t],
                           shadow->tested_census_alone[t]);
                }
            }
        }
        if ((shadow->reader & BUSTER_NODE_READER_VERIFY) != 0)
        {
            printf("node-shadow: reader-time verify nodes-whose-value-is-not-the-action's=%" PRIu64 " nodes-whose-action-warns=%" PRIu64 "\n", shadow->reader_twin_differences,
                   shadow->reader_twin_warnings);
        }
        if ((shadow->reader & BUSTER_NODE_READER_OWN) != 0)
        {
            // The tests in the machine's own form: what its pass sent to the census's code, and by what.
            printf("node-shadow: own tests expressions=%" PRIu64 " records=%" PRIu64 " expressions-with-a-record-sent=%" PRIu64 " (%.2f%%) records-sent=%" PRIu64
                   " (%.2f%%) walks=%" PRIu64 " (%.2f%%)%s differences=%" PRIu64 " walks-of-another-count=%" PRIu64 "\n",
                   shadow->own_expressions, shadow->own_records, shadow->own_visited, node_census_share(shadow->own_visited, shadow->own_expressions), shadow->own_visits,
                   node_census_share(shadow->own_visits, shadow->own_records), shadow->own_walks, node_census_share(shadow->own_walks, shadow->own_expressions),
                   (shadow->reader & BUSTER_NODE_READER_BOTH) != 0 ? " compared-with-the-census's-code" : "", shadow->own_differences, shadow->own_count_differences);
            printf("node-shadow: own tests listed leaves=%" PRIu64 " (%.2f%%) tree=%" PRIu64 " (%.2f%%) operators=%" PRIu64 " (%.2f%%: calls %" PRIu64
                   ", with a constant operand %" PRIu64 ", with a third operand %" PRIu64 ")\n",
                   shadow->own_leaves, node_census_share(shadow->own_leaves, shadow->own_records), shadow->own_trees, node_census_share(shadow->own_trees, shadow->own_records),
                   shadow->own_operators, node_census_share(shadow->own_operators, shadow->own_records), shadow->own_calls, shadow->own_constants, shadow->own_thirds);
            for (uint32_t t = 0; t < NODE_TESTS_TRIGGERS; t += 1)
            {
                if (shadow->own_triggers[t] != 0)
                {
                    printf("node-shadow: own tests sent-by=%s %s=%" PRIu64 " (%.2f%%)\n", node_tests_trigger_names[t],
                           t == NODE_TESTS_TRIGGER_UNSEQUENCED ? "expressions" : "records", shadow->own_triggers[t],
                           node_census_share(shadow->own_triggers[t], t == NODE_TESTS_TRIGGER_UNSEQUENCED ? shadow->own_expressions : shadow->own_records));
                }
            }
        }
        printf("node-shadow: reader-time C and X expressions=%" PRIu64 " taken=%" PRIu64 " entries=%" PRIu64 " tapes-not-kept=%" PRIu64 "\n", shadow->swept_expressions,
               shadow->swept_refused[NODE_MACHINE_TAKEN], shadow->swept_entries, shadow->swept_unkept);
        for (uint32_t refusal = NODE_MACHINE_TAKEN + 1; refusal < NODE_MACHINE_REFUSALS; refusal += 1)
        {
            if (shadow->swept_refused[refusal] != 0)
            {
                printf("node-shadow: reader-time C and X refused=%s expressions=%" PRIu64 "\n", node_machine_refusal_names[refusal], shadow->swept_refused[refusal]);
            }
        }
        printf("node-shadow: reader-time C and X compared=%" PRIu64 " (entries %" PRIu64 ") differences=%" PRIu64 " (root %" PRIu64 ", refusal %" PRIu64 ", tape %" PRIu64
               ", labels-and-calls %" PRIu64 ") patched: globals'-entries=%" PRIu64 " direct-calls=%" PRIu64 "\n",
               shadow->swept_compared, shadow->swept_compared_entries, shadow->swept_differences, shadow->swept_root_differences, shadow->swept_refusal_differences,
               shadow->swept_tape_differences, shadow->swept_side_differences, shadow->swept_references, shadow->swept_call_targets);
    }
    // The reader's coded walk in the files' one reading, by its parts (TSC ticks): beside the front's where it ran, and
    // alone where it didn't, the walk as it is.
    printf("node-shadow: walk ticks classes=%" PRIu64 " prepare=%" PRIu64 " actions=%" PRIu64 " visits=%" PRIu64 "\n",
           shadow->walk_ticks[BUSTER_CHECK_EXPRESSION_PART_CLASSES], shadow->walk_ticks[BUSTER_CHECK_EXPRESSION_PART_PREPARE],
           shadow->walk_ticks[BUSTER_CHECK_EXPRESSION_PART_ACTIONS], shadow->walk_ticks[BUSTER_CHECK_EXPRESSION_PART_VISITS]);
    static const char* const sweep_names[BUSTER_NODE_SHADOW_SWEEPS] = { "T", "T+C", "T+C+X", "T+CX", "walk", "walk;T+C+X", "walk;T+CX" };
    const bool timed[BUSTER_NODE_SHADOW_SWEEPS] = { true, true, true, shadow->fused, shadow->interleave, shadow->interleave, shadow->interleave && shadow->fused };
    for (uint32_t k = 0; shadow->timed_nodes != 0 && k < BUSTER_NODE_SHADOW_SWEEPS; k += 1)
    {
        if (timed[k])
        {
            printf("node-shadow: time sweeps=%s passes=%u repeat=%u cycles=%" PRIu64 " instructions=%" PRIu64 " ticks=%" PRIu64 " per-evaluated-node: cycles=%.1f "
                   "instructions=%.1f ticks=%.1f%s\n",
                   sweep_names[k], shadow->passes, shadow->repeat, shadow->cycles[k], shadow->instructions[k], shadow->ticks[k],
                   (double)shadow->cycles[k] / (double)shadow->timed_nodes, (double)shadow->instructions[k] / (double)shadow->timed_nodes,
                   (double)shadow->ticks[k] / (double)shadow->timed_nodes, shadow->counted ? "" : " (no cycle counter)");
            if (k > BUSTER_NODE_SHADOW_SET_WALK)
            {
                printf("node-shadow: interleaved sweeps=%s chunk=%u sweeps' own ticks=%" PRIu64 " per-evaluated-node: ticks=%.1f\n", sweep_names[k], shadow->chunk,
                       shadow->inner[k], (double)shadow->inner[k] / (double)shadow->timed_nodes);
            }
        }
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
            NodeFrontTypes* typed = program->input->node_front != NULL ? program->input->node_front->types : NULL;
            if (typed != NULL && pops[p].node < typed->node_capacity)
            {
                // Reader time's T ran on it, or not, before the selection: the lowering popped this root.
                typed->refusals[pops[p].node] |= BUSTER_NODE_FRONT_TYPED_POPPED;
            }
            uint64_t tests = s->expression_tests[p] | node_census_expression(s, pops[p].node, pops[p].flags, context, &walked);
            tests |= region ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
            census->largest = walked > census->largest ? walked : census->largest;
            node_census_count(census, s, tests, s->expression_nodes[p], context, pops[p].node);
            if (census->shadow != NULL && typed != NULL)
            {
                node_shadow_tested(census->shadow, s, typed, pops[p].node, tests, context, s->expression_nodes[p], true);
            }
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
    lower_source_node_front = census->shadow != NULL ? census->shadow->front : 0;
    lower_source_node_reader = census->shadow != NULL ? census->shadow->reader : 0;
    const bool valid = lower_source_open(&source, file, options, option_count);
    lower_source_node_front = 0;
    lower_source_node_reader = 0;
    const bool prepared = source.prepared;
    CheckCorpusPath* path = &source.path;
    CheckProgramSelection selected = source.selected;
    const uint64_t log_capacity = prepared ? 4 * (uint64_t)selected.program.stream->count + 65536 : 0;
    // Each body's labels: its controls' and the ones its tasks number (the shadow's branches are read there).
    const uint64_t label_capacity = prepared ? 2 * (uint64_t)selected.program.control_count + selected.program.body_count + 4 * (uint64_t)selected.program.stream->count + 65536 : 0;
    // Each body's named result, two words a body (CheckLowerOwners.named): none until its body is lowered.
    const uint64_t named_capacity = prepared ? 2 * ((uint64_t)selected.program.body_count + 1) : 0;
    Arena log_arena = arena_reserve(log_capacity * sizeof(CheckLowerOwnerPop) + (label_capacity + named_capacity) * sizeof(uint32_t) + 8192);
    CheckLowerOwners owners = { .capacity = log_capacity, .label_capacity = label_capacity };
    owners.pops = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, CheckLowerOwnerPop, owners.capacity) : 0;
    owners.labels = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, uint32_t, owners.label_capacity) : 0;
    owners.named = prepared ? BUSTER_ARENA_PUSH_ARRAY(&log_arena, uint32_t, named_capacity) : 0;
    if (owners.named != 0)
    {
        memset(owners.named, 0xff, named_capacity * sizeof(uint32_t));
    }
    CheckProgramLowered lowered = { 0 };
    const bool lowering = prepared && owners.pops != 0 && owners.labels != 0 && owners.named != 0 &&
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
        uint32_t** node_columns[] = { &s.marks, &s.parents, &s.depths, &s.node_controls, &s.list, &s.stack, &s.node_pop };
        bool room = node_census_events(&s, path, &scratch);
        s.type_bits_count = selected.program.types->types.count;
        s.type_bits = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)s.type_bits_count + 1);
        s.declaration_facts = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)source.parser.tokens.count + 1);
        room = room && s.type_bits != NULL && s.declaration_facts != NULL;
        if (room)
        {
            memset(s.type_bits, 0, (uint64_t)s.type_bits_count + 1);
            memset(s.declaration_facts, 0, (uint64_t)source.parser.tokens.count + 1);
        }
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
        NodeFacts facts;
        NodeReads reads;
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
            s.consumers = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, BUSTER_NODE_MACHINE_MOST);
            s.fused_kinds = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, BUSTER_NODE_MACHINE_TAPE_MOST);
            s.fused_classes = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, BUSTER_NODE_MACHINE_TAPE_MOST);
            s.fused_tape = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, BUSTER_NODE_MACHINE_TAPE_MOST);
            s.fused_labels = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, 2 * BUSTER_NODE_MACHINE_MOST);
            s.fused_calls = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, BUSTER_NODE_MACHINE_MOST);
            shadow->timed_capacity = (uint32_t)(owners.count < UINT32_MAX ? owners.count : UINT32_MAX);
            shadow->timed = BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeShadowExpression, (uint64_t)shadow->timed_capacity + 1);
            shadow->timed_count = 0;
            room = s.object_locals != NULL && s.object_stamps != NULL && s.local_objects != NULL && s.local_stamps != NULL && s.temporary_locals != NULL && s.consumers != NULL &&
                   s.fused_kinds != NULL && s.fused_classes != NULL && s.fused_tape != NULL && s.fused_labels != NULL && s.fused_calls != NULL && shadow->timed != NULL &&
                   node_machine_begin(&machine, &selected.program, shadow->scalar, shadow->verify, &scratch);
            if (room)
            {
                memset(s.object_stamps, 0, (uint64_t)objects * sizeof(uint32_t));
                memset(s.local_stamps, 0, (uint64_t)locals * sizeof(uint32_t));
                s.machine = &machine;
            }
            if (room && shadow->facts)
            {
                room = node_facts_begin(&facts, &machine, nodes, shadow->facts_window, shadow->facts_packed, shadow->facts_mode != BUSTER_NODE_FACTS_WHOLE,
                                        shadow->facts_operators, &scratch);
                s.facts = room ? &facts : NULL;
            }
            if (room && shadow->facts_reads)
            {
                room = node_reads_begin(&reads, &facts, &shadow->reads, &scratch);
                s.reads = room ? &reads : NULL;
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
                    s.named_local = owners.named[2 * (uint64_t)b];
                    s.hidden_local = owners.named[2 * (uint64_t)b + 1];
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
                shadow->scratch = &s;
                shadow->twin_warnings += machine.twin_warnings;
                node_shadow_time(shadow, &machine, shadow->file_nodes);
                shadow->file_nodes = 0;
                if (s.facts != NULL)
                {
                    node_shadow_facts(shadow, &s);
                }
                const NodeFrontShadow* front = path->input.node_front;
                shadow->front_units += front != NULL;
                shadow->front_unread += shadow->front != 0 && front == NULL;
                if (front != NULL)
                {
                    shadow->front_ticks += front->ticks;
                    shadow->front_tokens += front->tokens;
                    shadow->front_records += front->records;
                    shadow->front_checked += front->checked;
                    shadow->front_resets += front->resets;
                    shadow->front_waiting += front->waiting;
                    shadow->front_operands += front->operands;
                    shadow->front_operands_near += front->operands_near;
                    shadow->front_operands_byte += front->operands_byte;
                    shadow->front_operands_far += front->operands_far;
                    shadow->front_root_operands += front->root_operands;
                    shadow->front_root_operands_past += front->root_operands_past;
                    shadow->front_arguments += front->arguments;
                    shadow->front_arguments_past += front->arguments_past;
                    shadow->front_counts_past += front->counts_past;
                    shadow->front_places_past += front->places_past;
                    shadow->front_compact_ticks += front->compact_ticks;
                    shadow->front_identifier_ticks += front->identifier_ticks;
                    shadow->front_literal_ticks += front->literal_ticks;
                    static const char* const fields[] = { "?", "node", "operator", "operand", "leaf", "member", "arguments", "type", "context", "distance", "structure" };
                    for (uint32_t d = 0; d < front->differences && d < BUSTER_NODE_FRONT_LISTED && shadow->front_listed < node_census_list; d += 1)
                    {
                        const uint32_t token = front->difference_tokens[d];
                        shadow->front_listed += 1;
                        printf("node-shadow: front difference file=%s line=%u token=%u field=%s\n", file, node_census_line(&source.pp, &source.parser, token), token,
                               fields[front->difference_fields[d] < BUSTER_ARRAY_LENGTH(fields) ? front->difference_fields[d] : 0]);
                    }
                    shadow->front_differences += front->differences;
                    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
                    {
                        shadow->front_acting[code] += front->acting[code];
                        shadow->front_left[code] += front->left[code];
                    }
                    shadow->front_left_queries += front->left_queries;
                }
                const NodeFrontTypes* typed = front != NULL ? front->types : NULL;
                if (typed != NULL)
                {
                    shadow->typed_expressions += typed->expressions;
                    shadow->typed_records += typed->records;
                    shadow->typed_typed += typed->typed;
                    shadow->typed_ticks += typed->ticks;
                    shadow->typed_setup_ticks += typed->setup_ticks;
                    shadow->typed_late_types += typed->late_types;
                    shadow->typed_skipped += typed->skipped;
                    shadow->typed_discards += typed->discards;
                    shadow->swept_expressions += typed->swept;
                    shadow->swept_entries += typed->swept_entries;
                    shadow->swept_unkept += typed->swept_unkept;
                    for (uint32_t refusal = 0; refusal < NODE_MACHINE_REFUSALS; refusal += 1)
                    {
                        shadow->typed_refused[refusal] += typed->refused[refusal];
                        shadow->swept_refused[refusal] += typed->swept_refused[refusal];
                    }
                    shadow->reader_twin_differences += typed->twin_differences;
                    shadow->reader_twin_warnings += typed->twin_warnings;
                    // The reader's two passes over a chunk each ran the own form and counted: a pass's counts are half.
                    shadow->own_expressions += typed->own_expressions / 2;
                    shadow->own_records += typed->own_records / 2;
                    shadow->own_visited += typed->own_visited / 2;
                    shadow->own_visits += typed->own_visits / 2;
                    shadow->own_walks += typed->own_walks / 2;
                    shadow->own_leaves += typed->own_leaves / 2;
                    shadow->own_trees += typed->own_trees / 2;
                    shadow->own_operators += typed->own_operators / 2;
                    shadow->own_calls += typed->own_calls / 2;
                    shadow->own_constants += typed->own_constants / 2;
                    shadow->own_thirds += typed->own_thirds / 2;
                    shadow->own_differences += typed->own_differences;
                    shadow->own_count_differences += typed->own_count_differences;
                    for (uint32_t t = 0; t < NODE_TESTS_TRIGGERS; t += 1)
                    {
                        shadow->own_triggers[t] += typed->own_triggers[t] / 2;
                    }
                    if (typed->tests != NULL)
                    {
                        shadow->tested_expressions += typed->tested;
                        shadow->tested_refused += typed->tested_refused;
                        shadow->tested_missed += typed->tested_missed;
                        for (uint32_t t = 0; t < NODE_CENSUS_TESTS; t += 1)
                        {
                            shadow->tested_any[t] += typed->tested_any[t];
                            shadow->tested_first[t] += typed->tested_first[t];
                        }
                        // The enabled warnings as reader time read them, against the pipeline's.
                        const NodeCensusPolicy policy = node_census_policy(&source.pp, selected.program.input);
                        shadow->tested_policy_differences += typed->policy == NULL || memcmp(typed->policy, &policy, sizeof(policy)) != 0;
                    }
                    const CheckProgram* program = &selected.program;
                    for (uint32_t node = 0; node < path->input.graph.count && node < typed->node_capacity; node += 1)
                    {
                        const bool popped = (typed->refusals[node] & BUSTER_NODE_FRONT_TYPED_POPPED) != 0;
                        const uint32_t refusal = typed->refusals[node] & BUSTER_NODE_FRONT_TYPED_REFUSAL;
                        shadow->typed_popped += typed->counts[node] != 0 && popped;
                        shadow->typed_unpopped[refusal < NODE_MACHINE_REFUSALS ? refusal : 0] += typed->counts[node] != 0 && !popped;
                        if (typed->counts[node] != 0 && !popped)
                        {
                            // In a body the lowering lowered, or not: by the bodies' braces.
                            const uint32_t token = path->input.graph.tokens[node];
                            bool inside = false;
                            for (uint32_t b = 0; b < program->body_count && !inside; b += 1)
                            {
                                const uint32_t opening = program->bodies[b].opening;
                                const uint32_t closing = opening < source.parser.tokens.count ? source.parser.tree.parents[opening] : 0;
                                inside = lowered.entry_tokens[b] != 0 && token > opening && token < closing;
                            }
                            shadow->typed_unpopped_inside[refusal < NODE_MACHINE_REFUSALS ? refusal : 0] += inside;
                        }
                        if (typed->tests != NULL && typed->counts[node] != 0 && !popped && (typed->refusals[node] & BUSTER_NODE_FRONT_TYPED_TESTED) != 0)
                        {
                            // A root the lowering never popped (a body the selection dropped): the census's tests on it
                            // now, from the stream, with the root's kind and its object's type as reader time gave them.
                            const NodeCensusRoot context = typed->kinds[node] < NODE_CENSUS_ROOTS ? (NodeCensusRoot)typed->kinds[node] : NODE_CENSUS_ROOT_OTHER;
                            uint32_t walked = 0;
                            s.given_declared = typed->declared[node];
                            uint64_t tests = node_census_expression(&s, node, 0, context, &walked);
                            s.given_declared = 0;
                            tests |= node_census_region_at(program, path->input.graph.tokens[node]) ? UINT64_C(1) << NODE_CENSUS_REGION : 0;
                            node_shadow_tested(shadow, &s, typed, node, tests, context, walked, false);
                        }
                        if (typed->library[node] != 0)
                        {
                            // Reader time's answer for the call against the program's, which reads the stream.
                            const bool same = (typed->library[node] == 2) == check_program_call_library(program, node);
                            shadow->typed_library_calls += 1;
                            shadow->typed_library_differences += !same;
                            if (!same && shadow->typed_listed < node_census_list)
                            {
                                shadow->typed_listed += 1;
                                printf("node-shadow: reader-time library call difference file=%s line=%u reader=%s\n", file,
                                       node_census_line(&source.pp, &source.parser, path->input.graph.tokens[node]), typed->library[node] == 2 ? "own" : "plain");
                            }
                        }
                    }
                    for (uint32_t object = 1; object < typed->object_count && object < program->object_count; object += 1)
                    {
                        // Reader time's columns against the program's: the last static declaration, returns_twice and an
                        // asm label.
                        const uint32_t declaration = program->object_declarations != NULL ? program->object_declarations[object] : BUSTER_CHECK_NONE;
                        const bool twice = program->object_returns_twice != NULL && program->object_returns_twice[object] != 0;
                        const bool labelled = program->object_labelled != NULL && program->object_labelled[object] != 0;
                        const uint8_t facts = typed->object_facts[object];
                        const bool same = typed->object_declarations[object] == declaration && ((facts & BUSTER_NODE_MACHINE_OBJECT_TWICE) != 0) == twice &&
                                          ((facts & BUSTER_NODE_MACHINE_OBJECT_LABELLED) != 0) == labelled;
                        shadow->typed_objects += declaration != BUSTER_CHECK_NONE || typed->object_declarations[object] != BUSTER_CHECK_NONE;
                        shadow->typed_object_differences += !same;
                        if (!same && shadow->typed_listed < node_census_list)
                        {
                            shadow->typed_listed += 1;
                            printf("node-shadow: reader-time object difference file=%s object=%u declaration=%u (program %u) twice=%u (%u) labelled=%u (%u)\n", file, object,
                                   typed->object_declarations[object], declaration, (facts & BUSTER_NODE_MACHINE_OBJECT_TWICE) != 0, twice,
                                   (facts & BUSTER_NODE_MACHINE_OBJECT_LABELLED) != 0, labelled);
                        }
                    }
                }
                for (uint32_t part = 0; part < BUSTER_CHECK_EXPRESSION_PARTS; part += 1)
                {
                    shadow->walk_ticks[part] += path->input.expression_part_ticks[part];
                }
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
        else if (strcmp(arguments[i], "--fused") == 0)
        {
            shadow.fused = true;
        }
        else if (strcmp(arguments[i], "--facts") == 0)
        {
            shadow.facts = true;
        }
        else if (strcmp(arguments[i], "--facts-packed") == 0)
        {
            shadow.facts = true;
            shadow.facts_packed = true;
        }
        else if (strcmp(arguments[i], "--facts-lean") == 0 || strcmp(arguments[i], "--facts-loads") == 0)
        {
            shadow.facts = true;
            shadow.facts_mode = strcmp(arguments[i], "--facts-lean") == 0 ? BUSTER_NODE_FACTS_LEAN : BUSTER_NODE_FACTS_LOADS;
        }
        else if (strcmp(arguments[i], "--facts-operators") == 0)
        {
            shadow.facts = true;
            shadow.facts_operators = true;
        }
        else if (strcmp(arguments[i], "--facts-reads") == 0)
        {
            shadow.facts = true;
            shadow.facts_operators = true;
            shadow.facts_reads = true;
        }
        else if (strcmp(arguments[i], "--facts-window") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number >= 16 && number <= BUSTER_NODE_FACTS_WINDOW;
            shadow.facts = true;
            shadow.facts_window = (uint32_t)number;
        }
        else if (strcmp(arguments[i], "--front") == 0 || strcmp(arguments[i], "--front-first") == 0)
        {
            shadow.front = (uint8_t)((shadow.front & ~BUSTER_NODE_FRONT_ORDER) | (strcmp(arguments[i], "--front") == 0 ? BUSTER_NODE_FRONT_AFTER : BUSTER_NODE_FRONT_BEFORE));
        }
        else if (strcmp(arguments[i], "--front-lean") == 0 || strcmp(arguments[i], "--front-warm") == 0 || strcmp(arguments[i], "--front-types") == 0 ||
                 strcmp(arguments[i], "--front-sweeps") == 0)
        {
            // The lean front, the front with its arrays touched beforehand, T at reader time on the front's expressions,
            // or T, C and X there: after the walk unless --front-first says before.
            const uint8_t flag = strcmp(arguments[i], "--front-lean") == 0   ? BUSTER_NODE_FRONT_LEAN :
                                 strcmp(arguments[i], "--front-warm") == 0   ? BUSTER_NODE_FRONT_WARM :
                                 strcmp(arguments[i], "--front-types") == 0 ? BUSTER_NODE_FRONT_TYPES :
                                                                               BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS;
            shadow.front = (uint8_t)(shadow.front | flag | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
        }
        else if (strcmp(arguments[i], "--front-tests") == 0 || strcmp(arguments[i], "--front-own-tests") == 0)
        {
            // The census's tests at reader time, between T and C and X there: by the census's code on the view, or in the
            // machine's own form.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS | (strcmp(arguments[i], "--front-own-tests") == 0 ? BUSTER_NODE_READER_OWN : 0);
        }
        else if (strcmp(arguments[i], "--front-tests-last") == 0)
        {
            // The tests after C and X at reader time, on what those take.
            shadow.reader |= BUSTER_NODE_READER_LAST;
        }
        else if (strcmp(arguments[i], "--interleave") == 0)
        {
            uint64_t number = 0;
            valid = i + 1 < count && check_corpus_number(arguments[++i], &number) && number <= 65536;
            shadow.interleave = true;
            shadow.chunk = (uint32_t)number;
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
    // The operators' kernel makes whole or lean records: the loads' kernel is a lookup's alone. The reads' census runs the
    // kernel's rules on whole records.
    valid = valid && file_count != 0 && (shadowed || (!shadow.scalar && !shadow.verify && !shadow.fused && !shadow.interleave && !shadow.facts)) &&
            !(shadow.facts_operators && shadow.facts_mode == BUSTER_NODE_FACTS_LOADS) && !(shadow.facts_reads && shadow.facts_mode != BUSTER_NODE_FACTS_WHOLE);
    // --verify with T at reader time checks its rows and rules against the action there too.
    shadow.reader |= shadow.verify && (shadow.front & BUSTER_NODE_FRONT_TYPES) != 0 ? BUSTER_NODE_READER_VERIFY : 0;
    shadow.reader |= shadow.verify && (shadow.reader & BUSTER_NODE_READER_OWN) != 0 ? BUSTER_NODE_READER_BOTH : 0;
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
        if (shadow.facts)
        {
            node_shadow_facts_report(&shadow);
        }
        if (shadow.facts_reads)
        {
            node_shadow_reads_report(&shadow);
        }
    }
    else
    {
        fprintf(stderr, "usage: compiler measure-node-machine [--list N] [--shadow [--scalar] [--verify] [--facts | --facts-lean | --facts-loads] [--facts-operators] [--facts-reads] [--facts-packed] [--facts-window N] [--passes N] [--repeat N]] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    const bool differences = shadowed && (shadow.value_differences != 0 || shadow.entry_differences != 0 || shadow.twin_differences != 0 || shadow.fused_differences != 0 ||
                                          shadow.facts_differences != 0 || shadow.facts_list_differences != 0 || shadow.facts_failures != 0 ||
                                          shadow.facts_conversions != shadow.facts_conversions_derived || shadow.reads.same_differences != 0 ||
                                          shadow.reads.misplaced != 0 || shadow.front_differences != 0 || shadow.typed_differences != 0 ||
                                          shadow.typed_object_differences != 0 || shadow.typed_library_differences != 0 || shadow.swept_differences != 0 ||
                                          shadow.tested_unsound != 0 || shadow.tested_differences != 0 || shadow.tested_kind_differences != 0 ||
                                          shadow.tested_missed != 0 || shadow.tested_policy_differences != 0 || shadow.reader_twin_differences != 0 ||
                                          shadow.own_differences != 0);
    return valid && census.failures == 0 && census.files != 0 && !differences ? 0 : 1;
}

#endif
