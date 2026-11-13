// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1382 fixtures, included after ir_optimize_test.c's independent executor. Both candidate paths must
// produce the same verified IR, then preserve results, faults and the effect trace through ordinary optimization.

#include "ir_if_convert.c"

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

// Shape 0 is a diamond; 1/2 are triangles with the direct edge false/true. Every phi reverses its incoming list
// relative to its neighbor, so neither edge order nor a particular phi-list order can determine select polarity.
static bool ir_if_convert_test_fixture(Arena* arena, IrFunction* f, uint32_t type, uint32_t shape, uint32_t phis, uint32_t yes_rows, uint32_t no_rows)
{
    const uint32_t join = shape == 0 ? 3 : 2;
    const uint32_t arm_rows = yes_rows + (shape == 0 ? no_rows : 0);
    const IrSizes sizes = {
        .instruction_count = 5 + arm_rows + (shape == 0 ? 2 : 1) + phis + 1,
        .block_count = join + 1,
        .extra_count = phis * 4,
        .constant_count = 2,
        .parameter_count = 2,
        .call_count = 1,
        .call_type_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I1;
        f->parameter_types[1] = type;
        f->result_type = type;
        f->constants[0].low = type == IR_I1 ? 0 : 7;
        f->constants[1].low = 1;
        // A zero-argument call record is present for the negative call fixture; unused metadata is valid too.
        f->calls[0].result_type = type;
        f->call_types[0] = type;
        f->symbol_count = 2;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_PARAM, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_CBR, IR_VOID, 0, shape == 2 ? join : 1, shape == 0 ? 2 : shape == 2 ? 1 : join);
        uint32_t next = 5;
        uint32_t arm_value[2] = { 1, 2 };
        for (uint32_t arm = 0; arm < (shape == 0 ? 2u : 1u); arm += 1)
        {
            f->block_offsets[arm + 1] = next;
            const uint32_t rows = arm == 0 ? yes_rows : no_rows;
            for (uint32_t row = 0; row < rows; row += 1)
            {
                const uint32_t op = type == IR_PTR || type == IR_F64 ? IR_COPY : arm == 0 ? IR_ADD : IR_SUB;
                ir_optimize_test_instruction(f, next, op, type, arm_value[arm], op == IR_COPY ? BUSTER_IR_NONE : 3, BUSTER_IR_NONE);
                arm_value[arm] = next++;
            }
            ir_optimize_test_instruction(f, next++, IR_BR, IR_VOID, join, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        f->block_offsets[join] = next;
        const uint32_t first_phi = next;
        for (uint32_t phi = 0; phi < phis; phi += 1)
        {
            ir_optimize_test_instruction(f, next++, IR_PHI, type, phi * 4, 2, BUSTER_IR_NONE);
            const uint32_t first = phi * 4 + (phi & 1) * 2;
            const uint32_t second = phi * 4 + ((phi & 1) ^ 1) * 2;
            f->extra[first] = 1;
            f->extra[first + 1] = phi & 1 ? 1 : arm_value[0];
            f->extra[second] = shape == 0 ? 2 : 0;
            f->extra[second + 1] = phi & 1 ? arm_value[shape == 0 ? 1 : 0] : shape == 0 ? arm_value[1] : 2;
            // A triangle's direct input must be available in the head, even for the alternating phi.
            if (shape != 0)
            {
                f->extra[second + 1] = phi & 1 ? 1 : 2;
            }
        }
        ir_optimize_test_instruction(f, next++, IR_RET, IR_VOID, first_phi + phis - 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        f->block_offsets[join + 1] = next;
    }
    return created;
}

static void ir_if_convert_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector, Arena* scalar, Arena* optimized, Arena* scratch,
                                       uint32_t regions, uint32_t selects)
{
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
    const uint64_t om = optimized->length;
    const uint64_t tm = scratch->length;
    const IrCheck valid = ir_verify_complete(f, scratch, false);
    ir_optimize_test_expect(test, valid.error == IR_OK, "if-conversion fixture is complete SSA");
    if (valid.error == IR_OK)
    {
        IrIfConvertResult v = { 0 };
        IrIfConvertResult s = { 0 };
        const IrCheck vc = ir_if_convert(f, vector, scratch, false, &v);
        const IrCheck sc = ir_if_convert(f, scalar, scratch, true, &s);
        ir_optimize_test_expect(test, vc.error == IR_OK && sc.error == IR_OK, "if-conversion accepts verified SSA on both paths");
        ir_optimize_test_expect(test, scratch->length == tm, "if-conversion restores scratch");
        if (vc.error == IR_OK && sc.error == IR_OK)
        {
            ir_optimize_test_expect(test, v.regions == regions && v.selects == selects && s.regions == regions && s.selects == selects, "if-conversion bounded admission and select count");
            ir_optimize_test_expect(test, ir_optimize_test_equal(&v.function, &s.function), "if-conversion scalar/vector exact columns and side tables");
            ir_optimize_test_expect(test, ir_verify_complete(&v.function, scratch, true).error == IR_OK && ir_verify_complete(&s.function, scratch, false).error == IR_OK,
                                    "if-conversion preserves complete SSA, including unreachable arm stubs");
            if (regions == 0)
            {
                ir_optimize_test_expect(test, v.function.op == f->op && s.function.extra == f->extra && vector->length == vm && scalar->length == sm,
                                        "refused regions borrow unchanged input without output allocation");
            }
            IrOptimizeResult o = { 0 };
            const IrCheck oc = ir_optimize(&v.function, optimized, scratch, ir_optimize_options(), &o);
            ir_optimize_test_expect(test, oc.error == IR_OK, "ordinary optimization consumes the if-conversion view");
            for (uint32_t trial = 0; trial < 48; trial += 1)
            {
                const uint64_t parameter = trial < 16 ? trial : trial == 16 ? UINT64_MAX : trial == 17 ? UINT64_C(1) << 63 : ir_optimize_test_random(test);
                const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, trial & 1);
                const IrOptimizeExecution after = ir_optimize_test_execute(&v.function, scratch, parameter, trial & 1);
                ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.effects == after.effects && (before.status != 1 || before.value == after.value),
                                        "if-conversion preserves independently executed values, traps and effects");
                if (oc.error == IR_OK)
                {
                    const IrOptimizeExecution final = ir_optimize_test_execute(&o.function, scratch, parameter, trial & 1);
                    ir_optimize_test_expect(test, before.status == final.status && before.effects == final.effects && (before.status != 1 || before.value == final.value),
                                            "if-conversion followed by cleanup preserves independent execution");
                }
            }
        }
    }
    arena_pop_to(vector, vm);
    arena_pop_to(scalar, sm);
    arena_pop_to(optimized, om);
}

