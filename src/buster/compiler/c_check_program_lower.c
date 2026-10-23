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
    uint32_t runtime_symbol_count;
    uint32_t count;
    CheckProgramStatus status;
    uint32_t token;
    uint32_t backend_node;
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
} CheckLowerTaskKind;

typedef struct CheckLowerTask CheckLowerTask;
struct CheckLowerTask
{
    CheckLowerTaskKind kind;
    uint32_t node;
    TypeId type;
    uint64_t value;
    bool address;
    bool initialized;
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
    uint32_t* labels;
    uint32_t* label_heads;
    uint32_t* stack_parents;
    uint8_t* stack_classes;
    uint32_t* local_map;
    uint32_t* switch_locals;
    uint32_t* extent_locals;
    uint32_t* initializer_declarations;
    uint8_t* local_classes;
    uint8_t* local_flags;
    SymbolId* call_targets;
    uint32_t* call_first;
    uint32_t* call_counts;
    uint8_t* call_variadic;
    uint8_t* argument_classes;
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
    uint32_t stack_head;
    uint32_t stack_count;
    bool reachable;
    const CheckProgramBody* body;
    TypeId result;
    bool scalar;
    bool track;
};

static void check_lower_fail(CheckLowerWriter* writer, CheckProgramStatus status)
{
    if (writer->output->status == CHECK_PROGRAM_OK)
    {
        writer->output->status = status;
        writer->output->token = writer->token;
    }
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

static uint64_t check_lower_stride(CheckLowerWriter* writer, TypeId pointer)
{
    const CheckAction action = { .extensions = writer->program->input->graph.extensions };
    const uint64_t size = check_action_pointer_stride(writer->program->types, pointer, &action);
    if (size == 0)
    {
        // Runtime-sized pointees require saved extent values. Tier0's immediate scale cannot stand in for them.
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
    if (kind <= BUSTER_TYPED_GLOBAL_ADDRESS || kind == BUSTER_TYPED_DUP)
    {
        push = true;
    }
    else if (kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT || kind == BUSTER_TYPED_CONVERT || kind == BUSTER_TYPED_LOAD)
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
        pop = value < writer->call_count ? writer->call_counts[value] : 0;
        push = class != BUSTER_TYPED_CLASS_VOID;
    }
    else if (kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE)
    {
        pop = 1;
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
    }
    else if (writer->count >= writer->capacity || writer->count >= BUSTER_TIER0_MAX_ENTRIES)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const bool operand = (kind >= BUSTER_TYPED_NEG && kind <= BUSTER_TYPED_SET_GLOBAL) ||
                             (kind >= BUSTER_TYPED_DISCARD && kind <= BUSTER_TYPED_BRANCH_TRUE) || kind == BUSTER_TYPED_JUMP_VALUE ||
                             (kind == BUSTER_TYPED_RETURN && class != BUSTER_TYPED_CLASS_VOID);
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
                writer->values[writer->count++] = 0;
                writer->kinds[writer->count] = BUSTER_TYPED_DISCARD;
                writer->classes[writer->count] = top;
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
            writer->count += 1;
        }
    }
}

