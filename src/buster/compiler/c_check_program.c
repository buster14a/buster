// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Executable typed bodies. The source reader retains expression roots and declaration/body ownership; this
// consumer turns parser statements into bounded control columns without redoing name or type lookup. Expressions
// stay in CheckGraph/CheckStream, and control entries retain node identities even when Tier0 lowering refuses a type.
// Include after c_check_input.c, calling_convention.c and tier0.c; all input storage outlives this output.
// Output and scratch are disjoint; failure rolls back output and always restores scratch. No padding is required.
// Backend tests share only CheckProgramStatus, the vector/x86 rows and the frame verifier.
#ifndef BUSTER_C_CHECK_PROGRAM_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_INCLUDED

typedef enum CheckProgramStatus
{
    CHECK_PROGRAM_OK,
    CHECK_PROGRAM_INVALID,
    CHECK_PROGRAM_UNSUPPORTED,
    CHECK_PROGRAM_RESOURCE,
    CHECK_PROGRAM_BACKEND,
} CheckProgramStatus;
#if !defined(BUSTER_UNIT) || BUSTER_SOURCE_CONSUMER
// Cold constraint facts survive a failed program's output rollback. No source or arena pointer is retained.
typedef enum CheckProgramMessage
{
    CHECK_PROGRAM_MESSAGE_GENERIC,
    CHECK_PROGRAM_MESSAGE_DUPLICATE_CASE,
    CHECK_PROGRAM_MESSAGE_DUPLICATE_DEFAULT,
    // A switch's condition of a type that isn't an integer's (`type`).
    CHECK_PROGRAM_MESSAGE_CONDITION,
} CheckProgramMessage;

typedef struct CheckProgramFailure CheckProgramFailure;
struct CheckProgramFailure
{
    CheckProgramMessage message;
    TypeId type;
    unsigned __int128 value;
    // A duplicate case's limbs, where its switch is on a _BitInt past 128 bits (#2220), or null.
    const uint64_t* wide;
};

// One of the constraint errors check_program_diagnose_all lists (#1447): where clang reports it (`token`), where its
// statement is complete (`position`, which publishes it), its words' facts (`failure`, and `node` for a condition's
// type's spelling) and the token of clang's "previous case defined here" note (`previous`, or BUSTER_CHECK_NONE).
typedef struct CheckProgramConstraint CheckProgramConstraint;
struct CheckProgramConstraint
{
    uint32_t token;
    uint32_t position;
    uint32_t previous;
    uint32_t node;
    CheckProgramFailure failure;
};

// A variable-length array's object (block scope, automatic, of a runtime size) is ALLOCATEd at its declaration (`token`;
// `target` is its block's closing token), after its bounds' VLA entries; the first of its block is preceded by the
// block's STACK_SAVE, and the block's end and every break, continue or goto that leaves the block's region (after the
// save, to its end) by a STACK_RESTORE of the outermost block it leaves (`target`, a closing token). The function's
// own block's end needs none: its return releases its frame.
// GNU C's labels as values: a label whose address the program takes (&&label) marks its place with CHECK_CONTROL_LABEL
// (`token` the label), which does nothing else, and a computed goto (goto *expression) is CHECK_CONTROL_JUMP_INDIRECT of
// its operand (`node`, converted to const void *), which goes to one of them. It releases no variable-length array's
// storage on its way out of their blocks, as clang's code doesn't: the function's return does.
// GNU's cleanup attribute (CheckInput.cleanups): each exit from a variable's scope calls its function with the variable's
// address, CHECK_CONTROL_CLEANUP (`target` the variable's record, `token` the exit: its block's closing token, or the
// jump), the innermost variable's first, before the exit's STACK_RESTORE: its block's end, a break, a continue or a goto
// that leaves the scope, and a return, whose value (`node`) a CHECK_CONTROL_RESULT computes first, which its RETURN, of
// no node and target BUSTER_CHECK_CONTROL_KEPT, then returns. A for statement's own variables' cleanups come after its
// end's label, which its break reaches, and a statement expression's block's after its END, which its region counts
// (CheckProgramRegion.cleanups): its value is computed before them.
// An exit that leaves cleanups' scopes from the function's own statements (#1370) is one CHECK_CONTROL_EXIT instead of
// their run: `node` BUSTER_CHECK_NONE, `token` the jump and `target` its CheckProgramExit, which names the innermost
// record it runs and the outermost. The controls after it (its tail: its STACK_RESTORE, its LIFETIME ends and its JUMP,
// or its RETURN) are what follows the calls, so every walk of the controls sees the path it saw when the calls were
// there. The lowering runs each record's call once, at its CLEANUP at its scope's end, the chain the exit enters
// (check_program_chains, CheckProgramChain). Statement expressions share these chains too: their block's own entries
// run after the value is kept, and exits through enclosing records abandon the region's saved operands.
// A block's objects' lifetimes (#177, c_check_program_lifetime.c), in a program asked for them: CHECK_CONTROL_LIFETIME
// (`target` the block's closing brace, whose locals CheckProgram.lifetime_first lists, `node` 0 for their start and 1 for
// their end, `token` where) where the block's statements begin, where they end, after its cleanups, and before each
// break, continue or goto that leaves it, after the jump's cleanups.
typedef enum CheckControlKind
{
    CHECK_CONTROL_EVALUATE,
    CHECK_CONTROL_INITIALIZE,
    CHECK_CONTROL_VLA,
    CHECK_CONTROL_ASM,
    CHECK_CONTROL_RETURN,
    CHECK_CONTROL_SWITCH,
    CHECK_CONTROL_CASE,
    CHECK_CONTROL_END,
    CHECK_CONTROL_STACK_SAVE,
    CHECK_CONTROL_ALLOCATE,
    CHECK_CONTROL_STACK_RESTORE,
    CHECK_CONTROL_LABEL,
    CHECK_CONTROL_JUMP_INDIRECT,
    CHECK_CONTROL_CLEANUP,
    CHECK_CONTROL_RESULT,
    CHECK_CONTROL_LIFETIME,
    CHECK_CONTROL_EXIT,
    // The kinds from here on go to a label: their `target`.
    CHECK_CONTROL_BRANCH_FALSE,
    CHECK_CONTROL_BRANCH_TRUE,
    CHECK_CONTROL_JUMP,
} CheckControlKind;

// A RETURN's target that returns the value its CHECK_CONTROL_RESULT computed, before the return's cleanups.
#define BUSTER_CHECK_CONTROL_KEPT UINT32_C(0xfffffffe)
// A RETURN's target that makes its value's call a tail call: a musttail return's (#147, CheckInput.musttails), whose
// call c_check_musttail.c checked, which has no cleanup to run.
#define BUSTER_CHECK_CONTROL_TAIL UINT32_C(0xfffffffd)

typedef struct CheckControl CheckControl;
struct CheckControl
{
    uint32_t node;
    uint32_t token;
    uint32_t target;
    CheckControlKind kind;
};

// Lifetime controls use node for their start/end bit. All other controls without an expression already carry NONE.
static uint32_t check_control_expression_node(const CheckControl* control)
{
    return control->kind == CHECK_CONTROL_LIFETIME ? BUSTER_CHECK_NONE : control->node;
}

typedef struct CheckProgramBody CheckProgramBody;
struct CheckProgramBody
{
    uint32_t declaration;
    uint32_t opening;
    uint32_t object;
    uint32_t first;
    uint32_t count;
    uint32_t entry_count;
    uint32_t reference_first;
    uint32_t reference_count;
    // Its source labels' places (CheckProgram.label_positions): label_count of them from label_first.
    uint32_t label_first;
    uint32_t label_count;
    // Its cleanups' records (CheckInput.cleanups, cleanup_count from cleanup_first), and whether an exit of its runs
    // them through their chain (#1370: CheckProgram.chains holds their plan).
    uint32_t cleanup_first;
    uint32_t cleanup_count;
    bool chains;
    bool is_main;
    bool return_flow;
    bool falls;
    uint8_t return_warning;
};

typedef struct CheckProgramRegion CheckProgramRegion;
struct CheckProgramRegion
{
    uint32_t first;
    uint32_t count;
    uint32_t body;
    uint32_t node;
    uint32_t result;
    // The last of its `count` controls that are its block's cleanups, which run after its value is computed.
    uint32_t cleanups;
};

// An exit through cleanups' chain (#1370, CHECK_CONTROL_EXIT's `target`): the innermost record it runs (`first`) and the
// outermost (`last`), both CheckInput.cleanups' records; the destination slot's value it stores before it jumps to its
// first record's entry, BUSTER_CHECK_NONE where no dispatch on its path tests it; its destination's CheckProgramStop;
// and how many controls after it form its tail. The first emitted exit keeps that tail unless the destination is a
// lone JUMP's target; subsequent emitted exits leave theirs out. A `tail_count` of BUSTER_CHECK_NONE marks an exit that
// runs its calls in place: one through a record without a CLEANUP at its scope's end (a constant switch's kept
// statements leave their compounds' ends out), or one no path completes (its JUMP unreached, an END now).
typedef struct CheckProgramExit CheckProgramExit;
struct CheckProgramExit
{
    uint32_t first;
    uint32_t last;
    // The innermost record whose scope holds where the exit goes, BUSTER_CHECK_NONE for none (a return's): `last` is its
    // child on the path from `first`, which the planner finds.
    uint32_t hold;
    uint32_t value;
    uint32_t destination;
    uint32_t tail_count;
};

// A record's place in its body's chain of cleanups (#1370, check_program_chains), by its index in CheckInput.cleanups:
// `entry`, its CLEANUP control at its scope's end (body-relative), which runs its call once for every exit and the
// scope's fall alike; `parent` and `depth`, its place among the body's records; and what follows its call: the stops of
// `stop_count` destinations from `stop_first` (CheckProgram.chain_stops), each where the slot holds its value, then a
// jump to its parent's entry for the exits that go on (OUTWARD), then the next control (NATURAL).
#define BUSTER_CHECK_CHAIN_OUTERMOST UINT8_C(1)
#define BUSTER_CHECK_CHAIN_FALLS UINT8_C(2)
#define BUSTER_CHECK_CHAIN_NATURAL UINT8_C(4)
#define BUSTER_CHECK_CHAIN_OUTWARD UINT8_C(8)
#define BUSTER_CHECK_CHAIN_HEAD UINT8_C(16)
// The head of its scope's run, where the fall stores the slot's value 0 before it, and exits enter past the store.
#define BUSTER_CHECK_CHAIN_FALL_STORE UINT8_C(32)
#define BUSTER_CHECK_CHAIN_TESTED UINT8_C(64)
typedef struct CheckProgramChain CheckProgramChain;
struct CheckProgramChain
{
    uint32_t entry;
    uint32_t parent;
    uint32_t depth;
    uint32_t stop_first;
    uint32_t stop_count;
    // The planner's: the least depth of the exits' last records from its subtree, and the depth plus one of its nearest
    // ancestor (itself included) whose dispatch tests the slot, 0 for none.
    uint32_t reach;
    uint32_t tested;
    uint8_t flags;
};

// A destination of a record's dispatch (#1370): the slot's value its exits store, and a lone JUMP's body-relative target,
// or BUSTER_CHECK_NONE for a tail chosen from the first emitted exit. An unevaluated region may contain the first exit
// in control order, so the planner cannot choose its tail as the dispatch's destination.
typedef struct CheckProgramStop CheckProgramStop;
struct CheckProgramStop
{
    uint32_t value;
    uint32_t label;
};

typedef struct CheckProgramOptions CheckProgramOptions;
struct CheckProgramOptions
{
    const CheckInitializers* initializers;
    const CheckAsms* asms;
    // Completed static declarations from the same unit, when emission already prepared them.
    const CheckDataPrepared* prepared_data;
    // Borrowed for main recognition and compatibility checks before adding target-runtime call symbols.
    const InternTable* identifiers;
    const uint32_t* initializer_references;
    uint32_t initializer_reference_count;
    bool scalar;
    // Give non-aggregate functions the return-flow walk that -Wreturn-type needs. A warning-suppressed command leaves
    // it out, so its checking cost stays where it was before the warning existed.
    bool return_warnings;
    // A void function can also fall off despite its noreturn declaration. Its warning group is independent of
    // -Wreturn-type, so turning the latter off must not remove this analysis.
    bool noreturn_warnings;
    // Also references in the operands of &&, || and ?: that a constant condition never evaluates, which no emission
    // needs: test-checker-corpus --symbols' model of the references clang -O0's code has (#129). An if whose condition
    // is a constant leaves its dead arm out whatever this says (CheckProgram.dropped_ranges).
    bool unselected_operands;
    // Optional deterministic tally: unit graph-mark initialization plus return-warning expression visits.
    uint64_t* return_flow_work;
    // Optional deterministic tally of the blocks' lifetimes' walks (#1355): the parent paths' steps
    // (check_program_lifetime_entered) and the writer's (CheckControlWriter.lifetime_steps), which test-checker bounds by
    // the tokens, the jumps and the ends they make.
    uint64_t* lifetime_work;
    // Optional deterministic tallies (#2995): the parents the walks from statement expressions to their GNU break and
    // continue targets pass (check_control_region_targets), at most one a token, and those the walks from
    // variable-length arrays' objects and bounds to their statements pass (check_program_statement), at most two.
    uint64_t* region_target_work;
    uint64_t* statement_walk_work;
    // The selection census (#2536's step 2.1, --selection-census): each body's candidates' references besides its own, by
    // a walk from every expression root of the body and by scans of the graph's VALUE nodes (check_program_census_walk and
    // _scan), into CheckProgram's census_walk, census_scan and census_arms.
    bool selection_census;
    // The candidates' pass alone (#2536's step 2.2b, --provisional-selection): CheckProgram.census_pass, for a
    // provisional selection the emission numbers its symbols by (check_driver_provisional_select), without the other
    // censuses.
    bool candidates;
    // The generated code's instruction set (x86_target.c's X86Target, the preprocessor's from -march and the -m flags):
    // which vector types the lowering takes, how wide a vector register the convention passes them in, and which
    // instructions its rows use. Zero is x86-64 baseline.
    X86Target target;
    // The options it came from (c_predefines.c), against which a function's target attribute resolves (#180); null leaves
    // every function the unit's target.
    const PredefinesOptions* predefines;
    // The blocks' objects' lifetimes (#177, c_check_program_lifetime.c): CHECK_CONTROL_LIFETIME entries, which the
    // lowering turns into its functions' lifetime events, for Tier 1's frame (the C driver's --tier1).
    bool lifetimes;
    // The frame pointer the code keeps (#177, predefines_frame_pointer's BUSTER_PREDEFINES_FRAME_POINTER_*, which
    // check_request_program_options takes from the predefines): every function's (Tier0Function.frame_pointer).
    uint8_t frame_pointer;
    // Whether the code probes the stack as it grows it (#177, -fstack-clash-protection, predefines_stack_clash): every
    // function's (Tier0Function.probe_stack).
    bool probe_stack;
    // -fpatchable-function-entry=N[,M[,SECTION]] (#1447, predefines_patchable): the NOPs every function without a
    // patchable_function_entry attribute gets, `patchable_count` of them, `patchable_offset` before its symbol (none for a
    // count of 0), and the section of their entries for every function whose attribute names none (empty for
    // __patchable_function_entries). The emission reads them through check_program_patchable.
    uint32_t patchable_count;
    uint32_t patchable_offset;
    String8 patchable_section;
    // Every asm statement with instructions keeps its helper, none placed in its caller (#1059, check_asm_row): for a
    // lowering Tier 1's IR takes without asm bodies (the IR's source tools, whose text form has none), which has no
    // operation for a placed statement's row and calls its helper as any function's.
    bool asm_helpers;
    // Every asm statement placed in its caller is an asm body, none a row (#1839, check_asm_inline_code): for a lowering
    // Tier 1's IR takes (the C driver's --tier1), whose body calls run asm bodies (IrCall.body) but which has no
    // operation for a statement's row. A function Tier 1 refuses falls back to Tier 0, which takes bodies as it does rows.
    bool asm_bodies;
    // The helper families (a bit each, CheckProgramHelperFamily's) whose every operation takes its helper where it has a
    // direct form (#1059's paired measurements: the C driver's --helper-forms, measure-helpers): the fences', the atomic
    // accesses', the read/modify/writes' (compare-exchanges and fetch loops among them) builtins, and asm statements. An
    // implicit _Atomic object's accesses have no helper to take, and stay the typed stream's.
    uint32_t helper_forms;
    // The scratch gives its pages past this pass's columns back in whole huge pages (arena_discard_huge): a lane of the
    // C driver's units that will compile another unit after this one (#777), whose parse and reader push as far again.
    bool discard_huge;
    // With discard_huge, the bytes past this pass's columns that it keeps besides: for a unit read as its view (#2531),
    // what its columns would have taken for the parse's tokens the view leaves out, which its next unit's parse and
    // reader push as far again (without it, each viewed unit gave back huge pages that the next one took fresh, cleared).
    uint64_t discard_keep;
};

typedef struct CheckProgramCase CheckProgramCase;
struct CheckProgramCase
{
    unsigned __int128 lower;
    unsigned __int128 upper;
    // A switch on a _BitInt past 128 bits (#2220): its values' limbs (c_check_wide.c's, which live as long as the process),
    // whose low 128 bits `lower` and `upper` keep; null for every other switch's.
    const uint64_t* wide_lower;
    const uint64_t* wide_upper;
    TypeId type;
    uint32_t token;
    uint32_t owner;
    uint32_t target;
    bool fallback;
    bool range;
};

// GNU's alias, ifunc and weakref attributes (#147, c_check_program_alias.c): what a file-scope declaration with one makes
// its object. An alias is a symbol of its own name at another definition of the unit's, an ifunc an STT_GNU_IFUNC symbol
// at its resolver function's, and a weakref, whose own name names no symbol, makes its references weak ones to its
// target's name. Clang ignores the first two on a block-scope declaration.
typedef enum CheckProgramAliasKind
{
    CHECK_PROGRAM_ALIAS,
    CHECK_PROGRAM_IFUNC,
    CHECK_PROGRAM_WEAKREF,
} CheckProgramAliasKind;

// An alias's or ifunc's error, as clang's code generation reports it (CodeGenModule::checkAliases), at -c and when it
// links but not at -fsyntax-only, or a shape the emission refuses by name (THREAD_LOCAL). A weakref has none.
typedef enum CheckProgramAliasError
{
    CHECK_PROGRAM_ALIAS_OK,
    // "alias must point to a defined variable or function", "ifunc must point to a defined function", with a note that
    // the name must be the mangled one: its name names nothing the unit defines (a weakref's name is none, nor is an
    // inline function's that the unit doesn't emit: BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED).
    CHECK_PROGRAM_ALIAS_UNDEFINED,
    // An ifunc's resolver that is a definition but not a function's (a variable's, or another ifunc's): the same error,
    // without the note.
    CHECK_PROGRAM_ALIAS_NOT_FUNCTION,
    // "alias definition is part of a cycle", "ifunc definition is part of a cycle".
    CHECK_PROGRAM_ALIAS_CYCLE,
    // A function's alias of a variable, "cannot alias a variable with a function", and a variable's of a function,
    // "cannot alias a function with a variable", each with a note at the aliasee's definition.
    CHECK_PROGRAM_ALIAS_TO_VARIABLE,
    CHECK_PROGRAM_ALIAS_TO_FUNCTION,
    // "ifunc resolver function must return a pointer".
    CHECK_PROGRAM_ALIAS_RESOLVER,
    // An alias declared thread-local of a definition that isn't, or the other way: its symbol is its definition's type,
    // as clang's object has it, where nothing refers to it; refused where the selection's code does
    // (BUSTER_CHECK_PROGRAM_ALIAS_REFERENCED), whose references would reach it as its own declaration says and the
    // symbol isn't (clang's code is as wrong, #1849).
    CHECK_PROGRAM_ALIAS_THREAD_LOCAL,
} CheckProgramAliasError;

// An object's alias, ifunc or weakref, from its first declaration with one (in token order), whose attributes give the
// kind (weakref first, then ifunc, then alias) and the name: weakref's argument, or the alias attribute's beside it.
typedef struct CheckProgramAlias CheckProgramAlias;
struct CheckProgramAlias
{
    uint32_t object;
    // The declaration, and the attribute's name token, where clang reports its errors.
    uint32_t token;
    uint32_t attribute;
    String8 target;
    // The unit's object whose symbol the name is (an asm label's or else an identifier's, of a file-scope declaration that
    // isn't a weakref's), or 0 for none; but for a weakref whose name names none, the object of the name's first such
    // weakref, whose symbol it takes (0 for that one, whose own symbol is named after its target).
    uint32_t target_object;
    // Clang's aliasee: the first object along the chain of aliases from the target that isn't an alias (a function, a
    // variable or an ifunc), or 0 for none or a cycle. And the definition the symbol is at: the aliasee, or an aliased
    // ifunc's resolver; 0 where it has an error.
    uint32_t aliasee;
    uint32_t base;
    // The aliasee's definition, for the error's note, or BUSTER_CHECK_NONE; and its name, the last alias's target along
    // the chain, whose spelling an undefined one's notes look for among the program's identifiers.
    uint32_t definition;
    String8 aliasee_name;
    uint8_t kind;
    // The symbol's type (BUSTER_LINK_SYMBOL_*): an ifunc's and an alias of one's IFUNC, else its definition's.
    uint8_t type;
    uint8_t error;
};

// CheckProgram.alias_states, a byte per entry: a weakref's target is referenced through weakrefs alone, which makes its
// undefined symbol weak; an alias's or ifunc's definition isn't emitted (an inline function's, which the selection
// doesn't select), clang's error that it isn't defined.
#define BUSTER_CHECK_PROGRAM_ALIAS_WEAK 1u
#define BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED 2u
// An alias whose thread-local storage isn't its definition's (CHECK_PROGRAM_ALIAS_THREAD_LOCAL) that the selection's code
// refers to (#1849), which the emission refuses: its references would reach it as its declaration says.
#define BUSTER_CHECK_PROGRAM_ALIAS_REFERENCED 4u

// A group of clang's cpu_specific and cpu_dispatch functions of one name (#1447, c_check_program_cpu_dispatch.c): its
// first declaration with an attribute (its name and linkage), its reference object R (its symbol f.ifunc, which every
// reference reaches), its dispatcher's object and declaration with cpu_dispatch (0 and BUSTER_CHECK_NONE for none), and
// its versions, `version_count` of CheckProgram.cpu_versions from `version_first`.
typedef struct CheckProgramCpuGroup CheckProgramCpuGroup;
struct CheckProgramCpuGroup
{
    uint32_t first;
    uint32_t reference;
    uint32_t dispatch;
    uint32_t dispatch_declaration;
    uint32_t version_first;
    uint32_t version_count;
    bool internal;
};

// A cpu_specific version: its object, its first declaration with the attribute (its CPU list) and its group.
typedef struct CheckProgramCpuVersion CheckProgramCpuVersion;
struct CheckProgramCpuVersion
{
    uint32_t object;
    uint32_t declaration;
    uint32_t group;
};

// A function multiversioned by target strings (#1838, c_check_program_target_versions.c): its first declaration with an
// attribute (its name and linkage); for target's versions, its reference object R (f.ifunc, which every reference to
// a version reaches) and its versions, `version_count` of CheckProgram.target_versions from `version_first`; for
// target_clones, its object (f, the IFUNC references reach) and declaration with the attribute, and the object of its
// body's own symbol (the first clone's, f.X.0). `resolved` is whether the unit emits its resolver (target's: where a
// version is referenced; target_clones': where the function is defined or referenced), whose code reads libgcc's CPU
// model (`model`) and its features' second array (`features2`).
typedef struct CheckProgramTargetGroup CheckProgramTargetGroup;
struct CheckProgramTargetGroup
{
    uint32_t first;
    uint32_t reference;
    uint32_t clones;
    uint32_t clones_declaration;
    uint32_t body;
    uint32_t version_first;
    uint32_t version_count;
    // Its resolver's options: its versions, or target_clones' versions with their repetitions.
    uint32_t option_count;
    // target_clones' body copies (check_program_target_copies): where `copies`, its body is compiled once per version
    // that isn't repeated, each with that version's instruction set (CheckProgram.clone_targets from `clone_first`, an
    // entry per option: CheckProgram.body_targets' value) and the copies at their own symbols
    // (CheckProgram.clone_symbols from `clone_first`, 0 for the first option, a repeated one or none).
    uint32_t clone_first;
    // Its body's labels whose addresses the program takes (CheckProgram.clone_label_tokens), where it has copies.
    uint32_t label_first;
    uint32_t label_count;
    bool copies;
    bool internal;
    bool resolved;
    bool model;
    bool features2;
};

// A version of target's: its object and its first declaration with the attribute (its string).
typedef struct CheckProgramTargetVersion CheckProgramTargetVersion;
struct CheckProgramTargetVersion
{
    uint32_t object;
    uint32_t declaration;
};

// One census's candidates' references (#2536's step 2.1, check_program_census_walk's or _scan's): each body's, sorted, from
// first[body], counts[body] of them; the bodies whose candidates' references aren't their own (differing), those of them
// without a dropped arm (unarmed) and those whose candidates miss one of their own (missing: none, for the superset the
// candidates must be); and the census's TSC ticks.
typedef struct CheckProgramCensus CheckProgramCensus;
struct CheckProgramCensus
{
    uint32_t* references;
    uint32_t* first;
    uint32_t* counts;
    uint32_t differing;
    uint32_t unarmed;
    uint32_t missing;
    uint64_t ticks;
};

typedef struct CheckProgram CheckProgram;
struct CheckProgram
{
    const CheckInput* input;
    const CheckStream* stream;
    const TypeTable* types;
    const Parser* parser;
    const InternTable* identifiers;
    const CheckInitializers* initializers;
    // Per initializer plan, whether an executable VALUE reaches this automatic compound literal. Null without any
    // automatic compound literal; unevaluated operands retain their diagnostic plans without needing constant data.
    uint8_t* compound_initializers;
    const CheckAsms* asms;
    CheckControl* controls;
    // The SWITCH controls followed by four CASE controls or more, which a jump table may dispatch
    // (check_lower_switch_tables): `switch_count` of them, by their index in `controls`, in its order.
    uint32_t* switches;
    CheckProgramBody* bodies;
    CheckProgramCase* cases;
    CheckProgramRegion* regions;
    // Per region, whether its body's references reach it (check_program_references, #1108): a statement expression in an
    // operand that a constant condition never evaluates, in sizeof's or in code the writer left out is reached by none,
    // and no code emits the block-scope statics it declares (check_program_select_mode). Null in a unit without regions.
    uint8_t* region_reached;
    uint32_t* references;
    // With CheckProgramOptions.selection_census (#2536's step 2.1), the candidates' references by the walk's census and by
    // the scan's (check_program_census_walk and _scan), the bodies with a dropped arm, and the TSC ticks of the references
    // pass (check_program's per-body check_program_references and its sort), which the censuses' ticks are set against.
    // Zero without the option; the scans' are zero where they have none.
    CheckProgramCensus census_walk;
    CheckProgramCensus census_scan;
    // The scan's without the writer's dropped arms (check_program_census_left_out) and the operands the walk leaves out.
    CheckProgramCensus census_arms;
    // The same as the pass before the bodies' controls finds it (#2536's step 2.2): its own constant ifs
    // (check_program_constant_arms, census_pass_arms ranges of them), not the writer's.
    CheckProgramCensus census_pass;
    uint32_t census_pass_arms;
    uint32_t census_dropped_bodies;
    uint64_t census_references_ticks;
    // The objects static data's initializers refer to (CheckProgramOptions.initializer_references, borrowed): what keeps
    // a function whose calls --tier1 expanded (#2541 step 2, check_tier1_inline_rooted).
    const uint32_t* initializer_references;
    uint32_t initializer_reference_count;
    uint32_t* builtin_nodes;
    uint32_t* asm_offsets;
    uint32_t* asm_targets;
    SymbolId* object_symbols;
    uint32_t* symbol_objects;
    uint32_t* object_declarations;
    // Per object, its first declaration with GNU's naked attribute (not ignored), which any declaration of a function may
    // hold, and BUSTER_CHECK_NONE for the others; null in a unit without one, the usual one. A function is naked when that
    // declaration is at or before its definition, as clang takes it (check_program_asm_naked).
    uint32_t* object_naked;
    // Per object, its first declaration with x86's interrupt attribute (#147, not ignored), and BUSTER_CHECK_NONE for the
    // others; null in a unit without one, the usual one. A function's body is an interrupt service routine's when that
    // declaration is at or before its definition, as clang takes it (check_program_interrupt): the lowering's writer reads
    // it of the body it lowers.
    uint32_t* object_interrupt;
    // Per object, its first declaration with x86's force_align_arg_pointer attribute (#1447, not ignored), and
    // BUSTER_CHECK_NONE for the others; null in a unit without one, the usual one. A function's body realigns the stack
    // in its prologue when that declaration is at or before its definition (check_program_realign), as object_interrupt's.
    uint32_t* object_realign;
    // The function declarations with a patchable_function_entry attribute (#1447, not ignored), as keys of their object in
    // the high half and their token in the low, sorted (check_program_patchable_keys); null in a unit without one, the
    // usual one. A body takes the first attribute of its function's last such declaration up to its definition
    // (check_program_patchable), as clang's redeclarations inherit it where they have none of their own.
    uint64_t* patchable_keys;
    uint32_t patchable_key_count;
    // CheckProgramOptions.patchable_count, patchable_offset and patchable_section: -fpatchable-function-entry's.
    uint32_t patchable_count;
    uint32_t patchable_offset;
    String8 patchable_section;
    // Per object, whether a declaration of the function has GNU's always_inline attribute (BUSTER_CHECK_PROGRAM_INLINE_ALWAYS)
    // or noinline (BUSTER_CHECK_PROGRAM_INLINE_NEVER), not ignored; null in a unit without either. --tier1's inliner
    // (#1282, c_check_tier1_inline.c) expands the calls of a defined function of internal linkage with the first and not
    // the second: the x86 intrinsics' wrappers.
    uint8_t* object_inline;
    // Whether any declaration of a function has returns_twice. Whole-object call initializers (#1549) use this to
    // retain their separate result object in bodies that may reenter them. Null where no declaration has it.
    uint8_t* object_returns_twice;
    // Per object, whether a declaration of it has an asm label, not ignored: clang's code calls a library builtin's
    // declaration of one by its label rather than compute it (#1373, check_program_call_computed). Made before the
    // references' pass (check_program_labelled_objects); null where no declaration has one.
    uint8_t* object_labelled;
    // GNU's alias, ifunc and weakref attributes (#147, check_program_aliases): an entry per object a file-scope declaration
    // gives one, in object order; null in a unit without one, the usual one. A selection shares them, but for their states
    // (BUSTER_CHECK_PROGRAM_ALIAS_*, a byte each), which are each program's: its own references and selection decide them.
    // An alias's or ifunc's object has a symbol, which the emission defines at its base's definition once the static
    // data's atoms exist (check_program_sections), and a referenced weakref's object has its target's (object_symbols),
    // or one of its own, named as its target, where no object of the unit has that name.
    CheckProgramAlias* aliases;
    uint8_t* alias_states;
    uint32_t alias_count;
    // Asm labels that name another object's symbol (#1979, check_program_label_merges): one symbol per name, as clang's
    // module has one global per name. Keys of an object in the high half and the object whose symbol it takes (its name's
    // definition, else its first tentative definition, else its first object) in the low, sorted by object; null in a unit
    // without them, the usual one, and shared by a selection. label_conflict is one more than the token of a function's
    // second definition of a name, clang's error at code generation, and label_previous the first's; label_refused one
    // more than the token of a variable's second definition of a name (0 for none).
    uint64_t* label_merges;
    uint32_t label_merge_count;
    uint32_t label_conflict;
    uint32_t label_previous;
    uint32_t label_refused;
    String8 label_conflict_name;
    // clang's cpu_specific and cpu_dispatch multiversioning (#1447, c_check_program_cpu_dispatch.c): the unit's groups and
    // versions, null in a unit without them, the usual one; a selection shares them. And each program's own symbols of the
    // versions (cpu_version_count of them, 0 for a version without a body) and of the groups' dispatchers (after them, 0
    // for none): their objects take their groups' reference symbols, as references reach them.
    CheckProgramCpuGroup* cpu_groups;
    CheckProgramCpuVersion* cpu_versions;
    SymbolId* cpu_symbols;
    uint32_t cpu_group_count;
    uint32_t cpu_version_count;
    // Multiversioning by target strings (#1838, c_check_program_target_versions.c): the unit's groups and target's
    // versions, null in a unit without them; and each version's own symbol (target_version_count of them, 0 for a version
    // without a body), then each group's body symbol (target_clones', 0 for none).
    CheckProgramTargetGroup* target_groups;
    CheckProgramTargetVersion* target_versions;
    SymbolId* target_symbols;
    uint32_t target_group_count;
    uint32_t target_version_count;
    // target_clones' copies (CheckProgramTargetGroup.copies): each group's options' instruction sets, whether each has a
    // copy of its own (not the first, nor a repeated one) and their symbols, from its clone_first; and per body, the option a copy compiles (0 for every other body; null where the program has no copy).
    uint32_t* clone_targets;
    uint8_t* clone_copied;
    SymbolId* clone_symbols;
    uint32_t clone_option_count;
    uint32_t* body_options;
    // The copies' symbols of their body's labels whose addresses the program takes (#2247, check_program_target_number):
    // by option from clone_first, the first of its labels' symbols, in their tokens' order, or 0 where the option's code
    // takes the labels' own (the last option with code, as clang's static data reaches the last clone emitted); and the
    // groups' labels, each group's `label_count` from its `label_first`, by token. Null in a unit without them.
    SymbolId* clone_label_symbols;
    uint32_t* clone_label_tokens;
    // The exits through cleanups' chains (#1370, CHECK_CONTROL_EXIT's), each record's plan (by its CheckInput.cleanups
    // index, of a body with such an exit) and the destinations of the records' dispatches; null in a unit without cleanups.
    CheckProgramExit* exits;
    CheckProgramChain* chains;
    CheckProgramStop* chain_stops;
    uint32_t exit_count;
    uint32_t chain_stop_count;
    uint32_t control_count;
    uint32_t switch_count;
    uint32_t body_count;
    uint32_t case_count;
    uint32_t region_count;
    uint32_t reference_count;
    uint32_t builtin_count;
    uint32_t asm_target_count;
    uint32_t object_count;
    uint32_t symbol_count;
    // The unit's constant CFStrings' calls (#148, CheckDataPrepared's), each of which takes its canonical call's symbol
    // (check_program_alias_number), and the object of the __CFConstantStringClassReference they point to, which is the
    // program's own where `cfstring_class_own` (the emission names it, an undefined global).
    const CheckDataCFString* cfstrings;
    uint32_t cfstring_count;
    uint32_t cfstring_class;
    bool cfstring_class_own;
    uint32_t token;
    uint32_t diagnostic_position;
    CheckProgramFailure failure;
    CheckProgramStatus status;
    // check_program_diagnose_all's list of every constraint error, in clang's order within a statement; null elsewhere,
    // where only the first is kept (`token`, `diagnostic_position`, `failure`).
    CheckProgramConstraint* constraints;
    uint32_t constraint_count;
    uint32_t constraint_capacity;
    uint64_t output_bytes;
    // CheckProgramOptions.unselected_operands, asm_helpers, asm_bodies, helper_forms, target, frame_pointer and probe_stack.
    bool unselected_operands;
    bool asm_helpers;
    bool asm_bodies;
    uint32_t helper_forms;
    X86Target target;
    uint8_t frame_pointer;
    bool probe_stack;
    // The tokens the writer left out (#1108), as ranges, keys of their first token in the high half and their last in
    // the low, in token order: an if whose condition is an integer constant without side effects writes only the arm
    // it runs, without the condition, when no label or case of an outer switch enters the other, as clang -O0's code
    // has it (CodeGenFunction::EmitIfStmt and ContainsLabel), and its condition's parentheses and the other arm are
    // two ranges. The ranges are apart, none inside another, as code left out is never expanded. What only such code
    // refers to isn't selected (check_program_select_mode), nor a block-scope static declared there, which clang emits
    // only where its declaration's statement is emitted, `used` or not; and the lowering gives the locals declared
    // there no place (check_lower_prepare).
    uint64_t* dropped_ranges;
    uint32_t dropped_range_count;
    // Per body, the instruction set of its function (#180), by its target attribute: 0 for the unit's (`target`), n for
    // targets[n - 1], or BUSTER_CHECK_PROGRAM_TARGET_VERSIONS for one of several versions (GNU's function
    // multiversioning: target_clones, or target attributes of other strings, which the lowering refuses); null where no
    // body has an attribute that applies (check_program_targets).
    uint32_t* body_targets;
    X86Target* targets;
    uint32_t target_count;
    // The function declarations with a target, target_clones, always_inline, flatten or noreturn attribute, as keys of
    // their object in the high half and their token in the low, sorted by object (per object, the target attributes' in
    // token order, then the others'), for code generation's check of target features at -c (#775,
    // check_program_target_features), or a late symbol rename's emission walk. Other units retain no keys.
    uint64_t* attribute_keys;
    uint32_t attribute_key_count;
    // Each body's source labels' places, its controls' indices where code after them begins, in the order the writer
    // expands their statements (CheckProgramBody.label_first): code generation's check of target features walks from
    // each, as clang emits a label's block whatever code precedes it. Null in a unit without a label.
    uint32_t* label_positions;
    uint32_t label_position_count;
    // The host's features (predefines_host_of), where check_program_targets read them: they are the CPU's, which
    // -march=native reads, and code generation's check of target features takes them from here.
    PredefinesHost host;
    bool host_read;
    // Per body of a selection (check_program_select_mode), whether clang's code generation emits its function where the
    // unit defines it rather than when its emission first refers to it or at the unit's end (ASTContext::
    // DeclMustBeEmitted, as CodeGenModule::EmitGlobal asks it when the parser hands over the definition): an external
    // definition, by the selection's rules of GNU's and C99's inline functions, or a function with a used, constructor
    // or destructor attribute, both by the function's declarations up to the definition (check_program_body_eager),
    // not by the unit's final ones: a later declaration that makes an inline definition external (C99's
    // `extern int f(int);`) is no emission point, it queues the function for the unit's end, where the deferred ones
    // are emitted. Code generation's check of target features takes clang's order of emission from it (#775), and the
    // lowering of a lock-free query whose answer is its function's emission's takes that emission's place
    // (CheckBuiltinAtomicEffect.late_completion); null in a program that no selection made, or in a unit without a
    // target attribute, an x86 builtin's call (#148), such a query, or a symbol rename after an earlier use.
    uint8_t* body_eager;
    // For a unit with a redefine_extname after an earlier use, one byte per retained symbol pragma: whether an
    // earlier emitted reference had already fixed that object's original spelling. The selection owns this cold
    // view; the input's asm-label facts remain unchanged for syntax and type checks.
    uint8_t* symbol_pragma_originals;
    // With CheckProgramOptions.lifetimes (#177): each block's local declarations whose lifetimes the controls give, from
    // GNU's weak attribute and `#pragma weak` after a use (#1849): the weak declarations of the objects none of whose
    // weak declarations is its first declaration's own (a `#pragma weak` after the declaration it attaches to being
    // late too), as keys of their object in the high half and their token in the low, sorted by key, and beside each
    // where clang applies it (weak_late_points: the declaration's token, or its pragma's when that is later); null in
    // the other units (check_program_weak_late). Clang's code generation gives such an object's symbol the binding of
    // the declaration its first emitted reference names (or its acting tentative definition), so the selection decides
    // which keep their weak binding (check_program_weak_bindings): weak_globals lists the objects whose symbols it makes
    // global, and weak_refused is one more than the token of a declaration whose binding it can't decide (0 for none);
    // weak_decided says it decided them (a program no selection made keeps every weak declaration's binding).
    uint64_t* weak_late_keys;
    uint32_t* weak_late_points;
    uint32_t weak_late_count;
    uint32_t* weak_globals;
    uint32_t weak_global_count;
    uint32_t weak_refused;
    bool weak_decided;
    // lifetime_first (by the block's closing brace, BUSTER_CHECK_NONE for a block without them) through lifetime_next (by
    // declaration), in token order; null otherwise.
    uint32_t* lifetime_first;
    uint32_t* lifetime_next;
    // The lifetime events the lowering makes of the controls at most: one per local of each LIFETIME control's block.
    uint32_t lifetime_event_count;
    // The rooms the setup reserves for the controls and the chains' stops (check_program_begin), which bound their counts
    // before the loop has written them: a lowering that reads no control total takes them (CheckLowerShared.deferred).
    // Last, as every field this change adds to a structure, so that no other field's offset moves.
    uint32_t control_capacity;
    uint32_t chain_stop_capacity;
    // The builtin nodes the candidates' pass reaches (#2536's step 2.2c.3, part 2: check_program_census_builtins), in node
    // order: a superset of the bodies' own (CheckProgram.builtin_nodes) that needs no body's controls; null without the
    // pass.
    uint32_t* census_pass_builtins;
    uint32_t census_pass_builtin_count;
    // The initializers' plans and the asm statements the candidates' pass reaches (#2536's step 2.2c.3, part 3:
    // check_program_census_statements), in token order, by index: a superset of the bodies' INITIALIZE and ASM controls'
    // targets that needs no body's controls; null without the pass.
    uint32_t* census_pass_initializers;
    uint32_t census_pass_initializer_count;
    uint32_t* census_pass_asms;
    uint32_t census_pass_asm_count;
    // The lists the lowering's pools and asm plan take in the controls' place, where they aren't null (#2536's step
    // 2.2c.3, part 3, a provisional selection's: check_driver_provisional_check): the plans whose INITIALIZE controls
    // the bodies would have (check_lower_init_pools) and the asm statements whose ASM controls they would have, in
    // token order (check_program_asm_plan, check_program_asm_sequence). The compound literals' plans are
    // compound_initializers'.
    const uint32_t* initialize_list;
    uint32_t initialize_list_count;
    const uint32_t* asm_list;
    uint32_t asm_list_count;
    // Whether a target group's state (CheckProgramTargetGroup.resolved) over the candidates' references, which the tail
    // before the loop takes (--provisional-selection, #2536's step 2.2c.2), differs from the one over the bodies' own,
    // which check_program_finish makes again: a window's lowering would have taken the first.
    bool target_states_differ;
    // The candidates' pass's left-out ranges (check_program_constant_arms), census_pass_arms of them, sorted and
    // joined, as the writer's dropped_ranges, kept with the pass: the ranges a selection made before the loop and its
    // lowering's locals take (--provisional-selection, #2536's step 2.2c.4, part 1).
    uint64_t* census_pass_ranges;
};

#define BUSTER_CHECK_PROGRAM_TARGET_VERSIONS UINT32_MAX

typedef enum CheckControlTaskKind
{
    CHECK_CONTROL_TASK_STATEMENT,
    CHECK_CONTROL_TASK_TOKEN,
    CHECK_CONTROL_TASK_ENTRY,
    CHECK_CONTROL_TASK_LABEL,
} CheckControlTaskKind;

typedef struct CheckControlTask CheckControlTask;
struct CheckControlTask
{
    CheckControlTaskKind kind;
    CheckControl entry;
    uint32_t token;
    uint32_t stop;
    uint32_t repeat;
};

typedef struct CheckControlWriter CheckControlWriter;
struct CheckControlWriter
{
    CheckProgram* program;
    const Parser* parser;
    // A resolved branch, asm or case edge enters the implicit END of the main body.
    bool targets_end;
    // The index of the body the writer builds (check_control_build), which its regions record (CheckProgramRegion.body).
    uint32_t body;
    // The body's children lists, by token: each token's first and last child and its siblings, which check_control_begin
    // builds from the body's tokens alone (check_control_children).
    uint32_t* first;
    uint32_t* last;
    uint32_t* next;
    uint32_t* previous;
    const uint32_t* token_first;
    const uint32_t* node_next;
    // Initialized on the first discarded INITIALIZE whose storage duration the control writer needs.
    uint64_t* static_initializers;
    bool static_initializers_ready;
    // Each statement's first initializer plan, where planned has its token's bit.
    const uint32_t* plan_first;
    const uint64_t* planned;
    const uint32_t* plan_next;
    // Whether the unit may have variable-length arrays' bounds or objects (check_program_variable): only then does it have
    // vla_first, allocation_first, allocation_next, vla_blocks and vla_saves, which are null otherwise (#877).
    bool variable;
    const uint32_t* vla_first;
    const uint32_t* vla_next;
    // The variable-length arrays' objects of each statement (by its terminator), in declaration order, and each block's
    // first one's declaration token plus one (by the block's closing token; 0 for a block without them), and the last
    // token before the block's STACK_SAVE plus one: the first one's own bounds' last `]`, or its declaration.
    const uint32_t* allocation_first;
    const uint32_t* allocation_next;
    const uint32_t* vla_blocks;
    const uint32_t* vla_saves;
    // Each asm statement's index by its token, or null for a unit without asm statements.
    const uint32_t* asm_statements;
    const uint32_t* case_first;
    const uint32_t* case_next;
    const uint32_t* case_map;
    // Only LABEL and CLOSE_CASE tokens receive a number.
    uint32_t* source_labels;
    // Ordinary breaks and continues carry their destinations on the statement worklist. Only a lifetime, VLA,
    // cleanup or statement expression can query these token-indexed destinations while walking ancestors.
    bool all_loop_targets;
    bool loop_targets;
    bool scalar_children;
    uint32_t* stops;
    uint32_t* repeats;
    // Each body's first region (by its opening token) and each region's next, or null for a unit without regions.
    const uint32_t* region_first;
    const uint32_t* region_next;
    // The labels a GNU break and continue take in a statement expression that opens at a token, by token, which a token of
    // the body being written keeps where its bit in `region_known` is set (check_control_region_targets), or null for a
    // unit without regions (#2995).
    uint32_t* region_stops;
    uint32_t* region_repeats;
    uint64_t* region_known;
    // CheckProgramOptions.region_target_work, or null.
    uint64_t* region_work;
    // Whether each label's address is taken (&&label), by its token: its place gets a CHECK_CONTROL_LABEL.
    const uint8_t* addressed;
    // The body's cleanups' records (CheckInput.cleanups, cleanup_count of them from cleanup_first), and each record's
    // parent, by its index there: the innermost record whose scope holds its declaration, BUSTER_CHECK_NONE for none.
    uint32_t* cleanup_parents;
    // Each record's outermost record of its own scope, and each scope's (by its opening token) last record, its innermost:
    // BUSTER_CHECK_NONE for a scope without one (#1370).
    uint32_t* cleanup_tops;
    uint32_t* cleanup_scope_last;
    uint32_t cleanup_first;
    uint32_t cleanup_count;
    // CheckProgram.exits while the bodies are written, `exit_capacity` of them.
    CheckProgramExit* exits;
    uint32_t exit_count;
    uint32_t exit_capacity;
    // The statement expression's last statement while its region is written: the region's result, never a statement of
    // its own, also where it is a label's (CheckInputRegion.final_statement).
    uint32_t region_final;
    CheckControl* entries;
    CheckControlTask* tasks;
    uint32_t* labels;
    // CheckProgram.label_positions while the bodies are written, a token's room each: each body's labels' numbers, then,
    // once it is written, their places.
    uint32_t* label_positions;
    uint32_t label_position_count;
    // CheckProgram.switches, and its room: a switch recorded there has four of the program's cases or more.
    uint32_t* switches;
    uint32_t switch_count;
    uint32_t switch_capacity;
    // CheckProgram.dropped_ranges while the bodies are written, in the order the writer leaves them out, a token's room
    // each (a range has a token at least), and per region a byte that says whether a dropped range holds it (null for a
    // unit without regions): such a region's statements are left out too (check_control_build).
    uint64_t* dropped_ranges;
    uint32_t dropped_range_count;
    uint8_t* dropped_regions;
    // check_control_foldable's stack, `fold_capacity` nodes: check_program's node stack, which no other pass uses while
    // the bodies are written.
    // Temporary storage for a known constant switch's nonrecursive source collector.
    Arena* switch_scratch;
    uint32_t* fold_stack;
    uint32_t fold_capacity;
    uint32_t count;
    uint32_t depth;
    uint32_t label_count;
    uint32_t capacity;
    uint32_t first_entry;
    uint32_t opening;
    uint32_t closing;
    uint32_t entry_count;
    // The body's cases: the program's from case_start (check_program_cases numbers them in token order), case_end of them.
    uint32_t case_start;
    uint32_t case_end;
    // With the blocks' lifetimes (#1355, c_check_program_lifetime.c): each token's innermost block with lifetimes above it
    // (check_program_lifetime_entered's chain), and by label where a break's or continue's walk to it ends, its loop's or
    // switch's token or the body's closing brace, BUSTER_CHECK_NONE until a jump asks (check_control_lifetime_stop); both
    // null otherwise.
    const uint32_t* lifetime_nearest;
    uint32_t* lifetime_stops;
    // The steps of the walks to a break's or continue's stop and of the walks along lifetime_nearest
    // (CheckProgramOptions.lifetime_work).
    uint64_t lifetime_steps;
};

static void check_program_fail(CheckProgram* program, CheckProgramStatus status, uint32_t token)
{
    if (program->status == CHECK_PROGRAM_OK)
    {
        program->status = status;
        program->token = token;
        program->diagnostic_position = token;
    }
}

// The attributes of a declaration that the static objects' pass reads (check_program_static_object), as bits.
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_NAKED 1u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET 2u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_CALLEE 4u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALIAS 8u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALWAYS_INLINE 16u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_NOINLINE 32u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_INTERRUPT 64u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_REALIGN 128u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_RETURNS_TWICE 256u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_PATCHABLE 512u
// CheckProgram.object_inline's bits.
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_WEAK 1024u
#define BUSTER_CHECK_PROGRAM_ATTRIBUTE_ASM_LABEL 2048u
#define BUSTER_CHECK_PROGRAM_INLINE_ALWAYS 1u
#define BUSTER_CHECK_PROGRAM_INLINE_NEVER 2u

// Which of them a declaration's `attributes` hold, by one pass over its records that aren't ignored: GNU's naked
// attribute (CheckProgram.object_naked), a target or target_clones attribute (CheckProgramTargetEvents), one of a
// callee's that code generation's check of target features reads, always_inline, flatten or noreturn (#775,
// CheckProgram.attribute_keys), an alias, ifunc or weakref attribute (#147, CheckProgram.aliases), and x86's interrupt
// (#147, CheckProgram.object_interrupt), force_align_arg_pointer (#1447, CheckProgram.object_realign), returns_twice
// (#1549, CheckProgram.object_returns_twice), patchable_function_entry (#1447, CheckProgram.patchable_keys) and weak
// (#1849, CheckProgram.weak_late_keys).
static uint32_t check_program_attribute_bits(const CheckInputAttributes* attributes)
{
    uint32_t bits = 0;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = &attributes->records[i];
        const CheckInputAttributeKind kind = attribute->kind;
        const uint32_t bit = kind == CHECK_ATTRIBUTE_NAKED ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_NAKED :
                             kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES || kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                                 kind == CHECK_ATTRIBUTE_CPU_DISPATCH ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET :
                             kind == CHECK_ATTRIBUTE_ALWAYS_INLINE || kind == CHECK_ATTRIBUTE_FLATTEN || kind == CHECK_ATTRIBUTE_NORETURN ?
                                 BUSTER_CHECK_PROGRAM_ATTRIBUTE_CALLEE :
                             kind == CHECK_ATTRIBUTE_ALIAS || kind == CHECK_ATTRIBUTE_IFUNC || kind == CHECK_ATTRIBUTE_WEAKREF ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALIAS :
                                 0;
        const uint32_t inlining = kind == CHECK_ATTRIBUTE_ALWAYS_INLINE ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALWAYS_INLINE : kind == CHECK_ATTRIBUTE_NOINLINE ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_NOINLINE : 0;
        const uint32_t interrupt = kind == CHECK_ATTRIBUTE_INTERRUPT && !attribute->subject_ignored ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_INTERRUPT : 0;
        const uint32_t realign = kind == CHECK_ATTRIBUTE_FORCE_ALIGN_ARG_POINTER ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_REALIGN : 0;
        const uint32_t twice = kind == CHECK_ATTRIBUTE_RETURNS_TWICE && !attribute->subject_ignored ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_RETURNS_TWICE : 0;
        const uint32_t patchable = kind == CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY && !attribute->subject_ignored ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_PATCHABLE : 0;
        const uint32_t weak = kind == CHECK_ATTRIBUTE_WEAK ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_WEAK : 0;
        const uint32_t label = kind == CHECK_ATTRIBUTE_ASM_LABEL ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_ASM_LABEL : 0;
        bits |= attribute->ignored ? 0 : bit | inlining | interrupt | realign | twice | patchable | weak | label;
    }
    return bits;
}

// The static functions' declarations with a target or target_clones attribute (#180), which check_program_targets
// resolves each body's instruction set from: keys of their object in the high half and their token in the low, in each
// object's declarations' order, in `scratch`, which grows them by doubling. A unit without one, the usual one, has none.
// Beside them, the declarations with an always_inline, flatten or noreturn attribute, as keys of the same form, which
// code generation's check of target features or a late symbol rename's emission walk reads (#775,
// CheckProgram.attribute_keys); the file-scope declarations with an alias, ifunc or weakref attribute, which
// check_program_aliases makes the unit's entries of (#147, CheckProgram.aliases); the function declarations with a
// patchable_function_entry attribute (#1447, CheckProgram.patchable_keys); the declarations with a weak attribute,
// `#pragma weak`'s among them (#1849, check_program_weak_late); and the declarations with linkage and an asm label
// (#1979, check_program_label_merges).
typedef struct CheckProgramTargetEvents CheckProgramTargetEvents;
struct CheckProgramTargetEvents
{
    Arena* scratch;
    uint64_t* keys;
    uint32_t count;
    uint32_t capacity;
    uint64_t* callee_keys;
    uint32_t callee_count;
    uint32_t callee_capacity;
    uint64_t* alias_keys;
    uint32_t alias_count;
    uint32_t alias_capacity;
    uint64_t* patchable_keys;
    uint32_t patchable_count;
    uint32_t patchable_capacity;
    bool room;
    uint64_t* weak_keys;
    uint32_t weak_count;
    uint32_t weak_capacity;
    uint64_t* label_keys;
    uint32_t label_count;
    uint32_t label_capacity;
    // The bodies' references the target groups read (check_program_target_referenced) when the tail runs before the loop
    // has written CheckProgram.references (#2536's step 2.2c.2): the candidates' (CheckProgram.census_pass); null after.
    const CheckProgramCensus* census;
};

// clang's cpu_specific and cpu_dispatch multiversioning (#1447), defined in c_check_program_cpu_dispatch.c.
static bool check_program_cpu_groups(CheckProgram* program, const CheckProgramTargetEvents* events, Arena* output);
static void check_program_cpu_needed(const CheckProgram* program, uint8_t* needed);
// GNU's weak attribute and `#pragma weak` after a use (#1849, CheckProgram.weak_late_keys), in
// c_check_program_symbol_pragma.c.
static bool check_program_weak_late(CheckProgram* program, const CheckProgramTargetEvents* events, bool scalar, Arena* output, Arena* scratch);
static bool check_program_cpu_number(CheckProgram* program, Arena* output);
static String8 check_program_cpu_version_features(const CheckProgram* program, const CheckInputAttribute* attribute, Arena* output);
static uint64_t check_program_cpu_features_write(const CheckProgram* program, const CheckInputAttribute* attribute, char* text, uint64_t capacity);
static SymbolId check_program_body_symbol(const CheckProgram* program, uint32_t b);
static SymbolId check_program_target_body_symbol(const CheckProgram* program, uint32_t b);
// Multiversioning by target strings (#1838), defined in c_check_program_target_versions.c.
static bool check_program_target_groups(CheckProgram* program, const CheckProgramTargetEvents* events, const uint32_t* initializer_references,
                                        uint32_t initializer_reference_count, Arena* output);
static void check_program_target_states(CheckProgram* program, const uint32_t* initializer_references, uint32_t initializer_reference_count);
static void check_program_target_needed(const CheckProgram* program, uint8_t* needed);
static bool check_program_target_number(CheckProgram* program, Arena* output);
static bool check_program_target_copies(CheckProgram* program, const PredefinesOptions* predefines, Arena* output);
// The room a cpu_specific version's features take (check_program_cpu_features_write): more than any CPU's, 712 bytes in
// c_check_cpu_dispatch.inc.
#define BUSTER_CHECK_PROGRAM_CPU_FEATURES 1024u

// Appends the key of `object`'s declaration at `token` to one of the events' arrays, which grows by doubling. It and
// check_program_attribute_keys stay out of line: few declarations reach them, and inlined, they changed how clang
// compiled the passes around them, so that -c of sqlite3.c, which has no target attribute, ran about 400,000 more
// instructions, 0.03% (#775).
__attribute__((noinline)) static void check_program_event_push(CheckProgramTargetEvents* events, uint64_t** keys, uint32_t* count, uint32_t* capacity, uint32_t object, uint32_t token)
{
    if (events->room && *count == *capacity)
    {
        const uint32_t grown = *capacity == 0 ? 64 : *capacity <= UINT32_MAX / 2 ? *capacity * 2 : 0;
        uint64_t* moved = grown != 0 ? BUSTER_ARENA_PUSH_ARRAY(events->scratch, uint64_t, grown) : 0;
        events->room = moved != 0;
        if (events->room)
        {
            if (*count != 0)
            {
                memcpy(moved, *keys, (uint64_t)*count * sizeof(uint64_t));
            }
            *keys = moved;
            *capacity = grown;
        }
    }
    if (events->room)
    {
        (*keys)[*count] = ((uint64_t)object << 32) | token;
        *count += 1;
    }
}

static void check_program_target_event(CheckProgramTargetEvents* events, uint32_t object, uint32_t token)
{
    check_program_event_push(events, &events->keys, &events->count, &events->capacity, object, token);
}

// Compact builtin descriptors start with their stable graph node and are source ordered in both schedulers.
static uint32_t check_program_builtin_index(const void* items, uint32_t count, uint32_t stride, uint32_t node)
{
    uint32_t first = 0;
    uint32_t end = count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const uint32_t current = *(const uint32_t*)((const uint8_t*)items + (uint64_t)middle * stride);
        if (current < node)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    const bool found = first < count && *(const uint32_t*)((const uint8_t*)items + (uint64_t)first * stride) == node;
    const uint32_t result = found ? first : BUSTER_CHECK_NONE;
    return result;
}

static const CheckOverloadResult* check_program_overload_address(const CheckProgram* program, uint32_t node, uint32_t operand)
{
    const CheckStream* stream = program->stream;
    uint32_t first = 0;
    uint32_t end = stream->overload_address_count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const CheckOverloadAddressEffect* effect = stream->overload_addresses + middle;
        if (effect->node < node || (effect->node == node && effect->operand < operand))
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    const CheckOverloadAddressEffect* effect = first < stream->overload_address_count ? stream->overload_addresses + first : NULL;
    const CheckOverloadResult* selected = effect != NULL && effect->node == node && effect->operand == operand ? &effect->selected : NULL;
    return selected;
}

static bool check_program_overload_valid(const CheckProgram* program, const CheckOverloadResult* selected)
{
    const bool valid = selected->status == CHECK_OVERLOAD_OK && selected->object != 0 && selected->object < program->object_count &&
                       type_kind(program->types, type_strip_aligned(program->types, selected->type)) == BUSTER_TYPE_KIND_FUNCTION;
    return valid;
}

static uint32_t check_program_argument_mask(CheckProgram* program, uint32_t node)
{
    const CheckOp op = program->input->graph.ops[node];
    const CheckStream* stream = program->stream;
    uint32_t mask = UINT32_MAX;
    uint32_t index = BUSTER_CHECK_NONE;
    if (op == CHECK_OP_BUILTIN_MISC)
    {
        index = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
        mask = index != BUSTER_CHECK_NONE ? stream->misc[index].evaluated_operands : 0;
    }
    else if (op == CHECK_OP_BUILTIN_FLOAT)
    {
        index = check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node);
        // This is a reference union, not an execution schedule: every possible runtime selector belongs here.
        mask = index != BUSTER_CHECK_NONE ? stream->floating[index].evaluated_operands : 0;
    }
    else if (op == CHECK_OP_BUILTIN_QUERY)
    {
        index = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), node);
        mask = index != BUSTER_CHECK_NONE ? stream->queries[index].evaluated_operands : 0;
    }
    else if (op == CHECK_OP_BUILTIN_ATOMIC)
    {
        index = check_program_builtin_index(stream->atomics, stream->atomic_count, sizeof(CheckBuiltinAtomicEffect), node);
        mask = index != BUSTER_CHECK_NONE ? stream->atomics[index].evaluated_operands : 0;
    }
    else if (op == CHECK_OP_BUILTIN_VECTOR)
    {
        index = check_program_builtin_index(stream->vectors, stream->vector_count, sizeof(CheckBuiltinVectorEffect), node);
        const uint32_t count = index != BUSTER_CHECK_NONE ? stream->vectors[index].evaluated_argument_count : 0;
        if (count > BUSTER_CHECK_BUILTIN_VECTOR_ARGUMENTS || count > program->input->graph.argument_counts[node])
        {
            check_program_fail(program, CHECK_PROGRAM_INVALID, program->input->graph.tokens[node]);
        }
        mask = count <= BUSTER_CHECK_BUILTIN_VECTOR_ARGUMENTS ? (1u << count) - 1 : 0;
    }
    else if (op == CHECK_OP_BUILTIN_X86)
    {
        index = check_program_builtin_index(stream->x86, stream->x86_count, sizeof(CheckBuiltinX86Effect), node);
    }
    else if (op == CHECK_OP_BUILTIN_BIT_CAST)
    {
        index = check_program_builtin_index(stream->bit_casts, stream->bit_cast_count, sizeof(CheckBuiltinBitCastEffect), node);
        mask = index != BUSTER_CHECK_NONE ? stream->bit_casts[index].evaluated_operands : 0;
    }
    if (op != CHECK_OP_CALL && index == BUSTER_CHECK_NONE)
    {
        check_program_fail(program, CHECK_PROGRAM_INVALID, program->input->graph.tokens[node]);
    }
    return mask;
}

static bool check_program_query_known(const CheckProgram* program, uint32_t node, const CheckBuiltinQueryEffect* effect)
{
    const bool constant = program->input->graph.members[node] == CBUILTIN_CONSTANT_P;
    const bool plan = !constant || effect->runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP ||
                      effect->runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
    // Query `known` describes the source proof or object bound. Runtime sentinels and strict constant_p plans
    // have their own value, even when the source proof differs or the selected object bound is unknown.
    const bool known = effect->unsupported == 0 && plan && effect->runtime_value.constant_kind != CHECK_CONSTANT_NONE &&
                       effect->runtime_value.fault == CHECK_FAULT_NONE;
    return known;
}

static bool check_program_runtime_effects(const CheckProgram* program, uint32_t node)
{
    const CheckStream* stream = program->stream;
    bool effects = (check_stream_flags(stream, node) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0;
    if (program->input->graph.ops[node] == CHECK_OP_BUILTIN_QUERY)
    {
        const uint32_t selected = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), node);
        if (selected != BUSTER_CHECK_NONE)
        {
            const CheckBuiltinQueryEffect* effect = stream->queries + selected;
            effects |= effect->evaluated_operands != 0 || effect->runtime_effects.flags != 0 ||
                       effect->runtime_effects.reads_fp_environment || effect->runtime_effects.may_raise_fp_exceptions;
        }
    }
    return effects;
}

static bool check_program_runtime_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    const CheckGraph* graph = &program->input->graph;
    // A condition that is no constant and no query has no truth to know (check_action_truth), which its columns say
    // without the copy of its whole value.
    const bool candidate = graph->ops[condition] == CHECK_OP_BUILTIN_QUERY || check_stream_constant_kind(program->stream, condition) != CHECK_CONSTANT_NONE;
    const CheckValue checked_value = candidate ? check_stream_value(program->stream, condition) : (CheckValue){ 0 };
    const CheckValue* value = &checked_value;
    const bool environment = graph->fp_environments != NULL && (graph->fp_environments[node] & BUSTER_CHECK_FP_ENV_ACCESS) != 0;
    bool runtime = false;
    bool available = true;
    if (graph->ops[condition] == CHECK_OP_BUILTIN_QUERY)
    {
        const CheckStream* stream = program->stream;
        const uint32_t selected = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), condition);
        available = selected != BUSTER_CHECK_NONE && check_program_query_known(program, condition, stream->queries + selected);
        if (available)
        {
            value = &stream->queries[selected].runtime_value;
            runtime = true;
        }
    }
    // A source constant proves a translation-time value. Runtime floating truth conversions can still observe
    // the environment or raise an exception, including a signaling NaN whose bits are known here.
    const bool known = candidate && available && (runtime || ((value->flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                                   !(environment && check_action_floating_type(program->types, value->type)))) &&
                       value->fault == CHECK_FAULT_NONE &&
                       check_action_truth(program->types, value, truth);
    return known;
}

// When a cast did not produce a constant, the nonzero address of a symbol cannot
// decide the converted value. Its low bits may all be zero after truncation.
static bool check_program_branch_cast(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t operand = graph->operand0[node];
    const bool unproved = check_stream_constant_kind(stream, node) == CHECK_CONSTANT_NONE && operand < graph->count &&
                          check_stream_constant_kind(stream, operand) == CHECK_CONSTANT_ADDRESS;
    return !unproved;
}

// A branch condition's truth as clang's CFG builder prunes its edges (#1447, CFGBuilder::tryEvaluateBool, whose
// EvaluateAsBooleanCondition ignores side effects), which the -Wreturn-type walk and the runtime warnings' reachability
// (check_common_body_reached) model: a known value's (check_program_runtime_truth), and a logical operator's that one
// operand decides though the other isn't known, `0 && x` and `1 || x` by the left and `x && 0` and `x || 1` by the right,
// or both together, through the condition's own node, conversions, casts and `!`. A conversion keeps the truth of the
// 0 or 1 such an operator gives, the one value it isn't already known for. A post-order walk on an explicit stack (the
// call graph's no-recursion rule), BUSTER_CHECK_BRANCH_TRUTH_DEPTH operands deep at most: each frame's node, its depth and
// whether its operands are pushed, and each value 0 (false), 1 (true) or 2 (unknown), the left operand's first.
#define BUSTER_CHECK_BRANCH_TRUTH_DEPTH 64u
#define BUSTER_CHECK_BRANCH_TRUTH_FRAMES (2u * BUSTER_CHECK_BRANCH_TRUTH_DEPTH + 2u)
static bool check_program_branch_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t frames[BUSTER_CHECK_BRANCH_TRUTH_FRAMES];
    uint8_t levels[BUSTER_CHECK_BRANCH_TRUTH_FRAMES];
    uint8_t expanded[BUSTER_CHECK_BRANCH_TRUTH_FRAMES];
    uint8_t values[BUSTER_CHECK_BRANCH_TRUTH_FRAMES];
    uint32_t top = 1;
    uint32_t count = 0;
    frames[0] = condition;
    levels[0] = 0;
    expanded[0] = 0;
    while (top != 0)
    {
        const uint32_t at = frames[top - 1];
        const uint8_t level = levels[top - 1];
        const CheckOp op = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
        const bool cast = op == CHECK_OP_CONVERT || op == CHECK_OP_CAST;
        const bool unary = op == CHECK_OP_CONDITION || cast || op == CHECK_OP_NOT;
        const bool binary = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
        if (expanded[top - 1] == 0)
        {
            bool value = false;
            const bool known = at < graph->count && check_program_runtime_truth(program, node, at, &value);
            if (!known && level < BUSTER_CHECK_BRANCH_TRUTH_DEPTH && (unary || binary) && (!cast || check_program_branch_cast(program, at)))
            {
                // Its operands, the left one on top.
                expanded[top - 1] = 1;
                if (binary)
                {
                    frames[top] = graph->operand1[at];
                    levels[top] = (uint8_t)(level + 1);
                    expanded[top] = 0;
                    top += 1;
                }
                frames[top] = graph->operand0[at];
                levels[top] = (uint8_t)(level + 1);
                expanded[top] = 0;
                top += 1;
            }
            else
            {
                values[count] = known ? (uint8_t)value : 2u;
                count += 1;
                top -= 1;
            }
        }
        else
        {
            uint8_t result = 2u;
            if (unary)
            {
                const uint8_t inner = values[count - 1];
                count -= 1;
                result = inner == 2u ? 2u : (uint8_t)(inner ^ (op == CHECK_OP_NOT ? 1u : 0u));
            }
            else
            {
                const uint8_t right = values[count - 1];
                const uint8_t left = values[count - 2];
                count -= 2;
                const uint8_t decider = op == CHECK_OP_LOGICAL_OR ? 1u : 0u;
                result = left == decider ? left :
                         left != 2u && right != 2u ? (op == CHECK_OP_LOGICAL_OR ? (uint8_t)(left | right) : (uint8_t)(left & right)) :
                         left == 2u && right == decider ? right : 2u;
            }
            values[count] = result;
            count += 1;
            top -= 1;
        }
    }
    *truth = values[0] == 1u;
    return values[0] != 2u;
}

// A switch's controlling type: an integer type, of up to 128 bits (check_action_integer_type) or a _BitInt past them
// (#2220, check_wide_type).
static bool check_program_switch_type(const TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    return check_action_integer_type(types, type, &width, &sign) || check_wide_type(types, type, &width, &sign);
}

// A case's value at `node`, converted to the switch's type `target`, into `result`: for a _BitInt past 128 bits (#2220),
// its low 128 bits, and its limbs into `wide` (which may be null for no other type), converted as clang converts a case's
// value (check_wide_convert); null there for every other type.
static bool check_program_case_value(const CheckProgram* program, uint32_t node, TypeId target, unsigned __int128* result, const uint64_t** wide)
{
    uint32_t width = 0;
    uint32_t source_width = 0;
    bool sign = false;
    bool source_sign = false;
    const bool narrow = node < program->stream->count && check_action_integer_type(program->types, target, &width, &sign);
    const bool limbs = !narrow && node < program->stream->count && wide != NULL && check_wide_type(program->types, target, &width, &sign);
    bool valid = narrow || limbs;
    if (wide != NULL)
    {
        *wide = NULL;
    }
    if (valid)
    {
        CheckValue converted = check_graph_context_value(program->types, &program->input->graph, program->stream, node);
        const CheckValue* value = &converted;
        valid = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                (check_action_integer_type(program->types, value->type, &source_width, &source_sign) ||
                 (limbs && check_wide_type(program->types, value->type, &source_width, &source_sign)));
        if (valid && limbs)
        {
            // The conversion leaves a fault, and no limbs, where it can't make them.
            check_wide_convert(program->types, &converted, target, node);
            converted.type = target;
            const uint64_t* held = check_wide_limbs(&converted, check_wide_count(width));
            valid = converted.fault == CHECK_FAULT_NONE && held != NULL;
            *result = converted.integer;
            *wide = valid ? held : NULL;
        }
        else if (valid)
        {
            const unsigned __int128 mask = check_action_mask(source_width);
            unsigned __int128 integer = value->integer & mask;
            if (source_sign && (integer >> (source_width - 1)) != 0)
            {
                integer |= ~mask;
            }
            *result = integer & check_action_mask(width);
        }
    }
    return valid;
}

static unsigned __int128 check_program_case_order(const CheckProgram* program, const CheckProgramCase* item, bool upper)
{
    uint32_t width = 0;
    bool sign = false;
    check_action_integer_type(program->types, item->type, &width, &sign);
    const unsigned __int128 value = upper ? item->upper : item->lower;
    const unsigned __int128 result = sign && width != 0 ? value ^ ((unsigned __int128)1 << (width - 1)) : value;
    return result;
}

// The order of a case's value (its `upper` one for a GNU range's high value) and another's in their switch's type: -1, 0
// or 1. A _BitInt's past 128 bits (#2220) by its limbs, the top one's sign first for a signed one; any other by its
// order key (check_program_case_order).
static int32_t check_program_case_compare(const CheckProgram* program, const CheckProgramCase* a, bool a_upper, const CheckProgramCase* b, bool b_upper)
{
    const uint64_t* x = a_upper ? a->wide_upper : a->wide_lower;
    const uint64_t* y = b_upper ? b->wide_upper : b->wide_lower;
    int32_t result = 0;
    if (x != NULL && y != NULL)
    {
        uint32_t width = 0;
        bool sign = false;
        check_wide_type(program->types, a->type, &width, &sign);
        const uint32_t count = check_wide_count(width);
        // The top limb holds the bits up to the width, zeros past it: its sign is bit width - 1.
        const uint64_t flip = sign ? UINT64_C(1) << ((width - 1) % 64) : 0;
        for (uint32_t i = count; result == 0 && i != 0; i -= 1)
        {
            const uint64_t left = x[i - 1] ^ (i == count ? flip : 0);
            const uint64_t right = y[i - 1] ^ (i == count ? flip : 0);
            result = left < right ? -1 : left > right ? 1 : 0;
        }
    }
    else
    {
        const unsigned __int128 left = check_program_case_order(program, a, a_upper);
        const unsigned __int128 right = check_program_case_order(program, b, b_upper);
        result = left < right ? -1 : left > right ? 1 : 0;
    }
    return result;
}

static bool check_program_case_before(const CheckProgram* program, uint32_t a, uint32_t b)
{
    const CheckProgramCase* left = program->cases + a;
    const CheckProgramCase* right = program->cases + b;
    const uint32_t left_kind = left->fallback ? 0 : left->range ? 2 : 1;
    const uint32_t right_kind = right->fallback ? 0 : right->range ? 2 : 1;
    const int32_t order = left->fallback || right->fallback ? 0 : check_program_case_compare(program, left, false, right, false);
    const bool before = left->owner != right->owner ? left->owner < right->owner : left_kind != right_kind ? left_kind < right_kind :
                        order != 0 ? order < 0 : left->token < right->token;
    return before;
}

#include "c_check_program_diagnostic.c"

// The tokens check_program_cases reads, 64 at a time (#530): each switch keyword and each case's closing token that isn't
// deferred, and the cases among them.
static uint64_t check_program_case_marks(const Parser* parser, const CheckInput* input, uint32_t token, uint64_t* cases)
{
    const uint64_t closes = _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(parser->tree.roles + token), _mm512_set1_epi8(PARSE_ROLE_CLOSE_CASE));
    uint64_t marked = closes | _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(parser->tokens.kinds + token), _mm512_set1_epi8(PARSE_KIND_SWITCH));
    marked &= input->deferred_tokens != 0 ? _mm512_testn_epi8_mask(_mm512_loadu_si512(input->deferred_tokens + token), _mm512_set1_epi8(-1)) : UINT64_MAX;
    *cases = marked & closes;
    return marked;
}

// One of those tokens: a switch's condition's type, and a case's value, range and place in its switch's list.
static void check_program_case(CheckProgram* program, const Parser* parser, uint32_t token, uint32_t* first, uint32_t* next, uint32_t* map)
{
    const CheckInput* input = program->input;
    const uint32_t tokens = input->token_count;
    const uint32_t owner = parser->tree.parents[token];
    if (parser->tokens.kinds[token] == PARSE_KIND_SWITCH && owner < tokens && parser->tree.roles[owner] == PARSE_ROLE_CLOSE_CONDITION)
    {
        const uint32_t condition = input->token_nodes[owner];
        if (condition >= program->stream->count || !check_program_switch_type(program->types, check_stream_type(program->stream, condition)))
        {
            check_program_constraint(program, owner);
        }
    }
    if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_CASE)
    {
        const uint32_t owner = check_sparse_get(input->control_owners, token);
        const uint32_t node = input->token_nodes[token];
        const uint32_t condition = owner < tokens ? input->token_nodes[owner] : BUSTER_CHECK_NONE;
        if (condition >= program->stream->count || !check_program_switch_type(program->types, check_stream_type(program->stream, condition)))
        {
            check_program_constraint(program, owner < tokens ? owner : token);
        }
        else
        {
            const uint32_t index = program->case_count;
            CheckProgramCase* item = program->cases + index;
            *item = (CheckProgramCase)
            {
                .type = check_stream_type(program->stream, condition),
                .token = token,
                .owner = owner,
                .target = BUSTER_CHECK_NONE,
                .fallback = node == BUSTER_CHECK_NONE,
                .range = check_sparse_get(input->case_ranges, token) != BUSTER_CHECK_NONE,
            };
            const uint32_t high = check_sparse_get(input->case_ranges, token) != BUSTER_CHECK_NONE ? check_sparse_get(input->case_ranges, token) : node;
            if (!item->fallback && (!check_program_case_value(program, node, item->type, &item->lower, &item->wide_lower) ||
                                   !check_program_case_value(program, high, item->type, &item->upper, &item->wide_upper)))
            {
                check_program_constraint(program, token);
            }
            else
            {
                program->case_count += 1;
                map[token] = index;
                next[index] = first[owner];
                first[owner] = index;
            }
        }
    }
}

// The cases, numbered in token order with their switches' lists: AVX-512 finds the switch keywords and cases 64 tokens at a
// time, to count the cases and then to check each in order; the rest and the `scalar` path test every token.
static void check_program_cases(CheckProgram* program, const Parser* parser, uint32_t* first, uint32_t* next, uint32_t* map, Arena* output, Arena* scratch, bool scalar)
{
    (void)scalar;

    const CheckInput* input = program->input;
    const uint32_t tokens = input->token_count;
    const uint32_t vector_end = BUSTER_SCALAR_PATH(scalar) ? 0 : tokens / 64 * 64;
    uint32_t count = 0;
    for (uint32_t token = 0; token < vector_end; token += 64)
    {
        uint64_t cases = 0;
        check_program_case_marks(parser, input, token, &cases);
        count += (uint32_t)__builtin_popcountll(cases);
    }
    for (uint32_t token = vector_end; token < tokens; token += 1)
    {
        count += parser->tree.roles[token] == PARSE_ROLE_CLOSE_CASE && (input->deferred_tokens == 0 || input->deferred_tokens[token] == 0);
    }
    program->cases = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramCase, count) : 0;
    if (count != 0 && program->cases == 0)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
    }
    memset(first, 0xff, ((uint64_t)tokens + 1) * sizeof(uint32_t));
    memset(map, 0xff, ((uint64_t)tokens + 1) * sizeof(uint32_t));
    for (uint32_t token = 0; token < vector_end && program->status != CHECK_PROGRAM_RESOURCE; token += 64)
    {
        uint64_t cases = 0;
        for (uint64_t marked = check_program_case_marks(parser, input, token, &cases); marked != 0 && program->status != CHECK_PROGRAM_RESOURCE; marked &= marked - 1)
        {
            check_program_case(program, parser, token + (uint32_t)__builtin_ctzll(marked), first, next, map);
        }
    }
    for (uint32_t token = vector_end; token < tokens && program->status != CHECK_PROGRAM_RESOURCE; token += 1)
    {
        if (input->deferred_tokens == 0 || input->deferred_tokens[token] == 0)
        {
            check_program_case(program, parser, token, first, next, map);
        }
    }
    if (program->status != CHECK_PROGRAM_RESOURCE)
    {
        check_program_case_conflicts(program, parser, 0, scratch);
    }
}

static void check_control_push(CheckControlWriter* writer, CheckControlTask task)
{
    if (writer->depth < writer->capacity)
    {
        writer->tasks[writer->depth++] = task;
    }
    else
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
    }
}

static uint32_t check_control_label(CheckControlWriter* writer)
{
    const uint32_t label = writer->label_count;
    if (label < writer->capacity)
    {
        writer->labels[label] = BUSTER_CHECK_NONE;
        // A body numbers its labels from 0 again: no earlier body's stop is this label's (#1355).
        if (writer->lifetime_stops != NULL)
        {
            writer->lifetime_stops[label] = BUSTER_CHECK_NONE;
        }
        writer->label_count += 1;
    }
    else
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, 0);
    }
    return label;
}

static void check_control_mark(CheckControlWriter* writer, uint32_t label)
{
    check_control_push(writer, (CheckControlTask){ .kind = CHECK_CONTROL_TASK_LABEL, .token = label });
}

static void check_control_entry(CheckControlWriter* writer, CheckControlKind kind, uint32_t token, uint32_t node, uint32_t target)
{
    check_control_push(writer, (CheckControlTask)
    {
        .kind = CHECK_CONTROL_TASK_ENTRY,
        .entry = { .node = node, .token = token, .target = target, .kind = kind },
        .token = token,
    });
}

// The jump under a chain of attribute specifiers (check_input_statement_attributes), from the closer of the first: each
// closer's child after it is the next specifier's closer, or the statement: a return (a musttail one's), a break, a
// continue or a goto, whose attributes change nothing of its jump (#1844: `[[clang::likely]] break;`,
// `__attribute__((nomerge)) continue;`). BUSTER_CHECK_NONE where the statement is another (an expression's, a null one's).
static uint32_t check_control_attributed(const CheckControlWriter* writer, uint32_t closer)
{
    const Parser* parser = writer->parser;
    uint32_t at = closer;
    for (uint32_t steps = 0; at != BUSTER_CHECK_NONE && steps < parser->tokens.count && parser->tree.roles[at] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER; steps += 1)
    {
        uint32_t next = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[at]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            next = child > at ? child : next;
        }
        at = next;
    }
    const uint8_t kind = at != BUSTER_CHECK_NONE ? parser->tokens.kinds[at] : 0;
    const bool jump = kind == PARSE_KIND_RETURN || kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE || kind == PARSE_KIND_GOTO;
    return at != BUSTER_CHECK_NONE && parser->tree.roles[at] == PARSE_ROLE_RETURN && jump ? at : BUSTER_CHECK_NONE;
}

// Whether return `token` is a musttail return's (CheckInput.musttails, in source order: a binary search).
static bool check_control_musttail(const CheckInput* input, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = input->musttail_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = input->musttails[middle].statement < token;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < input->musttail_count && input->musttails[low].statement == token;
}

// Whether a musttail return's value (`node`, the return's node: its conversion, then any selections) is a call of
// __builtin_call_with_static_chain (#148), which clang's code generation makes an ordinary call of, the return after it:
// no tail call, as for every builtin's call (c_check_musttail.c checked its signature).
static bool check_control_static_chain(const CheckInput* input, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    uint32_t value = node < graph->count && graph->ops[node] == CHECK_OP_CONVERT ? graph->operand0[node] : node;
    for (uint32_t steps = 0; value < graph->count && steps < graph->count && graph->ops[value] == CHECK_OP_SELECT; steps += 1)
    {
        value = graph->operand0[value];
    }
    return value < graph->count && graph->ops[value] == CHECK_OP_BUILTIN_MISC && graph->members[value] == CBUILTIN_CALL_WITH_STATIC_CHAIN;
}

static void check_control_statement(CheckControlWriter* writer, uint32_t token, uint32_t stop, uint32_t repeat)
{
    if (token != BUSTER_CHECK_NONE)
    {
        check_control_push(writer, (CheckControlTask){ .kind = CHECK_CONTROL_TASK_STATEMENT, .token = token, .stop = stop, .repeat = repeat });
    }
}

static void check_control_token(CheckControlWriter* writer, uint32_t token)
{
    check_control_push(writer, (CheckControlTask){ .kind = CHECK_CONTROL_TASK_TOKEN, .token = token });
}

// Whether bit `index` of `bits` is set, and setting it: the bitsets that mark which entries of a sparse column were
// written, so that the column needn't be cleared at every index (#877).
static bool check_program_bit(const uint64_t* bits, uint32_t index)
{
    return ((bits[index / 64] >> (index % 64)) & 1) != 0;
}

static void check_program_bit_set(uint64_t* bits, uint32_t index)
{
    bits[index / 64] |= UINT64_C(1) << (index % 64);
}

// Whether each initializer's last owning declaration has static storage duration. A declaration may share a node
// with an earlier one, so a later automatic declaration clears the bit.
static void check_program_static_initializers(const CheckInput* input, uint64_t* bits)
{
    const uint32_t count = input->graph.count;
    memset(bits, 0, ((uint64_t)count + 64) / 64 * sizeof(uint64_t));
    for (uint32_t t = check_sparse_next(input->initializer_nodes, 0, input->token_count); t < input->token_count;
         t = check_sparse_next(input->initializer_nodes, t + 1, input->token_count))
    {
        const uint32_t node = check_sparse_get(input->initializer_nodes, t);
        if (node < count)
        {
            const uint64_t bit = UINT64_C(1) << (node % 64);
            uint64_t* word = bits + node / 64;
            *word = (*word & ~bit) | (check_input_bit(input->declaration_bits, t, BUSTER_CHECK_BITS_STATIC) ? bit : 0);
        }
    }
}

static bool check_control_static(CheckControlWriter* writer, uint32_t node)
{
    const CheckInput* input = writer->program->input;
    const CheckGraph* graph = &input->graph;
    uint32_t cursor = node;
    while (cursor != BUSTER_CHECK_NONE && (graph->ops[cursor] == CHECK_OP_CAST || graph->ops[cursor] == CHECK_OP_CONVERT))
    {
        cursor = graph->operand0[cursor];
    }
    const bool initializer = cursor != BUSTER_CHECK_NONE && graph->ops[cursor] == CHECK_OP_INITIALIZE;
    if (initializer && !writer->static_initializers_ready)
    {
        check_program_static_initializers(input, writer->static_initializers);
        writer->static_initializers_ready = true;
    }
    return initializer && check_program_bit(writer->static_initializers, cursor);
}

// The STACK_RESTORE that a jump at `jump` needs before it, when it leaves blocks with variable-length arrays: of the
// outermost block whose region (after its STACK_SAVE, to its end) holds the jump but not where it goes. A jump out of
// the first array's own bounds, from a statement expression, comes before the save and needs none. A break or a
// continue goes past the blocks inside its loop or switch, the ancestor whose label in `targets` (stops or repeats) is
// its `target`; a goto (`targets` 0) goes to `label`, a token, inside or outside each region up to the function's
// block. Jumps into a region are the checker's errors.
static void check_control_leave(CheckControlWriter* writer, uint32_t jump, uint32_t label, const uint32_t* targets, uint32_t target)
{
    const Parser* parser = writer->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t left = BUSTER_CHECK_NONE;
    uint32_t steps = 0;
    bool past = false;
    for (uint32_t ancestor = writer->variable ? parser->tree.parents[jump] : count; ancestor < count && steps < count && !past;
         ancestor = parser->tree.parents[ancestor])
    {
        past = targets != 0 && targets[ancestor] == target;
        const uint32_t saved = writer->vla_saves[ancestor];
        const bool held = targets == 0 && label > saved - 1 && label < ancestor;
        left = !past && saved != 0 && saved - 1 < jump && !held ? ancestor : left;
        past |= ancestor == writer->closing;
        steps += 1;
    }
    if (left != BUSTER_CHECK_NONE)
    {
        check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, jump, BUSTER_CHECK_NONE, left);
    }
}

// The innermost of the body's cleanups whose scope holds `position`: the last one declared before it, or a parent of that.
// (A scope's own end takes its innermost record from cleanup_scope_last instead, #1370.)
static uint32_t check_control_cleanup_at(const CheckControlWriter* writer, uint32_t position)
{
    const CheckInputCleanup* cleanups = writer->program->input->cleanups;
    uint32_t low = writer->cleanup_first;
    uint32_t high = writer->cleanup_first + writer->cleanup_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = cleanups[middle].declaration < position ? middle + 1 : low;
        high = cleanups[middle].declaration < position ? high : middle;
    }
    uint32_t record = low != writer->cleanup_first ? low - 1 : BUSTER_CHECK_NONE;
    // A scope's records share its end: a closed scope is passed whole, from its outermost record (#1370).
    while (record != BUSTER_CHECK_NONE && cleanups[record].end <= position)
    {
        record = writer->cleanup_parents[writer->cleanup_tops[record]];
    }
    return record;
}

// The CLEANUP entries of an exit at `position` (a jump, or a scope's last token), each of a variable whose scope holds the
// exit, innermost first: up to the first whose scope holds `target` too (BUSTER_CHECK_NONE for a return's: none does), or
// where `scope` is a scope's opening (its block's end), those of that scope alone. check_control_drain writes the last
// task pushed first, so the run pushed here is reversed, the innermost's last.
static void check_control_cleanups(CheckControlWriter* writer, uint32_t position, uint32_t target, uint32_t scope)
{
    const CheckInputCleanup* cleanups = writer->program->input->cleanups;
    const uint32_t depth = writer->depth;
    // A scope's end runs its own records, from its innermost; an exit's, the innermost whose scope holds it, and up.
    const uint32_t innermost = scope != BUSTER_CHECK_NONE && scope < writer->program->input->token_count ? writer->cleanup_scope_last[scope] : BUSTER_CHECK_NONE;
    const bool owned = innermost >= writer->cleanup_first && innermost < writer->cleanup_first + writer->cleanup_count && cleanups[innermost].scope == scope;
    uint32_t record = writer->cleanup_count == 0 ? BUSTER_CHECK_NONE : scope != BUSTER_CHECK_NONE ? (owned ? innermost : BUSTER_CHECK_NONE) :
                      check_control_cleanup_at(writer, position);
    // An exit of the function's own statements runs them through their chain (#1370): one EXIT of its first record and
    // the innermost whose scope holds where it goes (`hold`, none for a return's), an ancestor of the first, as no jump
    // enters a cleanup's scope. Its last, the hold's child on the path, check_program_chains finds in one sweep, where a
    // climb here took each exit's whole path.
    // An exit from a statement expression's statements uses the same plan, including its own and enclosing scopes.
    const bool chained = scope == BUSTER_CHECK_NONE;
    const uint32_t hold = chained && record != BUSTER_CHECK_NONE && target != BUSTER_CHECK_NONE ? check_control_cleanup_at(writer, target) : BUSTER_CHECK_NONE;
    if (chained && record != BUSTER_CHECK_NONE && record != hold && writer->exit_count < writer->exit_capacity)
    {
        writer->exits[writer->exit_count] = (CheckProgramExit){ .first = record,
                                                                .last = BUSTER_CHECK_NONE,
                                                                .hold = hold,
                                                                .value = BUSTER_CHECK_NONE,
                                                                .destination = BUSTER_CHECK_NONE,
                                                                .tail_count = 0 };
        check_control_entry(writer, CHECK_CONTROL_EXIT, position, BUSTER_CHECK_NONE, writer->exit_count);
        writer->exit_count += 1;
    }
    else if (chained && record != BUSTER_CHECK_NONE && record != hold)
    {
        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, position);
    }
    while (!chained && record != BUSTER_CHECK_NONE && (scope != BUSTER_CHECK_NONE ? cleanups[record].scope == scope :
                                                       cleanups[record].declaration >= target || target >= cleanups[record].end))
    {
        check_control_entry(writer, CHECK_CONTROL_CLEANUP, position, BUSTER_CHECK_NONE, record);
        record = writer->cleanup_parents[record];
    }
    for (uint32_t low = depth, high = writer->depth; low + 1 < high; low += 1, high -= 1)
    {
        const CheckControlTask task = writer->tasks[low];
        writer->tasks[low] = writer->tasks[high - 1];
        writer->tasks[high - 1] = task;
    }
}

// The child of `closing` (a block's or a for statement's) of role `role`, its opening, BUSTER_CHECK_NONE for none.
static uint32_t check_control_opening(const CheckControlWriter* writer, uint32_t closing, uint8_t role)
{
    uint32_t child = writer->first[closing];
    while (child != BUSTER_CHECK_NONE && writer->parser->tree.roles[child] != role)
    {
        child = writer->next[child];
    }
    return child;
}

// The ancestor of a jump at `jump` where its walk past the blocks it leaves ends: the body's closing brace for a goto
// (`targets` 0); for a break or a continue the first of the body's closing brace and its loop or switch, the ancestor whose
// label in `targets` (stops or repeats) is its `target`, past the root (`count`) for neither. Every jump to a label is
// inside that label's loop or switch, so each label's is found once, by the first jump to it, and kept in lifetime_stops
// (#1355): an else-if chain's breaks would each climb the chain again.
static uint32_t check_control_lifetime_stop(CheckControlWriter* writer, uint32_t jump, const uint32_t* targets, uint32_t target)
{
    const Parser* parser = writer->parser;
    const uint32_t count = parser->tokens.count;
    const bool kept = targets != 0 && writer->lifetime_stops != NULL && target < writer->capacity;
    uint32_t stop = targets == 0 ? writer->closing : kept ? writer->lifetime_stops[target] : BUSTER_CHECK_NONE;
    if (targets != 0 && stop == BUSTER_CHECK_NONE)
    {
        uint32_t ancestor = parser->tree.parents[jump];
        uint32_t steps = 0;
        while (ancestor < count && steps < count && targets[ancestor] != target && ancestor != writer->closing)
        {
            ancestor = parser->tree.parents[ancestor];
            steps += 1;
        }
        stop = ancestor < count ? ancestor : count;
        writer->lifetime_steps += steps;
        if (kept)
        {
            writer->lifetime_stops[target] = stop;
        }
    }
    return stop < count ? stop : count;
}

// The CHECK_CONTROL_LIFETIME ends of a jump at `jump` (#177): one for each block with lifetimes that it leaves, innermost
// first. A break or a continue leaves the blocks inside its loop or switch, the ancestor whose label in `targets` (stops or
// repeats) is its `target`; a goto (`targets` 0) those that don't hold `label`, a token, and every block outside the
// innermost that holds it holds it too. Neither leaves the body's closing brace. The walk visits the blocks with lifetimes
// alone, along lifetime_nearest, up to the first at or above where it ends (check_control_lifetime_stop): its work is the
// entries it makes (#1355), where a walk of every ancestor was quadratic in an else-if chain.
static void check_control_lifetimes_left(CheckControlWriter* writer, uint32_t jump, uint32_t label, const uint32_t* targets, uint32_t target)
{
    const Parser* parser = writer->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t* first = writer->program->lifetime_first;
    const uint32_t* nearest = writer->lifetime_nearest;
    uint32_t block = first != NULL && nearest != NULL && jump < count ? nearest[jump] : count;
    const uint32_t stop = block < count ? check_control_lifetime_stop(writer, jump, targets, target) : count;
    const bool own = stop < count && parser->tree.roles[stop] == PARSE_ROLE_CLOSE_BLOCK && first[stop] != BUSTER_CHECK_NONE;
    const uint32_t end = own ? stop : stop < count ? nearest[stop] : count;
    uint32_t steps = 0;
    while (block < count && block != end && steps < count)
    {
        const uint32_t opening = targets == 0 ? check_control_opening(writer, block, PARSE_ROLE_OPEN_BLOCK) : BUSTER_CHECK_NONE;
        const bool holds = opening != BUSTER_CHECK_NONE && label > opening && label < block;
        if (!holds)
        {
            check_control_entry(writer, CHECK_CONTROL_LIFETIME, jump, 1, block);
        }
        block = holds ? count : nearest[block];
        steps += 1;
    }
    writer->lifetime_steps += steps;
}

static void check_control_extent(const CheckControlWriter* writer, uint32_t arm, uint32_t* low, uint32_t* high);

// The declaration a return at `jump` names alone, in parentheses or not (clang's IgnoreParens), BUSTER_CHECK_NONE for
// anything else: its expression's tokens, up to its terminator, whose child it is.
static uint32_t check_control_returned(const CheckControlWriter* writer, uint32_t jump)
{
    const Parser* parser = writer->parser;
    const uint32_t terminator = parser->tree.parents[jump];
    uint32_t first = jump + 1;
    uint32_t last = terminator > jump + 1 && terminator < parser->tokens.count ? terminator - 1 : jump;
    while (first < last && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == last)
    {
        first += 1;
        last -= 1;
    }
    const bool named = first == last && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
    return named ? writer->program->input->declarations[first] : BUSTER_CHECK_NONE;
}

// Whether a return at `jump` returns its function's named return value, which clang's code constructs in the return slot
// (Sema's NRVO, CodeGenFunction::EmitAutoVarAlloca), so that the return's cleanups write the value it returns: a struct or
// union variable of the function's result type (unqualified), automatic, neither volatile nor aligned past its type, named
// alone, with every return after its declaration in its scope naming it alone too (Scope::updateNRVOCandidate keeps it its
// scope's candidate then, which applyNRVO marks).
static bool check_control_named(const CheckControlWriter* writer, uint32_t jump)
{
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const TypeTable* types = writer->program->types;
    const uint32_t count = parser->tokens.count;
    const uint32_t function = check_sparse_get(input->body_declarations, writer->opening);
    const TypeId type = function < count ? type_strip_aligned(types, input->declared_types[function]) : 0;
    const TypeId result = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_FUNCTION ? type_unqualified(type_strip_aligned(types, type_return(types, type))) : 0;
    const uint8_t result_kind = result != 0 ? type_kind(types, result) : BUSTER_TYPE_KIND_NONE;
    const uint32_t variable = result_kind == BUSTER_TYPE_KIND_STRUCT || result_kind == BUSTER_TYPE_KIND_UNION ? check_control_returned(writer, jump) : BUSTER_CHECK_NONE;
    const TypeId declared = variable < count ? input->declared_types[variable] : 0;
    const CheckInputAttributes* attributes = variable < count ? check_input_attributes_at(input, variable) : NULL;
    const bool aligned = attributes != NULL && attributes->alignment_log2 != 0 && attributes->alignment_log2 - 1u > type_alignment_log2(types, declared);
    bool named = variable > writer->opening && variable < writer->closing && input->declaration_kinds[variable] == C_BINDING_OBJECT &&
                 !check_input_bit(input->declaration_bits, variable, BUSTER_CHECK_BITS_STATIC) && declared != 0 && (type_qualifiers(declared) & BUSTER_TYPE_VOLATILE) == 0 && !aligned &&
                 type_unqualified(type_strip_aligned(types, declared)) == result;
    // Its scope: its block's to the block's closing token, or its for statement's to the statement's last.
    uint32_t scope = named ? parser->tree.parents[variable] : count;
    uint32_t steps = 0;
    while (scope < count && steps < count && parser->tree.roles[scope] != PARSE_ROLE_CLOSE_BLOCK && parser->tree.roles[scope] != PARSE_ROLE_CLOSE_BODY &&
           parser->tree.roles[scope] != PARSE_ROLE_CLOSE_FOR)
    {
        scope = parser->tree.parents[scope];
        steps += 1;
    }
    uint32_t low = scope;
    uint32_t end = scope;
    if (scope < count && parser->tree.roles[scope] == PARSE_ROLE_CLOSE_FOR)
    {
        check_control_extent(writer, scope, &low, &end);
    }
    named &= scope < count;
    for (uint32_t token = variable + 1; named && token <= end; token += 1)
    {
        named = parser->tree.roles[token] != PARSE_ROLE_RETURN || parser->tokens.kinds[token] != PARSE_KIND_RETURN || check_control_returned(writer, token) == variable;
    }
    return named;
}

// The loop or switch a break or a continue at `jump` leaves: its ancestor whose stop or repeat (in `targets`) is `label`.
static uint32_t check_control_loop(const CheckControlWriter* writer, uint32_t jump, const uint32_t* targets, uint32_t label)
{
    const Parser* parser = writer->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = parser->tree.parents[jump];
    uint32_t steps = 0;
    while (ancestor < count && steps < count && targets[ancestor] != label)
    {
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
    return ancestor;
}

// The first and last tokens of the statement `arm`: its first children's first and its last children's last.
static void check_control_extent(const CheckControlWriter* writer, uint32_t arm, uint32_t* low, uint32_t* high)
{
    *low = arm;
    *high = arm;
    for (uint32_t token = arm; token != BUSTER_CHECK_NONE; token = writer->first[token])
    {
        *low = token < *low ? token : *low;
    }
    for (uint32_t token = arm; token != BUSTER_CHECK_NONE; token = writer->last[token])
    {
        *high = token > *high ? token : *high;
    }
}

// Whether tokens `low` to `high` hold a label, or a case of a switch outside them, as clang's CodeGenFunction::
// ContainsLabel finds: syntactically, a statement expression's in sizeof's operand too.
static bool check_control_labeled(const CheckControlWriter* writer, uint32_t low, uint32_t high)
{
    const uint8_t* roles = writer->parser->tree.roles;
    const CheckSparseColumn owners = writer->program->input->control_owners;
    bool labeled = false;
    for (uint32_t token = low; !labeled && token <= high; token += 1)
    {
        labeled = roles[token] == PARSE_ROLE_LABEL ||
                  (roles[token] == PARSE_ROLE_CLOSE_CASE && (check_sparse_get(owners, token) < low || check_sparse_get(owners, token) > high));
    }
    return labeled;
}

// Whether a jump can enter the statement `arm` (none for BUSTER_CHECK_NONE) other than at its start: it holds a label,
// or a case of a switch outside it (check_control_labeled).
static bool check_control_enterable(const CheckControlWriter* writer, uint32_t arm)
{
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, arm, &low, &high);
    return arm != BUSTER_CHECK_NONE && check_control_labeled(writer, low, high);
}

// Pushes `node` on the fold walk's stack (check_control_foldable), which takes a node a visit: whether it had room.
static bool check_control_fold_push(const CheckControlWriter* writer, uint32_t* depth, uint32_t visits, uint32_t node)
{
    const bool room = *depth < writer->fold_capacity && visits + *depth < writer->fold_capacity;
    if (room)
    {
        writer->fold_stack[(*depth)++] = node;
    }
    return room;
}

// Whether `value` is one that clang's evaluator takes where it evaluates an expression for nothing but its value (a
// statement expression's statement, an automatic object's initializer, a comma's left operand): an integer, floating
// or address constant, an rvalue (an lvalue's designator, `n;`, reads its object), without side effects or faults.
static bool check_control_fold_value(const CheckValue* value)
{
    return (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING || value->constant_kind == CHECK_CONSTANT_ADDRESS) &&
           value->category == CHECK_RVALUE && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
}

// Whether clang's evaluator takes expression `node` where it wants nothing but its value: check_control_fold_value's
// (a string literal's address after its decay among them), or through its casts and a declaration's initialization
// (CHECK_OP_INITIALIZE) its operand's, or the address of a named object, which it forms without reading the object
// (`&n` of a local, or a local array's decay); and its node on the walk's stack, for the __builtin_constant_p it may
// hold.
static bool check_control_fold_operand(const CheckControlWriter* writer, uint32_t node, uint32_t* depth, uint32_t visits)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    uint32_t operand = node;
    CheckValue value = operand < graph->count ? check_stream_value(program->stream, operand) : (CheckValue){ 0 };
    // The first value it takes on the way down: a string literal's is its decay's, not the array lvalue's under it.
    for (uint32_t steps = 0; !check_control_fold_value(&value) && operand < graph->count && steps < graph->count &&
                             (graph->ops[operand] == CHECK_OP_CAST || graph->ops[operand] == CHECK_OP_CONVERT || graph->ops[operand] == CHECK_OP_INITIALIZE);
         steps += 1)
    {
        operand = graph->operand0[operand];
        value = operand < graph->count ? check_stream_value(program->stream, operand) : (CheckValue){ 0 };
    }
    // A named array, including a VLA, decays without reading its elements. Its declaration's bound evaluations
    // remain obligations of the statement proof. An element's address also needs a constant, in-bounds index.
    const bool decays = operand < graph->count && graph->ops[operand] == CHECK_OP_VALUE && type_kind_is_array(check_action_kind(program->types, value.type));
    uint32_t object = operand < graph->count && graph->ops[operand] == CHECK_OP_ADDRESS ? graph->operand0[operand] : decays ? operand : BUSTER_CHECK_NONE;
    if (object < graph->count && graph->ops[object] == CHECK_OP_SUBSCRIPT)
    {
        const uint32_t left = graph->operand0[object];
        const uint32_t right = graph->operand1[object];
        const TypeId left_type = left < graph->count ? check_stream_type(program->stream, left) : 0;
        const bool left_array = type_kind_is_array(check_action_kind(program->types, left_type));
        const uint32_t array = left_array ? left : right;
        const uint32_t index = left_array ? right : left;
        const TypeId type = array < graph->count ? type_strip_aligned(program->types, check_stream_type(program->stream, array)) : 0;
        const CheckValue offset = index < graph->count ? check_stream_value(program->stream, index) : (CheckValue){ 0 };
        const bool bounded = check_action_kind(program->types, type) == BUSTER_TYPE_KIND_ARRAY && check_control_fold_value(&offset) &&
                             offset.constant_kind == CHECK_CONSTANT_INTEGER && offset.integer <= program->types->types.extent[type_index(type)];
        object = bounded ? array : BUSTER_CHECK_NONE;
    }
    const bool named = object < graph->count && graph->ops[object] == CHECK_OP_VALUE && check_graph_leaf_object(graph, object) != 0 &&
                       (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    return operand < graph->count && (check_control_fold_value(&value) || named) && check_control_fold_push(writer, depth, visits, operand);
}

// A statement on the fold walk's stack (check_control_foldable), beside the expressions' nodes: its token with this bit.
#define BUSTER_CHECK_CONTROL_FOLD_STATEMENT UINT32_C(0x80000000)

static inline uint32_t check_control_statement_head(const CheckControlWriter* writer, uint32_t token)
{
    return writer->token_first != NULL ? writer->token_first[token] : writer->program->input->statement_heads[token] - 1;
}

static inline uint32_t check_control_statement_next(const CheckControlWriter* writer, uint32_t token, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    return writer->node_next != NULL ? writer->node_next[node] :
           (node + 1 < graph->count && graph->tokens[node + 1] == token ? node + 1 : BUSTER_CHECK_NONE);
}

// Whether clang's evaluator takes what the writer emits at `token` (check_control_drain's token: a statement's
// terminator or a for statement's first `;`): its declarations' initializers, or else its expressions, each a value
// that check_control_fold_value takes, and none a static declaration, an asm statement or a typeof's operand. The
// expressions go on the walk's stack, for the __builtin_constant_p they may hold. An extern declaration has no code.
static bool check_control_fold_simple(const CheckControlWriter* writer, uint32_t token, uint32_t* depth, uint32_t visits)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, token, &low, &high);
    bool foldable = (writer->asm_statements == NULL || writer->asm_statements[token] == BUSTER_CHECK_NONE) &&
                    (!check_sparse_present(input->evaluation_nodes) || check_sparse_get(input->evaluation_nodes, token) == BUSTER_CHECK_NONE);
    for (uint32_t t = low; foldable && t <= high; t += 1)
    {
        const bool object = input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_OBJECT;
        foldable = !(object && check_input_bit(input->declaration_bits, t, BUSTER_CHECK_BITS_STATIC) && check_input_linkage(input->declaration_bits, t) == 0);
    }
    const uint32_t first_plan = foldable && program->initializers != NULL && check_program_bit(writer->planned, token) ? writer->plan_first[token] : BUSTER_CHECK_NONE;
    for (uint32_t i = first_plan; foldable && i != BUSTER_CHECK_NONE; i = writer->plan_next[i])
    {
        const CheckInitializer* plan = program->initializers->plans + i;
        for (uint32_t part = 0; foldable && part < plan->count; part += 1)
        {
            const CheckInitPart* write = plan->parts + part;
            // By its source expression where it has one, or by its value converted to the object's type (a string
            // literal's array, whose source is the array's lvalue, decays to its address), with the source on the
            // walk's stack all the same.
            const bool source = write->node < input->graph.count;
            foldable = write->kind != CHECK_INIT_VALUE || (source && check_control_fold_operand(writer, write->node, depth, visits)) ||
                       (check_control_fold_value(&write->value) && (!source || check_control_fold_push(writer, depth, visits, write->node)));
        }
    }
    for (uint32_t node = foldable && first_plan == BUSTER_CHECK_NONE ? check_control_statement_head(writer, token) : BUSTER_CHECK_NONE; foldable && node != BUSTER_CHECK_NONE;
         node = check_control_statement_next(writer, token, node))
    {
        foldable = check_control_fold_operand(writer, node, depth, visits);
    }
    return foldable;
}

// Whether clang's evaluator takes `statement`, of a statement expression before its value (VisitStmtExpr's
// EvaluateStmt), as far as it decides here: an empty statement or one check_control_fold_simple takes; a block, an
// else or a do's body, whose statements go on the walk's stack; an if of a constant condition, whose taken arm does; a
// while, a for or a do whose constant condition is false (the for's first clause taken by check_control_fold_simple);
// and a switch of a constant condition without a case. Every other statement, a jump that it evaluates among them, it
// doesn't.
static bool check_control_fold_statement(const CheckControlWriter* writer, uint32_t statement, uint32_t* depth, uint32_t visits)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const Parser* parser = writer->parser;
    const uint8_t role = parser->tree.roles[statement];
    bool foldable = false;
    if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        foldable = true;
        for (uint32_t child = writer->first[statement]; foldable && child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            foldable = parser->tree.roles[child] == PARSE_ROLE_OPEN_BLOCK || check_control_fold_push(writer, depth, visits, child | BUSTER_CHECK_CONTROL_FOLD_STATEMENT);
        }
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        // A do statement's terminator holds its body (DO) and its condition: its body once, then a false condition.
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        foldable = true;
        for (uint32_t child = writer->first[statement]; foldable && child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            const uint8_t child_role = parser->tree.roles[child];
            foldable = child_role != PARSE_ROLE_RETURN && child_role != PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
            body = child_role == PARSE_ROLE_DO ? child : body;
            condition = child_role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
        }
        if (body != BUSTER_CHECK_NONE)
        {
            const CheckGraph* graph = &input->graph;
            const uint32_t node = condition != BUSTER_CHECK_NONE ? input->token_nodes[condition] : BUSTER_CHECK_NONE;
            const uint32_t expression = node < graph->count && graph->ops[node] == CHECK_OP_CONDITION ? graph->operand0[node] : node;
            const CheckValue value = expression < graph->count ? check_stream_value(program->stream, expression) : (CheckValue){ 0 };
            bool truth = true;
            foldable = foldable && value.constant_kind != CHECK_CONSTANT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                       check_action_truth(program->types, &value, &truth) && !truth && check_control_fold_push(writer, depth, visits, expression) &&
                       check_control_fold_push(writer, depth, visits, body | BUSTER_CHECK_CONTROL_FOLD_STATEMENT);
        }
        else
        {
            foldable = foldable && check_control_fold_simple(writer, statement, depth, visits);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR)
    {
        // check_control_expand's children: an if, while or switch's keyword, body and else; a for's two `;`s and body.
        uint32_t keyword = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[statement]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            const uint8_t child_role = parser->tree.roles[child];
            const bool semi = child < statement && parser->tokens.kinds[child] == PARSE_KIND_SEMI;
            keyword = child < statement && child_role == PARSE_ROLE_LEAF_RIGHT ? child : keyword;
            condition = semi && first != BUSTER_CHECK_NONE ? child : condition;
            first = semi && first == BUSTER_CHECK_NONE ? child : first;
            alternate = child > statement && child_role == PARSE_ROLE_ELSE ? child : alternate;
            body = child > statement && child_role != PARSE_ROLE_ELSE ? child : body;
        }
        const bool loop = role == PARSE_ROLE_CLOSE_FOR;
        const uint32_t node = loop ? (condition != BUSTER_CHECK_NONE ? check_control_statement_head(writer, condition) : BUSTER_CHECK_NONE) : input->token_nodes[statement];
        const CheckGraph* graph = &input->graph;
        const uint32_t expression = node < graph->count && graph->ops[node] == CHECK_OP_CONDITION ? graph->operand0[node] : node;
        const CheckValue value = expression < graph->count ? check_stream_value(program->stream, expression) : (CheckValue){ 0 };
        bool truth = false;
        const uint8_t kind = keyword != BUSTER_CHECK_NONE ? parser->tokens.kinds[keyword] : 0;
        foldable = value.constant_kind != CHECK_CONSTANT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && check_action_truth(program->types, &value, &truth) &&
                   check_control_fold_push(writer, depth, visits, expression);
        if (loop)
        {
            foldable = foldable && !truth && first != BUSTER_CHECK_NONE && check_control_fold_simple(writer, first, depth, visits);
        }
        else if (kind == PARSE_KIND_IF)
        {
            const uint32_t taken = truth ? body : alternate;
            foldable = foldable && (taken == BUSTER_CHECK_NONE || check_control_fold_push(writer, depth, visits, taken | BUSTER_CHECK_CONTROL_FOLD_STATEMENT));
        }
        else if (kind == PARSE_KIND_WHILE)
        {
            foldable = foldable && !truth;
        }
        else if (kind == PARSE_KIND_SWITCH)
        {
            uint32_t low = 0;
            uint32_t high = 0;
            check_control_extent(writer, body, &low, &high);
            for (uint32_t t = low; foldable && body != BUSTER_CHECK_NONE && t <= high; t += 1)
            {
                foldable = parser->tree.roles[t] != PARSE_ROLE_CLOSE_CASE;
            }
        }
        else
        {
            foldable = false;
        }
    }
    return foldable;
}

// Whether clang's constant evaluation folds the condition `expression`, whose source value is an integer constant,
// outside a constant context too (CodeGenFunction::ConstantFoldsToSimpleInteger's Expr::EvaluateAsInt): not where it
// evaluates a __builtin_constant_p of an argument that is no constant and has no side effect, which the evaluator
// refuses there, though a constant context's value, the checker's source value, is 0; nor a comma whose left operand
// is no constant; nor a statement expression with a statement before its value that the evaluator refuses
// (check_control_fold_statement). The walk takes the operands and statements the evaluation takes: not the operands
// that a constant condition of &&, || or ?: skips, nor sizeof's, _Alignof's or a query's, nor an untaken if's arm.
// Without room on the writer's fold stack (an entry a visit), it says no.
static bool check_control_foldable(const CheckControlWriter* writer, uint32_t expression)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckStream* stream = program->stream;
    uint32_t* stack = writer->fold_stack;
    uint32_t depth = 0;
    uint32_t visits = 0;
    bool foldable = expression < graph->count && check_control_fold_push(writer, &depth, visits, expression);
    while (foldable && depth != 0)
    {
        const uint32_t entry = stack[--depth];
        const uint32_t node = entry & ~BUSTER_CHECK_CONTROL_FOLD_STATEMENT;
        const CheckOp op = entry == node ? graph->ops[node] : CHECK_OP_VALUE;
        visits += 1;
        if (entry != node)
        {
            foldable = check_control_fold_statement(writer, node, &depth, visits);
        }
        else if (op == CHECK_OP_BUILTIN_QUERY)
        {
            const uint32_t index = graph->members[node] == CBUILTIN_CONSTANT_P ?
                                   check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), node) : BUSTER_CHECK_NONE;
            const uint32_t argument = index != BUSTER_CHECK_NONE && graph->argument_counts[node] != 0 ? graph->argument_nodes[graph->argument_offsets[node]] : BUSTER_CHECK_NONE;
            foldable = index == BUSTER_CHECK_NONE || stream->queries[index].source_proof != CHECK_BUILTIN_QUERY_PROOF_ZERO ||
                       (argument < graph->count && (check_stream_flags(stream, argument) & BUSTER_CHECK_SIDE_EFFECT) != 0);
        }
        else if (op == CHECK_OP_STATEMENT_VALUE)
        {
            // Its statements before its value, then its value.
            const uint32_t token = graph->tokens[node];
            const uint32_t region = input->region_count != 0 && token < input->token_count ? input->expression_regions[token] : 0;
            const CheckInputRegion* source = region != 0 && region <= input->region_count ? input->regions + region - 1 : NULL;
            foldable = source != NULL && source->block < input->token_count && source->result < graph->count;
            foldable = foldable && check_control_fold_push(writer, &depth, visits, source->result);
            for (uint32_t child = foldable ? writer->first[source->block] : BUSTER_CHECK_NONE; foldable && child != BUSTER_CHECK_NONE; child = writer->next[child])
            {
                const bool statement = child != source->final_statement && writer->parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK;
                foldable = !statement || check_control_fold_push(writer, &depth, visits, child | BUSTER_CHECK_CONTROL_FOLD_STATEMENT);
            }
        }
        else if (op != CHECK_OP_VALUE && op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF)
        {
            uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            // A comma's left operand is evaluated for nothing but its value (check_control_fold_operand, which walks it).
            foldable = op != CHECK_OP_COMMA || check_control_fold_operand(writer, operands[0], &depth, visits);
            operands[0] = op == CHECK_OP_COMMA ? BUSTER_CHECK_NONE : operands[0];
            bool truth = false;
            const CheckValue condition = (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL) && operands[0] < graph->count ?
                                         check_stream_value(stream, operands[0]) : (CheckValue){ 0 };
            if (condition.constant_kind != CHECK_CONSTANT_NONE && check_action_truth(program->types, &condition, &truth))
            {
                operands[1] = op == CHECK_OP_CONDITIONAL ? (truth ? operands[1] : BUSTER_CHECK_NONE) : truth == (op == CHECK_OP_LOGICAL_AND) ? operands[1] : BUSTER_CHECK_NONE;
                operands[2] = op == CHECK_OP_CONDITIONAL && !truth ? operands[2] : BUSTER_CHECK_NONE;
            }
            // GNU's `a ?: b` reuses its condition as its first arm.
            operands[1] = op == CHECK_OP_CONDITIONAL && operands[1] == operands[0] ? BUSTER_CHECK_NONE : operands[1];
            const uint32_t arguments = check_graph_has_arguments(op) ? graph->argument_counts[node] : 0;
            for (uint32_t k = 0; foldable && k < BUSTER_ARRAY_LENGTH(operands) + arguments; k += 1)
            {
                const uint32_t operand = k < BUSTER_ARRAY_LENGTH(operands) ? operands[k] : graph->argument_nodes[graph->argument_offsets[node] + k - BUSTER_ARRAY_LENGTH(operands)];
                foldable = operand >= graph->count || check_control_fold_push(writer, &depth, visits, operand);
            }
        }
    }
    return foldable;
}

#include "c_check_program_fold.c"

// Leaves tokens `low` to `high` out of the program (CheckProgram.dropped_ranges): their statement expressions' regions
// are written empty (check_control_build), and the cases of their own switches go where the writer is, since every
// case of a body has a place.
static void check_control_drop(CheckControlWriter* writer, uint32_t low, uint32_t high)
{
    const uint8_t* roles = writer->parser->tree.roles;
    const CheckInput* input = writer->program->input;
    const uint32_t* expression_regions = writer->dropped_regions != NULL ? input->expression_regions : NULL;
    for (uint32_t token = low; token <= high; token += 1)
    {
        if (roles[token] == PARSE_ROLE_CLOSE_CASE)
        {
            check_control_mark(writer, writer->source_labels[token]);
        }
        const uint32_t region = expression_regions != NULL ? expression_regions[token] : 0;
        if (region != 0 && region <= input->region_count)
        {
            writer->dropped_regions[region - 1] = 1;
        }
    }
    writer->dropped_ranges[writer->dropped_range_count++] = ((uint64_t)low << 32) | high;
}

#include "c_check_program_switch.c"

// Appends a source label's number, a writer's label, to the writer's CheckProgram.label_positions, which the bodies'
// loop turns into its place once the label's body is written: a label is a token, so the column has room.
static void check_control_source_label(CheckControlWriter* writer, uint32_t label)
{
    writer->label_positions[writer->label_position_count++] = label;
}

// A named goto or asm goto can only name a label of this body.
static uint32_t check_control_named_target(const CheckControlWriter* writer, uint32_t token)
{
    return token > writer->opening && token < writer->closing && writer->parser->tree.roles[token] == PARSE_ROLE_LABEL ?
           writer->source_labels[token] : BUSTER_CHECK_NONE;
}

static void check_control_expand(CheckControlWriter* writer, CheckControlTask task)
{
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const uint32_t token = task.token;
    const uint8_t role = parser->tree.roles[token];
    if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        // A block's variable-length arrays are released at its end, after its statements and its cleanups.
        if (role == PARSE_ROLE_CLOSE_BLOCK && writer->variable && writer->vla_blocks[token] != 0)
        {
            check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, token, BUSTER_CHECK_NONE, token);
        }
        // Its objects' lifetimes (#177): their end after its statements and its cleanups, their start before its statements.
        const bool lifetimes = role == PARSE_ROLE_CLOSE_BLOCK && writer->program->lifetime_first != NULL && writer->program->lifetime_first[token] != BUSTER_CHECK_NONE;
        if (lifetimes)
        {
            check_control_entry(writer, CHECK_CONTROL_LIFETIME, token, 1, token);
        }
        const uint32_t block = role == PARSE_ROLE_CLOSE_BLOCK && writer->cleanup_count != 0 ? check_control_opening(writer, token, PARSE_ROLE_OPEN_BLOCK) :
                                                                                            BUSTER_CHECK_NONE;
        if (block != BUSTER_CHECK_NONE)
        {
            check_control_cleanups(writer, token, BUSTER_CHECK_NONE, block);
        }
        for (uint32_t child = writer->last[token]; child != BUSTER_CHECK_NONE; child = writer->previous[child])
        {
            if (parser->tree.roles[child] != PARSE_ROLE_OPEN_BODY && parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
            {
                check_control_statement(writer, child, task.stop, task.repeat);
            }
        }
        if (lifetimes)
        {
            check_control_entry(writer, CHECK_CONTROL_LIFETIME, token, 0, token);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_CONDITION)
    {
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        uint32_t keyword = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            if (child < token && parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT)
            {
                keyword = child;
            }
            else if (child > token)
            {
                if (parser->tree.roles[child] == PARSE_ROLE_ELSE)
                {
                    alternate = child;
                }
                else
                {
                    body = child;
                }
            }
        }
        const uint32_t end = check_control_label(writer);
        const uint32_t condition = input->token_nodes[token];
        if (keyword == BUSTER_CHECK_NONE || condition == BUSTER_CHECK_NONE || body == BUSTER_CHECK_NONE)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_IF)
        {
            // clang -O0's code (#1108): the arm a constant condition runs, without the condition (CheckProgram.
            // dropped_ranges). The condition's node is its truth (CHECK_OP_CONDITION); clang folds the expression's, of
            // an integer type, unless it holds a label itself (CodeGenFunction::ConstantFoldsToSimpleInteger), by its
            // source value, not a builtin query's runtime value, where clang's evaluation would fold it outside a
            // constant context too (check_control_foldable: not __builtin_constant_p of a variable).
            const CheckGraph* graph = &input->graph;
            const uint32_t expression = graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
            bool truth = false;
            const bool folded = check_control_fold_truth(writer, expression, &truth) &&
                                !check_control_labeled(writer, keyword + 1, token) && !check_control_enterable(writer, truth ? alternate : body);
            const uint32_t executed = truth ? body : alternate;
            const uint32_t skipped = truth ? alternate : body;
            if (folded)
            {
                // The condition and the arm left out are dropped ranges; the arm takes its own switches with it, whose
                // cases go where the if ends.
                uint32_t low = 0;
                uint32_t high = 0;
                check_control_extent(writer, skipped, &low, &high);
                check_control_mark(writer, end);
                check_control_drop(writer, keyword + 1, token);
                if (skipped != BUSTER_CHECK_NONE)
                {
                    check_control_drop(writer, low, high);
                }
                if (executed != BUSTER_CHECK_NONE)
                {
                    check_control_statement(writer, executed, task.stop, task.repeat);
                }
            }
            else
            {
                const uint32_t otherwise = alternate != BUSTER_CHECK_NONE ? check_control_label(writer) : end;
                check_control_mark(writer, end);
                if (alternate != BUSTER_CHECK_NONE)
                {
                    check_control_statement(writer, alternate, task.stop, task.repeat);
                    check_control_mark(writer, otherwise);
                    check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, end);
                }
                check_control_statement(writer, body, task.stop, task.repeat);
                check_control_entry(writer, CHECK_CONTROL_BRANCH_FALSE, token, condition, otherwise);
            }
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_WHILE)
        {
            const uint32_t start = check_control_label(writer);
            if (writer->loop_targets)
            {
                writer->stops[token] = end;
                writer->repeats[token] = start;
            }
            check_control_mark(writer, end);
            check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, start);
            check_control_statement(writer, body, end, start);
            check_control_entry(writer, CHECK_CONTROL_BRANCH_FALSE, token, condition, end);
            check_control_mark(writer, start);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
        {
            check_control_switch(writer, keyword, token, body, condition, end, task.repeat);
        }
        else
        {
            check_program_fail(writer->program, CHECK_PROGRAM_UNSUPPORTED, keyword);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_FOR)
    {
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            if (child < token && parser->tokens.kinds[child] == PARSE_KIND_SEMI)
            {
                if (first == BUSTER_CHECK_NONE)
                {
                    first = child;
                }
                else
                {
                    condition = child;
                }
            }
            else if (child > token)
            {
                body = child;
            }
        }
        const uint32_t start = check_control_label(writer);
        const uint32_t update = check_control_label(writer);
        const uint32_t end = check_control_label(writer);
        if (writer->loop_targets)
        {
            writer->stops[token] = end;
            writer->repeats[token] = update;
        }
        if (first == BUSTER_CHECK_NONE || condition == BUSTER_CHECK_NONE || body == BUSTER_CHECK_NONE)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
        }
        else
        {
            // Its own variable-length arrays are released where it ends (#1447), after its own variables' cleanups, which
            // run there too, which its condition and its breaks reach.
            if (writer->variable && writer->vla_blocks[token] != 0)
            {
                check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, token, BUSTER_CHECK_NONE, token);
            }
            const uint32_t scope = writer->cleanup_count != 0 ? check_control_opening(writer, token, PARSE_ROLE_OPEN_FOR) : BUSTER_CHECK_NONE;
            if (scope != BUSTER_CHECK_NONE)
            {
                check_control_cleanups(writer, token, BUSTER_CHECK_NONE, scope);
            }
            check_control_mark(writer, end);
            check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, start);
            check_control_token(writer, token);
            check_control_mark(writer, update);
            check_control_statement(writer, body, end, update);
            const uint32_t node = check_control_statement_head(writer, condition);
            if (node != BUSTER_CHECK_NONE)
            {
                check_control_entry(writer, CHECK_CONTROL_BRANCH_FALSE, condition, node, end);
            }
            check_control_mark(writer, start);
            check_control_token(writer, first);
        }
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        uint32_t jump = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            const uint8_t child_role = parser->tree.roles[child];
            // A return's attributes' specifiers are its parents (a musttail return's), the last one its terminator's child.
            const uint32_t attributed = child_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ? check_control_attributed(writer, child) : BUSTER_CHECK_NONE;
            jump = attributed != BUSTER_CHECK_NONE ? attributed : child_role == PARSE_ROLE_RETURN ? child : jump;
            body = child_role == PARSE_ROLE_DO ? child : body;
            condition = child_role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
        }
        if (jump != BUSTER_CHECK_NONE)
        {
            const uint8_t kind = parser->tokens.kinds[jump];
            if (kind == PARSE_KIND_RETURN && input->musttail_count != 0 && check_control_musttail(input, jump) &&
                !check_control_static_chain(input, input->token_nodes[jump]))
            {
                // A musttail return's call, a tail call: no cleanup is in its scopes (c_check_musttail.c).
                check_control_entry(writer, CHECK_CONTROL_RETURN, jump, input->token_nodes[jump], BUSTER_CHECK_CONTROL_TAIL);
            }
            else if (kind == PARSE_KIND_RETURN)
            {
                // With cleanups to run, the value first, then the cleanups, then the return of the value; but the named
                // return value's, which the cleanups may write, after them.
                const uint32_t node = input->token_nodes[jump];
                const bool cleans = writer->cleanup_count != 0 && check_control_cleanup_at(writer, jump) != BUSTER_CHECK_NONE;
                const bool kept = cleans && node != BUSTER_CHECK_NONE && !check_control_named(writer, jump);
                check_control_entry(writer, CHECK_CONTROL_RETURN, jump, kept ? BUSTER_CHECK_NONE : node, kept ? BUSTER_CHECK_CONTROL_KEPT : BUSTER_CHECK_NONE);
                if (cleans)
                {
                    check_control_cleanups(writer, jump, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                }
                if (kept)
                {
                    check_control_entry(writer, CHECK_CONTROL_RESULT, jump, node, BUSTER_CHECK_NONE);
                }
            }
            else if ((kind == PARSE_KIND_BREAK && task.stop != BUSTER_CHECK_NONE) || (kind == PARSE_KIND_CONTINUE && task.repeat != BUSTER_CHECK_NONE))
            {
                // The entries come out in the reverse of their pushes: the cleanups, the restore, then the jump. Its loop's
                // or switch's token is inside the scopes it doesn't leave: a for statement's own variables' too, whose
                // cleanups run at its end.
                const bool stops = kind == PARSE_KIND_BREAK;
                const uint32_t* targets = stops ? writer->stops : writer->repeats;
                const uint32_t label = stops ? task.stop : task.repeat;
                check_control_entry(writer, CHECK_CONTROL_JUMP, jump, BUSTER_CHECK_NONE, label);
                check_control_lifetimes_left(writer, jump, BUSTER_CHECK_NONE, targets, label);
                check_control_leave(writer, jump, BUSTER_CHECK_NONE, targets, label);
                if (writer->cleanup_count != 0)
                {
                    check_control_cleanups(writer, jump, check_control_loop(writer, jump, targets, label), BUSTER_CHECK_NONE);
                }
            }
            else if (kind == PARSE_KIND_GOTO && check_input_indirect_goto(writer->parser, jump))
            {
                const uint32_t node = input->token_nodes[jump + 1];
                if (node != BUSTER_CHECK_NONE)
                {
                    check_control_entry(writer, CHECK_CONTROL_JUMP_INDIRECT, jump, node, BUSTER_CHECK_NONE);
                }
                else
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_INVALID, jump);
                }
            }
            else if (kind == PARSE_KIND_GOTO)
            {
                const uint32_t target = check_sparse_get(input->label_targets, jump);
                const uint32_t label = check_control_named_target(writer, target);
                if (label != BUSTER_CHECK_NONE)
                {
                    check_control_entry(writer, CHECK_CONTROL_JUMP, jump, BUSTER_CHECK_NONE, label);
                    check_control_lifetimes_left(writer, jump, target, 0, BUSTER_CHECK_NONE);
                    check_control_leave(writer, jump, target, 0, BUSTER_CHECK_NONE);
                    check_control_cleanups(writer, jump, target, BUSTER_CHECK_NONE);
                }
                else
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_INVALID, jump);
                }
            }
            else if ((kind != PARSE_KIND_BREAK && kind != PARSE_KIND_CONTINUE) || !input->recovered_valid)
            {
                // (A `break` or `continue` outside its loop, whose error the reader recovered from, #1447's semantic
                // recovery, phase 3, is clang's invalid statement, which does nothing: its unit is never emitted.)
                check_program_fail(writer->program, CHECK_PROGRAM_UNSUPPORTED, jump);
            }
        }
        else if (body != BUSTER_CHECK_NONE && condition != BUSTER_CHECK_NONE)
        {
            const uint32_t start = check_control_label(writer);
            const uint32_t test = check_control_label(writer);
            const uint32_t end = check_control_label(writer);
            if (writer->loop_targets)
            {
                writer->stops[body] = end;
                writer->repeats[body] = test;
            }
            check_control_mark(writer, end);
            check_control_entry(writer, CHECK_CONTROL_BRANCH_TRUE, condition, input->token_nodes[condition], start);
            check_control_mark(writer, test);
            check_control_statement(writer, body, end, test);
            check_control_mark(writer, start);
        }
        else
        {
            check_control_token(writer, token);
        }
    }
    else if (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
    {
        for (uint32_t child = writer->last[token]; child != BUSTER_CHECK_NONE; child = writer->previous[child])
        {
            if (child > token && child != writer->region_final)
            {
                check_control_statement(writer, child, task.stop, task.repeat);
            }
        }
        if (role == PARSE_ROLE_LABEL && writer->addressed != 0 && writer->addressed[token] != 0)
        {
            check_control_entry(writer, CHECK_CONTROL_LABEL, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        if (role == PARSE_ROLE_LABEL)
        {
            check_control_source_label(writer, writer->source_labels[token]);
        }
        check_control_mark(writer, writer->source_labels[token]);
    }
}

// The body's own token columns, just before its build reads them (the unit's columns, built
// before every body, were read cold): each token's children and siblings from its parent, its source label (a label's or a
// case's, numbered in token order), the cases' targets, and no loop's stop or repeat yet. A parent outside the body links
// nothing: the closing brace's, and the declarator's (an earlier sibling of the opening brace, never a statement).
static void check_control_children(CheckControlWriter* writer, uint32_t opening, uint32_t closing)
{
    const uint32_t* parents = writer->parser->tree.parents;
    const uint8_t* roles = writer->parser->tree.roles;
    const uint64_t extent = (uint64_t)closing - opening + 1;
    memset(writer->first + opening, 0xff, extent * sizeof(uint32_t));
    memset(writer->last + opening, 0xff, extent * sizeof(uint32_t));
    const uint32_t sink = writer->parser->tokens.count;
    for (uint32_t token = opening; token <= closing; token += 1)
    {
        const uint32_t parent = parents[token];
        const bool inside = parent - opening <= closing - opening;
        const uint32_t sibling = inside ? writer->last[parent] : BUSTER_CHECK_NONE;
        writer->previous[token] = sibling;
        writer->next[token] = BUSTER_CHECK_NONE;
        // #892: unconditional stores at a selected index — the columns have count + 1 entries and nothing reads index
        // `sink`, which is never a token — where 44% of these branches' taken instances used to mispredict.
        writer->next[inside && sibling != BUSTER_CHECK_NONE ? sibling : sink] = token;
        writer->first[inside && sibling == BUSTER_CHECK_NONE ? parent : sink] = token;
        writer->last[inside ? parent : sink] = token;
    }
    writer->loop_targets = writer->all_loop_targets || (writer->region_first != NULL && writer->region_first[opening] != BUSTER_CHECK_NONE);
    if (writer->loop_targets)
    {
        memset(writer->stops + opening, 0xff, extent * sizeof(uint32_t));
        memset(writer->repeats + opening, 0xff, extent * sizeof(uint32_t));
    }
    writer->case_start = BUSTER_CHECK_NONE;
    writer->case_end = 0;
    for (uint32_t token = opening + 1; token < closing; token += 64)
    {
        const uint32_t count = closing - token < 64 ? closing - token : 64;
        uint64_t labels = 0;
        if (BUSTER_SCALAR_PATH(writer->scalar_children))
        {
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                const uint8_t role = roles[token + lane];
                labels |= (uint64_t)(role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE) << lane;
            }
        }
        else
        {
            const uint64_t live = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i block = _mm512_maskz_loadu_epi8(live, roles + token);
            labels = live & (_mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8(PARSE_ROLE_LABEL)) |
                             _mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8(PARSE_ROLE_CLOSE_CASE)));
        }
        for (; labels != 0; labels &= labels - 1)
        {
            const uint32_t position = token + (uint32_t)__builtin_ctzll(labels);
            const uint32_t label = check_control_label(writer);
            writer->source_labels[position] = label;
            if (label != BUSTER_CHECK_NONE && roles[position] == PARSE_ROLE_CLOSE_CASE)
            {
                const uint32_t index = writer->case_map[position];
                writer->program->cases[index].target = label;
                writer->case_start = writer->case_start == BUSTER_CHECK_NONE ? index : writer->case_start;
                writer->case_end = index + 1;
            }
        }
    }
}

static void check_control_begin(CheckControlWriter* writer, uint32_t opening)
{
    const uint32_t closing = writer->parser->tree.parents[opening];
    writer->depth = 0;
    writer->label_count = 0;
    writer->first_entry = writer->count;
    writer->opening = opening;
    writer->closing = closing;
    check_control_children(writer, opening, closing);
    // The body's cleanups, its records in source order, each with its parent: the last before it whose scope holds its
    // declaration, which the parents of the one before it reach, as nested scopes are.
    const CheckInput* input = writer->program->input;
    const CheckInputCleanup* cleanups = input->cleanups;
    uint32_t first = 0;
    uint32_t end = input->cleanup_count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        first = cleanups[middle].declaration < opening ? middle + 1 : first;
        end = cleanups[middle].declaration < opening ? end : middle;
    }
    end = first;
    while (end < input->cleanup_count && cleanups[end].declaration < closing)
    {
        uint32_t parent = end != first ? end - 1 : BUSTER_CHECK_NONE;
        while (parent != BUSTER_CHECK_NONE && cleanups[parent].end <= cleanups[end].declaration)
        {
            parent = writer->cleanup_parents[parent];
        }
        writer->cleanup_parents[end] = parent;
        writer->cleanup_tops[end] = parent != BUSTER_CHECK_NONE && cleanups[parent].scope == cleanups[end].scope ? writer->cleanup_tops[parent] : end;
        if (cleanups[end].scope < input->token_count)
        {
            writer->cleanup_scope_last[cleanups[end].scope] = end;
        }
        end += 1;
    }
    writer->cleanup_first = first;
    writer->cleanup_count = end - first;
    // A body has its function declarator as an earlier sibling of its opening brace. Only body statements enter
    // this worklist; this also avoids interpreting a function's parameter/declarator expressions as executable.
    for (uint32_t child = writer->last[closing]; child != BUSTER_CHECK_NONE; child = writer->previous[child])
    {
        if (child > opening)
        {
            check_control_statement(writer, child, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
    }
    if (writer->variable && writer->vla_first[opening] != BUSTER_CHECK_NONE)
    {
        check_control_token(writer, opening);
    }
}

static void check_control_drain(CheckControlWriter* writer)
{
    while (writer->depth != 0 && writer->program->status == CHECK_PROGRAM_OK)
    {
        const CheckControlTask task = writer->tasks[--writer->depth];
        if (task.kind == CHECK_CONTROL_TASK_LABEL)
        {
            writer->labels[task.token] = writer->count - writer->first_entry;
        }
        else if (task.kind == CHECK_CONTROL_TASK_ENTRY)
        {
            if (writer->count < writer->capacity)
            {
                writer->entries[writer->count++] = task.entry;
            }
            else
            {
                check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
            }
        }
        else if (task.kind == CHECK_CONTROL_TASK_TOKEN)
        {
            const uint32_t assembly = writer->asm_statements != NULL ? writer->asm_statements[task.token] : BUSTER_CHECK_NONE;
            if (assembly != BUSTER_CHECK_NONE)
            {
                if (writer->count < writer->capacity)
                {
                    writer->entries[writer->count++] = (CheckControl){ .kind = CHECK_CONTROL_ASM, .node = BUSTER_CHECK_NONE, .token = task.token, .target = assembly };
                }
                else
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
                }
            }
            const uint32_t plan = check_program_bit(writer->planned, task.token) ? writer->plan_first[task.token] : BUSTER_CHECK_NONE;
            uint32_t i = plan;
            uint32_t v = writer->variable ? writer->vla_first[task.token] : BUSTER_CHECK_NONE;
            uint32_t a = writer->variable ? writer->allocation_first[task.token] : BUSTER_CHECK_NONE;
            // A variably modified typeof's operand (check_input_expressions' evaluation_nodes) is a side effect of the
            // declaration, after its type's variable bounds and before its initializers.
            const uint32_t evaluation = check_sparse_present(writer->program->input->evaluation_nodes) ?
                                        check_sparse_get(writer->program->input->evaluation_nodes, task.token) : BUSTER_CHECK_NONE;
            bool evaluated = false;
            while ((i != BUSTER_CHECK_NONE || v != BUSTER_CHECK_NONE || a != BUSTER_CHECK_NONE) && writer->program->status == CHECK_PROGRAM_OK)
            {
                const CheckInitializers* initializers = writer->program->initializers;
                const CheckInput* input = writer->program->input;
                // A declaration's bounds first, then its array's allocation (an allocation's list holds its declaration),
                // then its initializer.
                const uint32_t bounded = v != BUSTER_CHECK_NONE ? input->vlas[v].declaration : BUSTER_CHECK_NONE;
                const uint32_t initialized = i != BUSTER_CHECK_NONE ? initializers->declaration_tokens[i] : BUSTER_CHECK_NONE;
                const bool bound = v != BUSTER_CHECK_NONE && bounded <= a && bounded <= initialized;
                const bool allocation = !bound && a != BUSTER_CHECK_NONE && a <= initialized;
                const uint32_t declaration = bound ? bounded : initialized;
                if (evaluation != BUSTER_CHECK_NONE && !evaluated && !bound)
                {
                    if (writer->count < writer->capacity)
                    {
                        writer->entries[writer->count++] = (CheckControl){ .kind = CHECK_CONTROL_EVALUATE, .node = evaluation, .token = task.token, .target = BUSTER_CHECK_NONE };
                        evaluated = true;
                    }
                    else
                    {
                        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
                    }
                }
                if (allocation)
                {
                    const uint32_t closing = writer->parser->tree.parents[task.token];
                    const CheckControl save = { .kind = CHECK_CONTROL_STACK_SAVE, .node = BUSTER_CHECK_NONE, .token = a, .target = closing };
                    const CheckControl allocate = { .kind = CHECK_CONTROL_ALLOCATE, .node = BUSTER_CHECK_NONE, .token = a, .target = closing };
                    if (writer->count + 1 < writer->capacity)
                    {
                        writer->entries[writer->count] = save;
                        writer->count += writer->vla_blocks[closing] == a + 1 ? 1 : 0;
                        writer->entries[writer->count++] = allocate;
                    }
                    else
                    {
                        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, a);
                    }
                    a = writer->allocation_next[a];
                }
                else if (bound || !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC))
                {
                    if (writer->count < writer->capacity)
                    {
                        writer->entries[writer->count++] = (CheckControl)
                        {
                            .kind = bound ? CHECK_CONTROL_VLA : CHECK_CONTROL_INITIALIZE,
                            .node = bound ? input->vlas[v].bound_node : BUSTER_CHECK_NONE,
                            .token = declaration,
                            .target = bound ? v : i,
                        };
                    }
                    else
                    {
                        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, declaration);
                    }
                }
                if (bound)
                {
                    v = writer->vla_next[v];
                }
                else if (!allocation)
                {
                    i = writer->plan_next[i];
                }
            }
            if (evaluation != BUSTER_CHECK_NONE && !evaluated && writer->program->status == CHECK_PROGRAM_OK)
            {
                // A declaration whose only entries were variable bounds: its operand follows them.
                if (writer->count < writer->capacity)
                {
                    writer->entries[writer->count++] = (CheckControl){ .kind = CHECK_CONTROL_EVALUATE, .node = evaluation, .token = task.token, .target = BUSTER_CHECK_NONE };
                }
                else
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
                }
            }
            // The graph's token groups are in postorder. Each wrapper here is a complete discarded expression
            // or declaration initializer; expressions of nested statements have their own source tokens.
            for (uint32_t node = plan == BUSTER_CHECK_NONE && assembly == BUSTER_CHECK_NONE ? check_control_statement_head(writer, task.token) : BUSTER_CHECK_NONE; node != BUSTER_CHECK_NONE; node = check_control_statement_next(writer, task.token, node))
            {
                // The evaluated typeof operand is already a control entry; the discard wrapper the expression walk
                // made of its value at the terminator must not evaluate it again.
                uint32_t operand = node;
                while (operand != BUSTER_CHECK_NONE && (writer->program->input->graph.ops[operand] == CHECK_OP_CAST ||
                                                        writer->program->input->graph.ops[operand] == CHECK_OP_CONVERT))
                {
                    operand = writer->program->input->graph.operand0[operand];
                }
                if (evaluation != BUSTER_CHECK_NONE && operand == evaluation)
                {
                    continue;
                }
                if (!check_control_static(writer, node))
                {
                    if (writer->count < writer->capacity)
                    {
                        writer->entries[writer->count++] = (CheckControl)
                        {
                            .node = node,
                            .token = task.token,
                            .target = BUSTER_CHECK_NONE,
                            .kind = CHECK_CONTROL_EVALUATE,
                        };
                    }
                    else
                    {
                        check_program_fail(writer->program, CHECK_PROGRAM_RESOURCE, task.token);
                    }
                }
            }
        }
        else
        {
            check_control_expand(writer, task);
        }
    }
}

static void check_control_end(CheckControlWriter* writer, uint32_t token)
{
    check_control_entry(writer, CHECK_CONTROL_END, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    check_control_drain(writer);
}

// Whether `token` is one of the body's, whose bits in region_known check_control_build clears before its regions: a token
// outside it may have a bit an earlier body set.
static bool check_control_region_inside(const CheckControlWriter* writer, uint32_t token)
{
    return writer->opening <= writer->closing && writer->closing < writer->program->input->token_count && token - writer->opening <= writer->closing - writer->opening;
}

// Whether a token of the body being written keeps its region targets (check_control_region_targets).
static bool check_control_region_kept(const CheckControlWriter* writer, uint32_t token)
{
    return check_control_region_inside(writer, token) && check_program_bit(writer->region_known, token);
}

// The labels a GNU break and continue take in the statement expression that opens at `token` (#2995): the stop and the
// repeat, each on its own, of its nearest ancestor below the body's closing brace that has one and is before it or is a
// do statement's (a loop's own controlling expression belongs to its enclosing loop). Every subtree is a run of tokens
// (docs/c-parser.md), so an ancestor of a token's parent is before the token where it is before the parent: a token's
// answer is its parent's label where the parent qualifies and has one, and the parent's own answer otherwise. The walk
// goes up to the first token that keeps its answers, linking each token it passes to the one below it in that token's
// room, then down again, keeping each one's: nested statement expressions, or many under one deep statement, walk each
// token once, where each walked up to the body. A kept answer stays right: the regions come in token order, outer ones
// first (check_input_region_initialize), and a region's statements set the labels of the loops and switches inside it
// alone, which only a later region is inside. It stays scalar, a chain of dependent loads from a few starts that passes a
// token at most once a body: docs/c-checker.md has the counts against a data-parallel pass over every token.
static void check_control_region_targets(CheckControlWriter* writer, uint32_t token, uint32_t* stop, uint32_t* repeat)
{
    const uint32_t count = writer->program->input->token_count;
    const uint32_t* parents = writer->parser->tree.parents;
    const uint8_t* roles = writer->parser->tree.roles;
    const uint32_t closing = writer->closing;
    uint32_t* stops = writer->region_stops;
    uint32_t* repeats = writer->region_repeats;
    uint32_t below = token;
    uint32_t above = token < count ? parents[token] : count;
    uint32_t steps = 0;
    while (above < count && above != closing && steps < count && !check_control_region_kept(writer, above))
    {
        stops[above] = below;
        below = above;
        above = parents[above];
        steps += 1;
    }
    // From the highest token passed down to `token`, each one's answers from its parent's: none above the body.
    uint32_t parent = above;
    bool answered = above < count && above != closing && check_control_region_kept(writer, above);
    uint32_t at = below;
    uint32_t stop_label = BUSTER_CHECK_NONE;
    uint32_t repeat_label = BUSTER_CHECK_NONE;
    for (uint32_t step = 0; token < count && step <= steps; step += 1)
    {
        // The link down, before the token's answer takes its room.
        const uint32_t next = at != token ? stops[at] : BUSTER_CHECK_NONE;
        const bool qualifies = answered && (parent < at || roles[parent] == PARSE_ROLE_DO);
        const uint32_t parent_stop = qualifies ? writer->stops[parent] : BUSTER_CHECK_NONE;
        const uint32_t parent_repeat = qualifies ? writer->repeats[parent] : BUSTER_CHECK_NONE;
        stop_label = parent_stop != BUSTER_CHECK_NONE ? parent_stop : answered ? stops[parent] : BUSTER_CHECK_NONE;
        repeat_label = parent_repeat != BUSTER_CHECK_NONE ? parent_repeat : answered ? repeats[parent] : BUSTER_CHECK_NONE;
        stops[at] = stop_label;
        repeats[at] = repeat_label;
        if (check_control_region_inside(writer, at))
        {
            check_program_bit_set(writer->region_known, at);
        }
        parent = at;
        answered = true;
        at = next;
    }
    if (writer->region_work != NULL)
    {
        *writer->region_work += steps;
    }
    *stop = stop_label;
    *repeat = repeat_label;
}

static void check_control_build(CheckControlWriter* writer, uint32_t opening)
{
    const CheckInput* input = writer->program->input;
    const uint32_t closing = writer->parser->tree.parents[opening];
    check_control_begin(writer, opening);
    check_control_drain(writer);
    // The body's own variables' cleanups, where it ends.
    if (writer->cleanup_count != 0)
    {
        check_control_cleanups(writer, closing, BUSTER_CHECK_NONE, opening);
        check_control_drain(writer);
    }
    check_control_end(writer, closing);
    writer->entry_count = writer->count - writer->first_entry;
    const uint32_t first_region = writer->region_first != NULL ? writer->region_first[opening] : BUSTER_CHECK_NONE;
    // No token of the body keeps its region targets yet (check_control_region_targets).
    if (first_region != BUSTER_CHECK_NONE && opening <= closing && closing < input->token_count)
    {
        memset(writer->region_known + opening / 64, 0, ((uint64_t)closing / 64 - opening / 64 + 1) * sizeof(uint64_t));
    }
    for (uint32_t r = first_region; r != BUSTER_CHECK_NONE && writer->program->status == CHECK_PROGRAM_OK; r = writer->region_next[r])
    {
        const CheckInputRegion* source = input->regions + r;
        CheckProgramRegion* region = writer->program->regions + r;
        region->first = writer->count - writer->first_entry;
        region->body = writer->body;
        region->node = input->token_nodes[source->closing];
        region->result = source->result;
        uint32_t stop = BUSTER_CHECK_NONE;
        uint32_t repeat = BUSTER_CHECK_NONE;
        check_control_region_targets(writer, source->opening, &stop, &repeat);
        if (source->block >= input->token_count || region->node >= input->graph.count || source->result >= input->graph.count)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, source->opening);
        }
        else if (writer->dropped_regions != NULL && writer->dropped_regions[r] != 0)
        {
            // A dropped range's (check_control_expand's constant if): no code reaches it, and its statements' loops
            // and switches, which a break or continue in it would need, weren't written. Its END alone keeps it a region.
            check_control_end(writer, source->closing);
            region->cleanups = 0;
            region->count = writer->count - writer->first_entry - region->first;
        }
        else
        {
            writer->region_final = source->final_statement;
            for (uint32_t child = writer->last[source->block]; child != BUSTER_CHECK_NONE; child = writer->previous[child])
            {
                if (child != source->final_statement && writer->parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
                {
                    check_control_statement(writer, child, stop, repeat);
                }
            }
            check_control_drain(writer);
            check_control_end(writer, source->closing);
            writer->region_final = BUSTER_CHECK_NONE;
            // Its block's cleanups, after its END: they run once its value is computed (check_lower_region), and then its
            // variable-length arrays' release (#1447), as a block's end has them.
            const uint32_t ended = writer->count;
            const uint32_t block = writer->cleanup_count != 0 ? check_control_opening(writer, source->block, PARSE_ROLE_OPEN_BLOCK) : BUSTER_CHECK_NONE;
            if (block != BUSTER_CHECK_NONE)
            {
                check_control_cleanups(writer, source->block, BUSTER_CHECK_NONE, block);
                check_control_drain(writer);
            }
            if (writer->variable && writer->vla_blocks[source->block] != 0)
            {
                check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, source->block, BUSTER_CHECK_NONE, source->block);
                check_control_drain(writer);
            }
            region->cleanups = writer->count - ended;
            region->count = writer->count - writer->first_entry - region->first;
        }
    }
    const uint32_t end = writer->entry_count - 1u;
    writer->targets_end = false;
    for (uint32_t i = writer->first_entry; i < writer->count && writer->program->status == CHECK_PROGRAM_OK; i += 1)
    {
        CheckControl* entry = &writer->entries[i];
        if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
        {
            if (entry->target >= writer->label_count || writer->labels[entry->target] == BUSTER_CHECK_NONE)
            {
                check_program_fail(writer->program, CHECK_PROGRAM_INVALID, entry->token);
            }
            else
            {
                entry->target = writer->labels[entry->target];
                writer->targets_end |= entry->target == end;
            }
        }
        else if (entry->kind == CHECK_CONTROL_ASM)
        {
            const CheckInputAsm* assembly = input->asm_statements + entry->target;
            for (uint32_t l = 0; l < assembly->label_count; l += 1)
            {
                const uint32_t token = assembly->labels[l].target;
                const uint32_t label = check_control_named_target(writer, token);
                if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_INVALID, assembly->labels[l].token);
                }
                else
                {
                    writer->program->asm_targets[writer->program->asm_offsets[entry->target] + l] = writer->labels[label];
                    writer->targets_end |= writer->labels[label] == end;
                }
            }
        }
    }
    // The body's cases, in token order: a contiguous run of the program's, since each case of a body has one.
    for (uint32_t c = writer->case_start; c < writer->case_end && writer->program->status == CHECK_PROGRAM_OK; c += 1)
    {
        CheckProgramCase* item = writer->program->cases + c;
        const uint32_t label = item->target;
        if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, item->token);
        }
        else
        {
            item->target = writer->labels[label];
            writer->targets_end |= item->target == end;
        }
    }
}

#include "c_check_return_flow.c"

// Aggregate emission retains both conditional edges and removes unreachable transfers without renumbering labels.
// The warning folds conditions. First assume every expression returns: if END is unreachable even then, no
// expression contract can make it reachable. Only a body that can fall through needs its accurate return contracts.
static void check_program_return_flow(CheckControlWriter* writer, CheckProgramBody* body, CheckReturnFacts* return_facts, bool return_warnings,
                                      bool noreturn_warnings)
{
    CheckProgram* program = writer->program;
    const TypeId result = type_conversion_operand(program->types, type_return(program->types, program->input->declared_types[body->declaration]));
    const uint32_t kind = type_kind(program->types, result);
    const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_COMPLEX;
    const bool noreturn = program->input->function_flags != NULL &&
                          (program->input->function_flags[body->declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0;
    // main returns 0 without a return statement, so it has no -Wreturn-type fall-off warning.
    const bool scalar = return_warnings && result != 0 && kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && !aggregate && !body->is_main && !noreturn;
    if (program->status == CHECK_PROGRAM_OK && (aggregate || scalar || (noreturn_warnings && noreturn)) && body->count != 0 && body->count == body->entry_count)
    {
        CheckControl* entries = writer->entries + body->first;
        // A scalar return needs no emission walk. END can only be reached by a resolved target or by falling
        // through its preceding entry. An indirect jump can enter only a source LABEL.
        if (scalar && !writer->targets_end && body->count >= 2 && entries[body->count - 1].kind == CHECK_CONTROL_END)
        {
            const uint32_t last = entries[body->count - 2].kind;
            if (last == CHECK_CONTROL_RETURN || last == CHECK_CONTROL_JUMP || last == CHECK_CONTROL_JUMP_INDIRECT || last == CHECK_CONTROL_END)
            {
                return;
            }
        }
        uint8_t* seen = (uint8_t*)writer->tasks;
        uint32_t* queue = writer->labels;
        const CheckGraph* graph = &program->input->graph;
        enum { EMISSION, POSSIBLE_FALLTHROUGH, RETURN_WARNING };
        const bool warnings = return_warnings || (noreturn_warnings && noreturn);
        for (uint32_t pass = aggregate ? EMISSION : POSSIBLE_FALLTHROUGH; pass <= (warnings ? RETURN_WARNING : EMISSION); pass += 1)
        {
            bool warning_safe = true;
            memset(seen, 0, body->count);
            uint32_t head = 0;
            uint32_t tail = 1;
            queue[0] = 0;
            seen[0] = 1;
            bool returns = false;
            bool labels = false;
            while (head < tail)
            {
                const uint32_t index = queue[head++];
                const CheckControl* entry = entries + index;
                uint32_t targets[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
                bool next = entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END;
                returns |= entry->kind == CHECK_CONTROL_RETURN;
                if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
                {
                    bool truth = false;
                    const bool jump = entry->kind == CHECK_CONTROL_JUMP;
                    const bool known = pass != EMISSION && !jump && check_program_branch_truth(program, entry->node, entry->node, &truth);
                    const bool taken = truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
                    targets[0] = jump || !known || taken ? entry->target : BUSTER_CHECK_NONE;
                    next = !jump && (!known || !taken);
                }
                else if (pass != EMISSION && entry->kind == CHECK_CONTROL_SWITCH)
                {
                    // Clang also recognizes exhaustive enum switches. Their enumerator set is not retained here;
                    // avoid a false fall-off warning until that coverage fact is available, as for noreturn calls.
                    uint32_t source = entry->node;
                    for (uint32_t steps = 0; source < graph->count && steps < graph->count &&
                         (graph->ops[source] == CHECK_OP_CONVERT || graph->ops[source] == CHECK_OP_POSITIVE); steps += 1)
                    {
                        source = graph->operand0[source];
                    }
                    unsigned __int128 value = 0;
                    const bool known = check_program_case_value(program, entry->node, check_stream_type(program->stream, entry->node), &value, NULL);
                    const bool enumeration = source < graph->count &&
                        type_kind(program->types, type_strip_aligned(program->types, check_stream_type(program->stream, source))) == BUSTER_TYPE_KIND_ENUM;
                    warning_safe &= !enumeration || known;
                }
                else if (entry->kind == CHECK_CONTROL_CASE)
                {
                    const CheckProgramCase* item = program->cases + entry->target;
                    const uint32_t condition = pass != EMISSION ? program->input->token_nodes[item->owner] : BUSTER_CHECK_NONE;
                    CheckProgramCase selected = { .type = item->type };
                    const bool known = pass != EMISSION && check_program_case_value(program, condition, item->type, &selected.lower, &selected.wide_lower);
                    const bool taken = known && check_program_case_compare(program, &selected, false, item, false) >= 0 &&
                                       check_program_case_compare(program, &selected, false, item, true) <= 0;
                    targets[0] = !known || taken ? item->target : BUSTER_CHECK_NONE;
                    next = !known || !taken;
                }
                else if (entry->kind == CHECK_CONTROL_ASM || entry->kind == CHECK_CONTROL_JUMP_INDIRECT)
                {
                    // An indirect jump can enter any source label; asm goto has its own explicit target table.
                    warning_safe = false;
                    if (entry->kind == CHECK_CONTROL_ASM)
                    {
                        for (uint32_t l = program->asm_offsets[entry->target]; l < program->asm_offsets[entry->target + 1]; l += 1)
                        {
                            const uint32_t target = program->asm_targets[l];
                            if (target < body->count && seen[target] == 0)
                            {
                                seen[target] = 1;
                                queue[tail++] = target;
                            }
                        }
                    }
                    else if (!labels)
                    {
                        for (uint32_t l = 0; l < body->count; l += 1)
                        {
                            if (entries[l].kind == CHECK_CONTROL_LABEL && seen[l] == 0)
                            {
                                seen[l] = 1;
                                queue[tail++] = l;
                            }
                        }
                        labels = true;
                    }
                    next = entry->kind != CHECK_CONTROL_JUMP_INDIRECT;
                }
                if (pass == RETURN_WARNING)
                {
                    uint8_t fact = check_program_return_cached(program, return_facts, entry->node);
                    if (entry->kind == CHECK_CONTROL_INITIALIZE && program->initializers != NULL && entry->target < program->initializers->count)
                    {
                        const CheckInitializer* plan = program->initializers->plans + entry->target;
                        for (uint32_t part = 0; part < plan->count; part += 1)
                        {
                            const CheckInitPart* value = plan->parts + part;
                            fact |= value->kind == CHECK_INIT_VALUE ? check_program_return_cached(program, return_facts, value->node) : 0;
                        }
                    }
                    // A guaranteed nonreturning expression ends only this CFG edge. Unknown contracts retain the
                    // previous conservative warning policy; an unrelated proven stop never hides another fall-off.
                    // A path that ends so doesn't fall off: with another that does, clang's warning says "in all control
                    // paths", as after a return.
                    if ((fact & BUSTER_CHECK_RETURN_STOPS) != 0)
                    {
                        targets[0] = BUSTER_CHECK_NONE;
                        next = false;
                        returns = true;
                    }
                    else
                    {
                        warning_safe &= (fact & BUSTER_CHECK_RETURN_UNKNOWN) == 0;
                    }
                }
                targets[1] = next && index + 1 < body->count ? index + 1 : BUSTER_CHECK_NONE;
                for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(targets); t += 1)
                {
                    const uint32_t target = targets[t];
                    if (target < body->count && seen[target] == 0)
                    {
                        seen[target] = 1;
                        queue[tail++] = target;
                    }
                }
            }
            const bool falls = body->count != 0 && seen[body->count - 1] != 0;
            if (pass == EMISSION)
            {
                body->return_flow = aggregate;
                body->falls = falls;
                for (uint32_t i = 0; aggregate && i < body->count; i += 1)
                {
                    if (seen[i] == 0 && entries[i].kind >= CHECK_CONTROL_BRANCH_FALSE)
                    {
                        entries[i].kind = CHECK_CONTROL_END;
                    }
                }
            }
            if (!falls)
            {
                break;
            }
            if (pass == RETURN_WARNING)
            {
                body->return_warning = warning_safe ? noreturn ? (noreturn_warnings ? 3 : 0) : returns ? 2 : 1 : 0;
            }
        }
    }
}

// Collect actual expression edges, never all graph nodes: sizeof/alignof and statically selected generic arms do
// not make an object referenced by an executing body. A declaration identity maps through its canonical object.
static uint32_t check_program_reference_initializer(CheckProgram* program, const CheckInitializer* plan, uint32_t epoch,
                                                     uint32_t* nodes, uint32_t* stack, uint32_t* depth, uint32_t* seen,
                                                     uint64_t* references, uint8_t* needed, uint32_t reference_count)
{
    const CheckGraph* graph = &program->input->graph;
    for (uint32_t part = 0; part < plan->count; part += 1)
    {
        const CheckInitPart* write = plan->parts + part;
        const uint32_t expression = write->node;
        if (write->kind == CHECK_INIT_VALUE && expression < graph->count)
        {
            if (check_stream_overload_head(program->stream, expression) != 0)
            {
                const CheckValue value = write->value;
                if (value.overload_head != 0 || value.constant_kind != CHECK_CONSTANT_ADDRESS || value.object == 0 || value.object >= program->object_count)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, write->token);
                }
                else if (seen[value.object] != epoch)
                {
                    seen[value.object] = epoch;
                    needed[value.object] = 1;
                    references[reference_count++] = (uint64_t)value.object << 32;
                }
            }
            else if (nodes[expression] != epoch)
            {
                nodes[expression] = epoch;
                stack[(*depth)++] = expression;
            }
        }
    }
    return reference_count;
}

// Whether two exits' tails, the controls from `a` and from `b`, `length` of each, are the same: kinds, nodes and targets.
static bool check_program_tails_equal(const CheckControl* controls, uint32_t a, uint32_t b, uint32_t length)
{
    bool equal = true;
    for (uint32_t k = 0; equal && k < length; k += 1)
    {
        const CheckControl* x = controls + a + k;
        const CheckControl* y = controls + b + k;
        equal = x->kind == y->kind && x->node == y->node && x->target == y->target;
    }
    return equal;
}

// The plan of a body's chains of cleanups (#1370), once its controls are written and their labels resolved. Each record
// runs its call once, at its CLEANUP at its scope's end (its entry); an exit (CHECK_CONTROL_EXIT) jumps to its first
// record's entry, and after each record's call its dispatch goes on to (CheckProgramChain):
// - its destinations' tails, of the exits whose last record it is: exits with the same last record and the same tail
//   share one, whose tail the first emitted exit keeps in place (the others leave theirs out), or for a lone JUMP its target;
// - its parent's entry, for the exits that go past it from its scope's outermost record (OUTWARD);
// - the next control (NATURAL): its parent's CLEANUP in its scope's run, which every path through it takes, or after the
//   run's outermost record, what follows the scope, which only the scope's fall takes.
// A dispatch of two continuations or more tests the destination slot (TESTED), which every exit and fall whose path
// crosses one stores first: an exit its destination's value, a fall 0, at its run's head (FALL_STORE), past which the
// exits enter. A fall is reached unless the control before its run transfers elsewhere and no branch, case, asm goto or
// label's address goes to the run. The records are in source order, a preorder of their scopes: a parent comes before
// its children, so forward passes see a record's parent first and a backward pass its children. Linear in the body's
// controls, records and exits, apart from a hash of the tails.
static void check_program_chains(CheckControlWriter* writer, CheckProgramBody* body, uint32_t exit_start, Arena* scratch)
{
    CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckInputCleanup* cleanups = input->cleanups;
    const CheckControl* controls = writer->entries + body->first;
    CheckProgramChain* chains = program->chains;
    CheckProgramExit* exits = program->exits;
    const uint32_t count = body->count;
    const uint32_t record_first = body->cleanup_first;
    const uint32_t record_end = body->cleanup_first + body->cleanup_count;
    const uint32_t exit_count = writer->exit_count - exit_start;
    uint32_t buckets = 16;
    while (buckets < exit_count * 2)
    {
        buckets *= 2;
    }
    const uint64_t mark = scratch->length;
    uint64_t* targeted = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count / 64 + 1);
    uint32_t* places = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* destination_stop = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* destination_of = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    // The destinations: each one's last record, its tail's first control, its length and its label.
    uint32_t* destination_last = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* destination_tail = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* destination_length = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* destination_label = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, exit_count);
    uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, buckets);
    uint32_t* absent = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body->cleanup_count + 1);
    uint32_t* path = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body->cleanup_count + 1);
    uint32_t* path_first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body->cleanup_count + 1);
    uint32_t* path_next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)exit_count + 1);
    if (targeted == 0 || absent == 0 || path == 0 || path_first == 0 || path_next == 0 || places == 0 || destination_stop == 0 || destination_of == 0 || destination_last == 0 || destination_tail == 0 || destination_length == 0 ||
        destination_label == 0 || table == 0)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, body->opening);
    }
    else
    {
        memset(targeted, 0, ((uint64_t)count / 64 + 1) * sizeof(uint64_t));
        memset(table, 0, (uint64_t)buckets * sizeof(uint32_t));
        for (uint32_t k = 0; k < exit_count; k += 1)
        {
            places[k] = BUSTER_CHECK_NONE;
        }
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            const uint32_t parent = writer->cleanup_parents[r];
            const bool outermost = parent == BUSTER_CHECK_NONE || cleanups[parent].scope != cleanups[r].scope;
            chains[r] = (CheckProgramChain){ .entry = BUSTER_CHECK_NONE, .parent = parent, .depth = parent != BUSTER_CHECK_NONE ? chains[parent].depth + 1 : 0,
                                             .reach = UINT32_MAX, .flags = outermost ? BUSTER_CHECK_CHAIN_OUTERMOST : 0 };
        }
        // Each exit's last record, the child of its hold on the path from its first (the root for none): the exits listed
        // by their first record, and a sweep of the records in preorder that keeps the path to each by depth.
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            path_first[r - record_first] = BUSTER_CHECK_NONE;
        }
        for (uint32_t k = 0; k < exit_count; k += 1)
        {
            const uint32_t first = exits[exit_start + k].first;
            const bool held = first >= record_first && first < record_end;
            path_next[k] = held ? path_first[first - record_first] : BUSTER_CHECK_NONE;
            if (held)
            {
                path_first[first - record_first] = k;
            }
            else
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, body->opening);
            }
        }
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            const uint32_t depth = chains[r].depth;
            path[depth] = r;
            for (uint32_t k = path_first[r - record_first]; k != BUSTER_CHECK_NONE; k = path_next[k])
            {
                CheckProgramExit* exit = exits + exit_start + k;
                const uint32_t hold = exit->hold;
                const bool ancestor = hold == BUSTER_CHECK_NONE ||
                                      (hold >= record_first && hold < record_end && chains[hold].depth < depth && path[chains[hold].depth] == hold);
                exit->last = ancestor ? path[hold == BUSTER_CHECK_NONE ? 0 : chains[hold].depth + 1] : BUSTER_CHECK_NONE;
                if (!ancestor)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, body->opening);
                }
            }
        }
        // The controls that branches go to, the records' entries and the exits' places. An asm goto or a label whose
        // address is taken may go to any: every fall is then reached.
        // A statement expression's region's CLEANUPs past the function's controls are entries too, including its block's
        // run after its END: check_lower_region keeps its value before lowering that run, with an empty evaluation stack.
        bool anywhere = false;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckControl* control = controls + i;
            if (control->kind >= CHECK_CONTROL_BRANCH_FALSE && control->target < count)
            {
                check_program_bit_set(targeted, control->target);
            }
            else if (control->kind == CHECK_CONTROL_CLEANUP && control->target >= record_first && control->target < record_end)
            {
                chains[control->target].entry = i;
            }
            else if (control->kind == CHECK_CONTROL_EXIT && control->target >= exit_start && control->target < writer->exit_count)
            {
                places[control->target - exit_start] = i;
            }
            anywhere |= control->kind == CHECK_CONTROL_ASM || control->kind == CHECK_CONTROL_LABEL;
        }
        for (uint32_t c = writer->case_start; c < writer->case_end; c += 1)
        {
            if (program->cases[c].target < count)
            {
                check_program_bit_set(targeted, program->cases[c].target);
            }
        }
        // The falls: each run's head, its innermost record, and whether control reaches it other than by an exit.
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            const uint32_t entry = chains[r].entry;
            const CheckControl* before = entry != BUSTER_CHECK_NONE && entry != 0 ? controls + entry - 1 : NULL;
            const bool head = entry != BUSTER_CHECK_NONE &&
                              (before == NULL || before->kind != CHECK_CONTROL_CLEANUP || before->target >= input->cleanup_count ||
                               cleanups[before->target].scope != cleanups[r].scope);
            const bool transfers = before != NULL && (before->kind == CHECK_CONTROL_RETURN || before->kind == CHECK_CONTROL_JUMP ||
                                                      before->kind == CHECK_CONTROL_JUMP_INDIRECT || before->kind == CHECK_CONTROL_END);
            // A region's END precedes its value, not a transfer. Its ordinary completion reaches the post-value run
            // and must reset the destination slot, even if evaluating that value used another cleanup chain.
            const bool region_end = before != NULL && before->kind == CHECK_CONTROL_END && entry >= body->entry_count;
            const bool falls = head && (anywhere || !transfers || region_end || check_program_bit(targeted, entry));
            if (head)
            {
                chains[r].flags |= BUSTER_CHECK_CHAIN_HEAD;
            }
            for (uint32_t q = r; falls && q != BUSTER_CHECK_NONE; q = (chains[q].flags & BUSTER_CHECK_CHAIN_OUTERMOST) != 0 ? BUSTER_CHECK_NONE : chains[q].parent)
            {
                chains[q].flags |= BUSTER_CHECK_CHAIN_FALLS;
            }
        }
        // Each record's nearest ancestor (itself included) without an entry, its depth plus one (0 for none): a constant
        // switch's kept statements leave their compounds' ends out (check_control_switch_prune), and an exit through such
        // a record keeps its run of calls in place (check_lower_exit), as does one whose transfer no path reaches.
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            const uint32_t parent = chains[r].parent;
            absent[r - record_first] = chains[r].entry == BUSTER_CHECK_NONE ? chains[r].depth + 1 : parent != BUSTER_CHECK_NONE ? absent[parent - record_first] : 0;
        }
        // The exits' destinations: by their last record and their tail, the controls after the EXIT to its transfer.
        uint32_t destination_count = 0;
        for (uint32_t k = 0; k < exit_count; k += 1)
        {
            CheckProgramExit* exit = exits + exit_start + k;
            const uint32_t place = places[k];
            uint32_t end = place != BUSTER_CHECK_NONE ? place + 1 : count;
            while (end < count && (controls[end].kind == CHECK_CONTROL_STACK_RESTORE || controls[end].kind == CHECK_CONTROL_LIFETIME))
            {
                end += 1;
            }
            const bool held = exit->first >= record_first && exit->first < record_end && exit->last >= record_first && exit->last < record_end;
            const uint32_t missing = held ? absent[exit->first - record_first] : 0;
            const bool live = held && end < count && (controls[end].kind == CHECK_CONTROL_RETURN || controls[end].kind == CHECK_CONTROL_JUMP) &&
                              (missing == 0 || missing - 1 < chains[exit->last].depth);
            if (!held)
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, body->opening);
            }
            destination_of[k] = BUSTER_CHECK_NONE;
            exit->tail_count = live ? end - place : BUSTER_CHECK_NONE;
            exit->value = BUSTER_CHECK_NONE;
            if (live)
            {
                const uint32_t tail = place + 1;
                const uint32_t length = end + 1 - tail;
                uint64_t hash = (uint64_t)exit->last * UINT64_C(0x9e3779b97f4a7c15);
                for (uint32_t i = tail; i <= end; i += 1)
                {
                    hash = (hash ^ ((uint64_t)controls[i].kind << 56 ^ (uint64_t)controls[i].node << 24 ^ controls[i].target)) * UINT64_C(0xff51afd7ed558ccd);
                }
                uint32_t bucket = (uint32_t)(hash >> 32) & (buckets - 1);
                uint32_t found = BUSTER_CHECK_NONE;
                while (table[bucket] != 0 && found == BUSTER_CHECK_NONE)
                {
                    const uint32_t d = table[bucket] - 1;
                    const bool same = destination_last[d] == exit->last && destination_length[d] == length &&
                                      check_program_tails_equal(controls, destination_tail[d], tail, length);
                    found = same ? d : BUSTER_CHECK_NONE;
                    bucket = same ? bucket : (bucket + 1) & (buckets - 1);
                }
                const bool lone = length == 1 && controls[end].kind == CHECK_CONTROL_JUMP;
                if (found == BUSTER_CHECK_NONE)
                {
                    found = destination_count++;
                    table[bucket] = found + 1;
                    destination_last[found] = exit->last;
                    destination_tail[found] = tail;
                    destination_length[found] = length;
                    destination_label[found] = lone ? controls[end].target : BUSTER_CHECK_NONE;
                    chains[exit->last].stop_count += 1;
                }
                destination_of[k] = found;
                chains[exit->first].reach = chains[exit->first].reach < chains[exit->last].depth ? chains[exit->first].reach : chains[exit->last].depth;
            }
        }
        // The records' destinations, contiguous by record (CheckProgram.chain_stops), each one's value its rank plus one.
        uint32_t stop = program->chain_stop_count;
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            chains[r].stop_first = stop;
            stop += chains[r].stop_count;
            chains[r].tested = 0;
        }
        for (uint32_t d = 0; d < destination_count; d += 1)
        {
            CheckProgramChain* chain = chains + destination_last[d];
            destination_stop[d] = chain->stop_first + chain->tested;
            program->chain_stops[destination_stop[d]] = (CheckProgramStop){ .value = d + 1, .label = destination_label[d] };
            chain->tested += 1;
        }
        program->chain_stop_count = stop;
        // The exits that go past each record: from its subtree, to a record above it.
        for (uint32_t r = record_end; r-- > record_first;)
        {
            const uint32_t parent = chains[r].parent;
            if (parent != BUSTER_CHECK_NONE && chains[r].reach < chains[parent].reach)
            {
                chains[parent].reach = chains[r].reach;
            }
        }
        // Each record's continuations, and its nearest ancestor (itself included) whose dispatch tests the slot.
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            CheckProgramChain* chain = chains + r;
            const bool falls = (chain->flags & BUSTER_CHECK_CHAIN_FALLS) != 0;
            const bool passes = chain->reach < chain->depth;
            const bool outermost = (chain->flags & BUSTER_CHECK_CHAIN_OUTERMOST) != 0;
            const bool natural = outermost ? falls : falls || passes;
            const bool outward = outermost && passes;
            const uint32_t continuations = chain->stop_count + natural + outward;
            chain->flags |= (natural ? BUSTER_CHECK_CHAIN_NATURAL : 0) | (outward ? BUSTER_CHECK_CHAIN_OUTWARD : 0) |
                            (continuations >= 2 ? BUSTER_CHECK_CHAIN_TESTED : 0);
            chain->tested = continuations >= 2 ? chain->depth + 1 : chain->parent != BUSTER_CHECK_NONE ? chains[chain->parent].tested : 0;
        }
        // The stores: of an exit whose path, its first record up to its last, crosses a tested dispatch, and of a fall
        // whose run has one.
        for (uint32_t k = 0; k < exit_count; k += 1)
        {
            CheckProgramExit* exit = exits + exit_start + k;
            const uint32_t tested = destination_of[k] != BUSTER_CHECK_NONE ? chains[exit->first].tested : 0;
            exit->value = tested != 0 && tested - 1 >= chains[exit->last].depth ? destination_of[k] + 1 : BUSTER_CHECK_NONE;
            exit->destination = destination_of[k] != BUSTER_CHECK_NONE ? destination_stop[destination_of[k]] : BUSTER_CHECK_NONE;
        }
        for (uint32_t r = record_first; r < record_end; r += 1)
        {
            const uint8_t needed = BUSTER_CHECK_CHAIN_HEAD | BUSTER_CHECK_CHAIN_FALLS;
            bool stores = false;
            for (uint32_t q = (chains[r].flags & needed) == needed ? r : BUSTER_CHECK_NONE; q != BUSTER_CHECK_NONE;
                 q = (chains[q].flags & BUSTER_CHECK_CHAIN_OUTERMOST) != 0 ? BUSTER_CHECK_NONE : chains[q].parent)
            {
                stores |= (chains[q].flags & BUSTER_CHECK_CHAIN_TESTED) != 0;
            }
            chains[r].flags |= stores ? BUSTER_CHECK_CHAIN_FALL_STORE : 0;
        }
        body->chains = true;
    }
    arena_pop_to(scratch, mark);
}

static uint32_t check_program_reference_roots(CheckProgram* program, const CheckControl* controls, uint32_t count, uint32_t epoch,
                                              uint32_t* nodes, uint32_t* stack, uint32_t* depth, uint32_t* seen,
                                              uint64_t* references, uint8_t* needed, uint32_t reference_count)
{
    const CheckGraph* graph = &program->input->graph;
    for (uint32_t i = 0; i < count; i += 1)
    {
        // Each entry's node pushed without a branch (#892): an entry without one marks the marks' last slot, which names no
        // node, and a node marked already is stored past the stack's top, which has room for one more. Tested an entry at a
        // time, the push was missed at most of the entries it took on the 9700X.
        const uint32_t node = check_control_expression_node(controls + i);
        const uint32_t mark = node != BUSTER_CHECK_NONE ? node : graph->count;
        const bool fresh = (node != BUSTER_CHECK_NONE) & (nodes[mark] != epoch);
        nodes[mark] = epoch;
        stack[*depth] = node;
        *depth += fresh;
        if (controls[i].kind == CHECK_CONTROL_ASM)
        {
            const CheckInputAsm* assembly = program->input->asm_statements + controls[i].target;
            for (uint32_t operand = 0; operand < assembly->output_count + assembly->input_count; operand += 1)
            {
                const uint32_t expression = assembly->operands[operand].node;
                if (expression < graph->count && nodes[expression] != epoch)
                {
                    nodes[expression] = epoch;
                    stack[(*depth)++] = expression;
                }
            }
        }
        if (controls[i].kind == CHECK_CONTROL_INITIALIZE)
        {
            const CheckInitializer* plan = &program->initializers->plans[controls[i].target];
            reference_count = check_program_reference_initializer(program, plan, epoch, nodes, stack, depth, seen, references, needed, reference_count);
        }
        // A cleanup's call of its function: a CLEANUP's, or each of an EXIT's records' (#1370), which run there where their
        // scope's end has none.
        const bool exits = controls[i].kind == CHECK_CONTROL_EXIT && controls[i].target < program->exit_count;
        uint32_t record = controls[i].kind == CHECK_CONTROL_CLEANUP ? controls[i].target : exits ? program->exits[controls[i].target].first : BUSTER_CHECK_NONE;
        for (uint32_t steps = 0; record < program->input->cleanup_count && steps < program->input->cleanup_count; steps += 1)
        {
            const uint32_t function = program->input->cleanups[record].function;
            const uint32_t object = function < program->input->token_count ? program->input->objects[function] : 0;
            if (object != 0 && object < program->object_count && seen[object] != epoch)
            {
                seen[object] = epoch;
                needed[object] = 1;
                references[reference_count++] = (uint64_t)object << 32;
            }
            const bool more = exits && record != program->exits[controls[i].target].last && program->exits[controls[i].target].tail_count == BUSTER_CHECK_NONE;
            record = more ? program->chains[record].parent : BUSTER_CHECK_NONE;
        }
    }
    return reference_count;
}

// Whether `root`'s code holds a label — a statement expression's whose block does — which code generation emits even
// where a constant condition leaves the operand out (clang's ContainsLabel: a label is a statement's, so an operand
// can hold one only through a statement expression, or a case of a switch outside it). The scan is syntactic: a
// `sizeof` or `_Alignof` operand counts too, as it does for ContainsLabel, though code generation evaluates neither.
// The scan's marks in `nodes` are
// `tag`, disjoint from the run's epochs, so they don't hide a node the run hasn't pushed; `stack`/`depth` start where
// the run's does and return to it.
static bool check_program_labeled(CheckProgram* program, uint32_t root, uint32_t tag, uint32_t* nodes, uint32_t* stack, uint32_t depth)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const Parser* parser = program->parser;
    uint32_t count = 0;
    bool labeled = false;
    if (root < graph->count && nodes[root] != tag)
    {
        nodes[root] = tag;
        stack[depth + count++] = root;
    }
    // The scan runs out: a mark means the node's subtree was explored whole, so later queries skip it soundly.
    while (count != 0)
    {
        const uint32_t node = stack[depth + --count];
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_STATEMENT_VALUE)
        {
            const uint32_t token = graph->tokens[node];
            const uint32_t region = token < input->token_count ? input->expression_regions[token] : 0;
            if (region != 0 && region <= input->region_count)
            {
                const CheckInputRegion* item = input->regions + region - 1;
                for (uint32_t t = item->opening; !labeled && t <= item->closing; t += 1)
                {
                    labeled = parser->tree.roles[t] == PARSE_ROLE_LABEL ||
                              (parser->tree.roles[t] == PARSE_ROLE_CLOSE_CASE &&
                               (check_sparse_get(input->control_owners, t) < item->opening || check_sparse_get(input->control_owners, t) > item->closing));
                }
            }
        }
        else
        {
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
            {
                if (operands[k] < graph->count && nodes[operands[k]] != tag)
                {
                    nodes[operands[k]] = tag;
                    stack[depth + count++] = operands[k];
                }
            }
            if (check_graph_has_arguments(op))
            {
                for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                    if (nodes[argument] != tag)
                    {
                        nodes[argument] = tag;
                        stack[depth + count++] = argument;
                    }
                }
            }
        }
    }
    return labeled;
}

// A builtin's size or value argument whose value the code may take as a constant, as ordinary constant emission's proof
// takes it: a retained folded integer is no runtime value where a floating environment or evaluation effect still
// matters, and then its evaluation runs no code. Its value's low 64 bits in *value.
static bool check_program_constant_operand(const CheckGraph* graph, const CheckStream* stream, uint32_t node, uint64_t* value)
{
    const CheckOp op = graph->ops[node];
    const CheckValue checked = check_stream_value(stream, node);
    const bool constant = op != CHECK_OP_BUILTIN_QUERY && op != CHECK_OP_STATEMENT_VALUE && op != CHECK_OP_INITIALIZE && checked.category == CHECK_RVALUE &&
                          checked.constant_kind == CHECK_CONSTANT_INTEGER && checked.fault == CHECK_FAULT_NONE &&
                          (checked.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    *value = constant ? (uint64_t)checked.integer : 0;
    return constant;
}

// CheckProgram.object_labelled: the objects of the declarations with an asm label not ignored (check_input_attribute_of's),
// by one scan of the tokens
// with attributes (CheckInput.attributes), in a unit with an asm keyword alone; false where the column has no room.
static bool check_program_labelled_objects(CheckProgram* program, Arena* output)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    bool room = true;
    for (uint32_t first = input->asm_capacity != 0 ? check_input_attributed_next(input->attributes, 0, count) : count; room && first < count;
         first = check_input_attributed_next(input->attributes, first + 16, count))
    {
        const uint32_t lanes = check_input_attributed_group(input->attributes, first, count);
        for (uint32_t selected = lanes; room && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            const uint32_t object = input->objects[token];
            const CheckInputAttribute* label = object != 0 && object <= count && input->declarations[token] == token ?
                                               check_input_attribute_of(check_input_attributes_at(input, token), CHECK_ATTRIBUTE_ASM_LABEL) : NULL;
            if (label != NULL && program->object_labelled == NULL)
            {
                program->object_labelled = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, (uint64_t)count + 1);
                room = program->object_labelled != NULL;
                if (room)
                {
                    memset(program->object_labelled, 0, (uint64_t)count + 1);
                }
            }
            if (label != NULL && room)
            {
                program->object_labelled[object] = 1;
            }
        }
    }
    return room;
}

// The plain library builtin a call computes inline as clang -O0's code computes its intrinsic (#967, #1373), its row of
// cbuiltin_library_plain_folds, or BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS: a call of the unit's declaration of one that clang
// makes the builtin (the reader's mark, BUSTER_CHECK_LIBRARY_CALL) whose row computes it (CBuiltinLibraryComputed), with
// its prototype's arguments, memcpy, memmove and memset with a constant size of at most
// BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES; but of one with an asm label on a declaration, which clang's code calls by its
// label (EmitBuiltinExpr's BuiltinIDIfNoAsmLabel is 0 for it): CheckProgram.object_labelled. The references' pass and
// the lowering ask for every call, so it reads no token: a search of the unit's declarations for each computed memcpy
// took sqlite3.c's -c from 126 to 187 ms on the 9700X.
static __attribute__((noinline)) uint32_t check_program_call_marked(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t member = graph->members[node];
    const uint32_t index = member & ~BUSTER_CHECK_LIBRARY_CALL;
    const bool library = (member & BUSTER_CHECK_LIBRARY_CALL) != 0 && index < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    const CBuiltinLibraryComputed computed = library ? cbuiltin_library_plain_folds[index].computed : CBUILTIN_LIBRARY_COMPUTED_NONE;
    const bool memory = computed == CBUILTIN_LIBRARY_COMPUTED_COPY || computed == CBUILTIN_LIBRARY_COMPUTED_MOVE || computed == CBUILTIN_LIBRARY_COMPUTED_SET;
    const bool unary = computed == CBUILTIN_LIBRARY_COMPUTED_ABSOLUTE || computed == CBUILTIN_LIBRARY_COMPUTED_FLOAT_ABSOLUTE;
    const uint32_t arity = memory ? 3 : unary ? 1 : 2;
    bool marked = computed != CBUILTIN_LIBRARY_COMPUTED_NONE && graph->argument_counts[node] == arity;
    uint64_t size = 0;
    if (marked && memory)
    {
        const uint32_t size_node = graph->argument_nodes[graph->argument_offsets[node] + 2];
        marked = check_program_constant_operand(graph, program->stream, size_node, &size) && size <= BUSTER_CBUILTIN_LIBRARY_INLINE_BYTES;
    }
    const uint32_t callee = marked && program->object_labelled != NULL ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const uint32_t object = callee < graph->count ? check_stream_value(program->stream, callee).object : 0;
    const bool labelled = object != 0 && object < program->object_count && program->object_labelled[object] != 0;
    return marked && !labelled ? index : BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
}

// check_program_call_marked's answer for a call the reader marked, BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS for any other: the
// references' pass and the lowering ask of every call, and most are unmarked.
static inline __attribute__((always_inline)) uint32_t check_program_call_computed(const CheckProgram* program, uint32_t node)
{
    const bool marked = (program->input->graph.members[node] & BUSTER_CHECK_LIBRARY_CALL) != 0;
    return marked ? check_program_call_marked(program, node) : BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
}

// Whether a call has a plain library builtin's semantics of its own, which the node machine and its census leave to the
// pipeline: the reader marked it (BUSTER_CHECK_LIBRARY_CALL), and its row folds (where its arguments are constants) or
// the code computes it (check_program_call_computed). Any other marked call, a memcpy of a size that is no small constant,
// is an ordinary call of its declaration. The checker unit's alone call it (c_check_node_machine_type.c,
// c_check_node_census.c).
static __attribute__((unused)) bool check_program_call_library(const CheckProgram* program, uint32_t node)
{
    const uint32_t member = program->input->graph.members[node];
    const uint32_t index = member & ~BUSTER_CHECK_LIBRARY_CALL;
    const bool marked = (member & BUSTER_CHECK_LIBRARY_CALL) != 0 && index < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    return marked && (cbuiltin_library_plain_folds[index].fold != CBUILTIN_LIBRARY_FOLD_NONE ||
                      check_program_call_computed(program, node) != BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS);
}

static uint32_t check_program_references(CheckProgram* program, const CheckControl* controls, uint32_t count, uint32_t epoch,
                                         uint32_t* seen, uint32_t* nodes, uint32_t* stack, uint64_t* references, uint8_t* needed)
{
    const CheckGraph* graph = &program->input->graph;
    const bool regions = program->input->region_count != 0;
    uint32_t depth = 0;
    uint32_t reference_count = 0;
    reference_count = check_program_reference_roots(program, controls, count, epoch, nodes, stack, &depth, seen, references, needed, reference_count);
    while (depth != 0)
    {
        const uint32_t node = stack[--depth];
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE)
        {
            const CheckValue value = check_graph_leaf_value(graph, node);
            if (value.overload_head == 0 && value.object != 0 && value.object < program->object_count &&
                ((value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0 || check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_STATIC)) && seen[value.object] != epoch)
            {
                seen[value.object] = epoch;
                needed[value.object] = 1;
                references[reference_count++] = (uint64_t)value.object << 32;
            }
            if (value.object != 0 && value.object < program->object_count && check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
                !check_input_bit(program->input->declaration_bits, value.object - 1, BUSTER_CHECK_BITS_STATIC))
            {
                const uint32_t selected = program->initializers != 0 ? check_initializers_plan_index(program->initializers, value.object - 1) : 0;
                if (selected == 0 || selected > program->initializers->count)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, value.object - 1);
                }
                else
                {
                    const CheckInitializer* plan = program->initializers->plans + selected - 1;
                    if (program->compound_initializers != NULL)
                    {
                        program->compound_initializers[selected - 1] = 1;
                    }
                    reference_count = check_program_reference_initializer(program, plan, epoch, nodes, stack, &depth, seen, references, needed, reference_count);
                }
            }
        }
        else if (op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF)
        {
            if (op == CHECK_OP_BUILTIN_MISC && (graph->members[node] == CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING || graph->members[node] == CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING))
            {
                // A constant CFString's call (#148), whose value is its object's address: a reference to its canonical
                // record (check_data_cfstring_keys), whose symbol the call's object takes.
                const CheckValue value = check_stream_value(program->stream, node);
                const uint32_t object = value.constant_kind == CHECK_CONSTANT_ADDRESS && (value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0 ?
                                        check_data_cfstring_canonical(program->cfstrings, program->cfstring_count, value.object) : 0;
                if (object != 0 && object < program->object_count && seen[object] != epoch)
                {
                    seen[object] = epoch;
                    needed[object] = 1;
                    references[reference_count++] = (uint64_t)object << 32;
                }
            }
            if (op == CHECK_OP_STATEMENT_VALUE)
            {
                const uint32_t selected = program->input->expression_regions[graph->tokens[node]];
                if (selected == 0 || selected > program->region_count)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, graph->tokens[node]);
                }
                else
                {
                    const CheckProgramRegion* region = program->regions + selected - 1;
                    program->region_reached[selected - 1] = 1;
                    reference_count = check_program_reference_roots(program, controls + region->first, region->count, epoch,
                                                                    nodes, stack, &depth, seen, references, needed, reference_count);
                }
            }
            uint32_t operands[] = {
                graph->operand0[node],
                graph->operand1[node],
                graph->operand2[node],
            };
            if (op == CHECK_OP_CALL)
            {
                const CheckStream* stream = program->stream;
                const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
                if (overload != BUSTER_CHECK_NONE)
                {
                    const CheckOverloadResult* selected = &stream->overloads[overload].selected;
                    const uint32_t object = selected->object;
                    if (!check_program_overload_valid(program, selected))
                    {
                        check_program_fail(program, CHECK_PROGRAM_INVALID, graph->tokens[node]);
                    }
                    else if (seen[object] != epoch)
                    {
                        seen[object] = epoch;
                        needed[object] = 1;
                        references[reference_count++] = (uint64_t)object << 32;
                    }
                    // This edge names the source overload set; the checked selection above is the callee.
                    operands[0] = BUSTER_CHECK_NONE;
                }
                // A plain fmax, memcpy or their kin that the code computes inline (#967, #1373, check_program_call_computed)
                // references no function, as clang's code doesn't.
                if (check_program_call_computed(program, node) != BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS)
                {
                    operands[0] = BUSTER_CHECK_NONE;
                }
            }
            if (op == CHECK_OP_VARARGS)
            {
                const uint32_t effect = graph->argument_offsets[node];
                if (effect >= program->stream->varargs_count || program->stream->varargs[effect].node != node)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, graph->tokens[node]);
                }
                else
                {
                    const uint32_t evaluated = program->stream->varargs[effect].evaluated_operands;
                    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
                    {
                        operands[k] = (evaluated & (1u << k)) != 0 ? operands[k] : BUSTER_CHECK_NONE;
                    }
                }
            }
            bool truth = false;
            if ((op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL) && !program->unselected_operands &&
                check_program_runtime_truth(program, node, operands[0], &truth))
            {
                // The operand a constant condition leaves out still emits — and so refers — where it holds a label
                // (check_program_labeled): a statement expression's block's, or a case of a switch outside it — which
                // only a unit with a statement expression can have. A label inside the condition itself keeps clang's
                // ConstantFoldsToSimpleInteger from folding it, so then the operand emits whatever it holds.
                const uint32_t dropped = op == CHECK_OP_CONDITIONAL ? (truth ? 2 : 1) : 1;
                const bool leaves = op == CHECK_OP_CONDITIONAL || (op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth);
                if (leaves && (!regions || !(check_program_labeled(program, operands[0], epoch | UINT32_C(0x20000000), nodes, stack, depth) ||
                                             check_program_labeled(program, operands[dropped], epoch | UINT32_C(0x40000000), nodes, stack, depth))))
                {
                    operands[dropped] = BUSTER_CHECK_NONE;
                }
            }
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
            {
                const uint32_t operand = operands[k];
                // A C unit has no overloads, so no operand's address takes one (#1175).
                const CheckOverloadResult* selected = __builtin_expect(program->stream->overload_address_count != 0, 0) && operand != BUSTER_CHECK_NONE ?
                                                      check_program_overload_address(program, node, k) : NULL;
                if (selected != NULL)
                {
                    if (!check_program_overload_valid(program, selected))
                    {
                        check_program_fail(program, CHECK_PROGRAM_INVALID, graph->tokens[node]);
                    }
                    else if (seen[selected->object] != epoch)
                    {
                        seen[selected->object] = epoch;
                        needed[selected->object] = 1;
                        references[reference_count++] = (uint64_t)selected->object << 32;
                    }
                }
                else if (operand != BUSTER_CHECK_NONE && nodes[operand] != epoch)
                {
                    nodes[operand] = epoch;
                    stack[depth++] = operand;
                }
            }
            if (check_graph_has_arguments(op))
            {
                const uint32_t mask = check_program_argument_mask(program, node);
                for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                    if (op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || (a < 32 ? (mask & (1u << a)) != 0 : mask == UINT32_MAX))
                    {
                        const CheckOverloadResult* selected = check_program_overload_address(program, node, a + 3);
                        if (selected != NULL)
                        {
                            if (!check_program_overload_valid(program, selected))
                            {
                                check_program_fail(program, CHECK_PROGRAM_INVALID, graph->tokens[node]);
                            }
                            else if (seen[selected->object] != epoch)
                            {
                                seen[selected->object] = epoch;
                                needed[selected->object] = 1;
                                references[reference_count++] = (uint64_t)selected->object << 32;
                            }
                        }
                        else if (nodes[argument] != epoch)
                        {
                            nodes[argument] = epoch;
                            stack[depth++] = argument;
                        }
                    }
                }
            }
        }
    }
    return reference_count;
}

// Only units with resolved &&label expressions need this table. Scan their kind/role columns in bounded blocks;
// scalar also handles the tail without requiring padding, and is the independent program path's exact twin.
static void check_program_addressed(const Parser* parser, const CheckInput* input, uint8_t* addressed, bool scalar)
{
    (void)scalar;

    const uint32_t count = input->token_count;
    memset(addressed, 0, (uint64_t)count + 1);
    uint32_t token = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && count - token >= 64; token += 64)
    {
        const __m512i kinds = _mm512_loadu_si512(parser->tokens.kinds + token);
        const __m512i roles = _mm512_loadu_si512(parser->tree.roles + token);
        uint64_t selected = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_AMPAMP)) &
                            _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_PREFIX));
        while (selected != 0)
        {
            const uint32_t target = check_sparse_get(input->label_targets, token + (uint32_t)__builtin_ctzll(selected));
            if (target < count)
            {
                addressed[target] = 1;
            }
            selected &= selected - 1;
        }
    }
    for (; token < count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_AMPAMP && parser->tree.roles[token] == PARSE_ROLE_PREFIX && check_sparse_get(input->label_targets, token) < count)
        {
            addressed[check_sparse_get(input->label_targets, token)] = 1;
        }
    }
}

#define BUSTER_CHECK_PROGRAM_LANES 16u

// The opening tokens of the bodies this program reads, in source order. The sparse column contains one entry per
// definition. A deferred body belongs to its separate reading.
static uint32_t check_program_openings(const CheckInput* input, uint32_t* openings, bool scalar)
{
    (void)scalar;
    uint32_t count = 0;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t token = 0; token < input->token_count; token += 1)
        {
            if (check_sparse_get(input->body_declarations, token) != BUSTER_CHECK_NONE &&
                (input->deferred_tokens == NULL || input->deferred_tokens[token] == 0 || check_boundary_taken(token) != NULL))
            {
                openings[count++] = token;
            }
        }
    }
    else
    {
        count = check_sparse_entries(input->body_declarations, 0, input->token_count, openings);
        if (input->deferred_tokens != NULL)
        {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t token = openings[i];
                openings[kept] = token;
                // SCAFFOLD (c_check_boundary.c): a supplied body is masked and still the program's.
                kept += input->deferred_tokens[token] == 0 || check_boundary_taken(token) != NULL;
            }
            count = kept;
        }
    }
    return count;
}

// The walks from the variable-length arrays' declarations up to their statements (check_program_statement), in
// check_program_begin's scratch: each token's answer in `ends`, under the key in `keys`, where `known` has its bit (#2995).
typedef struct CheckProgramStatements CheckProgramStatements;
struct CheckProgramStatements
{
    uint32_t* ends;
    uint32_t* keys;
    uint64_t* known;
    // CheckProgramOptions.statement_walk_work, or null.
    uint64_t* work;
};

// Where the walk from `token` up its parents stops (#2995): at a statement's terminator, at `body`, or at a token past the
// unit's `count`; BUSTER_CHECK_NONE where it would take `count` steps. Each token it passes keeps that answer under its
// key, `body` (the unit's count for a walk that no body stops, whose `body` is past the tokens), and a later walk with the
// key stops at the first kept token: a declarator's bounds ([n][n]...[n]) and a declaration's declarators (a0[n], a1[n],
// ...) walk the path they share once, where each walked it whole. Scalar, as check_control_region_targets is.
static uint32_t check_program_statement(const Parser* parser, uint32_t count, uint32_t token, uint32_t body, const CheckProgramStatements* statements)
{
    const uint32_t key = body < count ? body : count;
    uint32_t end = token;
    uint32_t steps = 0;
    bool kept = false;
    while (end < count && end != body && parser->tree.roles[end] != PARSE_ROLE_TERMINATOR && !kept && steps < count)
    {
        kept = check_program_bit(statements->known, end) && statements->keys[end] == key;
        end = kept ? end : parser->tree.parents[end];
        steps += kept ? 0 : 1;
    }
    const uint32_t found = steps < count ? (kept ? statements->ends[end] : end) : BUSTER_CHECK_NONE;
    uint32_t at = token;
    for (uint32_t walked = 0; found != BUSTER_CHECK_NONE && walked < steps; walked += 1)
    {
        const uint32_t next = parser->tree.parents[at];
        statements->ends[at] = found;
        statements->keys[at] = key;
        check_program_bit_set(statements->known, at);
        at = next;
    }
    if (statements->work != NULL)
    {
        *statements->work += steps;
    }
    return found;
}

// A variable-length array's object, declared at `token`: into its statement's allocation list (by the statement's
// terminator), as its block's first array.
static void check_program_allocation(CheckProgram* program, const Parser* parser, uint32_t token, uint32_t* allocation_first, uint32_t* allocation_next,
                                     uint32_t* vla_blocks, uint32_t* vla_saves, const CheckProgramStatements* statements)
{
    const uint32_t count = program->input->token_count;
    const uint32_t terminator = check_program_statement(parser, count, token, count, statements);
    if (terminator < count && parser->tree.parents[terminator] < count)
    {
        allocation_next[token] = allocation_first[terminator];
        allocation_first[terminator] = token;
        vla_blocks[parser->tree.parents[terminator]] = token + 1;
        vla_saves[parser->tree.parents[terminator]] = token + 1;
    }
    else
    {
        check_program_fail(program, CHECK_PROGRAM_INVALID, token);
    }
}

// Whether `token` declares a body's object (not a static one) whose type has a runtime size: a variable-length array's.
static bool check_program_runtime_object(const CheckInput* input, const TypeTable* types, uint32_t token)
{
    return input->declarations[token] == token && check_sparse_get(input->declaration_bodies, token) < input->token_count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
           !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC) && check_action_runtime_size(types, input->declared_types[token]);
}

// Each statement's initializer plans, linked from the last back so that a statement's list is in declaration order: a
// statement's first entry is written, and its bit set in `planned`, at its first plan, and no other entry of plan_first is
// (#877). A compound literal's plan joins no statement's list; true when one of a runtime object does.
static bool check_program_plans(CheckProgram* program, const Parser* parser, const CheckInitializers* initializers, uint32_t* plan_first, uint64_t* planned,
                                uint32_t* plan_next, uint32_t* parents)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    bool compound_initializers = false;
    uint64_t work = 0;
    bool cached = false;
    for (uint32_t i = initializers != 0 ? initializers->count : 0; i-- > 0;)
    {
        const uint32_t declaration = initializers->declaration_tokens[i];
        const bool compound = declaration < count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
        uint32_t token = declaration < count && check_sparse_get(input->initializer_begins, declaration) != 0 ? check_sparse_get(input->initializer_begins, declaration) - 1 : BUSTER_CHECK_NONE;
        if (!compound)
        {
            if (!cached && count != 0 && work >= count)
            {
                // Only copy after the uncached walks paid a token-wide pass. The caller lends not-yet-live `seen`
                // scratch, cleared before its later uses: no extra retained column or copy for cheap queries (#1378).
                memcpy(parents, parser->tree.parents, (uint64_t)count * sizeof(uint32_t));
                cached = true;
            }
            token = check_input_cached_ancestor(parser, token, cached ? parents : NULL, false, &work);
        }
        if (compound)
        {
            // A compound literal's lifetime belongs to its enclosing scope, but its initialization belongs
            // to evaluating its VALUE node. sizeof and unchosen branches must never initialize it.
            plan_next[i] = BUSTER_CHECK_NONE;
            compound_initializers |= !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC);
        }
        else if (token < count)
        {
            if (!check_program_bit(planned, token))
            {
                check_program_bit_set(planned, token);
                plan_first[token] = BUSTER_CHECK_NONE;
            }
            plan_next[i] = plan_first[token];
            plan_first[token] = i;
        }
        else
        {
            check_program_fail(program, CHECK_PROGRAM_INVALID, declaration);
        }
    }
    return compound_initializers;
}

// The variable-length arrays' objects, by their statements, and each block's first, from the last token back, so that the
// lists are in declaration order and a block's last write is its first array (#530): AVX-512 finds the bodies' objects'
// declarations 16 tokens at a time, whose types alone it asks; the rest and the `scalar` path ask every token.
static void check_program_allocations(CheckProgram* program, const Parser* parser, const TypeTable* types, uint32_t* allocation_first, uint32_t* allocation_next,
                                      uint32_t* vla_blocks, uint32_t* vla_saves, const CheckProgramStatements* statements, bool scalar)
{
    (void)scalar;

    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t token = count;
    for (; !BUSTER_SCALAR_PATH(scalar) && token >= BUSTER_CHECK_PROGRAM_LANES; token -= BUSTER_CHECK_PROGRAM_LANES)
    {
        const uint32_t base = token - BUSTER_CHECK_PROGRAM_LANES;
        const __m512i tokens = _mm512_add_epi32(_mm512_set1_epi32((int)base), lanes);
        const __mmask16 objects = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(input->declarations + base), tokens) &
                                  _mm512_cmplt_epu32_mask(check_sparse_vector(input->declaration_bodies, base), _mm512_set1_epi32((int)count)) &
                                  _mm_cmpeq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->declaration_kinds + base)), _mm_set1_epi8((char)C_BINDING_OBJECT)) &
                                  _mm_testn_epi8_mask(_mm_loadu_si128((const __m128i*)(input->declaration_bits + base)), _mm_set1_epi8((char)BUSTER_CHECK_BITS_STATIC));
        for (uint32_t bits = objects; bits != 0;)
        {
            const uint32_t lane = 31 - (uint32_t)__builtin_clz(bits);
            bits &= ~(1u << lane);
            if (check_action_runtime_size(types, input->declared_types[base + lane]))
            {
                check_program_allocation(program, parser, base + lane, allocation_first, allocation_next, vla_blocks, vla_saves, statements);
            }
        }
    }
    while (token != 0)
    {
        token -= 1;
        if (check_program_runtime_object(input, types, token))
        {
            check_program_allocation(program, parser, token, allocation_first, allocation_next, vla_blocks, vla_saves, statements);
        }
    }
}

// Whether the unit may have a variable-length array's bounds or object, of which check_program keeps four token-sized
// columns and finds the objects by a pass over every token (check_program_allocations): where it has a bound
// (CheckInput.vlas), or its type table has a variable-length array's type, without which no type has a runtime size
// (check_action_runtime_size). Most units have neither, and their program none of the columns (#877).
static bool check_program_variable(const CheckInput* input, const TypeTable* types)
{
    return input->vla_count != 0 || memchr(types->types.kind, BUSTER_TYPE_KIND_VARIABLE_ARRAY, types->types.count) != NULL;
}

// The operators of the builtins the program lists (CheckProgram.builtin_nodes): every one from CHECK_OP_BUILTIN_UNARY on,
// which check_program_builtins' vector path takes for its candidates.
static bool check_program_builtin_op(CheckOp op)
{
    return op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT ||
           op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC || op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST;
}

_Static_assert(CHECK_OP_BUILTIN_OVERFLOW > CHECK_OP_BUILTIN_UNARY && CHECK_OP_BUILTIN_MISC > CHECK_OP_BUILTIN_UNARY && CHECK_OP_BUILTIN_FLOAT > CHECK_OP_BUILTIN_UNARY &&
               CHECK_OP_BUILTIN_QUERY > CHECK_OP_BUILTIN_UNARY && CHECK_OP_BUILTIN_ATOMIC > CHECK_OP_BUILTIN_UNARY && CHECK_OP_BUILTIN_VECTOR > CHECK_OP_BUILTIN_UNARY &&
               CHECK_OP_BUILTIN_X86 > CHECK_OP_BUILTIN_UNARY && CHECK_OP_BUILTIN_BIT_CAST > CHECK_OP_BUILTIN_UNARY, "the builtins' operators follow CHECK_OP_BUILTIN_UNARY");

// The builtin nodes some body reached (`nodes`, nonzero), in node order, into `listed` when it isn't null; returns their
// count. AVX-512 keeps the reached nodes from CHECK_OP_BUILTIN_UNARY on, 16 at a time, a few percent of a unit's, for
// check_program_builtin_op; the rest and the `scalar` path test every node.
static uint32_t check_program_builtins(const CheckGraph* graph, const uint32_t* nodes, uint32_t* listed, bool scalar)
{
    (void)scalar;

    uint32_t found = 0;
    uint32_t node = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && graph->count - node >= BUSTER_CHECK_PROGRAM_LANES; node += BUSTER_CHECK_PROGRAM_LANES)
    {
        const __m512i ops = _mm512_loadu_si512(graph->ops + node);
        const __mmask16 candidates = _mm512_cmpge_epu32_mask(ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_UNARY)) & _mm512_test_epi32_mask(_mm512_loadu_si512(nodes + node), _mm512_set1_epi32(-1));
        for (uint32_t lanes = candidates; lanes != 0; lanes &= lanes - 1)
        {
            const uint32_t at = node + (uint32_t)__builtin_ctz(lanes);
            if (check_program_builtin_op(graph->ops[at]))
            {
                if (listed != NULL)
                {
                    listed[found] = at;
                }
                found += 1;
            }
        }
    }
    for (; node < graph->count; node += 1)
    {
        if (nodes[node] != 0 && check_program_builtin_op(graph->ops[node]))
        {
            if (listed != NULL)
            {
                listed[found] = node;
            }
            found += 1;
        }
    }
    return found;
}

// A static object's declaration at `token`: its object's declaration, whether the object is needed, the first of its
// declarations with GNU's naked attribute (CheckProgram.object_naked, made in `output` with the unit's first one), a
// function's declaration with a target or target_clones attribute, or an always_inline, flatten or noreturn one, and a
// file-scope declaration with an alias, ifunc or weakref attribute (`events`), all found by one pass over its
// attributes. Returns whether the column had room, `room` again unless making it failed.
static bool check_program_static_object(CheckProgram* program, uint8_t* needed, uint32_t token, Arena* output, bool room, CheckProgramTargetEvents* events)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    const uint32_t object = input->objects[token];
    if (object != 0 && object <= count)
    {
        program->object_declarations[object] = token;
        needed[object] |= check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE);
        const uint32_t attributes = check_input_attributes_at(input, token) != 0 ? check_program_attribute_bits(check_input_attributes_at(input, token)) : 0;
        const uint32_t function_attributes = attributes != 0 && input->declaration_kinds[token] == C_BINDING_FUNCTION ? attributes : 0;
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET) != 0)
        {
            check_program_target_event(events, object, token);
        }
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_CALLEE) != 0)
        {
            check_program_event_push(events, &events->callee_keys, &events->callee_count, &events->callee_capacity, object, token);
        }
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_PATCHABLE) != 0)
        {
            check_program_event_push(events, &events->patchable_keys, &events->patchable_count, &events->patchable_capacity, object, token);
        }
        // An alias's, ifunc's or weakref's: clang's code generation takes them at file scope alone (EmitGlobal).
        if ((attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_WEAK) != 0)
        {
            check_program_event_push(events, &events->weak_keys, &events->weak_count, &events->weak_capacity, object, token);
        }
        if ((attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_ASM_LABEL) != 0 && check_input_linkage(input->declaration_bits, token) != 0 &&
            !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER))
        {
            check_program_event_push(events, &events->label_keys, &events->label_count, &events->label_capacity, object, token);
        }
        if ((attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALIAS) != 0 && check_sparse_get(input->declaration_bodies, token) == BUSTER_CHECK_NONE)
        {
            check_program_event_push(events, &events->alias_keys, &events->alias_count, &events->alias_capacity, object, token);
        }
        const uint32_t inlining = function_attributes & (BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALWAYS_INLINE | BUSTER_CHECK_PROGRAM_ATTRIBUTE_NOINLINE);
        if (inlining != 0)
        {
            // The column comes with the unit's first such declaration, as object_naked's.
            if (program->object_inline == 0 && room)
            {
                program->object_inline = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, (uint64_t)count + 1);
                room = program->object_inline != 0;
                if (room)
                {
                    memset(program->object_inline, 0, (uint64_t)count + 1);
                }
            }
            if (room)
            {
                program->object_inline[object] |= (uint8_t)(((inlining & BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALWAYS_INLINE) != 0 ? BUSTER_CHECK_PROGRAM_INLINE_ALWAYS : 0) |
                                                            ((inlining & BUSTER_CHECK_PROGRAM_ATTRIBUTE_NOINLINE) != 0 ? BUSTER_CHECK_PROGRAM_INLINE_NEVER : 0));
            }
        }
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_RETURNS_TWICE) != 0)
        {
            // Keep the fact by object: a later redeclaration need not repeat the earlier one's attribute records.
            if (program->object_returns_twice == NULL && room)
            {
                program->object_returns_twice = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, (uint64_t)count + 1);
                room = program->object_returns_twice != NULL;
                if (room)
                {
                    memset(program->object_returns_twice, 0, (uint64_t)count + 1);
                }
            }
            if (room)
            {
                program->object_returns_twice[object] = 1;
            }
        }
        if ((attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_NAKED) != 0)
        {
            if (program->object_naked == 0 && room)
            {
                program->object_naked = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1);
                room = program->object_naked != 0;
                if (room)
                {
                    memset(program->object_naked, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
                }
            }
            if (room && program->object_naked[object] == BUSTER_CHECK_NONE)
            {
                program->object_naked[object] = token;
            }
        }
        // x86's interrupt service routines (#147), a function's declarations', as object_naked's.
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_INTERRUPT) != 0)
        {
            if (program->object_interrupt == 0 && room)
            {
                program->object_interrupt = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1);
                room = program->object_interrupt != 0;
                if (room)
                {
                    memset(program->object_interrupt, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
                }
            }
            if (room && program->object_interrupt[object] == BUSTER_CHECK_NONE)
            {
                program->object_interrupt[object] = token;
            }
        }
        // x86's force_align_arg_pointer (#1447), a function's declarations', as object_interrupt's.
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_REALIGN) != 0)
        {
            if (program->object_realign == 0 && room)
            {
                program->object_realign = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1);
                room = program->object_realign != 0;
                if (room)
                {
                    memset(program->object_realign, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
                }
            }
            if (room && program->object_realign[object] == BUSTER_CHECK_NONE)
            {
                program->object_realign[object] = token;
            }
        }
    }
    return room;
}

// Whether body `b` is an interrupt service routine's (#147): some declaration of its function up to its definition has
// x86's interrupt attribute (CheckProgram.object_interrupt), as clang takes it; clang ignores one after the definition,
// with a warning.
static bool check_program_interrupt(const CheckProgram* program, uint32_t b)
{
    const CheckProgramBody body = program->bodies[b];
    return program->object_interrupt != 0 && body.object < program->object_count && program->object_interrupt[body.object] <= body.declaration;
}

// Whether body `b`'s prologue realigns the stack (#1447): some declaration of its function up to its definition has x86's
// force_align_arg_pointer attribute (CheckProgram.object_realign), as clang takes it; clang ignores one after the
// definition, with a warning.
static bool check_program_realign(const CheckProgram* program, uint32_t b)
{
    const CheckProgramBody body = program->bodies[b];
    return program->object_realign != 0 && body.object < program->object_count && program->object_realign[body.object] <= body.declaration;
}

// The static objects' declarations, needs, naked functions and target attributes, by object, in token order (#530):
// AVX-512 finds the static declarations 16 tokens at a time; the rest and the `scalar` path test every token. Returns
// whether the naked functions' column had room.
static bool check_program_static_objects(CheckProgram* program, uint8_t* needed, Arena* output, bool scalar, CheckProgramTargetEvents* events)
{
    (void)scalar;

    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    bool room = true;
    uint32_t token = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && count - token >= BUSTER_CHECK_PROGRAM_LANES; token += BUSTER_CHECK_PROGRAM_LANES)
    {
        const __m512i tokens = _mm512_add_epi32(_mm512_set1_epi32((int)token), lanes);
        const __mmask16 declared = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(input->declarations + token), tokens) &
                                   _mm_test_epi8_mask(_mm_loadu_si128((const __m128i*)(input->declaration_bits + token)), _mm_set1_epi8((char)BUSTER_CHECK_BITS_STATIC));
        for (uint32_t bits = declared; bits != 0; bits &= bits - 1)
        {
            room = check_program_static_object(program, needed, token + (uint32_t)__builtin_ctz(bits), output, room, events);
        }
    }
    for (; token < count; token += 1)
    {
        if (input->declarations[token] == token && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
        {
            room = check_program_static_object(program, needed, token, output, room, events);
        }
    }
    return room;
}

// Preparation already grouped each object's declarations in their original token order. Different objects have no
// shared state here: the last declaration, first naked spelling, needed bit and target attributes are each an object's
// own reduction.
static bool check_program_static_prepared(CheckProgram* program, const CheckDataPrepared* prepared, uint8_t* needed, Arena* output,
                                          CheckProgramTargetEvents* events)
{
    bool room = true;
    for (uint32_t d = 0; d < prepared->declaration_count; d += 1)
    {
        room = check_program_static_object(program, needed, (uint32_t)prepared->declaration_keys[d], output, room, events);
    }
    return room;
}

static bool check_program_text_equal(String8 a, String8 b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.pointer, b.pointer, a.length) == 0);
}

// Each body's instruction set by its function's target attributes (#180, CheckProgram.body_targets), as clang takes
// them: the first that applies, on the definition or a declaration before it, of a string predefines_target_attribute
// finds a version or the default (clang drops one after the definition where none came before, with a warning); and a
// function of several versions where another one's string differs, or it has target_clones. Each string resolves once
// (predefines_function_target), against the unit's -march=native host features, read once, and an attribute of the same
// string as the one before is taken as it was. The keys sort by object with the radix kernel's twins (c_bindings_sort),
// and a body in their objects' range finds its object's by a binary search: the rest is scalar, as a unit holds few of
// them (main.c's 5,344 among its 23.5 MB of preprocessed text, the clang headers' intrinsics), and none in the usual
// unit, which this leaves at once. Returns false if a column had no room.
static bool check_program_targets(CheckProgram* program, const CheckProgramOptions* options, CheckProgramTargetEvents* events, Arena* output, Arena* scratch)
{
    bool valid = events->room;
    const CheckInput* input = program->input;
    uint64_t* keys = events->keys;
    const uint32_t count = valid ? events->count : 0;
    if (count != 0)
    {
        uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
        valid = temporary != 0;
        uint32_t varying = 0;
        for (uint32_t e = 1; valid && e < count; e += 1)
        {
            varying |= (uint32_t)((keys[0] ^ keys[e]) >> 32);
        }
        keys = valid ? c_bindings_sort(keys, temporary, count, !BUSTER_SCALAR_PATH(options->scalar), varying) : keys;
    }
    // The strings of the targets made so far, in their order.
    String8* texts = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, count) : 0;
    valid = valid && (count == 0 || texts != 0);
    uint32_t last_target = UINT32_MAX;
    // The last string read and how predefines_target_attribute takes it: a header's functions share theirs, so most
    // attributes are the one before.
    String8 last_text = { 0 };
    PredefinesTargetAttribute last_kind = PREDEFINES_TARGET_ATTRIBUTE_IGNORED;
    int32_t last_cpu = -1;
    bool last_read = false;
    // The keys' objects' range: a body outside it has none, as most of a unit's bodies (main.c's own, after its headers').
    const uint32_t first_object = count != 0 ? (uint32_t)(keys[0] >> 32) : 0;
    const uint32_t last_object = count != 0 ? (uint32_t)(keys[count - 1] >> 32) : 0;
    for (uint32_t b = 0; valid && count != 0 && b < program->body_count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        // The object's first key: the first whose object isn't below the body's.
        uint32_t low = body.object < first_object || body.object > last_object ? count : 0;
        uint32_t high = count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            low = (uint32_t)(keys[middle] >> 32) < body.object ? middle + 1 : low;
            high = (uint32_t)(keys[middle] >> 32) < body.object ? high : middle;
        }
        const CheckInputAttribute* applied = 0;
        PredefinesTargetAttribute kind = PREDEFINES_TARGET_ATTRIBUTE_IGNORED;
        int32_t cpu = -1;
        bool versions = false;
        // A cpu_specific version's (#1447): the features of its CPUs, as a target attribute's text.
        const CheckInputAttribute* specific = 0;
        for (uint32_t e = low; e < count && (uint32_t)(keys[e] >> 32) == body.object; e += 1)
        {
            const uint32_t token = (uint32_t)keys[e];
            const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = &attributes->records[i];
                const bool readable = !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_TARGET && attribute->text.length < UINT32_MAX;
                if (readable && !(last_read && check_program_text_equal(attribute->text, last_text)))
                {
                    last_kind = predefines_target_attribute(attribute->text.pointer, (uint32_t)attribute->text.length, &last_cpu);
                    last_text = attribute->text;
                    last_read = true;
                }
                const PredefinesTargetAttribute attribute_kind = readable ? last_kind : PREDEFINES_TARGET_ATTRIBUTE_IGNORED;
                const int32_t attribute_cpu = readable ? last_cpu : -1;
                // A target_clones body's instruction sets are its versions' (#1838, check_program_target_copies, below).
                versions |= attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH && !attribute->ignored;
                specific = specific == 0 && attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC && !attribute->ignored ? attribute : specific;
                if (attribute_kind != PREDEFINES_TARGET_ATTRIBUTE_IGNORED && applied == 0 && token <= body.declaration)
                {
                    applied = attribute;
                    kind = attribute_kind;
                    cpu = attribute_cpu;
                }
                else if (attribute_kind != PREDEFINES_TARGET_ATTRIBUTE_IGNORED && applied != 0)
                {
                    versions |= !check_program_text_equal(attribute->text, applied->text);
                }
            }
        }
        uint32_t target = versions ? BUSTER_CHECK_PROGRAM_TARGET_VERSIONS : 0;
        const String8 text = specific != 0 && !versions ? check_program_cpu_version_features(program, specific, output) : applied != 0 ? applied->text : (String8){ 0 };
        valid &= specific == 0 || versions || text.pointer != 0;
        cpu = specific != 0 ? -1 : cpu;
        kind = specific != 0 ? PREDEFINES_TARGET_ATTRIBUTE_VERSION : kind;
        if (valid && !versions && kind == PREDEFINES_TARGET_ATTRIBUTE_VERSION && options->predefines != NULL)
        {
            // The target of the same string (the last body's, as a header's functions share theirs, first), or a new one.
            uint32_t found = last_target < program->target_count && check_program_text_equal(texts[last_target], text) ? last_target :
                                                                                                                         program->target_count;
            for (uint32_t t = 0; t < program->target_count && found == program->target_count; t += 1)
            {
                found = check_program_text_equal(texts[t], text) ? t : found;
            }
            if (found == program->target_count)
            {
                program->targets = program->targets == 0 ? BUSTER_ARENA_PUSH_ARRAY(output, X86Target, count) : program->targets;
                valid = program->targets != 0;
                if (valid && !program->host_read)
                {
                    program->host = predefines_host_of(options->predefines);
                    program->host_read = true;
                }
                if (valid)
                {
                    program->targets[found] = predefines_function_target(options->predefines, &program->host, program->target, text.pointer,
                                                                         (uint32_t)text.length, cpu);
                    texts[found] = text;
                    program->target_count += 1;
                }
            }
            last_target = found;
            target = found + 1;
        }
        if (valid && target != 0 && program->body_targets == 0)
        {
            program->body_targets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->body_count);
            valid = program->body_targets != 0;
            if (valid)
            {
                memset(program->body_targets, 0, (uint64_t)program->body_count * sizeof(uint32_t));
            }
        }
        if (valid && target != 0)
        {
            program->body_targets[b] = target;
        }
    }
    // target_clones' bodies, compiled once per version (#1838).
    if (valid && program->target_group_count != 0 && options->predefines != NULL)
    {
        valid = check_program_target_copies(program, options->predefines, output);
    }
    return valid;
}

// The keys code generation's check of target features reads (#775, CheckProgram.attribute_keys), in `output`, where the
// unit has a target attribute: the target attributes' declarations (check_program_targets sorted them stably by object,
// so each object's are still in token order) and then the always_inline, flatten and noreturn ones, sorted stably by
// object with the radix kernel's twins (c_bindings_sort). A late symbol rename also needs the callee keys for the
// shared emission walk's noreturn decisions. Other units without a target attribute keep none. Returns false if
// there was no room.
__attribute__((noinline)) static bool check_program_attribute_keys(CheckProgram* program, const CheckProgramTargetEvents* events, bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t total = (uint64_t)events->count + events->callee_count;
    bool valid = events->room && total <= UINT32_MAX;
    bool late = false;
    const CheckInputSymbolPragmas* pragmas = program->input->symbol_pragmas;
    for (uint32_t i = 0; events->callee_count != 0 && pragmas != NULL && !late && i < pragmas->count; i += 1)
    {
        late = pragmas->records[i].after_use;
    }
    // A late weak declaration's emission walk reads the callee keys too (#1849).
    late |= events->callee_count != 0 && program->weak_late_count != 0;
    if (valid && (events->count != 0 || late))
    {
        uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, total);
        uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, total);
        valid = keys != 0 && temporary != 0;
        if (valid)
        {
            if (events->count != 0)
            {
                memcpy(keys, events->keys, (uint64_t)events->count * sizeof(uint64_t));
            }
            if (events->callee_count != 0)
            {
                memcpy(keys + events->count, events->callee_keys, (uint64_t)events->callee_count * sizeof(uint64_t));
            }
            uint32_t varying = 0;
            for (uint64_t e = 1; e < total; e += 1)
            {
                varying |= (uint32_t)((keys[0] ^ keys[e]) >> 32);
            }
            const uint64_t* sorted = c_bindings_sort(keys, temporary, (uint32_t)total, !scalar, varying);
            if (sorted != keys)
            {
                memcpy(keys, sorted, total * sizeof(uint64_t));
            }
            program->attribute_keys = keys;
            program->attribute_key_count = (uint32_t)total;
        }
    }
    return valid;
}

// The patchable_function_entry declarations' keys (#1447, CheckProgram.patchable_keys), in `output`, where the unit has
// one: in token order as the scan found them, sorted stably by object with the radix kernel's twins (c_bindings_sort), so
// that each object's are in token order. Returns false if there was no room.
__attribute__((noinline)) static bool check_program_patchable_keys(CheckProgram* program, const CheckProgramTargetEvents* events, bool scalar, Arena* output, Arena* scratch)
{
    const uint32_t total = events->patchable_count;
    bool valid = events->room;
    if (valid && total != 0)
    {
        uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, total);
        uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, total);
        valid = keys != 0 && temporary != 0;
        if (valid)
        {
            memcpy(keys, events->patchable_keys, (uint64_t)total * sizeof(uint64_t));
            uint32_t varying = 0;
            for (uint32_t e = 1; e < total; e += 1)
            {
                varying |= (uint32_t)((keys[0] ^ keys[e]) >> 32);
            }
            const uint64_t* sorted = c_bindings_sort(keys, temporary, total, !scalar, varying);
            if (sorted != keys)
            {
                memcpy(keys, sorted, (uint64_t)total * sizeof(uint64_t));
            }
            program->patchable_keys = keys;
            program->patchable_key_count = total;
        }
    }
    return valid;
}

// A function's patchable entry (#1447): `count` bytes of NOPs, `offset` of them before its symbol (none for a count of 0),
// and the section of its entry (empty for __patchable_function_entries).
typedef struct CheckProgramPatchable CheckProgramPatchable;
struct CheckProgramPatchable
{
    uint32_t count;
    uint32_t offset;
    String8 section;
};

// Body `b`'s patchable entry (#1447), as clang's code generation takes it (CodeGenFunction::StartFunction): the first
// patchable_function_entry attribute of its function's last declaration with one up to its definition (clang's
// redeclarations inherit it where they have none of their own; it drops one after the definition with a warning), whose
// count of 0 makes none; or without one, -fpatchable-function-entry's (CheckProgram.patchable_count); and its section, the
// attribute's, or where it names none, the option's. A binary search of CheckProgram.patchable_keys, which a unit without
// the attribute hasn't.
static CheckProgramPatchable check_program_patchable(const CheckProgram* program, uint32_t b)
{
    const CheckProgramBody body = program->bodies[b];
    CheckProgramPatchable result = { .count = program->patchable_count, .offset = program->patchable_offset };
    String8 section = { 0 };
    if (program->patchable_key_count != 0)
    {
        // The last key at or before (object, definition).
        const uint64_t wanted = ((uint64_t)body.object << 32) | body.declaration;
        uint32_t low = 0;
        uint32_t high = program->patchable_key_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            low = program->patchable_keys[middle] <= wanted ? middle + 1 : low;
            high = program->patchable_keys[middle] <= wanted ? high : middle;
        }
        const uint64_t key = low != 0 ? program->patchable_keys[low - 1] : UINT64_MAX;
        const CheckInputAttributes* attributes = key != UINT64_MAX && (uint32_t)(key >> 32) == body.object ? check_input_attributes_at(program->input, (uint32_t)key) : 0;
        const CheckInputAttribute* attribute = 0;
        for (uint32_t i = 0; attributes != 0 && attribute == 0 && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* record = &attributes->records[i];
            attribute = record->kind == CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY && !record->ignored && !record->subject_ignored && record->argument_values != 0 ? record : 0;
        }
        if (attribute != 0)
        {
            result.count = (uint32_t)attribute->argument_values[0];
            result.offset = attribute->argument_count >= 2 ? (uint32_t)attribute->argument_values[1] : 0;
            section = attribute->argument_count == 3 ? attribute->text : section;
        }
    }
    result.section = section.length != 0 ? section : program->patchable_section;
    // (The checker and the driver refuse an offset past the count.)
    result.offset = result.count != 0 ? result.offset : 0;
    return result;
}

// #442's kernels (c_check_program_select.c), which check_program numbers the needed objects' symbols with too.
static uint32_t check_program_needed_count(const uint8_t* needed, uint32_t objects, bool scalar);
static void check_program_needed_number(const uint8_t* needed, uint32_t objects, SymbolId* object_symbols, uint32_t* symbol_objects, bool scalar);

#include "c_check_x86_diagnostic.c"
#include "c_check_target_feature.c"
#include "c_check_program_alias.c"
#include "c_check_program_lifetime.c"

// The entries that cleanups add at most (#147, #1370): at each jump (whose keyword has the role RETURN) a RESULT and an
// EXIT, and at each variable's scope's end its CLEANUP. Statement expressions use the same compact exits; a fallback
// through a missing scope end expands only during lowering. No exit needs a run's worth of control entries.
static uint64_t check_program_cleanup_room(const Parser* parser, const CheckInput* input, uint32_t* jumps_out)
{
    uint64_t room = 0;
    *jumps_out = 0;
    if (input->cleanup_count != 0)
    {
        const uint32_t count = input->token_count;
        const uint8_t* const roles = parser->tree.roles;
        uint32_t jumps = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            jumps += roles[token] == PARSE_ROLE_RETURN;
        }
        *jumps_out = jumps;
        room = (uint64_t)jumps * 2 + input->cleanup_count;
    }
    return room;
}

// A census's body `b` (#2536's step 2.1): its candidates' references, `found` of them in `sorted`'s high halves, kept as
// census->references' run from census->first[b], each body's pushed after the one before into `output`, which nothing
// else takes meanwhile. False if there's no room.
static bool check_program_census_store(CheckProgramCensus* census, uint32_t b, const uint64_t* sorted, uint32_t found, uint32_t* total, Arena* output)
{
    uint32_t* kept = found != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, found) : NULL;
    const bool valid = found == 0 || kept != NULL;
    if (valid)
    {
        census->references = census->references == NULL ? kept : census->references;
        census->first[b] = *total;
        census->counts[b] = found;
        for (uint32_t r = 0; r < found; r += 1)
        {
            kept[r] = (uint32_t)(sorted[r] >> 32);
        }
        *total += found;
    }
    return valid;
}

// Whether body `b` has an arm of a constant `if` that its controls leave out: one of the writer's `dropped` ranges,
// `dropped_count` of them, each's first token in its key's high half, inside the body's braces.
static bool check_program_census_armed(const CheckProgram* program, const Parser* parser, uint32_t b, const uint64_t* dropped, uint32_t dropped_count)
{
    const CheckProgramBody* body = program->bodies + b;
    const uint32_t closing = parser->tree.parents[body->opening];
    bool arm = false;
    for (uint32_t d = 0; d < dropped_count; d += 1)
    {
        const uint32_t first = (uint32_t)(dropped[d] >> 32);
        arm = arm || (first > body->opening && first < closing);
    }
    return arm;
}

// A census's comparison with the bodies' own references (CheckProgram.references; both sorted), by one merge a body: a
// body counts as differing if they aren't the same, as unarmed if so without a dropped arm (check_program_census_armed),
// and as missing if the candidates leave out one of its own.
static void check_program_census_compare(const CheckProgram* program, const Parser* parser, CheckProgramCensus* census, const uint64_t* dropped,
                                         uint32_t dropped_count)
{
    for (uint32_t b = 0; census->first != NULL && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        const uint32_t* kept = census->references + census->first[b];
        const uint32_t found = census->counts[b];
        uint32_t extra = 0;
        uint32_t missing = 0;
        uint32_t e = 0;
        for (uint32_t r = 0; r < found; r += 1)
        {
            while (e < body->reference_count && program->references[body->reference_first + e] < kept[r])
            {
                missing += 1;
                e += 1;
            }
            const bool matched = e < body->reference_count && program->references[body->reference_first + e] == kept[r];
            extra += matched ? 0 : 1;
            e += matched ? 1 : 0;
        }
        missing += body->reference_count - e;
        const bool same = extra == 0 && missing == 0;
        census->differing += same ? 0 : 1;
        census->unarmed += !same && !check_program_census_armed(program, parser, b, dropped, dropped_count) ? 1 : 0;
        census->missing += missing != 0 ? 1 : 0;
    }
}

// The walk's census (#2536's step 2.1, docs/all-core-path.md's "Step 2: the selection after the windows"): each body's
// candidates' references by check_program_references' walk started from every expression root of the body (a node of its
// tokens, its declaration's to its closing brace's, that no node takes as an operand; token_first and node_next list
// them), from its automatic objects' initializer plans (`plan_first`'s lists) and from its cleanups, rather than from the
// controls it reaches. So a body's candidates' references are its own and those of the arms of a constant `if` its
// controls leave out, and of the operands no node takes once the checker has folded the node that did (`sizeof
// "literal"`, `_Generic`'s other associations). A static declaration's initializer is its object's data, which the
// selection reaches through the object: its plan's nodes and the nodes over them are no body's roots. Into
// program->census_walk, its runs in `output`. The walk's marks in `seen` and `nodes` take epochs past the bodies', its
// `needed` column is its own, and what it writes besides its references (CheckProgram.region_reached,
// compound_initializers, the status) is put back. False if there's no room.
static bool check_program_census_walk(CheckProgram* program, const Parser* parser, const uint32_t* token_first, const uint32_t* node_next, const uint32_t* plan_first,
                                      const uint64_t* planned, const uint32_t* plan_next, uint32_t* seen,
                                      uint32_t* nodes, uint32_t* stack, uint64_t* reference_items, uint64_t* reference_sort, bool scalar, Arena* output,
                                      Arena* scratch)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t count = input->token_count;
    const uint32_t node_count = graph->count;
    const uint32_t plans = program->initializers != NULL ? program->initializers->count : 0;
    const uint64_t temporary = scratch->length;
    CheckProgramCensus* census = &program->census_walk;
    uint8_t* operand = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)node_count + 1);
    uint8_t* data_nodes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)node_count + 1);
    uint8_t* needed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    CheckControl* roots = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckControl, (uint64_t)node_count + plans + input->cleanup_count + 1);
    uint8_t* region_reached = program->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, program->region_count) : NULL;
    uint8_t* compound = program->compound_initializers != NULL && plans != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, plans) : NULL;
    census->first = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)program->body_count + 1);
    census->counts = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)program->body_count + 1);
    bool valid = operand != NULL && data_nodes != NULL && needed != NULL && roots != NULL && (program->region_count == 0 || region_reached != NULL) &&
                 (program->compound_initializers == NULL || plans == 0 || compound != NULL) && census->first != NULL && census->counts != NULL;
    const CheckProgramStatus status = program->status;
    const uint32_t token = program->token;
    if (valid)
    {
        memset(operand, 0, (uint64_t)node_count + 1);
        memset(data_nodes, 0, (uint64_t)node_count + 1);
        memset(needed, 0, (uint64_t)count + 1);
        for (uint32_t plan = 0; plan < plans; plan += 1)
        {
            const uint32_t declaration = program->initializers->declaration_tokens[plan];
            const CheckInitializer* initializer = program->initializers->plans + plan;
            for (uint32_t part = 0; declaration < count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) && part < initializer->count;
                 part += 1)
            {
                const uint32_t node = initializer->parts[part].node;
                data_nodes[node < node_count ? node : node_count] = 1;
            }
        }
        data_nodes[node_count] = 0;
        // Operands come before the nodes that take them, so one pass marks the operands and carries the data's mark up.
        for (uint32_t node = 0; node < node_count; node += 1)
        {
            const uint32_t a = graph->operand0[node] < node_count ? graph->operand0[node] : node_count;
            const uint32_t c = graph->operand1[node] < node_count ? graph->operand1[node] : node_count;
            const uint32_t d = graph->operand2[node] < node_count ? graph->operand2[node] : node_count;
            operand[a] = 1;
            operand[c] = 1;
            operand[d] = 1;
            data_nodes[node] |= data_nodes[a] | data_nodes[c] | data_nodes[d];
        }
        for (uint32_t node = 0; node < node_count; node += 1)
        {
            operand[node] |= data_nodes[node];
        }
        if (region_reached != NULL)
        {
            memcpy(region_reached, program->region_reached, program->region_count);
        }
        if (compound != NULL)
        {
            memcpy(compound, program->compound_initializers, plans);
        }
    }
    uint32_t total = 0;
    for (uint32_t b = 0; valid && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        const uint32_t closing = parser->tree.parents[body->opening];
        uint32_t root_count = 0;
        for (uint32_t t = body->declaration; t <= closing && t < count; t += 1)
        {
            for (uint32_t node = token_first[t]; node < node_count; node = node_next[node])
            {
                if (operand[node] == 0)
                {
                    roots[root_count++] = (CheckControl){ .node = node, .token = t, .target = 0, .kind = CHECK_CONTROL_EVALUATE };
                }
            }
            if (check_program_bit(planned, t))
            {
                for (uint32_t plan = plan_first[t]; plan < plans; plan = plan_next[plan])
                {
                    const uint32_t declaration = program->initializers->declaration_tokens[plan];
                    if (declaration < count && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC))
                    {
                        roots[root_count++] = (CheckControl){ .node = BUSTER_CHECK_NONE, .token = t, .target = plan, .kind = CHECK_CONTROL_INITIALIZE };
                    }
                }
            }
        }
        for (uint32_t r = 0; r < body->cleanup_count; r += 1)
        {
            roots[root_count++] = (CheckControl){ .node = BUSTER_CHECK_NONE, .token = 0, .target = body->cleanup_first + r, .kind = CHECK_CONTROL_CLEANUP };
        }
        const uint32_t epoch = program->body_count + 1 + b;
        const uint32_t found = check_program_references(program, roots, root_count, epoch, seen, nodes, stack, reference_items, needed);
        uint32_t varying = 0;
        for (uint32_t r = 1; r < found; r += 1)
        {
            varying |= (uint32_t)((reference_items[0] ^ reference_items[r]) >> 32);
        }
        const uint64_t* sorted = c_bindings_sort(reference_items, reference_sort, found, !BUSTER_SCALAR_PATH(scalar), varying);
        valid = check_program_census_store(census, b, sorted, found, &total, output);
    }
    if (region_reached != NULL)
    {
        memcpy(program->region_reached, region_reached, program->region_count);
    }
    if (compound != NULL)
    {
        memcpy(program->compound_initializers, compound, plans);
    }
    program->status = status;
    program->token = token;
    arena_pop_to(scratch, temporary);
    return valid;
}

// Whether token `t` is in one of `arms`, `arm_count` token ranges sorted and apart, each's first token in its key's high
// half and its last in its low half: a binary search for the last range that starts at or before it.
static bool check_program_census_left_out(const uint64_t* arms, uint32_t arm_count, uint32_t t)
{
    uint32_t low = 0;
    uint32_t high = arm_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = (uint32_t)(arms[middle] >> 32) <= t;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low != 0 && t <= (uint32_t)arms[low - 1];
}

// The graph's 64-node chunks that hold a node of a body (bit k of `touched` for nodes 64k to 64k + 63; the last, partial
// chunk's always): a chunk whose tokens' least to greatest meet one of the bodies `declarations` to `closings` (each's
// declaration to its closing brace, `body_count` of them, sorted and apart), the last that starts at or before the
// greatest, which a cursor finds as the chunks go. A chunk with a token past the unit's (none of a body's) counts. No
// other chunk has a node that names a body's object, takes a body's operand or holds a body's condition, so the pass's
// scans skip them: on main.c, whose unit has the compiler's tables, more than half its nodes. Four vectors' least and
// greatest lane by lane, then one reduction of each. AVX-512 only; the scalar path scans every node.
static void check_program_census_touched(const CheckProgram* program, const uint32_t* declarations, const uint32_t* closings, uint32_t body_count, uint64_t* touched)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t chunks = graph->count / 64;
    const uint32_t count = program->input->token_count;
    memset(touched, 0, ((uint64_t)chunks / 64 + 1) * sizeof(uint64_t));
    uint32_t at = 0;
    for (uint32_t k = 0; k < chunks; k += 1)
    {
        const uint32_t* tokens = graph->tokens + (uint64_t)k * 64;
        const __m512i first = _mm512_loadu_si512(tokens);
        const __m512i second = _mm512_loadu_si512(tokens + 16);
        const __m512i third = _mm512_loadu_si512(tokens + 32);
        const __m512i fourth = _mm512_loadu_si512(tokens + 48);
        const __m512i least = _mm512_min_epu32(_mm512_min_epu32(first, second), _mm512_min_epu32(third, fourth));
        const __m512i greatest = _mm512_max_epu32(_mm512_max_epu32(first, second), _mm512_max_epu32(third, fourth));
        const bool past = _mm512_cmpge_epu32_mask(greatest, _mm512_set1_epi32((int)count)) != 0;
        const uint32_t low = _mm512_reduce_min_epu32(least);
        const uint32_t high = _mm512_reduce_max_epu32(greatest);
        while (!past && at + 1 < body_count && declarations[at + 1] <= high)
        {
            at += 1;
        }
        while (!past && at > 0 && declarations[at] > high)
        {
            at -= 1;
        }
        const bool hit = past || (at < body_count && declarations[at] <= high && low <= closings[at]);
        touched[k / 64] |= (uint64_t)hit << (k % 64);
    }
    touched[chunks / 64] |= UINT64_C(1) << (chunks % 64);
}

// The first 16-node block from node `block` on, before `end`, that is in a chunk `touched` marks
// (check_program_census_touched), else `end`; `block` itself without it, or from `end` on. The search reads the marks'
// words, a word for 4,096 nodes, so a run of chunks outside the bodies costs a word, not its blocks' iterations.
static inline uint64_t check_program_census_next(const uint64_t* touched, uint64_t block, uint64_t end)
{
    uint64_t next = block;
    if (touched != NULL && block < end)
    {
        const uint64_t last = (end - 1) / 4096;
        uint64_t word = block / 4096;
        uint64_t bits = touched[word] & (~UINT64_C(0) << ((block / 64) % 64));
        while (bits == 0 && word < last)
        {
            word += 1;
            bits = touched[word];
        }
        const uint64_t chunk = bits != 0 ? (word * 64 + (uint64_t)__builtin_ctzll(bits)) * 64 : end;
        next = chunk > block ? chunk : block;
        next = next < end ? next : end;
    }
    return next;
}

// Whether one of `arms`, `arm_count` token ranges as check_program_census_left_out takes them, meets tokens `low` to
// `high`: the last range that starts at or before `high` ends at or after `low`.
static bool check_program_census_meets(const uint64_t* arms, uint32_t arm_count, uint32_t low, uint32_t high)
{
    uint32_t first = 0;
    uint32_t end = arm_count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        const bool before = (uint32_t)(arms[middle] >> 32) <= high;
        first = before ? middle + 1 : first;
        end = before ? end : middle;
    }
    return first != 0 && low <= (uint32_t)arms[first - 1];
}

// Whether node `node` has its mark in `dead`, a bit a node (check_program_census_prune).
static inline bool check_program_census_dead(const uint64_t* dead, uint32_t node)
{
    return ((dead[node / 64] >> (node % 64)) & 1) != 0;
}

// The pruned scan's marks (check_program_census_scan): the operands that `node` leaves out, as check_program_references
// does (all of sizeof's and _Alignof's, the one a constant condition of &&, || or ?: skips, a computed call's callee), and
// every node below them, in `dead` (a bit a node), by a walk on `stack` (room for every node: a node is pushed once, as
// it's marked).
static void check_program_census_prune(const CheckProgram* program, uint32_t node, uint64_t* dead, uint32_t* stack)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    bool drop[] = { false, false, false };
    bool truth = false;
    if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
    {
        drop[0] = true;
        drop[1] = true;
        drop[2] = true;
    }
    else if (op == CHECK_OP_CALL)
    {
        drop[0] = check_program_call_computed(program, node) != BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS;
    }
    else if (!program->unselected_operands && operands[0] < graph->count && check_program_runtime_truth(program, node, operands[0], &truth))
    {
        const uint32_t dropped = op == CHECK_OP_CONDITIONAL ? (truth ? 2 : 1) : 1;
        drop[dropped] = op == CHECK_OP_CONDITIONAL || (op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth);
    }
    uint32_t depth = 0;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
    {
        if (drop[k] && operands[k] < graph->count && !check_program_census_dead(dead, operands[k]))
        {
            dead[operands[k] / 64] |= UINT64_C(1) << (operands[k] % 64);
            stack[depth++] = operands[k];
        }
    }
    while (depth != 0)
    {
        const uint32_t at = stack[--depth];
        const uint32_t below[] = { graph->operand0[at], graph->operand1[at], graph->operand2[at] };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(below); k += 1)
        {
            if (below[k] < graph->count && !check_program_census_dead(dead, below[k]))
            {
                dead[below[k] / 64] |= UINT64_C(1) << (below[k] % 64);
                stack[depth++] = below[k];
            }
        }
        for (uint32_t a = 0; check_graph_has_arguments(graph->ops[at]) && a < graph->argument_counts[at]; a += 1)
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[at] + a];
            if (!check_program_census_dead(dead, argument))
            {
                dead[argument / 64] |= UINT64_C(1) << (argument % 64);
                stack[depth++] = argument;
            }
        }
    }
}

// The cleanups of the body between `opening` and `closing`, as check_control_begin finds them: the first of its records
// (CheckInput.cleanups, in declaration order) in *first and how many in *count.
static void check_program_body_cleanups(const CheckInput* input, uint32_t opening, uint32_t closing, uint32_t* first, uint32_t* count)
{
    const CheckInputCleanup* cleanups = input->cleanups;
    uint32_t low = 0;
    uint32_t high = input->cleanup_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = cleanups[middle].declaration < opening ? middle + 1 : low;
        high = cleanups[middle].declaration < opening ? high : middle;
    }
    uint32_t end = low;
    while (end < input->cleanup_count && cleanups[end].declaration < closing)
    {
        end += 1;
    }
    *first = low;
    *count = end - low;
}

// The scan's vector path gathers a leaf's flags and object as one qword.
_Static_assert(offsetof(CheckGraphLeaf, object) == offsetof(CheckGraphLeaf, flags) + sizeof(uint32_t), "a leaf's object follows its flags");

// A live leaf `node` of the scan that names `object`, an automatic compound literal's (#2536's step 2.2c.3, part 3): its plan
// marked in `marks` where a body holds the node's token and no range of `arms` holds it, as the references pass marks
// CheckProgram.compound_initializers for the leaves its walk reaches. Its own cursor over the bodies, *b; such leaves are
// few.
static void check_program_census_compound(const CheckProgram* program, const uint32_t* declarations, const uint32_t* closings, uint32_t body_count,
                                          const uint64_t* arms, uint32_t arm_count, uint32_t node, uint32_t object, uint32_t* b, uint8_t* marks)
{
    const uint32_t t = program->input->graph.tokens[node];
    uint32_t at = *b;
    while (at + 1 < body_count && declarations[at + 1] <= t)
    {
        at += 1;
    }
    while (at > 0 && declarations[at] > t)
    {
        at -= 1;
    }
    const bool inside = at < body_count && declarations[at] <= t && t <= closings[at] && !check_program_census_left_out(arms, arm_count, t);
    const uint32_t plan = inside && program->initializers != NULL ? check_initializers_plan_index(program->initializers, object - 1) : 0;
    if (plan != 0 && plan <= program->initializers->count)
    {
        marks[plan - 1] = 1;
    }
    *b = at;
}

// One VALUE node of the scan (check_program_census_scan): where its object has static storage, the node isn't `dead` and
// no range of `arms` holds its token, the object's pair with the body whose tokens hold the node (a declaration's to its
// closing brace's: `declarations` and `closings`, body_count of them), the cursor *b moved to that body.
static inline __attribute__((always_inline)) void check_program_census_leaf(const CheckProgram* program, const uint32_t* declarations, const uint32_t* closings,
                                                                           uint32_t body_count, const uint64_t* dead, const uint64_t* arms, uint32_t arm_count,
                                                                           uint32_t node, uint32_t* b, uint64_t* pairs, uint32_t* pair_count, uint32_t* starts,
                                                                           uint32_t* compounds, uint32_t* compound_count)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t object = check_graph_leaf_object(graph, node);
    // With `compounds`, a live leaf that names an automatic compound literal, kept for check_program_census_compound.
    const bool compound = compounds != NULL && object != 0 && object < program->object_count && (dead == NULL || !check_program_census_dead(dead, node)) &&
                          (input->declaration_bits[object - 1] & (BUSTER_CHECK_BITS_COMPOUND | BUSTER_CHECK_BITS_STATIC)) == BUSTER_CHECK_BITS_COMPOUND;
    if (compounds != NULL)
    {
        compounds[*compound_count] = node;
        *compound_count += compound ? 1 : 0;
    }
    const bool fixed = object != 0 && object < program->object_count && (dead == NULL || !check_program_census_dead(dead, node)) &&
                       ((check_graph_leaf_flags(graph, node) & BUSTER_CHECK_STATIC_OBJECT) != 0 ||
                        check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC));
    const uint32_t t = graph->tokens[node];
    uint32_t at = *b;
    while (fixed && at + 1 < body_count && declarations[at + 1] <= t)
    {
        at += 1;
    }
    while (fixed && at > 0 && declarations[at] > t)
    {
        at -= 1;
    }
    const bool inside = fixed && at < body_count && declarations[at] <= t && t <= closings[at] && !check_program_census_left_out(arms, arm_count, t);
    pairs[*pair_count] = (uint64_t)at << 32 | object;
    *pair_count += inside ? 1 : 0;
    starts[at < body_count ? at : body_count] += inside ? 1 : 0;
    *b = at;
}

// The builtin nodes the candidates' pass reaches (#2536's step 2.2c.3, part 2): every builtin node (check_program_builtin_op)
// whose token one of the bodies holds (a declaration's to its closing brace's: `declarations` and `closings`, body_count
// of them), but not one of `dead`'s (an operand the references' walk leaves out, check_program_census_prune) or one that
// a range of `arms` holds (a constant if's dropped arm): the builtins the bodies' references would list
// (CheckProgram.builtin_nodes) and those the walk reaches no other way, in node order, into
// CheckProgram.census_pass_builtins from `output`. With AVX-512 the ops of `touched`'s chunks 16 at a time, few of them
// builtins, each of which takes the scalar test of its body; the rest and the `scalar` path one node at a time. False if
// there's no room.
static bool check_program_census_builtins(CheckProgram* program, const uint32_t* declarations, const uint32_t* closings, uint32_t body_count, const uint64_t* dead,
                                          const uint64_t* arms, uint32_t arm_count, const uint64_t* touched, bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    const CheckGraph* graph = &program->input->graph;
    const uint32_t node_count = graph->count;
    const uint64_t temporary = scratch->length;
    uint32_t* found = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1);
    uint32_t count = 0;
    uint32_t at = 0;
    const uint64_t limit = BUSTER_SCALAR_PATH(scalar) || found == NULL ? 0 : node_count & ~UINT32_C(15);
    uint64_t node = check_program_census_next(touched, 0, limit);
    for (; node < limit; node = check_program_census_next(touched, node + 16, limit))
    {
        const __m512i ops = _mm512_loadu_si512(graph->ops + node);
        __mmask16 candidates = _mm512_cmpge_epu32_mask(ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_UNARY));
        candidates &= dead != NULL ? (__mmask16)~(dead[node / 64] >> (node % 64)) : (__mmask16)0xffff;
        for (uint32_t lanes = candidates; lanes != 0; lanes &= lanes - 1)
        {
            const uint32_t n = (uint32_t)node + (uint32_t)__builtin_ctz(lanes);
            const uint32_t t = graph->tokens[n];
            while (at + 1 < body_count && declarations[at + 1] <= t)
            {
                at += 1;
            }
            while (at > 0 && declarations[at] > t)
            {
                at -= 1;
            }
            const bool inside = at < body_count && declarations[at] <= t && t <= closings[at] && !check_program_census_left_out(arms, arm_count, t);
            found[count] = n;
            count += check_program_builtin_op(graph->ops[n]) && inside ? 1 : 0;
        }
    }
    for (node = limit; found != NULL && node < node_count; node += 1)
    {
        const uint32_t t = graph->tokens[node];
        while (at + 1 < body_count && declarations[at + 1] <= t)
        {
            at += 1;
        }
        while (at > 0 && declarations[at] > t)
        {
            at -= 1;
        }
        const bool inside = at < body_count && declarations[at] <= t && t <= closings[at] && !check_program_census_left_out(arms, arm_count, t);
        const bool alive = dead == NULL || !check_program_census_dead(dead, (uint32_t)node);
        found[count] = (uint32_t)node;
        count += check_program_builtin_op(graph->ops[node]) && alive && inside ? 1 : 0;
    }
    program->census_pass_builtins = found != NULL && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : NULL;
    program->census_pass_builtin_count = program->census_pass_builtins != NULL ? count : 0;
    const bool valid = found != NULL && (count == 0 || program->census_pass_builtins != NULL);
    if (program->census_pass_builtins != NULL)
    {
        memcpy(program->census_pass_builtins, found, (uint64_t)count * sizeof(uint32_t));
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// The initializers' plans and the asm statements the candidates' pass reaches (#2536's step 2.2c.3, part 3): each plan of a
// local declaration (CheckInitializers.declaration_tokens, not of static storage) and each asm statement of a body
// (CheckInputAsm.statement) whose token one of the bodies holds (`declarations` to `closings`, body_count of them) and no
// range of `arms` holds, and each automatic compound literal's plan that `marks` has (check_program_census_compound: a live
// leaf names it), by index in token order, into CheckProgram.census_pass_initializers and census_pass_asms from `output`:
// the targets of the INITIALIZE and ASM controls the bodies' writer would make and the compound literals their references
// reach, which the lowering's pools and asm plan read. A pass over the plans' and the statements' tokens in token order, a
// merge with the bodies and the arms (below), the asm statements, a unit's few, by the bodies alone. False if there's no
// room.
static bool check_program_census_statements(CheckProgram* program, const uint32_t* declarations, const uint32_t* closings, uint32_t body_count, const uint64_t* arms,
                                            uint32_t arm_count, const uint8_t* marks, Arena* output)
{
    const CheckInput* input = program->input;
    const CheckInitializers* initializers = program->initializers;
    const uint32_t plan_count = initializers != NULL && initializers->declaration_tokens != NULL ? initializers->count : 0;
    const uint32_t asm_count = input->asm_statements != NULL ? input->asm_count : 0;
    program->census_pass_initializers = plan_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, plan_count) : NULL;
    program->census_pass_asms = asm_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, asm_count) : NULL;
    const bool valid = (plan_count == 0 || program->census_pass_initializers != NULL) && (asm_count == 0 || program->census_pass_asms != NULL);
    uint32_t count = 0;
    uint32_t at = 0;
    uint32_t arm = 0;
    // The plans' tokens ascend, as the bodies' and the dropped arms' do, so the plans are a merge of the three: a cursor over
    // the bodies and one over the arms (those that start at or before the plan's token), each only moving on. It stays
    // scalar: a body's plans are few (2.5 on main.c, 1.5 on sqlite3.c), so 16 plans at a time met several bodies nearly
    // every time and fell back to a plan at a time, where the arms' binary search, which this cursor replaces, cost main.c's
    // pass 0.2 ms on the 9700X.
    for (uint32_t i = 0; valid && i < plan_count; i += 1)
    {
        const uint32_t t = initializers->declaration_tokens[i];
        while (at + 1 < body_count && declarations[at + 1] <= t)
        {
            at += 1;
        }
        while (arm < arm_count && (uint32_t)(arms[arm] >> 32) <= t)
        {
            arm += 1;
        }
        const bool known = t < input->token_count;
        const uint8_t bits = known ? input->declaration_bits[t] : 0;
        const bool compound = (bits & BUSTER_CHECK_BITS_COMPOUND) != 0;
        const bool inside = known && at < body_count && declarations[at] <= t && t <= closings[at] && !(arm != 0 && t <= (uint32_t)arms[arm - 1]) &&
                            (bits & BUSTER_CHECK_BITS_STATIC) == 0;
        program->census_pass_initializers[count] = i;
        count += compound ? (marks != NULL && marks[i] != 0 ? 1 : 0) : inside ? 1 : 0;
    }
    program->census_pass_initializer_count = valid ? count : 0;
    count = 0;
    at = 0;
    for (uint32_t a = 0; valid && a < asm_count; a += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + a;
        const uint32_t t = assembly->statement;
        while (at + 1 < body_count && declarations[at + 1] <= t)
        {
            at += 1;
        }
        while (at > 0 && declarations[at] > t)
        {
            at -= 1;
        }
        const bool inside = assembly->body != BUSTER_CHECK_NONE && at < body_count && declarations[at] <= t && t <= closings[at] &&
                            !check_program_census_left_out(arms, arm_count, t);
        program->census_pass_asms[count] = a;
        count += inside ? 1 : 0;
    }
    program->census_pass_asm_count = valid ? count : 0;
    return valid;
}

// The scan's census (#2536's step 2.1): each body's candidates' references by one pass over the graph's VALUE nodes
// rather than a walk: every one of the body's tokens (its declaration's to its closing brace's) that names an object of
// static storage, as check_program_references' leaves do, whatever control evaluates it or doesn't, and its cleanups'
// functions. The bodies are the ones `openings` opens, `body_count` of them, so the scan needs no body's controls. Node
// order is token order but for an expression's operands, so one cursor finds each node's body. A body's candidates are
// its walk's and what an operand of sizeof, typeof or _Alignof, a computed call's callee and a static local's initializer
// name. A unit with overloads or CFStrings, whose references come from what no VALUE node names, has none: the walk's
// census stands for it. With `arms` (check_program_census_left_out's ranges), it leaves out their tokens' nodes, and with
// `prune` the operands the walk leaves out: of sizeof and _Alignof, a constant condition's, a computed call's callee. Into
// `census`, its runs in `output`. With AVX-512 the scans take 16 nodes at a time and, given `touched`
// (check_program_census_touched), only its chunks; the rest and the `scalar` path one at a time. False if there's no room.
static bool check_program_census_scan(CheckProgram* program, CheckProgramCensus* census, const Parser* parser, const uint32_t* openings, uint32_t body_count,
                                      const uint64_t* arms, uint32_t arm_count, bool prune, bool builtins, const uint64_t* touched, uint64_t* reference_items,
                                      uint64_t* reference_sort, bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t node_count = graph->count;
    const uint64_t temporary = scratch->length;
    // A pair a VALUE node at most, each a leaf (CheckGraph.leaf_count), and room for a block's 16 stored whole.
    const uint64_t leaves = (uint64_t)graph->leaf_count + 1;
    uint64_t* pairs = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, leaves + 16);
    uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body_count + 1);
    uint32_t* objects = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, leaves);
    uint32_t* declarations = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body_count + 1);
    uint32_t* closings = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)body_count + 1);
    census->first = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)body_count + 1);
    census->counts = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)body_count + 1);
    // The dead marks, a bit a node: an eighth of a byte column's pages, each a fault in a fresh scratch.
    const uint64_t dead_words = (uint64_t)node_count / 64 + 1;
    uint64_t* dead = prune ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, dead_words) : NULL;
    uint32_t* walk = prune ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : NULL;
    bool valid = pairs != NULL && starts != NULL && objects != NULL && declarations != NULL && closings != NULL && census->first != NULL && census->counts != NULL &&
                 (!prune || (dead != NULL && walk != NULL));
    uint32_t pair_count = 0;
    uint32_t b = 0;
    for (uint32_t k = 0; valid && k < body_count; k += 1)
    {
        declarations[k] = check_sparse_get(input->body_declarations, openings[k]);
        closings[k] = parser->tree.parents[openings[k]];
    }
    if (valid && prune)
    {
        // The operands check_program_references leaves out and every node below them: the nodes that leave operands out
        // are few (sizeof, _Alignof, &&, || and ?: of a constant condition, a computed call), so a scan of the ops finds
        // them, 16 at a time with AVX-512, and a walk from each of their left-out operands marks its nodes. A label in a
        // left-out operand, for which the walk keeps it, isn't looked for: the census's missing references would say so.
        memset(dead, 0, dead_words * sizeof(uint64_t));
        // Of the calls, only one the reader marked (BUSTER_CHECK_LIBRARY_CALL in its member) can be computed. Of &&, || and
        // ?:, only one whose condition is a constant or a builtin's query has a truth (check_program_runtime_truth): their
        // nodes, compressed into `pending` as the scan finds them, have their conditions' constant kinds and ops gathered
        // 16 at a time (a kind's byte column read as dwords where 3 bytes past are the column's), and only those, a dense
        // stream's and one near the column's end take check_program_census_prune. The marks are a union, in any order.
        const CheckStream* stream = program->stream;
        uint32_t pending[128 + 16];
        uint32_t pending_count = 0;
        const __m512i lane = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        const __m512i zero = _mm512_setzero_si512();
        // A gather's index is signed: past 2^31 nodes, the scalar loop takes them all.
        const uint32_t scanned = BUSTER_SCALAR_PATH(scalar) || node_count >= (UINT32_C(1) << 31) ? 0 : node_count & ~UINT32_C(15);
        // The last block, `scanned`, scans nothing and takes what is pending.
        for (uint64_t block = check_program_census_next(touched, 0, scanned); !BUSTER_SCALAR_PATH(scalar) && node_count < (UINT32_C(1) << 31) && block <= scanned;
             block = check_program_census_next(touched, block + 16, scanned))
        {
            const bool scanning = block < scanned;
            const __m512i ops = scanning ? _mm512_loadu_si512(graph->ops + block) : zero;
            const __mmask16 marked =
                scanning ? _mm512_test_epi32_mask(_mm512_loadu_si512(graph->members + block), _mm512_set1_epi32((int)BUSTER_CHECK_LIBRARY_CALL)) : (__mmask16)0;
            const __mmask16 always = scanning ? _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_SIZEOF)) |
                                                    _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_ALIGNOF)) |
                                                    (marked & _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CALL))) :
                                                (__mmask16)0;
            const __mmask16 conditional = scanning && !program->unselected_operands ? _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_LOGICAL_AND)) |
                                                                                          _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_LOGICAL_OR)) |
                                                                                          _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CONDITIONAL)) :
                                                                                      (__mmask16)0;
            for (uint32_t lanes = always; lanes != 0; lanes &= lanes - 1)
            {
                check_program_census_prune(program, (uint32_t)block + (uint32_t)__builtin_ctz(lanes), dead, walk);
            }
            _mm512_mask_compressstoreu_epi32(pending + pending_count, conditional, _mm512_add_epi32(_mm512_set1_epi32((int)block), lane));
            pending_count += (uint32_t)__builtin_popcount(conditional);
            uint32_t ready = pending_count >= 128 ? pending_count & ~UINT32_C(15) : 0;
            ready = block < scanned ? ready : pending_count;
            for (uint32_t i = 0; i < ready; i += 16)
            {
                const __mmask16 present = (__mmask16)(ready - i >= 16 ? 0xffff : (1u << (ready - i)) - 1);
                const __m512i nodes = _mm512_maskz_loadu_epi32(present, pending + i);
                const __m512i conditions = _mm512_mask_i32gather_epi32(zero, present, nodes, graph->operand0, 4);
                const __mmask16 inside = present & _mm512_cmplt_epu32_mask(conditions, _mm512_set1_epi32((int)node_count));
                const __mmask16 safe =
                    stream->dense_values == NULL && node_count >= 4 ? inside & _mm512_cmplt_epu32_mask(conditions, _mm512_set1_epi32((int)(node_count - 3))) : 0;
                const __m512i kinds = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, safe, conditions, stream->value_constants, 1), _mm512_set1_epi32(0xff));
                const __m512i operations = _mm512_mask_i32gather_epi32(zero, inside, conditions, graph->ops, 4);
                const __mmask16 constant = _mm512_cmpneq_epi32_mask(kinds, _mm512_set1_epi32(CHECK_CONSTANT_NONE));
                const __mmask16 query = _mm512_cmpeq_epi32_mask(operations, _mm512_set1_epi32(CHECK_OP_BUILTIN_QUERY));
                for (uint32_t lanes = inside & ((safe & constant) | (__mmask16)~safe | query); lanes != 0; lanes &= lanes - 1)
                {
                    check_program_census_prune(program, pending[i + (uint32_t)__builtin_ctz(lanes)], dead, walk);
                }
            }
            memmove(pending, pending + ready, (pending_count - ready) * sizeof(uint32_t));
            pending_count -= ready;
        }
        uint32_t node = scanned;
        for (; node < node_count; node += 1)
        {
            const CheckOp op = graph->ops[node];
            if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_CALL)
            {
                check_program_census_prune(program, node, dead, walk);
            }
        }
        dead[node_count / 64] &= ~(UINT64_C(1) << (node_count % 64));
    }
    // With `builtins`, the builtin nodes the pass reaches too (check_program_census_builtins), and the automatic compound
    // literals its live leaves name, a mark a plan (check_program_census_compound), for its initializers' list after the
    // leaves (check_program_census_statements).
    valid = valid && (!builtins || check_program_census_builtins(program, declarations, closings, body_count, dead, arms, arm_count, touched, scalar, output, scratch));
    const uint32_t plan_count = builtins && program->initializers != NULL ? program->initializers->count : 0;
    uint8_t* marks = valid && plan_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, plan_count) : NULL;
    valid = valid && (plan_count == 0 || marks != NULL);
    if (marks != NULL)
    {
        memset(marks, 0, plan_count);
    }
    // The leaves that name automatic compound literals, a leaf at most each and a block's 16 stored whole.
    uint32_t* compounds = marks != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)graph->leaf_count + 17) : NULL;
    valid = valid && (marks == NULL || compounds != NULL);
    uint32_t compound_count = 0;
    if (valid)
    {
        memset(starts, 0, ((uint64_t)body_count + 1) * sizeof(uint32_t));
        uint32_t node = 0;
        // 16 nodes at a time with AVX-512: their leaves' objects and flags gathered from the records, their dead marks, and
        // the static bit (CheckInput.declaration_bits, a byte a token and one past) of the objects the flags don't mark; only
        // the leaves of static objects, few, take the scalar step that finds their body. An object within 3 tokens of the
        // end, whose bit a dword gather would read past, takes it too; so does every leaf of a graph whose values are dense.
        const uint32_t count = input->token_count;
        const bool gathered = !BUSTER_SCALAR_PATH(scalar) && graph->dense_leaves == NULL && graph->leaf_count < (UINT32_C(1) << 26) && count >= 4;
        const __m512i zero = _mm512_setzero_si512();
        const uint64_t limit = node_count & ~UINT32_C(15);
        for (node = gathered ? (uint32_t)check_program_census_next(touched, 0, limit) : node; gathered && node < limit;
             node = (uint32_t)check_program_census_next(touched, (uint64_t)node + 16, limit))
        {
            const __m512i slots = _mm512_loadu_si512(graph->leaf_slots + node);
            const __mmask16 leaves = _mm512_cmpneq_epi32_mask(slots, _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
            // Each record's flags and object, adjacent, as one qword: 8 lanes a gather, then the halves apart.
            const __m512i offsets = _mm512_add_epi32(_mm512_mullo_epi32(slots, _mm512_set1_epi32((int)sizeof(CheckGraphLeaf))),
                                                     _mm512_set1_epi32((int)offsetof(CheckGraphLeaf, flags)));
            const __m512i low = _mm512_mask_i32gather_epi64(zero, (__mmask8)leaves, _mm512_castsi512_si256(offsets), graph->leaf_records, 1);
            const __m512i high = _mm512_mask_i32gather_epi64(zero, (__mmask8)(leaves >> 8), _mm512_extracti64x4_epi64(offsets, 1), graph->leaf_records, 1);
            const __m512i flags = _mm512_permutex2var_epi32(low, _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0), high);
            const __m512i objects = _mm512_permutex2var_epi32(low, _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1), high);
            __mmask16 named = leaves & _mm512_cmpneq_epi32_mask(objects, zero) & _mm512_cmplt_epu32_mask(objects, _mm512_set1_epi32((int)program->object_count));
            // The block's 16 dead marks, one aligned piece of a word.
            named &= dead != NULL ? (__mmask16)~(dead[node / 64] >> (node % 64)) : (__mmask16)0xffff;
            const __mmask16 flagged = named & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_CHECK_STATIC_OBJECT));
            const __mmask16 asked = named & (__mmask16)~flagged & _mm512_cmple_epu32_mask(objects, _mm512_set1_epi32((int)(count - 3)));
            const __m512i bits = _mm512_mask_i32gather_epi32(zero, asked, _mm512_sub_epi32(objects, _mm512_set1_epi32(1)), input->declaration_bits, 1);
            const __mmask16 fixed = flagged | (asked & _mm512_test_epi32_mask(bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_STATIC)));
            const __mmask16 near_end = named & (__mmask16)~flagged & (__mmask16)~asked;
            // With `compounds`, the leaves that name automatic compound literals, by the same bits: their nodes compressed into it,
            // for check_program_census_compound after the scan, which calls nothing in this loop.
            const __mmask16 compound = compounds != NULL ? asked & _mm512_cmpeq_epi32_mask(_mm512_and_si512(bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_COMPOUND | BUSTER_CHECK_BITS_STATIC)),
                                                                                          _mm512_set1_epi32(BUSTER_CHECK_BITS_COMPOUND)) :
                                                           (__mmask16)0;
            if (compound != 0)
            {
                _mm512_mask_compressstoreu_epi32(compounds + compound_count, compound,
                                                 _mm512_add_epi32(_mm512_set1_epi32((int)node), _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)));
                compound_count += (uint32_t)__builtin_popcount(compound);
            }
            // Where the static leaves' tokens all lie in one body, the cursor's once moved to the first of them, and no
            // left-out range meets them, their pairs are that body's: two stores of 8, compressed in lane order, as the
            // scalar steps would write them. Otherwise each takes its scalar step.
            const __m512i tokens = _mm512_maskz_loadu_epi32(fixed, graph->tokens + node);
            const uint32_t first_token = _mm512_reduce_min_epu32(_mm512_mask_blend_epi32(fixed, _mm512_set1_epi32(-1), tokens));
            const uint32_t last_token = _mm512_reduce_max_epu32(tokens);
            uint32_t at = b;
            while (fixed != 0 && at + 1 < body_count && declarations[at + 1] <= first_token)
            {
                at += 1;
            }
            while (fixed != 0 && at > 0 && declarations[at] > first_token)
            {
                at -= 1;
            }
            const bool one = fixed != 0 && at < body_count && declarations[at] <= first_token && last_token <= closings[at] && (at + 1 >= body_count || declarations[at + 1] > last_token) &&
                             !check_program_census_meets(arms, arm_count, first_token, last_token);
            if (one)
            {
                const __m512i packed = _mm512_maskz_compress_epi32(fixed, objects);
                const __m512i body = _mm512_set1_epi64((int64_t)((uint64_t)at << 32));
                _mm512_storeu_si512(pairs + pair_count, _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(packed)), body));
                _mm512_storeu_si512(pairs + pair_count + 8, _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(packed, 1)), body));
                pair_count += (uint32_t)__builtin_popcount(fixed);
                starts[at] += (uint32_t)__builtin_popcount(fixed);
                b = at;
            }
            for (uint32_t lanes = (one ? 0 : fixed) | near_end; lanes != 0; lanes &= lanes - 1)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
                // (A near-end leaf's compound literal is its scalar step's; the vector lanes' are above.)
                check_program_census_leaf(program, declarations, closings, body_count, dead, arms, arm_count, node + lane, &b, pairs, &pair_count, starts,
                                          (near_end >> lane & 1) != 0 ? compounds : NULL, &compound_count);
            }
        }
        for (; !BUSTER_SCALAR_PATH(scalar) && node_count - node >= 16; node += 16)
        {
            const __mmask16 leaves = _mm512_cmpneq_epi32_mask(_mm512_loadu_si512(graph->leaf_slots + node), _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
            for (uint32_t lanes = leaves; lanes != 0; lanes &= lanes - 1)
            {
                check_program_census_leaf(program, declarations, closings, body_count, dead, arms, arm_count, node + (uint32_t)__builtin_ctz(lanes), &b, pairs,
                                          &pair_count, starts, compounds, &compound_count);
            }
        }
        for (; node < node_count; node += 1)
        {
            if (graph->leaf_slots[node] != BUSTER_CHECK_NONE)
            {
                check_program_census_leaf(program, declarations, closings, body_count, dead, arms, arm_count, node, &b, pairs, &pair_count, starts, compounds,
                                          &compound_count);
            }
        }
        // The compound literals' plans the kept leaves name (check_program_census_compound), then the statements' lists.
        uint32_t compound_body = 0;
        for (uint32_t k = 0; k < compound_count; k += 1)
        {
            const uint32_t leaf = compounds[k];
            check_program_census_compound(program, declarations, closings, body_count, arms, arm_count, leaf, check_graph_leaf_object(graph, leaf), &compound_body, marks);
        }
        valid = !builtins || check_program_census_statements(program, declarations, closings, body_count, arms, arm_count, marks, output);
        // Each body's objects together, in node order: the counts' exclusive prefix sums, then one scatter.
        uint32_t sum = 0;
        for (uint32_t k = 0; k < body_count; k += 1)
        {
            const uint32_t here = starts[k];
            starts[k] = sum;
            census->first[k] = sum;
            sum += here;
        }
        for (uint32_t p = 0; p < pair_count; p += 1)
        {
            objects[starts[pairs[p] >> 32]++] = (uint32_t)pairs[p];
        }
    }
    uint32_t total = 0;
    for (uint32_t k = 0; valid && k < body_count; k += 1)
    {
        // The body's objects, repeated as often as its leaves name them, and its cleanups' functions; sorted, then each
        // kept once. No object-sized column of marks is read, whose entries are cache misses on a large unit.
        uint32_t found = 0;
        for (uint32_t p = census->first[k]; p < starts[k]; p += 1)
        {
            reference_items[found++] = (uint64_t)objects[p] << 32;
        }
        uint32_t cleanup_first = 0;
        uint32_t cleanup_count = 0;
        check_program_body_cleanups(input, openings[k], closings[k], &cleanup_first, &cleanup_count);
        for (uint32_t r = 0; r < cleanup_count; r += 1)
        {
            const uint32_t function = input->cleanups[cleanup_first + r].function;
            const uint32_t object = function < input->token_count ? input->objects[function] : 0;
            reference_items[found] = (uint64_t)object << 32;
            found += object != 0 && object < program->object_count ? 1 : 0;
        }
        // A body's list is short (six on sqlite3.c's average), where an insertion sort takes far less than a radix sort's
        // histograms; a long one takes the radix sort.
        uint64_t* sorted = reference_items;
        if (found <= 32)
        {
            for (uint32_t r = 1; r < found; r += 1)
            {
                const uint64_t item = reference_items[r];
                uint32_t at = r;
                while (at > 0 && reference_items[at - 1] > item)
                {
                    reference_items[at] = reference_items[at - 1];
                    at -= 1;
                }
                reference_items[at] = item;
            }
        }
        else
        {
            uint32_t varying = 0;
            for (uint32_t r = 1; r < found; r += 1)
            {
                varying |= (uint32_t)((reference_items[0] ^ reference_items[r]) >> 32);
            }
            sorted = c_bindings_sort(reference_items, reference_sort, found, !BUSTER_SCALAR_PATH(scalar), varying);
        }
        uint32_t kept = 0;
        for (uint32_t r = 0; r < found; r += 1)
        {
            const bool fresh = kept == 0 || sorted[kept - 1] != sorted[r];
            sorted[kept] = sorted[r];
            kept += fresh ? 1 : 0;
        }
        valid = check_program_census_store(census, k, sorted, kept, &total, output);
    }
    arena_pop_to(scratch, temporary);
    return valid;
}

// The if keyword whose condition is CHECK_OP_CONDITION node `node` and whose writer could fold it (check_control_fold_truth):
// a condition that is a constant rvalue of an integer type, or a statement expression. An object's lvalue, whose address
// is constant, is no such question. The keyword is the one token before the condition's closing parenthesis (the node's
// token) whose parent the parenthesis is. BUSTER_CHECK_NONE for any other condition.
static uint32_t check_program_constant_if(const CheckProgram* program, const Parser* parser, uint32_t node)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t expression = graph->operand0[node];
    // The value's columns, not the whole value: most constant conditions are lvalues of static objects, which ask no more.
    const bool constant = expression < graph->count && check_stream_constant_kind(program->stream, expression) != CHECK_CONSTANT_NONE;
    const bool rvalue = constant && check_stream_category(program->stream, expression) == CHECK_RVALUE;
    uint32_t width = 0;
    bool sign = false;
    const bool asked = expression < graph->count && ((rvalue && check_action_integer_type(program->types, check_stream_type(program->stream, expression), &width, &sign)) ||
                                                     graph->ops[expression] == CHECK_OP_STATEMENT_VALUE);
    const uint32_t close = graph->tokens[node];
    uint32_t keyword = BUSTER_CHECK_NONE;
    for (uint32_t t = close; asked && keyword == BUSTER_CHECK_NONE && t-- > 0 && close < input->token_count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CONDITION;)
    {
        keyword = parser->tree.parents[t] == close && parser->tree.roles[t] == PARSE_ROLE_LEAF_RIGHT ? t : keyword;
    }
    return keyword < input->token_count && parser->tokens.kinds[keyword] == PARSE_KIND_IF ? keyword : BUSTER_CHECK_NONE;
}

// The left-out tokens of the constant ifs (#2536's step 2.2), as the bodies' controls (check_control_statement's if) leave
// them out, found before any body's controls are built. An if whose condition could fold (check_program_constant_if,
// asked of every CHECK_OP_CONDITION node: a scan of the ops, 16 at a time with AVX-512) is the writer's question, which only
// such a body asks. That body's children and cleanups are set up as its build sets them (check_control_children, which the
// build makes again, and check_program_body_cleanups), then each such if takes the writer's own test
// (check_control_fold_truth, labeled, enterable) and, where it folds, leaves out its condition's tokens and its skipped
// arm's extent. An if the writer never reaches lies in a range it leaves out itself, so these ranges are among the
// writer's (constant switches' aside, which this keeps). Into *found, in `scratch`, sorted and joined where one holds
// another; their count (0 where there's no room). Calls `writer` before its first body, whose label count it leaves as it
// found it.
static uint32_t check_program_constant_arms(CheckControlWriter* writer, const uint32_t* openings, uint32_t body_count, const uint64_t* touched, uint64_t** found,
                                            bool scalar, Arena* scratch)
{
    (void)scalar;

    const Parser* parser = writer->parser;
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t node_count = graph->count;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* parents = parser->tree.parents;
    // The ifs' keys, few, each pushed after the one before (nothing else takes `scratch` meanwhile).
    uint64_t* keys = NULL;
    uint32_t key_count = 0;
    bool room = true;
    uint32_t node = 0;
    // With AVX-512 the conditions' nodes, compressed into `pending` as the scan of the ops finds them, take the cheap part
    // of the test 16 at a time: their expressions, gathered, then each's op and its constant kind and category (byte
    // columns, read as dwords where 3 bytes past are the column's). Only an expression that is a constant rvalue, a
    // statement expression, a dense stream's or one near the column's end takes check_program_constant_if, whose test it is.
    const CheckStream* stream = program->stream;
    uint32_t pending[128 + 16];
    uint32_t pending_count = 0;
    const __m512i lane = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i zero = _mm512_setzero_si512();
    // A gather's index is signed: past 2^31 nodes, the scalar loop takes them all.
    const uint32_t scanned = BUSTER_SCALAR_PATH(scalar) || node_count >= (UINT32_C(1) << 31) ? 0 : node_count & ~UINT32_C(15);
    // The last block, `scanned`, scans nothing and takes what is pending.
    for (uint64_t block = check_program_census_next(touched, 0, scanned); !BUSTER_SCALAR_PATH(scalar) && node_count < (UINT32_C(1) << 31) && block <= scanned;
         block = check_program_census_next(touched, block + 16, scanned))
    {
        const __mmask16 conditions = block < scanned ?
                                         _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(graph->ops + block), _mm512_set1_epi32(CHECK_OP_CONDITION)) :
                                         (__mmask16)0;
        _mm512_mask_compressstoreu_epi32(pending + pending_count, conditions, _mm512_add_epi32(_mm512_set1_epi32((int)block), lane));
        pending_count += (uint32_t)__builtin_popcount(conditions);
        uint32_t ready = pending_count >= 128 ? pending_count & ~UINT32_C(15) : 0;
        ready = block == scanned ? pending_count : ready;
        for (uint32_t i = 0; i < ready; i += 16)
        {
            const __mmask16 present = (__mmask16)(ready - i >= 16 ? 0xffff : (1u << (ready - i)) - 1);
            const __m512i nodes = _mm512_maskz_loadu_epi32(present, pending + i);
            const __m512i expressions = _mm512_mask_i32gather_epi32(zero, present, nodes, graph->operand0, 4);
            const __mmask16 inside = present & _mm512_cmplt_epu32_mask(expressions, _mm512_set1_epi32((int)node_count));
            const __mmask16 safe =
                stream->dense_values == NULL && node_count >= 4 ? inside & _mm512_cmplt_epu32_mask(expressions, _mm512_set1_epi32((int)(node_count - 3))) : 0;
            const __m512i kinds = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, safe, expressions, stream->value_constants, 1), _mm512_set1_epi32(0xff));
            const __m512i categories = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, safe, expressions, stream->value_categories, 1), _mm512_set1_epi32(0xff));
            const __m512i operations = _mm512_mask_i32gather_epi32(zero, inside, expressions, graph->ops, 4);
            const __mmask16 constant = _mm512_cmpneq_epi32_mask(kinds, _mm512_set1_epi32(CHECK_CONSTANT_NONE)) &
                                       _mm512_cmpeq_epi32_mask(categories, _mm512_set1_epi32(CHECK_RVALUE));
            const __mmask16 statement = _mm512_cmpeq_epi32_mask(operations, _mm512_set1_epi32(CHECK_OP_STATEMENT_VALUE));
            const __mmask16 asked = inside & ((safe & constant) | (__mmask16)~safe | statement);
            for (uint32_t lanes = asked; lanes != 0; lanes &= lanes - 1)
            {
                const uint32_t keyword = check_program_constant_if(program, parser, pending[i + (uint32_t)__builtin_ctz(lanes)]);
                uint64_t* key = keyword != BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 1) : NULL;
                room = room && (keyword == BUSTER_CHECK_NONE || key != NULL);
                if (key != NULL)
                {
                    *key = (uint64_t)keyword << 32;
                    keys = keys == NULL ? key : keys;
                    key_count += 1;
                }
            }
        }
        memmove(pending, pending + ready, (pending_count - ready) * sizeof(uint32_t));
        pending_count -= ready;
    }
    node = scanned;
    for (; node < node_count; node += 1)
    {
        const uint32_t keyword = graph->ops[node] == CHECK_OP_CONDITION ? check_program_constant_if(program, parser, node) : BUSTER_CHECK_NONE;
        uint64_t* key = keyword != BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 1) : NULL;
        room = room && (keyword == BUSTER_CHECK_NONE || key != NULL);
        if (key != NULL)
        {
            *key = (uint64_t)keyword << 32;
            keys = keys == NULL ? key : keys;
            key_count += 1;
        }
    }
    // Two ranges an if, and a sort's room for them.
    uint64_t* arms = room && key_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 2 * (uint64_t)key_count) : NULL;
    uint64_t* sort = arms != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 2 * (uint64_t)key_count) : NULL;
    key_count = sort != NULL ? key_count : 0;
    // The ifs in token order, so that each body's set-up is made once.
    uint32_t varying = 0;
    for (uint32_t i = 1; i < key_count; i += 1)
    {
        varying |= (uint32_t)((keys[0] ^ keys[i]) >> 32);
    }
    const uint64_t* ordered = c_bindings_sort(keys, sort, key_count, !BUSTER_SCALAR_PATH(scalar), varying);
    uint32_t arm_count = 0;
    uint32_t b = 0;
    uint32_t prepared = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < key_count; i += 1)
    {
        const uint32_t keyword = (uint32_t)(ordered[i] >> 32);
        const uint32_t close = parents[keyword];
        while (b + 1 < body_count && openings[b + 1] < keyword)
        {
            b += 1;
        }
        const bool inside = b < body_count && openings[b] < keyword && keyword < parents[openings[b]];
        if (inside && prepared != b)
        {
            const uint32_t labels = writer->label_count;
            writer->label_count = 0;
            check_control_children(writer, openings[b], parents[openings[b]]);
            writer->label_count = labels;
            check_program_body_cleanups(input, openings[b], parents[openings[b]], &writer->cleanup_first, &writer->cleanup_count);
            prepared = b;
        }
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        for (uint32_t child = inside ? writer->first[close] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = writer->next[child])
        {
            alternate = child > close && roles[child] == PARSE_ROLE_ELSE ? child : alternate;
            body = child > close && roles[child] != PARSE_ROLE_ELSE ? child : body;
        }
        const uint32_t condition = input->token_nodes[close];
        const uint32_t expression = condition < node_count && graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
        bool truth = false;
        const bool folded = inside && body != BUSTER_CHECK_NONE && check_control_fold_truth(writer, expression, &truth) &&
                            !check_control_labeled(writer, keyword + 1, close) && !check_control_enterable(writer, truth ? alternate : body);
        const uint32_t skipped = truth ? alternate : body;
        if (folded)
        {
            arms[arm_count++] = ((uint64_t)(keyword + 1) << 32) | close;
        }
        if (folded && skipped != BUSTER_CHECK_NONE)
        {
            uint32_t low = 0;
            uint32_t high = 0;
            check_control_extent(writer, skipped, &low, &high);
            arms[arm_count++] = ((uint64_t)low << 32) | high;
        }
    }
    // Sorted by first token, then joined: a range another holds goes, as check_program_census_left_out takes them apart.
    varying = 0;
    for (uint32_t i = 1; i < arm_count; i += 1)
    {
        varying |= (uint32_t)((arms[0] ^ arms[i]) >> 32);
    }
    const uint64_t* sorted = c_bindings_sort(arms, sort, arm_count, !BUSTER_SCALAR_PATH(scalar), varying);
    uint32_t joined = 0;
    for (uint32_t i = 0; i < arm_count; i += 1)
    {
        const uint32_t high = (uint32_t)sorted[i];
        const uint32_t before = joined != 0 ? (uint32_t)arms[joined - 1] : 0;
        const bool held = joined != 0 && (uint32_t)(sorted[i] >> 32) <= before;
        arms[held ? joined - 1 : joined] = held ? (arms[joined - 1] & ~(uint64_t)UINT32_MAX) | (high > before ? high : before) : sorted[i];
        joined += held ? 0 : 1;
    }
    *found = arms;
    return joined;
}

// The state check_program keeps between its parts (#2536's step 2.2c.1): its setup's columns, the writer, the loop's
// place and the tail's inputs, so that its bodies' loop can run a range of bodies at a time.
typedef struct CheckProgramRun CheckProgramRun;
struct CheckProgramRun
{
    CheckProgram* program;
    const Parser* parser;
    const CheckInput* input;
    CheckStream* stream;
    const TypeTable* types;
    CheckProgramOptions options;
    Arena* output;
    Arena* scratch;
    Arena controls;
    CheckControlWriter writer;
    CheckProgramTargetEvents target_events;
    CheckReturnFacts return_facts;
    uint32_t* openings;
    uint32_t* seen;
    uint32_t* nodes;
    uint32_t* stack;
    uint64_t* reference_items;
    uint64_t* reference_sort;
    uint8_t* needed;
    uint32_t* token_first;
    uint32_t* node_next;
    uint32_t* plan_first;
    uint64_t* planned;
    uint32_t* plan_next;
    uint64_t* dropped_ranges;
    CheckControl* entries;
    uint64_t mark;
    uint64_t temporary;
    uint32_t count;
    uint32_t body_count;
    uint32_t next_body;
    uint32_t reference_count;
    bool valid;
    // Whether the tail's part over the needed marks ran before the loop (check_program_symbols), and its answer.
    bool symbols;
    bool symbols_valid;
    // Whether the tail leaves the setup's columns on the scratch (#2536's step 2.2c.4, part 2): under --windowed-loop the
    // emission's code, which ran with the loop, is on the scratch above them until its object is written.
    bool keep_scratch;
};

// check_program's first part (#2536's step 2.2c.1): the program's setup, its columns and the writer, and the candidates'
// pass, kept in `run` for the bodies' loop (check_program_bodies) and the tail (check_program_finish). Whether all went.
static bool check_program_begin(CheckProgramRun* run, CheckProgram* program, const Parser* parser, const CheckInput* input, CheckStream* stream,
                                const TypeTable* types, CheckProgramOptions options, Arena* output, Arena* scratch)
{
    *program = (CheckProgram){ .input = input, .stream = stream, .types = types, .parser = parser, .identifiers = options.identifiers,
                               .initializers = options.initializers, .asms = options.asms, .unselected_operands = options.unselected_operands,
                               .asm_helpers = options.asm_helpers, .asm_bodies = options.asm_bodies, .helper_forms = options.helper_forms, .target = options.target, .frame_pointer = options.frame_pointer,
                               .probe_stack = options.probe_stack, .patchable_count = options.patchable_count, .patchable_offset = options.patchable_offset,
                               .patchable_section = options.patchable_section, .initializer_references = options.initializer_references,
                               .initializer_reference_count = options.initializer_reference_count, .token = BUSTER_CHECK_NONE,
                               .diagnostic_position = BUSTER_CHECK_NONE };
    // The constant CFStrings' calls (#148), which the bodies' references read (check_program_references).
    if (options.prepared_data != NULL)
    {
        program->cfstrings = options.prepared_data->cfstrings;
        program->cfstring_count = options.prepared_data->cfstring_count;
        program->cfstring_class = options.prepared_data->cfstring_class;
        program->cfstring_class_own = options.prepared_data->cfstring_class_own;
    }
    if (options.return_flow_work != NULL)
    {
        *options.return_flow_work = 0;
    }
    if (options.lifetime_work != NULL)
    {
        *options.lifetime_work = 0;
    }
    if (options.region_target_work != NULL)
    {
        *options.region_target_work = 0;
    }
    if (options.statement_walk_work != NULL)
    {
        *options.statement_walk_work = 0;
    }
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = input->token_count;
    const uint32_t node_count = input->graph.count;
    CheckProgramTargetEvents target_events = { .scratch = scratch, .room = true };
    bool valid = output != scratch && count == parser->tokens.count && count < (UINT32_MAX - 16) / 8 &&
                 stream->status == CHECK_STATUS_OK && stream->count == node_count && stream->report_count == 0 && !input->resource &&
                 // A unit whose only reports are recovered ones (#1447) is checked as a valid one; it is never emitted.
                 (input->report_count == 0 || input->recovered_valid) &&
                 !input->visibility_error;
    valid = valid && (options.initializers == 0 || (options.initializers->status == CHECK_INIT_OK && options.initializers->token_count == count)) &&
            (input->asm_count == 0 || (options.asms != 0 && options.asms->status == CHECK_ASM_OK)) &&
            (options.initializer_reference_count == 0 || options.initializer_references != 0) &&
            (options.prepared_data == NULL || (options.prepared_data->status == CHECK_DATA_INPUT_OK &&
             options.prepared_data->object_count == count + 1 &&
             (options.prepared_data->declaration_count == 0 || options.prepared_data->declaration_keys != NULL)));
    program->object_count = count + 1;
    program->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    uint32_t exit_capacity = 0;
    const uint64_t cleanup_room = valid ? check_program_cleanup_room(parser, input, &exit_capacity) : 0;
    // The exits through cleanups' chains, their records' plans and their dispatches' destinations (#1370).
    program->exits = valid && exit_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramExit, exit_capacity) : 0;
    program->chain_stops = valid && exit_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramStop, exit_capacity) : 0;
    program->chains = valid && input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramChain, input->cleanup_count) : 0;
    valid = valid && (exit_capacity == 0 || (program->exits != 0 && program->chain_stops != 0)) && (input->cleanup_count == 0 || program->chains != 0);
    // The blocks' objects' lifetimes, asked for (#177): each block's locals, the room their controls take, and the two
    // columns of the pass that finds the blocks a jump enters, the first of which then keeps the writer's chain of the
    // blocks with lifetimes (#1355, check_program_lifetime_entered).
    const bool lifetimes = valid && options.lifetimes;
    uint32_t* lifetime_first = lifetimes ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* lifetime_next = lifetimes ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* lifetime_nearest = lifetimes ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* lifetime_sources = lifetimes ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    valid = valid && (!lifetimes || (lifetime_first != 0 && lifetime_next != 0 && lifetime_nearest != 0 && lifetime_sources != 0));
    const uint64_t lifetime_room = valid && lifetimes ? check_program_lifetime_blocks(parser, input, lifetime_first, lifetime_next, lifetime_nearest) : 0;
    valid = valid && cleanup_room <= UINT32_MAX && lifetime_room <= UINT32_MAX && cleanup_room + lifetime_room <= UINT32_MAX - ((uint64_t)count * 8 + 16);
    const uint32_t capacity = valid ? (uint32_t)((uint64_t)count * 8 + 16 + cleanup_room + lifetime_room) : 0;
    // The bodies' labels, the worklist of a body's controls and the unit's controls, with room for 8 a token: each body
    // takes the labels and the worklist again from their start, and the controls follow each other. They are in an arena
    // of their own, which check_program unmaps when it returns (#572). In the scratch, their starts and the columns pushed
    // after them took pages past those the reader touches, which the scratch kept to the end of the compile.
    // With the blocks' lifetimes, a label's room more: where a break's or continue's walk to it ends (#1355,
    // CheckControlWriter.lifetime_stops), whose pages only a body's labels touch.
    const uint64_t stop_room = lifetimes ? (uint64_t)capacity * sizeof(uint32_t) + 63 : 0;
    Arena controls = valid ? arena_reserve(((uint64_t)capacity * (sizeof(uint32_t) + sizeof(CheckControl) + sizeof(CheckControlTask))) + 3 * 63 + stop_room) : (Arena){ 0 };
    uint32_t* first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* last = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* previous = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    // The selection census enumerates every token. A VLA can ask for graph nodes at a function opening.
    const bool indexed = input->statement_heads == NULL || input->vla_count != 0 || input->report_count != 0 || options.selection_census;
    uint32_t* token_first = valid && indexed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* node_next = valid && indexed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    // Discarded initializer roots ask for their declarations' storage duration only when no initializer plan covers them.
    uint64_t* static_initializers = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, ((uint64_t)node_count + 64) / 64) : NULL;
    uint32_t* plan_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint64_t* planned = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, ((uint64_t)count + 64) / 64) : 0;
    uint32_t* plan_next = valid && options.initializers != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)options.initializers->count + 1) : 0;
    // The variable-length arrays' columns, the asm statements' and the regions', only for a unit that may have them: each
    // is 4 bytes a token, which its fill touched in every unit (#877).
    const bool variable = valid && check_program_variable(input, types);
    const bool assemblies = valid && input->asm_count != 0;
    const bool regions = valid && input->region_count != 0;
    const bool all_loop_targets = variable || lifetimes || input->cleanup_count != 0;
    const bool loop_targets = all_loop_targets || regions;
    uint32_t* vla_first = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->vla_count + 1) : 0;
    uint32_t* allocation_first = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* allocation_next = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_blocks = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_saves = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* asm_statements = assemblies ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_map = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* source_labels = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* stops = valid && loop_targets ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* repeats = valid && loop_targets ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* region_first = regions ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* region_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->region_count + 1) : 0;
    // Each body's opening brace, in token order: at most one per token, and only the bodies' entries are touched.
    uint32_t* openings = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* labels = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, uint32_t, capacity) : 0;
    uint32_t* lifetime_stops = valid && lifetimes ? BUSTER_ARENA_PUSH_ARRAY(&controls, uint32_t, capacity) : 0;
    uint32_t* label_positions = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    // The label scan a constant condition's dead operand can need nests inside the run's traversal, on marks of its
    // own: each set of pushes is bounded by node_count. Only a unit with a statement expression can need it, so its
    // extra room is the regions' alone (#891).
    uint32_t* stack = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count * (regions ? 2 : 1) + 2) : 0;
    uint32_t* nodes = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* seen = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    // A body's references: its nodes', and its cleanups' functions.
    uint64_t* reference_items = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + input->cleanup_count + 1) : 0;
    uint64_t* reference_sort = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + input->cleanup_count + 1) : 0;
    uint32_t* cleanup_parents = valid && input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : 0;
    uint32_t* cleanup_tops = valid && input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : 0;
    uint32_t* cleanup_scope_last = valid && input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    if (cleanup_scope_last != 0)
    {
        memset(cleanup_scope_last, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    }
    uint8_t* needed = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* addressed = valid && input->label_address_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    CheckControl* entries = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, CheckControl, capacity) : 0;
    CheckControlTask* tasks = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, CheckControlTask, capacity) : 0;
    valid = valid && first != 0 && last != 0 && next != 0 && previous != 0 && (!indexed || (token_first != NULL && node_next != NULL)) && static_initializers != NULL && labels != 0 &&
            stack != 0 && nodes != 0 && seen != 0 && needed != 0 && (input->label_address_count == 0 || addressed != 0) && entries != 0 && tasks != 0 && plan_first != 0 && planned != 0 &&
            (options.initializers == 0 || plan_next != 0) &&
            case_first != 0 && case_next != 0 && case_map != 0 && source_labels != 0 && label_positions != 0 && reference_items != 0 && reference_sort != 0 &&
            (!loop_targets || (stops != NULL && repeats != NULL)) && (!regions || region_first != 0) && region_next != 0 && openings != 0 && vla_next != 0 && (!assemblies || asm_statements != 0) &&
            (!lifetimes || lifetime_stops != 0) &&
            (!variable || (vla_first != 0 && allocation_first != 0 && allocation_next != 0 && vla_blocks != 0 && vla_saves != 0)) &&
            (input->cleanup_count == 0 || (cleanup_parents != 0 && cleanup_tops != 0 && cleanup_scope_last != 0));
    if (!valid && program->status == CHECK_PROGRAM_OK)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    // The scratch's pages past these columns go back to the kernel (arena_discard, #628): in the C driver they are the
    // parse's and the reader's, which touched 77 MB of sqlite3.c's scratch against these columns' 51, and which this
    // pass and the backend would otherwise hold while their outputs take pages of their own, through the compile's
    // peak. A lane of several units that has another to compile gives them back in whole huge pages
    // (CheckProgramOptions.discard_huge, #777): the huge page a discard frees part of is split, and its next unit's
    // parse and reader fault the part in again a base page at a time, as each later unit does.
    if (valid && options.discard_huge)
    {
        arena_discard_past(scratch, scratch->length + options.discard_keep, true);
    }
    else if (valid)
    {
        arena_discard(scratch);
    }
    // The ranges the writer leaves out (CheckProgram.dropped_ranges), a token's room each, and per region whether one
    // holds it, for a unit with regions. Past the discard: the writer fills a prefix of the ranges, a few kilobytes,
    // where room for a token's each before it kept 8 bytes a token of the scratch's earlier pages through the backend's
    // peak (sqlite3.c's 5.7 MB, #628).
    uint64_t* dropped_ranges = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    uint8_t* dropped_regions = regions ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, input->region_count) : 0;
    // The tokens' region targets, for a unit with regions (check_control_region_targets): a body's walks write the tokens
    // from its regions up, and clear the body's bits alone, so past the discard the rest of the columns' pages stay
    // untouched (#2995).
    uint32_t* region_stops = regions ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* region_repeats = regions ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint64_t* region_known = regions ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, ((uint64_t)count + 64) / 64) : 0;
    valid = valid && dropped_ranges != 0 && (!regions || (dropped_regions != 0 && region_stops != 0 && region_repeats != 0 && region_known != 0));
    if (!valid && program->status == CHECK_PROGRAM_OK)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    uint32_t body_count = 0;
    if (valid)
    {
        check_program_cases(program, parser, case_first, case_next, case_map, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
        // The children lists, source labels, stops and repeats are each body's, which check_control_begin fills.
        if (indexed)
        {
            memset(token_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        }
        memset(planned, 0, ((uint64_t)count + 64) / 64 * sizeof(uint64_t));
        if (variable)
        {
            memset(vla_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(allocation_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(vla_blocks, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(vla_saves, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        }
        if (assemblies)
        {
            memset(asm_statements, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            for (uint32_t i = 0; i < input->asm_count; i += 1)
            {
                const CheckInputAsm* assembly = input->asm_statements + i;
                if (assembly->body != BUSTER_CHECK_NONE && assembly->statement < count)
                {
                    asm_statements[assembly->statement] = i;
                }
            }
        }
        memset(needed, 0, (uint64_t)count + 1);
        if (addressed != 0)
        {
            check_program_addressed(parser, input, addressed, BUSTER_SCALAR_PATH(options.scalar));
        }
        if (lifetime_first != 0)
        {
            const uint64_t lifetime_work = check_program_lifetime_entered(program, parser, addressed, lifetime_nearest, lifetime_sources, lifetime_first);
            if (options.lifetime_work != NULL)
            {
                *options.lifetime_work = lifetime_work;
            }
            program->lifetime_first = lifetime_first;
            program->lifetime_next = lifetime_next;
        }
        memset(nodes, 0, ((uint64_t)node_count + 1) * sizeof(uint32_t));
        if (options.return_flow_work != NULL)
        {
            *options.return_flow_work += (uint64_t)node_count + 1;
        }
        if (regions)
        {
            memset(region_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(dropped_regions, 0, input->region_count);
        }
        for (uint32_t r = input->region_count; r-- > 0;)
        {
            const uint32_t body = input->regions[r].body;
            if (body >= count)
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, input->regions[r].opening);
            }
            else
            {
                region_next[r] = region_first[body];
                region_first[body] = r;
            }
        }
        body_count = check_program_openings(input, openings, BUSTER_SCALAR_PATH(options.scalar));
        // A cpu_dispatch function's body, which clang ignores (#1447), is no body of the program's.
        uint32_t kept = 0;
        for (uint32_t b = 0; input->cpu_dispatch && b < body_count; b += 1)
        {
            const uint32_t declaration = check_sparse_get(input->body_declarations, openings[b]);
            openings[kept] = openings[b];
            kept += check_cpu_dispatch_kind(check_input_attributes_at(input, declaration)) != CHECK_CPU_DISPATCH_DISPATCH;
        }
        body_count = input->cpu_dispatch ? kept : body_count;
        // Group ordinary tokens in postorder. A marked root keeps its own graph token and occupies its
        // semicolon's slot in the control list. Its source end reaches that semicolon past at most eight closers.
        uint32_t end = indexed ? node_count : 0;
        while (input->discard_roots != NULL && end != 0)
        {
            const uint32_t first = (end - 1) & ~UINT32_C(63);
            uint64_t marks = input->discard_roots[first / 64];
            while (marks != 0)
            {
                const uint32_t marked = first + 63 - (uint32_t)__builtin_clzll(marks);
                while (end > marked + 1)
                {
                    const uint32_t node = --end;
                    const uint32_t token = input->graph.tokens[node];
                    node_next[node] = token_first[token];
                    token_first[token] = node;
                }
                uint32_t token = input->graph.source_positions[marked];
                while (token < count && parser->tokens.kinds[token] == PARSE_KIND_R_PAREN)
                {
                    token += 1;
                }
                node_next[marked] = token_first[token];
                token_first[token] = marked;
                end = marked;
                marks ^= UINT64_C(1) << (marked % 64);
            }
            while (end > first)
            {
                const uint32_t node = --end;
                const uint32_t token = input->graph.tokens[node];
                node_next[node] = token_first[token];
                token_first[token] = node;
            }
        }
        while (end != 0)
        {
            const uint32_t node = --end;
            const uint32_t token = input->graph.tokens[node];
            node_next[node] = token_first[token];
            token_first[token] = node;
        }
        const bool compound_initializers = check_program_plans(program, parser, options.initializers, plan_first, planned, plan_next, seen);
        memset(seen, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        if (compound_initializers && program->status == CHECK_PROGRAM_OK)
        {
            program->compound_initializers = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, options.initializers->count);
            if (program->compound_initializers == NULL)
            {
                check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
            }
            else
            {
                memset(program->compound_initializers, 0, options.initializers->count);
            }
        }
        // The walks from the variable-length arrays' objects and bounds up to their statements keep their answers
        // (check_program_statement), in the scratch until the bounds are listed: the bits a token, the rest where a walk
        // passes (#2995).
        const uint64_t statements_mark = scratch->length;
        uint32_t* statement_ends = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
        uint32_t* statement_keys = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
        uint64_t* statement_known = variable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, ((uint64_t)count + 64) / 64) : 0;
        const CheckProgramStatements statements = {
            .ends = statement_ends,
            .keys = statement_keys,
            .known = statement_known,
            .work = options.statement_walk_work,
        };
        const bool walks = statements.ends != 0 && statements.keys != 0 && statements.known != 0;
        if (walks)
        {
            memset(statements.known, 0, ((uint64_t)count + 64) / 64 * sizeof(uint64_t));
        }
        else if (variable)
        {
            check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
        }
        if (walks)
        {
            check_program_allocations(program, parser, types, allocation_first, allocation_next, vla_blocks, vla_saves, &statements, BUSTER_SCALAR_PATH(options.scalar));
        }
        for (uint32_t i = walks ? input->vla_count : 0; i-- > 0;)
        {
            const CheckInputVla* vla = input->vlas + i;
            if (vla->evaluated && vla->declaration != BUSTER_CHECK_NONE)
            {
                const uint32_t token = check_program_statement(parser, count, vla->scope_begin == vla->body ? vla->body : vla->declaration, vla->body, &statements);
                if (token < count && vla->bound_node < node_count && input->graph.ops[vla->bound_node] == CHECK_OP_VLA_BOUND)
                {
                    vla_next[i] = vla_first[token];
                    vla_first[token] = i;
                    // The block's stack position is saved after its first array's own bounds, which may jump out of
                    // it (from a statement expression) before then.
                    const uint32_t block = parser->tree.parents[token];
                    if (block < count && vla_blocks[block] == vla->declaration + 1 && vla->bound_end >= vla_saves[block])
                    {
                        vla_saves[block] = vla->bound_end + 1;
                    }
                }
                else
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, vla->bound_begin);
                }
            }
        }
        arena_pop_to(scratch, statements_mark);
    }
    program->bodies = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramBody, body_count) : 0;
    const uint32_t switch_capacity = program->case_count / 4;
    program->switches = valid && switch_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, switch_capacity) : 0;
    program->regions = valid && input->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramRegion, input->region_count) : 0;
    program->region_reached = valid && input->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, input->region_count) : 0;
    program->region_count = input->region_count;
    program->object_symbols = valid ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, (uint64_t)count + 1) : 0;
    program->object_declarations = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    program->asm_offsets = valid && input->asm_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)input->asm_count + 1) : 0;
    uint64_t asm_targets = 0;
    for (uint32_t i = 0; valid && i < input->asm_count; i += 1)
    {
        if (program->asm_offsets != 0 && asm_targets <= UINT32_MAX)
        {
            program->asm_offsets[i] = (uint32_t)asm_targets;
            asm_targets += input->asm_statements[i].label_count;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && asm_targets <= UINT32_MAX;
    if (valid && input->asm_count != 0)
    {
        program->asm_offsets[input->asm_count] = (uint32_t)asm_targets;
    }
    program->asm_target_count = valid ? (uint32_t)asm_targets : 0;
    program->asm_targets = valid && asm_targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, asm_targets) : 0;
    valid = valid && (body_count == 0 || program->bodies != 0) && (switch_capacity == 0 || program->switches != 0) &&
            (input->region_count == 0 || (program->regions != 0 && program->region_reached != 0)) && program->object_symbols != 0 &&
            program->object_declarations != 0 && (asm_targets == 0 || program->asm_targets != 0);
    // Each body's own fields (#2536's step 2.2c.2): its declaration, opening brace, object and whether it is main, which
    // the tail's part over the needed marks reads (check_program_symbols), so that it can run before the loop, which
    // writes the rest. The bodies are all the program's from here on.
    for (uint32_t b = 0; valid && b < body_count; b += 1)
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, openings[b]);
        program->bodies[b] = (CheckProgramBody){
            .declaration = declaration,
            .opening = openings[b],
            .object = input->objects[declaration],
        };
        if (options.identifiers != 0)
        {
            const uint32_t name = parser->tokens.names[declaration];
            program->bodies[b].is_main = intern_length(options.identifiers, name) == 4 && memcmp(intern_bytes(options.identifiers, name), "main", 4) == 0;
        }
    }
    program->body_count = valid ? body_count : 0;
    // An asm statement in an arm the writer leaves out has no entry, which fills its targets: none. No region is reached
    // before the bodies' references are walked.
    if (valid && asm_targets != 0)
    {
        memset(program->asm_targets, 0xff, asm_targets * sizeof(uint32_t));
    }
    if (valid && input->region_count != 0)
    {
        memset(program->region_reached, 0, input->region_count);
    }
    if (!valid && program->status == CHECK_PROGRAM_OK)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    CheckControlWriter writer = {
        .program = program,
        .parser = parser,
        .first = first,
        .last = last,
        .next = next,
        .previous = previous,
        .token_first = token_first,
        .node_next = node_next,
        .static_initializers = static_initializers,
        .plan_first = plan_first,
        .planned = planned,
        .plan_next = plan_next,
        .variable = variable,
        .vla_first = vla_first,
        .vla_next = vla_next,
        .allocation_first = allocation_first,
        .allocation_next = allocation_next,
        .vla_blocks = vla_blocks,
        .vla_saves = vla_saves,
        .asm_statements = asm_statements,
        .case_first = case_first,
        .case_next = case_next,
        .case_map = case_map,
        .source_labels = source_labels,
        .all_loop_targets = all_loop_targets,
        .scalar_children = options.scalar,
        .stops = stops,
        .repeats = repeats,
        .region_first = region_first,
        .region_next = region_next,
        .region_stops = region_stops,
        .region_repeats = region_repeats,
        .region_known = region_known,
        .region_work = options.region_target_work,
        .region_final = BUSTER_CHECK_NONE,
        .addressed = addressed,
        .cleanup_parents = cleanup_parents,
        .cleanup_tops = cleanup_tops,
        .cleanup_scope_last = cleanup_scope_last,
        .exits = program->exits,
        .exit_capacity = exit_capacity,
        .entries = entries,
        .tasks = tasks,
        .labels = labels,
        .label_positions = label_positions,
        .switches = program->switches,
        .switch_capacity = switch_capacity,
        .dropped_ranges = dropped_ranges,
        .dropped_regions = dropped_regions,
        .switch_scratch = scratch,
        .fold_stack = stack,
        .fold_capacity = node_count + 1,
        .capacity = capacity,
        .lifetime_nearest = program->lifetime_first != NULL ? lifetime_nearest : NULL,
        .lifetime_stops = program->lifetime_first != NULL ? lifetime_stops : NULL,
    };
    valid = valid && program->status == CHECK_PROGRAM_OK;
    // The labelled library declarations, which a computed call's test reads in the references' pass below.
    valid = valid && check_program_labelled_objects(program, output);
    // The candidates as the pass before the bodies' controls finds them (#2536's step 2.2, census_pass): the constant ifs'
    // left-out tokens (check_program_constant_arms), then the pruned scan without them, timed together.
    if (valid && (options.selection_census || options.candidates) && program->status == CHECK_PROGRAM_OK)
    {
        const uint64_t pass_start = __rdtsc();
        const uint64_t mark = scratch->length;
        const bool scanned = stream->overload_count == 0 && stream->overload_address_count == 0 && program->cfstring_count == 0;
        // The graph's blocks that hold a body's node (check_program_census_touched), which both scans keep to with AVX-512.
        // Only where the bodies' tokens are at most three quarters of the unit's: the marking reads every node's token,
        // which a unit of bodies (sqlite3.c's are 98.6% of its nodes) pays without a node to skip.
        const bool blocked = scanned && !BUSTER_SCALAR_PATH(options.scalar) && body_count != 0;
        uint32_t* bounds = blocked ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * (uint64_t)body_count) : NULL;
        valid = valid && (!blocked || bounds != NULL);
        uint64_t covered = 0;
        for (uint32_t k = 0; valid && blocked && k < body_count; k += 1)
        {
            bounds[k] = check_sparse_get(input->body_declarations, openings[k]);
            bounds[body_count + k] = parser->tree.parents[openings[k]];
            covered += bounds[body_count + k] >= bounds[k] ? (uint64_t)bounds[body_count + k] - bounds[k] + 1 : 0;
        }
        const bool sparse = valid && blocked && covered * 4 <= (uint64_t)input->token_count * 3;
        uint64_t* touched = sparse ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count / 4096 + 1) : NULL;
        valid = valid && (!sparse || touched != NULL);
        if (valid && sparse)
        {
            check_program_census_touched(program, bounds, bounds + body_count, body_count, touched);
        }
        uint64_t* arms = NULL;
        const uint32_t arm_count = valid && scanned ? check_program_constant_arms(&writer, openings, body_count, touched, &arms, options.scalar, scratch) : 0;
        valid = valid && (!scanned || check_program_census_scan(program, &program->census_pass, parser, openings, body_count, arms, arm_count, true, true, touched,
                                                                reference_items, reference_sort, options.scalar, output, scratch));
        program->census_pass.ticks = scanned ? __rdtsc() - pass_start : 0;
        program->census_pass_arms = arm_count;
        // Kept for a selection before the loop (check_driver_provisional_select) and the census's comparison with the
        // writer's.
        uint64_t* ranges = valid && arm_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, arm_count) : NULL;
        valid = valid && (arm_count == 0 || ranges != NULL);
        if (ranges != NULL)
        {
            memcpy(ranges, arms, (uint64_t)arm_count * sizeof(uint64_t));
        }
        program->census_pass_ranges = ranges;
        arena_pop_to(scratch, mark);
    }
    *run = (CheckProgramRun){
        .program = program,
        .parser = parser,
        .input = input,
        .stream = stream,
        .types = types,
        .options = options,
        .output = output,
        .scratch = scratch,
        .controls = controls,
        .writer = writer,
        .target_events = target_events,
        .return_facts = { .work = options.return_flow_work },
        .openings = openings,
        .seen = seen,
        .nodes = nodes,
        .stack = stack,
        .reference_items = reference_items,
        .reference_sort = reference_sort,
        .needed = needed,
        .token_first = token_first,
        .node_next = node_next,
        .plan_first = plan_first,
        .planned = planned,
        .plan_next = plan_next,
        .dropped_ranges = dropped_ranges,
        .entries = entries,
        .mark = mark,
        .temporary = temporary,
        .count = count,
        .body_count = body_count,
        .valid = valid,
    };
    // The rooms the loop writes in, for a lowering that reads no control total (CheckProgram.control_capacity), from the
    // run's writer, after the setup's passes.
    program->control_capacity = run->writer.capacity;
    program->chain_stop_capacity = run->writer.exit_capacity;
    return valid;
}

// check_program's loop (#2536's step 2.2c.1): the bodies from the run's next to `end`, each's controls, return flow,
// chains and references, in token order, as the loop over them all would make them.
static void check_program_bodies(CheckProgramRun* run, uint32_t end)
{
    CheckProgram* program = run->program;
    const CheckProgramOptions* options = &run->options;
    Arena* output = run->output;
    Arena* scratch = run->scratch;
    const uint32_t* openings = run->openings;
    uint32_t* seen = run->seen;
    uint32_t* nodes = run->nodes;
    uint32_t* stack = run->stack;
    uint64_t* reference_items = run->reference_items;
    uint64_t* reference_sort = run->reference_sort;
    uint8_t* needed = run->needed;
    CheckControl* entries = run->entries;
    const bool valid = run->valid;
    const uint32_t last = end < run->body_count ? end : run->body_count;
    CheckControlWriter* writer = &run->writer;
    uint32_t reference_count = run->reference_count;
    for (uint32_t b = run->next_body; valid && b < last && program->status == CHECK_PROGRAM_OK; b += 1)
    {
        const uint32_t token = openings[b];
        const uint32_t start = writer->count;
        const uint32_t label_first = writer->label_position_count;
        const uint32_t exit_start = writer->exit_count;
        writer->body = b;
        // SCAFFOLD (c_check_boundary.c): a supplied body has no controls; its references are the recording's, which the
        // symbols' numbering and the selection read as they read a checked body's.
        const CheckBoundaryBody* taken = check_boundary_taken(token);
        if (__builtin_expect(taken != NULL, 0))
        {
            CheckProgramBody* supplied = &program->bodies[b];
            supplied->first = start;
            supplied->count = 0;
            supplied->entry_count = 0;
            supplied->reference_first = reference_count;
            supplied->reference_count = taken->reference_count;
            supplied->label_first = label_first;
            supplied->label_count = 0;
            supplied->cleanup_first = writer->cleanup_first;
            supplied->cleanup_count = 0;
            uint32_t* supplied_references = taken->reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, taken->reference_count) : 0;
            if (taken->reference_count != 0 && supplied_references == 0)
            {
                check_program_fail(program, CHECK_PROGRAM_RESOURCE, token);
            }
            else
            {
                if (program->references == 0)
                {
                    program->references = supplied_references;
                }
                for (uint32_t r = 0; r < taken->reference_count; r += 1)
                {
                    const uint32_t object = taken->references[r];
                    supplied_references[r] = object;
                    if (object != 0 && object <= run->count)
                    {
                        needed[object] = 1;
                    }
                    else
                    {
                        check_program_fail(program, CHECK_PROGRAM_INVALID, token);
                    }
                }
                reference_count += taken->reference_count;
            }
            continue;
        }
        check_control_build(writer, token);
        // Its source labels' places, from their numbers, before check_program_return_flow takes the writer's labels.
        for (uint32_t l = label_first; l < writer->label_position_count; l += 1)
        {
            const uint32_t label = writer->label_positions[l];
            writer->label_positions[l] = label < writer->label_count ? writer->labels[label] : BUSTER_CHECK_NONE;
        }
        // Its own fields the setup wrote (check_program_begin); the controls' and the references' here.
        CheckProgramBody* body = &program->bodies[b];
        body->first = start;
        body->count = writer->count - start;
        body->entry_count = writer->entry_count;
        body->reference_first = reference_count;
        body->label_first = label_first;
        body->label_count = writer->label_position_count - label_first;
        body->cleanup_first = writer->cleanup_first;
        body->cleanup_count = writer->cleanup_count;
        check_program_return_flow(writer, body, &run->return_facts, options->return_warnings, options->noreturn_warnings);
        program->exit_count = writer->exit_count;
        if (writer->exit_count != exit_start && program->status == CHECK_PROGRAM_OK)
        {
            check_program_chains(writer, body, exit_start, scratch);
        }
        const uint64_t references_start = options->selection_census ? __rdtsc() : 0;
        body->reference_count = check_program_references(program, entries + start, body->entry_count, b + 1, seen, nodes, stack, reference_items, needed);
        if (body->reference_count > UINT32_MAX - reference_count)
        {
            check_program_fail(program, CHECK_PROGRAM_RESOURCE, token);
        }
        else
        {
            uint32_t varying = 0;
            for (uint32_t r = 1; r < body->reference_count; r += 1)
            {
                varying |= (uint32_t)((reference_items[0] ^ reference_items[r]) >> 32);
            }
            // The existing bounded radix kernel sorts high 32-bit keys. Reuse its scalar/vector twins, keeping
            // this body's canonical object IDs compact instead of rescanning the entire translation unit.
            uint64_t* sorted = c_bindings_sort(reference_items, reference_sort, body->reference_count, !BUSTER_SCALAR_PATH(options->scalar), varying);
            uint32_t* output_references = body->reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, body->reference_count) : 0;
            if (body->reference_count != 0 && output_references == 0)
            {
                check_program_fail(program, CHECK_PROGRAM_RESOURCE, token);
            }
            else
            {
                if (program->references == 0)
                {
                    program->references = output_references;
                }
                for (uint32_t r = 0; r < body->reference_count; r += 1)
                {
                    output_references[r] = (uint32_t)(sorted[r] >> 32);
                }
            }
            reference_count += body->reference_count;
            program->census_references_ticks += options->selection_census ? __rdtsc() - references_start : 0;
        }
    }
    run->reference_count = reference_count;
    run->next_body = last > run->next_body ? last : run->next_body;
}

// check_program's tail (#2536's step 2.2c.1): after the bodies' loop, the builtins, the censuses, the controls' copy, the
// static objects, aliases and symbols, the targets' checks, and the failure's or the success's last word. Whether the
// program is valid.
// The tail's part over the needed marks (#2536's step 2.2c.2): the static objects, the roots, the groups, the aliases and
// asm labels, the symbols' numbering, the late weak declarations, the targets and the attributes' and patchable entries'
// keys, given `valid`. Of the bodies it reads their own fields alone (CheckProgramBody's declaration and object), which
// the setup writes, and it neither reads nor writes the loop's columns: it can run before the loop, as
// --provisional-selection runs it over the candidates' marks. Whether all went.
static bool check_program_symbols(CheckProgramRun* run, uint8_t* needed, bool valid)
{
    CheckProgram* program = run->program;
    CheckProgramOptions options = run->options;
    Arena* output = run->output;
    Arena* scratch = run->scratch;
    CheckProgramTargetEvents* events = &run->target_events;
    const uint32_t count = run->count;
    if (valid)
    {
        memset(program->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
        memset(program->object_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        // The naked functions' first declarations with the attribute, in token order (CheckProgram.object_naked): the
        // column comes with the unit's first one. And the functions' target attributes, which check_program_targets reads.
        const bool naked_room = options.prepared_data != NULL ? check_program_static_prepared(program, options.prepared_data, needed, output, events) :
                                                              check_program_static_objects(program, needed, output, BUSTER_SCALAR_PATH(options.scalar), events);
        // The constant CFStrings' records' references, the last ones, are no roots (#148): a live record's are needed
        // (check_program_alias_number).
        const CheckDataPrepared* prepared = options.prepared_data;
        const uint32_t roots = prepared != NULL && options.initializer_references == prepared->references &&
                               prepared->cfstring_reference_first < options.initializer_reference_count ? prepared->cfstring_reference_first :
                                                                                                          options.initializer_reference_count;
        for (uint32_t i = 0; i < roots; i += 1)
        {
            const uint32_t object = options.initializer_references[i];
            if (object != 0 && object <= count)
            {
                needed[object] = 1;
            }
            else if (object != 0)
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, 0);
            }
        }
        // clang's cpu_specific and cpu_dispatch groups (#1447), whose members' references take their groups' symbols as
        // the symbols are numbered.
        const bool grouped = (!program->input->cpu_dispatch || check_program_cpu_groups(program, events, output)) &&
                             (!program->input->target_multiversion ||
                              check_program_target_groups(program, events, options.initializer_references, options.initializer_reference_count, output));
        // GNU's alias, ifunc and weakref attributes (#147): the unit's entries, whose aliases' and ifuncs' objects are
        // needed, and whose referenced weakrefs take their targets' symbols as the symbols are numbered.
        const bool aliased = grouped && (events->alias_count == 0 ||
                                         check_program_aliases(program, events, needed, BUSTER_SCALAR_PATH(options.scalar), output, scratch));
        // Asm labels naming another object's symbol (#1979), whose objects take that symbol as the symbols are numbered.
        const bool labelled = aliased && (events->label_count == 0 || check_program_label_merges(program, events, BUSTER_SCALAR_PATH(options.scalar), output, scratch));
        valid = naked_room && labelled && check_program_alias_number(program, needed, program->alias_states, BUSTER_SCALAR_PATH(options.scalar), output);
    }
    valid = valid && (events->weak_count == 0 || check_program_weak_late(program, events, BUSTER_SCALAR_PATH(options.scalar), output, scratch));
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_targets(program, &options, events, output, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_attribute_keys(program, events, BUSTER_SCALAR_PATH(options.scalar), output, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_patchable_keys(program, events, BUSTER_SCALAR_PATH(options.scalar), output, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    return valid;
}

static bool check_program_finish(CheckProgramRun* run)
{
    CheckProgram* program = run->program;
    const Parser* parser = run->parser;
    const CheckInput* input = run->input;
    CheckStream* stream = run->stream;
    CheckProgramOptions options = run->options;
    Arena* output = run->output;
    Arena* scratch = run->scratch;
    Arena controls = run->controls;
    const uint32_t* openings = run->openings;
    uint32_t* seen = run->seen;
    uint32_t* nodes = run->nodes;
    uint32_t* stack = run->stack;
    uint64_t* reference_items = run->reference_items;
    uint64_t* reference_sort = run->reference_sort;
    uint8_t* needed = run->needed;
    uint32_t* token_first = run->token_first;
    uint32_t* node_next = run->node_next;
    uint32_t* plan_first = run->plan_first;
    uint64_t* planned = run->planned;
    uint32_t* plan_next = run->plan_next;
    uint64_t* dropped_ranges = run->dropped_ranges;
    CheckControl* entries = run->entries;
    const uint64_t mark = run->mark;
    const uint64_t temporary = run->temporary;
    const uint32_t count = run->count;
    const uint32_t reference_count = run->reference_count;
    const CheckControlWriter writer = run->writer;
    if (options.lifetime_work != NULL)
    {
        *options.lifetime_work += writer.lifetime_steps;
    }
    bool valid = run->valid && program->status == CHECK_PROGRAM_OK;
    program->builtin_count = valid ? check_program_builtins(&input->graph, nodes, NULL, BUSTER_SCALAR_PATH(options.scalar)) : 0;
    program->builtin_nodes = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->builtin_count) : 0;
    valid = valid && (program->builtin_count == 0 || program->builtin_nodes != 0);
    if (valid && program->builtin_count != 0)
    {
        check_program_builtins(&input->graph, nodes, program->builtin_nodes, BUSTER_SCALAR_PATH(options.scalar));
    }
    // The selection census (#2536's step 2.1), past the builtins' count, which reads the references' marks.
    if (valid && options.selection_census && program->status == CHECK_PROGRAM_OK)
    {
        const uint64_t walk_start = __rdtsc();
        valid = check_program_census_walk(program, parser, token_first, node_next, plan_first, planned, plan_next, seen, nodes, stack, reference_items, reference_sort,
                                          options.scalar, output, scratch);
        program->census_walk.ticks = __rdtsc() - walk_start;
        const uint64_t scan_start = __rdtsc();
        const bool scanned = stream->overload_count == 0 && stream->overload_address_count == 0 && program->cfstring_count == 0;
        valid = valid && (!scanned || check_program_census_scan(program, &program->census_scan, parser, openings, program->body_count, NULL, 0, false, false, NULL,
                                                                reference_items, reference_sort, options.scalar, output, scratch));
        program->census_scan.ticks = scanned ? __rdtsc() - scan_start : 0;
        // The scan without the writer's dropped arms and conditions, sorted, and the operands the walk leaves out: the
        // candidates of a pass before the windows that finds every constant if and switch the writer does. Its ticks don't
        // count the finding.
        const uint32_t arm_count = writer.dropped_range_count;
        uint64_t* arm_keys = valid && arm_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 2 * (uint64_t)arm_count) : NULL;
        valid = valid && (arm_count == 0 || arm_keys != NULL);
        const uint64_t* arms = NULL;
        if (valid && arm_count != 0)
        {
            uint32_t varying = 0;
            memcpy(arm_keys, writer.dropped_ranges, arm_count * sizeof(uint64_t));
            for (uint32_t i = 1; i < arm_count; i += 1)
            {
                varying |= (uint32_t)((arm_keys[0] ^ arm_keys[i]) >> 32);
            }
            arms = c_bindings_sort(arm_keys, arm_keys + arm_count, arm_count, !BUSTER_SCALAR_PATH(options.scalar), varying);
        }
        const uint64_t arms_start = __rdtsc();
        valid = valid && (!scanned || check_program_census_scan(program, &program->census_arms, parser, openings, program->body_count, arms, arm_count, true, false, NULL,
                                                                reference_items, reference_sort, options.scalar, output, scratch));
        program->census_arms.ticks = scanned ? __rdtsc() - arms_start : 0;
        CheckProgramCensus* censuses[] = { &program->census_walk, &program->census_scan, &program->census_arms, &program->census_pass };
        for (uint32_t c = 0; valid && c < BUSTER_ARRAY_LENGTH(censuses); c += 1)
        {
            check_program_census_compare(program, parser, censuses[c], writer.dropped_ranges, writer.dropped_range_count);
        }
        for (uint32_t b = 0; valid && b < program->body_count; b += 1)
        {
            program->census_dropped_bodies += check_program_census_armed(program, parser, b, writer.dropped_ranges, writer.dropped_range_count) ? 1 : 0;
        }
    }
    program->controls = valid && writer.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckControl, writer.count) : 0;
    program->label_positions = valid && writer.label_position_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, writer.label_position_count) : 0;
    // The dropped arms in token order: they are apart, so their first tokens, the keys' high halves, order them.
    const uint32_t dropped_count = writer.dropped_range_count;
    uint64_t* dropped_sort = valid && dropped_count > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, dropped_count) : 0;
    program->dropped_ranges = valid && dropped_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, dropped_count) : 0;
    valid = valid && (writer.count == 0 || program->controls != 0) && (writer.label_position_count == 0 || program->label_positions != 0) &&
            (dropped_count <= 1 || dropped_sort != 0) && (dropped_count == 0 || program->dropped_ranges != 0);
    if (valid && dropped_count != 0)
    {
        uint32_t varying = 0;
        for (uint32_t i = 1; i < dropped_count; i += 1)
        {
            varying |= (uint32_t)((dropped_ranges[0] ^ dropped_ranges[i]) >> 32);
        }
        const uint64_t* sorted = c_bindings_sort(dropped_ranges, dropped_sort, dropped_count, !BUSTER_SCALAR_PATH(options.scalar), varying);
        memcpy(program->dropped_ranges, sorted, (uint64_t)dropped_count * sizeof(uint64_t));
        program->dropped_range_count = dropped_count;
    }
    if (valid)
    {
        program->control_count = writer.count;
        program->exit_count = writer.exit_count;
        program->label_position_count = writer.label_position_count;
        if (writer.label_position_count != 0)
        {
            memcpy(program->label_positions, writer.label_positions, (uint64_t)writer.label_position_count * sizeof(uint32_t));
        }
        program->switch_count = writer.switch_count;
        program->reference_count = reference_count;
        if (writer.count != 0)
        {
            memcpy(program->controls, entries, (uint64_t)writer.count * sizeof(CheckControl));
        }
        // The lifetime events the lowering makes of the LIFETIME controls at most (#177): one per local of each one's block.
        uint64_t events = 0;
        for (uint32_t i = 0; program->lifetime_first != NULL && i < writer.count; i += 1)
        {
            for (uint32_t d = entries[i].kind == CHECK_CONTROL_LIFETIME && entries[i].target < count ? program->lifetime_first[entries[i].target] : BUSTER_CHECK_NONE;
                 d != BUSTER_CHECK_NONE; d = program->lifetime_next[d])
            {
                events += 1;
            }
        }
        program->lifetime_event_count = events <= UINT32_MAX - 1 ? (uint32_t)events : UINT32_MAX;
        if (events > UINT32_MAX - 1)
        {
            check_program_fail(program, CHECK_PROGRAM_RESOURCE, BUSTER_CHECK_NONE);
        }
    }
    // The tail's part over the needed marks, unless it ran before the loop over the candidates' (check_program): then the
    // target groups' states again, over the bodies' own references.
    if (run->symbols && valid && run->symbols_valid && program->status == CHECK_PROGRAM_OK)
    {
        check_program_target_states(program, options.initializer_references, options.initializer_reference_count);
    }
    valid = run->symbols ? valid && run->symbols_valid : check_program_symbols(run, needed, valid);
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_x86_diagnostics(program, stream, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (!valid || program->status != CHECK_PROGRAM_OK)
    {
        const CheckProgramStatus status = program->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : program->status;
        const uint32_t token = status == CHECK_PROGRAM_INVALID ? check_program_diagnostic_location(parser, program->token) : program->token;
        const uint32_t position = program->diagnostic_position;
        const CheckProgramFailure failure = program->failure;
        arena_pop_to(output, mark);
        *program = (CheckProgram){ .status = status, .token = token, .diagnostic_position = position, .failure = failure };
        valid = false;
    }
    else
    {
        program->output_bytes = output->length - mark;
    }
    if (!run->keep_scratch)
    {
        arena_pop_to(scratch, temporary);
    }
    arena_release(&run->return_facts.storage);
    arena_release(&controls);
    return valid;
}

// check_program's parts before its loop (#2536's step 2.2c.4, part 1): its setup and its pass (check_program_begin),
// then, with the candidates (--provisional-selection), the tail's part over their needed marks. A caller may make a
// selection before the loop (check_driver_provisional_select), then run the loop (check_program_loop) and the tail
// (check_program_finish), as check_program does.
static void check_program_start(CheckProgramRun* run, CheckProgram* program, const Parser* parser, const CheckInput* input, CheckStream* stream,
                                const TypeTable* types, CheckProgramOptions options, Arena* output, Arena* scratch)
{
    check_program_begin(run, program, parser, input, stream, types, options, output, scratch);
    // With the candidates (#2536's step 2.2c.2), the tail's part over the needed marks first, over the candidates'
    // references, which hold the exact ones: as the windows will need it before the loop. The selection makes its
    // symbols and the weakrefs' states again from its own references, so the objects are the same; the target groups'
    // states, which the selection takes from the program, check_program_finish makes again
    // (check_program_target_states).
    if (options.candidates && run->valid && program->status == CHECK_PROGRAM_OK && program->census_pass.first != NULL)
    {
        uint64_t total = 0;
        for (uint32_t b = 0; b < run->body_count; b += 1)
        {
            total += program->census_pass.counts[b];
        }
        for (uint64_t r = 0; r < total; r += 1)
        {
            run->needed[program->census_pass.references[r]] = 1;
        }
        run->target_events.census = &program->census_pass;
        run->symbols_valid = check_program_symbols(run, run->needed, true);
        run->target_events.census = NULL;
        run->symbols = true;
    }
}

// check_program's loop over every body, a body at a time, as the windows will run it a window's bodies at a time.
static void check_program_loop(CheckProgramRun* run)
{
    for (uint32_t b = 0; b < run->body_count; b += 1)
    {
        check_program_bodies(run, b + 1);
    }
}

static bool check_program(CheckProgram* program, const Parser* parser, const CheckInput* input, CheckStream* stream,
                          const TypeTable* types, CheckProgramOptions options, Arena* output, Arena* scratch)
{
    CheckProgramRun run;
    check_program_start(&run, program, parser, input, stream, types, options, output, scratch);
    check_program_loop(&run);
    return check_program_finish(&run);
}

#include "c_check_program_symbol_pragma.c"
#include "c_check_program_select.c"
#include "c_check_program_helpers.c"
#include "c_check_program_asm.c"
#include "c_check_program_lower.c"
#include "c_check_debug.c"
#include "c_check_clock.c"
#include "c_check_program_emit.c"
#include "c_check_tier0_inline.c"

#else
#include "c_check_program_vectors.c"
#include "c_check_program_x86.c"
#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
// The IR decoder's conversion tests' producer (#1840).
#include "c_check_program_vector_convert.c"
#undef BUSTER_CHECK_BASE_PACKED
#undef BUSTER_CHECK_BASE_UNARY
#endif
#include "c_check_program_emit.c"
#endif
#endif

