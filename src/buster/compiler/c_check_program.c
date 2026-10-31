// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Executable typed bodies. The source reader retains expression roots and declaration/body ownership; this
// consumer turns parser statement structure into bounded control columns without redoing name or type lookup.
// Expressions stay in CheckGraph/CheckStream, and control entries refer to their stable node identities. This
// representation remains valid when the separate Tier0 lowering cannot represent a checked C type.
// Include after c_check_input.c, calling_convention.c and tier0.c. All input storage outlives this output. Output
// and scratch are distinct arenas; failure rolls back output and always restores scratch. No padding is required.
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
    // Also references in the operands of &&, || and ?: that a constant condition never evaluates, which no emission
    // needs: test-checker-corpus --symbols' model of the references clang -O0's code has (#129).
    bool unselected_operands;
    // With them, of an if whose condition is an integer constant without side effects, only the arm it runs, when no
    // label or case of an outer switch enters the other, as clang -O0's code has (CodeGenFunction::EmitIfStmt): the
    // same model's. Tier 0 lowers both arms, which -c's program keeps.
    bool folded_ifs;
    // Optional deterministic tally: unit graph-mark initialization plus return-warning expression visits.
    uint64_t* return_flow_work;
    // The generated code's instruction set (x86_target.c's X86Target, the preprocessor's from -march and the -m flags):
    // which vector types the lowering takes, how wide a vector register the convention passes them in, and which
    // instructions its rows use. Zero is x86-64 baseline.
    X86Target target;
    // The options it came from (c_predefines.c), against which a function's target attribute resolves (#180); null leaves
    // every function the unit's target.
    const PredefinesOptions* predefines;
};

typedef struct CheckProgramCase CheckProgramCase;
struct CheckProgramCase
{
    unsigned __int128 lower;
    unsigned __int128 upper;
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
    // Refused: an alias declared thread-local of a definition that isn't, or the other way, whose references the code
    // would reach as its own declaration says and the symbol isn't (clang's code is as wrong).
    CHECK_PROGRAM_ALIAS_THREAD_LOCAL,
    // Refused: an alias variable that the unit also defines tentatively, which the static data would define too: after it
    // (`extern int w __attribute__((alias("v"))); int w;`), which clang takes as the alias's declaration, or before it,
    // clang's error "alias definition of 'w' after tentative definition", which the reader doesn't make.
    CHECK_PROGRAM_ALIAS_DEFINED,
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
    uint32_t* references;
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
    // GNU's alias, ifunc and weakref attributes (#147, check_program_aliases): an entry per object a file-scope declaration
    // gives one, in object order; null in a unit without one, the usual one. A selection shares them, but for their states
    // (BUSTER_CHECK_PROGRAM_ALIAS_*, a byte each), which are each program's: its own references and selection decide them.
    // An alias's or ifunc's object has a symbol, which the emission defines at its base's definition once the static
    // data's atoms exist (check_program_sections), and a referenced weakref's object has its target's (object_symbols),
    // or one of its own, named as its target, where no object of the unit has that name.
    CheckProgramAlias* aliases;
    uint8_t* alias_states;
    uint32_t alias_count;
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
    uint32_t token;
    uint32_t diagnostic_position;
    CheckProgramStatus status;
    uint64_t output_bytes;
    // CheckProgramOptions.unselected_operands, folded_ifs and target.
    bool unselected_operands;
    bool folded_ifs;
    X86Target target;
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
    // check_program_target_features); null in a unit without a target attribute, where that check finds nothing.
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
    // are emitted. Code generation's check of target features takes clang's order of emission from it (#775); null in
    // a program that no selection made, or in a unit without a target attribute or an x86 builtin's call (#148), which
    // the check skips.
    uint8_t* body_eager;
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
    // The body's children lists, by token: each token's first and last child and its siblings, which check_control_begin
    // builds from the body's tokens alone (check_control_children).
    uint32_t* first;
    uint32_t* last;
    uint32_t* next;
    uint32_t* previous;
    const uint32_t* token_first;
    const uint32_t* node_next;
    const uint32_t* initializers;
    const uint32_t* plan_first;
    const uint32_t* plan_next;
    const uint32_t* vla_first;
    const uint32_t* vla_next;
    // The variable-length arrays' objects of each statement (by its terminator), in declaration order, and each block's
    // first one's declaration token plus one (by the block's closing token; 0 for a block without them), and the last
    // token before the block's STACK_SAVE plus one: the first one's own bounds' last `]`, or its declaration.
    const uint32_t* allocation_first;
    const uint32_t* allocation_next;
    const uint32_t* vla_blocks;
    const uint32_t* vla_saves;
    const uint32_t* asm_statements;
    const uint32_t* case_first;
    const uint32_t* case_next;
    const uint32_t* case_map;
    uint32_t* source_labels;
    uint32_t* stops;
    uint32_t* repeats;
    const uint32_t* region_first;
    const uint32_t* region_next;
    // Whether each label's address is taken (&&label), by its token: its place gets a CHECK_CONTROL_LABEL.
    const uint8_t* addressed;
    // The body's cleanups' records (CheckInput.cleanups, cleanup_count of them from cleanup_first), and each record's
    // parent, by its index there: the innermost record whose scope holds its declaration, BUSTER_CHECK_NONE for none.
    uint32_t* cleanup_parents;
    uint32_t cleanup_first;
    uint32_t cleanup_count;
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

// Which of them a declaration's `attributes` hold, by one pass over its records that aren't ignored: GNU's naked
// attribute (CheckProgram.object_naked), a target or target_clones attribute (CheckProgramTargetEvents), one of a
// callee's that code generation's check of target features reads, always_inline, flatten or noreturn (#775,
// CheckProgram.attribute_keys), and an alias, ifunc or weakref attribute (#147, CheckProgram.aliases).
static uint32_t check_program_attribute_bits(const CheckInputAttributes* attributes)
{
    uint32_t bits = 0;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = &attributes->records[i];
        const CheckInputAttributeKind kind = attribute->kind;
        const uint32_t bit = kind == CHECK_ATTRIBUTE_NAKED ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_NAKED :
                             kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET :
                             kind == CHECK_ATTRIBUTE_ALWAYS_INLINE || kind == CHECK_ATTRIBUTE_FLATTEN || kind == CHECK_ATTRIBUTE_NORETURN ?
                                 BUSTER_CHECK_PROGRAM_ATTRIBUTE_CALLEE :
                             kind == CHECK_ATTRIBUTE_ALIAS || kind == CHECK_ATTRIBUTE_IFUNC || kind == CHECK_ATTRIBUTE_WEAKREF ? BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALIAS :
                                 0;
        bits |= attribute->ignored ? 0 : bit;
    }
    return bits;
}

