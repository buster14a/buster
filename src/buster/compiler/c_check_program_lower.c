// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked executable roots to Tier0Function. This separate boundary may refuse a machine class while leaving
// CheckProgram valid. Explicit worklists preserve evaluation and side effects; graph postorder is never mistaken
// for executable order. ABI signatures from every body and call are assigned together after type classification.
// Source constant proofs remain intact; FP_ENVIRONMENT requires runtime evaluation. Math leaves transport raw
// IEEE bits, and errno-enabled leaves receive the target libc accessor's real int pointer. Runtime dependencies
// append symbols after the source/helper map and do not invent source declarations or change binding identities.

typedef struct CheckProgramLowered CheckProgramLowered;
struct CheckProgramLowered
{
    Tier0Function* functions;
    CheckProgramHelpers helpers;
    SymbolId errno_symbol;
    TypeId errno_pointer_type;
    // Each function's entries' source tokens (the parser's), when check_program_lower is asked for them: entry e's, the
    // token of the expression, statement or initializer whose lowering wrote it (a function's first entries the
    // declaration's, its last its closing brace's), for source rows (#395). Null otherwise.
    uint32_t** entry_tokens;
    uint32_t runtime_symbol_count;
    uint32_t count;
    CheckProgramStatus status;
    uint32_t token;
    uint32_t backend_node;
    // What a failure was about, for its diagnostic: the feature CHECK_PROGRAM_UNSUPPORTED names, or the rule of
    // tier0_validate's that a lowered function broke (CHECK_PROGRAM_INVALID). NULL when nothing names it.
    const char* reason;
    uint64_t entries;
    uint64_t output_bytes;
};

typedef enum CheckLowerTaskKind
{
    CHECK_LOWER_EXPRESSION,
    CHECK_LOWER_INSTRUCTION,
    CHECK_LOWER_CONVERSION,
    CHECK_LOWER_LABEL,
    CHECK_LOWER_CONTROL,
    CHECK_LOWER_BUILTIN,
    CHECK_LOWER_BUILTIN_MISC,
    CHECK_LOWER_BUILTIN_FLOAT,
    CHECK_LOWER_BUILTIN_FLOAT_STRICT,
    CHECK_LOWER_CONSTANT,
    CHECK_LOWER_ADDRESS,
    CHECK_LOWER_QUERY,
    CHECK_LOWER_TERMINATE,
    // `value` bytes from the address on top to the one under it, which stays (`address`) or not.
    CHECK_LOWER_COPY,
    // An initializer's part of zeros or of bytes (CHECK_INIT_CLEAR or CHECK_INIT_BYTES): `node` its plan, `type` the
    // part, `value` the frame object it writes.
    CHECK_LOWER_FILL,
    // Frame object `value` cleared whole, `type` bytes, before an initializer's parts: what they don't write is zero.
    CHECK_LOWER_CLEAR,
    // A bit-field's value from the address of its first byte (check_lower_bit_read), `type` its packed place.
    CHECK_LOWER_BIT_READ,
    // The value on the stack stored into a bit-field (check_lower_bit_store): `type` its packed place, `value` the local
    // that holds the address of its first byte. The field's new value stays unless `discard`.
    CHECK_LOWER_BIT_STORE,
    // va_start, va_arg, va_copy or va_end's recipe (CBuiltinVaLowerCall `node`), `type` its value's.
    CHECK_LOWER_VARARGS,
    // The size of `type`, a variable-length array's, computed from its extents (an I64: a pointer's runtime stride).
    CHECK_LOWER_SIZE,
} CheckLowerTaskKind;

typedef struct CheckLowerTask CheckLowerTask;
struct CheckLowerTask
{
    CheckLowerTaskKind kind;
    uint32_t node;
    TypeId type;
    // The source token current when the task was pushed, which its entries take (CheckLowerWriter.source).
    uint32_t source;
    uint64_t value;
    bool address;
    bool initialized;
    // An expression whose value nobody reads (a statement's): an aggregate's assignment then leaves no address.
    bool discard;
};

typedef struct CheckLowerAbi CheckLowerAbi;
struct CheckLowerAbi
{
    TypeId* results;
    TypeId* arguments;
    uint32_t* counts;
    uint32_t* named;
    uint32_t* first;
    uint8_t* variadic;
    uint32_t count;
    uint32_t argument_count;
    uint32_t capacity;
    uint32_t argument_capacity;
};

typedef struct CheckLowerWriter CheckLowerWriter;
struct CheckLowerWriter
{
    const CheckProgram* program;
    CheckProgramLowered* output;
    CheckLowerAbi* abi;
    CheckLowerTask* tasks;
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    // Each entry's source token (CheckProgramLowered.entry_tokens).
    uint32_t* tokens;
    uint32_t* labels;
    uint32_t* label_heads;
    uint32_t* stack_parents;
    uint8_t* stack_classes;
    uint32_t* local_map;
    uint32_t* switch_locals;
    uint32_t* extent_locals;
    // Each variable-length array's identity plus one, by its bound's first token (0 for none), for its extent's local;
    // each block's local that holds the stack's position its arrays came after (CHECK_CONTROL_STACK_SAVE's), by its
    // closing token; and each local's role as a variable-length array's address (BUSTER_CHECK_LOWER_VLA_*).
    uint32_t* vla_identities;
    uint32_t* save_locals;
    uint8_t* indirect;
    uint32_t* initializer_declarations;
    uint8_t* local_classes;
    uint8_t* local_flags;
    // Tier0Function's optional object columns: a frame object's size (0 for a value's local) and alignment's log2.
    uint32_t* local_sizes;
    uint8_t* local_alignments;
    SymbolId* call_targets;
    uint32_t* call_first;
    uint32_t* call_counts;
    uint8_t* call_variadic;
    uint8_t* argument_classes;
    // Structs and unions by value (#137): each argument's bytes for an aggregate (0 for a value) and each call's result
    // object (1 + its local, or 0), Tier0Function's optional columns, which a function takes when it has one
    // (`aggregates`).
    uint32_t* argument_sizes;
    uint32_t* call_result_objects;
    CBuiltinLowerCall* builtins;
    CBuiltinMiscLowerCall* miscellaneous;
    CBuiltinFloatLowerCall* floating;
    uint32_t count;
    uint32_t depth;
    uint32_t capacity;
    uint32_t label_count;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t call_count;
    uint32_t argument_count;
    uint32_t builtin_count;
    uint32_t miscellaneous_count;
    uint32_t floating_count;
    uint32_t token;
    // The token whose lowering writes the next entries: an expression's node's, a statement's or an initializer part's,
    // saved with each task pushed and restored when it runs, so that an operation's entry, written after its operands',
    // takes its own node's token. `token`, which diagnostics name, is the last one set.
    uint32_t source;
    uint32_t stack_head;
    uint32_t stack_count;
    bool reachable;
    // Whether the function has a frame object, whose columns it then takes.
    bool objects;
    bool aggregates;
    const CheckProgramBody* body;
    TypeId result;
    // Every type's classification, made before the bodies, so that a function's own result (in registers, or through
    // the hidden pointer that is then local `hidden`, parameter 0) decides its returns.
    const AbiTable* abi_table;
    uint32_t hidden;
    // A variadic definition's (#137): va_start's offsets past the named registers, from the function's own call as
    // abi_assign gives it, and the anchors of its incoming frame (the register save area and the overflow area), made at
    // its first va_start.
    bool variadic;
    uint32_t gp_offset;
    uint32_t fp_offset;
    uint32_t save_anchor;
    uint32_t overflow_anchor;
    CBuiltinVaLowerCall* varargs;
    uint32_t varargs_count;
    bool scalar;
    bool track;
};

// A local's role as a variable-length array's address (CheckLowerWriter.indirect): none; its array's, declared, which
// its object's references read where an array's take LOCAL_ADDRESS; and allocated, once its CHECK_CONTROL_ALLOCATE
// came, which every declared array's must have.
#define BUSTER_CHECK_LOWER_VLA_NONE 0
#define BUSTER_CHECK_LOWER_VLA_DECLARED 1
#define BUSTER_CHECK_LOWER_VLA_ALLOCATED 2

static void check_lower_fail(CheckLowerWriter* writer, CheckProgramStatus status)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->status = status;
        writer->output->token = writer->token;
    }
}

// A construct the backend doesn't take yet, named for the diagnostic (#145: a program outside the subset fails with
// the feature it needs, never with wrong code).
static void check_lower_unsupported(CheckLowerWriter* writer, const char* feature)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->reason = feature;
    }
    check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
}

static uint8_t check_lower_class(const TypeTable* types, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    uint8_t result = UINT8_MAX;
    uint32_t bits = 0;
    bool sign = false;
    if (base == BUSTER_TYPE_VOID)
    {
        result = BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind))
    {
        result = BUSTER_TYPED_CLASS_U64;
    }
    else if (base == BUSTER_TYPE_FLOAT || base == BUSTER_TYPE_DOUBLE)
    {
        result = base == BUSTER_TYPE_FLOAT ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64;
    }
    else if (check_action_integer_type(types, base, &bits, &sign))
    {
        result = bits == 1 ? BUSTER_TYPED_CLASS_U8 : bits == 8 ? BUSTER_TYPED_CLASS_I8 : bits == 16 ? BUSTER_TYPED_CLASS_I16 :
                 bits == 32 ? BUSTER_TYPED_CLASS_I32 : bits == 64 ? BUSTER_TYPED_CLASS_I64 : UINT8_MAX;
        if (bits != 1 && result != UINT8_MAX && !sign)
        {
            result += 1;
        }
    }
    return result;
}

// Whether a type is a struct or a union: an aggregate value, which the stream carries as its object's address. Each
// consumer copies the bytes from there (an assignment, an initializer) or reads a member through it.
static bool check_lower_aggregate(const TypeTable* types, TypeId type)
{
    const uint8_t kind = type_kind(types, type_strip_aligned(types, type));
    return kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
}

// An expression's class on the stack: check_lower_class's, or U64 for an aggregate (its address).
static uint8_t check_lower_value_class(const TypeTable* types, TypeId type)
{
    return check_lower_aggregate(types, type) ? BUSTER_TYPED_CLASS_U64 : check_lower_class(types, type);
}

// What a value of a type check_lower_class has no class for needs, named for the diagnostic. An aggregate's value is
// its address, so the type of one that reaches here is passed or returned by value.
static const char* check_lower_type_feature(const TypeTable* types, TypeId type)
{
    const TypeId base = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, base);
    const char* feature = "values of this type";
    if (base == BUSTER_TYPE_LONG_DOUBLE)
    {
        feature = "long double";
    }
    else if (base == BUSTER_TYPE_INT128 || base == BUSTER_TYPE_UINT128)
    {
        feature = "__int128";
    }
    else if (base == BUSTER_TYPE_FLOAT16 || base == BUSTER_TYPE_FLOAT128 || kind == BUSTER_TYPE_KIND_FLOATING)
    {
        feature = "floating types other than float and double";
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        feature = "_Complex";
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        feature = "_BitInt";
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        feature = "vector types";
    }
    else if (kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        feature = "_Atomic";
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        feature = "structs and unions passed or returned by value";
    }
    else if (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
    {
        feature = "variable-length arrays";
    }
    return feature;
}

// Why a struct or union can't be passed or returned by value, or NULL. Tier 0 takes every location abi_assign gives one
// as an argument or a parameter, of up to BUSTER_TIER0_MAX_ARGUMENT_BYTES aligned to 16 at most, and every result in
// registers but an x87 one.
static const char* check_lower_by_value(const CheckLowerWriter* writer, TypeId type, bool result)
{
    const TypeTable* types = writer->program->types;
    const AbiReturn returned = writer->abi_table->results[type_index(type)];
    const bool x87 = ((returned.registers[0] | returned.registers[1]) & BUSTER_ABI_REGISTER_X87) != 0;
    const char* feature = NULL;
    if (type_alignment_log2(types, type) > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
    {
        feature = "structs and unions aligned past 16 bytes, by value";
    }
    else if (type_size(types, type) > BUSTER_TIER0_MAX_ARGUMENT_BYTES)
    {
        feature = "structs and unions larger than 64 KB, by value";
    }
    else if (result && returned.kind == BUSTER_ABI_RESULT_REGISTERS && !tier0_aggregate_result_valid(returned, (uint32_t)type_size(types, type)))
    {
        feature = x87 ? "long double" : "structs and unions returned in these registers";
    }
    return feature;
}

// Why an argument or a parameter of this type can't be passed, or NULL: a value of a class, or a struct or a union.
static const char* check_lower_passed(const CheckLowerWriter* writer, TypeId type)
{
    const TypeTable* types = writer->program->types;
    const char* feature = NULL;
    if (check_lower_aggregate(types, type))
    {
        feature = check_lower_by_value(writer, type, false);
    }
    else if (check_lower_class(types, type) == UINT8_MAX)
    {
        feature = check_lower_type_feature(types, type);
    }
    return feature;
}

// A pointer's stride: its pointee's size, or 0 for a variable-length array's, which its extents give at run time
// (check_lower_scale and CHECK_LOWER_SIZE).
static uint64_t check_lower_stride(CheckLowerWriter* writer, TypeId pointer)
{
    const TypeTable* types = writer->program->types;
    const CheckAction action = { .extensions = writer->program->input->graph.extensions };
    const uint64_t size = check_action_pointer_stride(types, pointer, &action);
    if (size == 0 && !check_action_runtime_size(types, type_element(types, type_strip_aligned(types, pointer))))
    {
        check_lower_unsupported(writer, "pointer arithmetic over types of no size");
    }
    return size;
}

// Persistent stack heads let a forward branch retain its complete typed stack without copying it at every
// label. This is bookkeeping for spilling live outer operands around statement regions; Tier0 still validates
// the finished stream independently, including both sides of every join.
static void check_lower_track(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    if (!writer->reachable)
    {
        writer->stack_head = 0;
        writer->reachable = true;
    }
    uint32_t pop = 0;
    bool push = false;
    if (kind <= BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_STACK_SAVE)
    {
        push = true;
    }
    else if (kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT || kind == BUSTER_TYPED_CONVERT || kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_ALLOCA)
    {
        pop = 1;
        push = true;
    }
    else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
    {
        pop = 2;
        push = kind == BUSTER_TYPED_STORE;
    }
    else if (kind >= BUSTER_TYPED_STORE_LOCAL && kind <= BUSTER_TYPED_SET_GLOBAL)
    {
        pop = 1;
        push = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL;
    }
    else if (kind == BUSTER_TYPED_CALL)
    {
        // An indirect call (callee 0) pops the address it calls too, which the stream pushed before the arguments.
        pop = value < writer->call_count ? writer->call_counts[value] + (writer->call_targets[value] == 0) : 0;
        push = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_STACK_RESTORE)
    {
        pop = 1;
    }
    else if (kind == BUSTER_TYPED_COPY)
    {
        pop = 2;
    }
    else if (kind == BUSTER_TYPED_RETURN)
    {
        pop = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind != BUSTER_TYPED_JUMP && kind != BUSTER_TYPED_JUMP_VALUE)
    {
        pop = 2;
        push = true;
    }
    for (uint32_t i = 0; i < pop; i += 1)
    {
        if (writer->stack_head == 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            writer->stack_head = writer->stack_parents[writer->stack_head];
        }
    }
    if (push && writer->stack_count + 1 < writer->capacity)
    {
        const uint32_t entry = ++writer->stack_count;
        writer->stack_parents[entry] = writer->stack_head;
        writer->stack_classes[entry] = class;
        writer->stack_head = entry;
    }
    else if (push)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE)
    {
        if (value < writer->label_count && writer->label_heads[value] == BUSTER_CHECK_NONE)
        {
            writer->label_heads[value] = writer->stack_head;
        }
        writer->reachable = kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE;
    }
    else if (kind == BUSTER_TYPED_RETURN)
    {
        if (writer->stack_head != 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        writer->reachable = false;
    }
}

static void check_lower_place(CheckLowerWriter* writer, uint32_t label)
{
    writer->labels[label] = writer->count;
    if (writer->track)
    {
        if (!writer->reachable)
        {
            writer->stack_head = writer->label_heads[label] != BUSTER_CHECK_NONE ? writer->label_heads[label] : 0;
        }
        if (writer->label_heads[label] == BUSTER_CHECK_NONE)
        {
            writer->label_heads[label] = writer->stack_head;
        }
        writer->reachable = true;
    }
}

static void check_lower_emit(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    if (class >= BUSTER_TYPED_CLASSES)
    {
        check_lower_unsupported(writer, "values of this type");
    }
    else if (writer->count >= writer->capacity || writer->count >= BUSTER_TIER0_MAX_ENTRIES)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const bool operand = (kind >= BUSTER_TYPED_NEG && kind <= BUSTER_TYPED_SET_GLOBAL) ||
                             (kind >= BUSTER_TYPED_DISCARD && kind <= BUSTER_TYPED_BRANCH_TRUE) || kind == BUSTER_TYPED_JUMP_VALUE ||
                             (kind == BUSTER_TYPED_RETURN && class != BUSTER_TYPED_CLASS_VOID) || kind == BUSTER_TYPED_ALLOCA || kind == BUSTER_TYPED_STACK_RESTORE;
        const uint8_t top = writer->stack_head != 0 ? writer->stack_classes[writer->stack_head] : BUSTER_TYPED_CLASS_VOID;
        if (writer->track && writer->reachable && operand && top != BUSTER_TYPED_CLASS_VOID && writer->count != 0 && writer->classes[writer->count - 1] != top)
        {
            // Tier0's operand-class column is the preceding entry's class. A void call or a spill of a different
            // stack class can interrupt that adjacency. This cancelling pair restores the contract without
            // reading or changing the pending value, and works for narrow, floating and pointer classes alike.
            if (writer->count + 2 >= writer->capacity || writer->count + 2 >= BUSTER_TIER0_MAX_ENTRIES)
            {
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
            }
            else
            {
                writer->kinds[writer->count] = BUSTER_TYPED_CONST;
                writer->classes[writer->count] = top;
                writer->tokens[writer->count] = writer->source;
                writer->values[writer->count++] = 0;
                writer->kinds[writer->count] = BUSTER_TYPED_DISCARD;
                writer->classes[writer->count] = top;
                writer->tokens[writer->count] = writer->source;
                writer->values[writer->count++] = 0;
            }
        }
        if (writer->output->status == CHECK_PROGRAM_OK)
        {
            if (writer->track)
            {
                check_lower_track(writer, kind, class, value);
            }
            writer->kinds[writer->count] = kind;
            writer->classes[writer->count] = class;
            writer->values[writer->count] = value;
            writer->tokens[writer->count] = writer->source;
            writer->count += 1;
        }
    }
}

