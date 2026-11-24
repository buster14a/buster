// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Call-site warnings for #1394. Included after the common warning helpers. One operator scan visits calls and the
// builtin forms that retain their original source calls. Nonnull reads the declaration visible at the call; builtin
// policy governs fortify diagnostics, while sizeof-pointer-memaccess checks the C library spelling
// even when builtin lowering is disabled. Only proven object extents are used: an arbitrary pointer's pointee type
// never proves a buffer size. Clang 23.1.4 supplies behavior, not implementation or fixtures.

typedef struct CheckCommonCall CheckCommonCall;
struct CheckCommonCall
{
    uint32_t token;
    uint32_t declaration;
    uint32_t closing;
    uint32_t offset;
    uint32_t count;
    String8 name;
    String8 plain;
    bool function;
    bool builtin;
    bool explicit_builtin;
    bool direct_contract;
};

// The checked library calls and their argument conventions. Fortify uses the destination and either the
// byte count or a literal string. The sizeof check compares the size operand with the first one or two pointers.
typedef enum CheckFortifyKind
{
    CHECK_FORTIFY_NONE, CHECK_FORTIFY_STRING, CHECK_FORTIFY_LIMITED, CHECK_FORTIFY_MEMORY, CHECK_FORTIFY_BZERO, CHECK_FORTIFY_BCOPY,
} CheckFortifyKind;

typedef struct CheckMemoryCall CheckMemoryCall;
struct CheckMemoryCall
{
    const char* name;
    uint8_t length;
    uint8_t fortify;
    uint8_t size_index;
    uint8_t pointers;
};

static const CheckMemoryCall* check_common_memory_call(String8 name)
{
#define CHECK_MEMORY(name, fortify, size, pointers) { name, sizeof(name) - 1, CHECK_FORTIFY_##fortify, size, pointers }
    static const CheckMemoryCall calls[] = {
        CHECK_MEMORY("bcopy", BCOPY, 0, 0), CHECK_MEMORY("bcmp", NONE, 2, 2), CHECK_MEMORY("bzero", BZERO, 1, 1),
        CHECK_MEMORY("memcpy", MEMORY, 2, 2), CHECK_MEMORY("memmove", MEMORY, 2, 2), CHECK_MEMORY("mempcpy", MEMORY, 0, 0),
        CHECK_MEMORY("memset", MEMORY, 2, 1), CHECK_MEMORY("memcmp", NONE, 2, 2),
        CHECK_MEMORY("strcpy", STRING, 0, 0), CHECK_MEMORY("stpcpy", STRING, 0, 0), CHECK_MEMORY("strcat", STRING, 0, 0),
        CHECK_MEMORY("strncpy", LIMITED, 2, 2), CHECK_MEMORY("stpncpy", LIMITED, 0, 0), CHECK_MEMORY("strncat", LIMITED, 2, 2),
        CHECK_MEMORY("strndup", NONE, 1, 1),
    };
#undef CHECK_MEMORY
    if (name.length >= 4 && name.length <= 7 && (name.pointer[0] == 'b' || name.pointer[0] == 'm' || name.pointer[0] == 's'))
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(calls); i += 1)
        {
            if (calls[i].length == name.length && memcmp(name.pointer, calls[i].name, name.length) == 0)
            {
                return &calls[i];
            }
        }
    }
    return NULL;
}

// Strip only graph conversions with no source cast. An explicit cast can deliberately change a contract or type.
static uint32_t check_common_call_unconverted(const CheckGraph* graph, uint32_t node)
{
    while (node < graph->count && graph->ops[node] == CHECK_OP_CONVERT && graph->operand0[node] < node)
    {
        node = graph->operand0[node];
    }
    return node;
}

