// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A shared frame row is a source object's address only during a proved lifetime. This bounded first case has one
// start/end pair in one IR block and a complete scalar store in that interval. Scheduling shrinks the boundaries:
// all earlier records must have executed before opening, and no later record may have executed before closing.
// A sweep of concrete frame writes then ends the location at any foreign overlapping overwrite. The allocator's
// lifetime hull is never interpreted as a PC range. Interblock or repeated lifetime descriptions remain unavailable.
// Work is bounded by locals, slots, records and sweep events; no object-by-instruction or object-by-block matrix.

typedef struct CheckTier1DebugObject CheckTier1DebugObject;
struct CheckTier1DebugObject
{
    uint32_t first;
    uint32_t end;
    uint32_t block;
    uint32_t initialized;
    uint8_t markers;
};

static uint32_t check_tier1_debug_object_bytes(const Tier0Function* function, uint32_t local)
{
    return function->local_sizes != NULL && function->local_sizes[local] != 0 ? function->local_sizes[local] : tier0_class_bytes(function->local_classes[local]);
}

// Which exact IR object a concrete source store writes, or NONE. A cached pointer or a nearby source token is not
// evidence of object identity. Such a write may change the object legitimately, but cannot establish a new home.
static uint32_t check_tier1_debug_object_store(const IrFunction* ir, const Tier1EmitResult* emitted, uint32_t record)
{
    const uint32_t origin = emitted->origins[record];
    const uint32_t node = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
    uint32_t result = BUSTER_IR_NONE;
    if ((origin >> BUSTER_TIER1_ORIGIN_SHIFT) == TIER1_ORIGIN_NODE && node < ir->instruction_count && ir->op[node] == IR_STORE)
    {
        const uint32_t address = ir->a[node];
        if (address < ir->instruction_count && ir->op[address] == IR_STACK_ADDR)
        {
            result = ir->a[address];
        }
    }
    return result;
}

