// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's lean steps: the tasks' loop's EXPRESSION task for the ordinary shapes, one visit per node. Included by
// c_check_program_lower.c after check_lower_expand, whose rules it follows case for case: for each node it takes, it
// pushes the tasks and writes the entries the expansion would, in the same order, through the same helpers. What it
// leaves out is the expansion's per-node overhead: the node's 96-byte CheckValue (a step reads the columns its case needs,
// and the node's payload row only where it has one), the chain of tests before the node's case (a step tests only the
// cases before its own that its node could take), the call chain of each operand's push (check_lower_lean_operand), and
// the push and pop of the operand the node evaluates first, which is the task the expansion pushes last and the loop pops
// next: the step continues with it, its conversion pushed below it as before. Every other node, and any node whose shape
// the steps don't take, goes to the expansion unchanged, node by node: the task stack holds the continuation, so a step
// needs no proof about the node's subtree.
//
// Entry: check_lower_lean_step, from check_lower_drain_body's plain copy (check_lower_drain), where the writer takes the
// steps (CheckLowerWriter.lean: the vector path, without -g's sources, in a program without a _BitInt type, whose loop is
// another copy, of a stream of columns). --scalar takes the expansion, the steps' exact rules and oracle: `test-checker`
// compares their objects. The steps are scalar code: #1197 and #1201 measured a separate pass over every node on the
// 9700X, scalar and AVX-512, at more than the expansion it would replace (docs/c-lowering.md), so the work stays in the
// loop's one visit of each node.

// What a lean step did with a node (check_lower_lean_step): nothing, which leaves the node to the expansion; took it
// whole; or took it and continues with the operand it evaluates first.
#define BUSTER_CHECK_LOWER_LEAN_NONE 0u
#define BUSTER_CHECK_LOWER_LEAN_DONE 1u
#define BUSTER_CHECK_LOWER_LEAN_NEXT 2u

// The shapes' ops, as bits of a word: the wrappers, whose cases read neither the node's class nor its facts but for the
// generic cases' tests, and the typed ones, whose cases do.
#define BUSTER_CHECK_LOWER_LEAN_OP(op) (UINT64_C(1) << (op))
#define BUSTER_CHECK_LOWER_LEAN_WRAPPERS                                                                                                                 \
    (BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_CAST) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_CONVERT) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_POSITIVE) |          \
     BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_SELECT) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_ADDRESS) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_CONDITION) |      \
     BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_COMMA))
#define BUSTER_CHECK_LOWER_LEAN_TYPED                                                                                                                    \
    (BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_VALUE) | ((BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_BIT_XOR + 1) - 1) & ~(BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_NEGATIVE) - 1)) | \
     BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_DEREFERENCE) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_ASSIGN) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_SUBSCRIPT) |   \
     BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_MEMBER) | BUSTER_CHECK_LOWER_LEAN_OP(CHECK_OP_ARROW))

_Static_assert(CHECK_OP_SELECT < 64 && CHECK_OP_CONDITION < 64 && CHECK_OP_CONVERT < 64 && CHECK_OP_ARROW < 64 && CHECK_OP_BIT_XOR < 64,
               "the lean steps' ops are bits of a 64-bit word");

// A type's class and facts in one word (CheckLowerShared.type_templates' layout: the class, then the facts from bit 8): the
// packed table where the writer has it, else the two columns. A type past the table has no class and no facts, as
// check_lower_class and check_lower_facts give it.
static inline __attribute__((always_inline)) uint32_t check_lower_lean_type(const CheckLowerWriter* writer, TypeId type)
{
    const uint32_t index = type_index(type);
    uint32_t info = UINT8_MAX;
    if (writer->type_templates != NULL)
    {
        info = index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
    }
    else if (index < writer->type_count)
    {
        info = writer->type_classes[index] | (uint32_t)writer->type_facts[index] << 8;
    }
    return info;
}