static CheckCommonCall check_common_call_source(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                               uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t limit = input->token_count < parser->tokens.count ? input->token_count : parser->tokens.count;
    CheckCommonCall result = { .token = BUSTER_CHECK_NONE, .declaration = BUSTER_CHECK_NONE, .direct_contract = true,
                               .closing = graph->tokens[node], .offset = graph->argument_offsets[node], .count = graph->argument_counts[node] };
    const uint32_t callee = graph->ops[node] == CHECK_OP_CALL ? check_common_callee_source(graph, graph->operand0[node], &result.direct_contract) : BUSTER_CHECK_NONE;
    uint32_t named = callee < graph->count && graph->ops[callee] == CHECK_OP_VALUE ? graph->tokens[callee] : BUSTER_CHECK_NONE;
    if (graph->ops[node] != CHECK_OP_CALL && result.closing < limit)
    {
        const uint32_t id = input->intrinsic_ids != NULL ? input->intrinsic_ids[result.closing] : 0;
        const CheckInputIntrinsicCall* source = id != 0 && id <= input->intrinsic_call_count ? input->intrinsic_calls + id - 1 : NULL;
        const uint32_t opening = source == NULL ? check_common_ignored_opener(parser, result.closing) : source->opening;
        named = source != NULL ? source->keyword : opening != BUSTER_CHECK_NONE && opening > 0 ? opening - 1 : BUSTER_CHECK_NONE;
    }
    if (named < limit && parser->tokens.names[named] != 0)
    {
        const uint32_t name = parser->tokens.names[named];
        result.name = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
        result.plain = result.name;
        result.explicit_builtin = result.name.length > 10 && memcmp(result.name.pointer, "__builtin_", 10) == 0;
        if (result.explicit_builtin)
        {
            result.plain.pointer += 10;
            result.plain.length -= 10;
        }
        result.declaration = input->declarations != NULL ? input->declarations[named] : BUSTER_CHECK_NONE;
        const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
        result.declaration = overload != BUSTER_CHECK_NONE ? stream->overloads[overload].selected.declaration : result.declaration;
        result.function = result.declaration < limit && input->declaration_kinds != NULL &&
                          input->declaration_kinds[result.declaration] == C_BINDING_FUNCTION;
        const bool external = result.function && check_input_linkage(input->declaration_bits, result.declaration) == 2;
        result.builtin = result.explicit_builtin || (external && pp_library_builtin(&pp->options, (const uint8_t*)result.name.pointer, (uint32_t)result.name.length));
        result.token = named;
        if (graph->ops[node] == CHECK_OP_CALL && graph->operand0[node] < graph->count)
        {
            result.token = check_warning_graph_first(graph, parser, graph->operand0[node]);
        }
    }
    return result;
}

// A checked integer used as a byte count or offset. Side effects and evaluation faults cannot establish a bound.
static bool check_common_call_integer(const CheckStream* stream, const TypeTable* types, uint32_t node, __int128* integer)
{
    const CheckValue value = check_stream_value(stream, node);
    uint32_t width = 0;
    bool signed_type = false;
    const bool known = value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE &&
                       (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION)) == 0 &&
                       check_action_integer_type(types, value.type, &width, &signed_type) && width <= 128;
    *integer = known ? check_common_integer_value(&value, width, signed_type) : 0;
    return known;
}

static void check_common_call_nonnull(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                      TypeTable* types, const CheckCommonCall* call, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const CheckInputAttributes* attributes = call->declaration < input->token_count ? check_input_attributes_at(input, call->declaration) : NULL;
    for (uint32_t argument = 0; attributes != NULL && argument < call->count; argument += 1)
    {
        const uint32_t node = graph->argument_nodes[call->offset + argument];
        if (node >= graph->count)
        {
            continue;
        }
        bool required = false;
        bool all = false;
        for (uint32_t a = 0; a < attributes->count; a += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + a;
            if (attribute->kind == CHECK_ATTRIBUTE_NONNULL && !attribute->ignored && !attribute->subject_ignored &&
                (call->direct_contract || attribute->keyword))
            {
                all |= attribute->argument_count == 0;
                for (uint32_t index = 0; attribute->argument_values != NULL && index < attribute->argument_count; index += 1)
                {
                    required |= attribute->argument_values[index] == (int64_t)argument + 1;
                }
            }
        }
        const CheckValue value = check_stream_value(stream, node);
        const CheckConversion conversion = stream->argument_conversions != NULL ? stream->argument_conversions[call->offset + argument] : (CheckConversion){ 0 };
        const TypeId type = conversion.type != 0 ? conversion.type : value.type;
        required = (required || all) && type != 0 && check_input_nonnull_pointer(types, type);
        bool truth = true;
        const bool null = required && value.fault == CHECK_FAULT_NONE && (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION)) == 0 &&
                          (check_action_null(types, &value, 0) || (value.constant_kind != CHECK_CONSTANT_NONE && check_action_truth(types, &value, &truth) && !truth));
        if (null)
        {
            const uint32_t first = check_common_type_safety_first(parser, call->closing, argument);
            const uint32_t token = all ? call->closing : check_common_type_safety_location(parser, graph, node, first, check_common_type_safety_last(parser, first));
            check_common_report_deferred(parser, pp, input, token, CHECK_WARNING_ID_NONNULL, false,
                                          "null passed to a callee that requires a non-null argument", NULL, 0, scratch);
        }
    }
}

