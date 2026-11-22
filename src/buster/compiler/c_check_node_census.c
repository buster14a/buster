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
//                                       [--front-warm] [--front-types] [--front-sweeps] [--front-tests] [--front-tests-last] [--passes N]
//                                       [--front-facts] [--front-window] [--window-tests] [--window-contexts] [--window-rows [--window-counter FAMILY EVENT]] [--repeat N]
//                                       [--front-alone | --front-alone-split | --front-alone-first] [--bodies] [--statement-controls]]
//                                       [preprocessor options]
//                                       file...
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
// This is a measurement outside the compile path: it runs the scalar task loop and reads the finished columns. Its one
// vector path is the tests on the window (--shadow --window-tests, node_tests_window_chunk): the machine's own form of
// the set's and the silence tests, whose loop is 64 records a step over the window the front wrote and the kernel made.
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
    NODE_CENSUS_ORDER,
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
    [NODE_CENSUS_ORDER] = "pointer-integer-order",
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
    [CHECK_WARNING_ID_CONSTANT_LOGICAL_OPERAND] = NODE_CENSUS_WARNING_TESTED,
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
    // An ordered comparison of a pointer with an integer (CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, #3201).
    bool ordered;
    bool void_dereference;
    bool string_plus_int;
    bool packed_member;
    bool unevaluated;
    bool alignof_expression;
    bool sizeof_decay;
    bool constant_conversion;
    bool constant_logical;
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
    // Where an event refuses it (NODE_CENSUS_EVENT), the event's warning: CHECK_WARNING_ID_COUNT for one that is none of
    // its tokens' span's.
    uint32_t event;
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
    // (node_tests_ranges); the current expression's records' types' bytes and what each is as an operand
    // (BUSTER_NODE_TESTS_OPERAND_*), by a record's number among the expression's (NodeTestsLoop's: the window's columns
    // where the expression's records are in a row there); and the unit's tables by operator (node_tests_tables).
    uint32_t* type_ranges;
    const uint8_t* own_types;
    const uint8_t* own_properties;
    _Alignas(64) uint8_t own_classes[128];
    uint8_t own_operand_bits[128];
    uint8_t own_sends[128];
    uint8_t own_quiets[128];
    uint8_t own_quiet_pairs[64];
    uint8_t own_tree_marks[128];
    uint8_t own_tree_firsts[128];
    uint8_t own_tree_sends[128];
    uint8_t own_roots[128];
    // Whether a call may be left out of the loop's list: not in a unit whose calls' test has something for each
    // (-Wtype-safety's type tags).
    bool own_calls_quiet;
    // With the reader's verify, the operators the loop left out of the expression at hand's list for their constants, as
    // its lists' words are; else null. And whether each record the tree's tests read that no listed operator has as
    // an operand has its test run all the same.
    const uint64_t* own_quiet_bits;
    bool own_tree_verify;
    // The declared type of the object whose initializer the current expression is, where the caller gives it (0
    // otherwise): at reader time, and after the pipeline for a root the lowering never popped, which no control names.
    TypeId given_declared;
    // And that object's declaration's token, where the caller gives it for the pipeline's tests of a root the lowering
    // never popped (BUSTER_CHECK_NONE otherwise, and at reader time, whose events' span is its caller's to widen).
    uint32_t given_declaration;
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
    // --bodies: the body's entries as its controls and its roots' tapes make them (node_bodies_begin), or none; and the
    // same from the tapes kept before the walk (node_bodies_kept).
    struct NodeBodies* bodies;
    struct NodeBodies* bodies_kept;
    struct NodeTwin* twin;
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
    policy.ordered = check_common_enabled(pp, input, CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, false);
    policy.void_dereference = check_common_enabled(pp, input, CHECK_WARNING_ID_VOID_PTR_DEREFERENCE, false);
    policy.string_plus_int = check_common_enabled(pp, input, CHECK_WARNING_ID_STRING_PLUS_INT, false);
    policy.packed_member = check_common_enabled(pp, input, CHECK_WARNING_ID_ADDRESS_OF_PACKED_MEMBER, false);
    policy.unevaluated = check_common_enabled(pp, input, CHECK_WARNING_ID_UNEVALUATED_EXPRESSION, false);
    policy.alignof_expression = check_common_enabled(pp, input, CHECK_WARNING_ID_GNU_ALIGNOF_EXPRESSION, false);
    policy.sizeof_decay = check_common_enabled(pp, input, CHECK_WARNING_ID_SIZEOF_ARRAY_DECAY, false);
    policy.constant_conversion = check_common_enabled(pp, input, CHECK_WARNING_ID_CONSTANT_CONVERSION, false) ||
                                 check_common_enabled(pp, input, CHECK_WARNING_ID_LITERAL_CONVERSION, false);
    policy.constant_logical = check_common_enabled(pp, input, CHECK_WARNING_ID_CONSTANT_LOGICAL_OPERAND, false);
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
    // And two attributes whose warnings no test stands for (#3201), each a fact of the unit the reader has before its
    // coded walk. x86's interrupt and no_caller_saved_registers, read on any declaration (CheckInput.interrupt_calls):
    // every other call in such a function's body warns (-Wexcessive-regsave, check_input_interrupt_calls), and a call of
    // an interrupt routine is an error. And noderef, where the unit spells it at all (the reader reserves the column
    // then, c_check_input.c): a dereference warns by its operand's declaration's type's annotation, which a typedef, a
    // member or a cast may give it (check_input_noderef_graph), and no node's type says.
    policy.unmodeled |= input->interrupt_calls || check_sparse_wide_present(input->noderef_types);
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

// A complex, _BitInt, atomic, vector, half-precision or __float128 type: the values the machine has no kind for. And an
// object's type in a segment's address space (#3205; 256, 257 and 258, x86's %gs, %fs and %ss, as check_lower_segment
// has them, an array's element's too): the lowering gives each load and store at such an object its segment's
// override (check_lower_access), which the machine's rows have no entry for. A unit without an extended qualifier
// (TypeTable.extended, nearly every one) asks nothing of a type.
static bool node_census_special_type(const TypeTable* types, TypeId type)
{
    const bool atomic = type != 0 && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC;
    const TypeId base = node_census_base(types, type);
    const uint8_t kind = type != 0 ? type_kind(types, base) : BUSTER_TYPE_KIND_NONE;
    const bool special_kind = kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_VECTOR ||
                              kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    const bool special_code = base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT16) || base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_HALF) ||
                              base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BF16) || base == BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT128);
    const uint32_t space = type != 0 && types->extended && type_is_valid(types, type) ? type_address_space(type_object_qualifiers(types, type)) : 0;
    const bool segment = space >= BUSTER_TYPE_SPACE_TARGET + 256 && space <= BUSTER_TYPE_SPACE_TARGET + 258;
    return atomic || special_kind || special_code || segment;
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
// A value that is a pointer once an operator converts it as an operand (check_action_operand_value's): a pointer's, an
// array's, and a function's (#3201). A function's type has a pointer's bit and the void pointee's with it, as
// nullptr_t's has: no plain pointer's byte (the quiet rule's kind is the pointer's bit alone), and what asks for a
// pointer to void reads the type itself.
#define BUSTER_NODE_CENSUS_TYPE_CONVERTS (BUSTER_NODE_CENSUS_TYPE_POINTER | BUSTER_NODE_CENSUS_TYPE_ARRAY)

static __attribute__((noinline)) uint8_t node_census_type_bits_make(const TypeTable* types, TypeId type)
{
    const uint8_t kind = node_census_kind(types, type);
    const TypeId pointee = kind == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_strip_aligned(types, type_element(types, node_census_base(types, type)))) : 0;
    const bool void_pointee = kind == BUSTER_TYPE_KIND_NULLPTR || (kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID);
    const bool function = kind == BUSTER_TYPE_KIND_FUNCTION;
    const uint8_t bits = (uint8_t)(BUSTER_NODE_CENSUS_TYPE_KNOWN | (node_census_special_type(types, type) ? BUSTER_NODE_CENSUS_TYPE_SPECIAL : 0) |
                                   (node_census_aggregate(types, type) ? BUSTER_NODE_CENSUS_TYPE_AGGREGATE : 0) |
                                   (node_census_enum(types, type) ? BUSTER_NODE_CENSUS_TYPE_ENUM : 0) |
                                   (node_census_floating(types, type) ? BUSTER_NODE_CENSUS_TYPE_FLOATING : 0) |
                                   (node_census_pointer(types, type) || function ? BUSTER_NODE_CENSUS_TYPE_POINTER : 0) |
                                   (void_pointee || function ? BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE : 0) | (type_kind_is_array(kind) ? BUSTER_NODE_CENSUS_TYPE_ARRAY : 0));
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

// Whether a declaration token has a list of diagnose_if's or enable_if's conditions (CheckInput.condition_declarations,
// #3201): its own or an earlier redeclaration's, which it inherits (c_check_condition.c), though no attribute of its own
// or of its entity's first declaration says so. A call of the name reads that list after the graph's run
// (check_input_condition_call). A use that is no call is diagnosed before the walk (check_input_condition_uses), and
// its event refuses its expression. A unit without the attributes has no list.
static bool node_census_conditioned(const CheckInput* input, uint32_t declaration)
{
    return input->condition_declaration_count != 0 && check_condition_declaration(input, declaration) != NULL;
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
                            node_census_attributed(input, declaration, node_census_callee_kinds, BUSTER_ARRAY_LENGTH(node_census_callee_kinds)) ||
                            node_census_conditioned(input, declaration);
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
// operands followed wait in the walk's stack (NodeCensusScratch.stack, free once the expression is walked), which has
// room for each of the expression's nodes once: every operand is followed, however deep the tree (a list of 64 refused a
// sum of more operands, which the tests on the window, asked only where an address is taken, never did: #3201).
static bool node_census_returns_address(const NodeCensusScratch* s, uint32_t root)
{
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    uint32_t* const pending = s->stack;
    uint32_t count = 0;
    bool result = false;
    pending[count++] = root;
    while (count != 0 && !result)
    {
        const uint32_t node = pending[--count];
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
                pending[count++] = first;
                if (second != BUSTER_CHECK_NONE)
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

// Whether an ordered comparison of the nodes `a` and `b` is one of a pointer with an integer, which the compiler warns of
// after the graph's run (check_warning_ordered_event, #3201): its test, of the operands as the action converts them
// (check_action_operand_value: an array's and a function's value is a pointer). Exactly one is a pointer, and the other
// an integer that is no null pointer constant (check_action_null, of its value before the conversion, which changes
// neither an integer's type nor its constant).
static bool node_census_ordered(const NodeCensusScratch* s, uint32_t a, uint32_t b)
{
    const TypeTable* types = s->program->types;
    const CheckValue left = node_view_value(s, a);
    const CheckValue right = node_view_value(s, b);
    const uint8_t left_kind = type_is_valid(types, left.type) ? check_action_kind(types, left.type) : BUSTER_TYPE_KIND_NONE;
    const uint8_t right_kind = type_is_valid(types, right.type) ? check_action_kind(types, right.type) : BUSTER_TYPE_KIND_NONE;
    const bool left_pointer = left_kind == BUSTER_TYPE_KIND_POINTER || left_kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(left_kind);
    const bool right_pointer = right_kind == BUSTER_TYPE_KIND_POINTER || right_kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(right_kind);
    const CheckValue* integer = left_pointer ? &right : &left;
    const bool result = left_pointer != right_pointer && type_is_valid(types, integer->type) && check_action_is_integer(types, integer->type) &&
                        !check_action_null(types, integer, 0);
    return result;
}

// Whether `token` was written in a system header, as the common warnings find it (check_common_severity): the graph's
// mark, or the preprocessor's level at the token's place.
static bool node_census_system(const NodeCensusScratch* s, uint32_t token)
{
    const CheckProgram* program = s->program;
    const Parser* parser = program->parser;
    const Preprocessor* pp = s->pp;
    bool system = check_graph_system_token(&program->input->graph, token);
    if (!system && pp->output.locations != NULL && token < parser->tokens.count && parse_source_index(&parser->tokens, token) < pp->output.count)
    {
        system = pp_system_level_at(pp, pp->output.locations[parse_source_index(&parser->tokens, token)]) != BUSTER_PP_USER_FILE;
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
#define BUSTER_NODE_CENSUS_FOLLOWED 64u
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

// Whether the compiler warns of node `source`'s value converted to `destination` as a null pointer constant that is no
// literal (-Wnon-literal-null-conversion, #3012): check_warning_null_event's own test of the value, called and not
// restated (a pointer's destination; an integer constant expression of an integer type that is no bool, of value zero,
// with no fault; and no literal's flag). Its test of the tokens, by which a lone number in parentheses is a literal, is
// not asked: a number's leaf has the literal's flag (check_input_literal_value) and parentheses make no node. An
// unevaluated operand's warning, which the compiler drops, is refused all the same.
static bool node_census_null_warned(const NodeCensusScratch* s, uint32_t source, TypeId destination)
{
    bool result = false;
    if (node_view_has(s, source))
    {
        const CheckValue value = node_view_value(s, source);
        result = check_warning_null_integer(s->program->types, destination, &value) && (value.flags & BUSTER_CHECK_LITERAL_ZERO) == 0;
    }
    return result;
}

// The silence test of constants at node `node`: its operands' conversions, a division by zero, a shift's constant count or
// negative operand, a null dereference, a constant subscript outside its array, a comparison with a constant outside or at
// the bound of its other operand's type, a non-literal null converted to a pointer (an assignment's, an initialization's
// or a conversion's source, and a call's argument, each to the type its node converts it to).
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
        // The source's slot and its conversion's type, as the compiler's pass is given them (check_input_warning_graph).
        const uint32_t slot = op == CHECK_OP_ASSIGN ? 1 : 0;
        result = node_census_null_warned(s, slot == 1 ? b : a, node_view_converted(s, node, slot));
    }
    for (uint32_t i = 0; policy->non_literal_null && op == CHECK_OP_CALL && !result && i < graph->argument_counts[node]; i += 1)
    {
        result = node_census_null_warned(s, graph->argument_nodes[graph->argument_offsets[node] + i], node_view_argument_converted(s, node, i));
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

// A scalar's existing value with a completed const-object receipt read at the logical expression's source cut.
// This is the lvalue conversion's proof alone: no folding, allocation or type interning. In particular a mutable
// named object is not a constant merely because its designator has a known address.
static CheckValue node_census_logical_value(const NodeCensusScratch* s, uint32_t node, uint32_t completion)
{
    const TypeTable* types = s->program->types;
    CheckValue value = node_view_value(s, node);
    if (value.category == CHECK_LVALUE)
    {
        CheckValue loaded = { 0 };
        const bool volatile_read = (type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) != 0 || (value.flags & BUSTER_CHECK_VOLATILE_PART) != 0;
        const bool known = !volatile_read && value.fault == CHECK_FAULT_NONE &&
                           (value.flags & (BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                           (value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_BIT_FIELD) != 0) &&
                           check_const_load(types, &s->program->input->const_values, value.object, value.offset, value.object_bit_offset,
                                            value.bit_width, value.type, completion, &loaded);
        if (known)
        {
            check_action_inherit(&loaded, &value);
            value = loaded;
        }
        else if (volatile_read || value.constant_kind == CHECK_CONSTANT_ADDRESS || (value.flags & BUSTER_CHECK_ICE) == 0)
        {
            value.constant_kind = CHECK_CONSTANT_NONE;
        }
    }
    return value;
}

// A required runtime operand proves that this integer expression cannot be an independently evaluated constant.
// Only ordinary calls, writes and scalar leaves prove that fact; arithmetic and casts require their operands, a
// conditional requires its condition, and a comma requires its discarded value too. Never descend through an
// address, sizeof or a short-circuit operator. Unknown shapes and a proof past this bounded frontier refuse below.
static bool node_census_logical_runtime(const NodeCensusScratch* s, uint32_t root, uint32_t completion)
{
    const CheckGraph* graph = &s->program->input->graph;
    uint32_t pending[BUSTER_NODE_CENSUS_FOLLOWED];
    uint32_t count = 0;
    uint32_t visited = 0;
    bool result = false;
    pending[count++] = root;
    while (count != 0 && visited < BUSTER_NODE_CENSUS_FOLLOWED && !result)
    {
        const uint32_t node = pending[--count];
        visited += 1;
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE && check_action_is_scalar(s->program->types, node_view_type(s, node)))
        {
            const CheckValue value = node_census_logical_value(s, node, completion);
            result = !check_value_evaluated_constant(&value) || (value.flags & BUSTER_CHECK_INVALID_CONSTANT) != 0;
        }
        else if (node_census_modifies(op) || (op == CHECK_OP_CALL &&
                 (graph->members == NULL || (graph->members[node] & BUSTER_CHECK_LIBRARY_CALL) == 0)))
        {
            result = true;
        }
        else
        {
            const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_NOT || op == CHECK_OP_BIT_NOT ||
                               op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL ||
                               op == CHECK_OP_DEREFERENCE || op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW;
            const bool binary = (op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR) || op == CHECK_OP_COMMA || op == CHECK_OP_SUBSCRIPT;
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node] };
            for (uint32_t i = 0; i < (binary ? 2u : unary ? 1u : 0u) && count < BUSTER_NODE_CENSUS_FOLLOWED; i += 1)
            {
                if (operands[i] < node && node_view_has(s, operands[i]))
                {
                    pending[count++] = operands[i];
                }
            }
        }
    }
    return result;
}

// -Wconstant-logical-operand is produced after the reader-time census. Source exclusions and Boolean right operands
// prove silence immediately; so does a required runtime operand. Other integer expressions may become constants
// only when the warning pass replays const-object reads, and are conservatively refused by the constant test.
// Both census forms call this one predicate, including when neither runtime operand has a constant payload.
static bool node_census_constant_logical(const NodeCensusScratch* s, uint32_t node)
{
    const CheckInput* input = s->program->input;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = s->program->parser;
    const TypeTable* types = s->program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const uint32_t token = graph->tokens[node];
    bool result = false;
    if (s->policy.constant_logical && (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR) &&
        a < graph->count && b < graph->count && node_view_has(s, a) && node_view_has(s, b) && token < parser->tokens.count)
    {
        const TypeId left = node_view_type(s, a);
        const TypeId right = node_view_type(s, b);
        const bool integers = check_action_is_integer(types, left) && check_action_is_integer(types, right);
        const bool boolean_type = type_unqualified(type_strip_aligned(types, left)) == BUSTER_TYPE_BOOL ||
                                  type_unqualified(type_strip_aligned(types, right)) == BUSTER_TYPE_BOOL;
        const bool macro = parser->tokens.flags != NULL && (parser->tokens.flags[token] & BUSTER_PP_FROM_MACRO) != 0;
        const bool enumerator = check_input_logical_enumerator(input, parser, token != 0 ? token - 1 : BUSTER_CHECK_NONE, token) ||
                                check_input_logical_enumerator(input, parser, token + 1, token);
        const CheckOp inner = graph->ops[b];
        const bool boolean_op = node_census_comparison(inner) || inner == CHECK_OP_NOT || inner == CHECK_OP_LOGICAL_AND ||
                                inner == CHECK_OP_LOGICAL_OR || inner == CHECK_OP_CONDITION;
        if (integers && !boolean_type && !macro && !enumerator && !boolean_op)
        {
            const uint32_t completion = graph->source_positions != NULL && graph->source_positions[node] != 0 ? graph->source_positions[node] : token + 1;
            const CheckValue value = node_census_logical_value(s, b, completion);
            uint32_t width = 0;
            bool sign = false;
            const bool boolean_value = value.constant_kind == CHECK_CONSTANT_INTEGER && node_census_integer(types, right, &width, &sign) &&
                                       (value.integer == 0 || value.integer == 1);
            result = !boolean_value && !node_census_logical_runtime(s, b, completion);
        }
    }
    return result;
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
    if (policy->ordered && op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL && binary)
    {
        tests |= node_census_ordered(s, a, b) ? UINT64_C(1) << NODE_CENSUS_ORDER : 0;
    }
    // A cast of a pointer, or of an array or a function, which the cast converts to one (check_warning_graph_pointer_cast's
    // `from_pointer`, #3201), to an integer narrower than it or to an enumeration.
    if (op == CHECK_OP_CAST && policy->pointer_casts && (node_census_type_bits(s, a_type) & BUSTER_NODE_CENSUS_TYPE_CONVERTS) != 0)
    {
        uint32_t width = 0;
        bool sign = false;
        const bool narrow = node_census_integer(types, type, &width, &sign) && width < 64;
        tests |= narrow || (type_bits & BUSTER_NODE_CENSUS_TYPE_ENUM) != 0 ? UINT64_C(1) << NODE_CENSUS_POINTER_CAST : 0;
    }
    // (A pointer's own type: a function's byte has the pointer's bit and the void pointee's too.)
    if ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference && (node_census_type_bits(s, a_type) & BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE) != 0 &&
        node_census_kind(types, a_type) == BUSTER_TYPE_KIND_POINTER)
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
    tests |= op != CHECK_OP_VALUE && (node_census_constants(s, node) || node_census_constant_logical(s, node)) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
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

// Whether a `return` at `token` is in the scope of a variable with a cleanup (CheckInput.cleanups, in source order: a
// record's scope is from its declaration to its `end`, #3202). The program then computes the value first, as a RESULT's,
// runs the cleanups and returns it (check_control_statement): the value's root is no return's after the pipeline, by the
// program's own control, and reader time says the same by this (node_census_reader_expression, the window's chunk).
// Of the records declared before the token, one's scope holds it where the furthest of their ends is past it, which
// the last of them has (CheckInputCleanup.reach): a search, whatever the unit's records.
static bool node_census_cleans(const CheckInput* input, uint32_t token)
{
    const CheckInputCleanup* cleanups = input->cleanups;
    uint32_t low = 0;
    uint32_t high = input->cleanup_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = cleanups[middle].declaration < token ? middle + 1 : low;
        high = cleanups[middle].declaration < token ? high : middle;
    }
    return low != 0 && cleanups[low - 1].reach > token;
}

// Whether the object declared at `declaration`, whose initializer's whole value a root is, is an automatic one with an
// asm label (#3201): one in a body, with no linkage, neither static nor `register` (a register variable's label names
// its register, as musl's system calls' arguments' do). Its label is ignored with a warning at the label, which the
// reader raises after the object's body, by this test (c_check_asm_input.c): the event is among the initializer's tokens
// after the pipeline (they begin after the object's name, node_census_initializer_low) and not there yet when the
// walk reads the initializer. So the declaration refuses its initializer, on both sides: its root is read as a brace
// initializer's, which the machine doesn't read (node_census_root, node_census_reader_expression). In a unit with an
// asm keyword alone.
static bool node_census_labelled(const CheckInput* input, const Parser* parser, uint32_t declaration)
{
    const bool known = input->asm_capacity != 0 && declaration < input->token_count && declaration < parser->tokens.count;
    const uint8_t bits = known ? input->declaration_bits[declaration] : 0;
    const bool automatic = known && parser->tree.depths[declaration] != 0 && input->declaration_kinds[declaration] == C_BINDING_OBJECT &&
                           (bits & (BUSTER_CHECK_BITS_REGISTER | BUSTER_CHECK_BITS_STATIC)) == 0 && check_input_linkage(input->declaration_bits, declaration) == 0;
    const CheckInputAttributes* attributes = automatic ? check_input_attributes_at(input, declaration) : NULL;
    bool result = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count && !result; i += 1)
    {
        result = attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL;
    }
    return result;
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

// The declaration's token of the object whose initializer's whole value is node `root`, by its INITIALIZE control:
// BUSTER_CHECK_NONE for none.
static uint32_t node_census_initialized_token(const NodeCensusScratch* s, uint32_t root)
{
    const uint32_t control = s->node_controls[root];
    return control != BUSTER_CHECK_NONE ? s->program->controls[control].token : BUSTER_CHECK_NONE;
}

// The declared type of the object whose initializer's whole value is node `root`.
static TypeId node_census_initialized_type(const NodeCensusScratch* s, uint32_t root)
{
    const uint32_t declaration = node_census_initialized_token(s, root);
    return declaration < s->program->input->token_count ? s->program->input->declared_types[declaration] : 0;
}

// The low end of an initializer's root's tokens' span after the pipeline, its nodes' tokens' least being `lowest` and
// its object's declaration's token `declaration` (BUSTER_CHECK_NONE for none): the token after the object's name
// (#3017). The compiler puts a warning of a whole initializer at its source's first token, which is no node's where it
// is a parenthesis (a cast's, a group's) or a keyword (check_warning_null_event), and an initializer's root is the
// bare expression, with no node before it as an assignment's, a call's or a return's has. What stands between the name
// and the source (the declarator's rest, its `=`) is the declaration's, whose events refuse its initializer too.
// Reader time's span stays its nodes': the events the reader has when it pops a root are at tokens it has read as
// nodes, and a warning the pipeline puts before them has a test of its own there (the null pointer constant's).
static inline uint32_t node_census_initializer_low(uint32_t lowest, uint32_t declaration)
{
    return declaration != BUSTER_CHECK_NONE ? declaration + 1 : lowest;
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
    const bool given = s->given_declared != 0 || s->records != NULL;
    const TypeId declared = context != NODE_CENSUS_ROOT_INITIALIZER ? 0 : given ? s->given_declared : node_census_initialized_type(s, root);
    const uint32_t low = node_census_initializer_low(lowest, context != NODE_CENSUS_ROOT_INITIALIZER ? BUSTER_CHECK_NONE : given ? s->given_declaration :
                                                             node_census_initialized_token(s, root));
    tests |= (s->policy.constant_conversion || s->policy.assign_enum) && declared != 0 && node_census_constant_changes(s, root, declared) ?
             UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    // And a null pointer constant that is no literal, which the initializer's walker warns of there (#3012).
    tests |= s->policy.non_literal_null && node_census_null_warned(s, root, declared) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    tests |= declared != 0 && node_census_special_type(program->types, declared) ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
    tests |= s->policy.return_address && context == NODE_CENSUS_ROOT_RETURN && node_census_returns_address(s, root) ?
             UINT64_C(1) << NODE_CENSUS_RETURN_ADDRESS : 0;
    tests |= s->policy.unsequenced && unsequenced && node_census_unsequenced(s, walked, root) ? UINT64_C(1) << NODE_CENSUS_UNSEQUENCED : 0;
    const bool events = s->event_prefix != NULL && low <= highest && s->event_prefix[highest + 1] != s->event_prefix[low];
    tests |= events ? UINT64_C(1) << NODE_CENSUS_EVENT : 0;
    // T's actions' warnings for the expression's nodes, those the unit's nodes suppress aside: at least the ones the
    // graph run keeps a node (it drops an unevaluated operand's folds').
    const uint32_t actions = s->records != NULL ? s->records->action_warnings & 0x01ffffffu : 0;
    tests |= (actions & s->policy.action_bits) != 0 ? UINT64_C(1) << NODE_CENSUS_ACTION_WARNING : 0;
    s->span_low = low;
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
    NODE_TESTS_TRIGGER_ORDER,
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
    [NODE_TESTS_TRIGGER_ORDER] = "pointer-integer-order",
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

// Whether the census's code is asked for a null pointer constant that is no literal (node_census_null_warned), of an
// integer constant's record `o` converted to a type whose byte is `target_bits`: where that is a pointer's and the
// record has no literal's flag. What the test asks beside (the constant's type, its value, its form) is its own to read.
static inline bool node_tests_null_asked(const NodeCensusScratch* s, uint32_t o, uint8_t target_bits)
{
    return (target_bits & BUSTER_NODE_CENSUS_TYPE_POINTER) != 0 && (s->records->columns->value_flags[o] & BUSTER_CHECK_LITERAL_ZERO) == 0;
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
        // An integer constant that is no literal converted to a pointer (#3012): the census's code's, which asks the
        // compiler's own test of its value (node_census_null_warned). Here only what that test can't pass without: the
        // record's own type a pointer's (an assignment's destination's, a conversion's target), the source an integer
        // constant, its record without the literal's flag.
        const uint32_t side = op == CHECK_OP_ASSIGN ? 1 : 0;
        const bool asked = policy->non_literal_null && kinds[side] == CHECK_CONSTANT_INTEGER && node_tests_null_asked(s, o[side], kept[r]);
        trigger = asked ? NODE_TESTS_TRIGGER_NULL_CONSTANT : trigger;
    }
    *fires = fired;
    return trigger;
}

// An operator's classes in the tests' loop. The set takes the first two bits directly; COPIES shares the
// aggregate type's bit. The sign bit lists operators that have tests whatever their operands are.
#define BUSTER_NODE_TESTS_CLASS_UNCODED 0x01u
#define BUSTER_NODE_TESTS_CLASS_ADDRESS 0x02u
#define BUSTER_NODE_TESTS_CLASS_COPIES BUSTER_NODE_CENSUS_TYPE_AGGREGATE
#define BUSTER_NODE_TESTS_CLASS_WRITE 0x08u
#define BUSTER_NODE_TESTS_CLASS_QUIET_ORDER 0x10u
#define BUSTER_NODE_TESTS_CLASS_ALWAYS 0x80u
// What a record is as an operand (NodeCensusScratch.own_properties, a byte a record): a constant; of an enumeration's
// type, a pointer's, a pointer's whose pointee may be void, an array's; an addition or a subtraction; a comparison; a
// left shift or a product. An operator's `own_sends` says which of its operands' properties send it to its own tests.
#define BUSTER_NODE_TESTS_OPERAND_CONSTANT 0x01u
#define BUSTER_NODE_TESTS_OPERAND_ENUM 0x02u
#define BUSTER_NODE_TESTS_OPERAND_ARRAY 0x04u
#define BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE 0x08u
#define BUSTER_NODE_TESTS_OPERAND_CONVERTS 0x10u
#define BUSTER_NODE_TESTS_OPERAND_SUM 0x20u
#define BUSTER_NODE_TESTS_OPERAND_COMPARISON 0x40u
#define BUSTER_NODE_TESTS_OPERAND_TRUTH 0x80u
// A pointer once converted as an operand (BUSTER_NODE_CENSUS_TYPE_CONVERTS, as an operand's properties): a pointer's, an
// array's or a function's value.
_Static_assert(BUSTER_NODE_TESTS_OPERAND_CONVERTS == BUSTER_NODE_TESTS_CLASS_QUIET_ORDER,
               "the ordered comparison and both operands use the same bit");
_Static_assert(CHECK_OP_COUNT <= 128, "the own form's tables have an entry an operator");

// The tree's tests (node_tests_tree) read a record's token's parent's role, and nearly every one ends there: a
// comparison's fires under a bitwise operator or an expression statement's `;`, a `!`'s under a comparison it comes
// before, an `=`'s under a condition's `)` or a `for`'s condition's `;`. That parent's record is the record's consumer:
// the bitwise operator's, the comparison's, or the root the reader puts at the `;` or the `)`, a discard (a cast to
// void) or a condition. So the loop lists the consumer, by what its operands are, and a record's token's parent is
// read only for the operands of the few consumers listed (node_tests_expression):
// - an operator's `own_tree_sends`: the properties of an operand, either one, that list it: a comparison, for `&`, `|`
//   and `^` and for a cast;
// - its `own_tree_firsts`: what its first operand's operator is (`own_tree_marks`, by that operator) that lists it: `!`
//   for a comparison, `=` for a condition.
// The warnings' switches are the tests' own to read: the few operators listed are listed whatever the unit enables.
#define BUSTER_NODE_TESTS_TREE_NOT 0x01u
#define BUSTER_NODE_TESTS_TREE_ASSIGN 0x02u

// The loop's quiet rule (node_tests_window_loop): an operator listed for a constant operand alone has no test to make
// where its operands are of kinds its class's constants' tests have nothing to say of.
//
// What a window's record is to those tests, as an operand: its kind.
// - None: no plain integer and no plain pointer; or no operand there.
// - A plain pointer: to no void.
// - A plain integer: its type an integer's of 8 to 64 bits (no bool, enumeration, _BitInt or atomic one), its value no
//   bit-field's. Signed or unsigned where it is no integer constant; and an integer constant by its value, which its
//   record says (c_check_node_machine_facts.c's node_facts_small): zero, 1 to 31, 32 to 126, or any other, one that
//   says none among them.
#define BUSTER_NODE_TESTS_KIND_NONE 0u
#define BUSTER_NODE_TESTS_KIND_POINTER 1u
#define BUSTER_NODE_TESTS_KIND_SIGNED 2u
#define BUSTER_NODE_TESTS_KIND_UNSIGNED 3u
#define BUSTER_NODE_TESTS_KIND_ZERO 4u
#define BUSTER_NODE_TESTS_KIND_TINY 5u
#define BUSTER_NODE_TESTS_KIND_SMALL 6u
#define BUSTER_NODE_TESTS_KIND_CONSTANT 7u
#define BUSTER_NODE_TESTS_KINDS 8u
// A record's kind in its value's byte (NodeCensusReader.window_values, above its BUSTER_NODE_TESTS_VALUE_* bits): three
// bits from bit 3, where a pair's second kind sits in the table's index, and bit 7 where its type is a plain integer's.
#define BUSTER_NODE_TESTS_KIND_SHIFT 3u
#define BUSTER_NODE_TESTS_KIND_OWN 0x80u
#define BUSTER_NODE_TESTS_KIND_BITS(kind) ((uint8_t)((kind) << BUSTER_NODE_TESTS_KIND_SHIFT | ((kind) >= BUSTER_NODE_TESTS_KIND_SIGNED ? BUSTER_NODE_TESTS_KIND_OWN : 0u)))

// An operator's class for the rule (NodeCensusScratch.own_quiets), a bit each: none for an operator the rule doesn't
// take.
#define BUSTER_NODE_TESTS_QUIET_NONE 0u
// `+`, `-`, `*`, `&`, `|`, `^` and their assignments, of two plain integers: the usual conversions narrow no integer.
// And of a plain pointer and a plain integer, which `+`, `-` and their assignments alone take, and a subscript: the
// integer is promoted, which narrows none either (check_action_pointer_arithmetic), and no other test of a constant
// is a sum's. A subscript's other tests are a null constant pointer's dereference, which the loop keeps listed, and an
// array's bound: an array is no plain pointer.
#define BUSTER_NODE_TESTS_QUIET_BINARY 0x01u
// Unary `+`, `-`, `~` of a plain integer: the promotions narrow none.
#define BUSTER_NODE_TESTS_QUIET_UNARY 0x02u
// `==`, `!=` of two plain integers: a constant against no constant is zero or 1 to 126. Or of a plain pointer and a
// constant zero.
#define BUSTER_NODE_TESTS_QUIET_EQUALITY 0x04u
// `<`, `<=`, `>`, `>=` of two plain integers: a constant against no constant is 1 to 126, or zero against a signed
// operand.
#define BUSTER_NODE_TESTS_QUIET_RELATIONAL 0x08u
// `<<`, `>>` of two plain integers, but under -Wshift-sign-overflow: a constant count under 32, a constant shifted zero
// or 1 to 126.
#define BUSTER_NODE_TESTS_QUIET_SHIFT 0x10u
// `/`, `%` of two plain integers: a constant divisor 1 to 126.
#define BUSTER_NODE_TESTS_QUIET_DIVISION 0x20u
// `=` of two plain integers (its own type is its first operand's): the constant stored zero or 1 to 126.
#define BUSTER_NODE_TESTS_QUIET_ASSIGN 0x40u
// A return's conversion of a constant zero or 1 to 126, its own type a plain integer's too: its record's own byte's
// bit. (An initialization's root is no record of the window's: its constant's test is the root's own,
// node_tests_root's.)
#define BUSTER_NODE_TESTS_QUIET_CONVERT 0x80u
_Static_assert(BUSTER_NODE_TESTS_QUIET_CONVERT == BUSTER_NODE_TESTS_KIND_OWN, "a return's conversion's class is the bit of its record's own type");

// The rule's table (NodeCensusScratch.own_quiet_pairs): by an operator's two operands' kinds, the first's and eight
// times the second's, the classes whose operators' constants' tests have nothing to say of such operands.
static void node_tests_quiet_pairs(uint8_t* pairs)
{
    for (uint32_t pair = 0; pair < BUSTER_NODE_TESTS_KINDS * BUSTER_NODE_TESTS_KINDS; pair += 1)
    {
        const uint32_t left = pair % BUSTER_NODE_TESTS_KINDS;
        const uint32_t right = pair / BUSTER_NODE_TESTS_KINDS;
        const bool integer_left = left >= BUSTER_NODE_TESTS_KIND_SIGNED;
        const bool integer_right = right >= BUSTER_NODE_TESTS_KIND_SIGNED;
        const bool constant_left = left >= BUSTER_NODE_TESTS_KIND_ZERO;
        const bool constant_right = right >= BUSTER_NODE_TESTS_KIND_ZERO;
        const bool zero_left = left == BUSTER_NODE_TESTS_KIND_ZERO;
        const bool zero_right = right == BUSTER_NODE_TESTS_KIND_ZERO;
        // 1 to 126.
        const bool small_left = left == BUSTER_NODE_TESTS_KIND_TINY || left == BUSTER_NODE_TESTS_KIND_SMALL;
        const bool small_right = right == BUSTER_NODE_TESTS_KIND_TINY || right == BUSTER_NODE_TESTS_KIND_SMALL;
        // 0 to 31.
        const bool tiny_right = right == BUSTER_NODE_TESTS_KIND_ZERO || right == BUSTER_NODE_TESTS_KIND_TINY;
        const bool signed_left = left == BUSTER_NODE_TESTS_KIND_SIGNED;
        const bool signed_right = right == BUSTER_NODE_TESTS_KIND_SIGNED;
        const bool both = integer_left && integer_right;
        const bool equal = (!constant_left || constant_right || zero_left || small_left) && (!constant_right || constant_left || zero_right || small_right);
        const bool ordered = (!constant_left || constant_right || small_left || (zero_left && signed_right)) &&
                             (!constant_right || constant_left || small_right || (zero_right && signed_left));
        const bool shifted = (!constant_right || tiny_right) && (!constant_left || zero_left || small_left);
        const bool divided = !constant_right || small_right;
        const bool stored = zero_right || small_right;
        const bool converted = zero_left || small_left;
        const bool null = (left == BUSTER_NODE_TESTS_KIND_POINTER && zero_right) || (right == BUSTER_NODE_TESTS_KIND_POINTER && zero_left);
        const bool pointed = (left == BUSTER_NODE_TESTS_KIND_POINTER && integer_right) || (integer_left && right == BUSTER_NODE_TESTS_KIND_POINTER);
        // An operator with one operand (a unary one, a return's conversion) has no second: its pair's second kind is
        // none, and its class's bit is the first's alone.
        pairs[pair] = (uint8_t)((both || pointed ? BUSTER_NODE_TESTS_QUIET_BINARY : 0) | (integer_left ? BUSTER_NODE_TESTS_QUIET_UNARY : 0) |
                                ((both && equal) || null ? BUSTER_NODE_TESTS_QUIET_EQUALITY : 0) | (both && ordered ? BUSTER_NODE_TESTS_QUIET_RELATIONAL : 0) |
                                (both && shifted ? BUSTER_NODE_TESTS_QUIET_SHIFT : 0) | (both && divided ? BUSTER_NODE_TESTS_QUIET_DIVISION : 0) |
                                (both && stored ? BUSTER_NODE_TESTS_QUIET_ASSIGN : 0) | (converted ? BUSTER_NODE_TESTS_QUIET_CONVERT : 0));
    }
}

// A root's kind at reader time by the control that pushed it, its operator `op` (node_census_root's), but for a root
// inside a statement expression, which is a nested control's, and an initialization's, which its object decides.
static inline NodeCensusRoot node_census_root_kind(CheckOp op)
{
    return op == CHECK_OP_CAST ? NODE_CENSUS_ROOT_STATEMENT : op == CHECK_OP_CONDITION ? NODE_CENSUS_ROOT_CONDITION : op == CHECK_OP_CONVERT ? NODE_CENSUS_ROOT_RETURN :
           op == CHECK_OP_POSITIVE ? NODE_CENSUS_ROOT_SWITCH : NODE_CENSUS_ROOT_OTHER;
}

// The own form's tables for a unit, by its enabled warnings: each operator's classes, what it is as an operand, the
// mask of its type's byte where it copies its value (an aggregate's), whether it always has tests of its own, the
// operand properties that give it some, its class for the quiet rule, what lists it for its operands' tree's tests,
// and the kind of a root it is; and the quiet rule's table.
static void node_tests_tables(NodeCensusScratch* s, const TypeTable* types)
{
    const NodeCensusPolicy* policy = &s->policy;
    const bool truths = policy->int_in_bool || policy->tautological;
    node_tests_quiet_pairs(s->own_quiet_pairs);
    for (uint32_t code = 0; code < 128; code += 1)
    {
        const CheckOp op = (CheckOp)code;
        const bool leaf = op == CHECK_OP_VALUE;
        const bool comparison = node_census_comparison(op);
        const bool order = op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL;
        const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
        const bool copies = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                            op == CHECK_OP_CAST;
        const bool truth_parent = op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL;
        s->own_operand_bits[code] = (uint8_t)((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT ? BUSTER_NODE_TESTS_OPERAND_SUM : 0) |
                                              (comparison ? BUSTER_NODE_TESTS_OPERAND_COMPARISON : 0) |
                                              (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_MULTIPLY ? BUSTER_NODE_TESTS_OPERAND_TRUTH : 0));
        // A call's callee and arguments and a comma's discard are read at each, and the third operand of a conditional
        // (or of an operator outside the set); a member's record only in a unit with an atomic one, a comparison's
        // operands' signs and an assignment's names only under their warnings.
        const bool always = op == CHECK_OP_CALL || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA ||
                            !node_census_coded(op) || (types->atomic_records && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)) ||
                            (policy->sign_compare && comparison) || (policy->self_assign && op == CHECK_OP_ASSIGN) ||
                            (policy->constant_logical && (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR));
        s->own_classes[code] = (uint8_t)((!node_census_coded(op) ? BUSTER_NODE_TESTS_CLASS_UNCODED : 0) | (op == CHECK_OP_ADDRESS ? BUSTER_NODE_TESTS_CLASS_ADDRESS : 0) |
                                         (copies ? BUSTER_NODE_TESTS_CLASS_COPIES : 0) | (node_census_modifies(op) ? BUSTER_NODE_TESTS_CLASS_WRITE : 0) |
                                         (order && policy->ordered && !always ? BUSTER_NODE_TESTS_CLASS_QUIET_ORDER : 0) | (!leaf && always ? BUSTER_NODE_TESTS_CLASS_ALWAYS : 0));
        // A constant operand's tests are no cast's (it converts nothing implicitly, and no other constant's test is a
        // cast's), and nor are an enumeration's: those read two operands' types, a conditional's arms' or a
        // conversion's, and a cast has one operand and converts by its own word.
        s->own_sends[code] = leaf ? 0 :
                             (uint8_t)((op != CHECK_OP_CAST ? BUSTER_NODE_TESTS_OPERAND_CONSTANT : 0) |
                                       (op != CHECK_OP_CAST && policy->enums ? BUSTER_NODE_TESTS_OPERAND_ENUM : 0) |
                                       (op == CHECK_OP_CAST && policy->pointer_casts ? BUSTER_NODE_TESTS_OPERAND_CONVERTS : 0) |
                                       (order && policy->ordered ? BUSTER_NODE_TESTS_OPERAND_CONVERTS : 0) |
                                       ((op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT) && policy->void_dereference ? BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE : 0) |
                                       (op == CHECK_OP_ADD && policy->string_plus_int ? BUSTER_NODE_TESTS_OPERAND_ARRAY : 0) |
                                       (shift && policy->shift_parentheses ? BUSTER_NODE_TESTS_OPERAND_SUM : 0) |
                                       (comparison && policy->chained ? BUSTER_NODE_TESTS_OPERAND_COMPARISON : 0) |
                                       (truth_parent && truths ? BUSTER_NODE_TESTS_OPERAND_TRUTH : 0));
        const bool sums = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR ||
                          op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN || op == CHECK_OP_MULTIPLY_ASSIGN || op == CHECK_OP_BIT_AND_ASSIGN ||
                          op == CHECK_OP_BIT_OR_ASSIGN || op == CHECK_OP_BIT_XOR_ASSIGN;
        const bool equality = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
        s->own_quiets[code] = (uint8_t)(sums || op == CHECK_OP_SUBSCRIPT ? BUSTER_NODE_TESTS_QUIET_BINARY :
                                        op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ? BUSTER_NODE_TESTS_QUIET_UNARY :
                                        equality ? BUSTER_NODE_TESTS_QUIET_EQUALITY : comparison ? BUSTER_NODE_TESTS_QUIET_RELATIONAL :
                                        shift && !policy->shift_sign_overflow ? BUSTER_NODE_TESTS_QUIET_SHIFT :
                                        op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER ? BUSTER_NODE_TESTS_QUIET_DIVISION :
                                        op == CHECK_OP_ASSIGN ? BUSTER_NODE_TESTS_QUIET_ASSIGN :
                                        op == CHECK_OP_CONVERT ? BUSTER_NODE_TESTS_QUIET_CONVERT : BUSTER_NODE_TESTS_QUIET_NONE);
        const bool bitwise = op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR;
        s->own_tree_marks[code] = (uint8_t)(op == CHECK_OP_NOT ? BUSTER_NODE_TESTS_TREE_NOT : op == CHECK_OP_ASSIGN ? BUSTER_NODE_TESTS_TREE_ASSIGN : 0);
        s->own_tree_firsts[code] = (uint8_t)(comparison ? BUSTER_NODE_TESTS_TREE_NOT : op == CHECK_OP_CONDITION ? BUSTER_NODE_TESTS_TREE_ASSIGN : 0);
        s->own_tree_sends[code] = bitwise || op == CHECK_OP_CAST ? BUSTER_NODE_TESTS_OPERAND_COMPARISON : 0;
        s->own_roots[code] = (uint8_t)node_census_root_kind(op);
    }
    s->own_calls_quiet = !policy->type_safety;
}

// The parser's tree's tests of a record (operator `op`, at `token` of role `role`), as node_census_node reads them: a
// comparison's token under a bitwise operator or an expression statement's terminator, a `!`'s under a comparison, an
// assignment's under a condition's `)` or in a for statement's head. Each is asked only where the token's parent can
// be one of those.
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
    const uint8_t all = (uint8_t)(operands | (o[2] != UINT32_MAX ? properties[o[2]] : 0));
    uint64_t tests = 0;
    NodeTestsTrigger trigger = NODE_TESTS_TRIGGERS;
    counts->own_calls += op == CHECK_OP_CALL;
    counts->own_constants += op != CHECK_OP_CALL && (all & BUSTER_NODE_TESTS_OPERAND_CONSTANT) != 0;
    counts->own_thirds += o[2] != UINT32_MAX;
    // What sent it: a constant operand alone has the constants' tests and no other.
    const uint8_t sent = (uint8_t)(all & s->own_sends[op]);
    bool fires = false;
    if ((s->own_classes[op] & BUSTER_NODE_TESTS_CLASS_ALWAYS) == 0 && sent == BUSTER_NODE_TESTS_OPERAND_CONSTANT)
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
    if ((s->own_classes[op] & BUSTER_NODE_TESTS_CLASS_ALWAYS) == 0 && sent == BUSTER_NODE_TESTS_OPERAND_CONSTANT)
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
        // An argument that may be a null pointer constant that is no literal (node_tests_null_asked): the census's code's.
        bool nulls = false;
        const uint32_t first_argument = graph->argument_offsets[node];
        for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
        {
            const uint32_t argument = node_tests_record(machine, graph->argument_nodes[first_argument + i]);
            const uint32_t kind = argument != UINT32_MAX ? (columns->standings[argument] >> 2) & 0x07u : 0;
            aggregate |= argument != UINT32_MAX && (kept[argument] & BUSTER_NODE_CENSUS_TYPE_AGGREGATE) != 0;
            const TypeId target = kind != CHECK_CONSTANT_NONE ? node_view_argument_converted(s, node, i) : 0;
            changes |= conversions && kind != CHECK_CONSTANT_NONE ? node_tests_conversion(s, argument, kind, target) : 0;
            nulls |= policy->non_literal_null && kind == CHECK_CONSTANT_INTEGER && node_tests_null_asked(s, argument, node_census_type_bits(s, target));
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
        trigger = (changes & BUSTER_NODE_TESTS_CONVERSION_ASKED) != 0 ? NODE_TESTS_TRIGGER_CONVERSION : nulls ? NODE_TESTS_TRIGGER_NULL_CONSTANT :
                  enums ? NODE_TESTS_TRIGGER_ENUM : trigger;
    }
    else
    {
        const bool comparison = node_census_comparison(op);
        const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT;
        const bool binary = graph->operand0[node] < graph->count && graph->operand1[node] < graph->count;
        const uint8_t first_bits = o[0] != UINT32_MAX ? kept[o[0]] : 0;
        const uint8_t first_property = o[0] != UINT32_MAX ? properties[o[0]] : 0;
        const uint8_t second_property = o[1] != UINT32_MAX ? properties[o[1]] : 0;
        // An ordered comparison of exactly one operand that is a pointer once converted: the census's code's test's.
        const bool order = op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL && binary &&
                           ((first_property & BUSTER_NODE_TESTS_OPERAND_CONVERTS) != 0) != ((second_property & BUSTER_NODE_TESTS_OPERAND_CONVERTS) != 0);
        tests |= op == CHECK_OP_ADD && policy->string_plus_int && binary && (node_census_string(program, graph->operand0[node]) || node_census_string(program, graph->operand1[node])) ?
                 UINT64_C(1) << NODE_CENSUS_STRING_PLUS_INT : 0;
        // A cast of a pointer, or of what a cast converts to one, to an integer narrower than it, or to an enumeration.
        if (op == CHECK_OP_CAST && policy->pointer_casts && (first_bits & BUSTER_NODE_CENSUS_TYPE_CONVERTS) != 0)
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
        else if (policy->ordered && order)
        {
            trigger = NODE_TESTS_TRIGGER_ORDER;
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
    tests |= fires || node_census_constant_logical(s, node) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
    if (trigger != NODE_TESTS_TRIGGERS)
    {
        node_tests_send(r, trigger, visit, visits, counts);
    }
    return tests;
}

// Whether the call at node `node` has an argument the calls' rule doesn't take though the call's test may find nothing:
// a constant but an address's or an integer's of zero or 1 to 126, read from T's records and the call's own list of
// arguments. The reader's verify asks it of each call the loop left out, beside the call's test, which finds an
// aggregate's type or a constant its parameter's type changes, and sends a call with an argument that may be a null
// pointer constant that is no literal.
static bool node_tests_call_loud(const NodeCensusScratch* s, uint32_t node)
{
    const NodeMachine* machine = s->records;
    const NodeMachineColumns* columns = machine->columns;
    const CheckGraph* graph = &s->program->input->graph;
    bool loud = false;
    const uint32_t first_argument = graph->argument_offsets[node];
    for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
    {
        const uint32_t argument = node_tests_record(machine, graph->argument_nodes[first_argument + i]);
        const uint32_t kind = argument != UINT32_MAX ? (columns->standings[argument] >> 2) & 0x07u : CHECK_CONSTANT_NONE;
        const bool small = kind == CHECK_CONSTANT_INTEGER && columns->payloads[argument] <= 126;
        loud = loud || (kind != CHECK_CONSTANT_NONE && kind != CHECK_CONSTANT_ADDRESS && !small);
    }
    return loud;
}

// The expression's own tests at reader time (node_census_root_tests' there), each asked only where it can fire: a
// statement's discard where its top doesn't write, an initializer's constant where its root is one that its object's
// type may change, a return's address where the expression takes an address or has an array (`class_bits` and
// `type_bits` are the pass's, over all records). The unsequenced test reads the walk's `walked` nodes where
// `unsequenced` says.
static inline uint64_t node_tests_root(NodeCensusScratch* s, uint32_t root, NodeCensusRoot context, uint32_t count, uint32_t lowest, uint32_t highest, bool unsequenced,
                                       uint32_t walked, uint32_t class_bits, uint32_t type_bits, NodeFrontTypes* counts)
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
        // And a null pointer constant that is no literal (node_census_root_tests'), asked of the census's code where it
        // can be one.
        const bool asked = policy->non_literal_null && kind == CHECK_CONSTANT_INTEGER && node_tests_null_asked(s, count - 1, node_census_type_bits(s, declared));
        counts->own_root_nulls += asked;
        tests |= asked && node_census_null_warned(s, root, declared) ? UINT64_C(1) << NODE_CENSUS_CONSTANT : 0;
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

// What the tests' loop makes of an expression, on the window the front wrote (node_tests_window_chunk): the set's tests,
// the lists of the records that have tests of their own, and what the expression's own tests read of all its records.
typedef struct NodeTestsLoop NodeTestsLoop;
struct NodeTestsLoop
{
    // The set's tests' bits, and a leaf's declaration's.
    uint64_t tests;
    // A bit a record, by its number among the expression's records (its root's last), in `words` words: the operators
    // listed for their operands' tree's tests, and the operators with tests of their own.
    const uint64_t* tree_bits;
    const uint64_t* operator_bits;
    uint32_t words;
    // By a record's number among the expression's: its type's byte, what it is as an operand, and its operands' such
    // bytes together. The window's columns from the expression's first record.
    const uint8_t* types;
    const uint8_t* properties;
    const uint8_t* operands;
    // The expression's tokens' span, its writes and the last of them, and of its records' classes' and types' bits those
    // its own tests read (an address taken, an array).
    uint32_t lowest;
    uint32_t highest;
    uint32_t writes;
    uint32_t last_write;
    uint32_t class_bits;
    uint32_t type_bits;
    // What the tests then made of the expression, for the shadow's comparison: their bits, the root's kind and the
    // tokens' span.
    uint64_t verdict;
    uint32_t span_low;
    uint32_t span_high;
    uint8_t kind;
    // Whether the tests ran on it: an expression the kernel made in place that the tests ran on at reader time.
    uint8_t run;
    // Whether it has anything for the tests that read T's records (node_tests_window_chunk's first sweep says). One
    // that has nothing has its verdict from the window alone, and neither T nor those tests run on it.
    uint8_t listed;
};

// The tests of the machine's current expression, as bits of NodeCensusTest: node_census_expression's, in the machine's
// own form. `root` is its root's node, `context` its root's kind; `walked` receives its records' count, and `counts`
// what was sent to the census's code.
//
// The loop over the expression's records is the window's (node_tests_window_loop), 64 records a step, and `loop` is
// what it made of this expression: the set's tests, what each record is as an operand, and two lists, the operators
// whose operands the tree's tests read and the operators that have tests of their own by their operands' bytes. Each
// list's records have their tests here, on T's records of the expression, and the few that a silence test may refuse
// go to the census's code after a walk. A leaf has no test here: its declaration's bit came with its record.
static uint64_t node_tests_expression(NodeCensusScratch* s, uint32_t root, NodeCensusRoot context, uint32_t* walked, NodeFrontTypes* counts, const NodeTestsLoop* loop)
{
    const NodeMachine* machine = s->records;
    const NodeMachineColumns* columns = machine->columns;
    const CheckProgram* program = s->program;
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    const NodeCensusPolicy* policy = &s->policy;
    const uint32_t count = machine->count;
    const uint32_t first = machine->first;
    const uint32_t token_count = parser->tokens.count;
    const uint32_t* tokens = graph->tokens;
    const uint8_t* roles = parser->tree.roles;
    const uint8_t* codes = columns->codes;
    const uint32_t words = loop->words;
    uint32_t trees = 0;
    uint32_t operators = 0;
    s->mixed_system = false;
    s->own_types = loop->types;
    s->own_properties = loop->properties;
    uint64_t tests = loop->tests;
    // The tree's tests, of the operands of each operator listed for them: each reads its own token's parent.
    uint64_t read[BUSTER_NODE_MACHINE_MOST / 64 + 1];
    for (uint32_t word = 0; s->own_tree_verify && word < words; word += 1)
    {
        read[word] = 0;
    }
    for (uint32_t word = 0; word < words; word += 1)
    {
        for (uint64_t pending = loop->tree_bits[word]; pending != 0; pending &= pending - 1)
        {
            const uint32_t r = word * 64 + (uint32_t)__builtin_ctzll(pending);
            const uint32_t node = r + 1 == count ? root : first + r;
            const uint32_t operand_nodes[2] = { graph->operand0[node], graph->operand1[node] };
            for (uint32_t side = 0; side < 2; side += 1)
            {
                const uint32_t o = node_tests_record(machine, operand_nodes[side]);
                if (o != UINT32_MAX)
                {
                    const uint32_t token = tokens[operand_nodes[side]];
                    tests |= node_tests_tree(s, (CheckOp)codes[o], token, token < token_count ? roles[token] : PARSE_ROLE_NONE);
                    read[o >> 6] |= s->own_tree_verify ? UINT64_C(1) << (o & 63) : 0;
                }
            }
            trees += 1;
        }
    }
    uint64_t visit[BUSTER_NODE_MACHINE_MOST / 64 + 1];
    uint32_t visits = 0;
    for (uint32_t word = 0; word < words; word += 1)
    {
        visit[word] = 0;
    }
    for (uint32_t word = 0; word < words; word += 1)
    {
        for (uint64_t pending = loop->operator_bits[word]; pending != 0; pending &= pending - 1)
        {
            const uint32_t r = word * 64 + (uint32_t)__builtin_ctzll(pending);
            tests |= node_tests_operator(s, r, r + 1 == count ? root : first + r, loop->operands[r], visit, &visits, counts);
            operators += 1;
        }
    }
    if (s->own_tree_verify)
    {
        // The reader's verify: no list has a bit past the expression's records.
        for (uint32_t word = 0; word < words; word += 1)
        {
            const uint32_t live = count > word * 64 ? (count - word * 64 < 64 ? count - word * 64 : 64) : 0;
            const uint64_t range = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
            counts->own_lists_past += ((loop->tree_bits[word] | loop->operator_bits[word]) & ~range) != 0;
        }
        // And a record whose token's parent the tree's tests read (a comparison's token, `!`, `=`) that no listed
        // operator has as an operand has its test run here all the same, and it finds nothing.
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint32_t token = tokens[r + 1 == count ? root : first + r];
            const uint8_t role = token < token_count ? roles[token] : PARSE_ROLE_NONE;
            const CheckOp op = (CheckOp)codes[r];
            if ((check_common_comparison_role(role) || op == CHECK_OP_NOT || op == CHECK_OP_ASSIGN) && (read[r >> 6] >> (r & 63) & 1) == 0)
            {
                counts->own_tree_checked += 1;
                counts->own_tree_wrong += node_tests_tree(s, op, token, role) != 0;
            }
        }
    }
    if (s->own_quiet_bits != NULL)
    {
        // The reader's verify: an operator the loop left out for its constants has its tests run here all the same,
        // and they find nothing and send nothing. What they count is put back.
        uint64_t unsent[BUSTER_NODE_MACHINE_MOST / 64 + 1];
        for (uint32_t word = 0; word < words; word += 1)
        {
            unsent[word] = 0;
        }
        const uint64_t calls = counts->own_calls;
        const uint64_t constants = counts->own_constants;
        const uint64_t thirds = counts->own_thirds;
        uint64_t triggers[BUSTER_ARRAY_LENGTH(counts->own_triggers)];
        memcpy(triggers, counts->own_triggers, sizeof(triggers));
        for (uint32_t word = 0; word < words; word += 1)
        {
            for (uint64_t pending = s->own_quiet_bits[word]; pending != 0; pending &= pending - 1)
            {
                const uint32_t r = word * 64 + (uint32_t)__builtin_ctzll(pending);
                uint32_t sends = 0;
                const uint32_t node = r + 1 == count ? root : first + r;
                const bool call = (CheckOp)codes[r] == CHECK_OP_CALL;
                const bool cast = (CheckOp)codes[r] == CHECK_OP_CAST;
                // An ordered comparison the orders' rule left out has an operand that is a pointer once converted;
                // one the quiet rule left out has two plain integers.
                const bool order = (CheckOp)codes[r] >= CHECK_OP_LESS && (CheckOp)codes[r] <= CHECK_OP_GREATER_EQUAL &&
                                   (loop->operands[r] & BUSTER_NODE_TESTS_OPERAND_CONVERTS) != 0;
                const uint64_t found = node_tests_operator(s, r, node, loop->operands[r], unsent, &sends, counts);
                const bool wrong = found != 0 || sends != 0;
                counts->own_quiet_checked += !call && !cast && !order;
                counts->own_quiet_wrong += !call && !cast && !order && wrong;
                counts->own_quiet_casts_checked += cast;
                counts->own_quiet_casts_wrong += cast && wrong;
                counts->own_quiet_orders_checked += order;
                counts->own_quiet_orders_wrong += order && wrong;
                // A call left out has no constant the rule doesn't take, by its own list of arguments.
                counts->own_quiet_calls_checked += call;
                counts->own_quiet_calls_wrong += call && wrong;
                counts->own_quiet_calls_loud += call && node_tests_call_loud(s, node);
            }
        }
        counts->own_calls = calls;
        counts->own_constants = constants;
        counts->own_thirds = thirds;
        memcpy(counts->own_triggers, triggers, sizeof(triggers));
    }
    // The unsequenced test needs two writes, or one below the expression's top: a lone write that is the root, or the
    // root's operand, has every other node but the root in its subtree.
    const uint32_t top = count >= 2 ? node_tests_record(machine, graph->operand0[root]) : UINT32_MAX;
    const bool sequenced = loop->writes == 0 || (loop->writes == 1 && (loop->last_write + 1 == count || loop->last_write == top));
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
    counts->own_trees += trees;
    counts->own_operators += operators;
    counts->own_visited += visits != 0;
    counts->own_visits += visits;
    tests |= node_tests_root(s, root, context, count, loop->lowest, loop->highest, unsequenced, walk_count, loop->class_bits, loop->type_bits, counts);
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
    if (token < parser->tokens.count && parse_source_index(&parser->tokens, token) < pp->output.count && pp->output.locations != NULL)
    {
        const char* name = NULL;
        uint32_t length = 0;
        line = pp_presumed(pp, pp->output.locations[parse_source_index(&parser->tokens, token)], &name, &length);
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
    // The warning whose event refuses it, the first in its tokens' span: CHECK_WARNING_ID_COUNT for none found there.
    const uint32_t found = (tests >> NODE_CENSUS_EVENT & 1) != 0 ? node_census_event_id(s) : UINT32_MAX;
    const uint32_t event = found < CHECK_WARNING_ID_COUNT ? found : CHECK_WARNING_ID_COUNT;
    if (census->records != NULL && census->record_count < census->record_capacity)
    {
        const uint32_t token = s->program->input->graph.tokens[root];
        census->records[census->record_count++] = (NodeCensusRecord){
            .line = node_census_line(s->pp, s->program->parser, token),
            .root = context,
            .tests = tests,
            .event = event,
        };
    }
    if ((tests >> NODE_CENSUS_EVENT & 1) != 0)
    {
        census->event_ids[event] += 1;
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
        root = whole && scalar && !brace && !node_census_labelled(program->input, parser, control->token) ? NODE_CENSUS_ROOT_INITIALIZER : NODE_CENSUS_ROOT_BRACE;
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
        const uint32_t node = check_control_expression_node(control);
        if (node < count)
        {
            s->node_controls[node] = value;
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
    // A bit a token: a kept warning event of the reader's at it; the events taken in so far; and the first and the last
    // token with one (UINT32_MAX and 0 for none): an expression whose tokens are all before or all after them has none.
    uint64_t* event_bits;
    uint32_t events_seen;
    uint32_t event_low;
    uint32_t event_high;
    // The outermost brace pairs that hold a diagnostic or floating-point pragma (node_census_region's bodies), each its
    // opening's and its closing's tokens.
    uint32_t* pragma_openings;
    uint32_t* pragma_closings;
    uint32_t pragma_count;
    // A bit a token: inside a statement expression (CheckInput.regions, the reader's): a root there is a nested control's.
    uint64_t* nested_bits;
    // The tests on the window (BUSTER_NODE_READER_WINDOW_TESTS; node_tests_window_chunk), a byte a record of the front's
    // chunk, each column with 64 zero bytes before its first record's: its type's byte (node_census_type_bits', less
    // the floating bit); what its value is (BUSTER_NODE_TESTS_VALUE_*) and, above that, what it is to a constant's
    // tests as an operand (BUSTER_NODE_TESTS_KIND_BITS); then what the loop makes of them: what it is as an operand, its
    // operands' such bytes together, and its part of the set's tests (BUSTER_NODE_TESTS_SET_*). And four masks a block
    // of 64 records, four words a block: the operators the loop's quiet rule left out, the operators listed for their
    // operands' tree's tests, the operators with tests of their own, and the writes.
    uint8_t* window_types;
    uint8_t* window_values;
    uint8_t* window_properties;
    uint8_t* window_operands;
    uint8_t* window_sets;
    uint64_t* window_masks;
    // A chunk's casts over a call the loop left out, for the asks (node_tests_window_asks): their records' numbers, one
    // after another, with room for a step's store past the last.
    uint16_t* window_asks;
    // A byte a declaration's line of the kernel's (NodeFacts.lines, by a line's number; BUSTER_NODE_TESTS_LINE_*):
    // whether the byte is made, and whether the tests refuse the declaration for its attributes.
    uint8_t* line_bytes;
    // By root node (`node_capacity` of them), the span of tokens the census's code gave the expression at reader time:
    // the tests on the window have the same.
    uint32_t* span_lows;
    uint32_t* span_highs;
    uint32_t node_capacity;
    // What the loop made of each of a batch's expressions (BUSTER_NODE_FACTS_READER_EXPRESSIONS of them), and their
    // lists' bits one expression's words after another's.
    NodeTestsLoop* loops;
    uint64_t* tree_bits;
    uint64_t* operator_bits;
    uint64_t* quiet_bits;
    // The batch's expressions that the tests that read T's records run on (node_tests_window_chunk), in their order.
    uint32_t* called;
    // Whether an operator the loop leaves out for its constants, and a record the tree's tests read that no listed
    // operator has as an operand, has its tests run all the same, to have none (the reader's verify).
    bool verify_quiet;
};

// A window's record's value's byte: a constant a test may read, a bit-field, a declaration the tests refuse; and, of a
// name's leaf, a declaration a call of which has semantics of its own (BUSTER_NODE_TESTS_LINE_LOUD), which a call reads
// of its callee.
#define BUSTER_NODE_TESTS_VALUE_CONSTANT 0x01u
#define BUSTER_NODE_TESTS_VALUE_BIT_FIELD 0x02u
#define BUSTER_NODE_TESTS_VALUE_DECLARATION 0x04u
#define BUSTER_NODE_TESTS_VALUE_LOUD 0x40u
_Static_assert((BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_CONSTANT) & 0x47u) == 0, "a record's kind sits beside its value's four bits");
// A window's record's type's byte has the six bits of its type's word (BUSTER_NODE_CENSUS_TYPE_*: a special type, an
// aggregate, an enumeration, a pointer, a pointer to void, an array), and two more: its type is void; and, of a call,
// the kernel found its arguments quiet (its record's exponent, BUSTER_NODE_FACTS_QUIET_CALL).
#define BUSTER_NODE_TESTS_TYPE_VOID 0x01u
#define BUSTER_NODE_TESTS_TYPE_QUIET 0x10u
// A declaration's line's byte: made; a declaration the tests refuse; one a call of which has semantics of its own (the
// census's code's fact of the declaration, asked at the line's first leaf where its type calls); and whether it asks
// for its result, the same code's other fact, asked at the first cast over a call of it the loop left out.
#define BUSTER_NODE_TESTS_LINE_KNOWN 0x01u
#define BUSTER_NODE_TESTS_LINE_FLAGGED 0x02u
#define BUSTER_NODE_TESTS_LINE_LOUD 0x04u
#define BUSTER_NODE_TESTS_LINE_RESULT_KNOWN 0x08u
#define BUSTER_NODE_TESTS_LINE_RESULT 0x10u
// And for C on the window (c_check_node_machine_window.c): whether the declaration's object has a symbol of its own (a
// global's; NodeMachine.object_symbols), made with the byte.
#define BUSTER_NODE_TESTS_LINE_SYMBOL 0x20u
// And for a summary's side facts (c_check_node_machine_rows.c's node_rows_window_summary), what the return flow asks
// of a call's callee's declaration (check_program_return_call), a function's alone: a plain one, a call of whose name
// has no fact (it is not declared noreturn, and its name is none of the library's that the flow leaves unknown); and
// one declared noreturn, a call of which stops. A function with neither is one of those names, unknown as a call
// through a value is. Made with the byte, where the tapes are made. The sweep copies a name's leaf's line's byte to
// the rows' column (NodeFactsReader.window_x_lines), whose readers know the two by the rows' names.
#define BUSTER_NODE_TESTS_LINE_PLAIN 0x40u
#define BUSTER_NODE_TESTS_LINE_STOPS 0x80u
_Static_assert(BUSTER_NODE_TESTS_LINE_PLAIN == BUSTER_NODE_ROWS_LINE_PLAIN && BUSTER_NODE_TESTS_LINE_STOPS == BUSTER_NODE_ROWS_LINE_STOPS,
               "a line's byte's bits are the rows' column's");
// A window's record's part of the set's tests: an operator outside the set, an address taken, a special type, an
// aggregate copied, a bit-field, an array, a leaf whose declaration the tests refuse.
#define BUSTER_NODE_TESTS_SET_UNCODED 0x01u
#define BUSTER_NODE_TESTS_SET_ADDRESS 0x02u
#define BUSTER_NODE_TESTS_SET_SPECIAL 0x04u
#define BUSTER_NODE_TESTS_SET_AGGREGATE 0x08u
#define BUSTER_NODE_TESTS_SET_BIT_FIELD 0x10u
#define BUSTER_NODE_TESTS_SET_ARRAY 0x20u
#define BUSTER_NODE_TESTS_SET_DECLARATION 0x40u
_Static_assert(BUSTER_NODE_TESTS_CLASS_UNCODED == BUSTER_NODE_TESTS_SET_UNCODED && BUSTER_NODE_TESTS_CLASS_ADDRESS == BUSTER_NODE_TESTS_SET_ADDRESS,
               "the set takes the uncoded and address bits from the operator's class");
// The masks a block, one line of the cache: the operators the loop left out of its list (the quiet rule's, and the calls
// with no test to make), the operators listed for the tree's tests, the operators listed for their own, the writes, the
// records that as a statement's top leave its test nothing (a write, a record of type void, a call left out whose
// callee asks for no result and whose discard is no further back than the block before).
#define BUSTER_NODE_TESTS_MASK_QUIET 0u
#define BUSTER_NODE_TESTS_MASK_TREE 1u
#define BUSTER_NODE_TESTS_MASK_OPERATORS 2u
#define BUSTER_NODE_TESTS_MASK_WRITES 3u
#define BUSTER_NODE_TESTS_MASK_DISCARDS 4u
#define BUSTER_NODE_TESTS_MASK_QUIET_CALLS 5u
#define BUSTER_NODE_TESTS_MASK_QUIET_CASTS 6u
#define BUSTER_NODE_TESTS_MASK_QUIET_ORDERS 7u
#define BUSTER_NODE_TESTS_MASKS 8u

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
        s->given_declaration = BUSTER_CHECK_NONE;
        tests->types = types;
        tests->event_low = UINT32_MAX;
        tests->program.input = input;
        tests->program.parser = reader->parser;
        tests->program.types = reader->types;
        tests->program.object_symbols = (SymbolId*)machine->object_symbols;
        tests->program.object_count = machine->object_count;
        s->program = &tests->program;
        s->pp = reader->pp;
        s->records = machine;
        if ((reader->pp->options.node_reader & BUSTER_NODE_READER_WINDOW_ROWS) != 0)
        {
            // The functions the return flow leaves unknown from one of their declarations on: its own table, made as
            // it makes it (check_program_return_unknowns reads the program's input and its identifiers alone), for a
            // summary's return fact (node_rows_window_summary). Null where the unit has none.
            CheckProgram flow = tests->program;
            uint32_t* unknowns = NULL;
            flow.identifiers = reader->pp->table;
            room = check_program_return_unknowns(&flow, arena, &unknowns);
            types->return_unknowns = unknowns;
        }
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
        if ((reader->pp->options.node_reader & BUSTER_NODE_READER_WINDOW_TESTS) != 0)
        {
            // The window's columns: a chunk's records, 64 zero bytes before them (a block's operands in the block before
            // the first, and no operand's byte) and a block's after.
            uint8_t** columns[] = { &tests->window_types, &tests->window_values, &tests->window_properties, &tests->window_operands, &tests->window_sets };
            for (uint32_t c = 0; room && c < BUSTER_ARRAY_LENGTH(columns); c += 1)
            {
                uint8_t* column = arena_push_array(arena, (uint64_t)BUSTER_NODE_FRONT_RECORDS + 192, 1, 64);
                room = column != NULL;
                if (room)
                {
                    memset(column, 0, (uint64_t)BUSTER_NODE_FRONT_RECORDS + 192);
                    *columns[c] = column + 64;
                }
            }
            tests->window_masks = arena_push_array(arena, BUSTER_NODE_TESTS_MASKS * ((uint64_t)BUSTER_NODE_FRONT_RECORDS / 64 + 2), sizeof(uint64_t), 64);
            tests->window_asks = arena_push_array(arena, (uint64_t)BUSTER_NODE_FRONT_RECORDS + 64, sizeof(uint16_t), 64);
            tests->span_lows = arena_push_array(arena, (uint64_t)node_capacity + 1, sizeof(uint32_t), _Alignof(uint32_t));
            tests->span_highs = arena_push_array(arena, (uint64_t)node_capacity + 1, sizeof(uint32_t), _Alignof(uint32_t));
            tests->node_capacity = node_capacity;
            // A batch's expressions' loops' results, and their lists' bits: a word an expression and one more each 64 of
            // its records.
            const uint64_t words = (uint64_t)BUSTER_NODE_FRONT_RECORDS / 64 + BUSTER_NODE_FACTS_READER_EXPRESSIONS + 1;
            tests->loops = arena_push_array(arena, BUSTER_NODE_FACTS_READER_EXPRESSIONS, sizeof(NodeTestsLoop), _Alignof(NodeTestsLoop));
            tests->tree_bits = arena_push_array(arena, words, sizeof(uint64_t), 64);
            tests->operator_bits = arena_push_array(arena, words, sizeof(uint64_t), 64);
            tests->quiet_bits = arena_push_array(arena, words, sizeof(uint64_t), 64);
            tests->called = arena_push_array(arena, (uint64_t)BUSTER_NODE_FACTS_READER_EXPRESSIONS + 1, sizeof(uint32_t), _Alignof(uint32_t));
            tests->verify_quiet = (reader->pp->options.node_reader & BUSTER_NODE_READER_VERIFY) != 0;
            room = room && tests->window_masks != NULL && tests->window_asks != NULL && tests->span_lows != NULL && tests->span_highs != NULL && tests->loops != NULL &&
                   tests->tree_bits != NULL && tests->operator_bits != NULL && tests->quiet_bits != NULL && tests->called != NULL;
            if (room)
            {
                // A block's word is read with the next block's: the last has zeros after it.
                memset(tests->window_masks, 0, BUSTER_NODE_TESTS_MASKS * ((uint64_t)BUSTER_NODE_FRONT_RECORDS / 64 + 2) * sizeof(uint64_t));
            }
        }
        room = room && s->type_bits != NULL && s->declaration_facts != NULL && tests->event_bits != NULL && tests->pragma_openings != NULL && tests->pragma_closings != NULL &&
               tests->nested_bits != NULL && s->type_ranges != NULL;
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

// The tests' state reading `input` for the reader's output: the machine with no node of the walk's has a graph of its
// own (c_check_node_machine_alone.c), and the census's code reads an expression's nodes there.
static void node_census_reader_input(NodeCensusReader* tests, const CheckInput* input)
{
    tests->program.input = input;
}

// The reader's warning events since the last chunk, into the tests' bits. Out of line: its caller is inlined into the
// front's own function, whose code this would move with each change here (on the 9700X its window's ticks read 1.3%
// to 1.6% higher with ten instructions more inline).
static __attribute__((noinline)) void node_census_reader_chunk(NodeCensusReader* tests, const CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const uint32_t tokens = reader->parser->tokens.count;
    for (; tests->events_seen < reader->warning_events.count; tests->events_seen += 1)
    {
        const CheckWarningEvent* event = reader->warning_events.events + tests->events_seen;
        if (event->token < tokens && node_census_event_kept(reader->pp, input, event))
        {
            tests->event_bits[event->token >> 6] |= UINT64_C(1) << (event->token & 63);
            tests->event_low = event->token < tests->event_low ? event->token : tests->event_low;
            tests->event_high = event->token > tests->event_high ? event->token : tests->event_high;
        }
    }
}

// Whether a root at `token` is inside a statement expression: a nested control's.
static inline bool node_census_reader_nested(const NodeCensusReader* tests, uint32_t token)
{
    return token < tests->program.parser->tokens.count && (tests->nested_bits[token >> 6] >> (token & 63) & 1) != 0;
}

// The tests of an expression that its tokens alone give, its root's `root_token` and its span `span_low` to `span_high`:
// a body with a pragma (node_census_region's), and the reader's own warning events so far at its tokens.
static inline uint64_t node_census_reader_marks(const NodeCensusReader* tests, uint32_t root_token, uint32_t span_low, uint32_t span_high)
{
    bool region = false;
    for (uint32_t i = 0; i < tests->pragma_count && !region; i += 1)
    {
        region = root_token > tests->pragma_openings[i] && root_token < tests->pragma_closings[i];
    }
    // The span's first and last words of the events' bits, which are one for most expressions, and the words between
    // them, which few have: no branch but for those.
    const bool some = span_low <= span_high;
    const uint32_t first = some ? span_low >> 6 : 0;
    const uint32_t last = some ? span_high >> 6 : 0;
    const uint64_t from = tests->event_bits[first] & (~UINT64_C(0) << (span_low & 63));
    const uint64_t to = tests->event_bits[last] & (~UINT64_C(0) >> (63 - (span_high & 63)));
    uint64_t events = first == last ? from & to : from | to;
    for (uint32_t word = first + 1; word < last; word += 1)
    {
        events |= tests->event_bits[word];
    }
    return (region ? UINT64_C(1) << NODE_CENSUS_REGION : 0) | (some && events != 0 ? UINT64_C(1) << NODE_CENSUS_EVENT : 0);
}

static uint64_t node_census_reader_expression(NodeCensusReader* tests, uint32_t root, CheckOp op, uint32_t token, uint32_t declaration, TypeId declared, uint8_t* kind,
                                              const struct NodeTestsLoop* given)
{
    NodeCensusScratch* s = &tests->scratch;
    const CheckProgram* program = s->program;
    const Parser* parser = program->parser;
    const CheckGraph* graph = &program->input->graph;
    // An initializer's whole value is a scalar object's without a brace before it, as node_census_root has it.
    const bool nested = node_census_reader_nested(tests, token);
    const CheckInput* input = program->input;
    NodeCensusRoot context = nested ? NODE_CENSUS_ROOT_OTHER : node_census_root_kind(op);
    if (__builtin_expect(input->cleanup_count != 0, 0) && context == NODE_CENSUS_ROOT_RETURN && node_census_cleans(input, token))
    {
        // A return's value under a cleanup is another control's, as the program makes it (node_census_cleans).
        context = NODE_CENSUS_ROOT_OTHER;
    }
    if (op == CHECK_OP_INITIALIZE && !nested)
    {
        // A scalar's type: no struct, union or array, by its byte.
        const uint8_t declared_bits = declared != 0 ? node_census_type_bits(s, declared) : 0;
        const bool scalar = declared != 0 && (declared_bits & (BUSTER_NODE_CENSUS_TYPE_AGGREGATE | BUSTER_NODE_CENSUS_TYPE_ARRAY)) == 0;
        // A brace between the object's name and its initializer's first token (the reader's, CheckInput.
        // initializer_begins), which is where the plan's part begins (node_census_root's): one after it is its value's
        // own, a cast's type's (`(enum { V })0`, #3202). The value's node's token where the reader has no such token.
        const uint32_t value_token = root < graph->count ? graph->tokens[root] : 0;
        const uint32_t begin = declaration < input->token_count && check_sparse_present(input->initializer_begins) ? check_sparse_get(input->initializer_begins, declaration) :
                                                                                                                     BUSTER_CHECK_NONE;
        const uint32_t limit = begin != BUSTER_CHECK_NONE && begin < value_token ? begin : value_token;
        bool brace = declaration >= parser->tokens.count;
        for (uint32_t t = declaration + 1; !brace && t < limit && t < parser->tokens.count; t += 1)
        {
            brace = parser->tokens.kinds[t] == PARSE_KIND_L_BRACE;
        }
        // And an object with an asm label's is its declaration's to refuse (node_census_labelled), as a brace's is.
        context = scalar && !brace && !node_census_labelled(input, parser, declaration) ? NODE_CENSUS_ROOT_INITIALIZER : NODE_CENSUS_ROOT_BRACE;
    }
    s->given_declared = context == NODE_CENSUS_ROOT_INITIALIZER ? declared : 0;
    uint32_t walked = 0;
    // The machine's own form where the window gives its loop's results, and else the census's code on the view, which
    // is its reference.
    uint64_t bits = given != NULL ? node_tests_expression(s, root, context, &walked, tests->types, given) : node_census_expression(s, root, 0, context, &walked);
    s->given_declared = 0;
    // An initializer's tokens begin after its object's name, as after the pipeline (node_census_initializer_low, #3201):
    // a declarator's event that the reader has raised by now (an attribute it ignores) refuses its initializer here too.
    const uint32_t marks_low = context == NODE_CENSUS_ROOT_INITIALIZER && declaration < parser->tokens.count ? node_census_initializer_low(s->span_low, declaration) : s->span_low;
    bits |= node_census_reader_marks(tests, root < graph->count ? graph->tokens[root] : 0, marks_low, s->span_high);
    // A node the records don't hold refuses the expression (none does: T takes one range of nodes, whole).
    bits |= s->missed != 0 ? UINT64_C(1) << NODE_CENSUS_SIZE : 0;
    tests->types->tested_missed += s->missed;
    s->missed = 0;
    if (given == NULL && tests->span_lows != NULL && root < tests->node_capacity)
    {
        // The span of the expression's tokens as the tests here have it, for the tests on the window to be compared with.
        tests->span_lows[root] = s->span_low;
        tests->span_highs[root] = s->span_high;
    }
    *kind = (uint8_t)context;
    return bits;
}

// The tests on the window the front wrote (measure-node-machine --shadow --window-tests; BUSTER_NODE_READER_WINDOW_TESTS).

// Room for a byte a declaration's line of the kernel's (`capacity` lines; NodeCensusReader.line_bytes), from `arena`:
// whether there was. A line's byte is read by a gather, which reads the three bytes after it.
static bool node_tests_window_lines(struct NodeCensusReader* tests, Arena* arena, uint32_t capacity)
{
    tests->line_bytes = arena_push_array(arena, (uint64_t)capacity + 4, 1, 64);
    const bool room = tests->line_bytes != NULL;
    if (room)
    {
        memset(tests->line_bytes, 0, (uint64_t)capacity + 4);
    }
    return room;
}

// The kernel's row of type `type` (node_facts_row_of's).
static inline const NodeFactsRow* node_tests_window_row(const NodeFacts* facts, TypeId type)
{
    const uint32_t index = type_index(type);
    return facts->rows + (index < facts->row_count ? index : facts->row_count);
}

// What the tests keep of the declaration that the name's leaf at `token` names at its line's first leaf: its line's bits
// but the first. Whether the tests refuse it for its attributes (node_census_leaf's test of them): the census's byte of
// the declaration, which its code makes at the declaration's first ask. And, where its type calls (`calls`: a
// function's or a pointer's to one, by its type's row), whether a call of it has semantics of its own: the same byte's
// second fact, which a call's test asks.
static __attribute__((noinline)) uint8_t node_tests_window_line(NodeCensusScratch* s, uint32_t token, bool calls)
{
    const CheckProgram* program = s->program;
    const uint32_t declaration = node_census_declaration(program, token);
    uint8_t bits = 0;
    if (declaration != BUSTER_CHECK_NONE)
    {
        const uint32_t name = program->parser->tokens.names[token];
        const uint8_t flagged = node_census_declaration_facts(s, declaration, name, BUSTER_NODE_CENSUS_DECLARATION_FLAGGED_KNOWN);
        const uint8_t callee = calls ? node_census_declaration_facts(s, declaration, name, BUSTER_NODE_CENSUS_DECLARATION_CALLEE_KNOWN) : 0;
        bits |= (flagged & BUSTER_NODE_CENSUS_DECLARATION_FLAGGED) != 0 ? BUSTER_NODE_TESTS_LINE_FLAGGED : 0;
        bits |= (callee & BUSTER_NODE_CENSUS_DECLARATION_CALLEE) != 0 ? BUSTER_NODE_TESTS_LINE_LOUD : 0;
    }
    return bits;
}

// The same line's two bits for the return flow (BUSTER_NODE_TESTS_LINE_PLAIN and _STOPS), of the declaration the
// name's leaf at `token` names: check_program_return_call's own questions of a direct call's declaration, a
// function's, in its order: declared noreturn (CHECK_INPUT_FUNCTION_NORETURN), which stops; or else a library's name
// the catalogue marks (check_program_return_library_search), which has neither bit; or else plain. Another
// declaration's line has neither.
static __attribute__((noinline)) uint8_t node_tests_window_returns(const NodeCensusScratch* s, uint32_t token)
{
    const CheckProgram* program = s->program;
    const CheckInput* input = program->input;
    const uint32_t declaration = node_census_declaration(program, token);
    const bool function = declaration != BUSTER_CHECK_NONE && declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                          input->function_flags != NULL;
    uint8_t bits = 0;
    if (function && (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0)
    {
        bits = BUSTER_NODE_TESTS_LINE_STOPS;
    }
    else if (function && check_program_return_library_search(s->pp->table, program->parser->tokens.names[declaration]) == 0)
    {
        bits = BUSTER_NODE_TESTS_LINE_PLAIN;
    }
    return bits;
}

// Whether the callee at the window's record `callee`, a name's leaf with a line, asks for its result
// (node_census_wants_result's), as its line's byte's two bits: its declaration's fact, which the census's code makes,
// kept in the line's byte.
static __attribute__((noinline)) uint8_t node_tests_window_wants(NodeCensusReader* tests, const NodeFront* front, uint32_t callee)
{
    const NodeCensusScratch* s = &tests->scratch;
    const CheckProgram* program = s->program;
    const uint32_t token = front->tokens[callee];
    const uint32_t declaration = node_census_declaration(program, token);
    const bool result = declaration != BUSTER_CHECK_NONE &&
                        (node_census_declaration_facts(s, declaration, program->parser->tokens.names[token], BUSTER_NODE_CENSUS_DECLARATION_RESULT_KNOWN) &
                         BUSTER_NODE_CENSUS_DECLARATION_RESULT) != 0;
    const uint8_t bits = (uint8_t)(BUSTER_NODE_TESTS_LINE_RESULT_KNOWN | (result ? BUSTER_NODE_TESTS_LINE_RESULT : 0));
    tests->line_bytes[front->window_entries[callee]] |= bits;
    return bits;
}

// The bytes of a chunk's records that the tests' loop reads, sixteen records a step, each from where it is once the
// kernel has made the chunk's records in place (`state`'s, by the front's record):
// - a record's type's byte, from its type's word's six bits (BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT), which its record has
//   whichever path wrote it;
// - its value's byte (BUSTER_NODE_TESTS_VALUE_*): a constant a test may read, by its standing and its object (an address
//   constant only where it is null or the record doesn't hold it whole); a bit-field, by its flags; and a declaration
//   the tests refuse: a register object's, by its flags, or a name's whose attributes the tables don't model, by its
//   line's byte (NodeCensusReader.line_bytes), which the census's code makes at the line's first leaf. A name's leaf
//   without a line (the lines were full, or its value is past the columns) asks the census's code each time; a name's
//   leaf a call of which has semantics of its own, by its line's byte's other bit (a leaf without a line, whatever it
//   names); and its kind as an operand of the loop's quiet rule (BUSTER_NODE_TESTS_KIND_*), by its type's word and
//   what its exponent says of an integer constant's value.
// A record of no expression the kernel made in place (NodeFactsReader.place_taken) has zeros, as have the bytes from
// the chunk's last record to its block's end.
// With `contexts` (a constant where this is inlined), C on the window's bytes of the same records too
// (c_check_node_machine_window.c), from the same gathers, and a declaration's line's byte says whether its object has
// a symbol. With `rows` (another), each record's type too, a dword of the same lines' halves, for X's rows on the
// window (c_check_node_machine_rows.c), which compare it with the record's target.
// The sweep's four columns' places among a record's line's first eight dwords, and what reading sixteen lines' first
// halves a step asks: a line is sixteen dwords, and the window's array is a whole number of steps, so a step whose
// first record is one of a chunk's reads lines of the array alone.
#define BUSTER_NODE_TESTS_COLUMN_WORD 1
#define BUSTER_NODE_TESTS_COLUMN_FLAGS 2
#define BUSTER_NODE_TESTS_COLUMN_OBJECT 3
#define BUSTER_NODE_TESTS_COLUMN_STANDING 6
_Static_assert(offsetof(NodeFactsRecord, word) == 4 * BUSTER_NODE_TESTS_COLUMN_WORD && offsetof(NodeFactsRecord, flags) == 4 * BUSTER_NODE_TESTS_COLUMN_FLAGS &&
               offsetof(NodeFactsRecord, object) == 4 * BUSTER_NODE_TESTS_COLUMN_OBJECT && offsetof(NodeFactsRecord, standing) == 4 * BUSTER_NODE_TESTS_COLUMN_STANDING &&
               sizeof(NodeFactsRecord) == 64 && BUSTER_NODE_FACTS_WINDOW % 16 == 0 && BUSTER_NODE_FRONT_RECORDS <= BUSTER_NODE_FACTS_WINDOW,
               "the sweep's four columns are dwords of a line's first half, and a step's sixteen lines are the window's");

static inline __attribute__((always_inline)) void node_tests_window_bytes_by(NodeCensusReader* tests, const struct NodeFactsReader* state, const NodeFront* front,
                                                                           uint64_t* made, const bool contexts, const bool rows, const bool summaries)
{
    NodeCensusScratch* s = &tests->scratch;
    const uint8_t* const records = (const uint8_t*)state->facts.records;
    const uint32_t count = front->count;
    const uint32_t end = (count + 63) & ~63u;
    const __m512i zero = _mm512_setzero_si512();
    // Each lane's record's line, in bytes from the step's first (the reader's verify's gathers).
    const __m512i lines = _mm512_set_epi32(960, 896, 832, 768, 704, 640, 576, 512, 448, 384, 320, 256, 192, 128, 64, 0);
    // The four columns' dwords of four records, two a register (a record's eight dwords each): each column's four
    // together, the word's, the flags', the object's and the standing's.
    const __m512i columns = _mm512_set_epi32(16 + BUSTER_NODE_TESTS_COLUMN_STANDING + 8, 16 + BUSTER_NODE_TESTS_COLUMN_STANDING, BUSTER_NODE_TESTS_COLUMN_STANDING + 8,
                                             BUSTER_NODE_TESTS_COLUMN_STANDING, 16 + BUSTER_NODE_TESTS_COLUMN_OBJECT + 8, 16 + BUSTER_NODE_TESTS_COLUMN_OBJECT,
                                             BUSTER_NODE_TESTS_COLUMN_OBJECT + 8, BUSTER_NODE_TESTS_COLUMN_OBJECT, 16 + BUSTER_NODE_TESTS_COLUMN_FLAGS + 8,
                                             16 + BUSTER_NODE_TESTS_COLUMN_FLAGS, BUSTER_NODE_TESTS_COLUMN_FLAGS + 8, BUSTER_NODE_TESTS_COLUMN_FLAGS,
                                             16 + BUSTER_NODE_TESTS_COLUMN_WORD + 8, 16 + BUSTER_NODE_TESTS_COLUMN_WORD, BUSTER_NODE_TESTS_COLUMN_WORD + 8,
                                             BUSTER_NODE_TESTS_COLUMN_WORD);
    // And the four records' types, a line's first dword, in the permute's first lane (the rows' column); and their
    // payloads, a line's third quadword, in its first four (the rows' values' column).
    const __m512i type_columns = _mm512_set_epi32(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 + 8, 16, 8, 0);
    const __m512i payload_columns = _mm512_set_epi64(0, 0, 0, 0, 8 + 6, 8 + 2, 6, 2);
    uint64_t lines_made = 0;
    uint64_t values_checked = 0;
    uint64_t values_wrong = 0;
    const NodeMachine* machine = state->facts.machine;
    for (uint32_t x = 0; x < end; x += 16)
    {
        __m128i own_bytes = _mm_setzero_si128();
        __m128i value_bytes = _mm_setzero_si128();
        __m128i class_bytes = _mm_setzero_si128();
        __m128i fact_bytes = _mm_setzero_si128();
        __m128i type_kind_bytes = _mm_setzero_si128();
        __m128i standing_bytes = _mm_setzero_si128();
        __m128i flag_bytes = _mm_setzero_si128();
        __m128i line_bytes = _mm_setzero_si128();
        __m512i type_dwords = zero;
        __m512i value_low = zero;
        __m512i value_high = zero;
        if (x < count)
        {
            const uint32_t live = count - x < 16 ? count - x : 16;
            const __mmask16 lanes = (__mmask16)((1u << live) - 1);
            const __m128i taken = _mm_maskz_loadu_epi8(lanes, state->place_taken + x);
            const __mmask16 placed = _mm_test_epi8_mask(taken, taken);
            const uint8_t* const base = records + 64 * (uint64_t)x;
            // Four columns of the step's sixteen records (a record's word, flags, object and standing), by sixteen loads
            // and twelve permutes: a gather of sixteen is 64 uops on Zen 5 (docs/coding-style.md, "SIMD and Zen 5"), and
            // the four it took 256 a step. A line's first half is read whole, two records a register; one permute of two
            // registers puts each column's four dwords of four records in a lane; and a column is that lane of the four
            // permutes, the records the kernel didn't place read as zeros, as a masked gather's were. A step reads its
            // sixteen lines whether placed or not: the window's own array's (the assert above the sweep).
            __m512i fours[4];
            __m128i types[4] = { _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128() };
            __m256i payloads[4] = { _mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256() };
            for (uint32_t four = 0; four < 4; four += 1)
            {
                const uint8_t* const at = base + 4 * sizeof(NodeFactsRecord) * (uint64_t)four;
                const __m512i first = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*)(const void*)at)),
                                                         _mm256_loadu_si256((const __m256i*)(const void*)(at + sizeof(NodeFactsRecord))), 1);
                const __m512i second = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*)(const void*)(at + 2 * sizeof(NodeFactsRecord)))),
                                                          _mm256_loadu_si256((const __m256i*)(const void*)(at + 3 * sizeof(NodeFactsRecord))), 1);
                fours[four] = _mm512_permutex2var_epi32(first, columns, second);
                if (rows)
                {
                    types[four] = _mm512_castsi512_si128(_mm512_permutex2var_epi32(first, type_columns, second));
                    payloads[four] = _mm512_castsi512_si256(_mm512_permutex2var_epi64(first, payload_columns, second));
                }
            }
            if (rows)
            {
                // The step's sixteen types, four a lane, the records the kernel didn't place as zeros like the columns.
                const __m512i joined = _mm512_inserti32x4(_mm512_inserti32x4(_mm512_inserti32x4(_mm512_castsi128_si512(types[0]), types[1], 1), types[2], 2), types[3], 3);
                type_dwords = _mm512_maskz_mov_epi32(placed, joined);
            }
            const __m512i words_flags_low = _mm512_shuffle_i32x4(fours[0], fours[1], 0x44);
            const __m512i words_flags_high = _mm512_shuffle_i32x4(fours[2], fours[3], 0x44);
            const __m512i objects_standings_low = _mm512_shuffle_i32x4(fours[0], fours[1], 0xee);
            const __m512i objects_standings_high = _mm512_shuffle_i32x4(fours[2], fours[3], 0xee);
            const __m512i words = _mm512_maskz_shuffle_i32x4(placed, words_flags_low, words_flags_high, 0x88);
            const __m512i flags = _mm512_maskz_shuffle_i32x4(placed, words_flags_low, words_flags_high, 0xdd);
            const __m512i objects = _mm512_maskz_shuffle_i32x4(placed, objects_standings_low, objects_standings_high, 0x88);
            const __m512i standings = _mm512_maskz_shuffle_i32x4(placed, objects_standings_low, objects_standings_high, 0xdd);
            // Its type's byte: the word's six bits at the byte's bits 1 to 3 and 5 to 7, and whether its type is void, by
            // the word's kind.
            const __m512i six = _mm512_srli_epi32(words, BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT);
            const __m512i own = _mm512_or_si512(_mm512_slli_epi32(_mm512_and_si512(six, _mm512_set1_epi32(0x07)), 1),
                                                _mm512_slli_epi32(_mm512_and_si512(six, _mm512_set1_epi32(0x38)), 2));
            const __mmask16 no_value = _mm512_mask_cmpeq_epi32_mask(placed, _mm512_and_si512(words, _mm512_set1_epi32(0x00ff0000)),
                                                                    _mm512_set1_epi32((int32_t)((uint32_t)BUSTER_TYPE_KIND_VOID << 16)));
            own_bytes = _mm_or_si128(_mm512_cvtepi32_epi8(own), _mm_maskz_set1_epi8(no_value, BUSTER_NODE_TESTS_TYPE_VOID));
            // And what a call's record says of its arguments (the kernel's node_facts_call_fast): its exponent, 0 or 1.
            // Another record's exponent has that bit for another reason, and the loop reads it of calls alone.
            const __mmask16 quiet_call = _mm512_test_epi32_mask(standings, _mm512_set1_epi32((int32_t)((uint32_t)BUSTER_NODE_FACTS_QUIET_CALL << 16)));
            own_bytes = _mm_or_si128(own_bytes, _mm_maskz_set1_epi8(quiet_call, BUSTER_NODE_TESTS_TYPE_QUIET));
            // A name's leaf's declaration: its line's byte, made at the line's first leaf. The two kinds are leaves'
            // alone: a name with its line, and a leaf without one (a name whose value is past the columns, or one made
            // once the lines were full; and a literal the columns don't hold, which names nothing).
            const __m128i kinds = _mm_maskz_loadu_epi8(lanes, front->window_kinds + x);
            const __mmask16 named = _mm_mask_cmpeq_epi8_mask(placed, kinds, _mm_set1_epi8((char)BUSTER_NODE_FACTS_IDENTIFIER));
            const __mmask16 stray = _mm_mask_cmpeq_epi8_mask(placed, kinds, _mm_set1_epi8((char)BUSTER_NODE_FACTS_LITERAL));
            const __m512i numbers = _mm512_maskz_loadu_epi32(named, front->window_entries + x);
            __m512i line = _mm512_mask_i32gather_epi32(zero, named, numbers, tests->line_bytes, 1);
            const __mmask16 unknown = _mm512_mask_testn_epi32_mask(named, line, _mm512_set1_epi32(BUSTER_NODE_TESTS_LINE_KNOWN));
            if (__builtin_expect(unknown != 0, 0))
            {
                for (uint32_t pending = unknown; pending != 0; pending &= pending - 1)
                {
                    const uint32_t r = x + (uint32_t)__builtin_ctz(pending);
                    const uint32_t number = front->window_entries[r];
                    const NodeFactsRecord* record = (const NodeFactsRecord*)(const void*)(records + 64 * (uint64_t)r);
                    const bool calls = (node_tests_window_row(&state->facts, record->type)->function & BUSTER_NODE_FACTS_FUNCTION) != 0;
                    const bool symbol = contexts && record->object != 0 && record->object < machine->object_count && machine->object_symbols[record->object] != 0;
                    lines_made += tests->line_bytes[number] == 0;
                    tests->line_bytes[number] = (uint8_t)(BUSTER_NODE_TESTS_LINE_KNOWN | (symbol ? BUSTER_NODE_TESTS_LINE_SYMBOL : 0) |
                                                          node_tests_window_line(s, front->tokens[r], calls) | (rows ? node_tests_window_returns(s, front->tokens[r]) : 0));
                }
                line = _mm512_mask_i32gather_epi32(zero, named, numbers, tests->line_bytes, 1);
            }
            uint32_t flagged = _mm512_test_epi32_mask(line, _mm512_set1_epi32(BUSTER_NODE_TESTS_LINE_FLAGGED));
            // A leaf without a line is loud as a callee whatever it names: its declaration is not asked, and a call of
            // it stays listed.
            const uint32_t loud = _mm512_test_epi32_mask(line, _mm512_set1_epi32(BUSTER_NODE_TESTS_LINE_LOUD)) | stray;
            for (uint32_t pending = stray; pending != 0; pending &= pending - 1)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(pending);
                flagged |= (node_tests_window_line(s, front->tokens[x + lane], false) & BUSTER_NODE_TESTS_LINE_FLAGGED) != 0 ? 1u << lane : 0;
            }
            // Its value's byte.
            const __m512i constant_kinds = _mm512_and_si512(_mm512_srli_epi32(standings, 2), _mm512_set1_epi32(0x07));
            const uint32_t some = _mm512_test_epi32_mask(constant_kinds, constant_kinds);
            const uint32_t address = _mm512_cmpeq_epi32_mask(constant_kinds, _mm512_set1_epi32(CHECK_CONSTANT_ADDRESS));
            const uint32_t wide = _mm512_test_epi32_mask(standings, _mm512_set1_epi32(BUSTER_NODE_MACHINE_WIDE));
            const uint32_t null = _mm512_testn_epi32_mask(objects, objects);
            const __mmask16 constant = (__mmask16)(some & (~address | wide | null));
            if (rows)
            {
                // A record's value, for the rows' values (NodeFactsReader.window_x_values): its object's number where
                // it has one and its constant is an address's (a name's read, an address constant's: what their
                // rows' first entry holds; the test is X's own, node_machine_address's), and else its payload (a
                // constant's bits, an address constant's offset where it has no object). The reader makes every
                // object's leaf with an address's constant (c_check_input.c; a `constexpr` object's has its value's,
                // and the window takes no read of one). A constant that is no address's may have an object all the
                // same: the kernel's rules give one to a name's line and keep it through what they make of an
                // address, and `(_Bool)&x` is an integer's constant, 1, of x's record. (None of the three corpora's
                // records has both: the measurement record's count. A one-statement program found it.) A record
                // the kernel didn't place has whatever its line has: its expression is not the window's.
                const uint32_t designates = ~null & address;
                value_low = _mm512_mask_mov_epi64(_mm512_inserti64x4(_mm512_castsi256_si512(payloads[0]), payloads[1], 1), (__mmask8)designates,
                                                  _mm512_cvtepu32_epi64(_mm512_castsi512_si256(objects)));
                value_high = _mm512_mask_mov_epi64(_mm512_inserti64x4(_mm512_castsi256_si512(payloads[2]), payloads[3], 1), (__mmask8)(designates >> 8),
                                                   _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(objects, 1)));
            }
            const __mmask16 bit_field = _mm512_test_epi32_mask(flags, _mm512_set1_epi32((int32_t)BUSTER_CHECK_BIT_FIELD));
            // A register object's flags are its leaf's, and a member's or an element's of it above that leaf in the same
            // expression: over the expression's records they say what its leaves' say.
            const uint32_t registered = _mm512_test_epi32_mask(flags, _mm512_set1_epi32((int32_t)(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)));
            const __mmask16 declaration = (__mmask16)(registered | flagged);
            const __m128i constant_byte = _mm_maskz_set1_epi8(constant, BUSTER_NODE_TESTS_VALUE_CONSTANT);
            const __m128i bit_field_byte = _mm_maskz_set1_epi8(bit_field, BUSTER_NODE_TESTS_VALUE_BIT_FIELD);
            const __m128i declaration_byte = _mm_maskz_set1_epi8(declaration, BUSTER_NODE_TESTS_VALUE_DECLARATION);
            const __m128i loud_byte = _mm_maskz_set1_epi8((__mmask16)loud, BUSTER_NODE_TESTS_VALUE_LOUD);
            value_bytes = _mm_or_si128(_mm_or_si128(constant_byte, bit_field_byte), _mm_or_si128(declaration_byte, loud_byte));
            // What it is to a constant's tests, as an operand: its kind. Its type by its word: a plain integer's has an
            // integer's kind, no fact (bool, _BitInt, atomic) and a class of 8 to 64 bits, one range of the word's low
            // three bytes, the unsigned classes the odd ones from its first. An integer constant's value is what its
            // record says (node_facts_small): the payload and one, in its exponent, which the standing's gather has. A
            // constant of such a type has its value whole in its payload. One that says none (T's own record) reads as
            // past every value, and its operator stays listed where the rule asks for a small one. (An integer constant
            // whose payload would be another storage's is no record of the columns': node_machine_whole, #3014.)
            const __m512i plain_words = _mm512_sub_epi32(_mm512_and_si512(words, _mm512_set1_epi32(0x00ffffff)),
                                                         _mm512_set1_epi32((int32_t)((uint32_t)BUSTER_TYPE_KIND_INTEGER << 16 | BUSTER_TYPED_CLASS_I8)));
            const __mmask16 integer = (__mmask16)(placed & ~bit_field) & _mm512_cmple_epu32_mask(plain_words, _mm512_set1_epi32(BUSTER_TYPED_CLASS_U64 - BUSTER_TYPED_CLASS_I8));
            const __mmask16 valued = _mm512_mask_cmpeq_epi32_mask(integer, constant_kinds, _mm512_set1_epi32(CHECK_CONSTANT_INTEGER));
            const __mmask16 unsigned_type = _mm512_mask_test_epi32_mask((__mmask16)(integer & ~valued), plain_words, _mm512_set1_epi32(1));
            const __m512i said = _mm512_srli_epi32(standings, 16);
            const __m512i value = _mm512_sub_epi32(said, _mm512_set1_epi32(1));
            const __mmask16 past_zero = _mm512_mask_cmpge_epu32_mask(valued, value, _mm512_set1_epi32(1));
            const __mmask16 past_tiny = _mm512_mask_cmpge_epu32_mask(valued, value, _mm512_set1_epi32(32));
            const __mmask16 past_small = _mm512_mask_cmpge_epu32_mask(valued, value, _mm512_set1_epi32(127));
            const __mmask16 pointer = _mm512_mask_cmpeq_epi32_mask(placed, six, _mm512_set1_epi32(0x08));
            // Each kind's bits over the one before, a constant's kinds one inside the other; into the value's byte.
            __m128i kind_bytes = _mm_maskz_set1_epi8(pointer, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_POINTER));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, integer, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_SIGNED));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, unsigned_type, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_UNSIGNED));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, valued, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_ZERO));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, past_zero, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_TINY));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, past_tiny, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_SMALL));
            kind_bytes = _mm_mask_set1_epi8(kind_bytes, past_small, (char)BUSTER_NODE_TESTS_KIND_BITS(BUSTER_NODE_TESTS_KIND_CONSTANT));
            value_bytes = _mm_or_si128(value_bytes, kind_bytes);
            if (contexts)
            {
                // A type's class, facts and kind, its word's low three bytes; a standing's byte; and a byte of the
                // flags C's rules read and of what the record's object is. Whether a name's leaf's object has a symbol
                // of its own is its declaration's line's byte's, which the gather above has: a local has none.
                const __mmask16 objected = _mm512_test_epi32_mask(objects, objects);
                const __mmask16 symbolled = _mm512_mask_test_epi32_mask(named, line, _mm512_set1_epi32(BUSTER_NODE_TESTS_LINE_SYMBOL));
                const __mmask16 effects = _mm512_test_epi32_mask(flags, _mm512_set1_epi32((int32_t)(BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)));
                const __mmask16 element = _mm512_test_epi32_mask(flags, _mm512_set1_epi32((int32_t)BUSTER_CHECK_VECTOR_ELEMENT));
                const __mmask16 register_object = _mm512_test_epi32_mask(flags, _mm512_set1_epi32((int32_t)BUSTER_CHECK_REGISTER));
                class_bytes = _mm512_cvtepi32_epi8(words);
                fact_bytes = _mm512_cvtepi32_epi8(_mm512_srli_epi32(words, 8));
                type_kind_bytes = _mm512_cvtepi32_epi8(_mm512_srli_epi32(words, 16));
                standing_bytes = _mm512_cvtepi32_epi8(standings);
                // A leaf with no line of its own (a value the columns don't hold whole; a name's without its
                // declaration's line has no symbol's bit to read) reads as a value the columns don't hold whole, which
                // lists its expression.
                standing_bytes = _mm_or_si128(standing_bytes, _mm_maskz_set1_epi8(stray, (char)BUSTER_NODE_MACHINE_WIDE));
                flag_bytes = _mm_or_si128(_mm_or_si128(_mm_maskz_set1_epi8(effects, BUSTER_NODE_CONTEXTS_EFFECTS), _mm_maskz_set1_epi8(element, BUSTER_NODE_CONTEXTS_VECTOR)),
                                          _mm_or_si128(_mm_maskz_set1_epi8(objected, BUSTER_NODE_CONTEXTS_OBJECT), _mm_maskz_set1_epi8(symbolled, BUSTER_NODE_CONTEXTS_SYMBOL)));
                flag_bytes = _mm_or_si128(flag_bytes, _mm_or_si128(_mm_maskz_set1_epi8(placed, BUSTER_NODE_CONTEXTS_PLACED),
                                                                   _mm_maskz_set1_epi8(register_object, (char)BUSTER_NODE_CONTEXTS_REGISTER)));
                // What a call's record says of its arguments' conversions (the kernel's call rule's two bits of its
                // exponent, which the standing's gather has).
                const __mmask16 converted_call = _mm512_test_epi32_mask(standings, _mm512_set1_epi32((int32_t)((uint32_t)BUSTER_NODE_FACTS_CONVERTED_CALL << 16)));
                const __mmask16 plain_call = _mm512_test_epi32_mask(standings, _mm512_set1_epi32((int32_t)((uint32_t)BUSTER_NODE_FACTS_PLAIN_CALL << 16)));
                flag_bytes = _mm_or_si128(flag_bytes, _mm_or_si128(_mm_maskz_set1_epi8(converted_call, BUSTER_NODE_CONTEXTS_CONVERTED),
                                                                   _mm_maskz_set1_epi8(plain_call, BUSTER_NODE_CONTEXTS_PLAIN)));
                if (summaries)
                {
                    // A name's leaf's line's byte, as the gather has it (another record's is zero), for a summary's
                    // side facts: the tests' loop asks it of a call's callee, and so does the sweep's own rule.
                    line_bytes = _mm512_cvtepi32_epi8(line);
                }
            }
            if (tests->verify_quiet)
            {
                // The reader's verify: a value a record says is its payload's, which the record holds whole.
                const __mmask16 told = _mm512_mask_test_epi32_mask(valued, said, said);
                const __mmask16 whole = _mm512_mask_testn_epi32_mask(told, standings, _mm512_set1_epi32(BUSTER_NODE_MACHINE_WIDE | BUSTER_NODE_MACHINE_FIELD));
                const __m512i low = _mm512_mask_i32gather_epi32(zero, told, lines, base + offsetof(NodeFactsRecord, payload), 1);
                const __m512i high = _mm512_mask_i32gather_epi32(zero, told, lines, base + offsetof(NodeFactsRecord, payload) + 4, 1);
                const __mmask16 near = _mm512_mask_cmplt_epu32_mask(told, said, _mm512_set1_epi32(0xffff));
                const __mmask16 same = _mm512_mask_cmpeq_epi32_mask(near, low, value) & _mm512_testn_epi32_mask(high, high);
                const __mmask16 past = _mm512_mask_cmpge_epu32_mask((__mmask16)(told & ~near), low, _mm512_set1_epi32(0xfffe)) |
                                       _mm512_mask_test_epi32_mask((__mmask16)(told & ~near), high, high);
                values_checked += (uint64_t)_mm_popcnt_u32(told);
                values_wrong += (uint64_t)_mm_popcnt_u32((uint32_t)(told & ~(whole & (same | past))));
                // And a record the kernel didn't place has no byte, nor has a lane past the chunk's last record: a step
                // reads such a line with the others, and the columns' masks leave it zeros.
                const __m128i unplaced_bytes = _mm_or_si128(_mm_or_si128(_mm_or_si128(own_bytes, value_bytes), _mm_or_si128(class_bytes, fact_bytes)),
                                                            _mm_or_si128(_mm_or_si128(type_kind_bytes, standing_bytes), flag_bytes));
                values_wrong += (uint64_t)_mm_popcnt_u32(_mm_mask_test_epi8_mask((__mmask16)~placed, unplaced_bytes, unplaced_bytes));
                if (rows)
                {
                    values_wrong += (uint64_t)_mm_popcnt_u32(_mm512_mask_test_epi32_mask((__mmask16)~placed, type_dwords, type_dwords));
                }
            }
        }
        _mm_storeu_si128((__m128i*)(void*)(tests->window_types + x), own_bytes);
        _mm_storeu_si128((__m128i*)(void*)(tests->window_values + x), value_bytes);
        if (contexts)
        {
            _mm_storeu_si128((__m128i*)(void*)(state->window_c_classes + x), class_bytes);
            _mm_storeu_si128((__m128i*)(void*)(state->window_c_facts + x), fact_bytes);
            _mm_storeu_si128((__m128i*)(void*)(state->window_c_kinds + x), type_kind_bytes);
            _mm_storeu_si128((__m128i*)(void*)(state->window_c_standings + x), standing_bytes);
            _mm_storeu_si128((__m128i*)(void*)(state->window_c_flags + x), flag_bytes);
        }
        if (rows)
        {
            _mm512_storeu_si512(state->window_x_types + x, type_dwords);
            _mm512_storeu_si512(state->window_x_values + x, value_low);
            _mm512_storeu_si512(state->window_x_values + x + 8, value_high);
        }
        if (summaries)
        {
            _mm_storeu_si128((__m128i*)(void*)(state->window_x_lines + x), line_bytes);
        }
    }
    *made += lines_made;
    tests->types->own_values_checked += values_checked;
    tests->types->own_values_wrong += values_wrong;
}

// The sweep in its four forms, each its own code: the tests' bytes alone, as it was before C ran on the window; with
// C's bytes where C runs; with the records' types too where X's rows are made; and with the lines' bytes' column where
// a summary's side facts are. One sweep with a test a step of whether C runs read the tests' bytes 0.2 to 2 ticks a
// record over the first on the 9700X with C off (`sqlite3.c` to musl).
static void node_tests_window_bytes(NodeCensusReader* tests, const struct NodeFactsReader* state, const NodeFront* front, uint64_t* made)
{
    if (state->x_tables != NULL && state->summaries)
    {
        node_tests_window_bytes_by(tests, state, front, made, true, true, true);
    }
    else if (state->x_tables != NULL)
    {
        node_tests_window_bytes_by(tests, state, front, made, true, true, false);
    }
    else if (state->c_tables != NULL)
    {
        node_tests_window_bytes_by(tests, state, front, made, true, false, false);
    }
    else
    {
        node_tests_window_bytes_by(tests, state, front, made, false, false, false);
    }
}

// A row of a GFNI byte transform. Bit 'to' in the result is copied from 'from' in
// the input byte. GFNI stores the row for result bit 0 in the most significant byte.
#define BUSTER_NODE_TESTS_COPY_BIT(to, from) ((uint64_t)(from) << (8u * (7u - (uint32_t)__builtin_ctz((uint32_t)(to)))))

// The tests' loop over a chunk's `count` records, 64 a step: what each record is as an operand (its value's and its
// type's bits and its operator's), its operands' such bytes together, its part of the set's tests, and a block's masks:
// the operators listed for their operands' tree's tests, the operators with tests of their own, the writes, the records
// that as a statement's top leave its test nothing, and the operators left out of the second: the quiet rule's, which
// `quiet_count` counts, the calls with no test to make, which `call_count` counts, and the casts with none, which
// `cast_count` counts. And the chunk's casts over a call left out, their records' numbers compressed into the asks'
// list: how many into `ask_count`. With `summaries` (X's rows with a summary's side facts), a block's mask of the calls
// those facts ask a record at a time (NodeFactsReader.window_x_suspects), made here where each record's first
// operand's place is at hand.
static inline __attribute__((always_inline)) void node_tests_window_loop_by(NodeCensusReader* tests, const struct NodeFactsReader* state, const NodeFront* front, uint32_t count,
                                                                          uint64_t* far_count, uint64_t* quiet_count, uint64_t* call_count, uint64_t* cast_count,
                                                                          uint64_t* order_count, uint32_t* ask_count, const bool summaries)
{
    const NodeCensusScratch* s = &tests->scratch;
    const __m512i quiet_pairs = _mm512_loadu_si512(s->own_quiet_pairs);
    const __mmask64 calls_rule = _cvtu64_mask64(s->own_calls_quiet ? UINT64_MAX : 0);
    // A line's byte's bit of a plain function. No bit, where no call of the unit's may be plain at all: one of its
    // functions is unknown from a declaration on (check_program_return_unknowns' table), which a call's rule asks by
    // its use.
    const __m512i plain_line = _mm512_set1_epi8(summaries && tests->types->return_unknowns == NULL ? (char)BUSTER_NODE_TESTS_LINE_PLAIN : 0);
    const uint8_t* const line_column = summaries ? state->window_x_lines : NULL;
    uint64_t* const suspect_masks = summaries ? state->window_x_suspects : NULL;
    __m512i previous = _mm512_setzero_si512();
    __m512i previous_values = _mm512_setzero_si512();
    __m512i previous_marks = _mm512_setzero_si512();
    __m512i previous_left_out = _mm512_setzero_si512();
    __m512i previous_plain = _mm512_setzero_si512();
    // A half block's records' places.
    const __m512i places = _mm512_set_epi16(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t ask_total = 0;
    uint64_t far_total = 0;
    const __m512i converts = _mm512_set1_epi8((char)BUSTER_NODE_TESTS_OPERAND_CONVERTS);
    // A plain pointer that is a constant a test may read: its kind and its constant's bit, among a value's byte's
    // kind's three bits and that bit.
    const __m512i pointer_bits = _mm512_set1_epi8((char)((BUSTER_NODE_TESTS_KINDS - 1) << BUSTER_NODE_TESTS_KIND_SHIFT | BUSTER_NODE_TESTS_VALUE_CONSTANT));
    const __m512i constant_pointer = _mm512_set1_epi8((char)(BUSTER_NODE_TESTS_KIND_POINTER << BUSTER_NODE_TESTS_KIND_SHIFT | BUSTER_NODE_TESTS_VALUE_CONSTANT));
    for (uint32_t x = 0; x < count; x += 64)
    {
        const __m512i codes = _mm512_and_si512(_mm512_loadu_si512(front->ops + x), _mm512_set1_epi8(0x7f));
        const __m512i quiets = node_window_table(s->own_quiets, codes);
        const __m512i classes = node_window_table(s->own_classes, codes);
        const __m512i operand_bits = node_window_table(s->own_operand_bits, codes);
        const __mmask64 always = _mm512_movepi8_mask(classes);
        const __m512i sends = node_window_table(s->own_sends, codes);
        const __m512i tree_marks = node_window_table(s->own_tree_marks, codes);
        const __m512i tree_firsts = node_window_table(s->own_tree_firsts, codes);
        const __m512i tree_sends = node_window_table(s->own_tree_sends, codes);
        const __m512i own = _mm512_loadu_si512(tests->window_types + x);
        const __m512i values = _mm512_loadu_si512(tests->window_values + x);
        // What it is as an operand: a constant, its type's four bits (an enumeration, a pointer, a pointer to void, an
        // array), its operator's three.
        const __m512i constant = _mm512_and_si512(values, _mm512_set1_epi8(BUSTER_NODE_TESTS_VALUE_CONSTANT));
        // An array, pointer, function and nullptr are mutually exclusive type kinds, so the
        // parity of the pointer and array bits is their union for the conversion property.
        const __m512i type_properties = _mm512_gf2p8affine_epi64_epi8(own, _mm512_set1_epi64(
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_OPERAND_ENUM, BUSTER_NODE_CENSUS_TYPE_ENUM) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_OPERAND_CONVERTS, BUSTER_NODE_CENSUS_TYPE_CONVERTS) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_OPERAND_VOID_POINTEE, BUSTER_NODE_CENSUS_TYPE_VOID_POINTEE) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_OPERAND_ARRAY, BUSTER_NODE_CENSUS_TYPE_ARRAY)), 0);
        const __m512i property = _mm512_or_si512(_mm512_or_si512(constant, type_properties), operand_bits);
        _mm512_storeu_si512(tests->window_properties + x, property);
        __mmask64 near_left = 0;
        __mmask64 far_left = 0;
        __mmask64 near_right = 0;
        __mmask64 far_right = 0;
        const __m512i at_left = node_window_reach(front->window_left + x, &near_left, &far_left);
        const __m512i at_right = node_window_reach(front->window_right + x, &near_right, &far_right);
        const __m512i left = node_window_operands(at_left, near_left, previous, property);
        const __m512i right = node_window_operands(at_right, near_right, previous, property);
        __m512i operands = _mm512_or_si512(left, right);
        _mm512_storeu_si512(tests->window_operands + x, operands);
        // The tree's rule: an operator is listed for the tree's tests of its operands where its first operand's operator
        // is one it asks for (a comparison's `!`, a condition's `=`), or an operand's property is (a comparison, of a
        // bitwise operator or of a cast).
        const __m512i mark_left = node_window_operands(at_left, near_left, previous_marks, tree_marks);
        __mmask64 tree_first = _mm512_test_epi8_mask(tree_firsts, mark_left);
        if (summaries)
        {
            // The calls a summary's side facts ask a record at a time (node_rows_window_summary): one whose callee is
            // not, in this block or the block before, a plain function's name's leaf, by its line's byte's bit
            // (another record's byte is zero). Any other call's own return fact is none.
            const __m512i plain_bytes = _mm512_and_si512(_mm512_loadu_si512(line_column + x), plain_line);
            const __m512i callee_plain = node_window_operands(at_left, near_left, previous_plain, plain_bytes);
            suspect_masks[x / 64] = _cvtmask64_u64(_mm512_mask_testn_epi8_mask(_mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8((char)CHECK_OP_CALL)), callee_plain, callee_plain));
            previous_plain = plain_bytes;
        }
        if (__builtin_expect((far_left | far_right) != 0, 0))
        {
            // An operand before the block before: its byte by a load, a record at a time, and its operator's mark.
            for (uint64_t pending = far_left; pending != 0; pending &= pending - 1)
            {
                const uint32_t r = x + (uint32_t)_tzcnt_u64(pending);
                const uint32_t operand = r - front->window_left[r];
                tests->window_operands[r] |= tests->window_properties[operand];
                tree_first |= (uint64_t)((s->own_tree_firsts[front->ops[r] & 0x7f] & s->own_tree_marks[front->ops[operand] & 0x7f]) != 0 ? 1 : 0) << (r & 63);
                // A call this far from the record it is the first operand of: if that is its statement's discard, no ask
                // reaches the call's callee, so the call is no top that leaves a statement's test nothing. Its block's
                // masks are made already.
                const uint64_t far_call = r < count && (front->ops[operand] & 0x7f) == CHECK_OP_CALL ? 1 : 0;
                tests->window_masks[BUSTER_NODE_TESTS_MASKS * (uint64_t)(operand / 64) + BUSTER_NODE_TESTS_MASK_DISCARDS] &= ~(far_call << (operand & 63));
            }
            for (uint64_t pending = far_right; pending != 0; pending &= pending - 1)
            {
                const uint32_t r = x + (uint32_t)_tzcnt_u64(pending);
                tests->window_operands[r] |= tests->window_properties[r - front->window_right[r]];
            }
            far_total += (uint64_t)_mm_popcnt_u64(far_left) + (uint64_t)_mm_popcnt_u64(far_right);
            operands = _mm512_loadu_si512(tests->window_operands + x);
        }
        // Its part of the set's tests.
        const __m512i fixed = _mm512_and_si512(classes, _mm512_set1_epi8(BUSTER_NODE_TESTS_SET_UNCODED | BUSTER_NODE_TESTS_SET_ADDRESS));
        const __m512i copy_type = _mm512_and_si512(own, _mm512_or_si512(classes, _mm512_set1_epi8((char)~BUSTER_NODE_TESTS_CLASS_COPIES)));
        const __m512i type_set = _mm512_gf2p8affine_epi64_epi8(copy_type, _mm512_set1_epi64(
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_SET_SPECIAL, BUSTER_NODE_CENSUS_TYPE_SPECIAL) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_SET_AGGREGATE, BUSTER_NODE_CENSUS_TYPE_AGGREGATE) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_SET_ARRAY, BUSTER_NODE_CENSUS_TYPE_ARRAY)), 0);
        const __m512i value_set = _mm512_gf2p8affine_epi64_epi8(values, _mm512_set1_epi64(
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_SET_BIT_FIELD, BUSTER_NODE_TESTS_VALUE_BIT_FIELD) |
            BUSTER_NODE_TESTS_COPY_BIT(BUSTER_NODE_TESTS_SET_DECLARATION, BUSTER_NODE_TESTS_VALUE_DECLARATION)), 0);
        const __m512i set = _mm512_or_si512(_mm512_or_si512(fixed, type_set), value_set);
        _mm512_storeu_si512(tests->window_sets + x, set);
        // The quiet rule: an operator listed for a constant operand alone, whose constants' tests have nothing to say of
        // its operands' kinds. A kind is in its record's value's byte, three bits up: the pair's place in the table is the
        // first's, shifted down, and the second's where it sits (the permute reads an index's low six bits). An operand
        // before the block before reads as none, and its operator stays listed. A return's conversion's class is bit 7,
        // its own type's bit of its own byte.
        const __m512i kind_left = node_window_operands(at_left, near_left, previous_values, values);
        const __m512i kind_right = node_window_operands(at_right, near_right, previous_values, values);
        const __m512i pair = _mm512_ternarylogic_epi64(_mm512_set1_epi8(BUSTER_NODE_TESTS_KINDS - 1), _mm512_srli_epi16(kind_left, BUSTER_NODE_TESTS_KIND_SHIFT), kind_right, 0xca);
        const __m512i paired = _mm512_permutexvar_epi8(pair, quiet_pairs);
        const __m512i own_bit = _mm512_or_si512(values, _mm512_set1_epi8((char)~BUSTER_NODE_TESTS_KIND_OWN));
        const __mmask64 quiet_class = _mm512_test_epi8_mask(_mm512_and_si512(paired, own_bit), quiets);
        const __mmask64 alone = ~always & _mm512_cmpeq_epi8_mask(_mm512_and_si512(sends, operands), _mm512_set1_epi8(BUSTER_NODE_TESTS_OPERAND_CONSTANT));
        // A subscript of the class's pairs has one test more, a null constant pointer's dereference: one whose pointer
        // is a constant a test may read stays listed.
        const __mmask64 subscripts = _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8((char)CHECK_OP_SUBSCRIPT));
        const __mmask64 pointer_read = _mm512_cmpeq_epi8_mask(_mm512_and_si512(kind_left, pointer_bits), constant_pointer) |
                                       _mm512_cmpeq_epi8_mask(_mm512_and_si512(kind_right, pointer_bits), constant_pointer);
        const __mmask64 quiet = quiet_class & alone & ~(subscripts & pointer_read);
        // The calls' rule: a call has no test to make where its own type is no aggregate's, its callee is a name whose
        // declaration gives a call no semantics of its own, or no name (its first operand's value's byte), its operands'
        // properties send it nothing, and its arguments are quiet: no aggregate, no constant but an address's or an
        // integer's of zero or 1 to 126 that its parameter takes unchanged. The kernel says the last of a call as it
        // makes its record, where it reads each argument's (node_facts_call_fast): a bit of the call's type's byte. A
        // callee before the block before says nothing, and its call stays listed.
        const __mmask64 calls = _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8((char)CHECK_OP_CALL));
        const __mmask64 quiet_arguments = _mm512_test_epi8_mask(own, _mm512_set1_epi8(BUSTER_NODE_TESTS_TYPE_QUIET));
        const __mmask64 loud_callee = _mm512_test_epi8_mask(kind_left, _mm512_set1_epi8(BUSTER_NODE_TESTS_VALUE_LOUD));
        const __mmask64 aggregates = _mm512_test_epi8_mask(own, _mm512_set1_epi8(BUSTER_NODE_CENSUS_TYPE_AGGREGATE));
        const __mmask64 quiet_calls = calls_rule & calls & near_left & quiet_arguments & ~loud_callee & ~aggregates & ~_mm512_test_epi8_mask(sends, operands);
        // What as a statement's top leaves its test nothing: a write, a record of type void (a cast to void; a call, a
        // conditional or a comma of that type is listed for itself, or is a call left out), and a call left out, until
        // the asks take out the one whose callee asks for its result. A statement's discard is a cast to void over its
        // top: one whose first operand is a call left out goes to the asks, its record's number compressed into the
        // chunk's list, a half block a step. (One whose operand is before the block before is the far path's, above.)
        const __mmask64 writes = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_NODE_TESTS_CLASS_WRITE));
        const __mmask64 voids = _mm512_test_epi8_mask(own, _mm512_set1_epi8(BUSTER_NODE_TESTS_TYPE_VOID));
        const __mmask64 casts = _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8((char)CHECK_OP_CAST));
        // The casts' rule: a cast is sent to its test by an operand that is a pointer once converted alone (the
        // table's), and that test asks whether its own type is an integer narrower than a pointer or an enumeration:
        // none where its own type is void (a statement's discard of a pointer's top) or a pointer. A cast to an
        // integer stays listed, whatever its width: the byte says none.
        const __mmask64 quiet_casts = casts & _mm512_test_epi8_mask(sends, operands) &
                                      _mm512_test_epi8_mask(own, _mm512_set1_epi8((char)(BUSTER_NODE_TESTS_TYPE_VOID | BUSTER_NODE_CENSUS_TYPE_POINTER)));
        // The orders' rule (#3201): an ordered comparison is sent to its test by an operand that is a pointer once
        // converted (the table's, under the warning), and that test asks for exactly one such operand: none where both
        // are, a loop's `p < end`. Left out where nothing else sends it (a constant, an enumeration, a comparison under
        // it) and its operator has no test whatever its operands are (the sign comparison's). An operand before the
        // block before reads as none, and its comparison stays listed.
        const __m512i sent_by = _mm512_and_si512(sends, operands);
        const __m512i both = _mm512_ternarylogic_epi64(left, right, classes, 0x80);
        const __mmask64 quiet_orders = _mm512_test_epi8_mask(both, converts) & _mm512_cmpeq_epi8_mask(sent_by, converts);
        const __m512i left_out = _mm512_movm_epi8(quiet_calls);
        const __m512i top_left_out = node_window_operands(at_left, near_left, previous_left_out, left_out);
        const uint64_t asks = _cvtmask64_u64(casts & voids & _mm512_test_epi8_mask(top_left_out, top_left_out));
        const __m512i numbers = _mm512_add_epi16(places, _mm512_set1_epi16((short)x));
        _mm512_storeu_si512(tests->window_asks + ask_total, _mm512_maskz_compress_epi16((__mmask32)asks, numbers));
        ask_total += (uint32_t)_mm_popcnt_u32((uint32_t)asks);
        _mm512_storeu_si512(tests->window_asks + ask_total, _mm512_maskz_compress_epi16((__mmask32)(asks >> 32), _mm512_add_epi16(numbers, _mm512_set1_epi16(32))));
        ask_total += (uint32_t)_mm_popcnt_u32((uint32_t)(asks >> 32));
        // The block's masks.
        const __mmask64 sent = always | _mm512_test_epi8_mask(sends, operands);
        uint64_t* masks = tests->window_masks + BUSTER_NODE_TESTS_MASKS * (uint64_t)(x / 64);
        masks[BUSTER_NODE_TESTS_MASK_TREE] = tree_first | _mm512_test_epi8_mask(tree_sends, operands);
        masks[BUSTER_NODE_TESTS_MASK_QUIET] = quiet | quiet_calls | quiet_casts | quiet_orders;
        masks[BUSTER_NODE_TESTS_MASK_OPERATORS] = sent & ~quiet & ~quiet_calls & ~quiet_casts & ~quiet_orders;
        masks[BUSTER_NODE_TESTS_MASK_WRITES] = writes;
        masks[BUSTER_NODE_TESTS_MASK_DISCARDS] = writes | voids | quiet_calls;
        masks[BUSTER_NODE_TESTS_MASK_QUIET_CALLS] = quiet_calls;
        masks[BUSTER_NODE_TESTS_MASK_QUIET_CASTS] = quiet_casts;
        masks[BUSTER_NODE_TESTS_MASK_QUIET_ORDERS] = quiet_orders;
        previous = property;
        previous_values = values;
        previous_marks = tree_marks;
        previous_left_out = left_out;
    }
    *far_count += far_total;
    // Each block occupies one cache line. Count its masks together after the sweep to keep the
    // four counters out of the block's live set. The four quiet masks cover disjoint operators.
    __m512i totals = _mm512_setzero_si512();
    for (uint32_t x = 0; x < count; x += 64)
    {
        const uint64_t* masks = tests->window_masks + BUSTER_NODE_TESTS_MASKS * (uint64_t)(x / 64);
        totals = _mm512_add_epi64(totals, _mm512_popcnt_epi64(_mm512_loadu_si512(masks)));
    }
    uint64_t counts[BUSTER_NODE_TESTS_MASKS];
    _mm512_storeu_si512(counts, totals);
    *call_count += counts[BUSTER_NODE_TESTS_MASK_QUIET_CALLS];
    *cast_count += counts[BUSTER_NODE_TESTS_MASK_QUIET_CASTS];
    *order_count += counts[BUSTER_NODE_TESTS_MASK_QUIET_ORDERS];
    *quiet_count += counts[BUSTER_NODE_TESTS_MASK_QUIET] - counts[BUSTER_NODE_TESTS_MASK_QUIET_CALLS] - counts[BUSTER_NODE_TESTS_MASK_QUIET_CASTS] - counts[BUSTER_NODE_TESTS_MASK_QUIET_ORDERS];
    *ask_count = ask_total;
}

// The loop in its two forms, each its own code: as it is without X's rows, and with the calls' mask for a summary's
// side facts.
static void node_tests_window_loop(NodeCensusReader* tests, const struct NodeFactsReader* state, const NodeFront* front, uint32_t count, uint64_t* far_count,
                                   uint64_t* quiet_count, uint64_t* call_count, uint64_t* cast_count, uint64_t* order_count, uint32_t* ask_count)
{
    if (state->x_tables != NULL && state->summaries)
    {
        node_tests_window_loop_by(tests, state, front, count, far_count, quiet_count, call_count, cast_count, order_count, ask_count, true);
    }
    else
    {
        node_tests_window_loop_by(tests, state, front, count, far_count, quiet_count, call_count, cast_count, order_count, ask_count, false);
    }
}

// The asks of a chunk: each of the `ask_count` casts in the loop's list, a record at a time. Its first operand is a call
// the loop left out, which a statement may discard: whether the call's callee asks for its result is its declaration's
// fact (node_census_wants_result's). A name's leaf's line's byte has it, made at the line's first ask; a callee that is
// no name's leaf with a line asks for none (a leaf without a line has no call left out). A call whose callee does is
// no top that leaves a statement's test nothing: its bit of its block's mask is cleared. Nothing here branches on what
// a record is but the fact's making and a callee that asks: a callee that is no name's leaf has its entry loaded all
// the same, and reads the first line's byte for nothing. Returns the declarations asked.
static uint64_t node_tests_window_asks(NodeCensusReader* tests, const NodeFront* front, uint32_t ask_count)
{
    uint64_t made = 0;
    for (uint32_t i = 0; i < ask_count; i += 1)
    {
        const uint32_t cast = tests->window_asks[i];
        const uint32_t call = cast - front->window_left[cast];
        const uint32_t callee = call - front->window_left[call];
        const uint32_t named = front->window_kinds[callee] == BUSTER_NODE_FACTS_IDENTIFIER;
        const uint8_t line = tests->line_bytes[front->window_entries[callee] & (0u - named)];
        uint32_t result = named != 0 ? line : BUSTER_NODE_TESTS_LINE_RESULT_KNOWN;
        if (__builtin_expect((result & BUSTER_NODE_TESTS_LINE_RESULT_KNOWN) == 0, 0))
        {
            result = node_tests_window_wants(tests, front, callee);
            made += 1;
        }
        // Few callees ask for their result (one call in the three corpora): the mask is written for those alone.
        if (__builtin_expect((result & BUSTER_NODE_TESTS_LINE_RESULT) != 0, 0))
        {
            tests->window_masks[BUSTER_NODE_TESTS_MASKS * (uint64_t)(call / 64) + BUSTER_NODE_TESTS_MASK_DISCARDS] &= ~(UINT64_C(1) << (call & 63));
        }
    }
    return made;
}

// The set's bytes of the window's records `first` to `first + size - 1` together: one OR of a byte column over a range.
static inline uint8_t node_tests_window_set(const uint8_t* sets, uint32_t first, uint32_t size)
{
    __m512i all = _mm512_setzero_si512();
    for (uint32_t done = 0; done < size; done += 64)
    {
        const uint32_t rest = size - done;
        const __mmask64 lanes = _cvtu64_mask64(rest >= 64 ? UINT64_MAX : (UINT64_C(1) << rest) - 1);
        all = _mm512_or_si512(all, _mm512_maskz_loadu_epi8(lanes, sets + first + done));
    }
    const uint64_t folded = (uint64_t)_mm512_reduce_or_epi64(all);
    const uint64_t halves = folded | folded >> 32;
    const uint64_t quarters = halves | halves >> 16;
    return (uint8_t)(quarters | quarters >> 8);
}

// Word `word` of the blocks' mask `kind` as the expression of the window's records `first` to `first + size - 1` has it:
// bit r is its record r's, so the word is the end of one block's word and the start of the next's, and no bit past the
// expression's last record.
static inline __attribute__((always_inline)) uint64_t node_tests_window_word(const uint64_t* masks, uint32_t kind, uint32_t first, uint32_t size, uint32_t word)
{
    const uint32_t lane = first & 63;
    const uint64_t* block = masks + BUSTER_NODE_TESTS_MASKS * ((uint64_t)(first / 64) + word) + kind;
    const uint32_t from = word * 64;
    const uint32_t live = size > from ? (size - from < 64 ? size - from : 64) : 0;
    const uint64_t range = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
    return (block[0] >> lane | (block[BUSTER_NODE_TESTS_MASKS] << 1) << (63 - lane)) & range;
}

// The lowest and the highest of the tokens of the window's records `first` to `first + size - 1` that are the unit's
// (it has `token_count`), sixteen records a step: UINT32_MAX and 0 where none is.
static inline void node_tests_window_span(const uint32_t* tokens, uint32_t token_count, uint32_t first, uint32_t size, uint32_t* lowest, uint32_t* highest)
{
    const __m512i limit = _mm512_set1_epi32((int32_t)token_count);
    __m512i low = _mm512_set1_epi32(-1);
    __m512i high = _mm512_setzero_si512();
    for (uint32_t done = 0; done < size; done += 16)
    {
        const uint32_t rest = size - done;
        const __mmask16 lanes = (__mmask16)(rest >= 16 ? 0xffffu : (1u << rest) - 1);
        const __m512i block = _mm512_maskz_loadu_epi32(lanes, tokens + first + done);
        const __mmask16 placed = _mm512_mask_cmplt_epu32_mask(lanes, block, limit);
        low = _mm512_mask_min_epu32(low, placed, low, block);
        high = _mm512_mask_max_epu32(high, placed, high, block);
    }
    *lowest = _mm512_reduce_min_epu32(low);
    *highest = _mm512_reduce_max_epu32(high);
}

// T takes no expression of more records than the census's tests do: the size's test has nothing to say of one T took.
_Static_assert(BUSTER_NODE_MACHINE_MOST <= BUSTER_NODE_CENSUS_MOST, "an expression T takes is no larger than the census's tests take");

// The chunk's tests: the kernel has just made `front`'s chunk's records in place (`state`'s facts' records, by the
// front's record), and each of the batch's expressions that it made there (NodeFactsReader.placed) and that the tests
// ran on at reader time has its tests made again from the window, in four sweeps:
// - the records' bytes, for the whole chunk (node_tests_window_bytes);
// - the loop over them (node_tests_window_loop);
// - each expression's own part of the loop's results (NodeCensusReader.loops). Its records are the window's `first` to
//   `first + size - 1`, its operand's subtree, and its root's record where the root is one of them (not an
//   initialization's). The set's tests are an OR of its records' bytes; the lists of the operators whose operands the
//   tree's tests read and of the operators with tests of their own are the blocks' masks' bits in its range; its
//   tokens' span is a minimum and a maximum of its records'. And whether it has anything for the tests that read T's
//   records, with its verdict where it has nothing;
// - the tests that read T's records still (node_tests_expression's: the tree's, the operators', the expression's own),
//   on each expression that has something for them, after T has made its records again. T's time there is not the
//   tests': a last sweep runs T alone on the same expressions, and the tests' part is the difference.
// The reference is what the census's code gave at reader time at the expression's root: its bits
// (NodeFrontTypes.tests), its root's kind and its tokens' span.
static void node_tests_window_chunk(NodeCensusReader* tests, struct NodeFactsReader* state, const NodeFront* front)
{
    NodeCensusScratch* s = &tests->scratch;
    NodeFrontTypes* types = tests->types;
    const NodeCensusPolicy* policy = &s->policy;
    NodeMachine* machine = state->facts.machine;
    const CheckInputReader* reader = state->reader;
    const uint32_t token_count = reader->parser->tokens.count;
    uint64_t* counts = types->facts_counts;
    NodeTestsLoop* loops = tests->loops;
    const bool verify_quiet = tests->verify_quiet;
    NodeWindowCounter* const counter = node_window_counting != NULL && node_window_counting->family == NODE_WINDOW_TESTS ? node_window_counting : NULL;
    uint64_t first_sample[3];
    const uint64_t t0 = __rdtsc();
    first_sample[0] = node_window_stamp(counter);
    node_tests_window_bytes(tests, state, front, &counts[NODE_FACTS_COUNT_TESTS_LINES]);
    const uint64_t t1 = __rdtsc();
    first_sample[1] = node_window_stamp(counter);
    uint32_t ask_count = 0;
    node_tests_window_loop(tests, state, front, front->count, &counts[NODE_FACTS_COUNT_TESTS_FAR], &types->own_quiet, &types->own_quiet_calls, &types->own_quiet_casts,
                           &types->own_quiet_orders, &ask_count);
    types->own_result_asks += node_tests_window_asks(tests, front, ask_count);
    const uint64_t t2 = __rdtsc();
    first_sample[2] = node_window_stamp(counter);
    if (counter != NULL)
    {
        // The repeated bytes and masks replace the same columns. The first run has filled the declaration
        // memos already. Keep the shadow's counters and the first run's number of asks.
        const uint64_t checked = types->own_values_checked;
        const uint64_t wrong = types->own_values_wrong;
        uint64_t made = 0;
        uint64_t far = 0;
        uint64_t quiet = 0;
        uint64_t calls = 0;
        uint64_t casts = 0;
        uint64_t ordered = 0;
        uint32_t asks = 0;
        uint64_t second_sample[3];
        second_sample[0] = node_window_stamp(counter);
        node_tests_window_bytes(tests, state, front, &made);
        (void)__rdtsc();
        second_sample[1] = node_window_stamp(counter);
        node_tests_window_loop(tests, state, front, front->count, &far, &quiet, &calls, &casts, &ordered, &asks);
        (void)node_tests_window_asks(tests, front, asks);
        (void)__rdtsc();
        second_sample[2] = node_window_stamp(counter);
        types->own_values_checked = checked;
        types->own_values_wrong = wrong;
        node_window_collect(counter, 0, first_sample, 2, front->count);
        node_window_collect(counter, 1, second_sample, 2, front->count);
    }
    types->own_result_casts += ask_count;
    // What every expression with nothing listed has, by the unit's warnings; and the unit's switches as bits.
    const uint64_t fixed = policy->unmodeled ? UINT64_C(1) << NODE_CENSUS_UNMODELED : 0;
    const uint32_t unsequenced = policy->unsequenced;
    const uint32_t return_address = policy->return_address;
    // A root's token is inside a statement expression only in a unit that has one.
    const bool nests = reader->output->region_count != 0;
    const bool cleanups = reader->output->cleanup_count != 0;
    uint32_t word_count = 0;
    uint32_t called = 0;
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        NodeTestsLoop* loop = &loops[e];
        const uint32_t root = state->roots[e];
        loop->run = state->placed[e] != 0 && root < types->node_capacity && (types->refusals[root] & BUSTER_NODE_FRONT_TYPED_TESTED) != 0;
        if (loop->run != 0)
        {
            const uint32_t record = state->records[e];
            const uint32_t operand = state->operands[e];
            const uint32_t size = front->sizes[operand];
            const uint32_t first = operand + 1 - size;
            const bool rooted = state->counts[e] > size;
            // The set, from the bits of all its records.
            const uint8_t set = (uint8_t)(node_tests_window_set(tests->window_sets, first, size) | (rooted ? tests->window_sets[record] : 0));
            uint64_t bits = (set & BUSTER_NODE_TESTS_SET_UNCODED) != 0 ? UINT64_C(1) << NODE_CENSUS_OPERATOR : 0;
            bits |= (set & BUSTER_NODE_TESTS_SET_SPECIAL) != 0 ? UINT64_C(1) << NODE_CENSUS_TYPE : 0;
            bits |= (set & BUSTER_NODE_TESTS_SET_BIT_FIELD) != 0 ? UINT64_C(1) << NODE_CENSUS_BIT_FIELD : 0;
            bits |= (set & BUSTER_NODE_TESTS_SET_AGGREGATE) != 0 ? UINT64_C(1) << NODE_CENSUS_AGGREGATE : 0;
            bits |= (set & BUSTER_NODE_TESTS_SET_ADDRESS) != 0 && policy->packed_member && s->packed_unit ? UINT64_C(1) << NODE_CENSUS_PACKED_MEMBER : 0;
            bits |= (set & BUSTER_NODE_TESTS_SET_DECLARATION) != 0 ? UINT64_C(1) << NODE_CENSUS_DECLARATION : 0;
            // Its lists and its writes, as bits by a record's number among its own (its root's the last), and its
            // tokens' span.
            const uint32_t words = size / 64 + 1;
            uint64_t* tree_bits = tests->tree_bits + word_count;
            uint64_t* operator_bits = tests->operator_bits + word_count;
            uint32_t writes = 0;
            uint32_t last_write = UINT32_MAX;
            uint64_t lists = 0;
            for (uint32_t word = 0; word < words; word += 1)
            {
                const uint64_t written = node_tests_window_word(tests->window_masks, BUSTER_NODE_TESTS_MASK_WRITES, first, size, word);
                tree_bits[word] = node_tests_window_word(tests->window_masks, BUSTER_NODE_TESTS_MASK_TREE, first, size, word);
                operator_bits[word] = node_tests_window_word(tests->window_masks, BUSTER_NODE_TESTS_MASK_OPERATORS, first, size, word);
                writes += (uint32_t)_mm_popcnt_u64(written);
                last_write = written != 0 ? word * 64 + 63 - (uint32_t)_lzcnt_u64(written) : last_write;
                lists |= tree_bits[word] | operator_bits[word];
            }
            uint32_t lowest = UINT32_MAX;
            uint32_t highest = 0;
            node_tests_window_span(front->tokens, token_count, first, size, &lowest, &highest);
            // Its root's record, where it is one of its records: a control's (a discard, a condition, a return's
            // conversion, a switch's promotion), no write. It is listed for the tree's tests of its operand (a
            // discarded comparison, a condition's `=`) and has tests of its own where its operand's bytes send it
            // there. An initialization's root is none of the window's: its operand's record stands here, whose token is
            // in the span already and whose bits are not taken again.
            const uint32_t root_record = rooted ? record : operand;
            const uint64_t* root_masks = tests->window_masks + BUSTER_NODE_TESTS_MASKS * (uint64_t)(root_record / 64);
            const uint32_t root_lane = root_record & 63;
            const uint32_t token = front->tokens[root_record];
            const uint32_t in_unit = token < token_count;
            const uint64_t root_tree = rooted ? root_masks[BUSTER_NODE_TESTS_MASK_TREE] >> root_lane & 1 : 0;
            const uint64_t root_operator = rooted ? root_masks[BUSTER_NODE_TESTS_MASK_OPERATORS] >> root_lane & 1 : 0;
            tree_bits[size / 64] |= root_tree << (size & 63);
            operator_bits[size / 64] |= root_operator << (size & 63);
            lists |= root_tree | root_operator;
            lowest = in_unit != 0 && token < lowest ? token : lowest;
            highest = in_unit != 0 && token > highest ? token : highest;
            loop->tests = bits;
            loop->tree_bits = tree_bits;
            loop->operator_bits = operator_bits;
            loop->words = words;
            loop->types = tests->window_types + first;
            loop->properties = tests->window_properties + first;
            loop->operands = tests->window_operands + first;
            loop->lowest = lowest;
            loop->highest = highest;
            loop->writes = writes;
            loop->last_write = last_write;
            loop->class_bits = (set & BUSTER_NODE_TESTS_SET_ADDRESS) != 0 ? BUSTER_NODE_TESTS_CLASS_ADDRESS : 0;
            loop->type_bits = (set & BUSTER_NODE_TESTS_SET_ARRAY) != 0 ? BUSTER_NODE_CENSUS_TYPE_ARRAY : 0;
            word_count += words;
            // Whether it has anything for the tests that read T's records: a listed operator; two writes, or one under
            // its top; an initialization's root, whose tests are its own; a statement whose top is no write, which may
            // be a value without an effect; a return of an address; a warning of T's actions. One with nothing has its
            // verdict here: the set's bits, and what its root's kind and its tokens give (node_tests_root's and
            // node_census_reader_expression's of such an expression; the reader's scratch has no events' prefix, and
            // its events are its tokens' bits). Both are made of every expression, with no branch on what an expression
            // is: which one is listed is no pattern a predictor finds. A branch is taken for what few units or few
            // expressions have: a statement expression's tokens, a pragma's body, a warning event among the expression's
            // tokens.
            const uint32_t held = in_unit != 0 ? token : 0;
            const uint32_t nested = nests ? in_unit & (uint32_t)(tests->nested_bits[held >> 6] >> (held & 63) & 1) : 0;
            uint32_t context = nested != 0 ? NODE_CENSUS_ROOT_OTHER : s->own_roots[front->ops[root_record] & 0x7f];
            if (__builtin_expect(cleanups, 0) && context == NODE_CENSUS_ROOT_RETURN && node_census_cleans(reader->output, token))
            {
                // A return's value under a cleanup is another control's (node_census_reader_expression's), in the few
                // units with a cleanup.
                context = NODE_CENSUS_ROOT_OTHER;
            }
            // Whether its top writes: its operand's last record. A root with a cast's operator is the front's discard at
            // a statement's terminator, the one such root it makes (CHECK_INPUT_FORM_DISCARD), and no explicit cast: what
            // a statement discards is its operand, whatever the root's token's role.
            const uint32_t top_writes = last_write + 1 == size;
            const uint32_t sequenced = (writes == 0) | ((writes == 1) & top_writes);
            // What a statement discards is its top: the block's mask says where that leaves its test nothing, a write, a
            // record of type void, and a call left out whose callee asks for no result (the asks' part).
            const uint32_t discards = (uint32_t)(tests->window_masks[BUSTER_NODE_TESTS_MASKS * (uint64_t)(operand / 64) + BUSTER_NODE_TESTS_MASK_DISCARDS] >> (operand & 63) & 1) ^ 1;
            const uint32_t addresses = (context == NODE_CENSUS_ROOT_RETURN) & return_address & ((set & (BUSTER_NODE_TESTS_SET_ADDRESS | BUSTER_NODE_TESTS_SET_ARRAY)) != 0);
            const uint32_t listed = (lists != 0) | !rooted | (unsequenced & (sequenced ^ 1)) | ((context == NODE_CENSUS_ROOT_STATEMENT) & discards) | addresses |
                                    ((state->placed[e] & BUSTER_NODE_FACTS_PLACED_WARNED) != 0);
            bits |= fixed | (context == NODE_CENSUS_ROOT_OTHER ? UINT64_C(1) << NODE_CENSUS_CONTROL : 0);
            // A `for`'s clause, under -Wfor-loop-analysis alone, which lists most expressions (-Wall's other warnings):
            // its two loads are for those with nothing listed.
            bits |= policy->loops && listed == 0 && node_census_for_clause(reader->parser, token) ? UINT64_C(1) << NODE_CENSUS_LOOP : 0;
            if (__builtin_expect(tests->pragma_count != 0 || (lowest <= tests->event_high && highest >= tests->event_low), 0))
            {
                bits |= node_census_reader_marks(tests, token, lowest, highest);
            }
            loop->verdict = bits;
            loop->kind = (uint8_t)context;
            loop->span_low = lowest;
            loop->span_high = highest;
            loop->listed = (uint8_t)listed;
            // The expressions the tests that read T's records run on, one after another.
            tests->called[called] = e;
            called += listed | verify_quiet;
        }
    }
    const uint64_t t3 = __rdtsc();
    if (verify_quiet)
    {
        // The reader's verify: each expression's operators the loop left out, by the same numbers as its lists', for
        // node_tests_expression to run their tests all the same.
        for (uint32_t e = 0; e < state->expression_count; e += 1)
        {
            const NodeTestsLoop* loop = &loops[e];
            if (loop->run != 0)
            {
                const uint32_t record = state->records[e];
                const uint32_t operand = state->operands[e];
                const uint32_t size = front->sizes[operand];
                const uint32_t first = operand + 1 - size;
                uint64_t* quiet_bits = tests->quiet_bits + (loop->operator_bits - tests->operator_bits);
                for (uint32_t word = 0; word < loop->words; word += 1)
                {
                    quiet_bits[word] = node_tests_window_word(tests->window_masks, BUSTER_NODE_TESTS_MASK_QUIET, first, size, word);
                }
                if (state->counts[e] > size)
                {
                    const uint64_t* masks = tests->window_masks + BUSTER_NODE_TESTS_MASKS * (uint64_t)(record / 64);
                    quiet_bits[size / 64] |= (masks[BUSTER_NODE_TESTS_MASK_QUIET] >> (record & 63) & 1) << (size & 63);
                }
            }
        }
    }
    // The machine as the reader's hook left it, for after: T runs here again, without checking itself against the
    // action a second time.
    const NodeMachine kept = *machine;
    machine->verify = false;
    for (uint32_t c = 0; c < called; c += 1)
    {
        const uint32_t e = tests->called[c];
        NodeTestsLoop* loop = &loops[e];
        const uint32_t record = state->records[e];
        const uint32_t operand = state->operands[e];
        const uint32_t root = state->roots[e];
        (void)node_machine_type(machine, state->firsts[e], root, state->counts[e]);
        // The tests read a root's bytes as the last of its expression's records'. A root's record apart from its
        // operand's (a control's, whose value waited) has its bytes put after its operand's. The records between the
        // two are of expressions whose roots came before this one, whose tests have run: the bytes stay there.
        const bool lone = state->counts[e] > front->sizes[operand] && record != operand + 1;
        uint8_t* const columns[3] = { tests->window_types, tests->window_properties, tests->window_operands };
        for (uint32_t column = 0; column < 3; column += 1)
        {
            columns[column][operand + 1] = lone ? columns[column][record] : columns[column][operand + 1];
        }
        const uint32_t token = front->tokens[record];
        const bool initialization = front->ops[record] == CHECK_OP_INITIALIZE;
        const uint32_t declaration = initialization && token < token_count ? reader->initializer_declarations[token] : BUSTER_CHECK_NONE;
        const TypeId declared = declaration < reader->output->token_count ? reader->declared_types[declaration] : 0;
        uint8_t kind = 0;
        s->own_quiet_bits = verify_quiet ? tests->quiet_bits + (loop->operator_bits - tests->operator_bits) : NULL;
        s->own_tree_verify = verify_quiet;
        const uint64_t verdict = node_census_reader_expression(tests, root, (CheckOp)front->ops[record], token, declaration, declared, &kind, loop);
        // The reader's verify: an expression with nothing listed has the same from the tests that read T's records as
        // from the window alone.
        types->own_direct_checked += loop->listed == 0;
        types->own_direct_wrong += loop->listed == 0 && (verdict != loop->verdict || kind != loop->kind || s->span_low != loop->span_low || s->span_high != loop->span_high);
        loop->verdict = verdict;
        loop->kind = kind;
        loop->span_low = s->span_low;
        loop->span_high = s->span_high;
    }
    const uint64_t t4 = __rdtsc();
    for (uint32_t c = 0; c < called; c += 1)
    {
        const uint32_t e = tests->called[c];
        (void)node_machine_type(machine, state->firsts[e], state->roots[e], state->counts[e]);
    }
    const uint64_t t5 = __rdtsc();
    *machine = kept;
    uint64_t apart = 0;
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        const NodeTestsLoop* loop = &loops[e];
        if (loop->run != 0)
        {
            // What the timed sweeps leave to count: the expressions with nothing listed; those the tests that read T's
            // records didn't run on, which count themselves there; and the roots' records apart from their operand's.
            const uint32_t operand = state->operands[e];
            const bool skipped = loop->listed == 0 && !verify_quiet;
            types->own_direct += loop->listed == 0;
            types->own_expressions += skipped;
            types->own_records += skipped ? state->counts[e] : 0;
            apart += state->counts[e] > front->sizes[operand] && state->records[e] != operand + 1;
            const uint32_t root = state->roots[e];
            const uint64_t reference = types->tests[root];
            const bool same = loop->verdict == reference && loop->kind == types->kinds[root] && loop->span_low == tests->span_lows[root] &&
                              loop->span_high == tests->span_highs[root];
            counts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS] += 1;
            counts[NODE_FACTS_COUNT_TESTS_RECORDS] += state->counts[e];
            counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] += !same;
            if (!same && types->own_listed < node_census_list)
            {
                types->own_listed += 1;
                printf("node-tests: window difference line=%u records=%u window=%#" PRIx64 " reference=%#" PRIx64 " kind=%u/%u span=%u..%u/%u..%u\n",
                       node_census_line(s->pp, s->program->parser, front->tokens[state->records[e]]), state->counts[e], loop->verdict, reference, loop->kind,
                       types->kinds[root], loop->span_low, loop->span_high, tests->span_lows[root], tests->span_highs[root]);
            }
        }
    }
    const uint64_t both = t4 - t3;
    const uint64_t alone = t5 - t4;
    const uint64_t rest = both > alone ? both - alone : 0;
    counts[NODE_FACTS_COUNT_TESTS_SWEPT] += front->count;
    counts[NODE_FACTS_COUNT_TESTS_APART] += apart;
    counts[NODE_FACTS_COUNT_TESTS_CHUNKS] += 1;
    counts[NODE_FACTS_COUNT_TESTS_BYTES_TICKS] += t1 - t0;
    counts[NODE_FACTS_COUNT_TESTS_LOOP_TICKS] += t2 - t1;
    counts[NODE_FACTS_COUNT_TESTS_REST_TICKS] += rest;
    counts[NODE_FACTS_COUNT_TESTS_AGAIN_TICKS] += alone;
    counts[NODE_FACTS_COUNT_TESTS_TICKS] += t3 - t0 + rest;
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

// The fields of a root's results the machine with no node of the walk's is compared by (NodeShadow.alone_fields).
#define BUSTER_NODE_ALONE_FIELD_RECORDS 0u
#define BUSTER_NODE_ALONE_FIELD_REFUSAL 1u
#define BUSTER_NODE_ALONE_FIELD_DIGEST 2u
#define BUSTER_NODE_ALONE_FIELD_TESTED 3u
#define BUSTER_NODE_ALONE_FIELD_TESTS 4u
#define BUSTER_NODE_ALONE_FIELD_SWEPT 5u
#define BUSTER_NODE_ALONE_FIELD_SWEEP_REFUSAL 6u
#define BUSTER_NODE_ALONE_FIELD_ROOT 7u
#define BUSTER_NODE_ALONE_FIELD_TAPE 8u
#define BUSTER_NODE_ALONE_FIELD_SIDE 9u
#define BUSTER_NODE_ALONE_FIELDS 10u
static const char* const node_alone_field_names[BUSTER_NODE_ALONE_FIELDS] = {
    "records", "refusal", "digest", "tested", "tests", "swept", "sweep-refusal", "root", "tape", "side",
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
    // --front-flat, --front-flat-first: the flat front beside the lean one (c_check_node_machine_flat.c). The files'
    // reports summed (NodeFrontShadow's flat_ fields): its pass's and its compaction's ticks, the tokens it visited, its
    // records, the children hopped over, the tokens with a child past the ring's reach and those left; and its
    // comparison with the lean front: the records both made, those that differ, and those only one made.
    uint64_t flat_ticks;
    uint64_t flat_compact_ticks;
    uint64_t flat_stage_ticks[8];
    uint64_t flat_tokens;
    uint64_t flat_records;
    uint64_t flat_empties;
    uint64_t flat_far;
    uint64_t flat_hops;
    uint64_t flat_beyond;
    uint64_t flat_left;
    uint64_t flat_compared;
    uint64_t flat_differences;
    uint64_t flat_only;
    uint64_t flat_lean_only;
    uint64_t flat_unread;
    uint32_t flat_listed;
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
    // And the program's own objects, which no declaration of the unit's declares (a multiversioned function's group's
    // reference): reader time has nothing for one, and nothing to compare. And the objects whose asm label a rename
    // pragma made once the reader was done (#3207): reader time has none for one, and T asks no label in such a unit
    // (node_machine_renames); their other facts are compared.
    uint64_t typed_objects_own;
    uint64_t typed_objects_renamed;
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
    // The machine with no node of the walk's (--front-alone, c_check_node_machine_alone.c): the bodies it read whole and
    // those it left, their postorder positions, its records, and its ticks, all of it and the sweeps' first run's; the
    // roots it ran on; those the machine ran on from the walk's nodes too, compared, and the ones that differ, by the
    // first field that does (the records' count, T's refusal, the records' digest, whether the tests ran, their bits or
    // the root's kind, whether C and X ran, their refusal, the root's target or object, the tape, the side's digest);
    // the roots only one of the two ran on (the walk's nodes' alone: in a function body, and outside any); and the
    // roots it took whose tests the walk's own events then refused.
    uint64_t alone_bodies;
    uint64_t alone_bodies_left;
    uint64_t alone_positions;
    uint64_t alone_records;
    uint64_t alone_ticks;
    uint64_t alone_machine_ticks;
    uint64_t alone_prepare_ticks;
    uint64_t alone_front_ticks;
    uint64_t alone_write_ticks;
    uint64_t alone_sweep_ticks;
    // The scan of each body's tokens for a root the front would lose: its ticks and the tokens it read.
    uint64_t alone_scan_ticks;
    uint64_t alone_scan_tokens;
    // With --front-alone-split, the machine's first run by sweep (T, the tests, C, X) and the clock's reads that made it.
    uint64_t alone_step_ticks[4];
    uint64_t alone_step_reads;
    uint64_t alone_roots;
    uint64_t alone_taken;
    uint64_t alone_compared;
    uint64_t alone_differences;
    uint64_t alone_fields[BUSTER_NODE_ALONE_FIELDS];
    uint64_t alone_walk_only;
    uint64_t alone_walk_only_outside;
    uint64_t alone_own_only;
    uint64_t alone_events_after;
    // The roots it took, checked after the walk against the tokens the walk's class actions left to its visit, and
    // those with one among their expression's tokens.
    uint64_t alone_taken_checked;
    uint64_t alone_taken_declined;
    // The tokens of the bodies it read that the walk marked as left by its class actions.
    uint64_t alone_declined_tokens;
    uint64_t alone_listed;
    // Its verdicts of the bodies the lowering lowered, by what stops it (BUSTER_NODE_ALONE_BODY_*), in bodies and in their
    // tokens; and against the machine on the walk's nodes, for which a body is whole where every root the lowering popped
    // is taken there: the bodies whole for both, for it alone (a root the lowering pops that is no root of the front's:
    // a braced initializer's element), and for the walk's nodes' alone (a root the front lost that the lowering never
    // pops: a condition it folds, a branch it drops). Neither is a difference: a body's verdict here says what the
    // machine's roots allow, and the statements are another piece's.
    uint64_t alone_verdicts[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_verdict_tokens[BUSTER_NODE_ALONE_BODY_VERDICTS];
    // By verdict too: its bodies' reading's ticks, their machine's first run's, those spent by the end of the chunk in
    // which each is first stopped, and the bodies (and their tokens) a scan of the tokens' kinds and classes alone
    // would refuse.
    uint64_t alone_verdict_reading[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_verdict_machine[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_verdict_before[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_verdict_scanned[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_verdict_scanned_tokens[BUSTER_NODE_ALONE_BODY_VERDICTS];
    uint64_t alone_scans_listed;
    // Its roots against the program's controls, in the lowered bodies (node_census_body). By the control's kind
    // (0: an EVALUATE, 1: a branch, 2: a RETURN with a value, 3: an INITIALIZE's part that is a value, 4: another with an
    // expression, a SWITCH's, a variable-length array's bound, an indirect jump's, a kept result's): the controls;
    // those whose expression's root's token has one root of its, none, or several; and those whose one root's control's
    // token, as the reader had it before the walk, is the control's. Then the roots the machine ran on there in those
    // bodies, and those some control's expression's token has.
    uint64_t alone_pair_controls[5];
    uint64_t alone_pair_one[5];
    uint64_t alone_pair_none[5];
    uint64_t alone_pair_several[5];
    uint64_t alone_pair_token[5];
    uint64_t alone_pair_roots;
    uint64_t alone_pair_claimed;
    uint64_t alone_pair_listed;
    // --bodies (node_bodies_begin): a body's entries made from the program's controls and its roots' tapes, compared with
    // the lowered function's. The lowered bodies; those the machine takes every popped root of; those the piece attempts,
    // their tokens and the entries it made; those whose entries are the function's and those that differ; and the bodies
    // it leaves out, in bodies and in tokens, by the first reason that does (NodeBodiesReason).
    // Each count is two: of the comparison with the machine's tapes after the pipeline (0), and of the one with the tapes
    // the machine with no node of the walk's kept before the walk, found by each control's token (1, with --front-alone;
    // node_bodies_kept), whose "every root the machine's" is that machine's verdict of the body before the walk.
    bool bodies;
    // --statement-controls: the unit read as -c reads it, with the statement controls' path. The units the reader took
    // the path in (those of 8,192 tokens and more, as in a compile), and the others.
    bool statement_controls;
    uint64_t statement_controls_units;
    uint64_t statement_controls_without;
    uint64_t bodies_lowered[2];
    uint64_t bodies_tokens[2];
    uint64_t bodies_whole[2];
    uint64_t bodies_whole_tokens[2];
    uint64_t bodies_attempted[2];
    uint64_t bodies_attempted_tokens[2];
    uint64_t bodies_entries[2];
    uint64_t bodies_same[2];
    uint64_t bodies_different[2];
    uint64_t bodies_reasons[2][16];
    uint64_t bodies_reason_tokens[2][16];
    // Those left out for a control of another kind, by the first such control's kind (CheckControlKind).
    uint64_t bodies_other_kinds[2][32];
    uint64_t bodies_listed[2];
    // Before the walk: the bodies the program dropped a range of (a constant `if`'s condition and the arm it skips), and
    // those of them attempted, which must be none: a body with a condition that is any constant is refused there.
    uint64_t bodies_dropped;
    uint64_t bodies_dropped_attempted;
    // The attempted bodies' plain locals (a class of their own, no size), and those whose address is taken.
    uint64_t bodies_locals[2];
    uint64_t bodies_locals_addressed[2];
    // Before the walk: the attempted bodies whose tapes' named objects are the program's references of the body; those
    // with an object the program does not list; those with a reference no tape names; and the two kinds' objects.
    uint64_t bodies_references_same;
    uint64_t bodies_references_more;
    uint64_t bodies_references_fewer;
    uint64_t bodies_references_extra;
    uint64_t bodies_references_missing;
    uint64_t bodies_references_listed;
    // The controls' twin from the tokens (node_twin_build), on the lowered bodies: by its reason (NodeTwinReason, the
    // attempted ones first) in bodies and tokens; the controls it made; the bodies whose controls are the program's and
    // those that differ.
    uint64_t twin_reasons[16];
    uint64_t twin_reason_tokens[16];
    uint64_t twin_controls;
    uint64_t twin_same;
    uint64_t twin_different;
    uint64_t twin_listed;
    // The return flow's silence by the twin's controls (node_twin_silent), on the bodies the twin attempts: those it
    // proves silent, and their tokens; those the program's own return flow has a warning for; and those it proved silent
    // of them, which must be none.
    uint64_t twin_silent;
    uint64_t twin_silent_tokens;
    uint64_t twin_loud;
    uint64_t twin_unsound;
    // A producer's two sets, of the lowered bodies: those no test of the tokens alone refuses, which it would read and
    // sweep; and those of them it would take (both comparisons the same, the return flow proved silent).
    uint64_t producer_swept;
    uint64_t producer_swept_tokens;
    uint64_t producer_taken;
    uint64_t producer_taken_tokens;
    uint64_t alone_body_tokens;
    uint64_t alone_whole_both;
    uint64_t alone_whole_both_tokens;
    uint64_t alone_whole_own;
    uint64_t alone_whole_walk;
    // Of the bodies the walk's nodes' machine takes whole and it doesn't, those with a block's static or extern
    // declaration, which are counted apart: a static object's initializer's values are roots of the front's that the
    // lowering never pops (they are data), so the machine on the walk's nodes is not asked about them.
    uint64_t alone_whole_walk_static;
    uint64_t alone_bodies_listed;
    // The times its front lost its waiting values (the arrays' room; a body's largest table), each a body's verdict's.
    uint64_t alone_resets;
    uint64_t alone_bodies_unread;
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
    uint16_t reader;
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
    // --window-tests: the tests in the machine's own form, on the window (NodeFrontTypes' counts, summed).
    uint64_t own_expressions;
    uint64_t own_records;
    uint64_t own_visited;
    uint64_t own_visits;
    uint64_t own_walks;
    uint64_t own_triggers[NODE_TESTS_TRIGGERS];
    uint64_t own_trees;
    uint64_t own_operators;
    uint64_t own_calls;
    uint64_t own_constants;
    uint64_t own_thirds;
    uint64_t own_count_differences;
    uint64_t own_quiet;
    uint64_t own_quiet_calls;
    uint64_t own_quiet_calls_checked;
    uint64_t own_quiet_calls_wrong;
    uint64_t own_quiet_calls_loud;
    uint64_t own_quiet_casts;
    uint64_t own_quiet_casts_checked;
    uint64_t own_quiet_casts_wrong;
    uint64_t own_quiet_orders;
    uint64_t own_quiet_orders_checked;
    uint64_t own_quiet_orders_wrong;
    uint64_t own_result_casts;
    uint64_t own_result_asks;
    uint64_t own_root_nulls;
    uint64_t own_quiet_checked;
    uint64_t own_quiet_wrong;
    uint64_t own_values_checked;
    uint64_t own_values_wrong;
    uint64_t own_tree_checked;
    uint64_t own_tree_wrong;
    uint64_t own_lists_past;
    uint64_t own_direct;
    uint64_t own_direct_checked;
    uint64_t own_direct_wrong;
    // --front-facts (#2850's step 2's third piece): the fact words' kernel at reader time
    // (c_check_node_machine_facts.c's node_facts_reader_flush). The files' counts summed (NodeFrontTypes.facts_counts,
    // facts_wanted_ops and facts_left_ops), and the differences listed.
    uint64_t reader_facts[NODE_FACTS_COUNTS];
    uint64_t reader_facts_wanted[CHECK_OP_COUNT];
    uint64_t reader_facts_left[CHECK_OP_COUNT];
    uint64_t reader_facts_whys[BUSTER_NODE_FACTS_WHYS];
    uint32_t reader_facts_listed;
    uint32_t reader_facts_skipped_listed;
    // --front-window (the piece after it): the front writes the kernel's window as it makes each record, and the kernel
    // reads it there (PPOptions.node_reader's BUSTER_NODE_READER_WINDOW). The expressions its window doesn't give, by
    // why, and those listed; and the front's own (NodeFrontShadow's): the same lean front without the window on the
    // same chunks, its ticks and records and the chunks in which the two differed; what the chunks' ends kept, and the
    // most the calls' arguments' list held at one (more than BUSTER_NODE_FRONT_RECORDS is a failure); and the names'
    // pass.
    uint64_t reader_window_whys[BUSTER_NODE_FACTS_WHYS];
    uint32_t reader_window_listed;
    // C on the window: the listed expressions by reason, and the differences shown.
    uint64_t contexts_listed[BUSTER_NODE_CONTEXTS_REASONS];
    uint32_t contexts_shown;
    // X's rows on the window: the differences shown.
    uint32_t rows_shown;
    // A summary's side facts on the window (--window-rows; node_shadow_summaries, after the pipeline): the roots reader
    // time kept them for, each compared with the program's own walks on it, and the references the program's walk
    // makes of them; those whose references differ and those whose return fact differs; the roots whose fold was
    // asked (the kept ones; with the control, every root reader time typed) and, of them, those whose truth was (a
    // branch's condition's), those whose truth check_program_branch_truth knows otherwise than their root's value
    // says it, and those whose root's constant the controls' fold would not fold as a leaf of that value folds; of the
    // compared, those with a stop and with an unknown for the program's fact, the conditions whose truth the program
    // knows and the roots whose constant the controls fold; and the expressions the references' store had no room
    // for. The switch selector type before promotion is compared with the warning pass; every difference must be zero.
    uint64_t summary_selectors;
    uint64_t summary_enum_selectors;
    uint64_t summary_selector_differences;
    uint64_t summary_compared;
    uint64_t summary_references;
    uint64_t summary_reference_differences;
    uint64_t summary_fact_differences;
    uint64_t summary_asked;
    uint64_t summary_conditions;
    uint64_t summary_truth_differences;
    uint64_t summary_fold_differences;
    uint64_t summary_stops;
    uint64_t summary_unknowns;
    uint64_t summary_truths;
    uint64_t summary_folds;
    uint64_t summary_unkept;
    uint32_t summary_listed;
    uint64_t front_plain_ticks;
    uint64_t front_plain_records;
    uint64_t front_plain_differences;
    uint64_t front_window_kept_values;
    uint64_t front_window_kept_records;
    uint64_t front_window_cut_values;
    uint64_t front_window_arguments_most;
    uint64_t front_window_name_ticks;
    uint64_t front_window_names;
    uint64_t front_window_lines;
    uint64_t front_window_line_ticks;
    uint64_t front_window_name_differences;
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
// (node_shadow_same_calls). A branch to the control's own label (BUSTER_NODE_MACHINE_CONTROL_LABEL, a statement's
// condition that is branches, #2890) is the stream's where its target is the entry of the root's control's target, a
// label of the body's by the control's index (CheckLowerOwnerPop.labels).
static bool node_shadow_same_entries(NodeCensusScratch* s, const NodeMachine* machine, const CheckLowerOwnerPop* pop, uint32_t* compared)
{
    const Tier0Function* function = s->function;
    const uint32_t first = machine->first;
    const uint32_t last = first + machine->count - 2;
    const uint32_t root = machine->root;
    const uint32_t control_index = s->node_controls[root];
    const CheckControl* control = control_index != BUSTER_CHECK_NONE ? s->program->controls + control_index : NULL;
    const bool branching = control != NULL && (control->kind == CHECK_CONTROL_BRANCH_FALSE || control->kind == CHECK_CONTROL_BRANCH_TRUE);
    const uint64_t control_label = branching ? (uint64_t)pop->labels + control->target : UINT64_MAX;
    const uint64_t control_entry = control_label < s->label_count ? s->labels[control_label] : UINT64_MAX;
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
                    branch && machine->tape[t] == BUSTER_NODE_MACHINE_CONTROL_LABEL ? function->values[e] == control_entry :
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
        // The one sweep writes its labels' entries and its calls' records where the three sweeps wrote theirs: an entry
        // it doesn't write must not read as theirs.
        memset(machine->label_entries, 0xff, (uint64_t)labels * sizeof(uint32_t));
        memset(machine->call_records, 0xff, (uint64_t)calls * sizeof(uint32_t));
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

// A summary's side facts, which reader time kept by root for each expression X's rows take whole on the window
// (c_check_node_machine_rows.c's node_rows_window_keep), against the program's own walks after the pipeline: what a
// pass after the reader reads below a full expression's root today, and what one summary node in its nodes' place
// would have to answer (docs/node-machine.md, "A summary node's side facts on the window").
// - Its references: check_program_references run on the root alone, the same set of objects as reader time's list
//   (which has an object once a leaf).
// - Its return fact: check_program_return_facts' byte for the root.
// - Its truth, where it is a branch's condition (a BRANCH_FALSE's or a BRANCH_TRUE's node: the return flow and the
//   warnings' reach ask it of those), which a summary node's value has to say: check_program_branch_truth, which
//   reads a condition's operands, knows a truth for the root exactly where check_program_runtime_truth, which reads
//   the root's value alone, does, and the same one.
// - A switch's selector type before promotion, as read by -Wswitch (check_common_switch_selector_type).
// - Its fold, the same: check_control_fold_truth, which walks below a root whose value is an integer's constant,
//   folds it wherever that value alone would (a leaf's: an integer's constant with no effect and a truth).
// With the control (BUSTER_NODE_READER_WINDOW_SUMMARY_FAULT) reader time kept each expression's facts with one of
// them changed, and every root's references or fact must differ; and its truth and its fold are asked of every root
// reader time typed, kept or not, so that the two are seen to count the expressions the window lists for them.
static void node_shadow_summaries(NodeShadow* shadow, const NodeCensusScratch* s, CheckProgram* program, const NodeFrontTypes* typed, Arena* scratch)
{
    const CheckGraph* graph = &program->input->graph;
    const uint64_t mark = scratch->length;
    const uint32_t nodes = graph->count < typed->node_capacity ? graph->count : typed->node_capacity;
    const uint64_t objects = (uint64_t)program->object_count + 2;
    const bool control = (shadow->reader & BUSTER_NODE_READER_WINDOW_SUMMARY_FAULT) != 0;
    const uint8_t* facts = check_program_return_facts(program, scratch, NULL, false);
    uint32_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects);
    uint32_t* ours = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, objects);
    uint32_t* marks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)graph->count + 2);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 4 * (uint64_t)graph->count + 64);
    uint64_t* references = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)graph->count + objects);
    uint8_t* needed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, objects);
    uint8_t* branches = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)graph->count + 1);
    // (A unit with no node has no facts' column, and no root.)
    const bool room = (facts != NULL || graph->count == 0) && seen != NULL && ours != NULL && marks != NULL && stack != NULL && references != NULL && needed != NULL &&
                      branches != NULL;
    // The controls' fold's own walk, on a stack of its own: a writer with what check_control_foldable reads of one for
    // an expression without a statement's value.
    const CheckControlWriter writer = {
        .program = program,
        .parser = program->parser,
        .fold_stack = stack,
        .fold_capacity = graph->count + 1,
    };
    if (room)
    {
        memset(seen, 0, objects * sizeof(uint32_t));
        memset(ours, 0, objects * sizeof(uint32_t));
        memset(marks, 0, ((uint64_t)graph->count + 2) * sizeof(uint32_t));
        // The branches' conditions.
        memset(branches, 0, (uint64_t)graph->count + 1);
        for (uint32_t c = 0; c < program->control_count; c += 1)
        {
            const CheckControl* entry = program->controls + c;
            if ((entry->kind == CHECK_CONTROL_BRANCH_FALSE || entry->kind == CHECK_CONTROL_BRANCH_TRUE) && entry->node < graph->count)
            {
                branches[entry->node] = 1;
            }
        }
    }
    shadow->summary_unkept += typed->summary_unkept + (room ? 0u : 1u);
    uint32_t epoch = 0;
    for (uint32_t root = 0; room && root < nodes; root += 1)
    {
        const bool kept = typed->summary_firsts[root] != UINT32_MAX;
        const bool asked = kept || (control && typed->counts[root] != 0);
        bool referenced = true;
        bool returned = true;
        bool selector_same = true;
        uint32_t found = 0;
        if (kept)
        {
            // The program's references of the root, and reader time's as a set.
            const CheckControl evaluated = { .kind = CHECK_CONTROL_EVALUATE, .node = root, .target = BUSTER_CHECK_NONE };
            const uint32_t* objects_kept = typed->summary_words + typed->summary_firsts[root];
            uint32_t distinct = 0;
            epoch += 1;
            found = check_program_references(program, &evaluated, 1, epoch, seen, marks, stack, references, needed);
            for (uint32_t i = 0; i < typed->summary_counts[root]; i += 1)
            {
                const uint32_t object = objects_kept[i];
                referenced = referenced && object < program->object_count;
                if (object < program->object_count)
                {
                    distinct += ours[object] != epoch ? 1u : 0u;
                    ours[object] = epoch;
                }
            }
            referenced = referenced && distinct == found;
            for (uint32_t r = 0; r < found; r += 1)
            {
                referenced = referenced && ours[(uint32_t)(references[r] >> 32)] == epoch;
            }
            // Use the parser's condition and the warning pass's walk as the oracle. Its diagnostic is located at
            // the selector's first token, which can have a different type from the value being switched on (#3200).
            const uint32_t closer = graph->tokens[root];
            if (graph->ops[root] == CHECK_OP_POSITIVE && closer < program->parser->tokens.count && program->parser->tree.roles[closer] == PARSE_ROLE_CLOSE_CONDITION)
            {
                const TypeId selector = check_common_switch_selector_type(program->input, program->stream, closer);
                const uint64_t at = (uint64_t)typed->summary_firsts[root] + typed->summary_counts[root];
                selector_same = selector != 0 && at < typed->summary_count && typed->summary_words[at] == selector;
                shadow->summary_selectors += 1;
                shadow->summary_enum_selectors += selector != 0 &&
                                                  type_kind(program->types, type_unqualified(type_strip_aligned(program->types, selector))) == BUSTER_TYPE_KIND_ENUM;
                shadow->summary_selector_differences += !selector_same;
            }
            returned = typed->summary_facts[root] == facts[root];
            shadow->summary_compared += 1;
            shadow->summary_references += found;
            shadow->summary_reference_differences += !referenced;
            shadow->summary_fact_differences += !returned;
            shadow->summary_stops += (facts[root] & BUSTER_CHECK_RETURN_STOPS) != 0;
            shadow->summary_unknowns += (facts[root] & BUSTER_CHECK_RETURN_UNKNOWN) != 0;
        }
        bool truthful = true;
        bool folding = true;
        if (asked)
        {
            // Its truth by its operands against its truth by its value; and its fold by the controls' walk against a
            // leaf's of its value. (A root with a statement's value below it is none the window takes.)
            const uint32_t first = typed->firsts[root];
            const uint32_t records = typed->counts[root];
            bool statement = graph->ops[root] == CHECK_OP_STATEMENT_VALUE;
            for (uint32_t node = first; node + 1 < first + records && node < graph->count; node += 1)
            {
                statement = statement || graph->ops[node] == CHECK_OP_STATEMENT_VALUE;
            }
            bool branch = false;
            bool truth = false;
            const bool condition = branches[root] != 0;
            const bool branched = condition && check_program_branch_truth(program, root, root, &branch);
            const bool valued = condition && check_program_runtime_truth(program, root, root, &truth);
            truthful = branched == valued && (!branched || branch == truth);
            const CheckValue value = check_stream_value(program->stream, root);
            uint32_t width = 0;
            bool sign = false;
            bool leaf_truth = false;
            const bool leaf = check_action_integer_type(program->types, value.type, &width, &sign) && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                              check_action_truth(program->types, &value, &leaf_truth);
            bool fold_truth = false;
            const bool folded = !statement && check_control_fold_truth(&writer, root, &fold_truth);
            folding = !statement && folded == leaf && (!folded || fold_truth == leaf_truth);
            shadow->summary_asked += 1;
            shadow->summary_conditions += condition;
            shadow->summary_truth_differences += !truthful;
            shadow->summary_fold_differences += !folding;
            shadow->summary_truths += kept && branched;
            shadow->summary_folds += kept && folded;
        }
        if ((!referenced || !returned || !truthful || !folding || !selector_same) && shadow->summary_listed < node_census_list)
        {
            shadow->summary_listed += 1;
            printf("node-shadow: window summaries difference file=%s line=%u token=%u %s: references=%s (the program's %u, reader time's %u) fact=%u (reader %u) truth=%s fold=%s selector=%s\n",
                   s->file, node_census_line(s->pp, program->parser, graph->tokens[root]), graph->tokens[root], kept ? "kept" : "not kept", referenced ? "same" : "DIFFERENT", found,
                   kept ? typed->summary_counts[root] : 0u, (uint32_t)facts[root], kept ? (uint32_t)typed->summary_facts[root] : 0u, truthful ? "same" : "DIFFERENT",
                   folding ? "same" : "DIFFERENT", selector_same ? "same" : "DIFFERENT");
        }
    }
    arena_pop_to(scratch, mark);
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

// A body's entries from its controls and its roots' tapes (measure-node-machine --shadow --bodies; docs/node-machine.md,
// "A body's entries from its controls and its tapes"): #2850's statements' first piece, in the shadow. The comparison
// above reads a tape against the entries its expression's nodes own, relative to the expression's first; it never
// reads the entries a control owns (its branch, its return, its initializer's store, its jump) nor a temporary's local.
// Here a lowered body's whole stream is made again from the program's controls and the tapes the machine makes of the
// body's roots, and compared with the lowered function's, entry for entry: its kind, its class and its value.
//
// - A control's label is its first entry (check_lower_control places it there), the body's end's the entry after its
//   last control's. A branch's or a jump's value is its target control's label's entry (check_lower_resolve_labels).
// - A root's tape follows its control's label, each entry's value made the stream's: a declared object's or a
//   parameter's local is the lowered function's, by the pairing the comparison above keeps (the locals' table is the
//   piece after this one's); a temporary's is the function's locals less the body's temporaries, plus the temporaries
//   before it; a label's is the tape's first entry in the body plus the label's place on the tape, and the control's own
//   (BUSTER_NODE_MACHINE_CONTROL_LABEL) its control's target's; a call's row is the body's calls before it plus its own.
// - A control's own entries follow its root's (check_lower_control pushes them before the root's task): a condition's
//   branch, unless the condition is branches (its root's record is BUSTER_NODE_MACHINE_BRANCHED); a return's RETURN of
//   the result's class, after a zero where a function with a result returns none; an initialization's SET_LOCAL of its
//   plan's type's class; a jump's JUMP.
// - The implicit return at the body's end (check_lower_part_body's): a void function's RETURN, main's zero and RETURN.
//
// A body the piece attempts: every root the lowering popped of it is the machine's, each the one root of an EVALUATE,
// a branch, a RETURN or an INITIALIZE of one whole scalar value; it has no other control but JUMP and END; its stack is
// not tracked; its function has a prototype or no parameter; its result is void or a scalar of a class I8 to F64. Every
// other body is counted by the first reason that leaves it out: a root first, then the body's own.
typedef enum NodeBodiesReason
{
    NODE_BODIES_ATTEMPTED,
    // A root the machine does not take (a test's refusal, or T's, C's or X's).
    NODE_BODIES_ROOT,
    // A root that is no control's of the body, a second one of a control, one before its control's turn, or a control
    // with an expression whose root the lowering did not pop.
    NODE_BODIES_STRAY,
    // A tracked stack (CheckLowerWriter.track): the lowering writes entries of its own between the controls'.
    NODE_BODIES_TRACKED,
    // A function without a prototype that has parameters: their promoted copies are entries before the first control.
    NODE_BODIES_PROLOGUE,
    // A result through a hidden pointer or a result object, or of a class past F64.
    NODE_BODIES_RESULT,
    NODE_BODIES_CONTROL,
    // A return whose value a RESULT control kept, or a tail call's.
    NODE_BODIES_RETURN,
    // An initialization that is not one whole value of a scalar object without a brace.
    NODE_BODIES_INITIALIZER,
    NODE_BODIES_ROOM,
    // Before the walk alone: a control with an expression whose token has no one root kept there with its tape and what
    // is beside it; and a condition whose root's value is a constant, which the program may fold.
    NODE_BODIES_UNKEPT,
    NODE_BODIES_CONSTANT,
    // A statement the program may leave out because no control reaches it (node_twin_unreached).
    NODE_BODIES_REACH,
    NODE_BODIES_REASONS,
} NodeBodiesReason;

static const char* const node_bodies_reason_names[NODE_BODIES_REASONS] = {
    "attempted",          "a-root-the-machine-does-not-take",  "a-root-of-no-control-of-its-own", "a-tracked-stack", "copies-of-parameters-without-a-prototype",
    "a-result-of-another-class", "a-control-of-another-kind", "a-return-of-another-kind",        "an-initialization-of-another-form", "no-room",
    "a-control's-root-not-kept-before-the-walk", "a-condition-that-is-a-constant", "a-statement-no-control-reaches",
};

// What an entry's value is until the body's end: the stream's already; a control's index, whose label's entry it
// becomes; a temporary's number among the body's; a declared object, whose local the lowered function gives.
#define BUSTER_NODE_BODIES_VALUE 0u
#define BUSTER_NODE_BODIES_LABEL 1u
#define BUSTER_NODE_BODIES_TEMPORARY 2u
#define BUSTER_NODE_BODIES_OBJECT 3u

typedef struct NodeBodies NodeBodies;
struct NodeBodies
{
    // The body's entries so far, `count` of `capacity`, and each one's value's kind (BUSTER_NODE_BODIES_*).
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    uint8_t* marks;
    uint32_t capacity;
    uint32_t count;
    // Each control's first entry by its index in the body, the body's end's after them: `label_capacity` of them.
    uint32_t* labels;
    uint32_t label_capacity;
    // By the function's local, `local_capacity` of them: whether an entry of the body takes its address.
    uint8_t* addressed;
    uint32_t local_capacity;
    // The objects the body's tapes name, kept before the walk (a global's entry's, a call's callee's): `named_count` of
    // `named_capacity`, for the comparison with the program's references of the body.
    uint32_t* named;
    uint32_t named_count;
    uint32_t named_capacity;
    const CheckProgramBody* body;
    // The temporaries and the calls' rows of the tapes so far.
    uint32_t temporaries;
    uint32_t calls;
    // The controls begun, and the one whose root is awaited or written (BUSTER_CHECK_NONE: none).
    uint32_t next;
    uint32_t open;
    // The open control's root is written; it is branches.
    bool rooted;
    bool branched;
    // Every root so far is the machine's; each is a control's own; the entries had room; the calls' rows are the function's.
    bool whole;
    bool placed;
    bool room;
    bool calls_same;
    // The body's own reason (NodeBodiesReason), the reason of a root out of place (NODE_BODIES_STRAY, or before the walk
    // _UNKEPT or _CONSTANT), the kind of its first control of another kind where that is the reason, its result's class
    // and whether it has none.
    uint8_t reason;
    uint8_t unplaced;
    uint8_t other_kind;
    uint8_t result_class;
    bool result_void;
};

static void node_bodies_put(NodeBodies* built, uint8_t kind, uint8_t class, uint64_t value, uint8_t mark)
{
    if (built->count < built->capacity)
    {
        built->kinds[built->count] = kind;
        built->classes[built->count] = class;
        built->values[built->count] = value;
        built->marks[built->count] = mark;
        built->count += 1;
    }
    else
    {
        built->room = false;
    }
}

// Whether an INITIALIZE control's plan is one whole value of a scalar object, without a brace before it: the machine's
// initializer root (node_census_root's).
static bool node_bodies_initializer(const NodeCensusScratch* s, const CheckControl* control)
{
    const CheckProgram* program = s->program;
    const CheckInitializers* initializers = program->initializers;
    const TypeTable* types = program->types;
    const Parser* parser = program->parser;
    bool plain = initializers != NULL && control->target < initializers->count && control->token < program->input->token_count;
    if (plain)
    {
        const CheckInitializer* plan = initializers->plans + control->target;
        const CheckInitPart* part = plan->count == 1 ? plan->parts : NULL;
        const TypeId declared = program->input->declared_types[control->token];
        const bool scalar = declared != 0 && !node_census_aggregate(types, declared) && !type_kind_is_array(node_census_kind(types, declared));
        bool brace = false;
        for (uint32_t t = control->token + 1; part != NULL && t < part->token && t < parser->tokens.count && !brace; t += 1)
        {
            brace = parser->tokens.kinds[t] == PARSE_KIND_L_BRACE;
        }
        plain = part != NULL && part->kind == CHECK_INIT_VALUE && part->bit_offset == 0 && part->bit_width == 0 && part->node < program->stream->count && scalar && !brace &&
                !node_census_labelled(program->input, parser, control->token);
    }
    return plain;
}

// The body `body` of the lowered function at hand (NodeCensusScratch.function): nothing written, and its own reason.
static void node_bodies_begin(NodeBodies* built, const NodeCensusScratch* s, const CheckProgramBody* body)
{
    const CheckProgram* program = s->program;
    const TypeTable* types = program->types;
    const Tier0Function* function = s->function;
    const TypeId type = program->input->declared_types[body->declaration];
    const TypeId result = type_return(types, type);
    const bool none = result == BUSTER_TYPE_VOID;
    const uint8_t class = none ? BUSTER_TYPED_CLASS_VOID : node_machine_class(s->machine, result);
    built->body = body;
    built->count = 0;
    built->temporaries = 0;
    built->calls = 0;
    built->next = 0;
    built->open = BUSTER_CHECK_NONE;
    built->rooted = false;
    built->branched = false;
    built->whole = true;
    built->placed = true;
    built->unplaced = NODE_BODIES_STRAY;
    built->room = true;
    built->calls_same = true;
    built->result_class = class;
    built->result_void = none;
    uint8_t reason = NODE_BODIES_ATTEMPTED;
    if (body->count != body->entry_count)
    {
        reason = NODE_BODIES_TRACKED;
    }
    else if ((types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 && function->parameter_count != 0)
    {
        reason = NODE_BODIES_PROLOGUE;
    }
    else if (function->hidden_size != 0 || function->result_size != 0 || (!none && (class < BUSTER_TYPED_CLASS_I8 || class > BUSTER_TYPED_CLASS_F64)))
    {
        reason = NODE_BODIES_RESULT;
    }
    for (uint32_t c = body->first; reason == NODE_BODIES_ATTEMPTED && c < body->first + body->count; c += 1)
    {
        const CheckControl* control = program->controls + c;
        const CheckControlKind kind = control->kind;
        if (kind == CHECK_CONTROL_RETURN && (control->target == BUSTER_CHECK_CONTROL_KEPT || control->target == BUSTER_CHECK_CONTROL_TAIL))
        {
            reason = NODE_BODIES_RETURN;
        }
        else if (kind == CHECK_CONTROL_INITIALIZE && !node_bodies_initializer(s, control))
        {
            reason = NODE_BODIES_INITIALIZER;
        }
        else if (kind != CHECK_CONTROL_EVALUATE && kind != CHECK_CONTROL_INITIALIZE && kind != CHECK_CONTROL_RETURN && kind != CHECK_CONTROL_END &&
                 kind != CHECK_CONTROL_BRANCH_FALSE && kind != CHECK_CONTROL_BRANCH_TRUE && kind != CHECK_CONTROL_JUMP)
        {
            reason = NODE_BODIES_CONTROL;
            built->other_kind = (uint8_t)kind;
        }
    }
    built->reason = reason == NODE_BODIES_ATTEMPTED && body->count >= built->label_capacity ? NODE_BODIES_ROOM : reason;
}

// The open control's own entries, after its root's, and no control open.
static void node_bodies_close(NodeBodies* built, const NodeCensusScratch* s)
{
    if (built->open != BUSTER_CHECK_NONE)
    {
        const CheckProgram* program = s->program;
        const CheckControl* control = program->controls + built->body->first + built->open;
        const CheckControlKind kind = control->kind;
        // The controls with an expression have one root each: a RETURN's where it has a value.
        const bool expected = kind == CHECK_CONTROL_EVALUATE || kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE || kind == CHECK_CONTROL_INITIALIZE ||
                              (kind == CHECK_CONTROL_RETURN && control->node != BUSTER_CHECK_NONE);
        built->placed = built->placed && expected == built->rooted;
        if ((kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE) && !built->branched)
        {
            node_bodies_put(built, kind == CHECK_CONTROL_BRANCH_FALSE ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, control->target,
                            BUSTER_NODE_BODIES_LABEL);
        }
        else if (kind == CHECK_CONTROL_JUMP)
        {
            node_bodies_put(built, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, control->target, BUSTER_NODE_BODIES_LABEL);
        }
        else if (kind == CHECK_CONTROL_RETURN)
        {
            if (control->node == BUSTER_CHECK_NONE && !built->result_void)
            {
                node_bodies_put(built, BUSTER_TYPED_CONST, built->result_class, 0, BUSTER_NODE_BODIES_VALUE);
            }
            node_bodies_put(built, BUSTER_TYPED_RETURN, built->result_class, 0, BUSTER_NODE_BODIES_VALUE);
        }
        else if (kind == CHECK_CONTROL_INITIALIZE)
        {
            const CheckInitializer* plan = program->initializers->plans + control->target;
            const uint32_t object = program->input->objects[control->token];
            node_bodies_put(built, BUSTER_TYPED_SET_LOCAL, node_machine_class(s->machine, plan->type), object, BUSTER_NODE_BODIES_OBJECT);
        }
        built->open = BUSTER_CHECK_NONE;
    }
}

// The controls up to `control` (an index in the body) begun and, but for it, ended: each one's label at its first entry.
static void node_bodies_advance(NodeBodies* built, const NodeCensusScratch* s, uint32_t control)
{
    while (built->next <= control && built->next < built->body->count)
    {
        node_bodies_close(built, s);
        built->labels[built->next] = built->count;
        built->open = built->next;
        built->rooted = false;
        built->branched = false;
        built->next += 1;
    }
}

// A root's tape and what a splice reads beside it: the machine's own after the pipeline, or the one kept before the walk.
typedef struct NodeBodiesTape NodeBodiesTape;
struct NodeBodiesTape
{
    const uint8_t* kinds;
    const uint8_t* classes;
    const uint64_t* values;
    // Each label's place on the tape.
    const uint32_t* labels;
    uint32_t entries;
    uint32_t label_count;
    uint32_t temporaries;
    uint32_t calls;
    bool branched;
    // A global's entry holds its object, which the program's numbering makes its symbol: a tape kept before the walk.
    bool objects;
};

// `tape` after the label of the body's control `index`: each entry's value the stream's.
static void node_bodies_splice(NodeBodies* built, const NodeCensusScratch* s, uint32_t index, const NodeBodiesTape* tape)
{
    const CheckProgram* program = s->program;
    node_bodies_advance(built, s, index);
    const uint32_t start = built->count;
    for (uint32_t t = 0; t < tape->entries; t += 1)
    {
        const uint8_t entry = tape->kinds[t];
        const uint64_t value = tape->values[t];
        const bool branch = entry >= BUSTER_TYPED_BRANCH_FALSE && entry <= BUSTER_TYPED_JUMP_VALUE;
        if (node_shadow_local_kind(entry) && (value & BUSTER_NODE_MACHINE_TEMPORARY) != 0)
        {
            node_bodies_put(built, entry, tape->classes[t], built->temporaries + (value & ~BUSTER_NODE_MACHINE_TEMPORARY), BUSTER_NODE_BODIES_TEMPORARY);
        }
        else if (node_shadow_local_kind(entry))
        {
            node_bodies_put(built, entry, tape->classes[t], value, BUSTER_NODE_BODIES_OBJECT);
        }
        else if (branch && value == BUSTER_NODE_MACHINE_CONTROL_LABEL)
        {
            node_bodies_put(built, entry, tape->classes[t], program->controls[built->body->first + index].target, BUSTER_NODE_BODIES_LABEL);
        }
        else if (branch)
        {
            node_bodies_put(built, entry, tape->classes[t], value < tape->label_count ? (uint64_t)start + tape->labels[value] : UINT64_MAX, BUSTER_NODE_BODIES_VALUE);
        }
        else if (entry == BUSTER_TYPED_CALL)
        {
            node_bodies_put(built, entry, tape->classes[t], built->calls + value, BUSTER_NODE_BODIES_VALUE);
        }
        else if (tape->objects && node_shadow_global_kind(entry))
        {
            node_bodies_put(built, entry, tape->classes[t], value < program->object_count ? program->object_symbols[value] : UINT64_MAX, BUSTER_NODE_BODIES_VALUE);
        }
        else
        {
            node_bodies_put(built, entry, tape->classes[t], value, BUSTER_NODE_BODIES_VALUE);
        }
    }
    built->temporaries += tape->temporaries;
    built->calls += tape->calls;
    built->rooted = true;
    built->branched = tape->branched;
}

// The machine's tape of the root whose pop is `pop`, taken, after its control's label: each entry's value the stream's.
static void node_bodies_root(NodeBodies* built, NodeCensusScratch* s, const NodeMachine* machine, const CheckLowerOwnerPop* pop, NodeCensusRoot context)
{
    const CheckProgram* program = s->program;
    const CheckProgramBody* body = built->body;
    const uint32_t index = s->node_controls[pop->node];
    const bool own = index != BUSTER_CHECK_NONE && index >= body->first && index - body->first < body->count &&
                     (built->open == BUSTER_CHECK_NONE || index - body->first > built->open || (index - body->first == built->open && !built->rooted));
    const CheckControlKind kind = own ? program->controls[index].kind : CHECK_CONTROL_END;
    const bool fits = own && (kind == CHECK_CONTROL_EVALUATE ? context == NODE_CENSUS_ROOT_STATEMENT :
                              kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE ? context == NODE_CENSUS_ROOT_CONDITION :
                              kind == CHECK_CONTROL_RETURN ? context == NODE_CENSUS_ROOT_RETURN :
                              kind == CHECK_CONTROL_INITIALIZE ? context == NODE_CENSUS_ROOT_INITIALIZER : false);
    built->placed = built->placed && fits;
    if (fits && built->reason == NODE_BODIES_ATTEMPTED)
    {
        // Its calls' rows' contents, at the body's rows they are.
        built->calls_same = built->calls_same && (machine->call_count == 0 || node_shadow_same_calls(machine, s->function, built->calls));
        const NodeBodiesTape tape = {
            .kinds = machine->kinds,
            .classes = machine->classes,
            .values = machine->tape,
            .labels = machine->label_entries,
            .entries = machine->entry_count,
            .label_count = machine->label_count,
            .temporaries = machine->temporary_count,
            .calls = machine->call_count,
            .branched = machine->count != 0 && (node_machine_record_state(machine, machine->count - 1) & BUSTER_NODE_MACHINE_BRANCHED) != 0,
        };
        node_bodies_splice(built, s, index - body->first, &tape);
    }
}

// The body's end: its last controls, its implicit return, each value made the stream's, and the comparison with the
// lowered function. `tokens` are the body's.
static void node_bodies_end(NodeBodies* built, NodeShadow* shadow, NodeCensusScratch* s, uint64_t tokens, uint32_t source)
{
    const CheckProgram* program = s->program;
    const CheckProgramBody* body = built->body;
    const Tier0Function* function = s->function;
    const uint8_t reason = !built->whole ? NODE_BODIES_ROOT : built->reason != NODE_BODIES_ATTEMPTED ? built->reason : !built->placed ? built->unplaced : NODE_BODIES_ATTEMPTED;
    shadow->bodies_lowered[source] += 1;
    shadow->bodies_tokens[source] += tokens;
    shadow->bodies_whole[source] += built->whole;
    shadow->bodies_whole_tokens[source] += built->whole ? tokens : 0;
    bool attempted = reason == NODE_BODIES_ATTEMPTED;
    if (attempted)
    {
        node_bodies_advance(built, s, body->count - 1);
        node_bodies_close(built, s);
        // A control with an expression and no root of the lowering's found only now.
        attempted = built->placed;
    }
    if (attempted)
    {
        built->labels[body->count] = built->count;
        if (built->result_void)
        {
            node_bodies_put(built, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0, BUSTER_NODE_BODIES_VALUE);
        }
        else if (body->is_main && built->result_class == BUSTER_TYPED_CLASS_I32)
        {
            node_bodies_put(built, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0, BUSTER_NODE_BODIES_VALUE);
            node_bodies_put(built, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I32, 0, BUSTER_NODE_BODIES_VALUE);
        }
        attempted = built->room;
    }
    const uint8_t final = attempted ? NODE_BODIES_ATTEMPTED : reason != NODE_BODIES_ATTEMPTED ? reason : !built->placed ? built->unplaced : NODE_BODIES_ROOM;
    shadow->bodies_reasons[source][final] += 1;
    shadow->bodies_reason_tokens[source][final] += tokens;
    shadow->bodies_other_kinds[source][final == NODE_BODIES_CONTROL && built->other_kind < BUSTER_ARRAY_LENGTH(shadow->bodies_other_kinds[source]) ? built->other_kind : 0] += final == NODE_BODIES_CONTROL;
    if (attempted)
    {
        // The comparison: the counts, then each entry. A temporary's local is the function's locals less the body's
        // temporaries plus its number; a declared object's is the function's, each pairing kept.
        const uint32_t declared = function->local_count >= built->temporaries ? function->local_count - built->temporaries : UINT32_MAX;
        const char* field = function->count != built->count ? "count" : declared == UINT32_MAX ? "locals" : function->call_count != built->calls ? "calls" :
                            !built->calls_same ? "calls'-rows" : NULL;
        uint32_t at = 0;
        for (uint32_t e = 0; field == NULL && e < built->count; e += 1)
        {
            const uint8_t mark = built->marks[e];
            const uint64_t ours = built->values[e];
            const uint64_t theirs = function->values[e];
            const uint64_t value = mark == BUSTER_NODE_BODIES_LABEL ? (ours <= body->count ? built->labels[ours] : UINT64_MAX) :
                                   mark == BUSTER_NODE_BODIES_TEMPORARY ? (uint64_t)declared + ours : ours;
            at = e;
            field = built->kinds[e] != function->kinds[e] ? "kind" : built->classes[e] != function->classes[e] ? "class" :
                    mark == BUSTER_NODE_BODIES_OBJECT ? (theirs < declared && node_shadow_same_local(s, ours, theirs) ? NULL : "local") :
                    value != theirs ? (mark == BUSTER_NODE_BODIES_LABEL ? "label" : mark == BUSTER_NODE_BODIES_TEMPORARY ? "temporary" : "value") : NULL;
        }
        // What the lowering's tasks write of a local beside the entries (check_lower_temporary's class, and the address
        // taken of a plain local, which keeps it in its frame): a temporary's class is its entries', and a plain local's
        // address is taken where an entry of the body takes it, and nowhere else.
        if (field == NULL && function->local_count <= built->local_capacity && function->local_classes != NULL && function->local_flags != NULL)
        {
            memset(built->addressed, 0, function->local_count);
            for (uint32_t e = 0; field == NULL && e < built->count; e += 1)
            {
                const uint8_t mark = built->marks[e];
                const uint64_t local = mark == BUSTER_NODE_BODIES_TEMPORARY ? (uint64_t)declared + built->values[e] : mark == BUSTER_NODE_BODIES_OBJECT ? function->values[e] : UINT64_MAX;
                if (local < function->local_count)
                {
                    const bool address = built->kinds[e] == BUSTER_TYPED_LOCAL_ADDRESS;
                    built->addressed[local] |= address;
                    at = e;
                    field = mark == BUSTER_NODE_BODIES_TEMPORARY && !address && function->local_classes[local] != built->classes[e] ? "temporary's-class" : NULL;
                }
            }
            for (uint32_t local = 0; field == NULL && local < function->local_count; local += 1)
            {
                // (A volatile declaration's local is kept in its frame by its declaration, which the lowering's own setup
                // reads: no entry need take its address.)
                const bool plain = function->local_classes[local] != BUSTER_TYPED_CLASS_VOID && (function->local_sizes == NULL || function->local_sizes[local] == 0) &&
                                   (function->local_flags[local] & BUSTER_TYPED_LOCAL_VOLATILE) == 0;
                const bool taken = (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
                shadow->bodies_locals[source] += plain;
                shadow->bodies_locals_addressed[source] += plain && taken;
                at = local;
                field = plain && taken != (built->addressed[local] != 0) ? "address-taken" : NULL;
            }
        }
        shadow->bodies_attempted[source] += 1;
        shadow->bodies_attempted_tokens[source] += tokens;
        shadow->bodies_entries[source] += built->count;
        shadow->bodies_same[source] += field == NULL;
        shadow->bodies_different[source] += field != NULL;
        if (field != NULL && shadow->bodies_listed[source] < node_census_list)
        {
            shadow->bodies_listed[source] += 1;
            printf("node-shadow: bodies%s difference file=%s line=%u field=%s entry=%u (the piece's entries %u, the function's %u; its locals %u, the tapes' temporaries %u)\n", source != 0 ? " before the walk" : "", s->file,
                   node_census_line(s->pp, program->parser, body->opening), field, at, built->count, function->count, function->local_count, built->temporaries);
        }
    }
}

// The shadow of the census's taken expression whose root pop is `p` of the body's `pops`, of root kind `context`, whose
// walk reached `walked` nodes, `nodes` of them evaluated.
static NodeMachineRefusal node_shadow_expression(NodeShadow* shadow, NodeCensusScratch* s, const CheckLowerOwnerPop* pops, uint32_t p, NodeCensusRoot context,
                                                 uint32_t walked, uint32_t nodes)
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
    // The root's context, as its control's task gives it: an address, a discard, or neither; and for a statement's
    // condition its control's sense (a do statement's branches where the condition holds), which C reads where the
    // condition is branches.
    const uint8_t flags = pop->flags & (uint8_t)~BUSTER_CHECK_LOWER_OWNER_TRACKED;
    const uint8_t task_context = flags & (BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS | BUSTER_CHECK_LOWER_TEMPLATE_DISCARD);
    const uint32_t root_control = s->node_controls[root];
    const bool branch_true = context == NODE_CENSUS_ROOT_CONDITION && root_control != BUSTER_CHECK_NONE && program->controls[root_control].kind == CHECK_CONTROL_BRANCH_TRUE;
    const uint8_t root_context = (uint8_t)(task_context | (branch_true ? BUSTER_NODE_MACHINE_ROOT_BRANCH_TRUE : 0u));
    NodeMachineRefusal refusal = walked != records ? NODE_MACHINE_REFUSED_SHAPE : (flags & ~task_context) != 0 ? NODE_MACHINE_REFUSED_ROOT : NODE_MACHINE_TAKEN;
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
        if (s->bodies != NULL)
        {
            // Its tape into its body's entries, while the machine holds it.
            node_bodies_root(s->bodies, s, machine, pop, context);
        }
    }
    // The one sweep, for every expression T typed: C and X took it or refused it.
    if (shadow->fused && ran && (refusal == NODE_MACHINE_TAKEN || refusal == NODE_MACHINE_REFUSED_TEMPLATE || refusal == NODE_MACHINE_REFUSED_ROW))
    {
        node_shadow_fused(shadow, s, refusal, root_context, pop->target, object);
    }
    return refusal;
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
                    if (token < parser->tokens.count && parse_source_index(&parser->tokens, token) < s->pp->output.count && s->pp->output.locations != NULL)
                    {
                        line = pp_presumed(s->pp, s->pp->output.locations[parse_source_index(&parser->tokens, token)], &name, &length);
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

// --front-facts' report: what the kernel ran on at reader time, what it made and left, how it compares with T's records
// there, the words, and its time beside T's on the same expressions.
static void node_shadow_reader_facts_report(const NodeShadow* shadow)
{
    const uint64_t* counts = shadow->reader_facts;
    const uint64_t records = counts[NODE_FACTS_COUNT_RECORDS];
    const uint64_t named = records - counts[NODE_FACTS_COUNT_LITERALS];
    const uint64_t wanted = counts[NODE_FACTS_COUNT_WANTED];
    const uint64_t made = counts[NODE_FACTS_COUNT_MADE];
    printf("node-shadow: reader-time fact words expressions=%" PRIu64 " records=%" PRIu64 " windows=%" PRIu64 " literals=%" PRIu64 " (from a line %" PRIu64 ", made %" PRIu64
           ") records-that-are-no-literal=%" PRIu64 " to-make=%" PRIu64 " given=%" PRIu64 "\n",
           counts[NODE_FACTS_COUNT_EXPRESSIONS], records, counts[NODE_FACTS_COUNT_WINDOWS], counts[NODE_FACTS_COUNT_LITERALS], counts[NODE_FACTS_COUNT_LINES],
           counts[NODE_FACTS_COUNT_LINES_MADE], named, wanted, named - wanted);
    printf("node-shadow: reader-time fact words made=%" PRIu64 " (%.2f%% of the records that are no literal; by a short path or a class's loop %" PRIu64 ", %.2f%%) left=%" PRIu64
           " differences=%" PRIu64 " (over decoys %" PRIu64 ") list-differences=%" PRIu64 " failures=%" PRIu64 "\n",
           made, node_census_share(made, named), counts[NODE_FACTS_COUNT_SHORT], node_census_share(counts[NODE_FACTS_COUNT_SHORT], named), wanted - made,
           counts[NODE_FACTS_COUNT_DIFFERENCES], counts[NODE_FACTS_COUNT_DECOY_DIFFERENCES], counts[NODE_FACTS_COUNT_LIST_DIFFERENCES], counts[NODE_FACTS_COUNT_FAILURES]);
    if (counts[NODE_FACTS_COUNT_TAKEN_RECORDS] != 0)
    {
        // The same share of the records in the expressions the whole machine takes there, which are the kernel's
        // windows' after the pipeline.
        const uint64_t taken_named = counts[NODE_FACTS_COUNT_TAKEN_RECORDS] - counts[NODE_FACTS_COUNT_TAKEN_LITERALS];
        printf("node-shadow: reader-time fact words in the expressions the whole machine takes there: records=%" PRIu64 " no-literal=%" PRIu64 " to-make=%" PRIu64
               " made=%" PRIu64 " (%.2f%% of the records that are no literal)\n",
               counts[NODE_FACTS_COUNT_TAKEN_RECORDS], taken_named, counts[NODE_FACTS_COUNT_TAKEN_WANTED], counts[NODE_FACTS_COUNT_TAKEN_MADE],
               node_census_share(counts[NODE_FACTS_COUNT_TAKEN_MADE], taken_named));
    }
    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
    {
        if (shadow->reader_facts_left[op] != 0)
        {
            printf("node-shadow: reader-time fact words left op=%s records=%" PRIu64 " of %" PRIu64 " (%.2f%%)\n",
                   lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?", shadow->reader_facts_left[op], shadow->reader_facts_wanted[op],
                   node_census_share(shadow->reader_facts_left[op], shadow->reader_facts_wanted[op]));
        }
    }
    printf("node-shadow: reader-time fact words words rows=%" PRIu64 " (%.1f ticks each) declarations'-lines=%" PRIu64 " (%.1f ticks each; %" PRIu64
           " made again; %" PRIu64 " uses asked the long way) member-slots=%" PRIu64 " pointer-conversion-slots=%" PRIu64 "\n",
           counts[NODE_FACTS_COUNT_ROWS], counts[NODE_FACTS_COUNT_ROWS] != 0 ? (double)counts[NODE_FACTS_COUNT_ROW_TICKS] / (double)counts[NODE_FACTS_COUNT_ROWS] : 0.0,
           counts[NODE_FACTS_COUNT_DECLARATION_LINES],
           counts[NODE_FACTS_COUNT_DECLARATION_LINES] != 0 ? (double)counts[NODE_FACTS_COUNT_LINE_TICKS] / (double)counts[NODE_FACTS_COUNT_DECLARATION_LINES] : 0.0,
           counts[NODE_FACTS_COUNT_LINE_VERSIONS], counts[NODE_FACTS_COUNT_LINE_ASKS], counts[NODE_FACTS_COUNT_MEMBER_SLOTS], counts[NODE_FACTS_COUNT_PAIR_SLOTS]);
    if (records != 0)
    {
        // The kernel's ticks a record of its windows, beside T's on the same expressions in the same turn; and the
        // words' making, which the kernel's own ticks leave out.
        const uint64_t again = counts[NODE_FACTS_COUNT_AGAIN_RECORDS];
        printf("node-shadow: reader-time fact words ticks a record: the kernel's first run=%.2f a later run=%.2f; T again=%.2f; the rows' and the lines' making=%.2f; "
               "the windows' making from T's columns=%.2f\n",
               (double)counts[NODE_FACTS_COUNT_FIRST_TICKS] / (double)records, (double)counts[NODE_FACTS_COUNT_TICKS] / (double)records,
               again != 0 ? (double)counts[NODE_FACTS_COUNT_AGAIN_TICKS] / (double)again : 0.0,
               (double)(counts[NODE_FACTS_COUNT_ROW_TICKS] + counts[NODE_FACTS_COUNT_LINE_TICKS]) / (double)records,
               (double)counts[NODE_FACTS_COUNT_APPEND_TICKS] / (double)records);
        printf("node-shadow: reader-time fact words ticks first=%" PRIu64 " later=%" PRIu64 " again=%" PRIu64 " rows=%" PRIu64 " lines=%" PRIu64 " append=%" PRIu64 "\n",
               counts[NODE_FACTS_COUNT_FIRST_TICKS], counts[NODE_FACTS_COUNT_TICKS], counts[NODE_FACTS_COUNT_AGAIN_TICKS], counts[NODE_FACTS_COUNT_ROW_TICKS],
               counts[NODE_FACTS_COUNT_LINE_TICKS], counts[NODE_FACTS_COUNT_APPEND_TICKS]);
    }
    if (counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS] + counts[NODE_FACTS_COUNT_FRONT_SKIPPED] != 0)
    {
        // The windows' inputs from the front's records: what they were made for, how they compare with those made from
        // T's columns, and their ticks: as each expression came (a declaration's line made at its first use among them,
        // and two reads of the counter an expression) and a batch's again in one loop.
        const uint64_t front_records = counts[NODE_FACTS_COUNT_FRONT_RECORDS];
        const uint64_t front_again = counts[NODE_FACTS_COUNT_FRONT_AGAIN_RECORDS];
        printf("node-shadow: reader-time fact words inputs from the front's records: expressions=%" PRIu64 " (%.2f%%) records=%" PRIu64 " (%.2f%%) differences=%" PRIu64
               " expressions-not-given-whole=%" PRIu64 " ticks=%" PRIu64 " (%.2f a record) again-ticks=%" PRIu64 " (%.2f a record of %" PRIu64 ")\n",
               counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS], node_census_share(counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS], counts[NODE_FACTS_COUNT_EXPRESSIONS]), front_records,
               node_census_share(front_records, records), counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES], counts[NODE_FACTS_COUNT_FRONT_SKIPPED],
               counts[NODE_FACTS_COUNT_FRONT_TICKS], front_records != 0 ? (double)counts[NODE_FACTS_COUNT_FRONT_TICKS] / (double)front_records : 0.0,
               counts[NODE_FACTS_COUNT_FRONT_AGAIN_TICKS], front_again != 0 ? (double)counts[NODE_FACTS_COUNT_FRONT_AGAIN_TICKS] / (double)front_again : 0.0, front_again);
        printf("node-shadow: reader-time fact words not given by the front's records: a chunk's end cut it=%" PRIu64 " an operand out of reach=%" PRIu64
               " a literal not decoded again=%" PRIu64 " a call of 255 operands or more=%" PRIu64 "\n",
               shadow->reader_facts_whys[BUSTER_NODE_FACTS_WHY_CUT], shadow->reader_facts_whys[BUSTER_NODE_FACTS_WHY_REACH],
               shadow->reader_facts_whys[BUSTER_NODE_FACTS_WHY_LITERAL], shadow->reader_facts_whys[BUSTER_NODE_FACTS_WHY_CALL]);
    }
    if ((shadow->reader & BUSTER_NODE_READER_WINDOW) != 0)
    {
        // The windows the front wrote, which the kernel reads in place: the expressions whose records are there, how
        // their inputs compare with both other makings', what the kernel made of them there and how that compares with
        // T's records; and what the window costs. The kernel's later run in place. The front's rise: its pass with the
        // window's columns and the chunks' ends that keep rows, over the same pass without them on the same chunks. The
        // names' pass, less the declarations' lines it made, which are the words' making as before (a line a
        // declaration, made once, at whichever making meets its first use).
        const uint64_t window_records = counts[NODE_FACTS_COUNT_WINDOW_RECORDS];
        const uint64_t rise = shadow->front_ticks > shadow->front_plain_ticks ? shadow->front_ticks - shadow->front_plain_ticks : 0;
        const uint64_t names = shadow->front_window_name_ticks > shadow->front_window_line_ticks ? shadow->front_window_name_ticks - shadow->front_window_line_ticks : 0;
        const double kernel = window_records != 0 ? (double)counts[NODE_FACTS_COUNT_WINDOW_KERNEL_TICKS] / (double)window_records : 0.0;
        const double written = window_records != 0 ? (double)rise / (double)window_records : 0.0;
        const double named = window_records != 0 ? (double)names / (double)window_records : 0.0;
        const uint64_t answers = counts[NODE_FACTS_COUNT_ARGUMENT_ANSWERS];
        const double answered = answers != 0 ? (double)counts[NODE_FACTS_COUNT_ARGUMENT_ANSWER_TICKS] / (double)answers : 0.0;
        printf("node-shadow: reader-time fact words windows the front wrote: expressions=%" PRIu64 " (%.2f%%) records=%" PRIu64 " (%.2f%%) differences=%" PRIu64
               " expressions-not-there-in-a-row=%" PRIu64 "\n",
               counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS], node_census_share(counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS], counts[NODE_FACTS_COUNT_EXPRESSIONS]), window_records,
               node_census_share(window_records, records), counts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES], counts[NODE_FACTS_COUNT_WINDOW_SKIPPED]);
        printf("node-shadow: reader-time fact words the kernel in place: to-make=%" PRIu64 " made=%" PRIu64 " left=%" PRIu64 " differences=%" PRIu64 " ticks=%" PRIu64
               " (%.2f a record)\n",
               counts[NODE_FACTS_COUNT_WINDOW_WANTED], counts[NODE_FACTS_COUNT_WINDOW_MADE], counts[NODE_FACTS_COUNT_WINDOW_WANTED] - counts[NODE_FACTS_COUNT_WINDOW_MADE],
               counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES], counts[NODE_FACTS_COUNT_WINDOW_KERNEL_TICKS], kernel);
        printf("node-shadow: reader-time fact words the kernel in place's calls: to-make=%" PRIu64 " made=%" PRIu64 " with-every-argument's-conversion=%" PRIu64
               " (%.2f%% of the calls to make) of-them-plain=%" PRIu64 " left-for-an-argument=%" PRIu64 "; the arguments' rule's answers=%" PRIu64
               " of-them-in-the-timed-run=%" PRIu64 " ticks=%" PRIu64 " (%.1f an answer)\n",
               counts[NODE_FACTS_COUNT_WINDOW_CALLS], counts[NODE_FACTS_COUNT_WINDOW_CALLS_MADE], counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED],
               node_census_share(counts[NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED], counts[NODE_FACTS_COUNT_WINDOW_CALLS]), counts[NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN],
               counts[NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT], answers, counts[NODE_FACTS_COUNT_WINDOW_ANSWERS], counts[NODE_FACTS_COUNT_ARGUMENT_ANSWER_TICKS], answered);
        printf("node-shadow: reader-time fact words the front with the window: ticks=%" PRIu64 " records=%" PRIu64 "; the same front without it, on the same chunks: ticks=%"
               PRIu64 " records=%" PRIu64 " chunks-that-differ=%" PRIu64 "; the rise=%" PRIu64 " (%.2f ticks a record of the windows)\n",
               shadow->front_ticks, shadow->front_records, shadow->front_plain_ticks, shadow->front_plain_records, shadow->front_plain_differences, rise, written);
        printf("node-shadow: reader-time fact words the names' pass: names=%" PRIu64 " ticks=%" PRIu64 ", of which the declarations' lines it made=%" PRIu64 " (%" PRIu64
               " lines); the rest=%" PRIu64 " (%.2f ticks a record of the windows) leaves-its-scalar-path-gives-otherwise=%" PRIu64 "\n",
               shadow->front_window_names, shadow->front_window_name_ticks, shadow->front_window_line_ticks, shadow->front_window_lines, names, named,
               shadow->front_window_name_differences);
        if ((shadow->reader & BUSTER_NODE_READER_WINDOW_TESTS) != 0)
        {
            // The tests on the window: the expressions whose records are there and that T's records' tests ran on.
            const uint64_t tested = counts[NODE_FACTS_COUNT_TESTS_RECORDS];
            const uint64_t ticks = counts[NODE_FACTS_COUNT_TESTS_TICKS];
            const uint64_t bytes = counts[NODE_FACTS_COUNT_TESTS_BYTES_TICKS];
            const uint64_t loop = counts[NODE_FACTS_COUNT_TESTS_LOOP_TICKS];
            const uint64_t rest = counts[NODE_FACTS_COUNT_TESTS_REST_TICKS];
            const uint64_t again = counts[NODE_FACTS_COUNT_TESTS_AGAIN_TICKS];
            const uint64_t own = ticks - bytes - loop - rest;
            const double per = tested != 0 ? 1.0 / (double)tested : 0.0;
            printf("node-shadow: window tests expressions=%" PRIu64 " records=%" PRIu64 " differences=%" PRIu64 " operands-before-the-block-before=%" PRIu64
                   " lines'-bytes-made=%" PRIu64 " records-swept=%" PRIu64 " roots-apart-from-their-operand=%" PRIu64 " chunks=%" PRIu64 "\n",
                   counts[NODE_FACTS_COUNT_TESTS_EXPRESSIONS], tested, counts[NODE_FACTS_COUNT_TESTS_DIFFERENCES], counts[NODE_FACTS_COUNT_TESTS_FAR],
                   counts[NODE_FACTS_COUNT_TESTS_LINES], counts[NODE_FACTS_COUNT_TESTS_SWEPT], counts[NODE_FACTS_COUNT_TESTS_APART], counts[NODE_FACTS_COUNT_TESTS_CHUNKS]);
            printf("node-shadow: window tests ticks=%" PRIu64 " (%.2f a record): the records' bytes=%" PRIu64 " (%.2f) the loop=%" PRIu64 " (%.2f) an expression's part=%"
                   PRIu64 " (%.2f) the tests on T's records=%" PRIu64 " (%.2f); T again for them, not counted=%" PRIu64 " (%.2f)\n",
                   ticks, (double)ticks * per, bytes, (double)bytes * per, loop, (double)loop * per, own, (double)own * per, rest, (double)rest * per, again,
                   (double)again * per);
        }
        if ((shadow->reader & BUSTER_NODE_READER_WINDOW_CONTEXTS) != 0)
        {
            // C on the window: the expressions the kernel placed and the reference's C ran on.
            const uint64_t decided = counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED];
            const uint64_t decided_records = counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED_RECORDS];
            const uint64_t swept = counts[NODE_FACTS_COUNT_CONTEXTS_SWEPT];
            const uint64_t loop = counts[NODE_FACTS_COUNT_CONTEXTS_LOOP_TICKS];
            const uint64_t targets = counts[NODE_FACTS_COUNT_CONTEXTS_TARGET_TICKS];
            const uint64_t part = counts[NODE_FACTS_COUNT_CONTEXTS_PART_TICKS];
            const uint64_t reference = counts[NODE_FACTS_COUNT_CONTEXTS_REFERENCE_TICKS];
            const uint64_t later_loop = counts[NODE_FACTS_COUNT_CONTEXTS_LATER_LOOP_TICKS];
            const uint64_t later_targets = counts[NODE_FACTS_COUNT_CONTEXTS_LATER_TARGET_TICKS];
            const uint64_t later_part = counts[NODE_FACTS_COUNT_CONTEXTS_LATER_PART_TICKS];
            const double per = swept != 0 ? 1.0 / (double)swept : 0.0;
            const double per_decided = decided_records != 0 ? 1.0 / (double)decided_records : 0.0;
            printf("node-shadow: window contexts expressions=%" PRIu64 " records=%" PRIu64 " decided=%" PRIu64 " (%.2f%%) their-records=%" PRIu64 " (%.2f%%) of-them-refused=%"
                   PRIu64 " differences=%" PRIu64 " records-that-differ=%" PRIu64 " strides-read=%" PRIu64 " of-them-asked-of-the-type-table=%" PRIu64
                   " rules-that-read-the-conversions=%" PRIu64 " out-of-a-block's-reach=%" PRIu64 " records-swept=%" PRIu64 "\n",
                   counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS], counts[NODE_FACTS_COUNT_CONTEXTS_RECORDS], decided,
                   node_census_share(decided, counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS]), decided_records,
                   node_census_share(decided_records, counts[NODE_FACTS_COUNT_CONTEXTS_RECORDS]), counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED],
                   counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES], counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES], counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES],
                   counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES_ASKED], counts[NODE_FACTS_COUNT_CONTEXTS_CONVERTED], counts[NODE_FACTS_COUNT_CONTEXTS_FAR], swept);
            // The updates C asks its plan of, and those of them the loop's masks took: another lists its expression.
            const uint64_t updates = counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES];
            const uint64_t updates_taken = counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES_TAKEN];
            printf("node-shadow: window contexts updates=%" PRIu64 " (%.2f%% of the records swept) taken=%" PRIu64 " listed=%" PRIu64 "\n", updates, node_census_share(updates, swept),
                   updates_taken, updates - updates_taken);
            // The records the loop evaluates as branches, and those of them a statement's CONDITION.
            printf("node-shadow: window contexts branched=%" PRIu64 " (%.2f%% of the records swept) of-them-a-condition=%" PRIu64 "\n", counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED],
                   node_census_share(counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED], swept), counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED_ROOTS]);
            // The operators' values, those of them whose consumer's conversion lists their expression, and the commas.
            printf("node-shadow: window contexts valued=%" PRIu64 " (%.2f%% of the records swept) of-them-listed-for-their-target=%" PRIu64 " commas=%" PRIu64 "\n",
                   counts[NODE_FACTS_COUNT_CONTEXTS_VALUED], node_census_share(counts[NODE_FACTS_COUNT_CONTEXTS_VALUED], swept), counts[NODE_FACTS_COUNT_CONTEXTS_SUNK],
                   counts[NODE_FACTS_COUNT_CONTEXTS_COMMAS]);
            printf("node-shadow: window contexts listed, by an expression's first record's reason:");
            for (uint32_t reason = 1; reason < BUSTER_NODE_CONTEXTS_REASONS; reason += 1)
            {
                printf(" %s=%" PRIu64, node_contexts_reason_names[reason], shadow->contexts_listed[reason]);
            }
            printf("\n");
            printf("node-shadow: window contexts ticks=%" PRIu64 " (%.2f a record swept, %.2f a record of the decided): the loop=%" PRIu64 " (%.2f) the targets=%" PRIu64
                   " (%.2f) an expression's part=%" PRIu64 " (%.2f); the reference's C on the decided expressions=%" PRIu64 " (%.2f a record of theirs)\n",
                   loop + targets + part, (double)(loop + targets + part) * per, (double)(loop + targets + part) * per_decided, loop, (double)loop * per, targets,
                   (double)targets * per, part, (double)part * per, reference, (double)reference * per_decided);
            printf("node-shadow: window contexts a second run over the same chunk, not counted: ticks=%" PRIu64 " (%.2f a record swept, %.2f a record of the decided): the loop=%"
                   PRIu64 " (%.2f) the targets=%" PRIu64 " (%.2f) an expression's part=%" PRIu64 " (%.2f)\n",
                   later_loop + later_targets + later_part, (double)(later_loop + later_targets + later_part) * per,
                   (double)(later_loop + later_targets + later_part) * per_decided, later_loop, (double)later_loop * per, later_targets, (double)later_targets * per, later_part,
                   (double)later_part * per);
        }
        if ((shadow->reader & BUSTER_NODE_READER_WINDOW_ROWS) != 0)
        {
            // X's rows on the window: the expressions C takes there.
            const uint64_t expressions = counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS];
            const uint64_t taken = counts[NODE_FACTS_COUNT_ROWS_TAKEN];
            const uint64_t taken_records = counts[NODE_FACTS_COUNT_ROWS_TAKEN_RECORDS];
            const uint64_t swept = counts[NODE_FACTS_COUNT_ROWS_SWEPT];
            const uint64_t reads = counts[NODE_FACTS_COUNT_ROWS_READ_TICKS];
            const uint64_t shapes = counts[NODE_FACTS_COUNT_ROWS_SHAPE_TICKS];
            const uint64_t updates = counts[NODE_FACTS_COUNT_ROWS_UPDATE_TICKS];
            const uint64_t write = counts[NODE_FACTS_COUNT_ROWS_WRITE_TICKS];
            const uint64_t valued = counts[NODE_FACTS_COUNT_ROWS_VALUE_TICKS];
            const uint64_t part = counts[NODE_FACTS_COUNT_ROWS_PART_TICKS];
            const uint64_t reference = counts[NODE_FACTS_COUNT_ROWS_REFERENCE_TICKS];
            const uint64_t later_reads = counts[NODE_FACTS_COUNT_ROWS_LATER_READ_TICKS];
            const uint64_t later_shapes = counts[NODE_FACTS_COUNT_ROWS_LATER_SHAPE_TICKS];
            const uint64_t later_updates = counts[NODE_FACTS_COUNT_ROWS_LATER_UPDATE_TICKS];
            const uint64_t later_write = counts[NODE_FACTS_COUNT_ROWS_LATER_WRITE_TICKS];
            const uint64_t later_valued = counts[NODE_FACTS_COUNT_ROWS_LATER_VALUE_TICKS];
            const uint64_t later_part = counts[NODE_FACTS_COUNT_ROWS_LATER_PART_TICKS];
            const double per = swept != 0 ? 1.0 / (double)swept : 0.0;
            const double per_taken = taken_records != 0 ? 1.0 / (double)taken_records : 0.0;
            printf("node-shadow: window rows expressions=%" PRIu64 " records=%" PRIu64 " taken=%" PRIu64 " (%.2f%%) their-records=%" PRIu64 " (%.2f%%) their-entries=%" PRIu64
                   " differences=%" PRIu64 " not-kept-by-the-reference=%" PRIu64 " second-runs-that-differ=%" PRIu64 " records-swept=%" PRIu64 " entries-written=%" PRIu64 "\n",
                   expressions, counts[NODE_FACTS_COUNT_ROWS_RECORDS], taken, node_census_share(taken, expressions), taken_records,
                   node_census_share(taken_records, counts[NODE_FACTS_COUNT_ROWS_RECORDS]), counts[NODE_FACTS_COUNT_ROWS_ENTRIES], counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES],
                   counts[NODE_FACTS_COUNT_ROWS_UNKEPT], counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES], swept, counts[NODE_FACTS_COUNT_ROWS_WRITTEN]);
            printf("node-shadow: window rows listed, by an expression's first record's reason: update=%" PRIu64 " branches=%" PRIu64 " refused-conversion=%" PRIu64
                   " constant-to-a-floating-class=%" PRIu64 "\n",
                   counts[NODE_FACTS_COUNT_ROWS_LISTED_UPDATE], counts[NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES], counts[NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION],
                   counts[NODE_FACTS_COUNT_ROWS_LISTED_FLOATING]);
            printf("node-shadow: window rows records read a record at a time: a target's word=%" PRIu64 " (%.2f%% of the records swept) a line or a table=%" PRIu64
                   " (%.2f%%) an update's line=%" PRIu64 " (%.2f%%); and operators evaluated as branches, a turn each=%" PRIu64 " (%.2f%%)\n",
                   counts[NODE_FACTS_COUNT_ROWS_WORDS], node_census_share(counts[NODE_FACTS_COUNT_ROWS_WORDS], swept), counts[NODE_FACTS_COUNT_ROWS_LINES],
                   node_census_share(counts[NODE_FACTS_COUNT_ROWS_LINES], swept), counts[NODE_FACTS_COUNT_ROWS_UPDATES],
                   node_census_share(counts[NODE_FACTS_COUNT_ROWS_UPDATES], swept), counts[NODE_FACTS_COUNT_ROWS_BRANCHED],
                   node_census_share(counts[NODE_FACTS_COUNT_ROWS_BRANCHED], swept));
            // The entries' values: the records on each of their lists (BUSTER_NODE_ROWS_LIST_*), and the expressions
            // whose calls' rows, labels and temporaries were numbered.
            const uint64_t* lists = counts + NODE_FACTS_COUNT_ROWS_LIST;
            uint64_t listed = 0;
            for (uint32_t list = 0; list <= NODE_FACTS_COUNT_ROWS_LIST_LAST - NODE_FACTS_COUNT_ROWS_LIST; list += 1)
            {
                listed += lists[list];
            }
            printf("node-shadow: window rows values: tapes that differ in a value alone=%" PRIu64 "; records on a list=%" PRIu64 " (%.3f a record swept): its record's value=%" PRIu64
                   " a store by name's destination's=%" PRIu64 " an integer constant's bits=%" PRIu64 " a stride=%" PRIu64 " the control's label=%" PRIu64 " the rest=%" PRIu64
                   " a number of its expression's after the first=%" PRIu64 "; expressions numbered=%" PRIu64 " (%.2f%% of the taken), the records their labels' walks went over=%"
                   PRIu64 "\n",
                   counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES], listed, (double)listed * per, lists[0], lists[1], lists[2], lists[3], lists[4], lists[5], lists[6],
                   counts[NODE_FACTS_COUNT_ROWS_NUMBERED], node_census_share(counts[NODE_FACTS_COUNT_ROWS_NUMBERED], taken), counts[NODE_FACTS_COUNT_ROWS_NUMBER_STEPS]);
            const uint64_t sum = reads + shapes + updates + write + valued + part;
            const uint64_t later_sum = later_reads + later_shapes + later_updates + later_write + later_valued + later_part;
            printf("node-shadow: window rows ticks=%" PRIu64 " (%.2f a record swept, %.2f a record of the taken): the reads=%" PRIu64 " (%.2f) the shapes=%" PRIu64
                   " (%.2f) the updates and the branches=%" PRIu64 " (%.2f) the rows=%" PRIu64 " (%.2f) the values=%" PRIu64 " (%.2f) an expression's part=%" PRIu64
                   " (%.2f); the reference's X on the taken expressions=%" PRIu64 " (%.2f a record of theirs)\n",
                   sum, (double)sum * per, (double)sum * per_taken, reads, (double)reads * per, shapes, (double)shapes * per, updates, (double)updates * per, write,
                   (double)write * per, valued, (double)valued * per, part, (double)part * per, reference, (double)reference * per_taken);
            printf("node-shadow: window rows a second run over the same chunk, not counted: ticks=%" PRIu64 " (%.2f a record swept, %.2f a record of the taken): the reads=%"
                   PRIu64 " (%.2f) the shapes=%" PRIu64 " (%.2f) the updates and the branches=%" PRIu64 " (%.2f) the rows=%" PRIu64 " (%.2f) the values=%" PRIu64
                   " (%.2f) an expression's part=%" PRIu64 " (%.2f)\n",
                   later_sum, (double)later_sum * per, (double)later_sum * per_taken, later_reads, (double)later_reads * per, later_shapes, (double)later_shapes * per,
                   later_updates, (double)later_updates * per, later_write, (double)later_write * per, later_valued, (double)later_valued * per, later_part,
                   (double)later_part * per);
            // A summary's side facts: what the sweep made at reader time, and the comparison after the pipeline.
            const uint64_t summaries = counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS];
            const uint64_t summary_ticks = counts[NODE_FACTS_COUNT_SUMMARY_TICKS];
            const uint64_t later_summary_ticks = counts[NODE_FACTS_COUNT_SUMMARY_LATER_TICKS];
            const uint64_t chunks = counts[NODE_FACTS_COUNT_TESTS_CHUNKS];
            printf("node-shadow: window summaries expressions=%" PRIu64 " their-references=%" PRIu64 " (%.3f a record swept) with-an-unknown=%" PRIu64 " with-a-stop=%" PRIu64
                   "; records listed for a reference=%" PRIu64 " calls asked a record at a time=%" PRIu64 " (%.3f a record swept) expressions reduced=%" PRIu64
                   " second-runs-that-differ=%" PRIu64 "; operators whose turn lists for a second operand whose truth is known=%" PRIu64 "\n",
                   summaries, counts[NODE_FACTS_COUNT_SUMMARY_REFERENCES], (double)counts[NODE_FACTS_COUNT_SUMMARY_REFERENCES] * per, counts[NODE_FACTS_COUNT_SUMMARY_UNKNOWN],
                   counts[NODE_FACTS_COUNT_SUMMARY_STOPS], counts[NODE_FACTS_COUNT_SUMMARY_LISTED], counts[NODE_FACTS_COUNT_SUMMARY_SUSPECTS],
                   (double)counts[NODE_FACTS_COUNT_SUMMARY_SUSPECTS] * per, counts[NODE_FACTS_COUNT_SUMMARY_REDUCED], counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES],
                   counts[NODE_FACTS_COUNT_SUMMARY_DECIDED]);
            printf("node-shadow: window summaries compared=%" PRIu64 " (not kept: %" PRIu64 ") the program's references=%" PRIu64 " reference-differences=%" PRIu64
                   " fact-differences=%" PRIu64 " (the program's: a stop=%" PRIu64 " an unknown=%" PRIu64 "); folds asked=%" PRIu64 " fold-differences=%" PRIu64
                   " (of the compared: a constant folded=%" PRIu64 "); branches' conditions' truths asked=%" PRIu64 " truth-differences=%" PRIu64
                   " (of the compared: a truth known=%" PRIu64 ")\n",
                   shadow->summary_compared, shadow->summary_unkept, shadow->summary_references, shadow->summary_reference_differences, shadow->summary_fact_differences,
                   shadow->summary_stops, shadow->summary_unknowns, shadow->summary_asked, shadow->summary_fold_differences, shadow->summary_folds,
                   shadow->summary_conditions, shadow->summary_truth_differences, shadow->summary_truths);
            printf("node-shadow: window summaries switch selectors=%" PRIu64 " enums=%" PRIu64 " type-differences=%" PRIu64 "\n",
                   shadow->summary_selectors, shadow->summary_enum_selectors, shadow->summary_selector_differences);
            printf("node-shadow: window summaries ticks=%" PRIu64 " (%.2f a record swept, %.0f a chunk); a second run over the same chunk, not counted: ticks=%" PRIu64
                   " (%.2f a record swept, %.0f a chunk)\n",
                   summary_ticks, (double)summary_ticks * per, chunks != 0 ? (double)summary_ticks / (double)chunks : 0.0, later_summary_ticks,
                   (double)later_summary_ticks * per, chunks != 0 ? (double)later_summary_ticks / (double)chunks : 0.0);
        }
        printf("node-shadow: reader-time fact words chunks' ends: waiting values kept whole=%" PRIu64 " (their records %" PRIu64 ") kept as their place alone, cut=%" PRIu64
               "; the calls' arguments' list at most=%" PRIu64 " (room %u)\n",
               shadow->front_window_kept_values, shadow->front_window_kept_records, shadow->front_window_cut_values, shadow->front_window_arguments_most,
               BUSTER_NODE_FRONT_RECORDS);
        printf("node-shadow: reader-time fact words ticks a record of the windows: the kernel in place=%.2f + the names' pass=%.2f + the front's rise=%.2f = %.2f\n", kernel,
               named, written, kernel + named + written);
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
    for (uint32_t source = 0; shadow->bodies && source < 2; source += 1)
    {
        // The bodies' entries made from their controls and their roots' tapes (node_bodies_begin): the machine's after the
        // pipeline, then those kept before the walk where the unit has them.
        if (source != 0 && shadow->bodies_lowered[source] == 0)
        {
            continue;
        }
        const char* where = source != 0 ? " before the walk" : "";
        if (source == 0 && shadow->statement_controls)
        {
            printf("node-shadow: bodies: read as -c reads a unit, the statement controls' path taken in units=%" PRIu64 " not-taken-in=%" PRIu64 "\n",
                   shadow->statement_controls_units, shadow->statement_controls_without);
        }
        printf("node-shadow: bodies%s lowered=%" PRIu64 " (tokens %" PRIu64 ") the-machine-takes-every-root-of=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%) attempted=%" PRIu64
               " (tokens %" PRIu64 ", %.2f%%; entries %" PRIu64 ") same=%" PRIu64 " different=%" PRIu64 "\n",
               where, shadow->bodies_lowered[source], shadow->bodies_tokens[source], shadow->bodies_whole[source], shadow->bodies_whole_tokens[source],
               node_census_share(shadow->bodies_whole_tokens[source], shadow->bodies_tokens[source]), shadow->bodies_attempted[source], shadow->bodies_attempted_tokens[source],
               node_census_share(shadow->bodies_attempted_tokens[source], shadow->bodies_tokens[source]), shadow->bodies_entries[source], shadow->bodies_same[source],
               shadow->bodies_different[source]);
        printf("node-shadow: bodies%s left out, by the first reason:", where);
        for (uint32_t reason = NODE_BODIES_ATTEMPTED + 1; reason < NODE_BODIES_REASONS; reason += 1)
        {
            if (shadow->bodies_reasons[source][reason] != 0)
            {
                printf(" %s=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%)", node_bodies_reason_names[reason], shadow->bodies_reasons[source][reason],
                       shadow->bodies_reason_tokens[source][reason], node_census_share(shadow->bodies_reason_tokens[source][reason], shadow->bodies_tokens[source]));
            }
        }
        printf("\n");
        if (shadow->bodies_reasons[source][NODE_BODIES_CONTROL] != 0)
        {
            // CheckControlKind's names, in its order.
            static const char* const kinds[] = { "evaluate", "initialize", "vla", "asm", "return", "switch", "case", "end", "stack-save", "allocate", "stack-restore", "label",
                                                 "jump-indirect", "cleanup", "result", "lifetime", "exit", "branch-false", "branch-true", "jump" };
            printf("node-shadow: bodies%s left out for a control of another kind, by the first such control:", where);
            for (uint32_t kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
            {
                if (shadow->bodies_other_kinds[source][kind] != 0)
                {
                    printf(" %s=%" PRIu64, kinds[kind], shadow->bodies_other_kinds[source][kind]);
                }
            }
            printf("\n");
        }
        printf("node-shadow: bodies%s the attempted bodies' plain locals=%" PRIu64 " their address taken=%" PRIu64 "\n", where, shadow->bodies_locals[source],
               shadow->bodies_locals_addressed[source]);
        if (source != 0)
        {
            printf("node-shadow: bodies before the walk the program dropped a range of=%" PRIu64 " attempted-there=%" PRIu64 "\n", shadow->bodies_dropped,
                   shadow->bodies_dropped_attempted);
            printf("node-shadow: bodies before the walk references: the tapes' named objects the program's references of the body=%" PRIu64 " with-an-object-the-program-does-not-list=%"
                   PRIu64 " (objects %" PRIu64 ") with-a-reference-no-tape-names=%" PRIu64 " (references %" PRIu64 ")\n",
                   shadow->bodies_references_same, shadow->bodies_references_more, shadow->bodies_references_extra, shadow->bodies_references_fewer,
                   shadow->bodies_references_missing);
            // The controls' twin from the tokens (node_twin_build).
            printf("node-shadow: controls from the tokens: attempted=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%; controls %" PRIu64 ") same=%" PRIu64 " different=%" PRIu64 " left out:",
                   shadow->twin_reasons[NODE_TWIN_ATTEMPTED], shadow->twin_reason_tokens[NODE_TWIN_ATTEMPTED],
                   node_census_share(shadow->twin_reason_tokens[NODE_TWIN_ATTEMPTED], shadow->bodies_tokens[source]), shadow->twin_controls, shadow->twin_same, shadow->twin_different);
            for (uint32_t reason = NODE_TWIN_ATTEMPTED + 1; reason < NODE_TWIN_REASONS; reason += 1)
            {
                if (shadow->twin_reasons[reason] != 0)
                {
                    printf(" %s=%" PRIu64 " (tokens %" PRIu64 ")", node_twin_reason_names[reason], shadow->twin_reasons[reason], shadow->twin_reason_tokens[reason]);
                }
            }
            printf("\n");
            printf("node-shadow: a producer of these: bodies no test of the tokens refuses, which it reads and sweeps=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%) of them it takes=%" PRIu64
                   " (tokens %" PRIu64 ", %.2f%%)\n",
                   shadow->producer_swept, shadow->producer_swept_tokens, node_census_share(shadow->producer_swept_tokens, shadow->bodies_tokens[source]), shadow->producer_taken,
                   shadow->producer_taken_tokens, node_census_share(shadow->producer_taken_tokens, shadow->bodies_tokens[source]));
            printf("node-shadow: controls from the tokens, the return flow: its silence proved for=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%) of %" PRIu64 " attempted; the program warns of=%" PRIu64
                   " proved-silent-and-warned-of=%" PRIu64 "\n",
                   shadow->twin_silent, shadow->twin_silent_tokens, node_census_share(shadow->twin_silent_tokens, shadow->bodies_tokens[source]),
                   shadow->twin_reasons[NODE_TWIN_ATTEMPTED], shadow->twin_loud, shadow->twin_unsound);
        }
    }
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
            printf("node-shadow: front not run in %" PRIu64 " of %" PRIu64 " units: the reader's coded walk didn't read them whole\n", shadow->front_unread,
                   shadow->front_unread + shadow->front_units);
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
    if ((shadow->front & BUSTER_NODE_FRONT_FLAT) != 0 && shadow->front_units != 0)
    {
        // The flat front: what it visited and made, how it compares with the lean front, and its ticks beside its own
        // compaction's.
        printf("node-shadow: flat front %s tokens=%" PRIu64 " (the lean front's less the %" PRIu64 " that pass values on) left=%" PRIu64 " records=%" PRIu64 " empty-places=%" PRIu64
               " hops=%" PRIu64 " (%.2f a token) past-the-rings'-reach=%" PRIu64 "\n",
               (shadow->front & BUSTER_NODE_FRONT_FLAT_FIRST) != 0 ? "before-the-walk" : "after-the-walk", shadow->flat_tokens, shadow->front_tokens - shadow->flat_tokens,
               shadow->flat_left, shadow->flat_records, shadow->flat_empties, shadow->flat_hops,
               shadow->flat_tokens != 0 ? (double)shadow->flat_hops / (double)shadow->flat_tokens : 0.0, shadow->flat_beyond);
        printf("node-shadow: flat front against the lean front: compared=%" PRIu64 " differences=%" PRIu64 " only-the-flat-front's=%" PRIu64 " only-the-lean-front's=%" PRIu64
               " past-the-records'-ring=%" PRIu64 " distances-past-a-byte-by-empty-places=%" PRIu64 "\n",
               shadow->flat_compared, shadow->flat_differences, shadow->flat_only, shadow->flat_lean_only, shadow->flat_unread, shadow->flat_far);
        printf("node-shadow: flat front ticks=%" PRIu64 " compact-ticks=%" PRIu64 "\n", shadow->flat_ticks, shadow->flat_compact_ticks);
        printf("node-shadow: flat front stage ticks classify=%" PRIu64 " leaves=%" PRIu64 " places=%" PRIu64 " records=%" PRIu64 " empties=%" PRIu64 "\n",
               shadow->flat_stage_ticks[BUSTER_NODE_FLAT_STAGE_CLASSIFY], shadow->flat_stage_ticks[BUSTER_NODE_FLAT_STAGE_LEAVES],
               shadow->flat_stage_ticks[BUSTER_NODE_FLAT_STAGE_PLACES], shadow->flat_stage_ticks[BUSTER_NODE_FLAT_STAGE_RECORDS],
               shadow->flat_stage_ticks[BUSTER_NODE_FLAT_STAGE_EMPTIES]);
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
        printf("node-shadow: reader-time T objects=%" PRIu64 " differences=%" PRIu64 " library-calls=%" PRIu64 " differences=%" PRIu64 " the-program's-own-objects=%" PRIu64
               " objects-a-pragma-labelled=%" PRIu64 "\n",
               shadow->typed_objects, shadow->typed_object_differences, shadow->typed_library_calls, shadow->typed_library_differences, shadow->typed_objects_own,
               shadow->typed_objects_renamed);
    }
    if ((shadow->reader & BUSTER_NODE_READER_FACTS) != 0 && shadow->front_units != 0)
    {
        node_shadow_reader_facts_report(shadow);
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
        if ((shadow->reader & BUSTER_NODE_READER_WINDOW_TESTS) != 0)
        {
            // The tests on the window, in the machine's own form: what they sent to the census's code, and by what.
            printf("node-shadow: own tests expressions=%" PRIu64 " records=%" PRIu64 " expressions-with-a-record-sent=%" PRIu64 " (%.2f%%) records-sent=%" PRIu64
                   " (%.2f%%) walks=%" PRIu64 " (%.2f%%) walks-of-another-count=%" PRIu64 "\n",
                   shadow->own_expressions, shadow->own_records, shadow->own_visited, node_census_share(shadow->own_visited, shadow->own_expressions), shadow->own_visits,
                   node_census_share(shadow->own_visits, shadow->own_records), shadow->own_walks, node_census_share(shadow->own_walks, shadow->own_expressions),
                   shadow->own_count_differences);
            printf("node-shadow: own tests listed tree=%" PRIu64 " (%.2f%%) operators=%" PRIu64 " (%.2f%%: calls %" PRIu64 ", with a constant operand %" PRIu64
                   ", with a third operand %" PRIu64 ")\n",
                   shadow->own_trees, node_census_share(shadow->own_trees, shadow->own_records), shadow->own_operators,
                   node_census_share(shadow->own_operators, shadow->own_records), shadow->own_calls, shadow->own_constants, shadow->own_thirds);
            printf("node-shadow: own tests operators-left-out-for-quiet-constants=%" PRIu64 " checked=%" PRIu64 " of-them-with-a-test=%" PRIu64
                   " constants'-said-values-checked=%" PRIu64 " not-their-payloads'=%" PRIu64 "\n",
                   shadow->own_quiet, shadow->own_quiet_checked, shadow->own_quiet_wrong, shadow->own_values_checked, shadow->own_values_wrong);
            printf("node-shadow: own tests calls-left-out=%" PRIu64 " checked=%" PRIu64 " of-them-with-a-test=%" PRIu64 " of-them-with-a-loud-argument=%" PRIu64
                   " casts-over-one=%" PRIu64 " callees-asked-for-their-result=%" PRIu64 " initializers-asked-for-a-null-constant=%" PRIu64 "\n",
                   shadow->own_quiet_calls, shadow->own_quiet_calls_checked, shadow->own_quiet_calls_wrong, shadow->own_quiet_calls_loud, shadow->own_result_casts,
                   shadow->own_result_asks, shadow->own_root_nulls);
            printf("node-shadow: own tests casts-left-out=%" PRIu64 " checked=%" PRIu64 " of-them-with-a-test=%" PRIu64 "\n", shadow->own_quiet_casts,
                   shadow->own_quiet_casts_checked, shadow->own_quiet_casts_wrong);
            printf("node-shadow: own tests pointers'-ordered-comparisons-left-out=%" PRIu64 " checked=%" PRIu64 " of-them-with-a-test=%" PRIu64 "\n", shadow->own_quiet_orders,
                   shadow->own_quiet_orders_checked, shadow->own_quiet_orders_wrong);
            printf("node-shadow: own tests tree's-tests'-records-no-listed-operator-has checked=%" PRIu64 " of-them-with-a-test=%" PRIu64
                   " lists'-words-with-a-bit-past-their-expression=%" PRIu64 "\n",
                   shadow->own_tree_checked, shadow->own_tree_wrong, shadow->own_lists_past);
            printf("node-shadow: own tests expressions-with-nothing-listed=%" PRIu64 " (%.2f%%) checked=%" PRIu64 " of-them-with-another-verdict=%" PRIu64 "\n",
                   shadow->own_direct, node_census_share(shadow->own_direct, shadow->own_expressions), shadow->own_direct_checked, shadow->own_direct_wrong);
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
    if (shadow->alone_bodies != 0 || shadow->alone_bodies_left != 0)
    {
        // The machine with no node of the walk's.
        // Its ticks in place, one run of it: the compaction, the entries, the front and the records' copy (all of it less
        // the machine's two runs), and the machine's first run on the chunks' roots (T, the tests, C and X).
        const uint64_t reading = shadow->alone_ticks > shadow->alone_machine_ticks ? shadow->alone_ticks - shadow->alone_machine_ticks : 0;
        printf("node-shadow: alone bodies=%" PRIu64 " left=%" PRIu64 " postorder-positions=%" PRIu64 " records=%" PRIu64 " roots=%" PRIu64 " taken=%" PRIu64
               " ticks: its-reading=%" PRIu64 " (the compaction and the entries %" PRIu64 ", the front %" PRIu64 ", its graph's writing %" PRIu64
               ") the-machine's-first-run=%" PRIu64 " a-position=%.1f\n",
               shadow->alone_bodies, shadow->alone_bodies_left, shadow->alone_positions, shadow->alone_records, shadow->alone_roots, shadow->alone_taken, reading,
               shadow->alone_prepare_ticks, shadow->alone_front_ticks, shadow->alone_write_ticks, shadow->alone_sweep_ticks,
               shadow->alone_positions != 0 ? (double)(reading + shadow->alone_sweep_ticks) / (double)shadow->alone_positions : 0.0);
        if (shadow->alone_step_reads != 0)
        {
            // The first run by sweep: each sweep's ticks, with the clock's reads that bound it.
            printf("node-shadow: alone the machine's first run by sweep: T=%" PRIu64 " the-tests=%" PRIu64 " C=%" PRIu64 " X=%" PRIu64 " (%" PRIu64
                   " of the clock's reads among them) the-rest-of-the-run=%" PRIu64 "\n",
                   shadow->alone_step_ticks[0], shadow->alone_step_ticks[1], shadow->alone_step_ticks[2], shadow->alone_step_ticks[3], shadow->alone_step_reads,
                   shadow->alone_sweep_ticks - (shadow->alone_step_ticks[0] + shadow->alone_step_ticks[1] + shadow->alone_step_ticks[2] + shadow->alone_step_ticks[3]));
        }
        printf("node-shadow: alone compared=%" PRIu64 " differences=%" PRIu64, shadow->alone_compared, shadow->alone_differences);
        for (uint32_t field = 0; field < BUSTER_NODE_ALONE_FIELDS; field += 1)
        {
            if (shadow->alone_fields[field] != 0)
            {
                printf(" %s=%" PRIu64, node_alone_field_names[field], shadow->alone_fields[field]);
            }
        }
        printf(" roots-of-the-walk's-nodes-alone=%" PRIu64 " (outside a body %" PRIu64 ") roots-of-its-own-alone=%" PRIu64 " taken-before-an-event-of-the-walk's=%" PRIu64
               " taken-with-a-token-the-walk's-actions-left=%" PRIu64 " (of %" PRIu64 " checked; the bodies hold %" PRIu64 " such tokens)\n",
               shadow->alone_walk_only, shadow->alone_walk_only_outside, shadow->alone_own_only, shadow->alone_events_after, shadow->alone_taken_declined,
               shadow->alone_taken_checked, shadow->alone_declined_tokens);
        static const char* const verdict_names[BUSTER_NODE_ALONE_BODY_VERDICTS] = { "whole", "a-token-left", "a-root-not-run", "T", "the-tests", "C-or-X", "no-room" };
        printf("node-shadow: alone lowered-bodies' verdicts before the walk (their tokens %" PRIu64 "):", shadow->alone_body_tokens);
        for (uint32_t verdict = 0; verdict < BUSTER_NODE_ALONE_BODY_VERDICTS; verdict += 1)
        {
            if (shadow->alone_verdicts[verdict] != 0)
            {
                printf(" %s=%" PRIu64 " (tokens %" PRIu64 ", %.2f%%)", verdict_names[verdict], shadow->alone_verdicts[verdict], shadow->alone_verdict_tokens[verdict],
                       shadow->alone_body_tokens != 0 ? 100.0 * (double)shadow->alone_verdict_tokens[verdict] / (double)shadow->alone_body_tokens : 0.0);
            }
        }
        printf(" not-read=%" PRIu64 " waiting-values-lost=%" PRIu64 "\n", shadow->alone_bodies_unread, shadow->alone_resets);
        for (uint32_t verdict = 0; verdict < BUSTER_NODE_ALONE_BODY_VERDICTS; verdict += 1)
        {
            if (shadow->alone_verdicts[verdict] != 0)
            {
                // A verdict's bodies' ticks, and what a scan of the tokens alone would have refused.
                printf("node-shadow: alone verdict=%s bodies=%" PRIu64 " tokens=%" PRIu64 " ticks: reading=%" PRIu64 " the-machine's-first-run=%" PRIu64
                       " by-the-chunk-that-stops-them=%" PRIu64 " a-scan-of-the-tokens-refuses: bodies=%" PRIu64 " tokens=%" PRIu64 "\n",
                       verdict_names[verdict], shadow->alone_verdicts[verdict], shadow->alone_verdict_tokens[verdict], shadow->alone_verdict_reading[verdict],
                       shadow->alone_verdict_machine[verdict], shadow->alone_verdict_before[verdict], shadow->alone_verdict_scanned[verdict],
                       shadow->alone_verdict_scanned_tokens[verdict]);
            }
        }
        static const char* const pair_names[5] = { "evaluate", "branch", "return", "initialize", "another" };
        printf("node-shadow: alone roots against the program's controls with an expression, in the lowered bodies:");
        for (uint32_t slot = 0; slot < 5; slot += 1)
        {
            printf(" %s=%" PRIu64 " (one root %" PRIu64 ", none %" PRIu64 ", several %" PRIu64 "; its control's token the control's %" PRIu64 ")", pair_names[slot],
                   shadow->alone_pair_controls[slot], shadow->alone_pair_one[slot], shadow->alone_pair_none[slot], shadow->alone_pair_several[slot], shadow->alone_pair_token[slot]);
        }
        printf(" roots-it-ran-on-there=%" PRIu64 " of-them-some-control's=%" PRIu64 "\n", shadow->alone_pair_roots, shadow->alone_pair_claimed);
        printf("node-shadow: alone a scan of every body's tokens for a root the front would lose: tokens-read=%" PRIu64 " ticks=%" PRIu64 " a-token-read=%.2f\n",
               shadow->alone_scan_tokens, shadow->alone_scan_ticks, shadow->alone_scan_tokens != 0 ? (double)shadow->alone_scan_ticks / (double)shadow->alone_scan_tokens : 0.0);
        printf("node-shadow: alone bodies whole for it and for the walk's nodes' machine=%" PRIu64 " (tokens %" PRIu64 ") for-it-alone=%" PRIu64 " for-the-walk's-nodes'-alone=%" PRIu64
               " (and with a block's static or extern declaration %" PRIu64 ")\n",
               shadow->alone_whole_both, shadow->alone_whole_both_tokens, shadow->alone_whole_own, shadow->alone_whole_walk, shadow->alone_whole_walk_static);
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
// The twin's controls of `body` against the program's, where the machine took the body whole before the walk
// (`verdict`): their count and each one's kind, token, target and whether it has an expression. `tokens` are the body's.
static void node_twin_compare(NodeTwin* twin, NodeShadow* shadow, const NodeCensusScratch* s, const CheckProgramBody* body, uint8_t verdict, uint64_t tokens)
{
    const CheckProgram* program = s->program;
    const uint32_t closing = program->parser->tree.parents[body->opening];
    bool dropped = false;
    for (uint32_t r = 0; r < program->dropped_range_count && !dropped; r += 1)
    {
        const uint32_t low = (uint32_t)(program->dropped_ranges[r] >> 32);
        dropped = low > body->opening && low < closing;
    }
    twin->reason = verdict != BUSTER_NODE_ALONE_BODY_WHOLE ? NODE_TWIN_UNWHOLE : dropped ? NODE_TWIN_DROPPED : NODE_TWIN_ATTEMPTED;
    const bool made = twin->reason == NODE_TWIN_ATTEMPTED && node_twin_build(twin, body->opening);
    shadow->twin_reasons[twin->reason] += 1;
    shadow->twin_reason_tokens[twin->reason] += tokens;
    if (made)
    {
        const char* field = twin->count != body->count ? "count" : NULL;
        uint32_t at = 0;
        for (uint32_t i = 0; field == NULL && i < twin->count; i += 1)
        {
            const CheckControl* control = program->controls + body->first + i;
            const NodeTwinControl* entry = twin->entries + i;
            const bool targeted = control->kind >= CHECK_CONTROL_BRANCH_FALSE || control->kind == CHECK_CONTROL_RETURN;
            const bool noded = control->kind == CHECK_CONTROL_EVALUATE || control->kind >= CHECK_CONTROL_BRANCH_FALSE || control->kind == CHECK_CONTROL_RETURN;
            at = i;
            field = (CheckControlKind)entry->kind != control->kind ? "kind" : entry->token != control->token ? "token" : targeted && entry->target != control->target ? "target" :
                    noded && entry->valued != (control->node != BUSTER_CHECK_NONE) ? "expression" : NULL;
        }
        shadow->twin_controls += twin->count;
        shadow->twin_same += field == NULL;
        shadow->twin_different += field != NULL;
        // The return flow's silence by the twin's controls, against the program's own verdict of the body.
        const bool silent = node_twin_silent(twin, body->declaration, body->is_main);
        shadow->twin_silent += silent;
        shadow->twin_silent_tokens += silent ? tokens : 0;
        shadow->twin_loud += body->return_warning != 0;
        shadow->twin_unsound += silent && body->return_warning != 0;
        if (field != NULL && shadow->twin_listed < node_census_list)
        {
            shadow->twin_listed += 1;
            printf("node-shadow: controls from the tokens difference file=%s line=%u field=%s control=%u (the twin's controls %u, the program's %u; its kind %u, the program's %u; its token %u, "
                   "the program's %u)\n",
                   s->file, node_census_line(s->pp, program->parser, body->opening), field, at, twin->count, body->count, at < twin->count ? twin->entries[at].kind : 0u,
                   at < body->count ? (unsigned)program->controls[body->first + at].kind : 0u, at < twin->count ? twin->entries[at].token : 0u,
                   at < body->count ? program->controls[body->first + at].token : 0u);
        }
    }
}

// The same body's entries from the tapes the machine with no node of the walk's kept before the walk (`fronted`'s, its
// verdict of the body `verdict`): each control with an expression finds its root by its own token, and the root's kept
// tape, labels, calls, temporaries and facts stand where the machine's after the pipeline stood. The controls are the
// program's. A body with a condition whose root's value is any constant is refused: the program may fold it, dropping
// an arm and the locals declared there. `tokens` are the body's.
static void node_bodies_kept(NodeBodies* built, NodeShadow* shadow, NodeCensusScratch* s, const CheckProgramBody* body, const NodeFrontShadow* fronted, uint8_t verdict,
                             uint64_t tokens)
{
    const CheckProgram* program = s->program;
    const Tier0Function* function = s->function;
    const NodeFrontTypes* kept = fronted->alone_types;
    const uint32_t token_count = program->parser->tokens.count;
    node_bodies_begin(built, s, body);
    built->whole = verdict == BUSTER_NODE_ALONE_BODY_WHOLE;
    built->named_count = 0;
    // Whether the program dropped a range of the body: a constant `if`'s.
    const uint32_t closing = program->parser->tree.parents[body->opening];
    bool dropped = false;
    for (uint32_t r = 0; r < program->dropped_range_count && !dropped; r += 1)
    {
        const uint32_t low = (uint32_t)(program->dropped_ranges[r] >> 32);
        dropped = low > body->opening && low < closing;
    }
    // An `if` whose condition's root, kept before the walk at its closing token, is a constant: by the tokens alone, since
    // the program's controls have no such condition where it folded it.
    bool constant = false;
    for (uint32_t token = body->opening + 1; built->whole && !constant && token < closing && token < token_count; token += 1)
    {
        const uint32_t close = program->parser->tree.parents[token];
        if (program->parser->tokens.kinds[token] == PARSE_KIND_IF && close < token_count && program->parser->tree.roles[close] == PARSE_ROLE_CLOSE_CONDITION)
        {
            const uint32_t slot = fronted->alone_control_roots[close];
            constant = slot != 0 && slot != UINT32_MAX && slot - 1 < kept->node_capacity && kept->side_label_firsts[slot - 1] != UINT32_MAX &&
                       (kept->side_facts[slot - 1] & BUSTER_NODE_FRONT_SIDE_CONSTANT) != 0;
        }
    }
    for (uint32_t i = 0; built->whole && built->reason == NODE_BODIES_ATTEMPTED && built->placed && i < body->count; i += 1)
    {
        const CheckControl* control = program->controls + body->first + i;
        const CheckControlKind kind = control->kind;
        const bool expected = kind == CHECK_CONTROL_EVALUATE || kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE || kind == CHECK_CONTROL_INITIALIZE ||
                              (kind == CHECK_CONTROL_RETURN && control->node != BUSTER_CHECK_NONE);
        if (expected)
        {
            const uint32_t slot = control->token < token_count ? fronted->alone_control_roots[control->token] : 0;
            const uint32_t root = slot - 1;
            const bool found = slot != 0 && slot != UINT32_MAX && root < kept->node_capacity && (kept->refusals[root] & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 &&
                               kept->sweep_refusals[root] == NODE_MACHINE_TAKEN && kept->tape_counts[root] != UINT32_MAX && kept->side_label_firsts[root] != UINT32_MAX;
            built->placed = found;
            built->unplaced = NODE_BODIES_UNKEPT;
            if (found)
            {
                // Its calls' rows at the body's: each one's callee, by the program's numbering, its arguments' count and
                // classes, whether it is variadic, and no result object.
                const uint32_t calls = kept->side_call_counts[root];
                bool same = built->calls <= function->call_count && calls <= function->call_count - built->calls;
                for (uint32_t k = 0; same && k < calls; k += 1)
                {
                    const uint64_t row = (uint64_t)kept->side_call_firsts[root] + k;
                    const uint32_t at = built->calls + k;
                    const uint32_t callee = kept->side_call_targets[row];
                    const uint32_t count = kept->side_call_argument_counts[row];
                    const uint32_t first = function->call_first[at];
                    same = function->call_targets[at] == (callee != 0 && callee < program->object_count ? program->object_symbols[callee] : 0) &&
                           function->call_argument_counts[at] == count && (function->call_variadic[at] != 0) == (kept->side_call_variadic[row] != 0) &&
                           (function->call_result_objects == NULL || function->call_result_objects[at] == 0) && first <= function->argument_count &&
                           count <= function->argument_count - first;
                    for (uint32_t a = 0; same && a < count; a += 1)
                    {
                        same = function->argument_classes[first + a] == kept->side_argument_classes[kept->side_call_argument_firsts[row] + a] &&
                               (function->argument_sizes == NULL || function->argument_sizes[first + a] == 0);
                    }
                }
                built->calls_same = built->calls_same && same;
                const uint64_t tape_first = kept->tape_firsts[root];
                // The objects it names: a global's entry's, and each call's callee's.
                for (uint32_t e = 0; e < kept->tape_counts[root]; e += 1)
                {
                    if (node_shadow_global_kind(kept->tape_kinds[tape_first + e]) && built->named_count < built->named_capacity)
                    {
                        built->named[built->named_count++] = (uint32_t)kept->tape_values[tape_first + e];
                    }
                }
                for (uint32_t k = 0; k < calls; k += 1)
                {
                    const uint32_t callee = kept->side_call_targets[(uint64_t)kept->side_call_firsts[root] + k];
                    if (callee != 0 && built->named_count < built->named_capacity)
                    {
                        built->named[built->named_count++] = callee;
                    }
                }
                const NodeBodiesTape tape = {
                    .kinds = kept->tape_kinds + tape_first,
                    .classes = kept->tape_classes + tape_first,
                    .values = kept->tape_values + tape_first,
                    .labels = kept->side_labels + kept->side_label_firsts[root],
                    .entries = kept->tape_counts[root],
                    .label_count = kept->side_label_counts[root],
                    .temporaries = kept->side_temporaries[root],
                    .calls = calls,
                    .branched = (kept->side_facts[root] & BUSTER_NODE_FRONT_SIDE_BRANCHED) != 0,
                    .objects = true,
                };
                node_bodies_splice(built, s, i, &tape);
            }
        }
    }
    if (constant)
    {
        built->placed = false;
        built->unplaced = NODE_BODIES_CONSTANT;
    }
    else if (built->whole && s->twin != NULL)
    {
        // A statement the program may leave out for no control reaching it (#2987), by the tokens and the kept roots'
        // facts: the program's controls have none of it where it did.
        node_twin_children(s->twin, body->opening);
        if (node_twin_unreached(s->twin))
        {
            built->placed = false;
            built->unplaced = NODE_BODIES_REACH;
        }
    }
    const uint64_t before = shadow->bodies_attempted[1];
    node_bodies_end(built, shadow, s, tokens, 1);
    shadow->bodies_dropped += dropped;
    shadow->bodies_dropped_attempted += dropped && shadow->bodies_attempted[1] != before;
    if (shadow->bodies_attempted[1] != before && built->named_count < built->named_capacity)
    {
        // Its tapes' named objects against the program's references of the body (CheckProgram.references, sorted): each
        // set's objects the other lacks.
        for (uint32_t i = 1; i < built->named_count; i += 1)
        {
            const uint32_t object = built->named[i];
            uint32_t at = i;
            while (at != 0 && built->named[at - 1] > object)
            {
                built->named[at] = built->named[at - 1];
                at -= 1;
            }
            built->named[at] = object;
        }
        uint32_t extra = 0;
        uint32_t missing = 0;
        uint32_t first_extra = 0;
        uint32_t first_missing = 0;
        uint32_t n = 0;
        uint32_t r = 0;
        while (n < built->named_count || r < body->reference_count)
        {
            const uint32_t named = n < built->named_count ? built->named[n] : UINT32_MAX;
            const uint32_t referenced = r < body->reference_count ? program->references[body->reference_first + r] : UINT32_MAX;
            if (named == referenced)
            {
                while (n < built->named_count && built->named[n] == named)
                {
                    n += 1;
                }
                r += 1;
            }
            else if (named < referenced)
            {
                first_extra = extra == 0 ? named : first_extra;
                extra += 1;
                while (n < built->named_count && built->named[n] == named)
                {
                    n += 1;
                }
            }
            else
            {
                first_missing = missing == 0 ? referenced : first_missing;
                missing += 1;
                r += 1;
            }
        }
        shadow->bodies_references_same += extra == 0 && missing == 0;
        shadow->bodies_references_more += extra != 0;
        shadow->bodies_references_fewer += missing != 0;
        shadow->bodies_references_extra += extra;
        shadow->bodies_references_missing += missing;
        if ((extra != 0 || missing != 0) && shadow->bodies_references_listed < node_census_list)
        {
            shadow->bodies_references_listed += 1;
            printf("node-shadow: bodies before the walk references file=%s line=%u the-tapes-name=%u the-program-lists=%u objects-the-program-does-not-list=%u (first %u) "
                   "references-no-tape-names=%u (first %u)\n",
                   s->file, node_census_line(s->pp, program->parser, body->opening), built->named_count, body->reference_count, extra, first_extra, missing, first_missing);
        }
    }
}

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
    // Whether every root the lowering popped is taken by the machine at reader time, on the walk's nodes: what the
    // machine with no node of the walk's said of the body before the walk is compared with (below).
    bool reader_whole = true;
    if (s->bodies != NULL)
    {
        node_bodies_begin(s->bodies, s, body);
    }
    for (uint32_t p = 0; p < count; p += 1)
    {
        if (s->root_pop[p] == p)
        {
            const NodeCensusRoot context = node_census_root(s, pops + p);
            uint32_t walked = 0;
            NodeFrontTypes* typed = program->input->node_front != NULL ? program->input->node_front->types : NULL;
            if (typed != NULL)
            {
                const uint32_t root_node = pops[p].node;
                const bool ran = root_node < typed->node_capacity && typed->counts[root_node] != 0;
                const uint8_t flags = ran ? typed->refusals[root_node] : 0;
                reader_whole = reader_whole && ran && (flags & BUSTER_NODE_FRONT_TYPED_REFUSAL) == NODE_MACHINE_TAKEN && (flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0 &&
                               typed->tests != NULL && typed->tests[root_node] == 0 && (flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 && typed->sweep_refusals != NULL &&
                               typed->sweep_refusals[root_node] == NODE_MACHINE_TAKEN;
            }
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
            const NodeMachineRefusal refusal = census->shadow != NULL && tests == 0 ? node_shadow_expression(census->shadow, s, pops, p, context, walked, s->expression_nodes[p]) :
                                                                                       NODE_MACHINE_REFUSED_ROOT;
            if (s->bodies != NULL)
            {
                s->bodies->whole = s->bodies->whole && tests == 0 && refusal == NODE_MACHINE_TAKEN;
            }
            whole = whole && tests == 0;
            body_nodes += s->expression_nodes[p];
        }
    }
    if (s->bodies != NULL)
    {
        const uint32_t end = program->parser->tree.parents[body->opening];
        node_bodies_end(s->bodies, census->shadow, s, end > body->opening ? (uint64_t)end - body->opening + 1 : 0, 0);
    }
    for (uint32_t p = 0; p < count; p += 1)
    {
        s->node_pop[pops[p].node] = none;
    }
    node_census_mark_controls(s, body, false);
    const NodeFrontShadow* fronted = program->input->node_front;
    if (census->shadow != NULL && fronted != NULL && fronted->alone_types != NULL && fronted->alone_body_verdicts != NULL)
    {
        // The body's verdict of the machine with no node of the walk's, by its opening brace among the bodies it read.
        NodeShadow* shadow = census->shadow;
        const uint32_t closing = program->parser->tree.parents[body->opening];
        const uint64_t tokens = closing > body->opening ? (uint64_t)closing - body->opening + 1 : 0;
        uint32_t low = 0;
        uint32_t high = fronted->alone_body_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (fronted->alone_body_openings[middle] < body->opening)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        shadow->alone_body_tokens += tokens;
        if (low < fronted->alone_body_count && fronted->alone_body_openings[low] == body->opening)
        {
            const uint8_t verdict = fronted->alone_body_verdicts[low] < BUSTER_NODE_ALONE_BODY_VERDICTS ? fronted->alone_body_verdicts[low] : BUSTER_NODE_ALONE_BODY_ROOM;
            const bool own = verdict == BUSTER_NODE_ALONE_BODY_WHOLE;
            shadow->alone_verdicts[verdict] += 1;
            shadow->alone_verdict_tokens[verdict] += tokens;
            shadow->alone_verdict_reading[verdict] += fronted->alone_body_reading[low];
            shadow->alone_verdict_machine[verdict] += fronted->alone_body_machine[low];
            shadow->alone_verdict_before[verdict] += fronted->alone_body_before[low];
            shadow->alone_verdict_scanned[verdict] += fronted->alone_body_scanned[low] != 0;
            shadow->alone_verdict_scanned_tokens[verdict] += fronted->alone_body_scanned[low] != 0 ? tokens : 0;
            shadow->alone_whole_both += own && reader_whole;
            shadow->alone_whole_both_tokens += own && reader_whole ? tokens : 0;
            shadow->alone_whole_own += own && !reader_whole;
            bool statics = false;
            for (uint32_t token = body->opening; !own && reader_whole && !statics && token < closing; token += 1)
            {
                const uint32_t name = program->parser->tokens.names[token];
                statics = program->parser->tree.roles[token] == PARSE_ROLE_SPECIFIER && (name == C_NAME_STATIC || name == C_NAME_EXTERN);
            }
            shadow->alone_whole_walk += !own && reader_whole && !statics;
            shadow->alone_whole_walk_static += !own && reader_whole && statics;
            if (s->twin != NULL && fronted->alone_control_roots != NULL)
            {
                s->twin->parser = program->parser;
                s->twin->input = program->input;
                s->twin->types = program->types;
                s->twin->kept = fronted->alone_types;
                s->twin->roots = fronted->alone_control_roots;
                const uint64_t kept_before = shadow->bodies_same[1];
                const uint64_t twin_before = shadow->twin_same;
                const uint64_t silent_before = shadow->twin_silent;
                if (s->bodies_kept != NULL && fronted->alone_types->side_label_firsts != NULL)
                {
                    node_bodies_kept(s->bodies_kept, shadow, s, body, fronted, verdict, tokens);
                }
                node_twin_compare(s->twin, shadow, s, body, verdict, tokens);
                // A producer's two sets. What it reads and sweeps: a body no test of the tokens alone refuses before any
                // work (the scan, the twin's gates, the body's own reasons, which the tokens and the declarations give).
                // What it takes of them: a body both comparisons attempted and found the same, its return flow proved
                // silent.
                s->twin->reason = NODE_TWIN_ATTEMPTED;
                node_twin_gates(s->twin, body->opening);
                const bool swept = fronted->alone_body_scanned[low] == 0 && s->twin->reason == NODE_TWIN_ATTEMPTED &&
                                   (s->bodies_kept == NULL || s->bodies_kept->reason == NODE_BODIES_ATTEMPTED);
                const bool taken = swept && shadow->bodies_same[1] != kept_before && shadow->twin_same != twin_before && shadow->twin_silent != silent_before;
                shadow->producer_swept += swept;
                shadow->producer_swept_tokens += swept ? tokens : 0;
                shadow->producer_taken += taken;
                shadow->producer_taken_tokens += taken ? tokens : 0;
            }
            if (fronted->alone_root_tokens != NULL && fronted->alone_roots != NULL)
            {
                // Its roots against the body's controls: each control's expression's root's token, and the roots there.
                const CheckGraph* graph = &program->input->graph;
                const NodeFrontTypes* alone = fronted->alone_types;
                const uint32_t token_count = program->parser->tokens.count;
                for (uint32_t c = body->first; c < body->first + body->count; c += 1)
                {
                    const CheckControl* control = program->controls + c;
                    const CheckControlKind kind = control->kind;
                    const uint32_t slot = kind == CHECK_CONTROL_EVALUATE ? 0 : kind == CHECK_CONTROL_BRANCH_FALSE || kind == CHECK_CONTROL_BRANCH_TRUE ? 1 :
                                          kind == CHECK_CONTROL_RETURN ? 2 : kind == CHECK_CONTROL_INITIALIZE ? 3 : 4;
                    const CheckInitializer* plan = kind == CHECK_CONTROL_INITIALIZE && program->initializers != NULL && control->target < program->initializers->count ?
                                                   program->initializers->plans + control->target : NULL;
                    const uint32_t parts = plan != NULL ? plan->count : 1;
                    for (uint32_t part = 0; part < parts; part += 1)
                    {
                        const uint32_t node = plan != NULL ? (plan->parts[part].kind == CHECK_INIT_VALUE ? plan->parts[part].node : BUSTER_CHECK_NONE) :
                                              check_control_expression_node(control);
                        if (node < graph->count && graph->tokens[node] < token_count)
                        {
                            uint32_t found = 0;
                            uint32_t ran = 0;
                            uint32_t root = BUSTER_CHECK_NONE;
                            for (uint32_t link = fronted->alone_roots[graph->tokens[node]]; link != 0; link = fronted->alone_chains[link - 1])
                            {
                                found += 1;
                                const bool run = link - 1 < alone->node_capacity && alone->counts[link - 1] != 0;
                                ran += run;
                                root = run ? link - 1 : root;
                            }
                            const bool one = ran == 1;
                            const bool tokened = one && root < fronted->node_capacity && fronted->alone_root_tokens[root] == control->token;
                            shadow->alone_pair_controls[slot] += 1;
                            shadow->alone_pair_one[slot] += one;
                            shadow->alone_pair_none[slot] += ran == 0;
                            shadow->alone_pair_several[slot] += ran > 1;
                            shadow->alone_pair_token[slot] += tokened;
                            shadow->alone_pair_claimed += ran;
                            if ((!one || !tokened) && verdict == BUSTER_NODE_ALONE_BODY_WHOLE && shadow->alone_pair_listed < node_census_list)
                            {
                                shadow->alone_pair_listed += 1;
                                printf("node-shadow: alone a control of a whole body without its one root file=%s line=%u kind=%u roots=%u ran=%u its-token=%u the-control's=%u\n", s->file,
                                       node_census_line(s->pp, program->parser, control->token), (unsigned)kind, found, ran,
                                       root < fronted->node_capacity ? fronted->alone_root_tokens[root] : BUSTER_CHECK_NONE, control->token);
                            }
                        }
                    }
                }
                for (uint32_t token = body->opening; token < closing && token < token_count; token += 1)
                {
                    for (uint32_t link = fronted->alone_roots[token]; link != 0; link = fronted->alone_chains[link - 1])
                    {
                        shadow->alone_pair_roots += link - 1 < alone->node_capacity && alone->counts[link - 1] != 0;
                    }
                }
            }
            const bool unscanned = verdict == BUSTER_NODE_ALONE_BODY_LEFT && fronted->alone_body_scanned[low] == 0;
            const bool misscanned = verdict != BUSTER_NODE_ALONE_BODY_LEFT && fronted->alone_body_scanned[low] != 0;
            if ((unscanned || misscanned) && shadow->alone_scans_listed < node_census_list)
            {
                // The scan and the front disagree on the body: listed, since the scan is to refuse a body before its reading.
                shadow->alone_scans_listed += 1;
                printf("node-shadow: alone %s file=%s line=%u verdict=%u tokens=%" PRIu64 "\n",
                       unscanned ? "the scan finds no lost root in a body the front lost one of" : "the scan finds a lost root in a body the front lost none of", s->file,
                       node_census_line(s->pp, program->parser, body->opening), verdict, tokens);
            }
            if (!own && reader_whole && !statics && shadow->alone_bodies_listed < node_census_list)
            {
                shadow->alone_bodies_listed += 1;
                printf("node-shadow: alone refuses a body whose popped roots the walk's nodes' machine takes file=%s line=%u verdict=%u\n", s->file,
                       node_census_line(s->pp, program->parser, body->opening), verdict);
            }
        }
        else
        {
            shadow->alone_bodies_unread += 1;
        }
    }
    census->bodies += 1;
    census->bodies_taken += whole;
    census->bodies_taken_nodes += whole ? body_nodes : 0;
}

// Whether a rename pragma the reader took gave object `object` its asm label (#3207): one of the unit's records of
// `#pragma redefine_extname`, without a warning, whose declaration (the one it labelled, check_input_symbol_attribute)
// declares the object.
static bool node_shadow_object_renamed(const CheckInput* input, uint32_t object)
{
    const CheckInputSymbolPragmas* pragmas = input->symbol_pragmas;
    bool renamed = false;
    for (uint32_t k = 0; pragmas != NULL && !renamed && k < pragmas->count; k += 1)
    {
        const CheckInputSymbolPragma* record = pragmas->records + k;
        renamed = record->supported && record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE && record->warning == CHECK_SYMBOL_WARNING_NONE &&
                  record->declaration < input->token_count && input->objects[record->declaration] == object;
    }
    return renamed;
}

// Reader time's facts of an object (`facts`, NodeMachine.reader_facts' bits, and its static declaration
// `reader_declaration`) against the program's (`declaration`, `twice`, `labelled`): whether they are the same.
// - An object no declaration of the unit's declares is the program's own, made after the reader (#3203, `own`): reader
//   time must have nothing for it.
// - A label the program has and reader time has not is no difference where a rename pragma made it (`pragma`, #3207:
//   T asks no label in such a unit), and is counted (`renamed`); any other difference of a label is one.
typedef struct NodeShadowObject NodeShadowObject;
struct NodeShadowObject
{
    bool same;
    bool own;
    bool renamed;
};

static NodeShadowObject node_shadow_object(uint8_t facts, uint32_t reader_declaration, uint32_t declaration, bool twice, bool labelled, bool pragma)
{
    const bool reader_labelled = (facts & BUSTER_NODE_MACHINE_OBJECT_LABELLED) != 0;
    const bool own = (facts & BUSTER_NODE_MACHINE_OBJECT_DECLARED) == 0 && declaration != BUSTER_CHECK_NONE;
    const bool renamed = !own && labelled && !reader_labelled && pragma;
    const bool same = own ? reader_declaration == BUSTER_CHECK_NONE && facts == 0 :
                            reader_declaration == declaration && ((facts & BUSTER_NODE_MACHINE_OBJECT_TWICE) != 0) == twice && (reader_labelled == labelled || renamed);
    return (NodeShadowObject){ .same = same, .own = own, .renamed = renamed };
}

// File `file`, read with `options`, prepared and lowered as -c lowers it, counted into `census`: whether its options could
// be read.
// The machine with no node of the walk's (c_check_node_machine_alone.c) against the machine on the walk's nodes, a root
// at a time, after the pipeline: a root the two ran on is one token's, the walk's node's in `graph` and the other graph's
// node kept by that token. Their records' count, T's refusal, the records' digest, the tests and their bits, C's and
// X's refusal, the root's target and object, the tape and the side's digest must be the same; the first field that
// isn't is counted, and listed with --list. The roots one of them alone ran on are counted, the walk's nodes' by
// whether they are in a function body (the other reads the bodies alone).
static void node_shadow_alone(NodeShadow* shadow, const NodeFrontShadow* front, const NodeFrontTypes* typed, const CheckGraph* graph, const Parser* parser,
                              Preprocessor* pp, const char* file)
{
    const NodeFrontTypes* alone = front->alone_types;
    const uint32_t tokens = parser->tokens.count;
    // A byte a token: whether a function body holds it.
    uint8_t* inside = calloc((size_t)tokens + 1, 1);
    for (uint32_t token = 0; inside != NULL && token < tokens; token += 1)
    {
        const uint32_t closing = parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY ? parser->tree.parents[token] : tokens;
        if (closing < tokens && closing > token)
        {
            memset(inside + token, 1, closing - token + 1);
        }
    }
    shadow->alone_bodies += front->alone_bodies;
    shadow->alone_bodies_left += front->alone_bodies_left;
    shadow->alone_positions += front->alone_positions;
    shadow->alone_records += front->alone_records;
    shadow->alone_ticks += front->alone_ticks;
    shadow->alone_machine_ticks += front->alone_machine_ticks;
    shadow->alone_resets += front->alone_resets;
    shadow->alone_taken_checked += front->alone_taken_checked;
    shadow->alone_taken_declined += front->alone_taken_declined;
    shadow->alone_declined_tokens += front->alone_declined_tokens;
    shadow->alone_prepare_ticks += front->alone_prepare_ticks;
    shadow->alone_front_ticks += front->alone_front_ticks;
    shadow->alone_write_ticks += front->alone_write_ticks;
    shadow->alone_scan_ticks += front->alone_scan_ticks;
    shadow->alone_scan_tokens += front->alone_scan_tokens;
    shadow->alone_sweep_ticks += alone->ticks;
    for (uint32_t step = 0; step < 4; step += 1)
    {
        shadow->alone_step_ticks[step] += alone->sweep_ticks[step];
    }
    shadow->alone_step_reads += alone->sweep_reads;
    shadow->alone_roots += alone->expressions;
    // The walk's nodes' roots in their order, each against the other's next one at its token: a token's roots are in the
    // order they were made in both (`next`: by token, the other's next root + 1 there once its first is taken, or
    // UINT32_MAX after its last).
    uint32_t* next = calloc((size_t)tokens + 1, sizeof(uint32_t));
    uint64_t matched = 0;
    for (uint32_t node = 0; next != NULL && node < graph->count && node < typed->node_capacity; node += 1)
    {
        if (typed->counts[node] != 0)
        {
            const uint32_t token = graph->tokens[node];
            const uint32_t kept = token >= tokens || next[token] == UINT32_MAX ? 0 : next[token] != 0 ? next[token] : front->alone_roots[token];
            const uint32_t own = kept != 0 ? kept - 1 : BUSTER_CHECK_NONE;
            if (token < tokens && own < alone->node_capacity)
            {
                next[token] = front->alone_chains[own] != 0 ? front->alone_chains[own] : UINT32_MAX;
            }
            if (own >= alone->node_capacity || alone->counts[own] == 0)
            {
                const bool body = inside != NULL && token < tokens && inside[token] != 0;
                shadow->alone_walk_only += body;
                shadow->alone_walk_only_outside += !body;
                if (body && shadow->alone_listed < node_census_list)
                {
                    shadow->alone_listed += 1;
                    printf("node-shadow: alone ran on no root the walk's nodes gave one file=%s line=%u token=%u\n", file, node_census_line(pp, parser, token), token);
                }
            }
            else
            {
                matched += 1;
                const uint8_t walk_flags = typed->refusals[node];
                const uint8_t own_flags = alone->refusals[own];
                const bool taken = (own_flags & BUSTER_NODE_FRONT_TYPED_REFUSAL) == NODE_MACHINE_TAKEN;
                const bool tested = (walk_flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0 && (own_flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0;
                const bool swept = (walk_flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 && (own_flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0;
                const bool both_taken = swept && typed->sweep_refusals[node] == NODE_MACHINE_TAKEN && alone->sweep_refusals[own] == NODE_MACHINE_TAKEN;
                uint32_t field = BUSTER_NODE_ALONE_FIELDS;
                if (typed->counts[node] != alone->counts[own])
                {
                    field = BUSTER_NODE_ALONE_FIELD_RECORDS;
                }
                else if ((walk_flags & BUSTER_NODE_FRONT_TYPED_REFUSAL) != (own_flags & BUSTER_NODE_FRONT_TYPED_REFUSAL))
                {
                    field = BUSTER_NODE_ALONE_FIELD_REFUSAL;
                }
                else if (typed->digests[node] != alone->digests[own])
                {
                    field = BUSTER_NODE_ALONE_FIELD_DIGEST;
                }
                else if ((walk_flags & BUSTER_NODE_FRONT_TYPED_TESTED) != (own_flags & BUSTER_NODE_FRONT_TYPED_TESTED))
                {
                    field = BUSTER_NODE_ALONE_FIELD_TESTED;
                }
                else if (tested && (typed->tests[node] != alone->tests[own] || typed->kinds[node] != alone->kinds[own]))
                {
                    field = BUSTER_NODE_ALONE_FIELD_TESTS;
                }
                else if ((walk_flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != (own_flags & BUSTER_NODE_FRONT_TYPED_SWEPT))
                {
                    field = BUSTER_NODE_ALONE_FIELD_SWEPT;
                }
                else if (swept && typed->sweep_refusals[node] != alone->sweep_refusals[own])
                {
                    field = BUSTER_NODE_ALONE_FIELD_SWEEP_REFUSAL;
                }
                else if (swept && (typed->targets[node] != alone->targets[own] || typed->objects[node] != alone->objects[own]))
                {
                    field = BUSTER_NODE_ALONE_FIELD_ROOT;
                }
                else if (both_taken)
                {
                    const uint32_t entries = typed->tape_counts[node];
                    const uint64_t walk_first = typed->tape_firsts[node];
                    const uint64_t own_first = alone->tape_firsts[own];
                    const bool same = entries == alone->tape_counts[own] &&
                                      (entries == UINT32_MAX || entries == 0 ||
                                       (memcmp(typed->tape_kinds + walk_first, alone->tape_kinds + own_first, entries) == 0 &&
                                        memcmp(typed->tape_classes + walk_first, alone->tape_classes + own_first, entries) == 0 &&
                                        memcmp(typed->tape_values + walk_first, alone->tape_values + own_first, (size_t)entries * sizeof(uint64_t)) == 0));
                    field = !same ? BUSTER_NODE_ALONE_FIELD_TAPE : typed->side_digests[node] != alone->side_digests[own] ? BUSTER_NODE_ALONE_FIELD_SIDE : field;
                }
                shadow->alone_compared += 1;
                shadow->alone_taken += taken && tested && alone->tests[own] == 0 && (own_flags & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 &&
                                       alone->sweep_refusals[own] == NODE_MACHINE_TAKEN;
                // A root it took before the walk raised an event at one of its tokens: what the gate on the walk's own
                // diagnostics counts (the tests' bits differ there, which is counted above too).
                shadow->alone_events_after += tested && (typed->tests[node] & (UINT64_C(1) << NODE_CENSUS_EVENT)) != 0 &&
                                              (alone->tests[own] & (UINT64_C(1) << NODE_CENSUS_EVENT)) == 0;
                if (field != BUSTER_NODE_ALONE_FIELDS)
                {
                    shadow->alone_differences += 1;
                    shadow->alone_fields[field] += 1;
                    if (shadow->alone_listed < node_census_list)
                    {
                        shadow->alone_listed += 1;
                        printf("node-shadow: alone difference file=%s line=%u token=%u field=%s records=%u (alone %u) refusal=%s (alone %s) tests=%#" PRIx64 " (alone %#" PRIx64
                               ")\n",
                               file, node_census_line(pp, parser, token), token, node_alone_field_names[field], typed->counts[node], alone->counts[own],
                               node_machine_refusal_names[walk_flags & BUSTER_NODE_FRONT_TYPED_REFUSAL], node_machine_refusal_names[own_flags & BUSTER_NODE_FRONT_TYPED_REFUSAL],
                               (walk_flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0 ? typed->tests[node] : 0, (own_flags & BUSTER_NODE_FRONT_TYPED_TESTED) != 0 ? alone->tests[own] : 0);
                    }
                }
            }
        }
    }
    // The roots it alone ran on: it ran on `expressions` of them, and the walk's nodes gave `matched` of those one.
    shadow->alone_own_only += alone->expressions > matched ? alone->expressions - matched : 0;
    free(next);
    free(inside);
}

static bool node_census_file(NodeCensus* census, const char* file, char** options, uint32_t option_count)
{
    static LowerSource source;
    lower_source_node_front = census->shadow != NULL ? census->shadow->front : 0;
    lower_source_node_reader = census->shadow != NULL ? census->shadow->reader : 0;
    lower_source_return_flow = census->shadow != NULL && census->shadow->bodies;
    lower_source_statement_controls = census->shadow != NULL && census->shadow->statement_controls;
    const bool valid = lower_source_open(&source, file, options, option_count, false);
    lower_source_node_front = 0;
    lower_source_node_reader = 0;
    lower_source_return_flow = false;
    lower_source_statement_controls = false;
    const bool prepared = source.prepared;
    CheckCorpusPath* path = &source.path;
    CheckProgramSelection selected = source.selected;
    if (prepared && census->shadow != NULL && census->shadow->statement_controls)
    {
        const bool taken = selected.program.input->statement_heads != NULL;
        census->shadow->statement_controls_units += taken;
        census->shadow->statement_controls_without += !taken;
    }
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
        NodeCensusScratch s = {
            .program = &selected.program,
            .pp = &source.pp,
            .file = file,
            .given_declaration = BUSTER_CHECK_NONE,
        };
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
            if (room && shadow->bodies)
            {
                // The bodies' entries: room for the longest lowered function's and a few more, and a label a control.
                uint32_t entries = 0;
                uint32_t controls = 0;
                for (uint32_t b = 0; b < selected.program.body_count; b += 1)
                {
                    entries = lowered.entry_tokens[b] != 0 && lowered.functions[b].count > entries ? lowered.functions[b].count : entries;
                    controls = selected.program.bodies[b].count > controls ? selected.program.bodies[b].count : controls;
                }
                // One for the tapes after the pipeline, one for those kept before the walk.
                NodeBodies* builders = BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeBodies, 2);
                room = builders != NULL;
                for (uint32_t which = 0; room && which < 2; which += 1)
                {
                    NodeBodies* built = builders + which;
                    *built = (NodeBodies){ .capacity = entries + 64, .label_capacity = controls + 2 };
                    built->kinds = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)built->capacity);
                    built->classes = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)built->capacity);
                    built->values = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, (uint64_t)built->capacity);
                    built->marks = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)built->capacity);
                    built->labels = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)built->label_capacity);
                    built->local_capacity = locals + 1;
                    built->addressed = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)built->local_capacity);
                    built->named_capacity = built->capacity;
                    built->named = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)built->named_capacity);
                    room = built->kinds != NULL && built->classes != NULL && built->values != NULL && built->marks != NULL && built->labels != NULL && built->addressed != NULL &&
                           built->named != NULL;
                }
                s.bodies = room ? builders : NULL;
                s.bodies_kept = room ? builders + 1 : NULL;
                // The controls' twin: its token columns, and room for a task, a label and a control a token.
                const uint64_t token_room = (uint64_t)selected.program.parser->tokens.count + 1;
                NodeTwin* twin = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeTwin, 1) : NULL;
                room = twin != NULL;
                if (room)
                {
                    *twin = (NodeTwin){ .capacity = (uint32_t)token_room };
                    uint32_t** columns[] = { &twin->first, &twin->last, &twin->next, &twin->previous, &twin->source_labels, &twin->plan_first, &twin->plan_last, &twin->plan_next,
                                             &twin->labels };
                    for (uint32_t c = 0; room && c < BUSTER_ARRAY_LENGTH(columns); c += 1)
                    {
                        *columns[c] = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, token_room);
                        room = *columns[c] != NULL;
                    }
                    twin->tasks = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeTwinTask, token_room) : NULL;
                    twin->reach = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeTwinReach, token_room) : NULL;
                    twin->entries = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, NodeTwinControl, token_room) : NULL;
                    twin->seen = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, token_room) : NULL;
                    twin->queue = room ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, token_room) : NULL;
                    room = room && twin->tasks != NULL && twin->reach != NULL && twin->entries != NULL && twin->seen != NULL && twin->queue != NULL;
                    s.twin = room ? twin : NULL;
                }
            }
            if (room && shadow->facts)
            {
                room = node_facts_begin(&facts, &machine, nodes, shadow->facts_window, shadow->facts_packed, shadow->facts_mode != BUSTER_NODE_FACTS_WHOLE,
                                        shadow->facts_operators, 0, &scratch);
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
                    shadow->front_plain_ticks += front->plain_ticks;
                    shadow->front_plain_records += front->plain_records;
                    shadow->front_plain_differences += front->plain_differences;
                    shadow->front_window_kept_values += front->window_kept_values;
                    shadow->front_window_kept_records += front->window_kept_records;
                    shadow->front_window_cut_values += front->window_cut_values;
                    if (front->window_arguments_most > shadow->front_window_arguments_most)
                    {
                        shadow->front_window_arguments_most = front->window_arguments_most;
                    }
                    shadow->front_window_name_ticks += front->window_name_ticks;
                    shadow->front_window_names += front->window_names;
                    shadow->front_window_lines += front->window_lines;
                    shadow->front_window_line_ticks += front->window_line_ticks;
                    shadow->front_window_name_differences += front->window_name_differences;
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
                    shadow->flat_ticks += front->flat_ticks;
                    shadow->flat_compact_ticks += front->flat_compact_ticks;
                    shadow->flat_tokens += front->flat_tokens;
                    shadow->flat_records += front->flat_records;
                    shadow->flat_empties += front->flat_empties;
                    shadow->flat_far += front->flat_far;
                    for (uint32_t stage = 0; stage < BUSTER_ARRAY_LENGTH(shadow->flat_stage_ticks); stage += 1)
                    {
                        shadow->flat_stage_ticks[stage] += front->flat_stage_ticks[stage];
                    }
                    shadow->flat_hops += front->flat_hops;
                    shadow->flat_beyond += front->flat_beyond;
                    shadow->flat_left += front->flat_left;
                    shadow->flat_compared += front->flat_compared;
                    shadow->flat_only += front->flat_only;
                    shadow->flat_lean_only += front->flat_lean_only;
                    shadow->flat_unread += front->flat_unread;
                    static const char* const flat_fields[] = { "compaction", "operator", "count", "distance", "root", "consumer", "leaf", "arguments", "operand", "classes" };
                    for (uint32_t d = 0; d < front->flat_differences && d < BUSTER_NODE_FRONT_LISTED && shadow->flat_listed < node_census_list; d += 1)
                    {
                        const uint32_t token = front->flat_difference_tokens[d];
                        const uint32_t field = front->flat_difference_fields[d] - BUSTER_NODE_FLAT_FIELD_COMPACTION;
                        shadow->flat_listed += 1;
                        printf("node-shadow: flat front difference file=%s line=%u token=%u field=%s\n", file, node_census_line(&source.pp, &source.parser, token), token,
                               field < BUSTER_ARRAY_LENGTH(flat_fields) ? flat_fields[field] : "?");
                    }
                    shadow->flat_differences += front->flat_differences;
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
                    // The tests on the window ran once on each expression.
                    shadow->own_expressions += typed->own_expressions;
                    shadow->own_records += typed->own_records;
                    shadow->own_visited += typed->own_visited;
                    shadow->own_visits += typed->own_visits;
                    shadow->own_walks += typed->own_walks;
                    shadow->own_trees += typed->own_trees;
                    shadow->own_operators += typed->own_operators;
                    shadow->own_calls += typed->own_calls;
                    shadow->own_constants += typed->own_constants;
                    shadow->own_thirds += typed->own_thirds;
                    shadow->own_count_differences += typed->own_count_differences;
                    shadow->own_quiet += typed->own_quiet;
                    shadow->own_quiet_calls += typed->own_quiet_calls;
                    shadow->own_quiet_calls_checked += typed->own_quiet_calls_checked;
                    shadow->own_quiet_calls_wrong += typed->own_quiet_calls_wrong;
                    shadow->own_quiet_calls_loud += typed->own_quiet_calls_loud;
                    shadow->own_quiet_casts += typed->own_quiet_casts;
                    shadow->own_quiet_casts_checked += typed->own_quiet_casts_checked;
                    shadow->own_quiet_casts_wrong += typed->own_quiet_casts_wrong;
                    shadow->own_quiet_orders += typed->own_quiet_orders;
                    shadow->own_quiet_orders_checked += typed->own_quiet_orders_checked;
                    shadow->own_quiet_orders_wrong += typed->own_quiet_orders_wrong;
                    shadow->own_result_casts += typed->own_result_casts;
                    shadow->own_result_asks += typed->own_result_asks;
                    shadow->own_root_nulls += typed->own_root_nulls;
                    shadow->own_quiet_checked += typed->own_quiet_checked;
                    shadow->own_values_checked += typed->own_values_checked;
                    shadow->own_values_wrong += typed->own_values_wrong;
                    shadow->own_quiet_wrong += typed->own_quiet_wrong;
                    shadow->own_tree_checked += typed->own_tree_checked;
                    shadow->own_tree_wrong += typed->own_tree_wrong;
                    shadow->own_lists_past += typed->own_lists_past;
                    shadow->own_direct += typed->own_direct;
                    shadow->own_direct_checked += typed->own_direct_checked;
                    shadow->own_direct_wrong += typed->own_direct_wrong;
                    for (uint32_t c = 0; c < NODE_FACTS_COUNTS; c += 1)
                    {
                        shadow->reader_facts[c] += typed->facts_counts[c];
                    }
                    for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
                    {
                        shadow->reader_facts_wanted[op] += typed->facts_wanted_ops[op];
                        shadow->reader_facts_left[op] += typed->facts_left_ops[op];
                    }
                    static const char* const facts_whys[BUSTER_NODE_FACTS_WHYS] = { "given", "its records are not all the chunk's", "an operand out of a byte's reach",
                                                                                     "a literal not decoded again", "a call of 255 operands or more" };
                    for (uint32_t why = 0; why < BUSTER_NODE_FACTS_WHYS; why += 1)
                    {
                        shadow->reader_facts_whys[why] += typed->facts_skipped_whys[why];
                        shadow->reader_window_whys[why] += typed->facts_window_whys[why];
                    }
                    // The differences first, and on a count of their own: the expressions not given are many.
                    static const char* const facts_kinds[] = { "a record over poison", "a record over decoys", "a window's lists", "a record's inputs from the front's records",
                                                               "a record's inputs as the front wrote them", "a record the kernel made of the front's window in place" };
                    for (uint32_t d = 0; d < typed->facts_listed && shadow->reader_facts_listed < node_census_list; d += 1)
                    {
                        const uint32_t token = typed->facts_difference_tokens[d];
                        const uint8_t op = typed->facts_difference_ops[d];
                        const uint8_t kind = typed->facts_difference_kinds[d];
                        shadow->reader_facts_listed += 1;
                        printf("node-shadow: reader-time fact words difference file=%s line=%u token=%u op=%s of=\"%s\"\n", file,
                               token != BUSTER_CHECK_NONE ? node_census_line(&source.pp, &source.parser, token) : 0, token,
                               op < CHECK_OP_COUNT && lower_inventory_op_names[op] != NULL ? lower_inventory_op_names[op] : "?",
                               kind < BUSTER_ARRAY_LENGTH(facts_kinds) ? facts_kinds[kind] : "?");
                    }
                    for (uint32_t d = 0; d < typed->facts_skipped_listed && shadow->reader_facts_skipped_listed < node_census_list; d += 1)
                    {
                        const uint32_t token = typed->facts_skipped_tokens[d];
                        const uint8_t why = typed->facts_skipped_reasons[d];
                        shadow->reader_facts_skipped_listed += 1;
                        printf("node-shadow: reader-time fact words not given by the front's records file=%s line=%u token=%u why=\"%s\"\n", file,
                               node_census_line(&source.pp, &source.parser, token), token, why < BUSTER_NODE_FACTS_WHYS ? facts_whys[why] : "?");
                    }
                    static const char* const window_whys[BUSTER_NODE_FACTS_WHYS] = { "given", "its records are not in a row", "?", "?", "?" };
                    for (uint32_t d = 0; d < typed->facts_window_listed && shadow->reader_window_listed < node_census_list; d += 1)
                    {
                        const uint32_t token = typed->facts_window_tokens[d];
                        const uint8_t why = typed->facts_window_reasons[d];
                        shadow->reader_window_listed += 1;
                        printf("node-shadow: reader-time fact words not given by the front's window file=%s line=%u token=%u why=\"%s\"\n", file,
                               node_census_line(&source.pp, &source.parser, token), token, why < BUSTER_NODE_FACTS_WHYS ? window_whys[why] : "?");
                    }
                    for (uint32_t reason = 0; reason < BUSTER_NODE_CONTEXTS_REASONS; reason += 1)
                    {
                        shadow->contexts_listed[reason] += typed->contexts_listed[reason];
                    }
                    for (uint32_t d = 0; d < typed->contexts_shown && shadow->contexts_shown < node_census_list; d += 1)
                    {
                        const uint32_t token = typed->contexts_tokens[d];
                        shadow->contexts_shown += 1;
                        printf("node-shadow: window contexts difference file=%s line=%u token=%u\n", file, node_census_line(&source.pp, &source.parser, token), token);
                    }
                    for (uint32_t d = 0; d < typed->rows_shown && shadow->rows_shown < node_census_list; d += 1)
                    {
                        const uint32_t token = typed->rows_tokens[d];
                        shadow->rows_shown += 1;
                        printf("node-shadow: window rows difference file=%s line=%u token=%u\n", file, node_census_line(&source.pp, &source.parser, token), token);
                    }
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
                    if (front->alone_types != NULL && front->alone_roots != NULL)
                    {
                        node_shadow_alone(shadow, front, typed, &path->input.graph, &source.parser, &source.pp, file);
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
                            s.given_declaration = context == NODE_CENSUS_ROOT_INITIALIZER ? typed->declarations[node] : BUSTER_CHECK_NONE;
                            uint64_t tests = node_census_expression(&s, node, 0, context, &walked);
                            s.given_declared = 0;
                            s.given_declaration = BUSTER_CHECK_NONE;
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
                        const bool pragma = labelled && (facts & BUSTER_NODE_MACHINE_OBJECT_LABELLED) == 0 && node_shadow_object_renamed(&path->input, object);
                        const NodeShadowObject compared = node_shadow_object(facts, typed->object_declarations[object], declaration, twice, labelled, pragma);
                        const bool own = compared.own;
                        const bool renamed = compared.renamed;
                        const bool same = compared.same;
                        shadow->typed_objects += !own && (declaration != BUSTER_CHECK_NONE || typed->object_declarations[object] != BUSTER_CHECK_NONE);
                        shadow->typed_objects_own += own;
                        shadow->typed_objects_renamed += renamed;
                        shadow->typed_object_differences += !same;
                        if (!same && shadow->typed_listed < node_census_list)
                        {
                            shadow->typed_listed += 1;
                            printf("node-shadow: reader-time object difference file=%s object=%u declaration=%u (program %u) twice=%u (%u) labelled=%u (%u)\n", file, object,
                                   typed->object_declarations[object], declaration, (facts & BUSTER_NODE_MACHINE_OBJECT_TWICE) != 0, twice,
                                   (facts & BUSTER_NODE_MACHINE_OBJECT_LABELLED) != 0, labelled);
                        }
                    }
                    if (typed->summary_firsts != NULL)
                    {
                        // The side facts reader time kept, against the program's own walks. (Last: the references'
                        // walk marks the program's regions and compound initializers as reached.)
                        node_shadow_summaries(shadow, &s, &selected.program, typed, &scratch);
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

// Whether anything the shadow compared differs from the pipeline's, or from its own second making: the command's
// status, and what a fixture of the whole shadow asks (c_check_node_census_test.c's node_objects_test_fixture).
static bool node_shadow_differs(const NodeShadow* shadow)
{
    if (shadow->statement_controls)
    {
        // Read as -c reads it, a statement's value has no node of the walk's for its discard: the front's records and the
        // machine's roots before the walk are not the walk's there, and what is compared is the machine's values and
        // entries after the pipeline and the bodies' comparisons, which read the lowered function.
        return shadow->value_differences != 0 || shadow->entry_differences != 0 || shadow->twin_differences != 0 || shadow->bodies_different[0] != 0 ||
               shadow->bodies_different[1] != 0 || shadow->bodies_dropped_attempted != 0 || shadow->twin_different != 0 || shadow->twin_unsound != 0;
    }
    return shadow->value_differences != 0 || shadow->entry_differences != 0 || shadow->twin_differences != 0 || shadow->fused_differences != 0 ||
           shadow->bodies_different[0] != 0 || shadow->bodies_different[1] != 0 || shadow->bodies_dropped_attempted != 0 || shadow->twin_different != 0 || shadow->twin_unsound != 0 ||
           shadow->facts_differences != 0 || shadow->facts_list_differences != 0 || shadow->facts_failures != 0 ||
           shadow->facts_conversions != shadow->facts_conversions_derived || shadow->reads.same_differences != 0 || shadow->reads.misplaced != 0 ||
           shadow->front_differences != 0 || shadow->typed_differences != 0 || shadow->typed_object_differences != 0 || shadow->typed_library_differences != 0 ||
           shadow->swept_differences != 0 || shadow->alone_differences != 0 || shadow->alone_walk_only != 0 || shadow->alone_own_only != 0 ||
           shadow->alone_bodies_unread != 0 || shadow->alone_taken_declined != 0 || shadow->alone_events_after != 0 || shadow->tested_unsound != 0 || shadow->tested_differences != 0 || shadow->tested_kind_differences != 0 ||
           shadow->tested_missed != 0 || shadow->tested_policy_differences != 0 || shadow->reader_twin_differences != 0 || shadow->own_count_differences != 0 ||
           shadow->own_quiet_wrong != 0 || shadow->own_quiet_calls_wrong != 0 || shadow->own_quiet_calls_loud != 0 || shadow->own_quiet_casts_wrong != 0 ||
           shadow->own_quiet_orders_wrong != 0 || shadow->own_values_wrong != 0 || shadow->own_tree_wrong != 0 || shadow->own_direct_wrong != 0 ||
           shadow->own_lists_past != 0 || shadow->flat_differences != 0 || shadow->reader_facts[NODE_FACTS_COUNT_DIFFERENCES] != 0 ||
           shadow->reader_facts[NODE_FACTS_COUNT_LIST_DIFFERENCES] != 0 || shadow->reader_facts[NODE_FACTS_COUNT_FAILURES] != 0 ||
           shadow->reader_facts[NODE_FACTS_COUNT_FRONT_DIFFERENCES] != 0 || shadow->reader_facts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] != 0 ||
           shadow->reader_facts[NODE_FACTS_COUNT_TESTS_DIFFERENCES] != 0 || shadow->reader_facts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] != 0 ||
           shadow->front_plain_differences != 0 || shadow->reader_facts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] != 0 ||
           shadow->reader_facts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] != 0 || shadow->reader_facts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] != 0 ||
           shadow->reader_facts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES] != 0 || shadow->summary_reference_differences != 0 ||
           shadow->summary_fact_differences != 0 || shadow->summary_truth_differences != 0 || shadow->summary_fold_differences != 0 ||
           shadow->summary_selector_differences != 0 || shadow->summary_unkept != 0 || shadow->front_window_name_differences != 0 || shadow->front_window_arguments_most > BUSTER_NODE_FRONT_RECORDS;
}

static int run_node_census(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    bool shadowed = false;
    NodeWindowCounter window_counter = { .descriptor = -1 };
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
        else if (strcmp(arguments[i], "--front-flat") == 0 || strcmp(arguments[i], "--front-flat-first") == 0)
        {
            // The flat front beside the lean one, its twin: after the walk as the lean front, or before the walk's
            // actions (the lean front stays where the other options put it, after the walk unless they say before).
            const uint8_t flags = (uint8_t)(BUSTER_NODE_FRONT_FLAT | BUSTER_NODE_FRONT_LEAN | (strcmp(arguments[i], "--front-flat-first") == 0 ? BUSTER_NODE_FRONT_FLAT_FIRST : 0));
            shadow.front = (uint8_t)(shadow.front | flags | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
        }
        else if (strcmp(arguments[i], "--statement-controls") == 0)
        {
            // The unit read as a compile reads it: an expression statement's value its control's own, no cast to void.
            shadow.statement_controls = true;
        }
        else if (strcmp(arguments[i], "--bodies") == 0)
        {
            // Each lowered body's entries made again from the program's controls and its roots' tapes (node_bodies_begin).
            shadow.bodies = true;
        }
        else if (strcmp(arguments[i], "--front-tests") == 0)
        {
            // The census's tests at reader time, between T and C and X there, by the census's code on the view.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS;
        }
        else if (strcmp(arguments[i], "--front-alone") == 0 || strcmp(arguments[i], "--front-alone-split") == 0 || strcmp(arguments[i], "--front-alone-first") == 0)
        {
            // The machine with no node of the walk's, a body at a time before the walk acts at it; and the machine on the
            // walk's nodes after it, which it is compared with: T, the census's tests, C and X at reader time.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_ALONE | (strcmp(arguments[i], "--front-alone-split") == 0 ? BUSTER_NODE_READER_SPLIT : 0) |
                             (strcmp(arguments[i], "--front-alone-first") == 0 ? BUSTER_NODE_READER_ALONE_FIRST : 0);
        }
        else if (strcmp(arguments[i], "--front-facts") == 0 || strcmp(arguments[i], "--front-window") == 0)
        {
            // #2850's fact words' kernel at reader time, after T there, on the lean front's expressions; and with
            // --front-window the front writes the kernel's window as it makes each record.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_LEAN | ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_FACTS | (strcmp(arguments[i], "--front-window") == 0 ? BUSTER_NODE_READER_WINDOW : 0);
        }
        else if (strcmp(arguments[i], "--window-tests") == 0)
        {
            // The tests in the machine's own form, on the window the front writes, from the kernel's records in place;
            // beside the census's code on T's records, whose bits are their reference: --front-window's and
            // --front-tests' options with it.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN |
                                     ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS;
        }
        else if (strcmp(arguments[i], "--window-contexts") == 0)
        {
            // C's answers on the same window (c_check_node_machine_window.c), beside C on T's columns at reader time,
            // their reference: --window-tests' options with it, whose sweep makes the records' bytes.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN |
                                     ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                             BUSTER_NODE_READER_WINDOW_CONTEXTS;
        }
        else if (strcmp(arguments[i], "--window-rows") == 0)
        {
            // X's rows on the same window (c_check_node_machine_rows.c), beside X's tape on T's columns at reader time,
            // their reference: --window-contexts' options with it, whose answers they are made from.
            shadow.front = (uint8_t)(shadow.front | BUSTER_NODE_FRONT_TYPES | BUSTER_NODE_FRONT_SWEEPS | BUSTER_NODE_FRONT_LEAN |
                                     ((shadow.front & BUSTER_NODE_FRONT_ORDER) != 0 ? 0 : BUSTER_NODE_FRONT_AFTER));
            shadow.reader |= BUSTER_NODE_READER_TESTS | BUSTER_NODE_READER_FACTS | BUSTER_NODE_READER_WINDOW | BUSTER_NODE_READER_WINDOW_TESTS |
                             BUSTER_NODE_READER_WINDOW_CONTEXTS | BUSTER_NODE_READER_WINDOW_ROWS;
        }
        else if (strcmp(arguments[i], "--window-counter") == 0)
        {
            valid = window_counter.event == NULL && i + 2 < count && node_window_select(&window_counter, arguments[i + 1], arguments[i + 2]);
            i += valid ? 2 : 0;
        }
        else if (strcmp(arguments[i], "--no-window-summaries") == 0)
        {
            // The switch for a summary's side facts' cost: --window-rows without them, in the same executable.
            shadow.reader |= BUSTER_NODE_READER_WINDOW_SUMMARY_OFF;
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
    valid = valid && (window_counter.event == NULL || (shadowed && (shadow.reader & BUSTER_NODE_READER_WINDOW_ROWS) != 0));
    if (valid && window_counter.event != NULL)
    {
        valid = node_window_pmu_open(&window_counter);
        if (!valid)
        {
            fprintf(stderr, "measure-node-machine: could not open or schedule pinned user counter %s\n", window_counter.event->name);
        }
        node_window_counting = valid ? &window_counter : NULL;
    }
    NodeCensus census = { .shadow = shadowed ? &shadow : NULL };
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = node_census_file(&census, files[i], options, option_count);
        fflush(stdout);
    }
    node_window_counting = NULL;
    if (window_counter.failed)
    {
        fprintf(stderr, "measure-node-machine: hardware counter reading failed\n");
        valid = false;
    }
    if (valid && window_counter.event != NULL)
    {
        node_window_pmu_report(&window_counter, (shadow.reader & BUSTER_NODE_READER_WINDOW_SUMMARY_OFF) == 0);
    }
    node_window_pmu_close(&window_counter);
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
        fprintf(stderr, "usage: compiler measure-node-machine [--list N] [--shadow [--scalar] [--verify] [--facts | --facts-lean | --facts-loads] [--facts-operators] [--facts-reads] [--facts-packed] [--facts-window N] [--window-rows [--window-counter FAMILY EVENT]] [--passes N] [--repeat N]] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    const bool differences = shadowed && node_shadow_differs(&shadow);
    return valid && census.failures == 0 && census.files != 0 && !differences ? 0 : 1;
}

#endif