// check_lower_operand's operand `operand` (0 or 1) of `node`, evaluated as an address or converted to `target`: its
// conversion task where it needs one, then its EXPRESSION task pushed, as check_lower_expression pushes them; or, where
// the expansion pushes it last (`last`), so that the loop would pop it next, its node and its task's flags (address,
// discard) in `child` and `flags` instead, for the step to continue with. Whether it gave them. The push left out keeps
// its failure: a stack with no room for it fails as check_lower_push does. An overloaded function's address takes
// check_lower_operand's ADDRESS task, as the expansion pushes it, and no continuation.
static inline __attribute__((always_inline)) bool check_lower_lean_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, bool address, TypeId target, bool last,
                                                                           uint32_t* child, uint8_t* flags)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    bool next = false;
    if (stream->overload_address_count != 0 && check_program_overload_address(program, node, operand) != NULL)
    {
        check_lower_operand(writer, node, operand, address, target);
    }
    else
    {
        const uint32_t first = operand == 0 ? graph->operand0[node] : graph->operand1[node];
        if (first < stream->count && check_stream_overload_head(stream, first) == 0)
        {
            const TypeId type = stream->value_types[first];
            // check_lower_expression's discard, which only a value converted to void can be: a statement's aggregate
            // assignment, a bit-field's store or update, a comma.
            bool discard = false;
            if (!address && target == BUSTER_TYPE_VOID)
            {
                const CheckOp op = graph->ops[first];
                const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
                const uint32_t destination = graph->operand0[first];
                const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) && destination < stream->count &&
                                       (stream->value_flags[destination] & BUSTER_CHECK_BIT_FIELD) != 0;
                discard = (store && check_lower_aggregate(writer, type)) || bit_field || op == CHECK_OP_COMMA;
            }
            if (!address && !discard && target != 0 && type != target)
            {
                check_lower_convert(writer, type, target);
            }
            if (!last)
            {
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_EXPRESSION, .node = first, .address = address, .discard = discard });
            }
            else if (writer->depth < writer->capacity)
            {
                *child = first;
                *flags = (uint8_t)((uint32_t)address | (uint32_t)discard << 2);
                next = true;
            }
            else
            {
                check_lower_fail(writer, CHECK_PROGRAM_RESOURCE);
            }
        }
        else
        {
            check_lower_fail(writer, CHECK_PROGRAM_INVALID);
        }
    }
    return next;
}

// A folded constant's entries (check_lower_constant's): an integer's of the classes I8 to U64 here, its bits sign- or
// zero-extended from its class's width; any other constant through check_lower_constant, with the node's whole value.
static inline __attribute__((always_inline)) void check_lower_lean_constant(CheckLowerWriter* writer, uint32_t node, const CheckValue* full, CheckConstantKind kind,
                                                                            uint32_t info)
{
    const uint8_t class = (uint8_t)info;
    const uint8_t facts = (uint8_t)(info >> 8);
    if (kind == CHECK_CONSTANT_INTEGER && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64 &&
        (facts & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) == 0)
    {
        uint64_t bits = full != NULL ? (uint64_t)full->integer : 0;
        const uint32_t width = 8u << ((class - 1) / 2);
        if (width < 64)
        {
            const uint64_t mask = (UINT64_C(1) << width) - 1;
            bits &= mask;
            bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
        }
        check_lower_emit(writer, BUSTER_TYPED_CONST, class, bits);
    }
    else
    {
        const CheckValue whole = check_stream_value(writer->program->stream, node);
        check_lower_constant(writer, &whole);
    }
}