typedef struct CheckCommonCallObject CheckCommonCallObject;
struct CheckCommonCallObject
{
    uint32_t root;
    uint64_t offset;
    uint64_t remaining;
    bool known;
};

// Follow a pointer's actual object path, accumulating checked constant offsets in a wide temporary. This accepts array
// decay and the address of an object, but stops at a pointer read, an unknown offset or an effectful path.
static CheckCommonCallObject check_common_call_object(const CheckInput* input, const CheckStream* stream, const TypeTable* types, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    CheckCommonCallObject result = { .root = BUSTER_CHECK_NONE };
    __int128 offset = 0;
    bool address = false;
    for (uint32_t steps = 0; node < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        const CheckValue value = check_stream_value(stream, node);
        const TypeId type = value.type != 0 ? type_unqualified(type_strip_aligned(types, value.type)) : 0;
        const uint8_t kind = type != 0 ? type_kind(types, type) : BUSTER_TYPE_KIND_NONE;
        if (value.fault != CHECK_FAULT_NONE || (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION)) != 0)
        {
            break;
        }
        if (op == CHECK_OP_CONVERT || op == CHECK_OP_CAST || op == CHECK_OP_ADDRESS)
        {
            if (op != CHECK_OP_ADDRESS && kind != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(kind))
            {
                break;
            }
            address |= op == CHECK_OP_ADDRESS;
            node = graph->operand0[node];
        }
        else if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || (op == CHECK_OP_SUBSCRIPT && (address || type_kind_is_array(kind))))
        {
            uint32_t base = graph->operand0[node];
            uint32_t index = graph->operand1[node];
            TypeId base_type = base < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, base))) : 0;
            uint8_t base_kind = base_type != 0 ? type_kind(types, base_type) : BUSTER_TYPE_KIND_NONE;
            if (op != CHECK_OP_SUBTRACT && base_kind != BUSTER_TYPE_KIND_POINTER && !type_kind_is_array(base_kind))
            {
                const uint32_t swap = base;
                base = index;
                index = swap;
                base_type = base < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, base))) : 0;
                base_kind = base_type != 0 ? type_kind(types, base_type) : BUSTER_TYPE_KIND_NONE;
            }
            __int128 index_value = 0;
            const TypeId element = base_type != 0 && (base_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(base_kind)) ? type_element(types, base_type) : 0;
            if (base >= node || index >= node || element == 0 || !type_is_complete(types, element) ||
                !check_common_call_integer(stream, types, index, &index_value) || index_value < INT64_MIN || index_value > INT64_MAX)
            {
                break;
            }
            __int128 displacement = 0;
            if (__builtin_mul_overflow(op == CHECK_OP_SUBTRACT ? -index_value : index_value, (__int128)type_size(types, element), &displacement) ||
                __builtin_add_overflow(offset, displacement, &offset))
            {
                break;
            }
            node = base;
            address = false;
        }
        else if (op == CHECK_OP_MEMBER && (address || type_kind_is_array(kind)))
        {
            const uint32_t base = graph->operand0[node];
            const CheckValue parent = base < node ? check_stream_value(stream, base) : (CheckValue){ 0 };
            if (base >= node || value.object == 0 || value.object != parent.object)
            {
                break;
            }
            if (__builtin_add_overflow(offset, (__int128)value.offset - parent.offset, &offset))
            {
                break;
            }
            node = base;
            address = true;
        }
        else
        {
            const bool object = op == CHECK_OP_VALUE && value.category == CHECK_LVALUE && value.object != 0 &&
                                (address || type_kind_is_array(kind)) && type_is_complete(types, type) && kind != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
            const uint64_t size = object ? type_size(types, type) : 0;
            if (object)
            {
                const uint64_t start = (unsigned __int128)offset < size ? (uint64_t)offset : size;
                result = (CheckCommonCallObject){ .root = node, .offset = start, .remaining = size - start, .known = true };
            }
            break;
        }
    }
    return result;
}

