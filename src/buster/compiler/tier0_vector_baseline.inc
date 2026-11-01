// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Non-EVEX value transport. Values keep the same 64-byte slots as the EVEX path. Only DUP copies a whole slot;
// accesses to user memory and objects touch exactly the value's bytes, including 4- and 8-byte tails. No general
// scratch register is needed. Selection reserves the first free floating register (tier0_vector_tops), so a live
// floating operand or a register home is never overwritten. The deep path supplies a window when all 16 are live.

static uint32_t tier0_vector_baseline_copy_bytes(const Tier0Function* function, uint32_t kind, uint64_t value)
{
    return kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_DUP ? 64 :
           kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL ? function->local_sizes[value] : (uint32_t)value;
}

static void tier0_emit_vector_baseline(const Tier0Function* function, const Tier0Entries* entries, const Tier0Frame* frame, const Tier0Home* homes,
                                      uint32_t i, Tier0Writer* writer)
{
    const uint32_t kind = entries->key_kinds[i];
    const uint32_t depth = entries->depths[i];
    const uint64_t value = function->values[i];
    const uint32_t bytes = tier0_vector_baseline_copy_bytes(function, kind, value);
    const uint32_t register_bytes = function->vector_bytes;
    const uint32_t part = bytes < register_bytes ? bytes : register_bytes;
    const uint8_t scratch = (uint8_t)entries->floating_depths[i];
    const uint32_t load = tier0_vector_target_load(function, part);
    const uint32_t store = tier0_vector_target_store(function, part);
    if (kind == BUSTER_TYPED_CONST)
    {
        // VEX.128 also clears the upper YMM half; legacy PXOR clears the complete available XMM register.
        tier0_put(writer, (X86Instruction){ .form = register_bytes == 32 ? X86_FORM_VXORPS_X_X_XM128 : X86_FORM_PXOR_X_XM128,
                                          .reg = scratch, .vvvv = register_bytes == 32 ? scratch : 0, .rm = scratch });
    }
    for (uint32_t offset = 0; offset < bytes; offset += part)
    {
        X86Instruction from = tier0_vector_slot(load, frame, depth == 0 ? 0 : depth - 1, scratch);
        X86Instruction to = tier0_vector_slot(store, frame, depth, scratch);
        if (kind == BUSTER_TYPED_LOAD)
        {
            from = tier0_instruction(load, scratch, tier0_integer_depths[depth - 1], BUSTER_X86_MEMORY_BASE, 0, 0);
            to = tier0_vector_slot(store, frame, depth - 1, scratch);
        }
        else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
        {
            to = tier0_instruction(store, scratch, tier0_integer_depths[depth - 2], BUSTER_X86_MEMORY_BASE, 0, 0);
        }
        else if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            const Tier0Home home = homes[value];
            if (kind == BUSTER_TYPED_LOCAL)
            {
                from = tier0_instruction(load, scratch, home.reg, home.mode, home.displacement, 0);
            }
            else
            {
                to = tier0_instruction(store, scratch, home.reg, home.mode, home.displacement, 0);
            }
        }
        from.displacement += (int32_t)offset;
        to.displacement += (int32_t)offset;
        if (kind != BUSTER_TYPED_CONST)
        {
            tier0_put(writer, from);
        }
        tier0_put(writer, to);
        if (kind == BUSTER_TYPED_STORE)
        {
            X86Instruction kept = tier0_vector_slot(store, frame, depth - 2, scratch);
            kept.displacement += (int32_t)offset;
            tier0_put(writer, kept);
        }
    }
}