static void check_lower_push(CheckLowerWriter* writer, CheckLowerTask task)
{
    if (writer->depth < writer->capacity)
    {
        task.source = writer->source;
        writer->tasks[writer->depth++] = task;
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
}

static void check_lower_instruction(CheckLowerWriter* writer, uint8_t kind, uint8_t class, uint64_t value)
{
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_INSTRUCTION, .node = kind, .type = class, .value = value });
}

static void check_lower_convert(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    if (to != 0 && from != to)
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONVERSION, .node = from, .type = to });
    }
}

static void check_lower_expression(CheckLowerWriter* writer, uint32_t node, bool address, TypeId target)
{
    const CheckProgram* program = writer->program;
    if (node < program->stream->count && program->stream->values[node].overload_head == 0)
    {
        // A statement's assignment of an aggregate copies the bytes and leaves no address to discard, and one into a
        // bit-field (an update's too) leaves no value.
        const CheckOp op = program->input->graph.ops[node];
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const uint32_t destination = program->input->graph.operand0[node];
        const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) &&
                               destination < program->stream->count && (program->stream->values[destination].flags & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool discard = !address && target == BUSTER_TYPE_VOID &&
                             ((store && check_lower_aggregate(program->types, program->stream->values[node].type)) || bit_field);
        if (!address && !discard)
        {
            check_lower_convert(writer, program->stream->values[node].type, target);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address, .discard = discard });
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
}

// Pending overload leaves carry a lexical placeholder. Only the checked consuming edge supplies an address.
static void check_lower_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, bool address, TypeId target)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckOverloadResult* selected = check_program_overload_address(program, node, operand);
    if (selected != NULL)
    {
        if (!check_program_overload_valid(program, selected))
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if ((selected->flags & BUSTER_CHECK_OVERLOADABLE) != 0)
        {
            check_lower_unsupported(writer, "addresses of overloadable functions");
        }
        else
        {
            if (!address)
            {
                check_lower_convert(writer, selected->type, target);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = selected->object });
        }
    }
    else
    {
        const uint32_t child = operand == 0 ? graph->operand0[node] : operand == 1 ? graph->operand1[node] : operand == 2 ? graph->operand2[node] :
                               operand - 3 < graph->argument_counts[node] ? graph->argument_nodes[graph->argument_offsets[node] + operand - 3] : BUSTER_CHECK_NONE;
        check_lower_expression(writer, child, address, target);
    }
}

static uint32_t check_lower_label(CheckLowerWriter* writer)
{
    const uint32_t label = writer->label_count;
    if (label < writer->capacity)
    {
        writer->labels[label] = BUSTER_CHECK_NONE;
        if (writer->track)
        {
            writer->label_heads[label] = BUSTER_CHECK_NONE;
        }
        writer->label_count += 1;
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return label;
}

static void check_lower_mark(CheckLowerWriter* writer, uint32_t label)
{
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_LABEL, .node = label });
}

static uint32_t check_lower_temporary(CheckLowerWriter* writer, uint8_t class)
{
    const uint32_t local = writer->local_count;
    if (local < writer->local_capacity && local < BUSTER_TIER0_MAX_LOCALS && class < BUSTER_TYPED_CLASSES && class != BUSTER_TYPED_CLASS_VOID)
    {
        writer->local_classes[local] = class;
        writer->local_flags[local] = 0;
        writer->local_sizes[local] = 0;
        writer->local_alignments[local] = 0;
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_NONE;
        writer->local_count += 1;
    }
    else if (class >= BUSTER_TYPED_CLASSES)
    {
        check_lower_unsupported(writer, "values of this type");
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return local;
}

// A frame object (an array, a struct or a union, or a value that must live in memory): a local of class VOID, reached
// only through LOCAL_ADDRESS. One of no size (a GNU empty struct, a zero-length array) still takes a byte of its own.
static uint32_t check_lower_object(CheckLowerWriter* writer, uint64_t size, uint32_t alignment_log2)
{
    const uint32_t local = writer->local_count;
    if (size > BUSTER_TIER0_MAX_OBJECT_BYTES || alignment_log2 > BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2)
    {
        check_lower_unsupported(writer, "local objects aligned past 16 bytes or larger than 1 GB");
    }
    else if (local < writer->local_capacity && local < BUSTER_TIER0_MAX_LOCALS)
    {
        writer->local_classes[local] = BUSTER_TYPED_CLASS_VOID;
        writer->local_flags[local] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        writer->local_sizes[local] = size != 0 ? (uint32_t)size : 1;
        writer->local_alignments[local] = (uint8_t)alignment_log2;
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_NONE;
        writer->local_count += 1;
        writer->objects = true;
    }
    else
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    return local;
}

static uint32_t check_lower_addressed_temporary(CheckLowerWriter* writer, uint8_t class)
{
    const uint32_t local = check_lower_temporary(writer, class);
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
    }
    return local;
}

static void check_lower_address(CheckLowerWriter* writer, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else if (object >= writer->program->object_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const uint32_t local = writer->local_map[object];
        const SymbolId symbol = writer->program->object_symbols[object];
        if (local != BUSTER_CHECK_NONE && writer->indirect[local] != BUSTER_CHECK_LOWER_VLA_NONE)
        {
            // A variable-length array's address, which its local holds.
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (local != BUSTER_CHECK_NONE)
        {
            writer->local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (symbol != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, symbol);
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        if (offset != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
}

// The address on top of the stack moved `offset` bytes on.
static void check_lower_offset(CheckLowerWriter* writer, uint64_t offset)
{
    if (offset != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
        check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    }
}

// The size in bytes of `type`, an array with variable-length dimensions, onto the stack as `class` (U64, or I64 for a
// stride): the extents' locals, which their bounds set where they were declared (VLA_BOUND), multiplied, then by the
// product of the constant dimensions and the element's size. A variable-length dimension's type holds its bound's first
// token plus one, by which its array (and its extent's local) is found.
static void check_lower_vla_size(CheckLowerWriter* writer, TypeId type, uint8_t class)
{
    const CheckProgram* program = writer->program;
    const TypeTable* types = program->types;
    uint64_t factor = 1;
    uint32_t extents = 0;
    bool valid = true;
    TypeId at = type_strip_aligned(types, type);
    for (uint32_t depth = 0; valid && type_kind_is_array(type_kind(types, at)) && depth < types->types.count; depth += 1)
    {
        const uint64_t extent = types->types.extent[type_index(at)];
        if (type_kind(types, at) == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
        {
            const uint32_t identity = extent != 0 && extent <= program->input->token_count ? writer->vla_identities[extent - 1] : 0;
            const uint32_t local = identity != 0 ? writer->extent_locals[identity - 1] : BUSTER_CHECK_NONE;
            valid = local != BUSTER_CHECK_NONE;
            if (valid)
            {
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
                if (extents != 0)
                {
                    check_lower_emit(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
                }
                extents += 1;
            }
        }
        else
        {
            factor *= extent;
        }
        at = type_strip_aligned(types, type_element(types, at));
    }
    factor *= type_size(types, at);
    if (valid && extents != 0)
    {
        if (factor != 1)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, factor);
            check_lower_emit(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
    else
    {
        // A dimension whose bound this function hasn't evaluated: the checker's scopes make that impossible.
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
}

// The index on top of the stack scaled by `pointer`'s stride (`size`, check_lower_stride's), as instructions pushed
// after the index's: a PTR_ADD by the stride, or for a pointer to a variable-length array, by 1 after the index's
// product with its size, computed from its extents (CHECK_LOWER_SIZE).
static void check_lower_scale(CheckLowerWriter* writer, TypeId pointer, uint64_t size)
{
    const TypeTable* types = writer->program->types;
    check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, size != 0 ? size : 1);
    if (size == 0)
    {
        check_lower_instruction(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_I64, 0);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_SIZE, .type = type_element(types, type_strip_aligned(types, pointer)) });
    }
}

// `size` bytes copied from the source's address on top of the stack to the destination's under it, which stays when
// `keep` (an assignment's value): one COPY, or COPYs of up to BUSTER_TIER0_MAX_COPY_BYTES each, the source's address
// in a temporary between them.
static void check_lower_copy(CheckLowerWriter* writer, uint64_t size, bool keep)
{
    if (size == 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        if (!keep)
        {
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    else if (!keep && size <= BUSTER_TIER0_MAX_COPY_BYTES)
    {
        check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
    }
    else
    {
        const uint32_t source = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, source);
        for (uint64_t offset = 0; offset < size && writer->output->status == CHECK_PROGRAM_OK; offset += BUSTER_TIER0_MAX_COPY_BYTES)
        {
            const uint64_t piece = size - offset < BUSTER_TIER0_MAX_COPY_BYTES ? size - offset : BUSTER_TIER0_MAX_COPY_BYTES;
            if (keep || offset + piece != size)
            {
                check_lower_emit(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
            }
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, source);
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, piece);
        }
    }
}

// Zeros past this many bytes are written by a loop of 8-byte stores rather than store by store.
#define BUSTER_CHECK_LOWER_FILL_UNROLLED 128

// The largest access of 8, 4, 2 or 1 bytes that `left` bytes take, and its class: runs of bytes are written (and a
// bit-field's bytes read) as such pieces, largest first.
static uint32_t check_lower_piece(uint64_t left)
{
    return left >= 8 ? 8 : left >= 4 ? 4 : left >= 2 ? 2 : 1;
}

static uint8_t check_lower_piece_class(uint32_t piece)
{
    return piece == 8 ? BUSTER_TYPED_CLASS_U64 : piece == 4 ? BUSTER_TYPED_CLASS_U32 : piece == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
}

// An initializer's zeros (CHECK_INIT_CLEAR) or bytes (CHECK_INIT_BYTES) written into frame object `object`: stores of 8
// bytes, then 4, 2 and 1 for the rest, the bytes as constants of the stores' classes (the host's byte order is the
// target's); past BUSTER_CHECK_LOWER_FILL_UNROLLED bytes, the zeros' words by a loop.
static void check_lower_fill(CheckLowerWriter* writer, const CheckInitPart* part, uint32_t object)
{
    const uint64_t begin = part->bit_offset / 8;
    const uint64_t size = part->byte_count;
    if (part->bit_offset % 8 != 0 || part->bit_width != 0 || (part->kind == CHECK_INIT_BYTES && size != 0 && part->bytes == NULL))
    {
        check_lower_unsupported(writer, "bit-field initializers in local objects");
    }
    else
    {
        uint64_t offset = 0;
        if (part->kind == CHECK_INIT_CLEAR && size > BUSTER_CHECK_LOWER_FILL_UNROLLED)
        {
            const uint32_t index = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            const uint32_t top = check_lower_label(writer);
            const uint32_t end = check_lower_label(writer);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_place(writer, top);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, size / 8);
            check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, end);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            check_lower_offset(writer, begin);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_emit(writer, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, index);
            check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, top);
            check_lower_place(writer, end);
            offset = size / 8 * 8;
        }
        while (offset < size && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t piece = check_lower_piece(size - offset);
            const uint8_t class = check_lower_piece_class(piece);
            uint64_t bits = 0;
            if (part->kind == CHECK_INIT_BYTES)
            {
                memcpy(&bits, part->bytes + offset, piece);
            }
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
            check_lower_offset(writer, begin + offset);
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
            offset += piece;
        }
    }
}

// A bit-field's place: `width` bits, `shift` bits up from the start of the byte its first bit is in, `first` bytes
// into its record. It is read and written through that byte and the ones after it that hold its bits, never another
// member's (C11 makes those other memory locations): 1 to 8 bytes, accessed as pieces (check_lower_piece) that make
// up one little-endian unit. Its value is in `class`, sign-extended from its width when its type is signed (plain int
// bit-fields are, as in clang and GCC; an enum's are as its underlying type is).
typedef struct CheckLowerBitField CheckLowerBitField;
struct CheckLowerBitField
{
    uint64_t first;
    uint32_t shift;
    uint32_t width;
    uint8_t class;
    bool is_signed;
};

// The place of a bit-field of `width` bits and type `type` at bit `offset`; false for one that has no integer class or
// whose bits are spread over more than 8 bytes (a packed 64-bit one off a byte boundary).
static bool check_lower_bit_place(const TypeTable* types, uint64_t offset, uint32_t width, TypeId type, CheckLowerBitField* field)
{
    uint32_t bits = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, type, &bits, &sign);
    field->first = offset / 8;
    field->shift = (uint32_t)(offset % 8);
    field->width = width;
    field->class = check_lower_class(types, type);
    field->is_signed = sign && bits != 1;
    return integer && width != 0 && width <= bits && field->shift + width <= 64 && field->class < BUSTER_TYPED_CLASSES &&
           field->class != BUSTER_TYPED_CLASS_VOID;
}

// The place of the bit-field that MEMBER or ARROW node `node` designates.
static bool check_lower_bit_member(const CheckProgram* program, uint32_t node, CheckLowerBitField* field)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    TypeId record = stream->values[graph->operand0[node]].type;
    if (graph->ops[node] == CHECK_OP_ARROW)
    {
        record = type_element(program->types, stream->conversions[node].operands[0].type);
    }
    uint64_t offset = 0;
    const uint32_t member = type_find_member(program->types, record, graph->members[node], &offset);
    return member != 0 && check_lower_bit_place(program->types, offset, stream->values[node].bit_width, stream->values[node].type, field);
}

// The MEMBER or ARROW node of the bit-field that a store's destination `node` designates, through _Generic's
// selections, or BUSTER_CHECK_NONE.
static uint32_t check_lower_bit_destination(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t destination = node;
    while (destination < program->stream->count && graph->ops[destination] == CHECK_OP_SELECT)
    {
        destination = graph->operand0[destination];
    }
    const bool member = destination < program->stream->count && (graph->ops[destination] == CHECK_OP_MEMBER || graph->ops[destination] == CHECK_OP_ARROW);
    return member ? destination : BUSTER_CHECK_NONE;
}

// A place in a task's `type`: the shift in bits 0 to 2, the width less one in bits 3 to 8, the class in bits 9 to 12
// and the sign in bit 13. Its first byte is in the address the task finds.
static uint32_t check_lower_bit_pack(const CheckLowerBitField* field)
{
    return field->shift | (field->width - 1) << 3 | (uint32_t)field->class << 9 | (uint32_t)field->is_signed << 13;
}

static CheckLowerBitField check_lower_bit_unpack(uint32_t packed)
{
    return (CheckLowerBitField){
        .shift = packed & 7,
        .width = (packed >> 3 & 63) + 1,
        .class = (uint8_t)(packed >> 9 & 15),
        .is_signed = (packed >> 13 & 1) != 0,
    };
}