static void check_common_call_fortify(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                      TypeTable* types, const CheckCommonCall* call, CheckFortifyKind kind, Arena* scratch)
{
    const bool string = kind == CHECK_FORTIFY_STRING;
    const bool bzero = kind == CHECK_FORTIFY_BZERO;
    const bool bcopy = kind == CHECK_FORTIFY_BCOPY;
    const uint32_t required = string || bzero ? 2u : 3u;
    const bool candidate = kind != CHECK_FORTIFY_NONE && call->count >= required;
    bool builtin = call->explicit_builtin;
    if (candidate && !builtin && call->function)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(call->name.pointer, (uint32_t)call->name.length);
        const TypeId declared = type_strip_aligned(types, input->declared_types[call->declaration]);
        if (info != NULL && type_kind(types, declared) == BUSTER_TYPE_KIND_FUNCTION)
        {
            uint32_t position = 0;
            builtin = (types->types.flags[type_index(declared)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ?
                      check_library_matches_prototype(types, declared, info->encoding) :
                      check_library_plain_type(types, type_return(types, declared), info->encoding, &position);
        }
    }
    if (candidate && builtin)
    {
        const CheckGraph* graph = &input->graph;
        const uint32_t destination = graph->argument_nodes[call->offset + (bcopy ? 1u : 0u)];
        const CheckCommonCallObject object = check_common_call_object(input, stream, types, destination);
        __int128 amount = 0;
        bool known = false;
        if (object.known && string)
        {
            const uint32_t source = graph->argument_nodes[call->offset + 1];
            const CheckCommonCallObject from = check_common_call_object(input, stream, types, source);
            const uint32_t token = from.known ? graph->tokens[from.root] : BUSTER_CHECK_NONE;
            const uint32_t index = token < input->token_count ? check_common_sparse_get(input->string_ids, token) : BUSTER_CHECK_NONE;
            const CheckInitString* literal = index < input->string_count ? input->strings + index : NULL;
            if (literal != NULL && type_element(types, literal->type) == BUSTER_TYPE_CHAR && from.offset < literal->byte_count)
            {
                const uint8_t* begin = literal->bytes + from.offset;
                const uint8_t* nul = memchr(begin, 0, literal->byte_count - from.offset);
                known = nul != NULL;
                amount = known ? (__int128)(nul - begin) + 1 : 0;
            }
        }
        else if (object.known)
        {
            const uint32_t source = graph->argument_nodes[call->offset + (bzero ? 1u : 2u)];
            known = source < graph->count && check_common_call_integer(stream, types, source, &amount);
            // The declared size_t argument converts signed negative sizes to the target's unsigned width.
            amount = known ? (uint64_t)amount : 0;
        }
        if (known && (unsigned __int128)amount > object.remaining)
        {
            const char* message = string ? check_warning_text(scratch, "'%.*s' will always overflow; destination buffer has size %llu, but the source string has length %llu (including NUL byte)",
                                          (int)call->plain.length, call->plain.pointer, (unsigned long long)object.remaining, (unsigned long long)amount) :
                                  check_warning_text(scratch, "'%.*s' %s; destination buffer has size %llu, but size argument is %llu",
                                          (int)call->plain.length, call->plain.pointer, kind == CHECK_FORTIFY_LIMITED ? "size argument is too large" : "will always overflow",
                                          (unsigned long long)object.remaining, (unsigned long long)amount);
            if (message != NULL)
            {
                check_common_report_deferred(parser, pp, input, call->token, CHECK_WARNING_ID_FORTIFY_SOURCE, false, message, NULL, 0, scratch);
            }
        }
    }
}

typedef struct CheckCommonCallPair CheckCommonCallPair;
struct CheckCommonCallPair
{
    uint32_t left;
    uint32_t right;
};

// The source expression identity needed by sizeof-pointer-memaccess. Grouping and implicit conversions are transparent;
// named lvalues retain their declaration identity. A small dynamic worklist handles compound pointer expressions.
static bool check_common_call_same(const CheckGraph* graph, const CheckStream* stream, uint32_t left, uint32_t right,
                                   Preprocessor* pp, Arena* scratch)
{
    uint32_t capacity = 16;
    CheckCommonCallPair* pairs = CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonCallPair, capacity);
    bool equal = pairs != NULL;
    uint32_t count = equal ? 1u : 0u;
    if (equal)
    {
        pairs[0] = (CheckCommonCallPair){ left, right };
    }
    else
    {
        check_common_exhausted(pp);
    }
    while (equal && count != 0)
    {
        const CheckCommonCallPair pair = pairs[--count];
        const uint32_t a = check_common_call_unconverted(graph, pair.left);
        const uint32_t b = check_common_call_unconverted(graph, pair.right);
        equal = a < graph->count && b < graph->count && graph->ops[a] == graph->ops[b] && check_stream_type(stream, a) == check_stream_type(stream, b);
        const CheckOp op = equal ? graph->ops[a] : CHECK_OP_COUNT;
        if (equal && op == CHECK_OP_VALUE)
        {
            const CheckValue x = check_stream_value(stream, a);
            const CheckValue y = check_stream_value(stream, b);
            equal = x.category == y.category && (x.object != 0 ? x.object == y.object && x.offset == y.offset :
                    x.constant_kind == CHECK_CONSTANT_INTEGER && y.constant_kind == CHECK_CONSTANT_INTEGER && x.integer == y.integer);
        }
        else if (equal)
        {
            equal = !check_graph_has_arguments(op) && op != CHECK_OP_STATEMENT_VALUE && graph->members[a] == graph->members[b];
            const uint32_t xa[3] = { graph->operand0[a], graph->operand1[a], graph->operand2[a] };
            const uint32_t xb[3] = { graph->operand0[b], graph->operand1[b], graph->operand2[b] };
            for (uint32_t child = 0; equal && child < 3; child += 1)
            {
                const bool live = xa[child] < a;
                equal = live == (xb[child] < b);
                if (equal && live)
                {
                    if (count == capacity)
                    {
                        const uint32_t grown = capacity <= UINT32_MAX / 2 ? capacity * 2 : 0;
                        CheckCommonCallPair* next = grown != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckCommonCallPair, grown) : NULL;
                        if (next != NULL)
                        {
                            memcpy(next, pairs, (uint64_t)count * sizeof(*pairs));
                            pairs = next;
                            capacity = grown;
                        }
                        else
                        {
                            check_common_exhausted(pp);
                            equal = false;
                        }
                    }
                    if (equal)
                    {
                        pairs[count++] = (CheckCommonCallPair){ xa[child], xb[child] };
                    }
                }
            }
        }
    }
    return equal;
}