// The static functions' declarations with a target or target_clones attribute (#180), which check_program_targets
// resolves each body's instruction set from: keys of their object in the high half and their token in the low, in each
// object's declarations' order, in `scratch`, which grows them by doubling. A unit without one, the usual one, has none.
// Beside them, the declarations with an always_inline, flatten or noreturn attribute, as keys of the same form, which
// only code generation's check of target features reads, and only in a unit with a target attribute (#775,
// CheckProgram.attribute_keys); and the file-scope declarations with an alias, ifunc or weakref attribute, which
// check_program_aliases makes the unit's entries of (#147, CheckProgram.aliases).
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
    bool room;
};

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
    bool effects = (stream->values[node].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0;
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
    const CheckValue* value = program->stream->values + condition;
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
    const bool known = available && (runtime || ((value->flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                                   !(environment && check_action_floating_type(program->types, value->type)))) &&
                       value->fault == CHECK_FAULT_NONE &&
                       check_action_truth(program->types, value, truth);
    return known;
}

static bool check_program_case_value(const CheckProgram* program, uint32_t node, TypeId target, unsigned __int128* result)
{
    uint32_t width = 0;
    uint32_t source_width = 0;
    bool sign = false;
    bool source_sign = false;
    bool valid = node < program->stream->count && check_action_integer_type(program->types, target, &width, &sign);
    if (valid)
    {
        const CheckValue converted = check_graph_context_value(program->types, &program->input->graph, program->stream, node);
        const CheckValue* value = &converted;
        valid = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE &&
                (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && check_action_integer_type(program->types, value->type, &source_width, &source_sign);
        if (valid)
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
    const unsigned __int128 result = sign ? value ^ ((unsigned __int128)1 << (width - 1)) : value;
    return result;
}

static bool check_program_case_before(const CheckProgram* program, uint32_t a, uint32_t b)
{
    const CheckProgramCase* left = program->cases + a;
    const CheckProgramCase* right = program->cases + b;
    const uint32_t left_kind = left->fallback ? 0 : left->range ? 2 : 1;
    const uint32_t right_kind = right->fallback ? 0 : right->range ? 2 : 1;
    const unsigned __int128 left_value = left->fallback ? 0 : check_program_case_order(program, left, false);
    const unsigned __int128 right_value = right->fallback ? 0 : check_program_case_order(program, right, false);
    const bool before = left->owner != right->owner ? left->owner < right->owner : left_kind != right_kind ? left_kind < right_kind :
                        left_value != right_value ? left_value < right_value : left->token < right->token;
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
        uint32_t width = 0;
        bool sign = false;
        if (condition >= program->stream->count || !check_action_integer_type(program->types, program->stream->values[condition].type, &width, &sign))
        {
            check_program_constraint(program, owner);
        }
    }
    if (parser->tree.roles[token] == PARSE_ROLE_CLOSE_CASE)
    {
        const uint32_t owner = input->control_owners[token];
        const uint32_t node = input->token_nodes[token];
        const uint32_t condition = owner < tokens ? input->token_nodes[owner] : BUSTER_CHECK_NONE;
        uint32_t width = 0;
        bool sign = false;
        if (condition >= program->stream->count || !check_action_integer_type(program->types, program->stream->values[condition].type, &width, &sign))
        {
            check_program_constraint(program, owner < tokens ? owner : token);
        }
        else
        {
            const uint32_t index = program->case_count;
            CheckProgramCase* item = program->cases + index;
            *item = (CheckProgramCase)
            {
                .type = program->stream->values[condition].type,
                .token = token,
                .owner = owner,
                .target = BUSTER_CHECK_NONE,
                .fallback = node == BUSTER_CHECK_NONE,
                .range = input->case_ranges[token] != BUSTER_CHECK_NONE,
            };
            const uint32_t high = input->case_ranges[token] != BUSTER_CHECK_NONE ? input->case_ranges[token] : node;
            if (!item->fallback && (!check_program_case_value(program, node, item->type, &item->lower) ||
                                   !check_program_case_value(program, high, item->type, &item->upper)))
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

// The return under a chain of attribute specifiers, a musttail return's (check_input_statement_attributes), from the
// closer of the first: each closer's child after it is the next specifier's closer, or the statement. BUSTER_CHECK_NONE
// where the statement isn't a return (another statement's attributes, which the reader refuses but a null statement's).
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
    return at != BUSTER_CHECK_NONE && parser->tree.roles[at] == PARSE_ROLE_RETURN && parser->tokens.kinds[at] == PARSE_KIND_RETURN ? at : BUSTER_CHECK_NONE;
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

static bool check_control_static(const CheckControlWriter* writer, uint32_t node)
{
    const CheckInput* input = writer->program->input;
    const CheckGraph* graph = &input->graph;
    uint32_t cursor = node;
    while (cursor != BUSTER_CHECK_NONE && (graph->ops[cursor] == CHECK_OP_CAST || graph->ops[cursor] == CHECK_OP_CONVERT))
    {
        cursor = graph->operand0[cursor];
    }
    const uint32_t declaration = cursor != BUSTER_CHECK_NONE && graph->ops[cursor] == CHECK_OP_INITIALIZE ? writer->initializers[cursor] : BUSTER_CHECK_NONE;
    return declaration != BUSTER_CHECK_NONE && input->static_objects[declaration];
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
    for (uint32_t ancestor = parser->tree.parents[jump]; ancestor < count && steps < count && !past; ancestor = parser->tree.parents[ancestor])
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
    while (record != BUSTER_CHECK_NONE && cleanups[record].end <= position)
    {
        record = writer->cleanup_parents[record];
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
    uint32_t record = writer->cleanup_count != 0 ? check_control_cleanup_at(writer, position) : BUSTER_CHECK_NONE;
    while (record != BUSTER_CHECK_NONE && (scope != BUSTER_CHECK_NONE ? cleanups[record].scope == scope :
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
    const uint32_t function = input->body_declarations[writer->opening];
    const TypeId type = function < count ? type_strip_aligned(types, input->declared_types[function]) : 0;
    const TypeId result = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_FUNCTION ? type_unqualified(type_strip_aligned(types, type_return(types, type))) : 0;
    const uint8_t result_kind = result != 0 ? type_kind(types, result) : BUSTER_TYPE_KIND_NONE;
    const uint32_t variable = result_kind == BUSTER_TYPE_KIND_STRUCT || result_kind == BUSTER_TYPE_KIND_UNION ? check_control_returned(writer, jump) : BUSTER_CHECK_NONE;
    const TypeId declared = variable < count ? input->declared_types[variable] : 0;
    const CheckInputAttributes* attributes = variable < count ? input->attributes[variable] : NULL;
    const bool aligned = attributes != NULL && attributes->alignment_log2 != 0 && attributes->alignment_log2 - 1u > type_alignment_log2(types, declared);
    bool named = variable > writer->opening && variable < writer->closing && input->declaration_kinds[variable] == C_BINDING_OBJECT &&
                 !input->static_objects[variable] && declared != 0 && (type_qualifiers(declared) & BUSTER_TYPE_VOLATILE) == 0 && !aligned &&
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

// Whether a jump can enter the statement `arm` (none for BUSTER_CHECK_NONE) other than at its start: it holds a label,
// or a case of a switch outside it, as clang's CodeGenFunction::ContainsLabel finds.
static bool check_control_enterable(const CheckControlWriter* writer, uint32_t arm)
{
    const uint8_t* roles = writer->parser->tree.roles;
    const uint32_t* owners = writer->program->input->control_owners;
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, arm, &low, &high);
    bool enterable = false;
    for (uint32_t token = low; arm != BUSTER_CHECK_NONE && !enterable && token <= high; token += 1)
    {
        enterable = roles[token] == PARSE_ROLE_LABEL || (roles[token] == PARSE_ROLE_CLOSE_CASE && (owners[token] < low || owners[token] > high));
    }
    return enterable;
}

// Appends a source label's number, a writer's label, to the writer's CheckProgram.label_positions, which the bodies'
// loop turns into its place once the label's body is written: a label is a token, so the column has room.
static void check_control_source_label(CheckControlWriter* writer, uint32_t label)
{
    writer->label_positions[writer->label_position_count++] = label;
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
        if (role == PARSE_ROLE_CLOSE_BLOCK && writer->vla_blocks[token] != 0)
        {
            check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, token, BUSTER_CHECK_NONE, token);
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
            // folded_ifs' model of clang -O0's code: the arm a constant condition runs, without the condition. The
            // condition's node is its truth (CHECK_OP_CONDITION); clang folds the expression's, of an integer type.
            const CheckGraph* graph = &input->graph;
            const uint32_t expression = graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
            const CheckValue* value = writer->program->stream->values + expression;
            uint32_t width = 0;
            bool sign = false;
            bool truth = false;
            const bool folded = writer->program->folded_ifs && expression < graph->count && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                check_action_integer_type(writer->program->types, value->type, &width, &sign) &&
                                check_program_runtime_truth(writer->program, condition, expression, &truth) &&
                                !check_control_enterable(writer, truth ? alternate : body);
            const uint32_t executed = truth ? body : alternate;
            const uint32_t skipped = truth ? alternate : body;
            if (folded)
            {
                // The arm left out takes its own switches with it, whose cases go where the if ends, since every case
                // of a body has a place.
                uint32_t low = 0;
                uint32_t high = 0;
                check_control_extent(writer, skipped, &low, &high);
                check_control_mark(writer, end);
                for (uint32_t case_token = low; skipped != BUSTER_CHECK_NONE && case_token <= high; case_token += 1)
                {
                    if (parser->tree.roles[case_token] == PARSE_ROLE_CLOSE_CASE)
                    {
                        check_control_mark(writer, writer->source_labels[case_token]);
                    }
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
            writer->stops[token] = end;
            writer->repeats[token] = start;
            check_control_mark(writer, end);
            check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, start);
            check_control_statement(writer, body, end, start);
            check_control_entry(writer, CHECK_CONTROL_BRANCH_FALSE, token, condition, end);
            check_control_mark(writer, start);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
        {
            writer->stops[token] = end;
            uint32_t width = 0;
            bool sign = false;
            if (!check_action_integer_type(writer->program->types, writer->program->stream->values[condition].type, &width, &sign))
            {
                check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
            }
            uint32_t fallback = end;
            for (uint32_t c = writer->case_first[token]; c != BUSTER_CHECK_NONE; c = writer->case_next[c])
            {
                const CheckProgramCase* item = writer->program->cases + c;
                if (item->fallback)
                {
                    fallback = writer->source_labels[item->token];
                }
            }
            check_control_mark(writer, end);
            check_control_statement(writer, body, end, task.repeat);
            check_control_entry(writer, CHECK_CONTROL_JUMP, token, BUSTER_CHECK_NONE, fallback);
            uint32_t cases = 0;
            for (uint32_t c = writer->case_first[token]; c != BUSTER_CHECK_NONE; c = writer->case_next[c])
            {
                if (!writer->program->cases[c].fallback)
                {
                    check_control_entry(writer, CHECK_CONTROL_CASE, writer->program->cases[c].token, BUSTER_CHECK_NONE, c);
                    cases += 1;
                }
            }
            check_control_entry(writer, CHECK_CONTROL_SWITCH, token, condition, BUSTER_CHECK_NONE);
            // A jump table takes four clusters of cases or more, so only a switch of four cases or more may have one: the
            // program lists those. The drain writes this SWITCH entry next, the last task pushed, at writer->count.
            if (cases >= 4 && writer->switch_count < writer->switch_capacity)
            {
                writer->switches[writer->switch_count++] = writer->count;
            }
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
        writer->stops[token] = end;
        writer->repeats[token] = update;
        if (first == BUSTER_CHECK_NONE || condition == BUSTER_CHECK_NONE || body == BUSTER_CHECK_NONE)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
        }
        else
        {
            // Its own variables' cleanups run where it ends, which its condition and its breaks reach.
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
            const uint32_t node = writer->token_first[condition];
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
            if (kind == PARSE_KIND_RETURN && input->musttail_count != 0 && check_control_musttail(input, jump))
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
                const uint32_t target = input->label_targets[jump];
                if (target > writer->opening && target < writer->closing && writer->source_labels[target] != BUSTER_CHECK_NONE)
                {
                    check_control_entry(writer, CHECK_CONTROL_JUMP, jump, BUSTER_CHECK_NONE, writer->source_labels[target]);
                    check_control_leave(writer, jump, target, 0, BUSTER_CHECK_NONE);
                    check_control_cleanups(writer, jump, target, BUSTER_CHECK_NONE);
                }
                else
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_INVALID, jump);
                }
            }
            else
            {
                check_program_fail(writer->program, CHECK_PROGRAM_UNSUPPORTED, jump);
            }
        }
        else if (body != BUSTER_CHECK_NONE && condition != BUSTER_CHECK_NONE)
        {
            const uint32_t start = check_control_label(writer);
            const uint32_t test = check_control_label(writer);
            const uint32_t end = check_control_label(writer);
            writer->stops[body] = end;
            writer->repeats[body] = test;
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

// The body's own token columns, in one pass over its tokens, just before its build reads them (the unit's columns, built
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
    writer->case_start = BUSTER_CHECK_NONE;
    writer->case_end = 0;
    for (uint32_t token = opening; token <= closing; token += 1)
    {
        const uint32_t parent = parents[token];
        const bool inside = parent - opening <= closing - opening;
        const uint32_t sibling = inside ? writer->last[parent] : BUSTER_CHECK_NONE;
        writer->previous[token] = sibling;
        writer->next[token] = BUSTER_CHECK_NONE;
        if (inside && sibling != BUSTER_CHECK_NONE)
        {
            writer->next[sibling] = token;
        }
        else if (inside)
        {
            writer->first[parent] = token;
        }
        if (inside)
        {
            writer->last[parent] = token;
        }
        const uint8_t role = roles[token];
        uint32_t label = BUSTER_CHECK_NONE;
        if (token != opening && token != closing && (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE))
        {
            label = check_control_label(writer);
        }
        if (label != BUSTER_CHECK_NONE && role == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t index = writer->case_map[token];
            writer->program->cases[index].target = label;
            writer->case_start = writer->case_start == BUSTER_CHECK_NONE ? index : writer->case_start;
            writer->case_end = index + 1;
        }
        writer->source_labels[token] = label;
        writer->stops[token] = BUSTER_CHECK_NONE;
        writer->repeats[token] = BUSTER_CHECK_NONE;
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
    if (writer->vla_first[opening] != BUSTER_CHECK_NONE)
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
            const uint32_t assembly = writer->asm_statements[task.token];
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
            const uint32_t plan = writer->plan_first[task.token];
            uint32_t i = plan;
            uint32_t v = writer->vla_first[task.token];
            uint32_t a = writer->allocation_first[task.token];
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
                else if (bound || !input->static_objects[declaration])
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
            // The graph's token groups are in postorder. Each wrapper here is a complete discarded expression
            // or declaration initializer; expressions of nested statements have their own source tokens.
            for (uint32_t node = plan == BUSTER_CHECK_NONE && assembly == BUSTER_CHECK_NONE ? writer->token_first[task.token] : BUSTER_CHECK_NONE; node != BUSTER_CHECK_NONE; node = writer->node_next[node])
            {
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
    for (uint32_t r = writer->region_first[opening]; r != BUSTER_CHECK_NONE && writer->program->status == CHECK_PROGRAM_OK; r = writer->region_next[r])
    {
        const CheckInputRegion* source = input->regions + r;
        CheckProgramRegion* region = writer->program->regions + r;
        region->first = writer->count - writer->first_entry;
        region->body = writer->program->body_count;
        region->node = input->token_nodes[source->closing];
        region->result = source->result;
        uint32_t stop = BUSTER_CHECK_NONE;
        uint32_t repeat = BUSTER_CHECK_NONE;
        uint32_t ancestor = writer->parser->tree.parents[source->opening];
        uint32_t steps = 0;
        while (ancestor < input->token_count && ancestor != closing && steps++ < input->token_count)
        {
            // A loop's own controlling expression belongs to its enclosing loop for GNU break/continue.
            if (source->opening > ancestor || writer->parser->tree.roles[ancestor] == PARSE_ROLE_DO)
            {
                stop = stop == BUSTER_CHECK_NONE ? writer->stops[ancestor] : stop;
                repeat = repeat == BUSTER_CHECK_NONE ? writer->repeats[ancestor] : repeat;
            }
            ancestor = writer->parser->tree.parents[ancestor];
        }
        if (source->block >= input->token_count || region->node >= input->graph.count || source->result >= input->graph.count)
        {
            check_program_fail(writer->program, CHECK_PROGRAM_INVALID, source->opening);
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
            // Its block's cleanups, after its END: they run once its value is computed (check_lower_region).
            const uint32_t ended = writer->count;
            const uint32_t block = writer->cleanup_count != 0 ? check_control_opening(writer, source->block, PARSE_ROLE_OPEN_BLOCK) : BUSTER_CHECK_NONE;
            if (block != BUSTER_CHECK_NONE)
            {
                check_control_cleanups(writer, source->block, BUSTER_CHECK_NONE, block);
                check_control_drain(writer);
            }
            region->cleanups = writer->count - ended;
            region->count = writer->count - writer->first_entry - region->first;
        }
    }
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
            }
        }
        else if (entry->kind == CHECK_CONTROL_ASM)
        {
            const CheckInputAsm* assembly = input->asm_statements + entry->target;
            for (uint32_t l = 0; l < assembly->label_count; l += 1)
            {
                const uint32_t token = assembly->labels[l].target;
                const uint32_t label = token < input->token_count ? writer->source_labels[token] : BUSTER_CHECK_NONE;
                if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
                {
                    check_program_fail(writer->program, CHECK_PROGRAM_INVALID, assembly->labels[l].token);
                }
                else
                {
                    writer->program->asm_targets[writer->program->asm_offsets[entry->target] + l] = writer->labels[label];
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
        }
    }
}

// Aggregate returns need an address even at a reachable implicit return. Reuse the completed writer's label and
// task scratch for a bounded CFG worklist; each control enters once. The first walk retains both conditional edges,
// as Tier 0 validates them, and removes dead transfers without renumbering labels. The second selects constant
// conditions for the source warning. Statement-expression regions keep the lowering's existing stack tracking.
static void check_program_return_flow(CheckControlWriter* writer, CheckProgramBody* body, uint32_t* node_marks, uint32_t* node_stack, uint64_t* work)
{
    CheckProgram* program = writer->program;
    const TypeId result = type_conversion_operand(program->types, type_return(program->types, program->input->declared_types[body->declaration]));
    const uint32_t kind = type_kind(program->types, result);
    const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_COMPLEX;
    if (program->status == CHECK_PROGRAM_OK && aggregate && body->count != 0 && body->count == body->entry_count)
    {
        CheckControl* entries = writer->entries + body->first;
        uint8_t* seen = (uint8_t*)writer->tasks;
        uint32_t* queue = writer->labels;
        const CheckGraph* graph = &program->input->graph;
        // Reference epochs are body identities, below token_count < 2^29. Temporarily OR the disjoint high bit
        // into their already initialized node marks, then restore every touched word before references consume it.
        // This avoids clearing the whole unit's graph for each small aggregate body. The task records are dead;
        // their suffix holds the visited IDs apart from the control marks. capacity is 8 * token_count + 16,
        // so the u32 suffix is aligned and graph->count <= capacity bounds it within the existing allocation.
        const uint32_t warning_mark = UINT32_C(1) << 31;
        _Static_assert(sizeof(CheckControlTask) >= 5, "control tasks hold byte marks and a u32 visited list");
        uint32_t* touched = (uint32_t*)((uint8_t*)writer->tasks + writer->capacity);
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            bool warning_safe = graph->count <= writer->capacity;
            memset(seen, 0, body->count);
            uint32_t touched_count = 0;
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
                    const bool known = pass != 0 && !jump && check_program_runtime_truth(program, entry->node, entry->node, &truth);
                    const bool taken = truth == (entry->kind == CHECK_CONTROL_BRANCH_TRUE);
                    targets[0] = jump || !known || taken ? entry->target : BUSTER_CHECK_NONE;
                    next = !jump && (!known || !taken);
                }
                else if (pass != 0 && entry->kind == CHECK_CONTROL_SWITCH)
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
                    const bool known = check_program_case_value(program, entry->node, program->stream->values[entry->node].type, &value);
                    const bool enumeration = source < graph->count &&
                        type_kind(program->types, type_strip_aligned(program->types, program->stream->values[source].type)) == BUSTER_TYPE_KIND_ENUM;
                    warning_safe &= !enumeration || known;
                }
                else if (entry->kind == CHECK_CONTROL_CASE)
                {
                    const CheckProgramCase* item = program->cases + entry->target;
                    const uint32_t condition = pass != 0 ? program->input->token_nodes[item->owner] : BUSTER_CHECK_NONE;
                    CheckProgramCase selected = { .type = item->type };
                    const bool known = pass != 0 && check_program_case_value(program, condition, item->type, &selected.lower);
                    const unsigned __int128 ordered = known ? check_program_case_order(program, &selected, false) : 0;
                    const bool taken = known && ordered >= check_program_case_order(program, item, false) &&
                                               ordered <= check_program_case_order(program, item, true);
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
                // Calls may be declared noreturn. Query operands also affect Clang's source-flow warning even
                // when unevaluated. Until these contracts reach controls, keep their warnings conservative.
                if (pass != 0 && warning_safe && entry->node < graph->count && (node_marks[entry->node] & warning_mark) == 0)
                {
                    uint32_t depth = 1;
                    node_stack[0] = entry->node;
                    node_marks[entry->node] |= warning_mark;
                    touched[touched_count++] = entry->node;
                    while (depth != 0 && warning_safe)
                    {
                        const uint32_t node = node_stack[--depth];
                        if (work != NULL)
                        {
                            *work += 1;
                        }
                        const CheckOp op = graph->ops[node];
                        warning_safe = op != CHECK_OP_CALL && op != CHECK_OP_BUILTIN_MISC && op != CHECK_OP_BUILTIN_QUERY;
                        if (warning_safe && op != CHECK_OP_VALUE && op != CHECK_OP_SIZEOF && op != CHECK_OP_ALIGNOF)
                        {
                            uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
                            if (op == CHECK_OP_VARARGS)
                            {
                                const uint32_t effect = graph->argument_offsets[node];
                                warning_safe = effect < program->stream->varargs_count && program->stream->varargs[effect].node == node;
                                const uint32_t evaluated = warning_safe ? program->stream->varargs[effect].evaluated_operands : 0;
                                for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
                                {
                                    operands[o] = (evaluated & (1u << o)) != 0 ? operands[o] : BUSTER_CHECK_NONE;
                                }
                            }
                            bool truth = false;
                            if ((op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL) &&
                                check_program_runtime_truth(program, node, operands[0], &truth))
                            {
                                if (op == CHECK_OP_CONDITIONAL)
                                {
                                    operands[truth ? 2 : 1] = BUSTER_CHECK_NONE;
                                }
                                else if (truth == (op == CHECK_OP_LOGICAL_OR))
                                {
                                    operands[1] = BUSTER_CHECK_NONE;
                                }
                            }
                            for (uint32_t o = 0; warning_safe && o < BUSTER_ARRAY_LENGTH(operands); o += 1)
                            {
                                const uint32_t operand = operands[o];
                                if (operand < graph->count && (node_marks[operand] & warning_mark) == 0)
                                {
                                    node_marks[operand] |= warning_mark;
                                    touched[touched_count++] = operand;
                                    node_stack[depth++] = operand;
                                }
                            }
                            if (warning_safe && check_graph_has_arguments(op))
                            {
                                const uint32_t mask = check_program_argument_mask(program, node);
                                for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
                                {
                                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                                    const bool evaluated = op == CHECK_OP_BUILTIN_X86 || (a < 32 && (mask & (1u << a)) != 0);
                                    if (evaluated && argument < graph->count && (node_marks[argument] & warning_mark) == 0)
                                    {
                                        node_marks[argument] |= warning_mark;
                                        touched[touched_count++] = argument;
                                        node_stack[depth++] = argument;
                                    }
                                }
                            }
                        }
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
            for (uint32_t t = 0; t < touched_count; t += 1)
            {
                node_marks[touched[t]] &= ~warning_mark;
            }
            const bool falls = body->count != 0 && seen[body->count - 1] != 0;
            if (pass == 0)
            {
                body->return_flow = true;
                body->falls = falls;
                for (uint32_t i = 0; i < body->count; i += 1)
                {
                    if (seen[i] == 0 && entries[i].kind >= CHECK_CONTROL_BRANCH_FALSE)
                    {
                        entries[i].kind = CHECK_CONTROL_END;
                    }
                }
            }
            else
            {
                body->return_warning = falls && warning_safe ? returns ? 2 : 1 : 0;
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
            if (program->stream->values[expression].overload_head != 0)
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

static uint32_t check_program_reference_roots(CheckProgram* program, const CheckControl* controls, uint32_t count, uint32_t epoch,
                                              uint32_t* nodes, uint32_t* stack, uint32_t* depth, uint32_t* seen,
                                              uint64_t* references, uint8_t* needed, uint32_t reference_count)
{
    const CheckGraph* graph = &program->input->graph;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t node = controls[i].node;
        if (node != BUSTER_CHECK_NONE && nodes[node] != epoch)
        {
            nodes[node] = epoch;
            stack[(*depth)++] = node;
        }
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
        // A cleanup's call of its function.
        const uint32_t function = controls[i].kind == CHECK_CONTROL_CLEANUP && controls[i].target < program->input->cleanup_count ?
                                  program->input->cleanups[controls[i].target].function : BUSTER_CHECK_NONE;
        const uint32_t object = function < program->input->token_count ? program->input->objects[function] : 0;
        if (object != 0 && object < program->object_count && seen[object] != epoch)
        {
            seen[object] = epoch;
            needed[object] = 1;
            references[reference_count++] = (uint64_t)object << 32;
        }
    }
    return reference_count;
}

static uint32_t check_program_references(CheckProgram* program, const CheckControl* controls, uint32_t count, uint32_t epoch,
                                         uint32_t* seen, uint32_t* nodes, uint32_t* stack, uint64_t* references, uint8_t* needed)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t depth = 0;
    uint32_t reference_count = 0;
    reference_count = check_program_reference_roots(program, controls, count, epoch, nodes, stack, &depth, seen, references, needed, reference_count);
    while (depth != 0)
    {
        const uint32_t node = stack[--depth];
        const CheckOp op = graph->ops[node];
        if (op == CHECK_OP_VALUE)
        {
            const CheckValue value = *check_graph_leaf_of(graph, node);
            if (value.overload_head == 0 && value.object != 0 && value.object < program->object_count &&
                ((value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0 || program->input->static_objects[value.object - 1]) && seen[value.object] != epoch)
            {
                seen[value.object] = epoch;
                needed[value.object] = 1;
                references[reference_count++] = (uint64_t)value.object << 32;
            }
            if (value.object != 0 && value.object < program->object_count && program->input->compound_objects[value.object - 1] &&
                !program->input->static_objects[value.object - 1])
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
                if (op == CHECK_OP_CONDITIONAL)
                {
                    operands[truth ? 2 : 1] = BUSTER_CHECK_NONE;
                }
                else if ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth))
                {
                    operands[1] = BUSTER_CHECK_NONE;
                }
            }
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands); k += 1)
            {
                const uint32_t operand = operands[k];
                const CheckOverloadResult* selected = operand != BUSTER_CHECK_NONE ? check_program_overload_address(program, node, k) : NULL;
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
                    if (op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || (a < 32 && (mask & (1u << a)) != 0))
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
            const uint32_t target = input->label_targets[token + (uint32_t)__builtin_ctzll(selected)];
            if (target < count)
            {
                addressed[target] = 1;
            }
            selected &= selected - 1;
        }
    }
    for (; token < count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_AMPAMP && parser->tree.roles[token] == PARSE_ROLE_PREFIX && input->label_targets[token] < count)
        {
            addressed[input->label_targets[token]] = 1;
        }
    }
}

#define BUSTER_CHECK_PROGRAM_LANES 16u

// The bodies to build and the initializers' declaration tokens by node, in one pass over the tokens (#530): each body's
// opening brace (a token with a body declaration, unless deferred) into `openings` in token order, and each initializer
// node's token into `initializers`, the last as a scan in token order leaves it. AVX-512 tests 16 tokens at a time, the
// rest and the `scalar` path one at a time. Returns the number of bodies.
static uint32_t check_program_openings(const CheckInput* input, uint32_t node_count, uint32_t* openings, uint32_t* initializers, bool scalar)
{
    (void)scalar;

    const uint32_t count = input->token_count;
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i nodes = _mm512_set1_epi32((int)node_count);
    uint32_t bodies = 0;
    uint32_t token = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && count - token >= BUSTER_CHECK_PROGRAM_LANES; token += BUSTER_CHECK_PROGRAM_LANES)
    {
        __mmask16 opened = _mm512_cmpneq_epi32_mask(_mm512_loadu_si512(input->body_declarations + token), none);
        if (input->deferred_tokens != 0)
        {
            opened &= _mm_cmpeq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->deferred_tokens + token)), _mm_setzero_si128());
        }
        for (uint32_t lanes = opened; lanes != 0; lanes &= lanes - 1)
        {
            openings[bodies++] = token + (uint32_t)__builtin_ctz(lanes);
        }
        const __mmask16 initialized = _mm512_cmplt_epu32_mask(_mm512_loadu_si512(input->initializer_nodes + token), nodes);
        for (uint32_t lanes = initialized; lanes != 0; lanes &= lanes - 1)
        {
            const uint32_t at = token + (uint32_t)__builtin_ctz(lanes);
            initializers[input->initializer_nodes[at]] = at;
        }
    }
    for (; token < count; token += 1)
    {
        if (input->body_declarations[token] != BUSTER_CHECK_NONE && (input->deferred_tokens == 0 || input->deferred_tokens[token] == 0))
        {
            openings[bodies++] = token;
        }
        if (input->initializer_nodes[token] < node_count)
        {
            initializers[input->initializer_nodes[token]] = token;
        }
    }
    return bodies;
}

// A variable-length array's object, declared at `token`: into its statement's allocation list (by the statement's
// terminator), as its block's first array.
static void check_program_allocation(CheckProgram* program, const Parser* parser, uint32_t token, uint32_t* allocation_first, uint32_t* allocation_next,
                                     uint32_t* vla_blocks, uint32_t* vla_saves)
{
    const uint32_t count = program->input->token_count;
    uint32_t terminator = token;
    uint32_t steps = 0;
    while (terminator < count && steps++ < count && parser->tree.roles[terminator] != PARSE_ROLE_TERMINATOR)
    {
        terminator = parser->tree.parents[terminator];
    }
    if (terminator < count && steps < count && parser->tree.parents[terminator] < count)
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
    return input->declarations[token] == token && input->declaration_bodies[token] < input->token_count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
           !input->static_objects[token] && check_action_runtime_size(types, input->declared_types[token]);
}

// The variable-length arrays' objects, by their statements, and each block's first, from the last token back, so that the
// lists are in declaration order and a block's last write is its first array (#530): AVX-512 finds the bodies' objects'
// declarations 16 tokens at a time, whose types alone it asks; the rest and the `scalar` path ask every token.
static void check_program_allocations(CheckProgram* program, const Parser* parser, const TypeTable* types, uint32_t* allocation_first, uint32_t* allocation_next,
                                      uint32_t* vla_blocks, uint32_t* vla_saves, bool scalar)
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
                                  _mm512_cmplt_epu32_mask(_mm512_loadu_si512(input->declaration_bodies + base), _mm512_set1_epi32((int)count)) &
                                  _mm_cmpeq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->declaration_kinds + base)), _mm_set1_epi8((char)C_BINDING_OBJECT)) &
                                  _mm_cmpeq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->static_objects + base)), _mm_setzero_si128());
        for (uint32_t bits = objects; bits != 0;)
        {
            const uint32_t lane = 31 - (uint32_t)__builtin_clz(bits);
            bits &= ~(1u << lane);
            if (check_action_runtime_size(types, input->declared_types[base + lane]))
            {
                check_program_allocation(program, parser, base + lane, allocation_first, allocation_next, vla_blocks, vla_saves);
            }
        }
    }
    while (token != 0)
    {
        token -= 1;
        if (check_program_runtime_object(input, types, token))
        {
            check_program_allocation(program, parser, token, allocation_first, allocation_next, vla_blocks, vla_saves);
        }
    }
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
        needed[object] |= input->defined[token] || input->tentative[token];
        const uint32_t attributes = input->attributes[token] != 0 ? check_program_attribute_bits(input->attributes[token]) : 0;
        const uint32_t function_attributes = attributes != 0 && input->declaration_kinds[token] == C_BINDING_FUNCTION ? attributes : 0;
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_TARGET) != 0)
        {
            check_program_target_event(events, object, token);
        }
        if ((function_attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_CALLEE) != 0)
        {
            check_program_event_push(events, &events->callee_keys, &events->callee_count, &events->callee_capacity, object, token);
        }
        // An alias's, ifunc's or weakref's: clang's code generation takes them at file scope alone (EmitGlobal).
        if ((attributes & BUSTER_CHECK_PROGRAM_ATTRIBUTE_ALIAS) != 0 && input->declaration_bodies[token] == BUSTER_CHECK_NONE)
        {
            check_program_event_push(events, &events->alias_keys, &events->alias_count, &events->alias_capacity, object, token);
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
    }
    return room;
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
                                   _mm_cmpneq_epi8_mask(_mm_loadu_si128((const __m128i*)(input->static_objects + token)), _mm_setzero_si128());
        for (uint32_t bits = declared; bits != 0; bits &= bits - 1)
        {
            room = check_program_static_object(program, needed, token + (uint32_t)__builtin_ctz(bits), output, room, events);
        }
    }
    for (; token < count; token += 1)
    {
        if (input->declarations[token] == token && input->static_objects[token])
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
        for (uint32_t e = low; e < count && (uint32_t)(keys[e] >> 32) == body.object; e += 1)
        {
            const uint32_t token = (uint32_t)keys[e];
            const CheckInputAttributes* attributes = input->attributes[token];
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
                versions |= attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES && !attribute->ignored;
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
        if (!versions && kind == PREDEFINES_TARGET_ATTRIBUTE_VERSION && options->predefines != NULL)
        {
            // The target of the same string (the last body's, as a header's functions share theirs, first), or a new one.
            uint32_t found = last_target < program->target_count && check_program_text_equal(texts[last_target], applied->text) ? last_target :
                                                                                                                                  program->target_count;
            for (uint32_t t = 0; t < program->target_count && found == program->target_count; t += 1)
            {
                found = check_program_text_equal(texts[t], applied->text) ? t : found;
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
                    program->targets[found] = predefines_function_target(options->predefines, &program->host, program->target, applied->text.pointer,
                                                                         (uint32_t)applied->text.length, cpu);
                    texts[found] = applied->text;
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
    return valid;
}

// The keys code generation's check of target features reads (#775, CheckProgram.attribute_keys), in `output`, where the
// unit has a target attribute: the target attributes' declarations (check_program_targets sorted them stably by object,
// so each object's are still in token order) and then the always_inline, flatten and noreturn ones, sorted stably by
// object with the radix kernel's twins (c_bindings_sort). A unit without a target attribute keeps none: no call of it
// can lack a feature by clang's rule. Returns false if there was no room.
__attribute__((noinline)) static bool check_program_attribute_keys(CheckProgram* program, const CheckProgramTargetEvents* events, bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t total = (uint64_t)events->count + events->callee_count;
    bool valid = events->room && total <= UINT32_MAX;
    if (valid && events->count != 0)
    {
        uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, total);
        uint64_t* temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, total);
        valid = keys != 0 && temporary != 0;
        if (valid)
        {
            memcpy(keys, events->keys, (uint64_t)events->count * sizeof(uint64_t));
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

// #442's kernels (c_check_program_select.c), which check_program numbers the needed objects' symbols with too.
static uint32_t check_program_needed_count(const uint8_t* needed, uint32_t objects, bool scalar);
static void check_program_needed_number(const uint8_t* needed, uint32_t objects, SymbolId* object_symbols, uint32_t* symbol_objects, bool scalar);

#include "c_check_x86_diagnostic.c"
#include "c_check_target_feature.c"
#include "c_check_program_alias.c"

// The entries that cleanups add at most (#147): a CHECK_CONTROL_CLEANUP at each exit from a variable's scope, its end and
// each jump inside it (whose keywords have the role RETURN), counted from a running count of those tokens, and a RESULT
// at each return. UINT64_MAX where the count's room can't be taken.
static uint64_t check_program_cleanup_room(const Parser* parser, const CheckInput* input, Arena* scratch)
{
    uint64_t room = 0;
    if (input->cleanup_count != 0)
    {
        const uint64_t mark = scratch->length;
        const uint32_t count = input->token_count;
        uint32_t* jumps = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
        room = jumps == NULL ? UINT64_MAX : 0;
        if (jumps != NULL)
        {
            jumps[0] = 0;
            for (uint32_t token = 0; token < count; token += 1)
            {
                jumps[token + 1] = jumps[token] + (parser->tree.roles[token] == PARSE_ROLE_RETURN);
            }
            room = jumps[count];
            for (uint32_t r = 0; r < input->cleanup_count; r += 1)
            {
                const CheckInputCleanup* cleanup = input->cleanups + r;
                room += cleanup->declaration < cleanup->end && cleanup->end <= count ? (uint64_t)jumps[cleanup->end] - jumps[cleanup->declaration] + 1 : 0;
            }
        }
        arena_pop_to(scratch, mark);
    }
    return room;
}

static bool check_program(CheckProgram* program, const Parser* parser, const CheckInput* input, CheckStream* stream,
                          const TypeTable* types, CheckProgramOptions options, Arena* output, Arena* scratch)
{
    *program = (CheckProgram){ .input = input, .stream = stream, .types = types, .parser = parser, .identifiers = options.identifiers,
                               .initializers = options.initializers, .asms = options.asms, .unselected_operands = options.unselected_operands,
                               .folded_ifs = options.folded_ifs, .target = options.target, .token = BUSTER_CHECK_NONE,
                               .diagnostic_position = BUSTER_CHECK_NONE };
    if (options.return_flow_work != NULL)
    {
        *options.return_flow_work = 0;
    }
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = input->token_count;
    const uint32_t node_count = input->graph.count;
    CheckProgramTargetEvents target_events = { .scratch = scratch, .room = true };
    bool valid = output != scratch && count == parser->tokens.count && count < (UINT32_MAX - 16) / 8 &&
                 stream->status == CHECK_STATUS_OK && stream->count == node_count && stream->report_count == 0 && !input->resource && input->report_count == 0 &&
                 !input->visibility_error;
    valid = valid && (options.initializers == 0 || (options.initializers->status == CHECK_INIT_OK && options.initializers->token_count == count)) &&
            (input->asm_count == 0 || (options.asms != 0 && options.asms->status == CHECK_ASM_OK)) &&
            (options.initializer_reference_count == 0 || options.initializer_references != 0) &&
            (options.prepared_data == NULL || (options.prepared_data->status == CHECK_DATA_INPUT_OK &&
             options.prepared_data->object_count == count + 1 &&
             (options.prepared_data->declaration_count == 0 || options.prepared_data->declaration_keys != NULL)));
    program->object_count = count + 1;
    program->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    const uint64_t cleanup_room = valid ? check_program_cleanup_room(parser, input, scratch) : 0;
    valid = valid && cleanup_room <= UINT32_MAX - ((uint64_t)count * 8 + 16);
    const uint32_t capacity = valid ? (uint32_t)((uint64_t)count * 8 + 16 + cleanup_room) : 0;
    // The bodies' labels, the worklist of a body's controls and the unit's controls, with room for 8 a token: each body
    // takes the labels and the worklist again from their start, and the controls follow each other. They are in an arena
    // of their own, which check_program unmaps when it returns (#572). In the scratch, their starts and the columns pushed
    // after them took pages past those the reader touches, which the scratch kept to the end of the compile.
    Arena controls = valid ? arena_reserve(((uint64_t)capacity * (sizeof(uint32_t) + sizeof(CheckControl) + sizeof(CheckControlTask))) + 3 * 63) : (Arena){ 0 };
    uint32_t* first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* last = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* previous = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* token_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* node_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* initializers = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* plan_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* plan_next = valid && options.initializers != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)options.initializers->count + 1) : 0;
    uint32_t* vla_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->vla_count + 1) : 0;
    uint32_t* allocation_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* allocation_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_blocks = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* vla_saves = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* asm_statements = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* case_map = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* source_labels = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* stops = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* repeats = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* region_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* region_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->region_count + 1) : 0;
    // Each body's opening brace, in token order: at most one per token, and only the bodies' entries are touched.
    uint32_t* openings = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* labels = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, uint32_t, capacity) : 0;
    uint32_t* label_positions = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* stack = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* nodes = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* seen = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    // A body's references: its nodes', and its cleanups' functions.
    uint64_t* reference_items = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + input->cleanup_count + 1) : 0;
    uint64_t* reference_sort = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + input->cleanup_count + 1) : 0;
    uint32_t* cleanup_parents = valid && input->cleanup_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, input->cleanup_count) : 0;
    uint8_t* needed = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* addressed = valid && input->label_address_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    CheckControl* entries = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, CheckControl, capacity) : 0;
    CheckControlTask* tasks = valid ? BUSTER_ARENA_PUSH_ARRAY(&controls, CheckControlTask, capacity) : 0;
    valid = valid && first != 0 && last != 0 && next != 0 && previous != 0 && token_first != 0 && node_next != 0 && initializers != 0 && labels != 0 &&
            stack != 0 && nodes != 0 && seen != 0 && needed != 0 && (input->label_address_count == 0 || addressed != 0) && entries != 0 && tasks != 0 && plan_first != 0 &&
            (options.initializers == 0 || plan_next != 0) &&
            case_first != 0 && case_next != 0 && case_map != 0 && source_labels != 0 && label_positions != 0 && reference_items != 0 && reference_sort != 0 &&
            stops != 0 && repeats != 0 && region_first != 0 && region_next != 0 && openings != 0 && vla_first != 0 && vla_next != 0 && asm_statements != 0 &&
            allocation_first != 0 && allocation_next != 0 && vla_blocks != 0 && vla_saves != 0 && (input->cleanup_count == 0 || cleanup_parents != 0);
    if (!valid && program->status == CHECK_PROGRAM_OK)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    uint32_t body_count = 0;
    if (valid)
    {
        check_program_cases(program, parser, case_first, case_next, case_map, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
        // The children lists, source labels, stops and repeats are each body's, which check_control_begin fills.
        memset(token_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(initializers, 0xff, ((uint64_t)node_count + 1) * sizeof(uint32_t));
        memset(plan_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(vla_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(allocation_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(vla_blocks, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(vla_saves, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(asm_statements, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < input->asm_count; i += 1)
        {
            const CheckInputAsm* assembly = input->asm_statements + i;
            if (assembly->body != BUSTER_CHECK_NONE && assembly->statement < count)
            {
                asm_statements[assembly->statement] = i;
            }
        }
        memset(needed, 0, (uint64_t)count + 1);
        if (addressed != 0)
        {
            check_program_addressed(parser, input, addressed, BUSTER_SCALAR_PATH(options.scalar));
        }
        memset(seen, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(nodes, 0, ((uint64_t)node_count + 1) * sizeof(uint32_t));
        if (options.return_flow_work != NULL)
        {
            *options.return_flow_work += (uint64_t)node_count + 1;
        }
        memset(region_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
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
        body_count = check_program_openings(input, node_count, openings, initializers, BUSTER_SCALAR_PATH(options.scalar));
        for (uint32_t node = node_count; node-- > 0;)
        {
            const uint32_t token = input->graph.tokens[node];
            node_next[node] = token_first[token];
            token_first[token] = node;
        }
        bool compound_initializers = false;
        for (uint32_t i = options.initializers != 0 ? options.initializers->count : 0; i-- > 0;)
        {
            const uint32_t declaration = options.initializers->declaration_tokens[i];
            const bool compound = declaration < count && input->compound_objects[declaration];
            uint32_t token = declaration < count && input->initializer_begins[declaration] != 0 ? input->initializer_begins[declaration] - 1 : BUSTER_CHECK_NONE;
            uint32_t steps = 0;
            while (!compound && token < count && steps < count && parser->tree.roles[token] != PARSE_ROLE_TERMINATOR)
            {
                token = parser->tree.parents[token];
                steps += 1;
            }
            if (compound)
            {
                // A compound literal's lifetime belongs to its enclosing scope, but its initialization belongs
                // to evaluating its VALUE node. sizeof and unchosen branches must never initialize it.
                plan_next[i] = BUSTER_CHECK_NONE;
                compound_initializers |= !input->static_objects[declaration];
            }
            else if (token < count && steps < count)
            {
                plan_next[i] = plan_first[token];
                plan_first[token] = i;
            }
            else
            {
                check_program_fail(program, CHECK_PROGRAM_INVALID, declaration);
            }
        }
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
        check_program_allocations(program, parser, types, allocation_first, allocation_next, vla_blocks, vla_saves, BUSTER_SCALAR_PATH(options.scalar));
        for (uint32_t i = input->vla_count; i-- > 0;)
        {
            const CheckInputVla* vla = input->vlas + i;
            if (vla->evaluated && vla->declaration != BUSTER_CHECK_NONE)
            {
                uint32_t token = vla->scope_begin == vla->body ? vla->body : vla->declaration;
                uint32_t steps = 0;
                while (token < count && token != vla->body && steps++ < count && parser->tree.roles[token] != PARSE_ROLE_TERMINATOR)
                {
                    token = parser->tree.parents[token];
                }
                if (token < count && steps < count && vla->bound_node < node_count && input->graph.ops[vla->bound_node] == CHECK_OP_VLA_BOUND)
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
    }
    program->bodies = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramBody, body_count) : 0;
    const uint32_t switch_capacity = program->case_count / 4;
    program->switches = valid && switch_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, switch_capacity) : 0;
    program->regions = valid && input->region_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramRegion, input->region_count) : 0;
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
            (input->region_count == 0 || program->regions != 0) && program->object_symbols != 0 && program->object_declarations != 0 &&
            (asm_targets == 0 || program->asm_targets != 0);
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
        .initializers = initializers,
        .plan_first = plan_first,
        .plan_next = plan_next,
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
        .stops = stops,
        .repeats = repeats,
        .region_first = region_first,
        .region_next = region_next,
        .region_final = BUSTER_CHECK_NONE,
        .addressed = addressed,
        .cleanup_parents = cleanup_parents,
        .entries = entries,
        .tasks = tasks,
        .labels = labels,
        .label_positions = label_positions,
        .switches = program->switches,
        .switch_capacity = switch_capacity,
        .capacity = capacity,
    };
    uint32_t reference_count = 0;
    for (uint32_t b = 0; valid && b < body_count && program->status == CHECK_PROGRAM_OK; b += 1)
    {
        const uint32_t token = openings[b];
        const uint32_t declaration = input->body_declarations[token];
        const uint32_t start = writer.count;
        const uint32_t label_first = writer.label_position_count;
        check_control_build(&writer, token);
        // Its source labels' places, from their numbers, before check_program_return_flow takes the writer's labels.
        for (uint32_t l = label_first; l < writer.label_position_count; l += 1)
        {
            const uint32_t label = writer.label_positions[l];
            writer.label_positions[l] = label < writer.label_count ? writer.labels[label] : BUSTER_CHECK_NONE;
        }
        CheckProgramBody* body = &program->bodies[program->body_count++];
        *body = (CheckProgramBody)
        {
            .declaration = declaration,
            .opening = token,
            .object = input->objects[declaration],
            .first = start,
            .count = writer.count - start,
            .entry_count = writer.entry_count,
            .reference_first = reference_count,
            .label_first = label_first,
            .label_count = writer.label_position_count - label_first,
        };
        if (options.identifiers != 0)
        {
            const uint32_t name = parser->tokens.names[declaration];
            body->is_main = intern_length(options.identifiers, name) == 4 && memcmp(intern_bytes(options.identifiers, name), "main", 4) == 0;
        }
        check_program_return_flow(&writer, body, nodes, stack, options.return_flow_work);
        body->reference_count = check_program_references(program, entries + start, body->entry_count, program->body_count, seen, nodes, stack, reference_items, needed);
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
            uint64_t* sorted = c_bindings_sort(reference_items, reference_sort, body->reference_count, !BUSTER_SCALAR_PATH(options.scalar), varying);
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
        }
    }
    valid = valid && program->status == CHECK_PROGRAM_OK;
    program->builtin_count = valid ? check_program_builtins(&input->graph, nodes, NULL, BUSTER_SCALAR_PATH(options.scalar)) : 0;
    program->builtin_nodes = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->builtin_count) : 0;
    valid = valid && (program->builtin_count == 0 || program->builtin_nodes != 0);
    if (valid && program->builtin_count != 0)
    {
        check_program_builtins(&input->graph, nodes, program->builtin_nodes, BUSTER_SCALAR_PATH(options.scalar));
    }
    program->controls = valid && writer.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckControl, writer.count) : 0;
    program->label_positions = valid && writer.label_position_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, writer.label_position_count) : 0;
    valid = valid && (writer.count == 0 || program->controls != 0) && (writer.label_position_count == 0 || program->label_positions != 0);
    if (valid)
    {
        program->control_count = writer.count;
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
        memset(program->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
        memset(program->object_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        // The naked functions' first declarations with the attribute, in token order (CheckProgram.object_naked): the
        // column comes with the unit's first one. And the functions' target attributes, which check_program_targets reads.
        const bool naked_room = options.prepared_data != NULL ? check_program_static_prepared(program, options.prepared_data, needed, output, &target_events) :
                                                              check_program_static_objects(program, needed, output, BUSTER_SCALAR_PATH(options.scalar), &target_events);
        for (uint32_t i = 0; i < options.initializer_reference_count; i += 1)
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
        // GNU's alias, ifunc and weakref attributes (#147): the unit's entries, whose aliases' and ifuncs' objects are
        // needed, and whose referenced weakrefs take their targets' symbols as the symbols are numbered.
        const bool aliased = target_events.alias_count == 0 ||
                             check_program_aliases(program, &target_events, needed, BUSTER_SCALAR_PATH(options.scalar), output, scratch);
        valid = naked_room && aliased && check_program_alias_number(program, needed, program->alias_states, BUSTER_SCALAR_PATH(options.scalar), output);
    }
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_targets(program, &options, &target_events, output, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_attribute_keys(program, &target_events, BUSTER_SCALAR_PATH(options.scalar), output, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid && program->status == CHECK_PROGRAM_OK && !check_program_x86_diagnostics(program, stream, scratch))
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    if (!valid || program->status != CHECK_PROGRAM_OK)
    {
        const CheckProgramStatus status = program->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : program->status;
        const uint32_t token = status == CHECK_PROGRAM_INVALID ? check_program_diagnostic_location(parser, program->token) : program->token;
        const uint32_t position = program->diagnostic_position;
        arena_pop_to(output, mark);
        *program = (CheckProgram){ .status = status, .token = token, .diagnostic_position = position };
        valid = false;
    }
    else
    {
        program->output_bytes = output->length - mark;
    }
    arena_pop_to(scratch, temporary);
    arena_release(&controls);
    return valid;
}

#include "c_check_program_select.c"
#include "c_check_program_helpers.c"
#include "c_check_program_asm.c"
#include "c_check_program_lower.c"
#include "c_check_debug.c"
#include "c_check_clock.c"
#include "c_check_program_emit.c"

#endif