// A bit-field's unit from the address of its first byte on top of the stack: each of its `bytes` bytes' pieces loaded,
// widened and put in place, as a U64. More than one piece reads the address from a temporary.
static void check_lower_bit_unit(CheckLowerWriter* writer, uint32_t bytes)
{
    const bool whole = (bytes & (bytes - 1)) == 0;
    const uint32_t address = whole ? BUSTER_CHECK_NONE : check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    if (!whole)
    {
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    }
    uint32_t offset = 0;
    while (offset < bytes && writer->output->status == CHECK_PROGRAM_OK)
    {
        const uint32_t piece = check_lower_piece(bytes - offset);
        const uint8_t class = check_lower_piece_class(piece);
        if (!whole)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
            check_lower_offset(writer, offset);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, 0);
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        }
        if (offset != 0)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 * offset);
            check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
        }
        offset += piece;
    }
}

// A bit-field's value from the U64 on top of the stack that holds its bits `shift` bits up: the bits moved to the top
// and back down, which sign- or zero-extends them, then converted to the field's class.
static void check_lower_bit_extract(CheckLowerWriter* writer, const CheckLowerBitField* field, uint32_t shift)
{
    const uint8_t wide = field->is_signed ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
    const uint32_t up = 64 - shift - field->width;
    if (up != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, up);
        check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (field->is_signed)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
    }
    if (field->width != 64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, wide, 64 - field->width);
        check_lower_emit(writer, BUSTER_TYPED_SHR, wide, 0);
    }
    if (field->class != wide)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, field->class, 0);
    }
}

static void check_lower_bit_read(CheckLowerWriter* writer, uint32_t packed)
{
    const CheckLowerBitField field = check_lower_bit_unpack(packed);
    check_lower_bit_unit(writer, (field.shift + field.width + 7) / 8);
    check_lower_bit_extract(writer, &field, field.shift);
}

// The value on top of the stack, in the field's class, stored into the bit-field whose first byte's address local
// `address` holds: its low `width` bits kept in a temporary; the unit read, the field's bits cleared and the value's
// put in their place; the unit written back by its pieces. The value as the field now reads it stays unless `discard`.
static void check_lower_bit_store(CheckLowerWriter* writer, uint32_t packed, uint32_t address, bool discard)
{
    const CheckLowerBitField field = check_lower_bit_unpack(packed);
    const uint32_t bytes = (field.shift + field.width + 7) / 8;
    const bool whole = (bytes & (bytes - 1)) == 0;
    const uint64_t mask = field.width == 64 ? UINT64_MAX : (UINT64_C(1) << field.width) - 1;
    const uint32_t value = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    if (field.class != BUSTER_TYPED_CLASS_U64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (field.width != 64)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, mask);
        check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    if (whole)
    {
        // The address the piece's SET writes through, under the unit.
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    }
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    check_lower_bit_unit(writer, bytes);
    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, ~(mask << field.shift));
    check_lower_emit(writer, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
    if (field.shift != 0)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, field.shift);
        check_lower_emit(writer, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
    }
    check_lower_emit(writer, BUSTER_TYPED_OR, BUSTER_TYPED_CLASS_U64, 0);
    if (whole)
    {
        const uint8_t class = check_lower_piece_class(bytes);
        if (class != BUSTER_TYPED_CLASS_U64)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
    }
    else
    {
        const uint32_t unit = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, unit);
        uint32_t offset = 0;
        while (offset < bytes && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t piece = check_lower_piece(bytes - offset);
            const uint8_t class = check_lower_piece_class(piece);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
            check_lower_offset(writer, offset);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, unit);
            if (offset != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 * offset);
                check_lower_emit(writer, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
            }
            if (class != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
            }
            check_lower_emit(writer, BUSTER_TYPED_SET, class, 0);
            offset += piece;
        }
    }
    if (!discard)
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, value);
        check_lower_bit_extract(writer, &field, 0);
    }
}

static uint8_t check_lower_binary(CheckOp op)
{
    uint8_t kind = BUSTER_TYPED_NONE;
    switch (op)
    {
        case CHECK_OP_ADD: case CHECK_OP_ADD_ASSIGN: kind = BUSTER_TYPED_ADD; break;
        case CHECK_OP_SUBTRACT: case CHECK_OP_SUBTRACT_ASSIGN: kind = BUSTER_TYPED_SUB; break;
        case CHECK_OP_MULTIPLY: case CHECK_OP_MULTIPLY_ASSIGN: kind = BUSTER_TYPED_MUL; break;
        case CHECK_OP_DIVIDE: case CHECK_OP_DIVIDE_ASSIGN: kind = BUSTER_TYPED_DIV; break;
        case CHECK_OP_REMAINDER: case CHECK_OP_REMAINDER_ASSIGN: kind = BUSTER_TYPED_MOD; break;
        case CHECK_OP_SHIFT_LEFT: case CHECK_OP_SHIFT_LEFT_ASSIGN: kind = BUSTER_TYPED_SHL; break;
        case CHECK_OP_SHIFT_RIGHT: case CHECK_OP_SHIFT_RIGHT_ASSIGN: kind = BUSTER_TYPED_SHR; break;
        case CHECK_OP_BIT_AND: case CHECK_OP_BIT_AND_ASSIGN: kind = BUSTER_TYPED_AND; break;
        case CHECK_OP_BIT_OR: case CHECK_OP_BIT_OR_ASSIGN: kind = BUSTER_TYPED_OR; break;
        case CHECK_OP_BIT_XOR: case CHECK_OP_BIT_XOR_ASSIGN: kind = BUSTER_TYPED_XOR; break;
        case CHECK_OP_LESS: kind = BUSTER_TYPED_LT; break;
        case CHECK_OP_LESS_EQUAL: kind = BUSTER_TYPED_LE; break;
        case CHECK_OP_GREATER: kind = BUSTER_TYPED_GT; break;
        case CHECK_OP_GREATER_EQUAL: kind = BUSTER_TYPED_GE; break;
        case CHECK_OP_EQUAL: kind = BUSTER_TYPED_EQ; break;
        case CHECK_OP_NOT_EQUAL: kind = BUSTER_TYPED_NE; break;
        default: break;
    }
    return kind;
}

// Whether a class is an integer narrower than 32 bits, which Tier 0 compares only once converted (`!E` is `0 == E`,
// where E is promoted).
static bool check_lower_narrow(uint8_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U16;
}

static void check_lower_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const uint8_t class = check_lower_class(writer->program->types, type);
    const bool narrow = check_lower_narrow(class);
    check_lower_instruction(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
    if (narrow)
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
    }
}

static void check_lower_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint8_t source = check_lower_value_class(types, from);
    const uint8_t target = check_lower_value_class(types, to);
    // An aggregate converts only to its own (compatible) type or to void: GNU's cast to a union isn't lowered.
    const bool mixed = check_lower_aggregate(types, from) != check_lower_aggregate(types, to) && target != BUSTER_TYPED_CLASS_VOID;
    if (source >= BUSTER_TYPED_CLASSES || target >= BUSTER_TYPED_CLASSES || mixed)
    {
        check_lower_unsupported(writer, mixed ? "casts to union types" : check_lower_type_feature(types, source >= BUSTER_TYPED_CLASSES ? from : to));
    }
    else if (target == BUSTER_TYPED_CLASS_VOID)
    {
        if (source != BUSTER_TYPED_CLASS_VOID)
        {
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, source, 0);
        }
    }
    else if (type_unqualified(type_strip_aligned(types, to)) == BUSTER_TYPE_BOOL)
    {
        if (check_lower_narrow(source))
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, check_lower_narrow(source) ? BUSTER_TYPED_CLASS_I32 : source, 0);
        check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (source != target)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, target, 0);
    }
}

static void check_lower_constant(CheckLowerWriter* writer, const CheckValue* value)
{
    const uint8_t class = check_lower_class(writer->program->types, value->type);
    uint64_t bits = (uint64_t)value->integer;
    if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        check_lower_address(writer, value->object, value->offset);
    }
    else
    {
        if (value->constant_kind == CHECK_CONSTANT_FLOATING)
        {
            if (class == BUSTER_TYPED_CLASS_F32)
            {
                const float number = (float)value->floating;
                uint32_t small;
                memcpy(&small, &number, sizeof(small));
                bits = small;
            }
            else if (class == BUSTER_TYPED_CLASS_F64)
            {
                const double number = (double)value->floating;
                memcpy(&bits, &number, sizeof(bits));
            }
        }
        else if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
        {
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                if ((class & 1) != 0 && (bits >> (width - 1)) != 0)
                {
                    bits |= ~mask;
                }
            }
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
    }
}

static void check_lower_call(CheckLowerWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    const TypeTable* types = writer->program->types;
    const uint32_t callee = graph->operand0[node];
    // The call's entries are its callee's line's, as GCC and clang place a call (the call's own node is its closing
    // parenthesis, which a call across lines ends on).
    writer->source = graph->tokens[callee];
    CheckValue value = stream->values[callee];
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    bool mangled = false;
    if (overload != BUSTER_CHECK_NONE)
    {
        const CheckOverloadResult* selected = &stream->overloads[overload].selected;
        value = (CheckValue){ .type = selected->type, .category = CHECK_FUNCTION, .constant_kind = CHECK_CONSTANT_ADDRESS,
                              .object = selected->object, .flags = BUSTER_CHECK_STATIC_OBJECT };
        mangled = (selected->flags & BUSTER_CHECK_OVERLOADABLE) != 0;
    }
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    TypeId type = type_strip_aligned(types, value.type);
    if (type_kind(types, type) == BUSTER_TYPE_KIND_POINTER)
    {
        type = type_strip_aligned(types, type_element(types, type));
    }
    uint32_t parameters = 0;
    const TypeId* parameter_types = type_parameters(types, type, &parameters);
    const uint8_t flags = types->types.flags[type_index(type)];
    const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
    CheckLowerAbi* abi = writer->abi;
    // What the parameters' and the arguments' types need that isn't lowered, the first one's (a struct or a union passes
    // its address, and its bytes go where abi_assign puts them).
    const char* passed = NULL;
    for (uint32_t a = 0; a < parameters && passed == NULL; a += 1)
    {
        passed = check_lower_passed(writer, parameter_types[a]);
    }
    for (uint32_t a = 0; a < count && passed == NULL; a += 1)
    {
        const CheckConversion* conversion = stream->argument_conversions + first + a;
        passed = (conversion->flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) != 0 ? "transparent unions" : check_lower_passed(writer, conversion->type);
    }
    // A struct or union result lands in a frame object of the call's own, whose address is the call's value.
    const TypeId result = stream->values[node].type;
    const bool aggregate_result = check_lower_aggregate(types, result);
    const char* returned = aggregate_result ? check_lower_by_value(writer, result, true) : check_lower_class(types, result) == UINT8_MAX ? check_lower_type_feature(types, result) : NULL;
    // A callee that is a function's own symbol is called directly; any other (a pointer's value, a function designator
    // through one) is evaluated before the arguments and called through: Tier 0's indirect call, callee 0.
    const bool direct = value.category != CHECK_LVALUE && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object != 0 && value.offset == 0 &&
                        value.object < writer->program->object_count && writer->program->object_symbols[value.object] != 0 &&
                        (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (mangled || passed != NULL || count > BUSTER_TIER0_MAX_CALL_ARGUMENTS || returned != NULL)
    {
        check_lower_unsupported(writer, mangled ? "calls of overloadable functions" : count > BUSTER_TIER0_MAX_CALL_ARGUMENTS ? "calls with more than 64 arguments" :
                                        passed != NULL ? passed : returned);
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const uint32_t call = writer->call_count++;
        const uint32_t object = aggregate_result ? check_lower_object(writer, type_size(types, result), type_alignment_log2(types, result)) : BUSTER_CHECK_NONE;
        writer->call_targets[call] = direct ? writer->program->object_symbols[value.object] : 0;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = variadic;
        writer->call_result_objects[call] = aggregate_result ? object + 1 : 0;
        writer->aggregates |= aggregate_result;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? parameters : count;
        abi->variadic[abi->count++] = variadic;
        for (uint32_t a = 0; a < count; a += 1)
        {
            const TypeId argument = stream->argument_conversions[first + a].type;
            const bool aggregate = check_lower_aggregate(types, argument);
            abi->arguments[abi->argument_count++] = argument;
            writer->argument_sizes[writer->argument_count] = aggregate ? (uint32_t)type_size(types, argument) : 0;
            writer->aggregates |= aggregate;
            writer->argument_classes[writer->argument_count++] = check_lower_value_class(types, argument);
        }
        if (aggregate_result)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CALL, aggregate_result ? BUSTER_TYPED_CLASS_VOID : check_lower_class(types, result), call);
        for (uint32_t a = count; a-- > 0;)
        {
            check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
        }
        if (!direct)
        {
            check_lower_operand(writer, node, 0, false, 0);
        }
    }
}

static void check_lower_helper(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    CheckLowerAbi* abi = writer->abi;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const CheckProgramHelper* helper = helpers->items + helpers->uses[selected].helper;
        if (count != helper->parameter_count)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            const uint32_t call = writer->call_count++;
            writer->call_targets[call] = helper->symbol;
            writer->call_first[call] = writer->argument_count;
            writer->call_counts[call] = count;
            writer->call_variadic[call] = 0;
            writer->call_result_objects[call] = 0;
            abi->first[abi->count] = abi->argument_count;
            abi->results[abi->count] = helper->result_type;
            abi->counts[abi->count] = count;
            abi->named[abi->count] = count;
            abi->variadic[abi->count++] = 0;
            for (uint32_t a = 0; a < count; a += 1)
            {
                const TypeId type = helper->parameter_types[a];
                abi->arguments[abi->argument_count++] = type;
                writer->argument_sizes[writer->argument_count] = 0;
                writer->argument_classes[writer->argument_count++] = check_lower_class(program->types, type);
            }
            if (helper->terminates)
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_TERMINATE });
            }
            else
            {
                check_lower_convert(writer, helper->result_type, stream->values[node].type);
            }
            check_lower_instruction(writer, BUSTER_TYPED_CALL, check_lower_class(program->types, helper->result_type), call);
            for (uint32_t a = count; a-- > 0;)
            {
                const TypeId checked = stream->argument_conversions[first + a].type;
                check_lower_convert(writer, checked, helper->parameter_types[a]);
                check_lower_operand(writer, node, a + 3, false, checked);
            }
        }
    }
}

static uint32_t check_lower_native_call(CheckLowerWriter* writer, SymbolId symbol, TypeId result, const TypeId* arguments, uint32_t count)
{
    CheckLowerAbi* abi = writer->abi;
    uint32_t call = BUSTER_CHECK_NONE;
    if (symbol == 0 || result == 0 || (count != 0 && arguments == NULL))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        call = writer->call_count++;
        writer->call_targets[call] = symbol;
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = 0;
        writer->call_result_objects[call] = 0;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = count;
        abi->variadic[abi->count++] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            abi->arguments[abi->argument_count++] = arguments[i];
            writer->argument_sizes[writer->argument_count] = 0;
            writer->argument_classes[writer->argument_count++] = check_lower_class(writer->program->types, arguments[i]);
        }
    }
    return call;
}

static void check_lower_math(CheckLowerWriter* writer, uint32_t node, const CheckBuiltinFloatEffect* effect)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const uint32_t selected = check_program_builtin_index(helpers->uses, helpers->use_count, sizeof(CheckProgramHelperUse), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    const uint8_t class = check_lower_class(program->types, effect->floating_type);
    const uint32_t parameters = count + effect->may_set_errno;
    if (selected == BUSTER_CHECK_NONE || helpers->uses[selected].helper >= helpers->count || effect->unsupported != 0 ||
        (class != BUSTER_TYPED_CLASS_F32 && class != BUSTER_TYPED_CLASS_F64))
    {
        check_lower_unsupported(writer, "this math builtin's type");
    }
    else
    {
        const CheckProgramHelper* helper = helpers->items + helpers->uses[selected].helper;
        if (count == 0 || count > 2 || effect->evaluated_operands != (1u << count) - 1 ||
            helper->id != effect->runtime_builtin || helper->parameter_count != parameters || helper->result_type != BUSTER_TYPE_ULONG ||
            (effect->may_set_errno && (writer->output->errno_symbol == 0 || effect->errno_pointer_type != writer->output->errno_pointer_type)))
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            const uint32_t call = check_lower_native_call(writer, helper->symbol, helper->result_type, helper->parameter_types, parameters);
            const uint32_t result = check_lower_addressed_temporary(writer, BUSTER_TYPED_CLASS_U64);
            // These loads reinterpret IEEE representation bits. Numeric FP/integer CONVERT is deliberately
            // absent at the helper boundary; only the checked source conversion precedes each bit transport.
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, result);
            check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, call);
            if (effect->may_set_errno)
            {
                const uint32_t accessor = check_lower_native_call(writer, writer->output->errno_symbol, effect->errno_pointer_type, NULL, 0);
                check_lower_instruction(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, accessor);
            }
            for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
            {
                const uint32_t local = check_lower_addressed_temporary(writer, class);
                if (class == BUSTER_TYPED_CLASS_F32)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                }
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_operand(writer, node, i + 3, false, stream->argument_conversions[first + i].type);
            }
        }
    }
}