static bool ir_if_convert_test_clamp(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 16,
        .block_count = 7,
        .extra_count = 8,
        .constant_count = 2,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I64;
        f->result_type = IR_I64;
        f->constants[0].low = 3;
        f->constants[1].low = 10;
        const uint32_t offsets[] = { 0, 5, 6, 7, 10, 11, 12, 16 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_ICMP, IR_I1, 0, 1, IR_LT);
        ir_optimize_test_instruction(f, 4, IR_CBR, IR_VOID, 3, 1, 2);
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_ICMP, IR_I1, 7, 2, IR_GT);
        ir_optimize_test_instruction(f, 9, IR_CBR, IR_VOID, 8, 4, 5);
        ir_optimize_test_instruction(f, 10, IR_BR, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_BR, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_ADD, IR_I64, 12, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_SUB, IR_I64, 13, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 1, 1, 2, 0, 5, 7, 4, 2 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static bool ir_if_convert_test_loop(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 17,
        .block_count = 6,
        .extra_count = 12,
        .constant_count = 3,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[1].low = 1;
        f->constants[2].low = 7;
        const uint32_t offsets[] = { 0, 4, 8, 11, 13, 16, 17 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ICMP, IR_I1, 4, 2, IR_ULT);
        ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 6, 2, 5);
        ir_optimize_test_instruction(f, 8, IR_AND, IR_I64, 4, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_ICMP, IR_I1, 8, 0, IR_NE);
        ir_optimize_test_instruction(f, 10, IR_CBR, IR_VOID, 9, 3, 4);
        ir_optimize_test_instruction(f, 11, IR_ADD, IR_I64, 5, 4, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_ADD, IR_I64, 4, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 16, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 0, 4, 14, 0, 0, 4, 13, 2, 5, 3, 11 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// The join precedes its arms in storage, so the phi's identity changes from 4 to 6. Its call argument shares a
// word with a switch case's constant index (both originally 4): remap the former, retain the latter and the input.
static bool ir_if_convert_test_shared_extra(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 12,
        .block_count = 5,
        .extra_count = 7,
        .constant_count = 5,
        .parameter_count = 1,
        .call_count = 1,
        .call_type_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I1;
        f->result_type = IR_I64;
        f->symbol_count = 2;
        f->constants[0].low = 7;
        f->constants[1].low = 1;
        f->constants[4].low = 8;
        f->call_types[0] = IR_I64;
        f->calls[0] = (IrCall){
            .argument_first = 5,
            .argument_count = 1,
            .parameter_count = 1,
            .result_type = IR_I64,
        };
        const uint32_t offsets[] = { 0, 4, 7, 9, 11, 12 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CBR, IR_VOID, 0, 2, 3);
        ir_optimize_test_instruction(f, 4, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_SWITCH, IR_VOID, 4, 4, 1);
        ir_optimize_test_instruction(f, 7, IR_ADD, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_SUB, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 2, 7, 3, 9, 4, 4, 4 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// The inner triangle's join has a third predecessor: its two entries fold into a select in its head, which the second
// round's diamond (the head, now ending in a branch to the join, and the third predecessor) takes in turn.
static bool ir_if_convert_test_three_predecessors(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 11,
        .block_count = 5,
        .extra_count = 6,
        .constant_count = 1,
        .parameter_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I1;
        f->parameter_types[1] = IR_I64;
        f->result_type = IR_I64;
        f->constants[0].low = 7;
        const uint32_t offsets[] = { 0, 4, 5, 7, 9, 11 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CBR, IR_VOID, 0, 1, 2);
        ir_optimize_test_instruction(f, 4, IR_CBR, IR_VOID, 0, 3, 4);
        ir_optimize_test_instruction(f, 5, IR_ADD, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_SUB, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_PHI, IR_I64, 0, 3, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_RET, IR_VOID, 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 1, 1, 2, 5, 3, 7 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// The inner diamond's join is an arm of an outer decision. Only the inner region qualifies in the first round;
// selecting it must neither speculate the outer decision nor mistake the original outer phi for an inner phi. The
// second round takes the outer decision, whose false arm is then the chain of the inner head and its join.
static bool ir_if_convert_test_nested(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 16,
        .block_count = 7,
        .extra_count = 8,
        .constant_count = 1,
        .parameter_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I1;
        f->parameter_types[1] = IR_I64;
        f->result_type = IR_I64;
        f->constants[0].low = 1;
        const uint32_t offsets[] = { 0, 5, 7, 8, 10, 12, 14, 16 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // The wide parameter's sign varies independently of the first parameter's low bit in the executor.
        ir_optimize_test_instruction(f, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_ICMP, IR_I1, 1, 2, IR_LT);
        ir_optimize_test_instruction(f, 4, IR_CBR, IR_VOID, 0, 1, 2);
        ir_optimize_test_instruction(f, 5, IR_ADD, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_BR, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 3, 3, 4);
        ir_optimize_test_instruction(f, 8, IR_ADD, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_BR, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_SUB, IR_I64, 1, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_BR, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_BR, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 3, 8, 4, 10, 1, 5, 5, 12 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// The conditional sum's shape: the head loads an element, and the arm recomputes its address and reloads it. Variant 0
// reuses the head's address, load and comparison; 1 and 5 (a store or a fence after the head's load) can't reuse the
// load but speculate it, the head's load of that address proving it; 2 (a call), 3 (a volatile arm load) and 4 (the
// head's load at another offset) leave the branch.
static bool ir_if_convert_test_reload(Arena* arena, IrFunction* f, uint32_t variant)
{
    const IrSizes sizes = {
        .instruction_count = 16,
        .block_count = 3,
        .extra_count = 4,
        .constant_count = 1,
        .address_count = 2,
        .parameter_count = 2,
        .call_count = 1,
        .call_type_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_PTR;
        f->parameter_types[1] = IR_I64;
        f->result_type = IR_I64;
        f->symbol_count = 2;
        f->constants[0].low = 3;
        f->calls[0].result_type = IR_I64;
        f->call_types[0] = IR_I64;
        f->addresses[0] = (IrAddress){
            .offset = variant == 4 ? 8 : 0,
            .scale = 1,
        };
        f->addresses[1] = (IrAddress){
            .offset = 0,
            .scale = 1,
        };
        const uint32_t offsets[] = { 0, 9, 14, 16 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
        ir_optimize_test_instruction(f, 5, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (variant == 1)
        {
            ir_optimize_test_instruction(f, 6, IR_STORE, IR_VOID, 4, 1, BUSTER_IR_NONE);
        }
        else if (variant == 2)
        {
            ir_optimize_test_instruction(f, 6, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        }
        else if (variant == 5)
        {
            ir_optimize_test_instruction(f, 6, IR_FENCE, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            ir_optimize_test_instruction(f, 6, IR_COPY, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, 7, IR_ICMP, IR_I1, 5, 2, IR_UGT);
        ir_optimize_test_instruction(f, 8, IR_CBR, IR_VOID, 7, 1, 2);
        ir_optimize_test_instruction(f, 9, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 1);
        ir_optimize_test_instruction(f, 10, IR_LOAD, IR_I64, 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        f->flags[10] = variant == 3 ? BUSTER_IR_VOLATILE : 0;
        ir_optimize_test_instruction(f, 11, IR_ADD, IR_I64, 10, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_ICMP, IR_I1, 10, 2, IR_UGT);
        ir_optimize_test_instruction(f, 13, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 1, 11 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// The C lowering's `&&` (variant 0) and `||` (1) value: the arm holds a comparison twice with its zero extensions and
// a dead shift, of which the phi needs two rows. The join tests the phi against zero, which becomes an i1 select of the
// two comparisons. Variant 2's dead division by a parameter could fault, so its arm stays.
static bool ir_if_convert_test_truth(Arena* arena, IrFunction* f, uint32_t variant)
{
    const IrSizes sizes = {
        .instruction_count = 20,
        .block_count = 3,
        .extra_count = 4,
        .constant_count = 3,
        .parameter_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I64;
        f->parameter_types[1] = IR_I64;
        f->result_type = IR_I64;
        f->constants[0].low = 7;
        f->constants[1].low = variant == 1 ? 1 : 0;
        const uint32_t offsets[] = { 0, 8, 15, 20 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_ICMP, IR_I1, 0, 2, IR_LT);
        ir_optimize_test_instruction(f, 5, IR_ZEXT, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ICMP, IR_I1, 0, 2, IR_LT);
        ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 4, variant == 1 ? 2 : 1, variant == 1 ? 1 : 2);
        ir_optimize_test_instruction(f, 8, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_ICMP, IR_I1, 1, 8, IR_LT);
        ir_optimize_test_instruction(f, 10, IR_ZEXT, IR_I64, 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_ICMP, IR_I1, 1, 8, IR_LT);
        ir_optimize_test_instruction(f, 12, IR_ZEXT, IR_I64, 11, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, variant == 2 ? IR_SDIV : IR_SHL, IR_I64, 0, variant == 2 ? 1 : 8, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 16, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 17, IR_ICMP, IR_I1, 15, 16, IR_NE);
        ir_optimize_test_instruction(f, 18, IR_ZEXT, IR_I64, 17, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 19, IR_RET, IR_VOID, 18, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 1, 12, 0, 3 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

// A running maximum over a parameter's bits: the head compares the loop's maximum, which the join's phi carries back,
// so variant 0 keeps its branch (a recurrence through the condition). Variant 1 compares with a constant instead and
// converts.
static bool ir_if_convert_test_maximum(Arena* arena, IrFunction* f, uint32_t variant)
{
    const IrSizes sizes = {
        .instruction_count = 17,
        .block_count = 6,
        .extra_count = 12,
        .constant_count = 3,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I64;
        f->result_type = IR_I64;
        f->constants[1].low = 1;
        f->constants[2].low = 16;
        const uint32_t offsets[] = { 0, 5, 9, 12, 13, 16, 17 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_ICMP, IR_I1, 5, 3, IR_ULT);
        ir_optimize_test_instruction(f, 8, IR_CBR, IR_VOID, 7, 2, 5);
        ir_optimize_test_instruction(f, 9, IR_XOR, IR_I64, 5, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_ICMP, IR_I1, 9, variant == 0 ? 6 : 3, IR_UGT);
        ir_optimize_test_instruction(f, 11, IR_CBR, IR_VOID, 10, 3, 4);
        ir_optimize_test_instruction(f, 12, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 14, IR_ADD, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 15, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 16, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 0, 1, 4, 14, 0, 1, 4, 13, 2, 6, 3, 9 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static void ir_if_convert_test_guards(IrOptimizeTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    void* mapping = page > 0 ? mmap(0, (uint64_t)page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = mapping != MAP_FAILED && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0;
    ir_optimize_test_expect(test, ready, "if-conversion guarded candidate column");
    for (uint32_t count = 0; ready && count <= BUSTER_IR_IF_CONVERT_BATCH; count += 1)
    {
        uint8_t* operations = (uint8_t*)mapping + page - count;
        uint64_t expected = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            operations[i] = i % 3 == 0 ? IR_CBR : IR_ADD;
            expected |= (uint64_t)(i % 3 == 0) << i;
        }
        ir_optimize_test_expect(test, ir_if_convert_candidates(operations, count, true) == expected && ir_if_convert_candidates(operations, count, false) == expected,
                                "if-conversion candidate mask respects every guarded tail");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page * 2);
    }
    ir_optimize_test_expect(test, ir_if_convert_candidates(0, 0, false) == 0 && ir_if_convert_candidates(0, 65, true) == 0,
                            "if-conversion empty and invalid mask counts do not access columns");
}

static void ir_if_convert_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(4ull << 20);
    Arena vector = arena_reserve(4ull << 20);
    Arena scalar = arena_reserve(4ull << 20);
    Arena optimized = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(8ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && optimized.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "if-conversion fixture arenas");
    ir_if_convert_test_guards(test);
    if (ready)
    {
        const uint8_t types[] = { IR_I1, IR_I8, IR_I16, IR_I32, IR_I64, IR_PTR, IR_F64 };
        for (uint32_t type = 0; type < sizeof(types); type += 1)
        {
            for (uint32_t shape = 0; shape < 3; shape += 1)
            {
                IrFunction f = { 0 };
                const bool created = ir_if_convert_test_fixture(&input, &f, types[type], shape, 2, 1, 1);
                ir_optimize_test_expect(test, created, "if-conversion scalar type and triangle fixture allocation");
                if (created)
                {
                    ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, types[type] == IR_F64 ? 0 : 1, types[type] == IR_F64 ? 0 : 2);
                }
                arena_pop_to(&input, 0);
            }
        }
        for (uint32_t kind = 0; kind < 19; kind += 1)
        {
            IrFunction f = { 0 };
            const uint32_t phis = kind == 14 ? 5 : 1;
            const uint32_t rows = kind == 12 ? 4 : kind == 13 ? 5 : kind == 9 || kind == 10 || kind == 17 ? 2 : 1;
            const bool created = ir_if_convert_test_fixture(&input, &f, IR_I64, 0, phis, rows, kind == 12 ? 4 : 1);
            ir_optimize_test_expect(test, created, "if-conversion speculation fixture allocation");
            if (created)
            {
                uint32_t expected = 0;
                if (kind < 4)
                {
                    const uint8_t operations[] = { IR_SDIV, IR_UDIV, IR_SREM, IR_UREM };
                    f.op[5] = operations[kind];
                    f.b[5] = 2;
                    f.constants[0].low = 0;
                }
                else if (kind < 7)
                {
                    f.op[5] = kind == 4 ? IR_SHL : kind == 5 ? IR_LSHR : IR_ASHR;
                    f.b[5] = kind == 4 ? 1 : 2;
                    f.constants[0].low = kind == 5 ? 64 : UINT64_MAX;
                }
                else if (kind == 7 || kind == 8)
                {
                    f.parameter_types[1] = IR_PTR;
                    f.type[1] = IR_PTR;
                    ir_optimize_test_instruction(&f, 5, IR_LOAD, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    f.flags[5] = kind == 8 ? BUSTER_IR_VOLATILE : 0;
                }
                else if (kind == 9 || kind == 10)
                {
                    ir_optimize_test_instruction(&f, 5, kind == 9 ? IR_FENCE : IR_CALL, kind == 9 ? IR_VOID : IR_I64, kind == 9 ? 0 : 1, kind == 9 ? BUSTER_IR_NONE : 0, BUSTER_IR_NONE);
                    f.a[6] = 2;
                }
                else if (kind == 11)
                {
                    f.op[5] = IR_UNDEF;
                    f.a[5] = BUSTER_IR_NONE;
                    f.b[5] = BUSTER_IR_NONE;
                }
                else if (kind == 15)
                {
                    bool ok = true;
                    f.labels = ir_allocate(&input, 1, 4, &ok);
                    f.label_count = ok ? 1 : 0;
                    if (ok)
                    {
                        f.labels[0] = 1;
                    }
                    ir_optimize_test_expect(test, ok, "if-conversion label fixture allocation");
                }
                else if (kind == 16)
                {
                    f.op[5] = IR_SHL;
                    f.b[5] = 3;
                    expected = 1;
                }
                else if (kind == 17 || kind == 18)
                {
                    f.parameter_types[1] = IR_PTR;
                    f.type[1] = IR_PTR;
                    if (kind == 17)
                    {
                        ir_optimize_test_instruction(&f, 5, IR_STORE, IR_VOID, 1, 2, BUSTER_IR_NONE);
                        f.a[6] = 2;
                    }
                    else
                    {
                        ir_optimize_test_instruction(&f, 5, IR_LOAD, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        f.flags[5] = BUSTER_IR_ATOMIC;
                        f.alignment_log2[5] = 3;
                    }
                }
                ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, expected, expected);
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t kind = 0; kind < 5; kind += 1)
        {
            IrFunction f = { 0 };
            const bool created = kind == 0 ? ir_if_convert_test_clamp(&input, &f) : kind == 1 ? ir_if_convert_test_loop(&input, &f) :
                                 kind == 2 ? ir_if_convert_test_shared_extra(&input, &f) : kind == 3 ? ir_if_convert_test_three_predecessors(&input, &f) : ir_if_convert_test_nested(&input, &f);
            ir_optimize_test_expect(test, created, "if-conversion CFG and side-table fixture allocation");
            if (created)
            {
                const uint32_t expected = kind == 0 || kind >= 3 ? 2 : 1;
                ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, expected, expected);
                if (kind == 1)
                {
                    // The loop's conditional sum, `s + (c ? i : 0)`: its phi adds a select of the index or zero.
                    IrIfConvertResult converted = { 0 };
                    const IrCheck check = ir_if_convert(&f, &vector, &scratch, false, &converted);
                    uint32_t sum = BUSTER_IR_NONE;
                    for (uint32_t i = 0; check.error == IR_OK && i < converted.function.instruction_count; i += 1)
                    {
                        const IrFunction* const g = &converted.function;
                        const uint32_t b = g->b[i];
                        const bool shaped = g->op[i] == IR_ADD && g->op[b] == IR_SELECT && (g->op[g->b[b]] == IR_CONST || g->op[g->c[b]] == IR_CONST);
                        sum = shaped ? i : sum;
                    }
                    ir_optimize_test_expect(test, check.error == IR_OK && converted.sunk == 1 && sum != BUSTER_IR_NONE,
                                            "if-conversion sinks an accumulation's select below its addition");
                    arena_pop_to(&vector, 0);
                }
                if (kind == 3 || kind == 4)
                {
                    IrIfConvertResult converted = { 0 };
                    const IrCheck check = ir_if_convert(&f, &vector, &scratch, false, &converted);
                    ir_optimize_test_expect(test, check.error == IR_OK && converted.rounds == 2 && converted.folded == (kind == 3 ? 1u : 0u),
                                            "if-conversion takes the outer region in a second round, folding a join with a third predecessor");
                    arena_pop_to(&vector, 0);
                }
                if (kind == 2)
                {
                    IrIfConvertResult converted = { 0 };
                    const IrCheck check = ir_if_convert(&f, &vector, &scratch, false, &converted);
                    ir_optimize_test_expect(test, check.error == IR_OK && converted.function.op[6] == IR_SELECT && converted.function.extra[5] == 4 &&
                                            converted.function.extra[converted.function.calls[0].argument_first] == 6 && f.extra[5] == 4 && f.calls[0].argument_first == 5,
                                            "if-conversion separates shared switch indices and remapped call values without mutating input");
                    arena_pop_to(&vector, 0);
                }
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t variant = 0; variant < 6; variant += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_if_convert_test_reload(&input, &f, variant);
            ir_optimize_test_expect(test, created, "if-conversion reload fixture allocation");
            if (created)
            {
                const uint32_t expected = variant == 0 || variant == 1 || variant == 5 ? 1 : 0;
                ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, expected, expected);
                IrIfConvertResult v = { 0 };
                IrIfConvertResult s = { 0 };
                const IrCheck vc = ir_if_convert(&f, &vector, &scratch, false, &v);
                const IrCheck sc = ir_if_convert(&f, &scalar, &scratch, true, &s);
                // Variant 0 reuses the address, the load and the dead comparison; 1 and 5 reuse the address alone.
                const uint32_t reused = variant == 0 ? 3 : expected != 0 ? 1 : 0;
                ir_optimize_test_expect(test, vc.error == IR_OK && sc.error == IR_OK && v.loads == expected && s.loads == expected && v.reused == reused && s.reused == reused,
                                        "if-conversion reuses or speculates an arm's reload only behind the head's proof");
                arena_pop_to(&vector, 0);
                arena_pop_to(&scalar, 0);
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_if_convert_test_maximum(&input, &f, variant);
            ir_optimize_test_expect(test, created, "if-conversion running maximum fixture allocation");
            if (created)
            {
                ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, variant, variant);
                IrIfConvertResult v = { 0 };
                IrIfConvertResult s = { 0 };
                const IrCheck vc = ir_if_convert(&f, &vector, &scratch, false, &v);
                const IrCheck sc = ir_if_convert(&f, &scalar, &scratch, true, &s);
                ir_optimize_test_expect(test, vc.error == IR_OK && sc.error == IR_OK && v.recurrences == 1 - variant && s.recurrences == 1 - variant,
                                        "if-conversion keeps the branch of a recurrence through its condition");
                arena_pop_to(&vector, 0);
                arena_pop_to(&scalar, 0);
            }
            arena_pop_to(&input, 0);
        }
        for (uint32_t variant = 0; variant < 3; variant += 1)
        {
            IrFunction f = { 0 };
            const bool created = ir_if_convert_test_truth(&input, &f, variant);
            ir_optimize_test_expect(test, created, "if-conversion truth fixture allocation");
            if (created)
            {
                const uint32_t expected = variant == 2 ? 0 : 1;
                ir_if_convert_test_compare(test, &f, &vector, &scalar, &optimized, &scratch, expected, expected);
                IrIfConvertResult v = { 0 };
                const IrCheck vc = ir_if_convert(&f, &vector, &scratch, false, &v);
                // The test of the phi is now an i1 select of the head's comparison and the arm's, in the head.
                uint32_t truth = BUSTER_IR_NONE;
                for (uint32_t i = 0; vc.error == IR_OK && i < v.function.instruction_count; i += 1)
                {
                    truth = v.function.op[i] == IR_SELECT && v.function.type[i] == IR_I1 ? i : truth;
                }
                const bool shaped = truth != BUSTER_IR_NONE && v.function.op[v.function.a[truth]] == IR_ICMP && v.function.op[v.function.b[truth]] == IR_ICMP &&
                                    v.function.op[v.function.c[truth]] == IR_ICMP && (variant == 0 ? v.function.c[truth] == v.function.a[truth] : v.function.b[truth] == v.function.a[truth]);
                ir_optimize_test_expect(test, vc.error == IR_OK && (variant == 2 ? truth == BUSTER_IR_NONE && v.reused == 0 : shaped && v.reused == 2),
                                        "if-conversion leaves dead rows, reuses repeated ones and tests the truth value as an i1 select");
                arena_pop_to(&vector, 0);
            }
            arena_pop_to(&input, 0);
        }
        IrFunction f = { 0 };
        const bool created = ir_if_convert_test_fixture(&input, &f, IR_I64, 0, 1, 1, 1);
        ir_optimize_test_expect(test, created, "if-conversion rollback fixture allocation");
        for (uint32_t kind = 0; created && kind < 2; kind += 1)
        {
            for (uint64_t available = 0; available < 2048; available += 31)
            {
                Arena small = kind == 0 ? vector : scratch;
                small.capacity = small.length + available;
                IrIfConvertResult result;
                memset(&result, 0xa5, sizeof(result));
                unsigned char before[sizeof(result)];
                memcpy(before, &result, sizeof(result));
                const uint64_t vm = vector.length;
                const uint64_t sm = scratch.length;
                const IrCheck check = ir_if_convert(&f, kind == 0 ? &small : &vector, kind == 0 ? &scratch : &small, false, &result);
                ir_optimize_test_expect(test, check.error == IR_OK || (check.error == IR_ERROR_MEMORY && memcmp(before, &result, sizeof(result)) == 0 &&
                                        (kind == 0 ? small.length == vm : vector.length == vm)), "if-conversion allocation failure preserves output and result");
                ir_optimize_test_expect(test, kind == 0 ? scratch.length == sm : small.length == sm, "if-conversion allocation failure restores scratch");
                arena_pop_to(&vector, vm);
            }
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&optimized);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

#endif