static bool check_tier1_debug_object_clips(const Tier0Function* function, const IrFunction* ir, const Tier1EmitResult* emitted,
                                         const uint32_t* record_order, const uint32_t* uses, const uint32_t* local_slots,
                                         const Tier0Home* homes, bool vector, Arena* scratch, CheckTier1DebugSpan** result)
{
    const uint32_t locals = function->local_count;
    const uint32_t slots = ir->slot_count;
    const uint32_t count = emitted->code.count;
    const Tier1Frame* frame = &emitted->frame;
    uint32_t shared = 0;
    for (uint32_t local = 0; local < locals; local += 1)
    {
        const uint32_t slot = local_slots[local];
        const uint32_t row = slot < slots ? tier1_emit_cfg_object_row(emitted, slot) : UINT32_MAX;
        shared += row < frame->count && uses[frame->slot[row]] > 1;
    }
    bool valid = true;
    CheckTier1DebugSpan* clips = shared != 0 ? ir_allocate(scratch, locals, sizeof(CheckTier1DebugSpan), &valid) : NULL;
    uint32_t* physical = shared != 0 ? ir_allocate(scratch, locals, 4, &valid) : NULL;
    for (uint32_t local = 0; valid && shared != 0 && local < locals; local += 1)
    {
        const uint32_t slot = local_slots[local];
        const uint32_t row = slot < slots ? tier1_emit_cfg_object_row(emitted, slot) : UINT32_MAX;
        const bool reused = row < frame->count && uses[frame->slot[row]] > 1;
        clips[local] = (CheckTier1DebugSpan){ .end = reused ? 0 : count };
        physical[local] = reused ? frame->slot[row] : BUSTER_IR_NONE;
    }
    // Unsupported certificates do not certify these additional homes. The existing independent base/unique-slot
    // proof can still describe other objects in the function.
    const bool prove = valid && shared != 0 && emitted->code_checked && emitted->code_reason == TIER1_CERT_EMITTED_OK &&
                       emitted->code.node_first != NULL && emitted->block_records != NULL;
    CheckTier1DebugObject* objects = prove ? ir_allocate(scratch, slots, sizeof(CheckTier1DebugObject), &valid) : NULL;
    uint32_t* place = prove ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* after = prove ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    uint32_t* before = prove ? ir_allocate(scratch, (uint64_t)count + 1, 4, &valid) : NULL;
    for (uint32_t slot = 0; valid && prove && slot < slots; slot += 1)
    {
        objects[slot] = (CheckTier1DebugObject){ .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE, .block = BUSTER_IR_NONE };
    }
    for (uint32_t r = 0; valid && prove && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        valid = old < count;
        if (valid)
        {
            place[old] = r;
        }
    }
    // The scheduler cuts at actual control-flow targets, not every IR block boundary: records may cross an
    // unlabelled fallthrough boundary. Global prefix/suffix bounds conservatively shrink those lifetimes too.
    // No additional debug-only cut is needed, and a legal permutation cannot turn into a publication failure.
    if (valid && prove)
    {
        after[0] = 0;
        before[count] = count;
    }
    for (uint32_t r = 0; valid && prove && r < count; r += 1)
    {
        after[r + 1] = after[r] > place[r] + 1 ? after[r] : place[r] + 1;
    }
    for (uint32_t r = count; valid && prove && r > 0; r -= 1)
    {
        before[r - 1] = before[r] < place[r - 1] ? before[r] : place[r - 1];
    }
    for (uint32_t block = 0; valid && prove && block < emitted->cfg_input.block_count; block += 1)
    {
        const uint32_t first = emitted->block_records[block];
        const uint32_t end = emitted->block_records[block + 1];
        valid = first <= end && end <= count;
    }
    const uint32_t added = emitted->cfg_input.block_count - ir->block_count;
    for (uint32_t block = 0; valid && prove && block < ir->block_count; block += 1)
    {
        for (uint32_t node = ir->block_offsets[block]; node < ir->block_offsets[block + 1]; node += 1)
        {
            if (ir->op[node] == IR_LIFETIME)
            {
                const uint32_t slot = ir->a[node];
                const bool start = ir->b[node] == 0;
                CheckTier1DebugObject* object = objects + slot;
                if (start && object->markers == 0)
                {
                    object->first = node;
                    object->block = block + added;
                    object->markers = 1;
                }
                else if (!start && object->markers == 1 && object->block == block + added)
                {
                    object->end = node;
                    object->markers = 2;
                }
                else
                {
                    object->markers = 3;
                }
            }
        }
    }
    // Use the last complete direct store in the lifetime. This intentionally leaves earlier initialization and
    // update intervals unavailable rather than guessing which stores complete a source initializer.
    for (uint32_t r = 0; valid && prove && r < count; r += 1)
    {
        const uint32_t old = record_order != NULL ? record_order[r] : r;
        const uint32_t slot = check_tier1_debug_object_store(ir, emitted, old);
        const uint32_t local = slot < slots ? ir->debug->locals[slot] : BUSTER_IR_NONE;
        if (local < locals && physical[local] != BUSTER_IR_NONE && local_slots[local] == slot && objects[slot].markers == 2)
        {
            const uint32_t node = emitted->origins[old] & BUSTER_TIER1_ORIGIN_PAYLOAD;
            const X86Instruction record = emitted->code.records[old];
            const uint32_t bytes = check_tier1_debug_object_bytes(function, local);
            const X86FormDescription* description = x86_form_description(record.form & (BUSTER_X86_FORM_FIXUP - 1));
            uint64_t read = 0;
            uint64_t written = 0;
            x86_instruction_effects_scalar(&record, &read, &written);
            const bool complete = bytes != 0 && bytes <= 8 && description->operand_bytes == bytes &&
                                  record.mode == BUSTER_X86_MEMORY_BASE && record.rm == frame->base_register && record.index == 0 &&
                                  record.form < X86_FORM_COUNT &&
                                  record.decorations == 0 && record.displacement == homes[local].displacement &&
                                  (written & BUSTER_X86_RESOURCE_MEMORY) != 0;
            if (complete && node > objects[slot].first && node < objects[slot].end)
            {
                objects[slot].initialized = r + 1;
            }
        }
    }
    uint64_t* keys = prove ? ir_allocate(scratch, frame->slot_count, 8, &valid) : NULL;
    uint64_t* sorting = prove ? ir_allocate(scratch, frame->slot_count, 8, &valid) : NULL;
    uint32_t* owners = prove ? ir_allocate(scratch, frame->slot_count, 4, &valid) : NULL;
    uint64_t* events = prove ? ir_allocate(scratch, (uint64_t)shared * 2, 8, &valid) : NULL;
    uint64_t* event_sorting = prove ? ir_allocate(scratch, (uint64_t)shared * 2, 8, &valid) : NULL;
    uint32_t key_count = 0;
    uint32_t event_count = 0;
    valid = valid && (!prove || (locals <= UINT32_MAX / 2 && shared <= UINT32_MAX / 2));
    if (valid && prove)
    {
        memset(owners, 0xff, (uint64_t)frame->slot_count * 4);
    }
    for (uint32_t local = 0; valid && prove && local < locals; local += 1)
    {
        const uint32_t slot = local_slots[local];
        if (physical[local] != BUSTER_IR_NONE && slot < slots && objects[slot].markers == 2 && objects[slot].initialized != 0)
        {
            const CheckTier1DebugObject object = objects[slot];
            const uint32_t first = emitted->code.node_first[object.first];
            const uint32_t end = emitted->code.node_first[object.end];
            valid = first <= end && end <= count && first >= emitted->block_records[object.block] && end <= emitted->block_records[object.block + 1];
            if (valid)
            {
                const uint32_t begin = after[first] > object.initialized ? after[first] : object.initialized;
                const uint32_t stop = before[end];
                if (begin < stop)
                {
                    clips[local] = (CheckTier1DebugSpan){ .begin = begin, .end = stop };
                    events[event_count++] = (uint64_t)object.initialized << 32 | (uint64_t)local * 2;
                    events[event_count++] = (uint64_t)stop << 32 | ((uint64_t)local * 2 + 1);
                    if (owners[physical[local]] == BUSTER_IR_NONE)
                    {
                        keys[key_count++] = (uint64_t)((uint32_t)homes[local].displacement ^ UINT32_C(0x80000000)) << 32 | physical[local];
                        owners[physical[local]] = local;
                    }
                    else
                    {
                        valid = homes[owners[physical[local]]].displacement == homes[local].displacement;
                    }
                }
            }
        }
    }
    if (valid && prove && event_count != 0)
    {
        keys = c_bindings_sort(keys, sorting, key_count, vector, UINT32_MAX);
        events = c_bindings_sort(events, event_sorting, event_count, vector, UINT32_MAX);
        memset(owners, 0xff, (uint64_t)frame->slot_count * 4);
        uint32_t event = 0;
        for (uint32_t r = 0; valid && r < count; r += 1)
        {
            while (event < event_count && (uint32_t)(events[event] >> 32) == r)
            {
                const uint32_t payload = (uint32_t)events[event++];
                const uint32_t local = payload / 2;
                const uint32_t slot = physical[local];
                if ((payload & 1) != 0)
                {
                    owners[slot] = owners[slot] == local ? BUSTER_IR_NONE : owners[slot];
                }
                else
                {
                    // An end and a start at the same PC may sort in either order. An old owner whose end is here
                    // is already dead; two actually overlapping source lifetimes cannot certify one shared row.
                    valid = owners[slot] == BUSTER_IR_NONE || clips[owners[slot]].end <= r;
                    owners[slot] = local;
                }
            }
            const uint32_t old = record_order != NULL ? record_order[r] : r;
            const X86Instruction record = emitted->code.records[old];
            uint64_t read = 0;
            uint64_t written = 0;
            x86_instruction_effects_scalar(&record, &read, &written);
            const bool base_address = (record.mode & (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE)) == (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE) && record.rm == frame->base_register;
            const bool indexed_address = (record.mode & (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_INDEX)) == (BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_INDEX) && record.index == frame->base_register;
            const bool frame_write = (base_address || indexed_address) && (written & BUSTER_X86_RESOURCE_MEMORY) != 0;
            if (frame_write)
            {
                const X86FormDescription* description = x86_form_description(record.form & (BUSTER_X86_FORM_FIXUP - 1));
                const bool direct = record.mode == BUSTER_X86_MEMORY_BASE && record.index == 0 && record.form < X86_FORM_COUNT;
                const uint32_t bytes = direct ? description->operand_bytes : 0;
                const int64_t low = bytes != 0 ? record.displacement : INT32_MIN;
                const int64_t high = bytes != 0 ? low + bytes : (int64_t)INT32_MAX + 1;
                uint32_t first = 0;
                uint32_t end = key_count;
                while (first < end)
                {
                    const uint32_t middle = first + (end - first) / 2;
                    const int64_t displacement = (int32_t)((uint32_t)(keys[middle] >> 32) ^ UINT32_C(0x80000000));
                    const bool earlier = displacement < low - 7;
                    first = earlier ? middle + 1 : first;
                    end = earlier ? end : middle;
                }
                const uint32_t stored = direct ? check_tier1_debug_object_store(ir, emitted, old) : BUSTER_IR_NONE;
                for (uint32_t k = first; k < key_count; k += 1)
                {
                    const int64_t displacement = (int32_t)((uint32_t)(keys[k] >> 32) ^ UINT32_C(0x80000000));
                    if (displacement >= high)
                    {
                        break;
                    }
                    const uint32_t local = owners[(uint32_t)keys[k]];
                    if (local != BUSTER_IR_NONE && displacement + check_tier1_debug_object_bytes(function, local) > low && local_slots[local] != stored)
                    {
                        clips[local].end = clips[local].end < r + 1 ? clips[local].end : r + 1;
                        owners[(uint32_t)keys[k]] = BUSTER_IR_NONE;
                    }
                }
            }
        }
    }
    *result = valid ? clips : NULL;
    return valid;
}