static void check_lower_terminate(CheckLowerWriter* writer)
{
    // Tier0 has ordinary calls and jumps. A terminal native helper traps; this self edge also prevents its
    // typed CFG from reaching the continuation. The validator checks unreachable expressions from an empty
    // stack, so recreate only their pending classes, with dead constants that no runtime path can execute.
    const uint32_t pending = writer->stack_head;
    for (uint32_t value = pending; value != 0; value = writer->stack_parents[value])
    {
        check_lower_emit(writer, BUSTER_TYPED_DISCARD, writer->stack_classes[value], 0);
    }
    const uint32_t label = check_lower_label(writer);
    check_lower_place(writer, label);
    check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, label);
    for (uint32_t value = pending; value != 0; value = writer->stack_parents[value])
    {
        check_lower_instruction(writer, BUSTER_TYPED_CONST, writer->stack_classes[value], 0);
    }
}

static void check_lower_update(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const uint32_t left = graph->operand0[node];
    const TypeId stored = stream->values[left].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const TypeId right_type = stream->conversions[node].operands[1].type;
    const uint8_t stored_class = check_lower_class(program->types, stored);
    const uint8_t calculated_class = check_lower_class(program->types, calculated);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool subtract = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT || op == CHECK_OP_SUBTRACT_ASSIGN;
    const bool pointer = type_kind(program->types, type_strip_aligned(program->types, stored)) == BUSTER_TYPE_KIND_POINTER;
    const uint32_t object = graph->ops[left] == CHECK_OP_VALUE ? stream->values[left].object : 0;
    const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
    const SymbolId symbol = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
    const bool direct = local != BUSTER_CHECK_NONE || symbol != 0;
    const uint8_t get = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
    const uint64_t destination = local != BUSTER_CHECK_NONE ? local : symbol;
    const uint8_t store = direct ? (postfix ? local != BUSTER_CHECK_NONE ? BUSTER_TYPED_SET_LOCAL : BUSTER_TYPED_SET_GLOBAL :
                                            local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL) :
                                  postfix ? BUSTER_TYPED_SET : BUSTER_TYPED_STORE;
    uint32_t address_temporary = BUSTER_CHECK_NONE;
    uint32_t old_temporary = BUSTER_CHECK_NONE;
    if (postfix && !direct)
    {
        address_temporary = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        old_temporary = check_lower_temporary(writer, stored_class);
    }
    check_lower_instruction(writer, store, stored_class, direct ? destination : 0);
    check_lower_convert(writer, calculated, stored);
    if (pointer)
    {
        check_lower_scale(writer, stored, check_lower_stride(writer, stored));
        if (subtract)
        {
            check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, BUSTER_TYPE_LONG);
        }
    }
    else
    {
        check_lower_instruction(writer, increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op), calculated_class, 0);
        if (increment)
        {
            uint64_t one = 1;
            if (calculated_class == BUSTER_TYPED_CLASS_F32)
            {
                one = UINT32_C(0x3f800000);
            }
            else if (calculated_class == BUSTER_TYPED_CLASS_F64)
            {
                one = UINT64_C(0x3ff0000000000000);
            }
            check_lower_instruction(writer, BUSTER_TYPED_CONST, calculated_class, one);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, right_type);
        }
    }
    check_lower_convert(writer, stored, calculated);
    if (direct)
    {
        if (postfix)
        {
            check_lower_instruction(writer, BUSTER_TYPED_DUP, stored_class, 0);
        }
        check_lower_instruction(writer, get, stored_class, destination);
    }
    else if (!postfix)
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_operand(writer, node, 0, true, 0);
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, stored_class, old_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, stored_class, old_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_LOAD, stored_class, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address_temporary);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// `destination = value` on a bit-field: the address of the field's first byte in a temporary, then the value converted
// to the field's type, stored by check_lower_bit_store, whose value is the field's as it now reads.
static void check_lower_bit_assign(CheckLowerWriter* writer, uint32_t node, bool discard)
{
    const CheckProgram* program = writer->program;
    const uint32_t destination = check_lower_bit_destination(program, program->input->graph.operand0[node]);
    CheckLowerBitField field = { 0 };
    if (destination == BUSTER_CHECK_NONE || !check_lower_bit_member(program, destination, &field))
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = check_lower_bit_pack(&field), .value = address, .discard = discard });
        check_lower_operand(writer, node, 1, false, program->stream->values[node].type);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

// ++, -- and op= on a bit-field, as check_lower_update does them on other lvalues: the address of the field's first
// byte in a temporary, the field read, converted, operated on and converted back, then stored by check_lower_bit_store.
// A postfix one's value is the field as it was, kept in a temporary.
static void check_lower_bit_update(CheckLowerWriter* writer, uint32_t node, bool discard)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const TypeId stored = stream->values[graph->operand0[node]].type;
    const TypeId calculated = stream->conversions[node].operands[0].type;
    const uint8_t calculated_class = check_lower_class(program->types, calculated);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool subtract = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT;
    const uint32_t destination = check_lower_bit_destination(program, graph->operand0[node]);
    CheckLowerBitField field = { 0 };
    if (destination == BUSTER_CHECK_NONE || !check_lower_bit_member(program, destination, &field) || calculated_class >= BUSTER_TYPED_CLASSES)
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        const uint32_t old = postfix && !discard ? check_lower_temporary(writer, field.class) : BUSTER_CHECK_NONE;
        const uint32_t packed = check_lower_bit_pack(&field);
        if (old != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, field.class, old);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = packed, .value = address, .discard = postfix || discard });
        check_lower_convert(writer, calculated, stored);
        check_lower_instruction(writer, increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op), calculated_class, 0);
        if (increment)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, calculated_class, 1);
        }
        else
        {
            check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
        }
        check_lower_convert(writer, stored, calculated);
        if (old != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_STORE_LOCAL, field.class, old);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_READ, .type = packed });
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        check_lower_operand(writer, node, 0, true, 0);
    }
}

static void check_lower_initializer(CheckLowerWriter* writer, CheckControl control);
static void check_lower_control(CheckLowerWriter* writer, uint32_t index);

static uint32_t check_lower_builtin_type(const TypeTable* types, TypeId type)
{
    uint32_t width = 0;
    bool sign = false;
    const bool integer = check_action_integer_type(types, type, &width, &sign);
    const uint32_t result = integer ? width | (sign ? BUSTER_CBUILTIN_SIGNED : 0) : 0;
    return result;
}

static void check_lower_builtin_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckNodeConversions* conversions = program->stream->conversions + node;
    const bool overflow = graph->ops[node] == CHECK_OP_BUILTIN_OVERFLOW;
    const uint32_t index = writer->builtin_count++;
    CBuiltinLowerCall* call = writer->builtins + index;
    *call = (CBuiltinLowerCall){ .id = (CBuiltinId)graph->members[node] };
    if (overflow)
    {
        const CheckStream* stream = program->stream;
        const uint32_t selected = check_program_builtin_index(stream->effects, stream->effect_count, sizeof(CheckStoreEffect), node);
        if (selected == BUSTER_CHECK_NONE)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if (stream->effects[selected].unsupported != 0 || stream->effects[selected].evaluated_operands != 7)
        {
            check_lower_unsupported(writer, "this overflow builtin");
        }
        else
        {
            call->id = stream->effects[selected].runtime_builtin;
        }
    }
    const uint32_t count = overflow ? 3 : 1;
    uint32_t locals[3] = { 0 };
    for (uint32_t i = 0; i < count; i += 1)
    {
        locals[i] = check_lower_temporary(writer, check_lower_class(program->types, conversions->operands[i].type));
    }
    call->a = locals[0];
    call->b = locals[1];
    call->destination = locals[2];
    call->a_type = check_lower_builtin_type(program->types, conversions->operands[0].type);
    call->b_type = overflow ? check_lower_builtin_type(program->types, conversions->operands[1].type) : 0;
    call->result_type = overflow ? check_lower_builtin_type(program->types, conversions->store.type) : 0;
    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN, .node = index, .type = program->stream->values[node].type });
    for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, writer->local_classes[locals[i]], locals[i]);
        check_lower_operand(writer, node, i, false, conversions->operands[i].type);
    }
}

static void check_lower_builtin_finish(CheckLowerWriter* writer, const CBuiltinLowerBuffer* buffer, CBuiltinLowerResult result, TypeId type)
{
    if (result.error == CBUILTIN_LOWER_UNSUPPORTED)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else if (result.error != CBUILTIN_LOWER_OK)
    {
        check_lower_fail(writer, result.error == CBUILTIN_LOWER_CAPACITY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID);
    }
    else
    {
        uint32_t labels[BUSTER_CBUILTIN_LOWER_ENTRIES + 1];
        memset(labels, 0xff, ((uint64_t)result.count + 1) * sizeof(uint32_t));
        const uint32_t first = writer->count;
        // The recipe's temporaries are values, not frame objects or variable-length arrays (the buffer writes only their
        // classes and flags).
        memset(writer->local_sizes + writer->local_count, 0, ((uint64_t)buffer->local_count - writer->local_count) * sizeof(uint32_t));
        memset(writer->local_alignments + writer->local_count, 0, (uint64_t)buffer->local_count - writer->local_count);
        memset(writer->indirect + writer->local_count, BUSTER_CHECK_LOWER_VLA_NONE, (uint64_t)buffer->local_count - writer->local_count);
        writer->local_count = buffer->local_count;
        // External builtin recipes use absolute typed-entry offsets. Convert only their local branches into
        // this writer's symbolic labels before the ordinary whole-function patch, retaining every branch join.
        for (uint32_t i = first; i < buffer->count && writer->output->status == CHECK_PROGRAM_OK; i += 1)
        {
            if (writer->kinds[i] >= BUSTER_TYPED_BRANCH_FALSE && writer->kinds[i] <= BUSTER_TYPED_JUMP_VALUE)
            {
                const uint64_t target = writer->values[i];
                if (target < first || target > buffer->count)
                {
                    check_lower_fail(writer, CHECK_PROGRAM_INVALID);
                }
                else
                {
                    const uint32_t offset = (uint32_t)target - first;
                    if (labels[offset] == BUSTER_CHECK_NONE)
                    {
                        labels[offset] = check_lower_label(writer);
                    }
                    writer->values[i] = labels[offset];
                }
            }
        }
        while (writer->count < buffer->count && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint32_t i = writer->count;
            if (labels[i - first] != BUSTER_CHECK_NONE)
            {
                check_lower_place(writer, labels[i - first]);
            }
            if (writer->track)
            {
                check_lower_track(writer, writer->kinds[i], writer->classes[i], writer->values[i]);
            }
            writer->tokens[i] = writer->source;
            writer->count += 1;
        }
        if (labels[result.count] != BUSTER_CHECK_NONE)
        {
            check_lower_place(writer, labels[result.count]);
        }
        const uint8_t class = check_lower_value_class(writer->program->types, type);
        if (class != result.result_class)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, class, 0);
        }
    }
}

static void check_lower_builtin_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinLowerResult result = writer->scalar ? cbuiltin_lower_scalar(&buffer, writer->builtins + task.node) : cbuiltin_lower(&buffer, writer->builtins + task.node);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
}

// A variadic definition's incoming frame anchors, made at its first va_start: address-only locals whose addresses are
// the register save area's and the overflow area's (Tier0Function.incoming_variadic).
static void check_lower_anchors(CheckLowerWriter* writer)
{
    if (writer->save_anchor == BUSTER_CHECK_NONE)
    {
        writer->save_anchor = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        writer->overflow_anchor = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        if (writer->output->status == CHECK_PROGRAM_OK)
        {
            writer->local_flags[writer->save_anchor] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA;
            writer->local_flags[writer->overflow_anchor] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA;
        }
    }
}

// va_start, va_arg, va_copy and va_end (#137), by c_builtin_varargs_lower.c's recipes: each list operand's value (a
// va_list's address) evaluated once into a local of its own, va_start's anchors' addresses into two more, then the
// recipe. va_start's offsets are the function's own (CheckLowerWriter.gp_offset), and va_arg's plan abi_va_arg's.
static void check_lower_varargs_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t effect = graph->argument_offsets[node];
    const CBuiltinId id = (CBuiltinId)graph->members[node];
    const bool start = id == CBUILTIN_VA_START || id == CBUILTIN_C23_VA_START;
    const bool argument = id == CBUILTIN_VA_ARG;
    const bool copy = id == CBUILTIN_VA_COPY;
    const TypeId requested = effect < stream->varargs_count ? stream->varargs[effect].requested_type : 0;
    if (effect >= stream->varargs_count || stream->varargs[effect].node != node || (start && !writer->variadic))
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (argument && check_lower_passed(writer, requested) != NULL)
    {
        check_lower_unsupported(writer, check_lower_passed(writer, requested));
    }
    else
    {
        const uint32_t index = writer->varargs_count++;
        CBuiltinVaLowerCall* call = writer->varargs + index;
        *call = (CBuiltinVaLowerCall){ .id = id };
        call->list = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        if (start)
        {
            check_lower_anchors(writer);
            call->register_save_area = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            call->overflow_arg_area = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            call->gp_offset = writer->gp_offset;
            call->fp_offset = writer->fp_offset;
        }
        if (argument)
        {
            // A struct or union's bytes are copied from the save area into a frame object of its own (with room for
            // whole eightbytes), or read in the overflow area; its value is their address.
            const bool aggregate = check_lower_aggregate(types, requested);
            const uint64_t size = aggregate ? type_size(types, requested) : 0;
            call->plan = abi_va_arg(writer->abi_table, types, requested);
            call->result_class = check_lower_value_class(types, requested);
            // (An empty one, which the call ignores, still names its object: 1 byte, as its frame object takes.)
            call->size = aggregate && size == 0 ? 1 : (uint32_t)size;
            call->object = aggregate ? check_lower_object(writer, (size + 7) & ~UINT64_C(7), type_alignment_log2(types, requested)) : 0;
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_VARARGS, .node = index, .type = stream->values[node].type });
        if (start)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->overflow_arg_area);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->overflow_anchor);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->register_save_area);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->save_anchor);
        }
        if (copy)
        {
            call->source = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->source);
            check_lower_operand(writer, node, 1, false, stream->conversions[node].operands[1].type);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, call->list);
        check_lower_operand(writer, node, 0, false, stream->conversions[node].operands[0].type);
    }
}

static void check_lower_varargs_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinVaLowerCall* call = writer->varargs + task.node;
    const CBuiltinLowerResult result = writer->scalar ? cbuiltin_varargs_lower_scalar(&buffer, call) : cbuiltin_varargs_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
}

static void check_lower_miscellaneous_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t selected = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
    const uint32_t count = graph->argument_counts[node];
    const uint32_t first = graph->argument_offsets[node];
    if (selected == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (count > 3 || stream->misc[selected].evaluated_operands != (1u << count) - 1)
    {
        check_lower_unsupported(writer, "this builtin");
    }
    else
    {
        const uint32_t index = writer->miscellaneous_count++;
        CBuiltinMiscLowerCall* call = writer->miscellaneous + index;
        *call = (CBuiltinMiscLowerCall){ .id = stream->misc[selected].runtime_builtin, .argument_count = count };
        for (uint32_t i = 0; i < count; i += 1)
        {
            const TypeId type = stream->argument_conversions[first + i].type;
            call->arguments[i] = check_lower_temporary(writer, check_lower_class(program->types, type));
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BUILTIN_MISC, .node = index, .type = stream->values[node].type });
        for (uint32_t i = count; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const uint32_t local = call->arguments[i];
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, writer->local_classes[local], local);
            check_lower_operand(writer, node, i + 3, false, stream->argument_conversions[first + i].type);
        }
    }
}

static void check_lower_miscellaneous_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinMiscLowerCall* call = writer->miscellaneous + task.node;
    const CBuiltinLowerResult result = writer->scalar ? cbuiltin_misc_lower_scalar(&buffer, call) : cbuiltin_misc_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, task.type);
}