static void check_lower_push(CheckLowerWriter* writer, CheckLowerTask task)
{
    if (writer->depth < writer->capacity)
    {
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
    if (node < writer->program->stream->count && writer->program->stream->values[node].overload_head == 0)
    {
        if (!address)
        {
            check_lower_convert(writer, writer->program->stream->values[node].type, target);
        }
        check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = node, .address = address });
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        writer->local_count += 1;
    }
    else
    {
        check_lower_fail(writer, class >= BUSTER_TYPED_CLASSES ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE);
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
        if (local != BUSTER_CHECK_NONE)
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

static void check_lower_truth(CheckLowerWriter* writer, TypeId type, bool negate)
{
    const uint8_t class = check_lower_class(writer->program->types, type);
    check_lower_instruction(writer, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
}

static void check_lower_conversion(CheckLowerWriter* writer, TypeId from, TypeId to)
{
    const TypeTable* types = writer->program->types;
    const uint8_t source = check_lower_class(types, from);
    const uint8_t target = check_lower_class(types, to);
    if (source >= BUSTER_TYPED_CLASSES || target >= BUSTER_TYPED_CLASSES)
    {
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        check_lower_emit(writer, BUSTER_TYPED_CONST, source, 0);
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
    bool scalar_parameters = true;
    for (uint32_t a = 0; a < parameters; a += 1)
    {
        scalar_parameters &= check_lower_class(types, parameter_types[a]) != UINT8_MAX;
    }
    for (uint32_t a = 0; a < count; a += 1)
    {
        scalar_parameters &= (stream->argument_conversions[first + a].flags & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) == 0;
    }
    if (mangled || !scalar_parameters || value.category == CHECK_LVALUE || value.constant_kind != CHECK_CONSTANT_ADDRESS || value.object == 0 || value.offset != 0 ||
        value.object >= writer->program->object_count || writer->program->object_symbols[value.object] == 0 ||
        (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0 || count > BUSTER_TIER0_MAX_CALL_ARGUMENTS)
    {
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
    }
    else if (abi->count >= abi->capacity || count > abi->argument_capacity - abi->argument_count)
    {
        check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
    }
    else
    {
        const uint32_t call = writer->call_count++;
        writer->call_targets[call] = writer->program->object_symbols[value.object];
        writer->call_first[call] = writer->argument_count;
        writer->call_counts[call] = count;
        writer->call_variadic[call] = variadic;
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = stream->values[node].type;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? parameters : count;
        abi->variadic[abi->count++] = variadic;
        for (uint32_t a = 0; a < count; a += 1)
        {
            const TypeId argument = stream->argument_conversions[first + a].type;
            abi->arguments[abi->argument_count++] = argument;
            writer->argument_classes[writer->argument_count++] = check_lower_class(types, argument);
        }
        check_lower_instruction(writer, BUSTER_TYPED_CALL, check_lower_class(types, stream->values[node].type), call);
        for (uint32_t a = count; a-- > 0;)
        {
            check_lower_operand(writer, node, a + 3, false, stream->argument_conversions[first + a].type);
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
            abi->first[abi->count] = abi->argument_count;
            abi->results[abi->count] = helper->result_type;
            abi->counts[abi->count] = count;
            abi->named[abi->count] = count;
            abi->variadic[abi->count++] = 0;
            for (uint32_t a = 0; a < count; a += 1)
            {
                const TypeId type = helper->parameter_types[a];
                abi->arguments[abi->argument_count++] = type;
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
        abi->first[abi->count] = abi->argument_count;
        abi->results[abi->count] = result;
        abi->counts[abi->count] = count;
        abi->named[abi->count] = count;
        abi->variadic[abi->count++] = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            abi->arguments[abi->argument_count++] = arguments[i];
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, check_lower_stride(writer, stored));
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
    if (result.error != CBUILTIN_LOWER_OK)
    {
        check_lower_fail(writer, result.error == CBUILTIN_LOWER_CAPACITY ? CHECK_PROGRAM_RESOURCE :
                                result.error == CBUILTIN_LOWER_UNSUPPORTED ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_INVALID);
    }
    else
    {
        uint32_t labels[BUSTER_CBUILTIN_LOWER_ENTRIES + 1];
        memset(labels, 0xff, ((uint64_t)result.count + 1) * sizeof(uint32_t));
        const uint32_t first = writer->count;
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
            writer->count += 1;
        }
        if (labels[result.count] != BUSTER_CHECK_NONE)
        {
            check_lower_place(writer, labels[result.count]);
        }
        const uint8_t class = check_lower_class(writer->program->types, type);
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
        }
        else if (selectors && effect->selected_selector < 5)
        {
            // A known category still retains Clang's exact evaluated selector prefix and its side effects.
            const uint32_t order[] = { 5, 4, 0, 1, 2, 3 };
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
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, subnormal);
                check_lower_operand(writer, node, 6, false, BUSTER_TYPE_INT);
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, result);
                check_lower_operand(writer, node, 5, false, BUSTER_TYPE_INT);
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        const uint8_t class = task.address ? BUSTER_TYPED_CLASS_U64 : check_lower_class(program->types, type);
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
    const uint8_t class = check_lower_class(types, value.type);
    writer->token = graph->tokens[node];
    const uint8_t value_kind = type_kind(types, type_strip_aligned(types, value.type));
    const bool address_value = value_kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(value_kind);
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
    else if ((value.flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        if (member == 0 || offset % 8 != 0)
        {
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
        }
        if (!task.address && !address_value)
        {
            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, 0);
        }
        if (offset != 0)
        {
            check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
        }
        check_lower_operand(writer, node, 0, op == CHECK_OP_MEMBER, type_a);
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
                check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, size);
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
                check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, size);
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
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
    }
}

static void check_lower_drain(CheckLowerWriter* writer)
{
    while (writer->depth != 0 && writer->output->status == CHECK_PROGRAM_OK)
    {
        const CheckLowerTask task = writer->tasks[--writer->depth];
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
        else
        {
            check_lower_expand(writer, task);
        }
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
        const uint8_t class = check_lower_class(program->types, plan->type);
        if (local == BUSTER_CHECK_NONE || class == UINT8_MAX)
        {
            check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
        }
        for (uint32_t part = plan->count; part-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
        {
            const CheckInitPart* write = &plan->parts[part];
            writer->token = write->token;
            if (write->bit_offset != 0 || write->bit_width != 0 || write->kind == CHECK_INIT_BYTES)
            {
                check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
            }
            else if (write->kind == CHECK_INIT_CLEAR)
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
                check_lower_instruction(writer, BUSTER_TYPED_CONST, class, 0);
            }
            else
            {
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, class, local);
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
                        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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
        }
    }
}

static void check_lower_case(CheckLowerWriter* writer, const CheckProgramCase* item)
{
    const uint32_t local = writer->switch_locals[item->owner];
    const uint8_t class = check_lower_class(writer->program->types, item->type);
    if (local == BUSTER_CHECK_NONE || class == UINT8_MAX)
    {
        check_lower_fail(writer, class == UINT8_MAX ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_INVALID);
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

static void check_lower_control(CheckLowerWriter* writer, uint32_t index)
{
    const CheckProgram* program = writer->program;
    const CheckControl control = program->controls[writer->body->first + index];
    check_lower_place(writer, index);
    writer->token = control.token;
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
    else if (control.kind == CHECK_CONTROL_ASM)
    {
        check_lower_fail(writer, CHECK_PROGRAM_UNSUPPORTED);
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

static bool check_program_lower(CheckProgramLowered* lowered, const CheckProgram* program, bool scalar, Arena* output, Arena* scratch)
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
    const uint64_t capacity64 = (uint64_t)nodes * 32 + (uint64_t)program->control_count * 4 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_ENTRIES + 32;
    const uint64_t local_capacity64 = (uint64_t)count + (uint64_t)nodes * 2 + (uint64_t)builtin_count * BUSTER_CBUILTIN_LOWER_TEMPORARIES + 1;
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
    BUSTER_CHECK_LOWER_COLUMN(labels, uint32_t, capacity);
    BUSTER_CHECK_LOWER_COLUMN(label_heads, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_parents, uint32_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(stack_classes, uint8_t, track ? capacity : 1);
    BUSTER_CHECK_LOWER_COLUMN(local_map, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_COLUMN(switch_locals, uint32_t, (uint64_t)count + 1);
    BUSTER_CHECK_LOWER_COLUMN(extent_locals, uint32_t, (uint64_t)input->vla_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(initializer_declarations, uint32_t, (uint64_t)nodes + 1);
    BUSTER_CHECK_LOWER_COLUMN(local_classes, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(local_flags, uint8_t, local_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_targets, SymbolId, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_first, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_counts, uint32_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(call_variadic, uint8_t, abi_capacity);
    BUSTER_CHECK_LOWER_COLUMN(argument_classes, uint8_t, argument_capacity);
    BUSTER_CHECK_LOWER_COLUMN(builtins, CBuiltinLowerCall, (uint64_t)builtin_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(miscellaneous, CBuiltinMiscLowerCall, (uint64_t)program->stream->misc_count + 1);
    BUSTER_CHECK_LOWER_COLUMN(floating, CBuiltinFloatLowerCall, (uint64_t)program->stream->floating_count + 1);
#undef BUSTER_CHECK_LOWER_COLUMN
    uint32_t* signatures = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->body_count + 1) : 0;
    uint32_t* local_first = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint32_t* local_next = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    lowered->functions = valid && program->body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, Tier0Function, program->body_count) : 0;
    valid = valid && signatures != 0 && local_first != 0 && local_next != 0 && (program->body_count == 0 || lowered->functions != 0);
    if (!valid && lowered->status == CHECK_PROGRAM_OK)
    {
        lowered->status = CHECK_PROGRAM_RESOURCE;
    }
    if (valid)
    {
        memset(writer.initializer_declarations, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
        memset(writer.local_map, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(writer.switch_locals, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
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
        writer.track = body.count != body.entry_count || terminal;
        writer.body = program->bodies + b;
        writer.result = result;
        writer.token = body.declaration;
        memset(writer.extent_locals, 0xff, ((uint64_t)input->vla_count + 1) * sizeof(uint32_t));
        for (uint32_t r = 0; r < body.reference_count; r += 1)
        {
            const uint32_t object = program->references[body.reference_first + r];
            const uint32_t declaration = program->object_declarations[object];
            if (declaration != BUSTER_CHECK_NONE && input->thread_local[declaration])
            {
                writer.token = declaration;
                check_lower_fail(&writer, CHECK_PROGRAM_UNSUPPORTED);
            }
        }
        memset(writer.labels, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        if (writer.track)
        {
            memset(writer.label_heads, 0xff, ((uint64_t)body.count + 1) * sizeof(uint32_t));
        }
        signatures[b] = abi.count;
        abi.first[abi.count] = abi.argument_count;
        abi.results[abi.count] = result;
        abi.variadic[abi.count] = 0;
        if ((types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0 || check_lower_class(types, result) == UINT8_MAX)
        {
            check_lower_fail(&writer, CHECK_PROGRAM_UNSUPPORTED);
        }
        for (uint32_t parameter = input->parameters[body.opening]; parameter != BUSTER_CHECK_NONE; parameter = input->parameter_next[parameter])
        {
            const TypeId parameter_type = input->declared_types[parameter];
            const CheckInputAttributes* attributes = input->attributes[parameter];
            if (type_alignment_log2(types, parameter_type) > 3 || (attributes != 0 && attributes->alignment_log2 > 4))
            {
                writer.token = parameter;
                check_lower_fail(&writer, CHECK_PROGRAM_UNSUPPORTED);
            }
            const uint32_t local = check_lower_temporary(&writer, check_lower_class(types, parameter_type));
            if ((type_qualifiers(parameter_type) & BUSTER_TYPE_VOLATILE) != 0 && local < writer.local_count)
            {
                writer.local_flags[local] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            writer.local_map[input->objects[parameter]] = local;
            abi.arguments[abi.argument_count++] = parameter_type;
            function->parameter_count += 1;
        }
        abi.counts[abi.count] = function->parameter_count;
        abi.named[abi.count++] = function->parameter_count;
        for (uint32_t declaration = local_first[body.opening]; declaration != BUSTER_CHECK_NONE; declaration = local_next[declaration])
        {
            if (writer.local_map[input->objects[declaration]] == BUSTER_CHECK_NONE)
            {
                const TypeId local_type = input->declared_types[declaration];
                const uint8_t kind = type_kind(types, type_strip_aligned(types, local_type));
                const CheckInputAttributes* attributes = input->attributes[declaration];
                if (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ||
                    type_alignment_log2(types, local_type) > 3 || (attributes != 0 && attributes->alignment_log2 > 4))
                {
                    writer.token = declaration;
                    check_lower_fail(&writer, CHECK_PROGRAM_UNSUPPORTED);
                }
                const uint32_t local = check_lower_temporary(&writer, check_lower_class(types, local_type));
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
        check_lower_place(&writer, body.count);
        // C specifies zero for main's implicit return. Other non-void fallthrough remains undefined behavior.
        if (result == BUSTER_TYPE_VOID)
        {
            check_lower_emit(&writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        }
        else if (body.is_main && result == BUSTER_TYPE_INT)
        {
            check_lower_emit(&writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(&writer, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I32, 0);
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
        BUSTER_CHECK_LOWER_COPY(call_targets, call_targets, SymbolId, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_first, call_first, uint32_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_argument_counts, call_counts, uint32_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(call_variadic, call_variadic, uint8_t, writer.call_count);
        BUSTER_CHECK_LOWER_COPY(argument_classes, argument_classes, uint8_t, writer.argument_count);
#undef BUSTER_CHECK_LOWER_COPY
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
    AbiTable table;
    AbiLocation* locations = valid && lowered->status == CHECK_PROGRAM_OK ? BUSTER_ARENA_PUSH_ARRAY(output, AbiLocation, (uint64_t)abi.argument_count + 1) : 0;
    AbiCall* calls = valid && lowered->status == CHECK_PROGRAM_OK ? BUSTER_ARENA_PUSH_ARRAY(output, AbiCall, (uint64_t)abi.count + 1) : 0;
    valid = valid && lowered->status == CHECK_PROGRAM_OK && locations != 0 && calls != 0 && abi_initialize(&table, scratch, types, 16);
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
        for (uint32_t i = 0; i < abi.argument_count + abi.count; i += 1)
        {
            const TypeId type = i < abi.argument_count ? abi.arguments[i] : abi.results[i - abi.argument_count];
            const uint32_t index = type_index(type);
            if ((table.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) == 0 || (table.pass[index] & BUSTER_ABI_PASS_UNSUPPORTED) != 0 || table.stack_bytes[index] == UINT32_MAX)
            {
                check_lower_fail(&writer, CHECK_PROGRAM_UNSUPPORTED);
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
            function->parameters = locations + first;
            function->result = calls[signature].result;
            function->calls = calls + signature + 1;
            function->argument_locations = locations + first + function->parameter_count;
            const Tier0Check check = tier0_validate(function);
            if (check.error != TIER0_OK)
            {
                lowered->status = CHECK_PROGRAM_INVALID;
                lowered->token = program->bodies[b].declaration;
                lowered->backend_node = check.node;
            }
        }
    }
    valid = valid && lowered->status == CHECK_PROGRAM_OK;
    if (!valid)
    {
        const CheckProgramStatus status = lowered->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : lowered->status;
        const uint32_t token = lowered->token;
        const uint32_t backend_node = lowered->backend_node;
        arena_pop_to(output, mark);
        *lowered = (CheckProgramLowered){ .status = status, .token = token, .backend_node = backend_node };
    }
    else
    {
        lowered->count = program->body_count;
        lowered->output_bytes = output->length - mark;
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
