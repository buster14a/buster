// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked scalar definitions for the straight-line emitter (#1114). The allocation certificate bounds each value's
// uninterrupted register placement. A separate owner sweep checks every actual selected operand, so shortened or
// missing allocator intervals cannot invent value flow. This path regenerates each concrete kernel, resolves its
// immediate choices with the scalar oracle, and checks its actual bytes and clobbers. No spill, reload, rematerialization,
// call-save, fixed-register sequence or hidden transition may be present. Those need the full transition proof;
// their absence here yields no definition claims, preserving independently proved parameters and constants.

static Tier1EmitError tier1_debug_straight(const Tier1SelectPlan* plan, const Tier1EmitOptions* options,
                                         Arena* output, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    emitted->debug_definitions = NULL;
    const uint32_t nodes = function->instruction_count;
    const Tier1SelectCode* code = &emitted->code;
    const Tier1AllocationInput* input = &emitted->prepared.allocation;
    const Tier1Allocation* allocation = &emitted->allocation;
    bool supported = emitted->certified && !emitted->general && emitted->prepared.call_count == 0 && emitted->fixed_count == 0 &&
                     allocation->spill_count == 0 && emitted->transition_count == 0 && emitted->rematerializations == 0 &&
                     code->branch_count == 0 && emitted->reference_count == 0 && !emitted->frame.probe_stack &&
                     !emitted->frame.realigned && input->value_count == nodes && code->node_first != NULL && emitted->origins != NULL;
    // Undef is legal IR, but an unspecified register's bits are no exact source operand. Leave such functions to
    // the independent parameter/literal proofs instead of rejecting an otherwise valid compilation.
    for (uint32_t node = 0; supported && node < nodes; node += 1)
    {
        supported = plan->covered[node] || function->op[node] != IR_UNDEF;
    }
    bool valid = true;
    Tier1EmitError error = TIER1_EMIT_OK;
    Tier1DebugDefinition* definitions = supported ? ir_allocate(output, nodes, sizeof(Tier1DebugDefinition), &valid) : NULL;
    error = valid ? error : TIER1_EMIT_MEMORY;
    for (uint32_t node = 0; supported && valid && node < nodes; node += 1)
    {
        definitions[node] = (Tier1DebugDefinition){ .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE };
    }
    // Uniform direct registers are the allocation's checked view. Require one actual register fragment too, so
    // that no absent transition can hide behind a coincidentally equal first/last register.
    for (uint32_t interval = 0; supported && valid && interval < input->count; interval += 1)
    {
        const uint32_t first = allocation->fragment_offsets[interval];
        const uint32_t end = allocation->fragment_offsets[interval + 1];
        const uint32_t node = input->values[interval];
        valid = node < nodes && first < allocation->fragment_count && end == first + 1 && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                allocation->registers[first] < 16 && allocation->value_registers[node] == allocation->registers[first];
    }
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild frame = { .plan = plan, .emitted = emitted, .stats = &stats, .base = (uint8_t)emitted->frame.base_register };
    uint32_t body = 0;
    valid = valid && (!supported || (tier1_cert_emitted_prologue(&frame, &body) && body == emitted->body_record && code->node_first[0] == body));
    uint32_t owners[16];
    memset(owners, 0xff, sizeof(owners));
    for (uint32_t node = 0; supported && valid && node < nodes; node += 1)
    {
        const uint32_t first = code->node_first[node];
        const uint32_t end = code->node_first[node + 1];
        const uint32_t op = function->op[node];
        const uint32_t type = function->type[node];
        const bool defined = type != IR_VOID;
        const uint32_t destination = allocation->value_registers[node];
        const Tier1SelectPattern* pattern = &plan->patterns[node];
        const uint32_t use_first = plan->effective_use_offsets[node];
        const uint32_t use_end = plan->effective_use_offsets[node + 1];
        valid = first >= body && first <= end && end <= code->count;
        valid = valid && use_first <= use_end && use_end <= plan->effective_use_count;
        uint32_t uses = 0;
        for (uint32_t p = 0; valid && !plan->covered[node] && p < BUSTER_TIER1_SELECT_VALUES; p += 1)
        {
            const uint32_t operand = pattern->values[p];
            if (operand != BUSTER_IR_NONE)
            {
                valid = operand < node && uses < use_end - use_first && plan->effective_use_values[use_first + uses] == operand;
                const uint32_t reg = valid ? allocation->value_registers[operand] : 16;
                valid = valid && reg < 16 && owners[reg] == operand;
                uses += 1;
            }
        }
        valid = valid && uses == use_end - use_first;
        if (valid && (plan->covered[node] || op == IR_BR))
        {
            valid = first == end;
        }
        else if (valid && op == IR_RET)
        {
            // No definition follows the final return. Its actual copies and epilogue writes are still replayed by
            // the final-PC location consumer, so these claims cannot restore an overwritten register in the tail.
            valid = node + 1 == nodes && end == code->count;
        }
        else if (valid)
        {
            valid = !defined || (destination < 16 && destination != X86_REGISTER_RSP && destination != BUSTER_TIER1_SELECT_SCRATCH);
            if (valid && op == IR_PARAM)
            {
                const Tier1SelectPlace parameter = tier1_select_parameter_place(function, function->a[node]);
                valid = parameter.kind == BUSTER_TIER1_PLACE_GENERAL && parameter.index < BUSTER_ABI_INTEGER_ARGUMENTS &&
                        destination == tier1_select_parameter_registers[parameter.index];
            }
            if (valid)
            {
                const Tier1SelectHoles holes = tier1_select_holes(plan, allocation->value_registers, node);
                Tier1SelectRecords selected = tier1_select_expand(&holes);
                valid = !selected.overflowed && selected.count == end - first;
                if (valid && selected.requested)
                {
                    uint16_t form = X86_FORM_NONE;
                    x86_choose_forms_scalar(&selected.row, &selected.value, &selected.context, 1, &form);
                    valid = form != X86_FORM_NONE && selected.request < selected.count;
                    if (valid)
                    {
                        selected.records[selected.request] = x86_choice_instruction(form, (X86Instruction){ .rm = selected.destination }, selected.value);
                    }
                }
                valid = valid && memcmp(selected.records, code->records + first, (uint64_t)selected.count * sizeof(X86Instruction)) == 0;
                const uint64_t allowed = BUSTER_X86_RESOURCE_GPR(BUSTER_TIER1_SELECT_SCRATCH) | (defined ? BUSTER_X86_RESOURCE_GPR(destination) : 0);
                for (uint32_t r = first; valid && r < end; r += 1)
                {
                    uint64_t read = 0;
                    uint64_t written = 0;
                    x86_instruction_effects_scalar(code->records + r, &read, &written);
                    valid = (written & UINT64_C(0xffff) & ~allowed) == 0;
                    uint32_t killed = (uint32_t)written & UINT32_C(0xffff);
                    while (killed != 0)
                    {
                        owners[(uint32_t)__builtin_ctz(killed)] = BUSTER_IR_NONE;
                        killed &= killed - 1;
                    }
                }
                // A vanished copy or truncation still defines its new SSA identity in the operand's register.
                // The allocation certificate forbids two distinct live intervals sharing that register; an old
                // value used later is independently rejected by this sweep even if its supplied interval was short.
                valid = valid && (!defined || op == IR_PARAM || first < end ||
                                  (uses == 1 && destination == holes.left &&
                                   (pattern->rule == TIER1_RULE_COPY || (pattern->rule == TIER1_RULE_TRUNCATE && pattern->width == 4))));
                if (valid && defined)
                {
                    owners[destination] = node;
                }
                const uint32_t narrow = tier1_cert_emitted_narrow(type);
                const uint32_t bytes = narrow != 0 ? narrow : type == IR_I32 ? 4 : type == IR_I64 || type == IR_PTR ? 8 : 0;
                if (valid && defined && op != IR_PARAM && bytes != 0 && first < end)
                {
                    definitions[node] = (Tier1DebugDefinition){
                        .first = first,
                        .end = end,
                        .reg = (uint8_t)destination,
                        .class = BUSTER_TIER1_ALLOCATE_GPR,
                        .bytes = (uint8_t)bytes,
                    };
                }
            }
        }
    }
    if (supported && valid)
    {
        emitted->debug_definitions = definitions;
    }
    else if (supported && error == TIER1_EMIT_OK)
    {
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CERTIFICATE, BUSTER_TIER1_EMIT_CODE_CERTIFICATE | TIER1_CERT_EMITTED_KERNEL);
        error = TIER1_EMIT_CERTIFICATE;
    }
    return error;
}