static void check_lower_floating_recipe(CheckLowerWriter* writer, const CBuiltinFloatLowerCall* call, TypeId type)
{
    CBuiltinLowerBuffer buffer = {
        .kinds = writer->kinds, .classes = writer->classes, .values = writer->values,
        .count = writer->count, .capacity = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES,
        .local_classes = writer->local_classes, .local_flags = writer->local_flags, .local_count = writer->local_count,
        .local_capacity = writer->local_capacity < BUSTER_TIER0_MAX_LOCALS ? writer->local_capacity : BUSTER_TIER0_MAX_LOCALS,
    };
    const CBuiltinLowerResult result = writer->scalar ? cbuiltin_float_lower_scalar(&buffer, call) : cbuiltin_float_lower(&buffer, call);
    check_lower_builtin_finish(writer, &buffer, result, type);
}

static void check_lower_floating_emit(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CBuiltinFloatLowerCall* call = writer->floating + task.node;
    const uint8_t class = (uint8_t)call->argument_class;
    const bool narrow = class == BUSTER_TYPED_CLASS_F32;
    const TypeId floating = narrow ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
    const uint64_t infinity = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const CBuiltinFloatLowerCall absolute = {
        .id = narrow ? CBUILTIN_FABSF : CBUILTIN_FABS,
        .argument_local = call->argument_local,
        .argument_class = class,
    };
    if (task.kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT && call->id == CBUILTIN_FPCLASSIFY)
    {
        // Match the checked conditional classifier: zero, unordered, infinity, then normal magnitude. Quiet
        // comparisons retain signaling-NaN exceptions and the machine's DAZ treatment without changing MXCSR.
        const uint32_t zero = check_lower_label(writer);
        const uint32_t nan = check_lower_label(writer);
        const uint32_t infinite = check_lower_label(writer);
        const uint32_t subnormal = check_lower_label(writer);
        const uint32_t end = check_lower_label(writer);
        const uint32_t magnitude = check_lower_temporary(writer, class);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, 0);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, zero);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, call->argument_local);
        check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, nan);
        check_lower_floating_recipe(writer, &absolute, floating);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, infinity);
        check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, infinite);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, magnitude);
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, narrow ? UINT64_C(0x00800000) : UINT64_C(0x0010000000000000));
        check_lower_emit(writer, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, subnormal);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 2);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, nan);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, infinite);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, subnormal);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_place(writer, zero);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 4);
        check_lower_place(writer, end);
    }
    else
    {
        if (task.kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT && call->id == CBUILTIN_ISINF_SIGN)
        {
            check_lower_floating_recipe(writer, &absolute, floating);
            check_lower_emit(writer, BUSTER_TYPED_CONST, class, infinity);
            check_lower_emit(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_floating_recipe(writer, call, task.type);
    }
}

static void check_lower_selector_test(CheckLowerWriter* writer, uint32_t local, uint32_t category, uint32_t target, bool equal)
{
    check_lower_instruction(writer, equal ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, target);
    check_lower_instruction(writer, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, category);
    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, local);
}

static void check_lower_floating_prepare(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t selected = check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node);
    const uint32_t count = graph->argument_counts[node];
    if (selected == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckBuiltinFloatEffect* effect = stream->floating + selected;
        const CBuiltinId id = effect->runtime_builtin;
        const bool classify = (id >= CBUILTIN_ISNAN && id <= CBUILTIN_ISINF_SIGN) || id == CBUILTIN_FABS || id == CBUILTIN_FABSF;
        const bool selectors = id == CBUILTIN_FPCLASSIFY;
        if (effect->unsupported != 0 || (classify && count != (selectors ? 6u : 1u)))
        {
            check_lower_unsupported(writer, "this floating builtin");
        }
        else if (selectors && effect->selected_selector < 5)
        {
            // A known category still retains Clang's exact evaluated selector prefix and its side effects. Clang's
            // CodeGen evaluates the normal and subnormal selectors as two arguments of one C++ call, an order C++
            // leaves unspecified: the approved host's GCC-built Clang 23.1.1 evaluates the subnormal one first.
            const uint32_t order[] = { 5, 4, 0, 1, 3, 2 };
            const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, result);
            for (uint32_t i = BUSTER_ARRAY_LENGTH(order); i-- > 0;)
            {
                const uint32_t operand = order[i];
                if ((effect->evaluated_operands & (1u << operand)) != 0)
                {
                    if (operand == effect->selected_selector)
                    {
                        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                    }
                    check_lower_operand(writer, node, operand + 3, false,
                                           operand == effect->selected_selector ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID);
                }
            }
        }
        else if (classify)
        {
            const uint32_t index = writer->floating_count++;
            const uint8_t class = check_lower_class(program->types, effect->floating_type);
            const uint32_t argument = check_lower_temporary(writer, class);
            writer->floating[index] = (CBuiltinFloatLowerCall){ .id = id, .argument_local = argument, .argument_class = class };
            if (selectors)
            {
                const uint32_t category = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t subnormal = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
                const uint32_t end = check_lower_label(writer);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_mark(writer, end);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, subnormal);
                check_lower_selector_test(writer, category, 3, end, false);
                // Emitted in reverse: the subnormal selector runs before the normal one, as the approved host's
                // Clang evaluates them (see the known-category order above).
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_operand(writer, node, 5, false, BUSTER_TYPE_INT);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, subnormal);
                check_lower_operand(writer, node, 6, false, BUSTER_TYPE_INT);
                for (uint32_t i = 3; i-- > 0;)
                {
                    const uint32_t operand = i == 0 ? 4 : i - 1;
                    check_lower_selector_test(writer, category, operand, end, true);
                    check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                    check_lower_operand(writer, node, operand + 3, false, BUSTER_TYPE_INT);
                }
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, category);
            }
            check_lower_push(writer, (CheckLowerTask){
                .kind = effect->may_raise_fp_exceptions ? CHECK_LOWER_BUILTIN_FLOAT_STRICT : CHECK_LOWER_BUILTIN_FLOAT,
                .node = index,
                .type = stream->values[node].type,
            });
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, argument);
            check_lower_operand(writer, node, selectors ? 8 : 3, false, effect->floating_type);
        }
        else if (stream->values[node].constant_kind == CHECK_CONSTANT_FLOATING &&
                 (stream->values[node].flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONSTANT, .node = node });
            for (uint32_t i = count; i-- > 0;)
            {
                if (i < 32 && (effect->evaluated_operands & (1u << i)) != 0)
                {
                    check_lower_operand(writer, node, i + 3, false, BUSTER_TYPE_VOID);
                }
            }
        }
        else
        {
            check_lower_unsupported(writer, "this floating builtin");
        }
    }
}

static void check_lower_region(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t selected = program->input->expression_regions[graph->tokens[task.node]];
    if (selected == 0 || selected > program->region_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckProgramRegion* region = program->regions + selected - 1;
        const uint32_t first = writer->local_count;
        // A region starts with an empty evaluation stack. Escaping to an outer label or returning abandons these
        // saved operands; only the ordinary exit restores them. This also covers regions in later call arguments.
        while (writer->reachable && writer->stack_head != 0 && writer->output->status == CHECK_PROGRAM_OK)
        {
            const uint8_t class = writer->stack_classes[writer->stack_head];
            const uint32_t local = check_lower_temporary(writer, class);
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        }
        const uint32_t end = writer->local_count;
        const TypeId type = program->stream->values[task.node].type;
        const uint8_t class = task.address ? BUSTER_TYPED_CLASS_U64 : check_lower_value_class(program->types, type);
        const uint32_t result = class != BUSTER_TYPED_CLASS_VOID ? check_lower_temporary(writer, class) : BUSTER_CHECK_NONE;
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, class, result);
        }
        for (uint32_t local = first; local < end; local += 1)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, writer->local_classes[local], local);
        }
        if (result != BUSTER_CHECK_NONE)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, result);
        }
        const TypeId converted = program->stream->conversions[task.node].operands[0].type;
        check_lower_operand(writer, task.node, 0, task.address, converted);
        for (uint32_t i = region->count; i-- > 0;)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CONTROL, .node = region->first + i });
        }
    }
}

static void check_lower_expand(CheckLowerWriter* writer, CheckLowerTask task)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const uint32_t node = task.node;
    const CheckOp op = graph->ops[node];
    const CheckValue value = stream->values[node];
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const TypeId type_a = stream->conversions[node].operands[0].type;
    const TypeId type_b = stream->conversions[node].operands[1].type;
    const uint8_t class = check_lower_value_class(types, value.type);
    writer->token = graph->tokens[node];
    writer->source = writer->token;
    const uint8_t value_kind = type_kind(types, type_strip_aligned(types, value.type));
    // A function, an array or an aggregate: its value is its address, which nothing loads through.
    const bool aggregate = check_lower_aggregate(types, value.type);
    const bool address_value = value_kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(value_kind) || aggregate;
    const bool constant = value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                          (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (op == CHECK_OP_VALUE && !task.initialized && value.object != 0 && value.object < program->object_count &&
        program->input->compound_objects[value.object - 1] && !program->input->static_objects[value.object - 1])
    {
        const uint32_t selected = program->initializers != 0 ? program->initializers->declaration_plans[value.object - 1] : 0;
        if (selected == 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            task.initialized = true;
            check_lower_push(writer, task);
            check_lower_initializer(writer, (CheckControl){ .token = value.object - 1, .target = selected - 1 });
        }
    }
    else if (op == CHECK_OP_STATEMENT_VALUE)
    {
        check_lower_region(writer, task);
    }
    else if (constant && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY &&
             ((value.category == CHECK_RVALUE && !task.address) || ((task.address || address_value) && value.constant_kind == CHECK_CONSTANT_ADDRESS)))
    {
        check_lower_constant(writer, &value);
    }
    else if (op == CHECK_OP_VALUE)
    {
        const uint32_t object = value.object;
        if (value.type == BUSTER_TYPE_VOID)
        {
            // The final value of a statement region without an expression statement is void.
        }
        else if (task.address || address_value)
        {
            check_lower_address(writer, object, value.offset);
        }
        else if (value.category == CHECK_LVALUE && object != 0 && object < program->object_count)
        {
            const uint32_t local = writer->local_map[object];
            const SymbolId symbol = program->object_symbols[object];
            if (local != BUSTER_CHECK_NONE)
            {
                check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
            }
            else if (symbol != 0)
            {
                check_lower_emit(writer, BUSTER_TYPED_GLOBAL, class, symbol);
            }
            else
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
        }
        else
        {
            check_lower_unsupported(writer, "this value");
        }
    }
    else if (op == CHECK_OP_SELECT)
    {
        check_lower_operand(writer, node, 0, task.address, 0);
    }
    else if (op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY)
    {
        const TypeId input_type = stream->values[a].type;
        const bool complex = type_kind(types, type_strip_aligned(types, input_type)) == BUSTER_TYPE_KIND_COMPLEX;
        if (!complex && op == CHECK_OP_REAL)
        {
            check_lower_operand(writer, node, 0, task.address, 0);
        }
        else if (!complex)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
        }
        else if (stream->values[a].category == CHECK_LVALUE)
        {
            if (!task.address)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            }
            if (op == CHECK_OP_IMAGINARY)
            {
                check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, type_size(types, type_element(types, input_type)));
            }
            check_lower_operand(writer, node, 0, true, 0);
        }
        else
        {
            check_lower_unsupported(writer, "_Complex values that are not lvalues");
        }
    }
    else if (op == CHECK_OP_VLA_BOUND || op == CHECK_OP_VLA_EXTENT)
    {
        const uint32_t identity = graph->members[node];
        if (identity == 0 || identity > program->input->vla_count || program->input->vlas[identity - 1].body != writer->body->opening)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else
        {
            if (writer->extent_locals[identity - 1] == BUSTER_CHECK_NONE)
            {
                writer->extent_locals[identity - 1] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
            }
            const uint32_t local = writer->extent_locals[identity - 1];
            check_lower_instruction(writer, op == CHECK_OP_VLA_BOUND ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
            if (op == CHECK_OP_VLA_BOUND)
            {
                check_lower_operand(writer, node, 0, false, BUSTER_TYPE_ULONG);
            }
        }
    }
    else if (op == CHECK_OP_SIZEOF_VLA)
    {
        check_lower_operand(writer, node, 0, false, BUSTER_TYPE_ULONG);
    }
    else if (op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW)
    {
        check_lower_builtin_prepare(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_MISC)
    {
        const CBuiltinId id = (CBuiltinId)graph->members[node];
        if (id == CBUILTIN_TRAP || id == CBUILTIN_UNREACHABLE)
        {
            check_lower_helper(writer, node);
        }
        else
        {
            check_lower_miscellaneous_prepare(writer, node);
        }
    }
    else if (op == CHECK_OP_BUILTIN_ATOMIC)
    {
        check_lower_helper(writer, node);
    }
    else if (op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_BIT_CAST)
    {
        check_lower_unsupported(writer, "vector, x86 and bit-cast builtins");
    }
    else if (op == CHECK_OP_BUILTIN_FLOAT)
    {
        const uint32_t selected = check_program_builtin_index(stream->floating, stream->floating_count, sizeof(CheckBuiltinFloatEffect), node);
        const CheckBuiltinFloatEffect* effect = selected != BUSTER_CHECK_NONE ? stream->floating + selected : NULL;
        if (effect != NULL && effect->math_kind != CHECK_BUILTIN_MATH_NONE && effect->runtime_builtin != CBUILTIN_NONE)
        {
            check_lower_math(writer, node, effect);
        }
        else if (graph->members[node] == CBUILTIN_FLT_ROUNDS)
        {
            check_lower_helper(writer, node);
        }
        else
        {
            check_lower_floating_prepare(writer, node);
        }
    }
    else if (op == CHECK_OP_BUILTIN_QUERY)
    {
        const uint32_t selected = check_program_builtin_index(stream->queries, stream->query_count, sizeof(CheckBuiltinQueryEffect), node);
        if (selected == BUSTER_CHECK_NONE)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if (!check_program_query_known(program, node, stream->queries + selected))
        {
            check_lower_unsupported(writer, "this constant query at run time");
        }
        else
        {
            const uint32_t count = graph->argument_counts[node];
            const uint32_t evaluated = stream->queries[selected].evaluated_operands;
            const uint32_t mask = count < 32 ? (UINT32_C(1) << count) - 1 : UINT32_MAX;
            const bool constant_query = graph->members[node] == CBUILTIN_CONSTANT_P;
            const bool evaluate = stream->queries[selected].runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
            if ((evaluated & ~mask) != 0 || (constant_query && (count != 1 || evaluated != (uint32_t)evaluate)))
            {
                check_lower_fail(writer, CHECK_PROGRAM_INVALID);
            }
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_QUERY, .node = selected });
            for (uint32_t i = count; i-- > 0;)
            {
                if (i < 32 && (evaluated & (UINT32_C(1) << i)) != 0)
                {
                    check_lower_operand(writer, node, i + 3, false, BUSTER_TYPE_VOID);
                }
            }
        }
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        check_lower_operand(writer, node, 0, true, 0);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        if (!task.address && !address_value)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
        }
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        TypeId record = stream->values[a].type;
        if (op == CHECK_OP_ARROW)
        {
            record = type_element(types, type_a);
        }
        uint64_t offset = 0;
        const uint32_t member = type_find_member(types, record, graph->members[node], &offset);
        // A bit-field's address, which only its stores ask for, is its first byte's.
        const bool bit_field = (value.flags & BUSTER_CHECK_BIT_FIELD) != 0;
        CheckLowerBitField field = { 0 };
        if (member == 0 || (!bit_field && offset % 8 != 0))
        {
            check_lower_unsupported(writer, "this member access");
        }
        else if (bit_field && !check_lower_bit_member(program, node, &field))
        {
            check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
        }
        else
        {
            if (bit_field && !task.address)
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_READ, .type = check_lower_bit_pack(&field) });
            }
            else if (!task.address && !address_value)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
            }
            if (offset / 8 != 0)
            {
                check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            }
            check_lower_operand(writer, node, 0, op == CHECK_OP_MEMBER, type_a);
        }
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        check_lower_operand(writer, node, 0, false, value.type);
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        check_lower_instruction(writer, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        check_lower_truth(writer, type_a, op == CHECK_OP_NOT);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else if (op == CHECK_OP_COMMA)
    {
        check_lower_operand(writer, node, 1, task.address, value.type);
        check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        const bool reuse = op == CHECK_OP_CONDITIONAL && a == b;
        bool truth = false;
        if (check_program_runtime_truth(program, node, a, &truth))
        {
            if (op == CHECK_OP_CONDITIONAL)
            {
                check_lower_operand(writer, node, truth ? 1 : 2, false, value.type);
            }
            else if ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth))
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, truth);
            }
            else
            {
                check_lower_truth(writer, type_b, false);
                check_lower_operand(writer, node, 1, false, type_b);
            }
            if ((!reuse || !truth) && check_program_runtime_effects(program, a))
            {
                check_lower_operand(writer, node, 0, false, BUSTER_TYPE_VOID);
            }
        }
        else
        {
            const uint32_t alternate = check_lower_label(writer);
            const uint32_t end = check_lower_label(writer);
            const uint32_t retained = reuse ? check_lower_temporary(writer, check_lower_class(types, type_a)) : BUSTER_CHECK_NONE;
            check_lower_mark(writer, end);
            if (op == CHECK_OP_CONDITIONAL)
            {
                check_lower_operand(writer, node, 2, false, value.type);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
            }
            check_lower_mark(writer, alternate);
            check_lower_instruction(writer, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, end);
            if (op == CHECK_OP_CONDITIONAL)
            {
                if (reuse)
                {
                    check_lower_convert(writer, type_a, value.type);
                    check_lower_instruction(writer, BUSTER_TYPED_LOCAL, check_lower_class(types, type_a), retained);
                }
                else
                {
                    check_lower_operand(writer, node, 1, false, value.type);
                }
            }
            else
            {
                check_lower_truth(writer, type_b, false);
                check_lower_operand(writer, node, 1, false, type_b);
            }
            check_lower_instruction(writer, op == CHECK_OP_LOGICAL_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, alternate);
            check_lower_truth(writer, type_a, false);
            if (reuse)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, check_lower_class(types, type_a), retained);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(types, type_a), retained);
            }
            check_lower_operand(writer, node, 0, false, type_a);
        }
    }
    else if (op == CHECK_OP_CALL)
    {
        check_lower_call(writer, node);
    }
    else if (op == CHECK_OP_VARARGS)
    {
        check_lower_varargs_prepare(writer, node);
    }
    else if (op == CHECK_OP_ASSIGN && (stream->values[a].flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        check_lower_bit_assign(writer, node, task.discard);
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN && (stream->values[a].flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        check_lower_bit_update(writer, node, task.discard);
    }
    else if ((op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE) && aggregate)
    {
        // A struct's or union's assignment copies its bytes, and the destination's address is its value.
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, value.type), .address = !task.discard });
        check_lower_operand(writer, node, op == CHECK_OP_INITIALIZE ? 0 : 1, false, value.type);
        if (op == CHECK_OP_ASSIGN)
        {
            check_lower_operand(writer, node, 0, true, 0);
        }
        else if (declaration < program->input->token_count)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = program->input->objects[declaration] });
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
        const uint32_t object = declaration != BUSTER_CHECK_NONE ? program->input->objects[declaration] : graph->ops[a] == CHECK_OP_VALUE ? stream->values[a].object : 0;
        const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
        const SymbolId symbol = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
        if (local != BUSTER_CHECK_NONE || symbol != 0)
        {
            check_lower_instruction(writer, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                     local != BUSTER_CHECK_NONE ? local : symbol);
            check_lower_operand(writer, node, op == CHECK_OP_INITIALIZE ? 0 : 1, false, value.type);
        }
        else if (op == CHECK_OP_ASSIGN)
        {
            check_lower_instruction(writer, BUSTER_TYPED_STORE, class, 0);
            check_lower_operand(writer, node, 1, false, value.type);
            check_lower_operand(writer, node, 0, true, 0);
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)
    {
        check_lower_update(writer, node);
    }
    else if (op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        const bool left_pointer = type_kind(types, type_strip_aligned(types, type_a)) == BUSTER_TYPE_KIND_POINTER;
        const bool right_pointer = type_kind(types, type_strip_aligned(types, type_b)) == BUSTER_TYPE_KIND_POINTER;
        if (left_pointer || right_pointer)
        {
            const TypeId pointer_type = left_pointer ? type_a : type_b;
            const uint64_t size = check_lower_stride(writer, pointer_type);
            if (left_pointer && right_pointer && op == CHECK_OP_SUBTRACT)
            {
                check_lower_instruction(writer, BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_I64, 0);
                if (size != 0)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, size);
                }
                else
                {
                    const TypeId pointee = type_element(types, type_strip_aligned(types, pointer_type));
                    check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_SIZE, .type = pointee });
                }
                check_lower_instruction(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
                check_lower_instruction(writer, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
                check_lower_operand(writer, node, 1, false, type_b);
                check_lower_operand(writer, node, 0, false, type_a);
            }
            else
            {
                if (op == CHECK_OP_SUBSCRIPT && !task.address && !address_value)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
                }
                check_lower_scale(writer, pointer_type, size);
                if (op == CHECK_OP_SUBTRACT)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
                }
                check_lower_operand(writer, node, left_pointer ? 1 : 0, false, BUSTER_TYPE_LONG);
                check_lower_operand(writer, node, left_pointer ? 0 : 1, false, pointer_type);
            }
        }
        else
        {
            check_lower_instruction(writer, check_lower_binary(op), class, 0);
            check_lower_operand(writer, node, 1, false, type_b);
            check_lower_operand(writer, node, 0, false, type_a);
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE)
    {
        check_lower_instruction(writer, check_lower_binary(op), class, 0);
        check_lower_operand(writer, node, 1, false, type_b);
        check_lower_operand(writer, node, 0, false, type_a);
    }
    else
    {
        check_lower_unsupported(writer, "this operation");
    }
}