// Count/fill only intersecting runs. Binary search skips source gaps before a clipped object's lifetime, so a tiny
// shared-row interval does not revisit every span in a large function.
static uint64_t check_tier1_debug_object_ranges(const Tier0Home* homes, const uint32_t* local_slots, uint32_t locals,
                                              const CheckTier1DebugSpan* spans, uint32_t span_count, const CheckTier1DebugSpan* clips,
                                              EmittedLocalRange* output)
{
    uint64_t count = 0;
    for (uint32_t local = 0; local < locals; local += 1)
    {
        const CheckTier1DebugSpan clip = clips != NULL ? clips[local] : (CheckTier1DebugSpan){ .end = UINT32_MAX };
        if (local_slots[local] != BUSTER_IR_NONE && clip.begin < clip.end)
        {
            uint32_t first = 0;
            uint32_t end = span_count;
            while (first < end)
            {
                const uint32_t middle = first + (end - first) / 2;
                const bool earlier = spans[middle].end <= clip.begin;
                first = earlier ? middle + 1 : first;
                end = earlier ? end : middle;
            }
            for (uint32_t k = first; k < span_count && spans[k].begin < clip.end; k += 1)
            {
                const uint32_t begin = spans[k].begin > clip.begin ? spans[k].begin : clip.begin;
                const uint32_t stop = spans[k].end < clip.end ? spans[k].end : clip.end;
                if (begin < stop)
                {
                    if (output != NULL)
                    {
                        output[count] = (EmittedLocalRange){ .home = homes[local], .local = local, .begin = begin, .end = stop };
                    }
                    count += 1;
                }
            }
        }
    }
    return count;
}