static void check_common_call_memaccess(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, CheckWarningSpelling* spell, const CheckCommonCall* call, const CheckMemoryCall* memory, Arena* scratch)
{
    const uint32_t length = memory->size_index;
    if (memory->pointers != 0 && call->count > length)
    {
        const CheckGraph* graph = &input->graph;
        const uint32_t size = check_common_call_unconverted(graph, graph->argument_nodes[call->offset + length]);
        const uint32_t operand = size < graph->count && graph->ops[size] == CHECK_OP_SIZEOF ? graph->operand0[size] : BUSTER_CHECK_NONE;
        bool reported = false;
        for (uint32_t argument = 0; !reported && operand < graph->count && argument < memory->pointers; argument += 1)
        {
            const uint32_t target = check_common_call_unconverted(graph, graph->argument_nodes[call->offset + argument]);
            const TypeId type = target < graph->count ? type_unqualified(type_strip_aligned(types, check_stream_type(stream, target))) : 0;
            const TypeId pointee = type != 0 && type_kind(types, type) == BUSTER_TYPE_KIND_POINTER ? type_element(types, type) : 0;
            const bool candidate = pointee != 0 && type_kind(types, type_unqualified(type_strip_aligned(types, pointee))) != BUSTER_TYPE_KIND_VOID;
            if (candidate && check_common_call_same(graph, stream, operand, target, pp, scratch))
            {
                const String8 pointer_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, type,
                                                                       check_common_operand_spelling(parser, input, graph, stream, types, target));
                const String8 pointee_name = check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, 0);
                const uint32_t token = graph->tokens[operand];
                const char* message = check_warning_text(scratch, "'%.*s' call operates on objects of type %.*s while the size is based on a different type %.*s",
                                         (int)call->plain.length, call->plain.pointer, (int)pointee_name.length, pointee_name.pointer,
                                         (int)pointer_name.length, pointer_name.pointer);
                const bool byte = type_is_complete(types, pointee) && type_size(types, pointee) == 1;
                const char* note_text = byte ? "did you mean to provide an explicit length?" : graph->ops[target] == CHECK_OP_ADDRESS ?
                                         "did you mean to remove the addressof in the argument to 'sizeof' (and multiply it by the number of elements)?" :
                                         "did you mean to dereference the argument to 'sizeof' (and multiply it by the number of elements)?";
                const CheckCommonNote note = { .token = token, .message = note_text };
                if (message != NULL && pointer_name.length != 0 && pointee_name.length != 0)
                {
                    // Its location is inside the size argument's sizeof. The enclosing call supplies the evaluated
                    // context: that inner sizeof is the error being diagnosed, not a reason to suppress it.
                    if (!check_common_unevaluated(parser, pp, input, call->closing, scratch) &&
                        !check_common_bound(parser, pp, input, call->closing, scratch))
                    {
                        const uint32_t boundary = check_common_function_boundary(parser, call->closing);
                        const uint32_t publication = check_common_publication_source(parser, pp, boundary);
                        check_common_report_published_phase(parser, pp, input, token, publication, PP_DIAGNOSTIC_DEFERRED,
                                                              CHECK_WARNING_ID_SIZEOF_POINTER_MEMACCESS, false, message, &note, 1, scratch);
                    }
                }
                reported = true;
            }
        }
    }
}