static void check_lower_drain(CheckLowerWriter* writer)
{
    while (writer->depth != 0 && writer->output->status == CHECK_PROGRAM_OK)
    {
        const CheckLowerTask task = writer->tasks[--writer->depth];
        writer->source = task.source;
        if (task.kind == CHECK_LOWER_INSTRUCTION)
        {
            check_lower_emit(writer, (uint8_t)task.node, (uint8_t)task.type, task.value);
        }
        else if (task.kind == CHECK_LOWER_CONVERSION)
        {
            check_lower_conversion(writer, task.node, task.type);
        }
        else if (task.kind == CHECK_LOWER_LABEL)
        {
            check_lower_place(writer, task.node);
        }
        else if (task.kind == CHECK_LOWER_CONTROL)
        {
            check_lower_control(writer, task.node);
        }
        else if (task.kind == CHECK_LOWER_BUILTIN)
        {
            check_lower_builtin_emit(writer, task);
        }
        else if (task.kind == CHECK_LOWER_BUILTIN_MISC)
        {
            check_lower_miscellaneous_emit(writer, task);
        }
        else if (task.kind == CHECK_LOWER_BUILTIN_FLOAT || task.kind == CHECK_LOWER_BUILTIN_FLOAT_STRICT)
        {
            check_lower_floating_emit(writer, task);
        }
        else if (task.kind == CHECK_LOWER_CONSTANT)
        {
            check_lower_constant(writer, writer->program->stream->values + task.node);
        }
        else if (task.kind == CHECK_LOWER_ADDRESS)
        {
            check_lower_address(writer, task.node, (int64_t)task.value);
        }
        else if (task.kind == CHECK_LOWER_QUERY)
        {
            check_lower_constant(writer, &writer->program->stream->queries[task.node].runtime_value);
        }
        else if (task.kind == CHECK_LOWER_TERMINATE)
        {
            check_lower_terminate(writer);
        }
        else if (task.kind == CHECK_LOWER_COPY)
        {
            check_lower_copy(writer, task.value, task.address);
        }
        else if (task.kind == CHECK_LOWER_FILL)
        {
            check_lower_fill(writer, writer->program->initializers->plans[task.node].parts + task.type, (uint32_t)task.value);
        }
        else if (task.kind == CHECK_LOWER_CLEAR)
        {
            const CheckInitPart clear = { .kind = CHECK_INIT_CLEAR, .byte_count = task.type };
            check_lower_fill(writer, &clear, (uint32_t)task.value);
        }
        else if (task.kind == CHECK_LOWER_BIT_READ)
        {
            check_lower_bit_read(writer, task.type);
        }
        else if (task.kind == CHECK_LOWER_BIT_STORE)
        {
            check_lower_bit_store(writer, task.type, (uint32_t)task.value, task.discard);
        }
        else if (task.kind == CHECK_LOWER_VARARGS)
        {
            check_lower_varargs_emit(writer, task);
        }
        else if (task.kind == CHECK_LOWER_SIZE)
        {
            check_lower_vla_size(writer, task.type, BUSTER_TYPED_CLASS_I64);
        }
        else
        {
            check_lower_expand(writer, task);
        }
    }
}

// An initializer's value, converted to its part's type: its expression, or the object an overloaded function's
// address selected.
static void check_lower_part_value(CheckLowerWriter* writer, const CheckInitPart* write)
{
    const CheckProgram* program = writer->program;
    if (write->overload_candidate != 0)
    {
        const CheckGraph* graph = &program->input->graph;
        const CheckOverloadCandidate* candidate = write->overload_candidate <= graph->overload_candidate_count ?
                                                  graph->overload_candidates + write->overload_candidate - 1 : NULL;
        if (candidate == NULL || write->value.overload_head != 0 || write->value.constant_kind != CHECK_CONSTANT_ADDRESS ||
            write->value.object != candidate->object || write->value.object == 0 || write->value.object >= program->object_count)
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
        else if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0)
        {
            check_lower_unsupported(writer, "addresses of overloadable functions");
        }
        else
        {
            check_lower_convert(writer, write->value.type, write->conversion.type);
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_ADDRESS, .node = write->value.object, .value = (uint64_t)write->value.offset });
        }
    }
    else
    {
        check_lower_expression(writer, write->node, false, write->conversion.type);
    }
}

// Part `index` of plan `plan` written into frame object `object`, as tasks: its zeros or bytes (check_lower_fill), or
// its value stored at its offset, or for an aggregate copied there.
static void check_lower_object_part(CheckLowerWriter* writer, uint32_t plan, uint32_t index, uint32_t object)
{
    const TypeTable* types = writer->program->types;
    const CheckInitPart* write = &writer->program->initializers->plans[plan].parts[index];
    const bool aggregate = check_lower_aggregate(types, write->type);
    const uint8_t class = check_lower_class(types, write->type);
    // A bit-field's value, or zeros over bits that don't start or end a byte (as unsigned bits of no particular type).
    const bool bits = write->bit_width != 0 && (write->kind == CHECK_INIT_VALUE || write->kind == CHECK_INIT_CLEAR);
    CheckLowerBitField field = {
        .first = write->bit_offset / 8,
        .shift = (uint32_t)(write->bit_offset % 8),
        .width = write->bit_width,
        .class = BUSTER_TYPED_CLASS_U64,
    };
    const bool placed = write->kind == CHECK_INIT_VALUE ? check_lower_bit_place(types, write->bit_offset, write->bit_width, write->type, &field)
                                                        : field.shift + field.width <= 64;
    if (bits && !placed)
    {
        check_lower_unsupported(writer, "bit-fields of this type or spread over more than 8 bytes");
    }
    else if (bits)
    {
        const uint32_t address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_BIT_STORE, .type = check_lower_bit_pack(&field), .value = address, .discard = true });
        if (write->kind == CHECK_INIT_CLEAR)
        {
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            check_lower_part_value(writer, write);
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        if (field.first != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, field.first);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
    else if (write->kind != CHECK_INIT_VALUE)
    {
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_FILL, .node = plan, .type = index, .value = object });
    }
    else if (write->bit_offset % 8 != 0 || (!aggregate && (class >= BUSTER_TYPED_CLASSES || class == BUSTER_TYPED_CLASS_VOID)))
    {
        check_lower_unsupported(writer, write->bit_offset % 8 != 0 ? "values that don't start a byte in local objects" :
                                                                     check_lower_type_feature(types, write->type));
    }
    else
    {
        if (aggregate)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, write->type) });
        }
        else
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET, class, 0);
        }
        check_lower_part_value(writer, write);
        if (write->bit_offset != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, write->bit_offset / 8);
        }
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    }
}

static void check_lower_initializer(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInitializers* initializers = program->initializers;
    const uint32_t declaration = control.token;
    if (initializers == 0 || control.target >= initializers->count || declaration >= program->input->token_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const CheckInitializer* plan = &initializers->plans[control.target];
        const uint32_t local = writer->local_map[program->input->objects[declaration]];
        const bool object = local != BUSTER_CHECK_NONE && writer->local_sizes[local] != 0;
        const uint8_t class = check_lower_class(program->types, plan->type);
        if (local == BUSTER_CHECK_NONE || (!object && class == UINT8_MAX))
        {
            check_lower_unsupported(writer, "initializers of this type");
        }
        for (uint32_t part = plan->count; part-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const CheckInitPart* write = &plan->parts[part];
            writer->token = write->token;
            writer->source = write->token;
            if (object)
            {
                check_lower_object_part(writer, control.target, part, local);
            }
            else if (write->bit_offset != 0 || write->bit_width != 0 || write->kind == CHECK_INIT_BYTES)
            {
                check_lower_unsupported(writer, "this initializer");
            }
            else if (write->kind == CHECK_INIT_CLEAR)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_part_value(writer, write);
            }
        }
        // What the parts leave out is zero (C11 6.7.9p21), which static storage has before any part: an object is
        // cleared first, unless its first part clears it whole.
        const CheckInitPart* first = plan->count != 0 ? plan->parts : NULL;
        const bool cleared = first != NULL && first->kind == CHECK_INIT_CLEAR && first->bit_offset == 0 && first->bit_width == 0 &&
                             object && first->byte_count >= writer->local_sizes[local];
        if (object && !cleared)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_CLEAR, .type = writer->local_sizes[local], .value = local });
        }
    }
}

static void check_lower_case(CheckLowerWriter* writer, const CheckProgramCase* item)
{
    const uint32_t local = writer->switch_locals[item->owner];
    const uint8_t class = check_lower_class(writer->program->types, item->type);
    if (class == UINT8_MAX)
    {
        check_lower_unsupported(writer, check_lower_type_feature(writer->program->types, item->type));
    }
    else if (local == BUSTER_CHECK_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else
    {
        const bool range = item->lower != item->upper;
        const uint32_t skip = range ? check_lower_label(writer) : BUSTER_CHECK_NONE;
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
        const CheckValue lower = { .type = item->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = item->lower };
        check_lower_constant(writer, &lower);
        check_lower_emit(writer, range ? BUSTER_TYPED_GE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
        if (range)
        {
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, skip);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, local);
            const CheckValue upper = { .type = item->type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = item->upper };
            check_lower_constant(writer, &upper);
            check_lower_emit(writer, BUSTER_TYPED_LE, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, item->target);
        if (range)
        {
            check_lower_place(writer, skip);
        }
    }
}

// A return of a struct or a union (`node` its value, the bytes' address; or none, for the end of the function): through
// the hidden pointer, its bytes copied where parameter 0 points and that address returned; in registers, the bytes'
// address, which Tier 0's epilogue loads them from (a frame object's of its own at the end); and an empty one's
// nothing, as a void function's. The end's frame object is there only for a function whose end is reached.
static void check_lower_return_aggregate(CheckLowerWriter* writer, uint32_t node)
{
    const TypeTable* types = writer->program->types;
    const uint64_t size = type_size(types, writer->result);
    const AbiReturn returned = writer->abi_table->results[type_index(writer->result)];
    if (writer->hidden != BUSTER_CHECK_NONE)
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, writer->hidden);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = size, .address = false });
            check_lower_expression(writer, node, false, 0);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, writer->hidden);
        }
    }
    else if (returned.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, node, false, 0);
        }
        else
        {
            const uint32_t object = check_lower_object(writer, size, type_alignment_log2(types, writer->result));
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        if (node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, node, false, BUSTER_TYPE_VOID);
        }
    }
}

