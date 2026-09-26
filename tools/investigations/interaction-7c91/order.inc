/* Disposable counterfactual. Ownership: interaction-7c91 only.
 * Dependency enumeration mirrors llvm_bc_type_dependencies_ready exactly.
 * Rank computation preserves the old sweep's successful-emission sequence.
 * Nothing in this file is installed in the production source tree. */
#if R7_TRACE || R7_ORDERED
BUSTER_GLOBAL_LOCAL u32 r7_dependency_count(LlvmBcContext* context, u32 index)
{
    IrType* type = context->program->types.types + index;
    u32 result = 0;
    if (type->is_atomic && type->unqualified_type.value < context->program->types.count &&
        type->unqualified_type.value != index)
    {
        result = 1;
    }
    else
    {
        switch (type->kind)
        {
        case IR_TYPE_ARRAY:
        case IR_TYPE_VECTOR:
            result = 1;
            break;
        case IR_TYPE_FUNCTION:
            result = type->parameter_count + 1;
            break;
        case IR_TYPE_STRUCT:
            result = type->field_count;
            break;
        case IR_TYPE_ENUM:
            result = type->unqualified_type.value != IR_ID_UNDERLYING_INVALID;
            break;
        default:
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 r7_dependency_at(LlvmBcContext* context, u32 index, u32 edge)
{
    IrType* type = context->program->types.types + index;
    u32 result = IR_ID_UNDERLYING_INVALID;
    if (type->is_atomic && type->unqualified_type.value < context->program->types.count &&
        type->unqualified_type.value != index)
    {
        result = type->unqualified_type.value;
    }
    else
    {
        switch (type->kind)
        {
        case IR_TYPE_ARRAY:
        case IR_TYPE_VECTOR:
            result = type->element_type.value;
            break;
        case IR_TYPE_FUNCTION:
            result = edge ? type->parameter_types[edge - 1].value : type->return_type.value;
            break;
        case IR_TYPE_STRUCT:
            result = type->fields[edge].type.value;
            break;
        case IR_TYPE_ENUM:
            result = type->unqualified_type.value;
            break;
        default:
            break;
        }
    }
    return result;
}
#endif

#if R7_TRACE
BUSTER_GLOBAL_LOCAL void r7_observe_graph(LlvmBcContext* context)
{
    /* This observer is distinct from algorithm work. It is disabled for clean
     * binaries and its edge visits are not added to dependency/plan counters. */
    for (u32 index = 0; index < context->program->types.count; index += 1)
    {
        u32 count = r7_dependency_count(context, index);
        if (context->r7.graph)
        {
            fprintf(stderr, "R7_NODE %u %u %u\n", (unsigned)index,
                    (unsigned)context->program->types.types[index].kind, (unsigned)count);
        }
        for (u32 edge = 0; edge < count; edge += 1)
        {
            u32 dependency = r7_dependency_at(context, index, edge);
            R7_ADD(context, observed_edges, 1);
            if (context->r7.graph)
            {
                fprintf(stderr, "R7_EDGE %u %u\n", (unsigned)index, (unsigned)dependency);
            }
        }
    }
}
#endif

BUSTER_GLOBAL_LOCAL bool r7_sweep_types(LlvmBcContext* context)
{
    u32 unresolved = context->program->types.count;
    u32 pass = 0;
    bool success = true;
    while (unresolved && success)
    {
        u32 progress = 0;
        pass += 1;
        R7_ADD(context, passes, 1);
        for (u32 index = 0; index < context->program->types.count && success; index += 1)
        {
            R7_ADD(context, slots, 1);
            if (context->ir_type_ids[index] == LLVM_BC_INVALID_ID)
            {
                R7_ADD(context, attempts, 1);
                if (llvm_bc_add_ir_type(context, index))
                {
                    unresolved -= 1;
                    progress += 1;
                    R7_ADD(context, resolved, 1);
                    R7_TYPE(context, index, pass);
                }
            }
            success = !llvm_bc_failed(context);
        }
        if (success && !progress)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE,
                         llvm_bc_s8("cyclic or unresolved LLVM type dependency"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            success = false;
        }
    }
    (void)pass;
    return success;
}

#if R7_ORDERED
typedef struct R7PlanResult R7PlanResult;
struct R7PlanResult
{
    bool handled;
    bool success;
};

BUSTER_GLOBAL_LOCAL R7PlanResult r7_ordered_types(LlvmBcContext* context)
{
    R7PlanResult result = {0};
    u32 count = context->program->types.count;
    TemporalArena temporary = scratch_begin(&context->arena, 1);
    u64 storage_count = count ? count : 1;
    u32* pending = arena_allocate(temporary.arena, u32, storage_count);
    u64* starts = arena_allocate(temporary.arena, u64, (u64)count + 1);
    u64* cursor = arena_allocate(temporary.arena, u64, storage_count);
    u32* ranks = arena_allocate(temporary.arena, u32, storage_count);
    u32* queue = arena_allocate(temporary.arena, u32, storage_count);
    u32* buckets = arena_allocate(temporary.arena, u32, (u64)count + 1);
    R7_ADD(context, setup_bytes, storage_count * 20 + ((u64)count + 1) * 12);
    memset(starts, 0, ((size_t)count + 1) * sizeof(*starts));
    memset(ranks, 0, (size_t)count * sizeof(*ranks));
    memset(buckets, 0, ((size_t)count + 1) * sizeof(*buckets));
    bool valid = count != (u32)-1;
    u64 edge_count = 0;
    for (u32 index = 0; index < count && valid; index += 1)
    {
        R7_ADD(context, plan_nodes, 1);
        IrType* type = context->program->types.types + index;
        valid = !(type->kind == IR_TYPE_FUNCTION && type->parameter_count == (u32)-1);
        pending[index] = valid ? r7_dependency_count(context, index) : 0;
        edge_count += pending[index];
        for (u32 edge = 0; edge < pending[index] && valid; edge += 1)
        {
            u32 dependency = r7_dependency_at(context, index, edge);
            R7_ADD(context, plan_edges, 1);
            valid = dependency < count;
            if (valid)
            {
                starts[dependency + 1] += 1;
            }
        }
    }
    if (valid && edge_count <= (u64)SIZE_MAX / sizeof(u32))
    {
        u32* successors = arena_allocate(temporary.arena, u32, edge_count ? edge_count : 1);
        R7_ADD(context, setup_bytes, (edge_count ? edge_count : 1) * sizeof(*successors));
        u32 head = 0;
        u32 tail = 0;
        for (u32 index = 0; index < count; index += 1)
        {
            R7_ADD(context, plan_nodes, 1);
            starts[index + 1] += starts[index];
            cursor[index] = starts[index];
            if (!pending[index])
            {
                queue[tail++] = index;
            }
        }
        for (u32 index = 0; index < count; index += 1)
        {
            R7_ADD(context, plan_nodes, 1);
            for (u32 edge = 0; edge < pending[index]; edge += 1)
            {
                u32 dependency = r7_dependency_at(context, index, edge);
                R7_ADD(context, plan_edges, 1);
                successors[cursor[dependency]++] = index;
            }
        }
        while (head < tail)
        {
            u32 dependency = queue[head++];
            R7_ADD(context, plan_nodes, 1);
            for (u64 edge = starts[dependency]; edge < starts[dependency + 1]; edge += 1)
            {
                u32 index = successors[edge];
                R7_ADD(context, delivered_edges, 1);
                u32 rank = ranks[dependency] + (dependency >= index ? 1u : 0u);
                ranks[index] = BUSTER_MAX(ranks[index], rank);
                pending[index] -= 1;
                if (!pending[index])
                {
                    queue[tail++] = index;
                }
            }
        }
        if (tail == count)
        {
            result.handled = true;
            result.success = true;
            for (u32 index = 0; index < count; index += 1)
            {
                R7_ADD(context, bucket_nodes, 1);
                buckets[ranks[index] + 1] += 1;
            }
            for (u32 rank = 0; rank < count; rank += 1)
            {
                R7_ADD(context, bucket_nodes, 1);
                buckets[rank + 1] += buckets[rank];
            }
            for (u32 index = 0; index < count; index += 1)
            {
                R7_ADD(context, bucket_nodes, 1);
                queue[buckets[ranks[index]]++] = index;
            }
            for (u32 position = 0; position < count && result.success; position += 1)
            {
                u32 index = queue[position];
                R7_ADD(context, slots, 1);
                R7_ADD(context, attempts, 1);
                bool added = llvm_bc_add_ir_type(context, index);
                if (added)
                {
                    R7_ADD(context, resolved, 1);
                    R7_TYPE(context, index, ranks[index] + 1);
                }
                result.success = added && !llvm_bc_failed(context);
                if (!added && !llvm_bc_failed(context))
                {
                    llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE,
                                 llvm_bc_s8("cyclic or unresolved LLVM type dependency"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
                }
            }
        }
    }
    scratch_end(temporary);
    return result;
}
#endif