static void check_common_call_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                       TypeTable* types, CheckWarningSpelling* spell, const uint8_t* evaluated, const CheckCommonCandidates* candidates, Arena* scratch)
{
    const bool nonnull = check_common_enabled(pp, input, CHECK_WARNING_ID_NONNULL, false);
    const bool fortify = check_common_enabled(pp, input, CHECK_WARNING_ID_FORTIFY_SOURCE, false);
    const bool memaccess = check_common_enabled(pp, input, CHECK_WARNING_ID_SIZEOF_POINTER_MEMACCESS, false);
    const CheckGraph* graph = &input->graph;
    CheckCommonScan scan = { 0 };
    for (uint32_t node = nonnull || fortify || memaccess ? check_common_next_candidate(candidates, CHECK_COMMON_CANDIDATE_CALL, &scan) : graph->count;
         node < graph->count && !pp->fatal; node = check_common_next_candidate(candidates, CHECK_COMMON_CANDIDATE_CALL, &scan))
    {
        if (stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        const CheckCommonCall call = check_common_call_source(parser, pp, input, stream, node);
        const bool arguments = call.offset <= graph->argument_count && call.count <= graph->argument_count - call.offset;
        if (call.token >= parser->tokens.count || !arguments)
        {
            continue;
        }
        const bool live = evaluated != NULL && evaluated[node] != 0;
        if (nonnull && live)
        {
            check_common_call_nonnull(parser, pp, input, stream, types, &call, scratch);
        }
        const CheckMemoryCall* memory = fortify || memaccess ? check_common_memory_call(call.plain) : NULL;
        if (memory != NULL && call.builtin && fortify && live)
        {
            check_common_call_fortify(parser, pp, input, stream, types, &call, (CheckFortifyKind)memory->fortify, scratch);
        }
        if (memory != NULL && memaccess && (call.function || call.explicit_builtin))
        {
            check_common_call_memaccess(parser, pp, input, stream, types, spell, &call, memory, scratch);
        }
    }
}