// A block's stack position, saved into a local of its own before its first variable-length array
// (CHECK_CONTROL_STACK_SAVE, once per block in the control stream), and restored from it at the block's end and before
// the jumps out of it (CHECK_CONTROL_STACK_RESTORE, which come after the save). `target` is the block's closing token.
static void check_lower_stack(CheckLowerWriter* writer, CheckControl control)
{
    const bool save = control.kind == CHECK_CONTROL_STACK_SAVE;
    const bool block = control.target < writer->program->input->token_count;
    if (save && block)
    {
        writer->save_locals[control.target] = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64);
    }
    const uint32_t local = block ? writer->save_locals[control.target] : BUSTER_CHECK_NONE;
    if (local >= writer->local_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (save)
    {
        check_lower_emit(writer, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        check_lower_emit(writer, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
    }
}

// A variable-length array's object at its declaration (CHECK_CONTROL_ALLOCATE): its size from its type's extents,
// taken from the stack (ALLOCA) into its local. Refused in a block whose end has no control entry of its own (a for
// statement's declaration, a statement expression's block), aligned past 16 bytes, or initialized (C23's = {}).
static void check_lower_allocate(CheckLowerWriter* writer, CheckControl control)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const uint32_t declaration = control.token;
    const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
    const uint32_t local = object != 0 && object < program->object_count ? writer->local_map[object] : BUSTER_CHECK_NONE;
    const uint8_t role = control.target < input->token_count ? program->parser->tree.roles[control.target] : 0;
    const bool block = (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY) && input->expression_regions[control.target] == 0;
    const TypeId type = local != BUSTER_CHECK_NONE ? input->declared_types[declaration] : 0;
    const CheckInputAttributes* attributes = local != BUSTER_CHECK_NONE ? input->attributes[declaration] : 0;
    const bool aligned = type != 0 && (type_alignment_log2(program->types, type) > 4 || (attributes != 0 && attributes->alignment_log2 > 5));
    const bool initialized = program->initializers != 0 && program->initializers->declaration_plans[object - 1] != 0;
    if (local >= writer->local_count || writer->indirect[local] == BUSTER_CHECK_LOWER_VLA_NONE)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (!block)
    {
        check_lower_unsupported(writer, "variable-length arrays declared in a for statement or in a statement expression's block");
    }
    else if (aligned)
    {
        check_lower_unsupported(writer, "variable-length arrays aligned past 16 bytes");
    }
    else if (initialized)
    {
        check_lower_unsupported(writer, "initialized variable-length arrays");
    }
    else
    {
        check_lower_vla_size(writer, type, BUSTER_TYPED_CLASS_U64);
        check_lower_emit(writer, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        writer->indirect[local] = BUSTER_CHECK_LOWER_VLA_ALLOCATED;
    }
}

static void check_lower_control(CheckLowerWriter* writer, uint32_t index)
{
    const CheckProgram* program = writer->program;
    const CheckControl control = program->controls[writer->body->first + index];
    check_lower_place(writer, index);
    writer->token = control.token;
    writer->source = control.token;
    if (control.kind == CHECK_CONTROL_JUMP)
    {
        check_lower_emit(writer, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, control.target);
    }
    else if (control.kind == CHECK_CONTROL_INITIALIZE)
    {
        check_lower_initializer(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_VLA)
    {
        check_lower_expression(writer, control.node, false, BUSTER_TYPE_VOID);
    }
    else if (control.kind == CHECK_CONTROL_STACK_SAVE || control.kind == CHECK_CONTROL_STACK_RESTORE)
    {
        check_lower_stack(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_ALLOCATE)
    {
        check_lower_allocate(writer, control);
    }
    else if (control.kind == CHECK_CONTROL_ASM)
    {
        check_lower_unsupported(writer, "inline assembly");
    }
    else if (control.kind == CHECK_CONTROL_SWITCH)
    {
        const TypeId type = program->stream->values[control.node].type;
        const uint8_t class = check_lower_class(program->types, type);
        const uint32_t local = check_lower_temporary(writer, class);
        writer->switch_locals[control.token] = local;
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
        check_lower_expression(writer, control.node, false, type);
    }
    else if (control.kind == CHECK_CONTROL_CASE)
    {
        check_lower_case(writer, program->cases + control.target);
    }
    else if (control.kind == CHECK_CONTROL_RETURN && check_lower_aggregate(program->types, writer->result))
    {
        check_lower_return_aggregate(writer, control.node);
    }
    else if (control.kind != CHECK_CONTROL_END)
    {
        if (control.kind == CHECK_CONTROL_RETURN)
        {
            check_lower_instruction(writer, BUSTER_TYPED_RETURN, check_lower_class(program->types, writer->result), 0);
        }
        else if (control.kind == CHECK_CONTROL_BRANCH_FALSE || control.kind == CHECK_CONTROL_BRANCH_TRUE)
        {
            check_lower_instruction(writer, control.kind == CHECK_CONTROL_BRANCH_FALSE ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_BRANCH_TRUE,
                                    BUSTER_TYPED_CLASS_I32, control.target);
        }
        if (control.node != BUSTER_CHECK_NONE)
        {
            check_lower_expression(writer, control.node, false, control.kind == CHECK_CONTROL_EVALUATE ? BUSTER_TYPE_VOID : 0);
        }
    }
}

static uint32_t check_lower_runtime_symbols(CheckProgramLowered* lowered, const CheckProgram* program)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    uint32_t calls = 0;
    uint32_t floating = 0;
    uint32_t query = 0;
    for (uint32_t i = 0; i < program->builtin_count && lowered->status == CHECK_PROGRAM_OK; i += 1)
    {
        const uint32_t node = program->builtin_nodes[i];
        while (floating < stream->floating_count && stream->floating[floating].node < node)
        {
            floating += 1;
        }
        while (query < stream->query_count && stream->queries[query].node < node)
        {
            query += 1;
        }
        if (input->graph.ops[node] == CHECK_OP_BUILTIN_QUERY)
        {
            const CheckBuiltinQueryEffect* effect = query < stream->query_count && stream->queries[query].node == node ? stream->queries + query : NULL;
            const uint32_t arguments = input->graph.argument_counts[node];
            const uint32_t mask = arguments < 32 ? (UINT32_C(1) << arguments) - 1 : UINT32_MAX;
            const bool constant = input->graph.members[node] == CBUILTIN_CONSTANT_P;
            if (effect == NULL)
            {
                lowered->status = CHECK_PROGRAM_INVALID;
            }
            else if (!check_program_query_known(program, node, effect))
            {
                lowered->status = CHECK_PROGRAM_UNSUPPORTED;
                lowered->reason = "this constant query at run time";
            }
            else if ((effect->evaluated_operands & ~mask) != 0 ||
                     (constant && (arguments != 1 || effect->evaluated_operands != (uint32_t)(effect->runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN))))
            {
                lowered->status = CHECK_PROGRAM_INVALID;
            }
            if (lowered->status != CHECK_PROGRAM_OK)
            {
                lowered->token = input->graph.tokens[node];
            }
        }
        if (floating < stream->floating_count && stream->floating[floating].node == node && stream->floating[floating].may_set_errno)
        {
            const TypeId pointer = stream->floating[floating].errno_pointer_type;
            if (pointer == 0 || type_kind(program->types, pointer) != BUSTER_TYPE_KIND_POINTER ||
                type_element(program->types, pointer) != BUSTER_TYPE_INT ||
                (lowered->errno_pointer_type != 0 && lowered->errno_pointer_type != pointer))
            {
                lowered->status = CHECK_PROGRAM_INVALID;
                lowered->token = input->graph.tokens[node];
            }
            else
            {
                lowered->errno_pointer_type = pointer;
                calls += 1;
            }
        }
    }
    if (calls != 0 && lowered->status == CHECK_PROGRAM_OK)
    {
        // Linux x86-64 libc's errno.h declares __errno_location as int *(void). This is an explicit target-runtime
        // dependency, not a fabricated C declaration or guessed TLS address. Source symbols keep their own IDs.
        static const char name[] = "__errno_location";
        const Parser* parser = program->parser;
        const InternTable* identifiers = program->identifiers;
        if (parser == NULL || identifiers == NULL)
        {
            lowered->status = CHECK_PROGRAM_INVALID;
        }
        for (uint32_t declaration = 0; declaration < input->token_count && lowered->status == CHECK_PROGRAM_OK; declaration += 1)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            if (input->declarations[declaration] == declaration && input->linkage[declaration] != 0 && identifier < identifiers->count &&
                intern_length(identifiers, identifier) == sizeof(name) - 1 && memcmp(intern_bytes(identifiers, identifier), name, sizeof(name) - 1) == 0)
            {
                const TypeId type = type_strip_aligned(program->types, input->declared_types[declaration]);
                uint32_t parameters = 0;
                const bool function = type_kind(program->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                if (function)
                {
                    type_parameters(program->types, type, &parameters);
                }
                if (!function || parameters != 0 || (program->types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0 ||
                    type_unqualified(type_return(program->types, type)) != lowered->errno_pointer_type)
                {
                    lowered->status = CHECK_PROGRAM_UNSUPPORTED;
                    lowered->reason = "a declaration of __errno_location other than int *(void)";
                    lowered->token = declaration;
                }
                else if (input->linkage[declaration] == 2)
                {
                    const uint32_t object = input->objects[declaration];
                    if (object != 0 && object < program->object_count && program->object_symbols[object] != 0)
                    {
                        lowered->errno_symbol = program->object_symbols[object];
                    }
                }
            }
        }
        if (lowered->status == CHECK_PROGRAM_OK && lowered->errno_symbol == 0)
        {
            const uint64_t symbol = (uint64_t)program->symbol_count + lowered->helpers.count + 1;
            if (symbol >= UINT32_MAX)
            {
                lowered->status = CHECK_PROGRAM_RESOURCE;
            }
            else
            {
                lowered->errno_symbol = (SymbolId)symbol;
                lowered->runtime_symbol_count = 1;
            }
        }
    }
    return calls;
}

// With `sources`, each function's entries' source tokens too (CheckProgramLowered.entry_tokens).
static bool check_program_lower(CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, bool sources, Arena* output, Arena* scratch)
{
    *lowered = (CheckProgramLowered){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    const uint32_t count = input != 0 ? input->token_count : 0;
    const uint32_t nodes = input != 0 ? input->graph.count : 0;
    uint32_t builtin_count = 0;
    for (uint32_t node = 0; node < nodes; node += 1)
    {
        builtin_count += input->graph.ops[node] == CHECK_OP_BUILTIN_UNARY || input->graph.ops[node] == CHECK_OP_BUILTIN_OVERFLOW ||
                         input->graph.ops[node] == CHECK_OP_BUILTIN_MISC || input->graph.ops[node] == CHECK_OP_BUILTIN_FLOAT;
    }
    // Each varargs builtin is a recipe with up to four locals of its own besides its temporaries, and each body has up to
    // three more (the hidden pointer and the anchors) and an end's frame object. A program the checker refused may have
    // no stream, and is refused below without reading one.
    const uint64_t varargs = program->status == CHECK_PROGRAM_OK && program->stream != 0 ? program->stream->varargs_count : 0;
    const uint64_t capacity64 = (uint64_t)nodes * 32 + (uint64_t)program->control_count * 4 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_ENTRIES +
                                varargs * BUSTER_CBUILTIN_LOWER_ENTRIES + 32;
    const uint64_t local_capacity64 = (uint64_t)count + (uint64_t)nodes * 2 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_TEMPORARIES +
                                      varargs * (BUSTER_CBUILTIN_LOWER_TEMPORARIES + 4) + (uint64_t)program->body_count * 4 + 1;
    bool valid = output != scratch && program->status == CHECK_PROGRAM_OK && capacity64 <= UINT32_MAX && local_capacity64 <= UINT32_MAX;
    lowered->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && !check_program_helpers(&lowered->helpers, program, output, scratch))
    {
        lowered->status = lowered->helpers.status;
        lowered->token = lowered->helpers.token;
        valid = false;
    }
    const uint32_t errno_calls = valid ? check_lower_runtime_symbols(lowered, program) : 0;
    valid = valid && lowered->status == CHECK_PROGRAM_OK;
    bool terminal = false;
    for (uint32_t i = 0; i < lowered->helpers.count; i += 1)
    {
        terminal |= lowered->helpers.items[i].terminates;
    }
    const bool track = program->region_count != 0 || terminal;
    for (uint32_t i = 0; valid && i < input->asm_count; i += 1)
    {
        if (input->asm_statements[i].body == BUSTER_CHECK_NONE)
        {
            lowered->status = CHECK_PROGRAM_UNSUPPORTED;
            lowered->reason = "file-scope assembly";
            lowered->token = input->asm_statements[i].token;
            valid = false;
        }
    }
    const uint32_t capacity = valid ? (uint32_t)capacity64 : 0;
    const uint32_t local_capacity = valid ? (uint32_t)local_capacity64 : 0;
    const uint64_t abi_capacity64 = (uint64_t)nodes + program->body_count + errno_calls + 1;
    const uint64_t argument_capacity64 = input != 0 ? (uint64_t)input->graph.argument_count + count + errno_calls + 1 : 0;
    if (valid && (abi_capacity64 > UINT32_MAX || argument_capacity64 > UINT32_MAX))
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    const uint32_t abi_capacity = valid ? (uint32_t)abi_capacity64 : 0;
    const uint32_t argument_capacity = valid ? (uint32_t)argument_capacity64 : 0;
    CheckLowerAbi abi = { .capacity = abi_capacity, .argument_capacity = argument_capacity };
#define BUSTER_CHECK_LOWER_ABI(field, type, length) abi.field = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, type, length) : 0; valid &= abi.field != 0
    BUSTER_CHECK_LOWER_ABI(results, TypeId, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(arguments, TypeId, argument_capacity);
    BUSTER_CHECK_LOWER_ABI(counts, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(named, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(first, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_ABI(variadic, uint8_t, abi_capacity);
#undef BUSTER_CHECK_LOWER_ABI
    CheckLowerWriter writer = { .program = program, .output = lowered, .abi = &abi, .capacity = capacity, .local_capacity = local_capacity, .scalar = scalar };
#define BUSTER_CHECK_LOWER_COLUMN(field, type, length) writer.field = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, type, length) : 0; valid &= writer.field != 0
    BUSTER_CHECK_LOWER_COLUMN(tasks, CheckLowerTask, capacity);
    BUSTER_CHECK_LOWER_COLUMN(kinds, uint8_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(classes, uint8_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(values, uint64_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(tokens, uint32_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(labels, uint32_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(label_heads, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_parents, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_classes, uint8_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(local_map, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_COLUMN(switch_locals, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_COLUMN(extent_locals, uint32_t, (uint64_t)input->vla_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(vla_identities, uint32_t, input->vla_count != 0 ? (uint64_t)count + 1 : 1);
    BUSTER_CHECK_LOWER_COLUMN(save_locals, uint32_t, input->vla_count != 0 ? (uint64_t)count + 1 : 1);
    BUSTER_CHECK_LOWER_COLUMN(indirect, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(initializer_declarations, uint32_t, (uint64_t)nodes + 1);
    BUSTER_CHECK_LOWER_COLUMN(local_classes, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_flags, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_sizes, uint32_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_alignments, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_targets, SymbolId, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_first, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_counts, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_variadic, uint8_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(argument_classes, uint8_t, argument_capacity);
    BUSTER_CHECK_LOWER_COLUMN(argument_sizes, uint32_t, argument_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_result_objects, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(varargs, CBuiltinVaLowerCall, (uint64_t)program->stream->varargs_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(builtins, CBuiltinLowerCall, (uint64_t)builtin_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(miscellaneous, CBuiltinMiscLowerCall, (uint64_t)program->stream->misc_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(floating, CBuiltinFloatLowerCall, (uint64_t)program->stream->floating_count + 1);
#undef BUSTER_CHECK_LOWER_COLUMN
    // Every type's classification first: a body's struct or union result decides its returns (check_lower_by_value),
    // and a variadic definition's named registers its va_start; abi_assign takes every signature together after.
    AbiTable table;
    valid = valid && abi_initialize(&table, scratch, types, 16);
    if (valid)
    {
        if (scalar)
        {
            abi_classify_all_scalar(&table, types);
        }
        else
        {
            abi_classify_all(&table, types);
        }
    }
    writer.abi_table = &table;
    uint32_t* signatures = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->body_count + 1) : 0;
    uint8_t* incoming = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->body_count + 1) : 0;
    uint32_t* local_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* local_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    lowered->functions = valid && program->body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, Tier0Function, program->body_count) : 0;
    lowered->entry_tokens = valid && sources && program->body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t*, program->body_count) : 0;
    valid = valid && signatures != 0 && incoming != 0 && local_first != 0 && local_next != 0 && (program->body_count == 0 || lowered->functions != 0) &&
            (!sources || program->body_count == 0 || lowered->entry_tokens != 0);
    if (!valid && lowered->status == CHECK_PROGRAM_OK)
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid)
    {
        memset(writer.initializer_declarations, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
        memset(writer.local_map, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(writer.switch_locals, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        if (input->vla_count != 0)
        {
            memset(writer.vla_identities, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(writer.save_locals, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
            for (uint32_t v = 0; v < input->vla_count; v += 1)
            {
                if (input->vlas[v].bound_begin < count)
                {
                    writer.vla_identities[input->vlas[v].bound_begin] = v + 1;
                }
            }
        }
        memset(local_first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = count; token-- > 0;)
        {
            if (input->initializer_nodes[token] < nodes)
            {
                writer.initializer_declarations[input->initializer_nodes[token]] = token;
            }
            const uint32_t body = input->declaration_bodies[token];
            if (input->declarations[token] == token && body < count && input->declaration_kinds[token] == C_BINDING_OBJECT && !input->static_objects[token])
            {
                local_next[token] = local_first[body];
                local_first[body] = token;
            }
        }
    }
    for (uint32_t b = 0; valid && b < program->body_count && lowered->status == CHECK_PROGRAM_OK; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const TypeId type = input->declared_types[body.declaration];
        const TypeId result = type_return(types, type);
        Tier0Function* function = &lowered->functions[b];
        *function = (Tier0Function){ 0 };
        writer.count = 0;
        writer.depth = 0;
        writer.local_count = 0;
        writer.call_count = 0;
        writer.argument_count = 0;
        writer.builtin_count = 0;
        writer.miscellaneous_count = 0;
        writer.floating_count = 0;
        writer.label_count = body.count + 1;
        writer.stack_count = 0;
        writer.stack_head = 0;
        writer.reachable = true;
        writer.objects = false;
        writer.aggregates = false;
        writer.track = body.count != body.entry_count || terminal;
        writer.body = program->bodies + b;
        writer.result = result;
        writer.token = body.declaration;
        writer.source = body.declaration;
        writer.hidden = BUSTER_CHECK_NONE;
        writer.variadic = (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        writer.save_anchor = BUSTER_CHECK_NONE;
        writer.overflow_anchor = BUSTER_CHECK_NONE;
        writer.varargs_count = 0;
        memset(writer.extent_locals, 0xff, ((uint64_t)input->vla_count + 1) * sizeof(uint32_t));
        memset(writer.labels, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        if (writer.track)
        {
            memset(writer.label_heads, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        }
        signatures[b] = abi.count;
        abi.first[abi.count] = abi.argument_count;
        abi.results[abi.count] = result;
        abi.variadic[abi.count] = writer.variadic;
        // A struct or union result goes through a hidden pointer (parameter 0, in rdi) or comes back in registers from
        // the address each return gives (Tier0Function.result_size).
        const bool aggregate_result = check_lower_aggregate(types, result);
        const char* returned = aggregate_result ? check_lower_by_value(&writer, result, true) : check_lower_class(types, result) == UINT8_MAX ? check_lower_type_feature(types, result) : NULL;
        if (returned != NULL)
        {
            check_lower_unsupported(&writer, returned);
        }
        else if (aggregate_result && table.results[type_index(result)].kind == BUSTER_ABI_RESULT_MEMORY)
        {
            writer.hidden = check_lower_temporary(&writer, BUSTER_TYPED_CLASS_U64);
            function->parameter_count += 1;
        }
        else if (aggregate_result && table.results[type_index(result)].kind == BUSTER_ABI_RESULT_REGISTERS)
        {
            function->result_size = (uint32_t)type_size(types, result);
        }
        uint32_t declared = 0;
        for (uint32_t parameter = input->parameters[body.opening]; parameter != BUSTER_CHECK_NONE; parameter = input->parameter_next[parameter])
        {
            const TypeId parameter_type = input->declared_types[parameter];
            const CheckInputAttributes* attributes = input->attributes[parameter];
            // A struct or union parameter is a frame object, at most aligned to 16, whose bytes the prologue places.
            const bool aggregate = check_lower_aggregate(types, parameter_type);
            const uint32_t declared_alignment = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
            const uint32_t alignment = type_alignment_log2(types, parameter_type) > declared_alignment ? type_alignment_log2(types, parameter_type) : declared_alignment;
            const char* passed = check_lower_passed(&writer, parameter_type);
            writer.token = parameter;
            if (alignment > (aggregate ? BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 : 3u))
            {
                check_lower_unsupported(&writer, aggregate ? "parameters aligned past 16 bytes" : "parameters aligned past 8 bytes");
            }
            if (passed != NULL)
            {
                check_lower_unsupported(&writer, passed);
            }
            const uint32_t local = aggregate ? check_lower_object(&writer, type_size(types, parameter_type), alignment) :
                                               check_lower_temporary(&writer, check_lower_class(types, parameter_type));
            if ((type_qualifiers(parameter_type) & BUSTER_TYPE_VOLATILE) != 0 && local < writer.local_count)
            {
                writer.local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            writer.local_map[input->objects[parameter]] = local;
            abi.arguments[abi.argument_count++] = parameter_type;
            function->parameter_count += 1;
            declared += 1;
        }
        writer.token = body.declaration;
        abi.counts[abi.count] = declared;
        abi.named[abi.count++] = declared;
        if (writer.variadic && lowered->status == CHECK_PROGRAM_OK)
        {
            // va_start's offsets: the named arguments' registers, as abi_assign gives the function's own call (the batch
            // below gives it the same).
            const uint64_t mark = scratch->length;
            AbiLocation* named = BUSTER_ARENA_PUSH_ARRAY(scratch, AbiLocation, (uint64_t)declared + 1);
            AbiCall own = { 0 };
            if (named == 0)
            {
                check_lower_fail(&writer, CHECK_PROGRAM_RESOURCE);
            }
            else
            {
                const uint8_t variadic = 1;
                const AbiSignatures signature = {
                    .results = abi.results + signatures[b],
                    .counts = &declared,
                    .named = &declared,
                    .variadic = &variadic,
                    .count = 1,
                    .arguments = abi.arguments + abi.first[signatures[b]],
                };
                abi_assign_scalar(&table, signature, named, &own);
            }
            arena_pop_to(scratch, mark);
            writer.gp_offset = BUSTER_ABI_SLOT_BYTES * own.integer_registers;
            writer.fp_offset = BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * own.vector_registers;
        }
        for (uint32_t declaration = local_first[body.opening]; declaration != BUSTER_CHECK_NONE; declaration = local_next[declaration])
        {
            if (writer.local_map[input->objects[declaration]] == BUSTER_CHECK_NONE)
            {
                const TypeId local_type = input->declared_types[declaration];
                const uint8_t kind = type_kind(types, type_strip_aligned(types, local_type));
                const CheckInputAttributes* attributes = input->attributes[declaration];
                // The attribute's alignment is its log2 plus one, 0 for none.
                const uint32_t declared_alignment = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1u : 0;
                const uint32_t alignment = type_alignment_log2(types, local_type) > declared_alignment ? type_alignment_log2(types, local_type) : declared_alignment;
                uint32_t local = BUSTER_CHECK_NONE;
                writer.token = declaration;
                if (check_action_runtime_size(types, local_type))
                {
                    // A variable-length array lives on the stack from its declaration (CHECK_CONTROL_ALLOCATE) to its
                    // block's end: its local holds its address.
                    local = check_lower_temporary(&writer, BUSTER_TYPED_CLASS_U64);
                    if (local < writer.local_count)
                    {
                        writer.indirect[local] = BUSTER_CHECK_LOWER_VLA_DECLARED;
                    }
                }
                else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                {
                    // Arrays, structs and unions are frame objects, at most aligned to 16 (a frame pointer would take
                    // more). An incomplete type has no size at all.
                    if (!type_is_complete(types, local_type))
                    {
                        check_lower_unsupported(&writer, "local objects of incomplete type");
                    }
                    local = check_lower_object(&writer, type_size(types, local_type), alignment);
                }
                else if (type_kind_is_array(kind) || alignment > 3)
                {
                    check_lower_unsupported(&writer, "local values aligned past 8 bytes");
                }
                else
                {
                    local = check_lower_temporary(&writer, check_lower_class(types, local_type));
                }
                if ((type_qualifiers(local_type) & BUSTER_TYPE_VOLATILE) != 0 && local < writer.local_count)
                {
                    writer.local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                }
                writer.local_map[input->objects[declaration]] = local;
            }
        }
        for (uint32_t i = 0; i < body.entry_count && lowered->status == CHECK_PROGRAM_OK; i += 1)
        {
            check_lower_control(&writer, i);
            check_lower_drain(&writer);
        }
        // Whether control reaches the function's end: a statement that falls through to it, or a branch to it.
        const bool falls = writer.track ? writer.reachable || writer.label_heads[body.count] != BUSTER_CHECK_NONE :
                                          writer.count == 0 || writer.kinds[writer.count - 1] != BUSTER_TYPED_RETURN;
        check_lower_place(&writer, body.count);
        // The function's end, its implicit return, is its closing brace's (an opener's parent is its closer).
        writer.source = program->parser->tree.parents[body.opening];
        for (uint32_t local = 0; local < writer.local_count; local += 1)
        {
            if (writer.indirect[local] == BUSTER_CHECK_LOWER_VLA_DECLARED)
            {
                // A variable-length array the control stream never allocated: its address would be garbage.
                check_lower_fail(&writer, CHECK_PROGRAM_INVALID);
            }
        }
        // C specifies zero for main's implicit return. Other non-void fallthrough remains undefined behavior, but a
        // struct or union result's return there must still give Tier 0 an address (or the hidden pointer) to return.
        if (result == BUSTER_TYPE_VOID)
        {
            check_lower_emit(&writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        }
        else if (body.is_main && result == BUSTER_TYPE_INT)
        {
            check_lower_emit(&writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(&writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I32, 0);
        }
        else if (aggregate_result && falls)
        {
            check_lower_return_aggregate(&writer, BUSTER_CHECK_NONE);
            check_lower_drain(&writer);
        }
        for (uint32_t i = 0; i < writer.count && lowered->status == CHECK_PROGRAM_OK; i += 1)
        {
            if (writer.kinds[i] >= BUSTER_TYPED_BRANCH_FALSE && writer.kinds[i] <= BUSTER_TYPED_JUMP_VALUE)
            {
                const uint64_t label = writer.values[i];
                if (label >= writer.label_count || writer.labels[label] == BUSTER_CHECK_NONE)
                {
                    check_lower_fail(&writer, CHECK_PROGRAM_INVALID);
                }
                else
                {
                    writer.values[i] = writer.labels[label];
                }
            }
        }
#define BUSTER_CHECK_LOWER_COPY(field, source, type, length) \
        if (lowered->status == CHECK_PROGRAM_OK && (length) != 0) \
        { \
            type* column = BUSTER_ARENA_PUSH_ARRAY(output, type, length); \
            if (column != 0) \
            { \
                memcpy(column, writer.source, (uint64_t)(length) * sizeof(type)); \
                function->field = column; \
            } \
            else \
            { \
                check_lower_fail(&writer, CHECK_PROGRAM_RESOURCE); \
            } \
        }
        BUSTER_CHECK_LOWER_COPY(kinds, kinds, uint8_t, writer.count);
        BUSTER_CHECK_LOWER_COPY(classes, classes, uint8_t, writer.count);
        BUSTER_CHECK_LOWER_COPY(values, values, uint64_t, writer.count);
        BUSTER_CHECK_LOWER_COPY(local_classes, local_classes, uint8_t, writer.local_count);
        BUSTER_CHECK_LOWER_COPY(local_flags, local_flags, uint8_t, writer.local_count);
        if (writer.objects)
        {
            BUSTER_CHECK_LOWER_COPY(local_sizes, local_sizes, uint32_t, writer.local_count);
            BUSTER_CHECK_LOWER_COPY(local_alignments, local_alignments, uint8_t, writer.local_count);
        }
        BUSTER_CHECK_LOWER_COPY(call_targets, call_targets, SymbolId, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_first, call_first, uint32_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_argument_counts, call_counts, uint32_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_variadic, call_variadic, uint8_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(argument_classes, argument_classes, uint8_t, writer.argument_count);
        if (writer.aggregates)
        {
            BUSTER_CHECK_LOWER_COPY(argument_sizes, argument_sizes, uint32_t, writer.argument_count);
            BUSTER_CHECK_LOWER_COPY(call_result_objects, call_result_objects, uint32_t, writer.call_count);
        }
#undef BUSTER_CHECK_LOWER_COPY
        if (sources && lowered->status == CHECK_PROGRAM_OK)
        {
            uint32_t* tokens = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)writer.count + 1);
            if (tokens != 0)
            {
                memcpy(tokens, writer.tokens, (uint64_t)writer.count * sizeof(uint32_t));
                lowered->entry_tokens[b] = tokens;
            }
            else
            {
                check_lower_fail(&writer, CHECK_PROGRAM_RESOURCE);
            }
        }
        // A variadic definition that starts its list has its incoming frame (Tier0Function.incoming_variadic): its own
        // call, which abi_assign gives below.
        incoming[b] = writer.save_anchor != BUSTER_CHECK_NONE;
        function->count = writer.count;
        function->local_count = writer.local_count;
        function->call_count = writer.call_count;
        function->argument_count = writer.argument_count;
        lowered->entries += writer.count;
        for (uint32_t declaration = local_first[body.opening]; declaration != BUSTER_CHECK_NONE; declaration = local_next[declaration])
        {
            writer.local_map[input->objects[declaration]] = BUSTER_CHECK_NONE;
        }
        for (uint32_t parameter = input->parameters[body.opening]; parameter != BUSTER_CHECK_NONE; parameter = input->parameter_next[parameter])
        {
            writer.local_map[input->objects[parameter]] = BUSTER_CHECK_NONE;
        }
    }
    AbiLocation* locations = valid && lowered->status == CHECK_PROGRAM_OK ? BUSTER_ARENA_PUSH_ARRAY(output, AbiLocation, (uint64_t)abi.argument_count + 1) : 0;
    AbiCall* calls = valid && lowered->status == CHECK_PROGRAM_OK ? BUSTER_ARENA_PUSH_ARRAY(output, AbiCall, (uint64_t)abi.count + 1) : 0;
    valid = valid && lowered->status == CHECK_PROGRAM_OK && locations != 0 && calls != 0;
    if (valid)
    {
        for (uint32_t i = 0; i < abi.argument_count + abi.count; i += 1)
        {
            const TypeId type = i < abi.argument_count ? abi.arguments[i] : abi.results[i - abi.argument_count];
            const uint32_t index = type_index(type);
            if ((table.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) == 0 || (table.pass[index] & BUSTER_ABI_PASS_UNSUPPORTED) != 0 || table.stack_bytes[index] == UINT32_MAX)
            {
                check_lower_unsupported(&writer, "this type in a call (its calling convention)");
            }
        }
        const AbiSignatures signature = {
            .results = abi.results,
            .counts = abi.counts,
            .named = abi.named,
            .variadic = abi.variadic,
            .count = abi.count,
            .arguments = abi.arguments,
        };
        if (lowered->status == CHECK_PROGRAM_OK)
        {
            if (scalar)
            {
                abi_assign_scalar(&table, signature, locations, calls);
            }
            else
            {
                abi_assign(&table, signature, locations, calls);
            }
        }
        for (uint32_t b = 0; b < program->body_count && lowered->status == CHECK_PROGRAM_OK; b += 1)
        {
            Tier0Function* function = &lowered->functions[b];
            const uint32_t signature = signatures[b];
            const uint32_t first = abi.first[signature];
            const uint32_t declared = abi.counts[signature];
            function->parameters = locations + first;
            function->result = calls[signature].result;
            function->calls = calls + signature + 1;
            function->argument_locations = locations + first + declared;
            function->incoming_variadic = incoming[b] ? calls + signature : 0;
            if (function->parameter_count != declared)
            {
                // The hidden pointer, parameter 0, in rdi before the declared parameters' locations.
                AbiLocation* parameters = BUSTER_ARENA_PUSH_ARRAY(output, AbiLocation, function->parameter_count);
                if (parameters == 0)
                {
                    lowered->status = CHECK_PROGRAM_RESOURCE;
                }
                else
                {
                    parameters[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
                    memcpy(parameters + 1, locations + first, (uint64_t)declared * sizeof(AbiLocation));
                    function->parameters = parameters;
                }
            }
            const Tier0Check check = lowered->status == CHECK_PROGRAM_OK ? tier0_validate(function) : (Tier0Check){ .error = TIER0_OK };
            if (check.error != TIER0_OK)
            {
                lowered->status = CHECK_PROGRAM_INVALID;
                lowered->token = program->bodies[b].declaration;
                lowered->backend_node = check.node;
                lowered->reason = check.rule;
            }
        }
    }
    valid = valid && lowered->status == CHECK_PROGRAM_OK;
    if (!valid)
    {
        const CheckProgramStatus status = lowered->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : lowered->status;
        const uint32_t token = lowered->token;
        const uint32_t backend_node = lowered->backend_node;
        const char* reason = lowered->reason;
        arena_pop_to(output, mark);
        *lowered = (CheckProgramLowered){ .status = status, .token = token, .backend_node = backend_node, .reason = reason };
    }
    else
    {
        lowered->count = program->body_count;
        lowered->output_bytes = output->length - mark;
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