// The lean step of EXPRESSION task `node` (`flags` its task's: address, initialized, discard), where its shape is one of
// the expansion's ordinary ones: a folded constant; a VALUE's read or address; a cast, a conversion, unary plus or a
// selection; `&x` and `*p`; a member access but a bit-field's; the negation, the complements and the truth tests; the
// comma; a scalar assignment; pointer arithmetic and subscripts; and the binary operators of classes below VECTOR. Each
// shape's guards are the expansion's tests before its case that its node could pass, so that a node it takes is one the
// expansion gives the same case. BUSTER_CHECK_LOWER_LEAN_NEXT leaves the operand to continue with in `child` and
// `child_flags`.
static inline __attribute__((always_inline)) uint32_t check_lower_lean_step(CheckLowerWriter* writer, uint32_t node, uint8_t flags, uint32_t* child, uint8_t* child_flags)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const bool address = (flags & 1) != 0;
    uint32_t result = BUSTER_CHECK_LOWER_LEAN_NONE;
    // The shapes, and among them the ones whose cases read the node's class and facts besides the generic cases' tests.
    const uint64_t bit = op < 64 ? BUSTER_CHECK_LOWER_LEAN_OP(op) : 0;
    const bool wrapper = (bit & BUSTER_CHECK_LOWER_LEAN_WRAPPERS) != 0;
    const bool typed = (bit & BUSTER_CHECK_LOWER_LEAN_TYPED) != 0;
    if (wrapper || typed)
    {
        // The node's value (check_stream_value's): its payload row where it has one, which holds it whole, else the columns.
        const uint32_t row = stream->payload_rows[node];
        const CheckValue* full = row != 0 && row <= stream->payload_count ? stream->payloads + row - 1 : NULL;
        const TypeId type = full != NULL ? full->type : stream->value_types[node];
        const CheckCategory category = full != NULL ? full->category : (CheckCategory)stream->value_categories[node];
        const CheckConstantKind constant_kind = full != NULL ? full->constant_kind : (CheckConstantKind)stream->value_constants[node];
        const bool lvalue = category == CHECK_LVALUE;
        const uint32_t info = typed || lvalue || constant_kind != CHECK_CONSTANT_NONE ? check_lower_lean_type(writer, type) : 0;
        const uint8_t facts = (uint8_t)(info >> 8);
        const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
        // The expansion's first cases: a compound literal's initialization (a VALUE's), an atomic lvalue's load, and a
        // folded constant.
        const uint32_t object = op != CHECK_OP_VALUE ? 0 : full != NULL ? full->object : stream->value_objects[node];
        const bool known = object != 0 && object < program->object_count;
        const bool compound = known && ((flags >> 1) & 1) == 0 && check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
                              !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC);
        const bool atomic = !address && lvalue && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0;
        bool folded = false;
        if (constant_kind != CHECK_CONSTANT_NONE)
        {
            const uint32_t value_flags = full != NULL ? full->flags : stream->value_flags[node];
            const bool clean = (full == NULL || full->fault == CHECK_FAULT_NONE) && (value_flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
            folded = clean && ((category == CHECK_RVALUE && !address) || ((address || address_value) && constant_kind == CHECK_CONSTANT_ADDRESS));
        }
        const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
        const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : (uint8_t)info;
        bool next = false;
        if (compound || atomic)
        {
        }
        else if (folded)
        {
            writer->token = graph->tokens[node];
            result = BUSTER_CHECK_LOWER_LEAN_DONE;
            // A discarded one (a comma's, whose operands have no effects) is nothing.
            if (((flags >> 2) & 1) == 0)
            {
                check_lower_lean_constant(writer, node, full, constant_kind, info);
            }
        }
        else if (op == CHECK_OP_VALUE)
        {
            // A VALUE of no class but void's is the expansion's to refuse.
            if (type == BUSTER_TYPE_VOID || address || address_value || (lvalue && known))
            {
                writer->token = graph->tokens[node];
                result = BUSTER_CHECK_LOWER_LEAN_DONE;
                if (type == BUSTER_TYPE_VOID)
                {
                    // The final value of a statement region without an expression statement is void.
                }
                else if (address || address_value)
                {
                    check_lower_address(writer, object, full != NULL ? full->offset : 0);
                }
                else
                {
                    const uint32_t local = check_lower_local_of(writer, object);
                    const SymbolId symbol = program->object_symbols[object];
                    if (check_lower_memory_class(class) && local == BUSTER_CHECK_NONE)
                    {
                        // A global of a memory class, through its address.
                        check_lower_address(writer, object, full != NULL ? full->offset : 0);
                        check_lower_emit(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, type));
                    }
                    else if (local != BUSTER_CHECK_NONE)
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
            }
        }
        else if (wrapper)
        {
            // A cast, a conversion, unary plus, a selection, `&x`, a condition's truth or a comma: no case before its own
            // takes it.
            writer->token = graph->tokens[node];
            result = BUSTER_CHECK_LOWER_LEAN_DONE;
            if (op == CHECK_OP_CONDITION)
            {
                const TypeId type_a = stream->conversions[node].operands[0].type;
                check_lower_truth(writer, type_a, false);
                next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
            }
            else if (op == CHECK_OP_COMMA)
            {
                (void)check_lower_lean_operand(writer, node, 1, address, ((flags >> 2) & 1) != 0 ? BUSTER_TYPE_VOID : type, false, child, child_flags);
                next = check_lower_lean_operand(writer, node, 0, false, BUSTER_TYPE_VOID, true, child, child_flags);
            }
            else
            {
                const bool operand_address = op == CHECK_OP_ADDRESS || (op == CHECK_OP_SELECT && address);
                const TypeId target = op == CHECK_OP_SELECT || op == CHECK_OP_ADDRESS ? 0 : type;
                next = check_lower_lean_operand(writer, node, 0, operand_address, target, true, child, child_flags);
            }
        }
        else if (op == CHECK_OP_DEREFERENCE)
        {
            const TypeId type_a = stream->conversions[node].operands[0].type;
            writer->token = graph->tokens[node];
            result = BUSTER_CHECK_LOWER_LEAN_DONE;
            if (!address && !address_value)
            {
                check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, type));
            }
            next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
        }
        else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
        {
            // A struct's or union's member at the offset the checker found it at (CheckStream.member_offsets); a bit-field's,
            // a vector's element and one the checker diagnosed are the expansion's.
            const TypeId type_a = stream->conversions[node].operands[0].type;
            const TypeId record = op == CHECK_OP_ARROW ? type_element(types, type_a) : check_stream_type(stream, graph->operand0[node]);
            const uint8_t record_kind = check_action_kind(types, record);
            const uint64_t offset = stream->member_offsets[node];
            const uint32_t value_flags = full != NULL ? full->flags : stream->value_flags[node];
            if (stream->diagnostics[node] == CHECK_DIAG_NONE && (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) &&
                (value_flags & BUSTER_CHECK_BIT_FIELD) == 0 && offset % 8 == 0)
            {
                writer->token = graph->tokens[node];
                result = BUSTER_CHECK_LOWER_LEAN_DONE;
                if (!address && !address_value)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, type));
                }
                if (offset / 8 != 0)
                {
                    check_lower_instruction(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                    check_lower_instruction(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
                }
                next = check_lower_lean_operand(writer, node, 0, op == CHECK_OP_MEMBER, type_a, true, child, child_flags);
            }
        }
        else if (op == CHECK_OP_ASSIGN)
        {
            // A local's or a global's store, or one through the destination's address; an atomic destination's, a
            // bit-field's and an aggregate's are the expansion's. A global of a memory class is stored through its address,
            // which goes first.
            const uint32_t destination = graph->operand0[node];
            const TypeId destination_type = check_stream_type(stream, destination);
            if ((check_lower_facts(writer, destination_type) & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 &&
                (check_stream_flags(stream, destination) & BUSTER_CHECK_BIT_FIELD) == 0 && !aggregate)
            {
                const uint32_t target = graph->ops[destination] == CHECK_OP_VALUE ? check_stream_object(stream, destination) : 0;
                const bool target_known = target != 0 && target < program->object_count;
                const uint32_t local = target_known ? check_lower_local_of(writer, target) : BUSTER_CHECK_NONE;
                const SymbolId symbol = target_known ? program->object_symbols[target] : 0;
                writer->token = graph->tokens[node];
                result = BUSTER_CHECK_LOWER_LEAN_DONE;
                if (local != BUSTER_CHECK_NONE || (!check_lower_memory_class(class) && symbol != 0))
                {
                    check_lower_instruction(writer, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                            local != BUSTER_CHECK_NONE ? local : symbol);
                    next = check_lower_lean_operand(writer, node, 1, false, type, true, child, child_flags);
                }
                else
                {
                    check_lower_instruction(writer, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, type));
                    (void)check_lower_lean_operand(writer, node, 1, false, type, false, child, child_flags);
                    next = check_lower_lean_operand(writer, node, 0, true, 0, true, child, child_flags);
                }
            }
        }
        else
        {
            // The unary and binary operators, pointer arithmetic and subscripts. The vector operators', the tiles' and the
            // aggregates' arithmetic (check_lower_complex_operation) and a complex value's or a __float128's truth are the
            // expansion's, and so is a vector's element.
            const TypeId type_a = stream->conversions[node].operands[0].type;
            const uint8_t facts_a = (uint8_t)(check_lower_lean_type(writer, type_a) >> 8);
            const bool aggregate_a = (facts_a & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
            const uint32_t value_flags = full != NULL ? full->flags : stream->value_flags[node];
            const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBSCRIPT;
            const bool taken = op == CHECK_OP_SUBSCRIPT ? (value_flags & BUSTER_CHECK_VECTOR_ELEMENT) == 0 :
                               op == CHECK_OP_NOT       ? !aggregate && !aggregate_a :
                                                          class < BUSTER_TYPED_CLASS_VECTOR && !aggregate && !aggregate_a;
            if (taken)
            {
                const TypeId type_b = stream->conversions[node].operands[1].type;
                writer->token = graph->tokens[node];
                result = BUSTER_CHECK_LOWER_LEAN_DONE;
                const bool left_pointer = arithmetic && (facts_a & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                const bool right_pointer = arithmetic && (check_lower_facts(writer, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
                {
                    check_lower_instruction(writer, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
                    next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
                }
                else if (op == CHECK_OP_NOT)
                {
                    check_lower_truth(writer, type_a, true);
                    next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
                }
                else if (left_pointer || right_pointer)
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
                        (void)check_lower_lean_operand(writer, node, 1, false, type_b, false, child, child_flags);
                        next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
                    }
                    else
                    {
                        if (op == CHECK_OP_SUBSCRIPT && !address && !address_value)
                        {
                            check_lower_instruction(writer, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, type));
                        }
                        check_lower_scale(writer, pointer_type, size);
                        if (op == CHECK_OP_SUBTRACT)
                        {
                            check_lower_instruction(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
                        }
                        (void)check_lower_lean_operand(writer, node, left_pointer ? 1 : 0, false, BUSTER_TYPE_LONG, false, child, child_flags);
                        next = check_lower_lean_operand(writer, node, left_pointer ? 0 : 1, false, pointer_type, true, child, child_flags);
                    }
                }
                else if (arithmetic)
                {
                    check_lower_instruction(writer, check_lower_binary(op), class, 0);
                    (void)check_lower_lean_operand(writer, node, 1, false, type_b, false, child, child_flags);
                    next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
                }
                else
                {
                    const uint8_t kind = check_lower_binary(op);
                    check_lower_operation(writer, kind, class);
                    check_lower_count(writer, kind, type_b);
                    (void)check_lower_lean_operand(writer, node, 1, false, type_b, false, child, child_flags);
                    next = check_lower_lean_operand(writer, node, 0, false, type_a, true, child, child_flags);
                }
            }
        }
        result = next ? BUSTER_CHECK_LOWER_LEAN_NEXT : result;
    }
    return result;
}
