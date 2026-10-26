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
    // The kinds from here on go to a label: their `target`.
    CHECK_CONTROL_BRANCH_FALSE,
    CHECK_CONTROL_BRANCH_TRUE,
    CHECK_CONTROL_JUMP,
} CheckControlKind;

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
    bool is_main;
};

typedef struct CheckProgramRegion CheckProgramRegion;
struct CheckProgramRegion
{
    uint32_t first;
    uint32_t count;
    uint32_t body;
    uint32_t node;
    uint32_t result;
};

typedef struct CheckProgramOptions CheckProgramOptions;
struct CheckProgramOptions
{
    const CheckInitializers* initializers;
    const CheckAsms* asms;
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

typedef struct CheckProgram CheckProgram;
struct CheckProgram
{
    const CheckInput* input;
    const CheckStream* stream;
    const TypeTable* types;
    const Parser* parser;
    const InternTable* identifiers;
    const CheckInitializers* initializers;
    const CheckAsms* asms;
    CheckControl* controls;
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
    uint32_t control_count;
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
    // CheckProgramOptions.unselected_operands and folded_ifs.
    bool unselected_operands;
    bool folded_ifs;
};

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
    const uint32_t* first;
    const uint32_t* last;
    const uint32_t* next;
    const uint32_t* previous;
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
    CheckControl* entries;
    CheckControlTask* tasks;
    uint32_t* labels;
    uint32_t count;
    uint32_t depth;
    uint32_t label_count;
    uint32_t capacity;
    uint32_t first_entry;
    uint32_t opening;
    uint32_t closing;
    uint32_t entry_count;
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
        if (count > 3 || count > program->input->graph.argument_counts[node])
        {
            check_program_fail(program, CHECK_PROGRAM_INVALID, program->input->graph.tokens[node]);
        }
        mask = count <= 3 ? (1u << count) - 1 : 0;
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
        const CheckValue* value = program->stream->values + node;
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

static void check_program_cases(CheckProgram* program, const Parser* parser, uint32_t* first, uint32_t* next, uint32_t* map, Arena* output, Arena* scratch)
{
    const CheckInput* input = program->input;
    const uint32_t tokens = input->token_count;
    uint32_t count = 0;
    for (uint32_t token = 0; token < tokens; token += 1)
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
    for (uint32_t token = 0; token < tokens && program->status != CHECK_PROGRAM_RESOURCE; token += 1)
    {
        if (input->deferred_tokens != 0 && input->deferred_tokens[token] != 0)
        {
            continue;
        }
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

static void check_control_expand(CheckControlWriter* writer, CheckControlTask task)
{
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const uint32_t token = task.token;
    const uint8_t role = parser->tree.roles[token];
    if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        // A block's variable-length arrays are released at its end, after its statements.
        if (role == PARSE_ROLE_CLOSE_BLOCK && writer->vla_blocks[token] != 0)
        {
            check_control_entry(writer, CHECK_CONTROL_STACK_RESTORE, token, BUSTER_CHECK_NONE, token);
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
            for (uint32_t c = writer->case_first[token]; c != BUSTER_CHECK_NONE; c = writer->case_next[c])
            {
                if (!writer->program->cases[c].fallback)
                {
                    check_control_entry(writer, CHECK_CONTROL_CASE, writer->program->cases[c].token, BUSTER_CHECK_NONE, c);
                }
            }
            check_control_entry(writer, CHECK_CONTROL_SWITCH, token, condition, BUSTER_CHECK_NONE);
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
            jump = child_role == PARSE_ROLE_RETURN ? child : jump;
            body = child_role == PARSE_ROLE_DO ? child : body;
            condition = child_role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
        }
        if (jump != BUSTER_CHECK_NONE)
        {
            const uint8_t kind = parser->tokens.kinds[jump];
            if (kind == PARSE_KIND_RETURN)
            {
                check_control_entry(writer, CHECK_CONTROL_RETURN, jump, input->token_nodes[jump], BUSTER_CHECK_NONE);
            }
            else if ((kind == PARSE_KIND_BREAK && task.stop != BUSTER_CHECK_NONE) || (kind == PARSE_KIND_CONTINUE && task.repeat != BUSTER_CHECK_NONE))
            {
                // The entries come out in the reverse of their pushes: the restore, then the jump.
                const bool stops = kind == PARSE_KIND_BREAK;
                check_control_entry(writer, CHECK_CONTROL_JUMP, jump, BUSTER_CHECK_NONE, stops ? task.stop : task.repeat);
                check_control_leave(writer, jump, BUSTER_CHECK_NONE, stops ? writer->stops : writer->repeats, stops ? task.stop : task.repeat);
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
            if (child > token)
            {
                check_control_statement(writer, child, task.stop, task.repeat);
            }
        }
        if (role == PARSE_ROLE_LABEL && writer->addressed != 0 && writer->addressed[token] != 0)
        {
            check_control_entry(writer, CHECK_CONTROL_LABEL, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        check_control_mark(writer, writer->source_labels[token]);
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
    memset(writer->source_labels + opening, 0xff, ((uint64_t)closing - opening + 1) * sizeof(uint32_t));
    for (uint32_t token = opening + 1; token < closing; token += 1)
    {
        const uint8_t role = writer->parser->tree.roles[token];
        if (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t label = check_control_label(writer);
            writer->source_labels[token] = label;
            if (role == PARSE_ROLE_CLOSE_CASE)
            {
                writer->program->cases[writer->case_map[token]].target = label;
            }
        }
    }
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
            for (uint32_t child = writer->last[source->block]; child != BUSTER_CHECK_NONE; child = writer->previous[child])
            {
                if (child != source->final_statement && writer->parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
                {
                    check_control_statement(writer, child, stop, repeat);
                }
            }
            check_control_drain(writer);
            check_control_end(writer, source->closing);
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
    for (uint32_t token = opening + 1; token < closing && writer->program->status == CHECK_PROGRAM_OK; token += 1)
    {
        if (writer->parser->tree.roles[token] == PARSE_ROLE_CLOSE_CASE)
        {
            CheckProgramCase* item = writer->program->cases + writer->case_map[token];
            const uint32_t label = item->target;
            if (label >= writer->label_count || writer->labels[label] == BUSTER_CHECK_NONE)
            {
                check_program_fail(writer->program, CHECK_PROGRAM_INVALID, token);
            }
            else
            {
                item->target = writer->labels[label];
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
            const CheckValue value = graph->leaves[node];
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
                const uint32_t selected = program->initializers != 0 ? program->initializers->declaration_plans[value.object - 1] : 0;
                if (selected == 0 || selected > program->initializers->count)
                {
                    check_program_fail(program, CHECK_PROGRAM_INVALID, value.object - 1);
                }
                else
                {
                    const CheckInitializer* plan = program->initializers->plans + selected - 1;
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
    const uint32_t count = input->token_count;
    memset(addressed, 0, (uint64_t)count + 1);
    uint32_t token = 0;
    for (; !scalar && count - token >= 64; token += 64)
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

static bool check_program(CheckProgram* program, const Parser* parser, const CheckInput* input, const CheckStream* stream,
                          const TypeTable* types, CheckProgramOptions options, Arena* output, Arena* scratch)
{
    *program = (CheckProgram){ .input = input, .stream = stream, .types = types, .parser = parser, .identifiers = options.identifiers,
                               .initializers = options.initializers, .asms = options.asms, .unselected_operands = options.unselected_operands,
                               .folded_ifs = options.folded_ifs, .token = BUSTER_CHECK_NONE, .diagnostic_position = BUSTER_CHECK_NONE };
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = input->token_count;
    const uint32_t node_count = input->graph.count;
    bool valid = output != scratch && count == parser->tokens.count && count < (UINT32_MAX - 16) / 8 &&
                 stream->status == CHECK_STATUS_OK && stream->count == node_count && stream->report_count == 0 && !input->resource && input->report_count == 0;
    valid = valid && (options.initializers == 0 || (options.initializers->status == CHECK_INIT_OK && options.initializers->token_count == count)) &&
            (input->asm_count == 0 || (options.asms != 0 && options.asms->status == CHECK_ASM_OK)) &&
            (options.initializer_reference_count == 0 || options.initializer_references != 0);
    program->object_count = count + 1;
    program->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    const uint32_t capacity = count * 8 + 16;
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
    uint32_t* labels = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, capacity) : 0;
    uint32_t* stack = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* nodes = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)node_count + 1) : 0;
    uint32_t* seen = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint64_t* reference_items = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + 1) : 0;
    uint64_t* reference_sort = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)node_count + 1) : 0;
    uint8_t* needed = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* addressed = valid && input->label_address_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    CheckControl* entries = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckControl, capacity) : 0;
    CheckControlTask* tasks = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckControlTask, capacity) : 0;
    valid = valid && first != 0 && last != 0 && next != 0 && previous != 0 && token_first != 0 && node_next != 0 && initializers != 0 && labels != 0 &&
            stack != 0 && nodes != 0 && seen != 0 && needed != 0 && (input->label_address_count == 0 || addressed != 0) && entries != 0 && tasks != 0 && plan_first != 0 &&
            (options.initializers == 0 || plan_next != 0) &&
            case_first != 0 && case_next != 0 && case_map != 0 && source_labels != 0 && reference_items != 0 && reference_sort != 0 &&
            stops != 0 && repeats != 0 && region_first != 0 && region_next != 0 && vla_first != 0 && vla_next != 0 && asm_statements != 0 &&
            allocation_first != 0 && allocation_next != 0 && vla_blocks != 0 && vla_saves != 0;
    if (!valid && program->status == CHECK_PROGRAM_OK)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
    }
    uint32_t body_count = 0;
    if (valid)
    {
        check_program_cases(program, parser, case_first, case_next, case_map, output, scratch);
        memset(first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(last, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(next, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(previous, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
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
            check_program_addressed(parser, input, addressed, options.scalar);
        }
        memset(seen, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(nodes, 0, ((uint64_t)node_count + 1) * sizeof(uint32_t));
        memset(stops, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(repeats, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
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
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t parent = parser->tree.parents[token];
            if (parent < count)
            {
                previous[token] = last[parent];
                if (last[parent] != BUSTER_CHECK_NONE)
                {
                    next[last[parent]] = token;
                }
                else
                {
                    first[parent] = token;
                }
                last[parent] = token;
            }
            body_count += input->body_declarations[token] != BUSTER_CHECK_NONE && (input->deferred_tokens == 0 || input->deferred_tokens[token] == 0);
            if (input->initializer_nodes[token] < node_count)
            {
                initializers[input->initializer_nodes[token]] = token;
            }
        }
        for (uint32_t node = node_count; node-- > 0;)
        {
            const uint32_t token = input->graph.tokens[node];
            node_next[node] = token_first[token];
            token_first[token] = node;
        }
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
        // The variable-length arrays' objects, by their statements, and each block's first: from the last token back,
        // so that the lists are in declaration order and a block's last write is its first array.
        for (uint32_t token = count; token-- > 0;)
        {
            if (input->declarations[token] == token && input->declaration_bodies[token] < count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
                !input->static_objects[token] && check_action_runtime_size(types, input->declared_types[token]))
            {
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
        }
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
    valid = valid && (body_count == 0 || program->bodies != 0) && (input->region_count == 0 || program->regions != 0) &&
            program->object_symbols != 0 && program->object_declarations != 0 && (asm_targets == 0 || program->asm_targets != 0);
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
        .addressed = addressed,
        .entries = entries,
        .tasks = tasks,
        .labels = labels,
        .capacity = capacity,
    };
    uint32_t reference_count = 0;
    for (uint32_t token = 0; valid && token < count && program->status == CHECK_PROGRAM_OK; token += 1)
    {
        const uint32_t declaration = input->body_declarations[token];
        if (declaration != BUSTER_CHECK_NONE && (input->deferred_tokens == 0 || input->deferred_tokens[token] == 0))
        {
            const uint32_t start = writer.count;
            check_control_build(&writer, token);
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
            };
            if (options.identifiers != 0)
            {
                const uint32_t name = parser->tokens.names[declaration];
                body->is_main = intern_length(options.identifiers, name) == 4 && memcmp(intern_bytes(options.identifiers, name), "main", 4) == 0;
            }
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
                uint64_t* sorted = c_bindings_sort(reference_items, reference_sort, body->reference_count, !options.scalar, varying);
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
    }
    valid = valid && program->status == CHECK_PROGRAM_OK;
    for (uint32_t node = 0; valid && node < node_count; node += 1)
    {
        const CheckOp op = input->graph.ops[node];
        program->builtin_count += nodes[node] != 0 && (op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW ||
                                  op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
                                  op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST);
    }
    program->builtin_nodes = valid && program->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->builtin_count) : 0;
    valid = valid && (program->builtin_count == 0 || program->builtin_nodes != 0);
    uint32_t builtin = 0;
    for (uint32_t node = 0; valid && node < node_count; node += 1)
    {
        const CheckOp op = input->graph.ops[node];
        if (nodes[node] != 0 && (op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW ||
            op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT || op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC ||
            op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST))
        {
            program->builtin_nodes[builtin++] = node;
        }
    }
    program->controls = valid && writer.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckControl, writer.count) : 0;
    valid = valid && (writer.count == 0 || program->controls != 0);
    if (valid)
    {
        program->control_count = writer.count;
        program->reference_count = reference_count;
        if (writer.count != 0)
        {
            memcpy(program->controls, entries, (uint64_t)writer.count * sizeof(CheckControl));
        }
        memset(program->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
        memset(program->object_declarations, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t object = input->objects[token];
            if (input->declarations[token] == token && input->static_objects[token] && object != 0 && object <= count)
            {
                program->object_declarations[object] = token;
                needed[object] |= input->defined[token] || input->tentative[token];
            }
        }
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
        for (uint32_t object = 1; object <= count; object += 1)
        {
            if (needed[object])
            {
                program->object_symbols[object] = ++program->symbol_count;
            }
        }
        program->symbol_objects = program->symbol_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->symbol_count) : 0;
        valid = program->symbol_count == 0 || program->symbol_objects != 0;
        for (uint32_t object = 1; valid && object <= count; object += 1)
        {
            const SymbolId symbol = program->object_symbols[object];
            if (symbol != 0)
            {
                program->symbol_objects[symbol - 1] = object;
            }
        }
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
    return valid;
}

#include "c_check_program_select.c"
#include "c_check_program_helpers.c"
#include "c_check_program_lower.c"
#include "c_check_debug.c"
#include "c_check_clock.c"
#include "c_check_program_emit.c"

#endif
