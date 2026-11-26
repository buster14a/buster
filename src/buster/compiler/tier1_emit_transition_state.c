// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The register/owner state of a fragment transition. Include before tier1_emit_phase; the exchange epoch walk uses
// exactly the same update. Records and spill-slot stamps do not depend on the update's internal order.
#ifndef BUSTER_TIER1_EMIT_TRANSITION_STATE_INCLUDED
#define BUSTER_TIER1_EMIT_TRANSITION_STATE_INCLUDED

static void tier1_emit_transition_state(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation,
                                        Tier1EmitTransitions* transitions, uint32_t fragment, uint32_t value)
{
    const uint32_t base = 32 * tier1_emit_class(prepared, value);
    const uint8_t previous = allocation->registers[fragment - 1];
    const uint8_t reg = allocation->registers[fragment];
    const bool held = allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER;
    if (allocation->kinds[fragment - 1] == BUSTER_TIER1_ALLOCATE_REGISTER && transitions->owners[base + previous] == value)
    {
        transitions->owners[base + previous] = BUSTER_IR_NONE;
    }
    if (held)
    {
        transitions->owners[base + reg] = value;
    }
    transitions->registers[value] = held ? (uint8_t)(base + reg) : UINT8_MAX;
}

#endif
