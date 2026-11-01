// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native direct-call fixtures, included after tier1_emit_test.c. The callers use original semantic IR;
// the hostile callees use original baseline records, never the selector or emitter under test. Every argument is
// recorded before all caller-saved GPRs and XMMs are overwritten. Callees also overwrite their red zone and record
// their entry alignment. The C oracle depends only on original inputs, not the selected or allocated function.
// Symbolic calls are checked against both encoder outputs and the independent E8/rel32 byte contract before loading
// local TEXT targets into the same mapping. These are ABI/consumer tests, not a C frontend or atomic-ordering proof.

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>

#define BUSTER_TIER1_CALL_HELPERS 4u
#define BUSTER_TIER1_CALL_RECORDS 160u
#define BUSTER_TIER1_CALL_CODE_BYTES (128u << 10)
#define BUSTER_TIER1_CALL_SAMPLES 24u
#define BUSTER_TIER1_CALL_RETURN_POISON UINT64_C(0xa5a55a5a00000000)

typedef enum Tier1CallHelperKind
{
    TIER1_CALL_MIX,
    TIER1_CALL_LOAD,
    TIER1_CALL_STORE,
    TIER1_CALL_FENCE,
    TIER1_CALL_SYNC,
    TIER1_CALL_POINTER,
} Tier1CallHelperKind;

typedef struct Tier1CallObserved Tier1CallObserved;
struct Tier1CallObserved
{
    uint64_t arguments[BUSTER_TIER1_CALL_HELPERS][6];
    uint64_t alignment[BUSTER_TIER1_CALL_HELPERS];
    uint64_t count[BUSTER_TIER1_CALL_HELPERS];
    uint64_t result[BUSTER_TIER1_CALL_HELPERS];
    // RAX at each callee's entry: a variadic call's AL counts the vector registers its arguments take.
    uint64_t al[BUSTER_TIER1_CALL_HELPERS];
    uint64_t trace;
};

typedef struct Tier1CallFixture Tier1CallFixture;
struct Tier1CallFixture
{
    IrFunction function;
    uint32_t width;
    uint32_t arguments;
    uint32_t permutation;
    uint32_t terms;
    uint32_t first_call;
    uint32_t helper_count;
    uint8_t kind[BUSTER_TIER1_CALL_HELPERS];
    bool memory;
    bool pointer;
    bool empty;
    bool void_return;
    bool mixed;
    bool narrow_return;
    // The calls variadic, each named on its first argument; a narrow return's type (I32, I8 or I16); a function of one
    // narrow parameter (I8 or I16, else none) that returns it zero-extended, without calls.
    bool variadic;
    IrType narrow_type;
    IrType parameter_type;
    // A call through its first parameter, the helper's address, of the next three, whose second it adds after the call.
    bool indirect;
    // A function that returns its parameter truncated to this narrow type (I8 or I16, else none), without calls.
    IrType return_type;
    // Divisions, remainders and shifts by a register of the four parameters, without calls, whose operands that live on
    // arrive in rdx and rcx, the registers they take (tier1_call_fixed_fixture).
    bool fixed;
};

static X86Instruction tier1_call_constant(uint32_t reg, uint64_t value)
{
    const X86Instruction record = {
        .form = X86_FORM_MOV_R64_IMM64,
        .rm = (uint8_t)reg,
        .immediate = (int32_t)value,
        .displacement = (int32_t)(value >> 32),
    };
    return record;
}

static uint32_t tier1_call_argument(uint32_t permutation, uint32_t argument)
{
    static const uint32_t duplicates[] = {
        2,
        2,
        0,
        2,
        0,
        1,
    };
    return permutation == 0 ? (argument + 1) % 6 : permutation == 1 ? argument ^ 1 : duplicates[argument];
}

static uint64_t tier1_call_term(uint32_t term)
{
    return UINT64_C(0x5317a4e9d26b0001) + UINT64_C(0x100010003) * term;
}

static uint64_t tier1_call_mix(const uint64_t* arguments, uint32_t count, uint32_t width)
{
    uint64_t result = UINT64_C(0x1367136713671367);
    for (uint32_t argument = 0; argument < count; argument += 1)
    {
        result = (result << 5) ^ (width == 4 ? (uint32_t)arguments[argument] : arguments[argument]);
    }
    return width == 4 ? (uint32_t)result : result;
}

static void tier1_call_descriptor(IrFunction* function, uint32_t index, uint32_t first, uint32_t count, IrType result)
{
    function->calls[index] = (IrCall){
        .argument_first = first,
        .argument_count = count,
        .type_first = first,
        .parameter_count = count,
        .result_type = (uint8_t)result,
    };
    for (uint32_t argument = 0; argument < count; argument += 1)
    {
        function->call_types[first + argument] = function->type[function->extra[first + argument]];
    }
}

static bool tier1_call_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrType type = fixture->width == 4 ? IR_I32 : IR_I64;
    const uint32_t count = 16 + fixture->terms * 3;
    const IrSizes sizes = {
        .instruction_count = count,
        .block_count = 1,
        .extra_count = fixture->arguments * 2,
        .constant_count = fixture->terms,
        .call_count = 2,
        .call_type_count = fixture->arguments * 2,
        .parameter_count = 6,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = (uint8_t)type;
        function->symbol_count = 3;
        function->block_offsets[1] = count;
        for (uint32_t argument = 0; argument < 6; argument += 1)
        {
            function->parameter_types[argument] = (uint8_t)type;
            tier1_emit_test_node(function, argument, IR_PARAM, type, argument, BUSTER_IR_NONE);
        }
        uint32_t node = 6;
        for (uint32_t term = 0; term < fixture->terms; term += 1)
        {
            function->constants[term].low = fixture->width == 4 ? (uint32_t)tier1_call_term(term) : tier1_call_term(term);
            tier1_emit_test_node(function, node, IR_CONST, type, term, BUSTER_IR_NONE);
            tier1_emit_test_node(function, node + 1, IR_XOR, type, 0, node);
            node += 2;
        }
        fixture->first_call = node;
        for (uint32_t argument = 0; argument < fixture->arguments; argument += 1)
        {
            function->extra[argument] = tier1_call_argument(fixture->permutation, argument);
            function->extra[fixture->arguments + argument] = argument == 0 ? node : argument;
        }
        tier1_call_descriptor(function, 0, 0, fixture->arguments, type);
        tier1_emit_test_node(function, node, IR_CALL, type, 1, 0);
        tier1_call_descriptor(function, 1, fixture->arguments, fixture->arguments, type);
        tier1_emit_test_node(function, node + 1, IR_CALL, type, 2, 1);
        for (uint32_t call = 0; fixture->variadic && call < 2; call += 1)
        {
            function->calls[call].variadic = true;
            function->calls[call].parameter_count = fixture->arguments != 0 ? 1 : 0;
        }
        tier1_emit_test_node(function, node + 2, IR_ADD, type, node, node + 1);
        uint32_t sum = node + 2;
        node += 3;
        for (uint32_t argument = 0; argument < 6; argument += 1)
        {
            tier1_emit_test_node(function, node, IR_ADD, type, sum, argument);
            sum = node++;
        }
        for (uint32_t term = 0; term < fixture->terms; term += 1)
        {
            tier1_emit_test_node(function, node, IR_ADD, type, sum, 7 + term * 2);
            sum = node++;
        }
        tier1_emit_test_node(function, node, IR_RET, IR_VOID, sum, BUSTER_IR_NONE);
        fixture->helper_count = 2;
        ready = node + 1 == count;
    }
    return ready;
}

static bool tier1_call_memory_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 15,
        .block_count = 1,
        .extra_count = 6,
        .constant_count = 1,
        .call_count = 4,
        .call_type_count = 6,
        .parameter_count = 3,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->symbol_count = 5;
        function->block_offsets[1] = 15;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = IR_I64;
        function->parameter_types[2] = IR_I32;
        for (uint32_t argument = 0; argument < 3; argument += 1)
        {
            tier1_emit_test_node(function, argument, IR_PARAM, (IrType)function->parameter_types[argument], argument, BUSTER_IR_NONE);
        }
        tier1_emit_test_node(function, 3, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 4, IR_STORE, IR_VOID, 0, 1);
        const uint32_t arguments[] = {
            0,
            2,
            0,
            7,
            2,
            2,
        };
        memcpy(function->extra, arguments, sizeof(arguments));
        tier1_call_descriptor(function, 0, 0, 2, IR_I64);
        tier1_emit_test_node(function, 5, IR_CALL, IR_I64, 1, 0);
        function->constants[0].low = tier1_call_term(3);
        tier1_emit_test_node(function, 6, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 7, IR_XOR, IR_I64, 1, 6);
        tier1_call_descriptor(function, 1, 2, 3, IR_VOID);
        tier1_emit_test_node(function, 8, IR_CALL, IR_VOID, 2, 1);
        tier1_call_descriptor(function, 2, 5, 1, IR_VOID);
        tier1_emit_test_node(function, 9, IR_CALL, IR_VOID, 3, 2);
        tier1_call_descriptor(function, 3, 6, 0, IR_VOID);
        tier1_emit_test_node(function, 10, IR_CALL, IR_VOID, 4, 3);
        tier1_emit_test_node(function, 11, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 12, IR_ADD, IR_I64, 3, 5);
        tier1_emit_test_node(function, 13, IR_ADD, IR_I64, 12, 11);
        tier1_emit_test_node(function, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE);
        fixture->width = 8;
        fixture->memory = true;
        fixture->first_call = 5;
        fixture->helper_count = 4;
        fixture->kind[0] = TIER1_CALL_LOAD;
        fixture->kind[1] = TIER1_CALL_STORE;
        fixture->kind[2] = TIER1_CALL_FENCE;
        fixture->kind[3] = TIER1_CALL_SYNC;
    }
    return ready;
}

static bool tier1_call_small_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture, bool pointer, bool void_return)
{
    const IrSizes sizes = {
        .instruction_count = void_return ? 2 : 3,
        .block_count = 1,
        .extra_count = pointer,
        .constant_count = !pointer && !void_return,
        .call_count = 1,
        .call_type_count = pointer,
        .parameter_count = pointer,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->symbol_count = 2;
        function->block_offsets[1] = (uint32_t)sizes.instruction_count;
        function->result_type = void_return ? IR_VOID : pointer ? IR_PTR : IR_I64;
        if (pointer)
        {
            function->parameter_types[0] = IR_PTR;
            tier1_emit_test_node(function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE);
            function->extra[0] = 0;
            tier1_call_descriptor(function, 0, 0, 1, IR_PTR);
            tier1_emit_test_node(function, 1, IR_CALL, IR_PTR, 1, 0);
            tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
        }
        else
        {
            tier1_call_descriptor(function, 0, 0, 0, IR_VOID);
            tier1_emit_test_node(function, 0, IR_CALL, IR_VOID, 1, 0);
            if (void_return)
            {
                tier1_emit_test_node(function, 1, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else
            {
                function->constants[0].low = tier1_call_term(0);
                tier1_emit_test_node(function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
                tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
            }
        }
        fixture->width = 8;
        fixture->pointer = pointer;
        fixture->empty = !pointer;
        fixture->void_return = void_return;
        fixture->first_call = pointer;
        fixture->helper_count = 1;
        fixture->kind[0] = pointer ? TIER1_CALL_POINTER : TIER1_CALL_SYNC;
    }
    return ready;
}

// The first destination is EDI, while its old full-width RDI value must survive through the cycle's temporary.
// Both full-width inputs and the narrow input also remain live after the call, exercising saves independently.
static bool tier1_call_mixed_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture, bool swap)
{
    const IrSizes sizes = {
        .instruction_count = 10,
        .block_count = 1,
        .extra_count = 3,
        .call_count = 1,
        .call_type_count = 3,
        .parameter_count = 3,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->symbol_count = 2;
        function->block_offsets[1] = 10;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = swap ? IR_I32 : IR_PTR;
        function->parameter_types[2] = swap ? IR_PTR : IR_I32;
        for (uint32_t argument = 0; argument < 3; argument += 1)
        {
            tier1_emit_test_node(function, argument, IR_PARAM, (IrType)function->parameter_types[argument], argument, BUSTER_IR_NONE);
        }
        function->extra[0] = swap ? 1 : 2;
        function->extra[1] = 0;
        function->extra[2] = swap ? 2 : 1;
        tier1_call_descriptor(function, 0, 0, 3, IR_I64);
        tier1_emit_test_node(function, 3, IR_CALL, IR_I64, 1, 0);
        tier1_emit_test_node(function, 4, IR_PTRTOINT, IR_I64, swap ? 2 : 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 5, IR_ZEXT, IR_I64, swap ? 1 : 2, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 6, IR_ADD, IR_I64, 3, 0);
        tier1_emit_test_node(function, 7, IR_XOR, IR_I64, 6, 4);
        tier1_emit_test_node(function, 8, IR_ADD, IR_I64, 7, 5);
        tier1_emit_test_node(function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE);
        fixture->width = 8;
        fixture->arguments = 3;
        fixture->permutation = swap;
        fixture->first_call = 3;
        fixture->helper_count = 1;
        fixture->mixed = true;
    }
    return ready;
}

// One allocatable register makes the I32 call result occupy RAX itself. The node-boundary assertion below checks
// the call's own normalization before ZEXT's separate 32-bit move can conceal a missing result capture.
static bool tier1_call_narrow_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .call_count = 1,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        const IrType narrow = fixture->narrow_type != 0 ? fixture->narrow_type : IR_I32;
        function->result_type = IR_I64;
        function->symbol_count = 2;
        function->block_offsets[1] = 3;
        tier1_call_descriptor(function, 0, 0, 0, narrow);
        tier1_emit_test_node(function, 0, IR_CALL, narrow, 1, 0);
        tier1_emit_test_node(function, 1, IR_ZEXT, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
        fixture->width = 4;
        fixture->helper_count = 1;
        fixture->narrow_return = true;
    }
    return ready;
}

// A function of one narrow parameter that returns it zero-extended: its caller passes the parameter's register with
// dirty bits past its width, which the function's canonical storage must clear (the CFG consumer's entry).
static bool tier1_call_parameter_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .parameter_count = 1,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->symbol_count = 1;
        function->block_offsets[1] = 3;
        function->parameter_types[0] = (uint8_t)fixture->parameter_type;
        tier1_emit_test_node(function, 0, IR_PARAM, fixture->parameter_type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_ZEXT, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
        fixture->width = 8;
        fixture->helper_count = 0;
    }
    return ready;
}

// p0 / p1 + p2 % p1 (unsigned) + (p0 << p3) + (p2 >> p3) + (p0 >>> p1) + p0, at the fixture's width: p2 arrives in rdx and
// p3 in rcx, and both live past the division and the first shift that take those registers, so they are saved around
// them; p1 is odd, at most 63, and the shift counts are in range (tier1_call_execute).
static bool tier1_call_fixed_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 15,
        .block_count = 1,
        .parameter_count = 4,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        const IrType type = fixture->width == 4 ? IR_I32 : IR_I64;
        function->result_type = (uint8_t)type;
        function->symbol_count = 1;
        function->block_offsets[1] = 15;
        for (uint32_t parameter = 0; parameter < 4; parameter += 1)
        {
            function->parameter_types[parameter] = (uint8_t)type;
            tier1_emit_test_node(function, parameter, IR_PARAM, type, parameter, BUSTER_IR_NONE);
        }
        tier1_emit_test_node(function, 4, IR_SDIV, type, 0, 1);
        tier1_emit_test_node(function, 5, IR_UREM, type, 2, 1);
        tier1_emit_test_node(function, 6, IR_SHL, type, 0, 3);
        tier1_emit_test_node(function, 7, IR_ASHR, type, 2, 3);
        tier1_emit_test_node(function, 8, IR_LSHR, type, 0, 1);
        tier1_emit_test_node(function, 9, IR_ADD, type, 4, 5);
        tier1_emit_test_node(function, 10, IR_ADD, type, 9, 6);
        tier1_emit_test_node(function, 11, IR_ADD, type, 10, 7);
        tier1_emit_test_node(function, 12, IR_ADD, type, 11, 8);
        tier1_emit_test_node(function, 13, IR_ADD, type, 12, 0);
        tier1_emit_test_node(function, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE);
        fixture->helper_count = 0;
        fixture->fixed = true;
    }
    return ready;
}

// A function that returns its parameter truncated to a narrow type: its result's canonical zero-extended 32 bits, which
// the caller reads as RAX (a 32-bit move's clears the rest).
static bool tier1_call_return_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .parameter_count = 1,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = (uint8_t)fixture->return_type;
        function->symbol_count = 1;
        function->block_offsets[1] = 3;
        function->parameter_types[0] = IR_I64;
        tier1_emit_test_node(function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_TRUNC, fixture->return_type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
        fixture->width = 8;
        fixture->helper_count = 0;
    }
    return ready;
}

// A call through a pointer: the first parameter is the helper's address, the next three its arguments, and the first of
// them lives past the call, added to its result. The CFG consumer moves the callee into R10 with the arguments.
static bool tier1_call_indirect_fixture(Tier1EmitTest* test, Tier1CallFixture* fixture)
{
    const IrSizes sizes = {
        .instruction_count = 7,
        .block_count = 1,
        .extra_count = 3,
        .call_count = 1,
        .call_type_count = 3,
        .parameter_count = 4,
    };
    IrFunction* function = &fixture->function;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->symbol_count = 1;
        function->block_offsets[1] = 7;
        function->parameter_types[0] = IR_PTR;
        tier1_emit_test_node(function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE);
        for (uint32_t argument = 1; argument < 4; argument += 1)
        {
            function->parameter_types[argument] = IR_I64;
            tier1_emit_test_node(function, argument, IR_PARAM, IR_I64, argument, BUSTER_IR_NONE);
            function->extra[argument - 1] = argument;
        }
        tier1_call_descriptor(function, 0, 0, 3, IR_I64);
        tier1_emit_test_node(function, 4, IR_CALL_INDIRECT, IR_I64, 0, 0);
        tier1_emit_test_node(function, 5, IR_ADD, IR_I64, 4, 1);
        tier1_emit_test_node(function, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE);
        fixture->width = 8;
        fixture->arguments = 3;
        fixture->helper_count = 1;
        fixture->indirect = true;
    }
    return ready;
}

// R10/R11 are scratch only after all six incoming arguments have been recorded. The helper is a SysV leaf:
// its own red zone is writable, but [RSP] is the return address and is never touched.
static uint32_t tier1_call_helper(const Tier1CallFixture* fixture, uint32_t helper, Tier1CallObserved* observed, X86Instruction* records)
{
    Tier1EmitBuffer buffer = {
        .records = records,
        .capacity = BUSTER_TIER1_CALL_RECORDS,
    };
    const uint32_t arguments = (uint32_t)offsetof(Tier1CallObserved, arguments) + helper * 6 * 8;
    const uint32_t result = (uint32_t)offsetof(Tier1CallObserved, result) + helper * 8;
    const uint32_t trace = (uint32_t)offsetof(Tier1CallObserved, trace);
    tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_R11, (uintptr_t)observed));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R11, (int32_t)(offsetof(Tier1CallObserved, al) + helper * 8)));
    for (uint32_t argument = 0; argument < 6; argument += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, abi_integer_arguments[argument], X86_REGISTER_R11, (int32_t)(arguments + argument * 8)));
    }
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_RSP));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_R10, 15));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R11, (int32_t)(offsetof(Tier1CallObserved, alignment) + helper * 8)));
    X86Instruction increment = tier1_frame_emit_memory(X86_FORM_ADD_RM64_IMM8, 0, X86_REGISTER_R11, (int32_t)(offsetof(Tier1CallObserved, count) + helper * 8));
    increment.immediate = 1;
    tier1_frame_emit_add(&buffer, increment);
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R11, (int32_t)trace));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_SHL_RM64_IMM8, X86_REGISTER_R10, 4));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_R10, (int32_t)helper + 1));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R11, (int32_t)trace));
    const uint32_t kind = fixture->kind[helper];
    if (kind == TIER1_CALL_MIX)
    {
        tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_RAX, UINT64_C(0x1367136713671367)));
        if (fixture->width == 4)
        {
            tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R32_RM32, X86_REGISTER_RAX, X86_REGISTER_RAX));
        }
        for (uint32_t argument = 0; argument < fixture->arguments; argument += 1)
        {
            tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(fixture->width == 4 ? X86_FORM_SHL_RM32_IMM8 : X86_FORM_SHL_RM64_IMM8, X86_REGISTER_RAX, 5));
            if (fixture->mixed && argument == 0)
            {
                // A callee may rely only on the low 32 bits of its I32 parameter, regardless of the caller's moves.
                tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_R32_RM32, X86_REGISTER_R10, X86_REGISTER_R11, (int32_t)arguments));
                tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_XOR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10));
            }
            else
            {
                tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(fixture->width == 4 ? X86_FORM_XOR_R32_RM32 : X86_FORM_XOR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R11, (int32_t)(arguments + argument * 8)));
            }
        }
        if (fixture->width == 4)
        {
            // SysV leaves an I32 result's upper half unspecified. Keep the low result but deliberately poison RAX.
            tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_R10, BUSTER_TIER1_CALL_RETURN_POISON));
            tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_OR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10));
        }
    }
    else if (kind == TIER1_CALL_LOAD)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI, 0));
    }
    else if (kind == TIER1_CALL_POINTER)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI));
    }
    else
    {
        if (kind == TIER1_CALL_STORE)
        {
            tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSI, X86_REGISTER_RDI, 0));
        }
        tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_RAX, UINT64_C(0xfeed0000dead0000) + helper));
    }
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R11, (int32_t)result));
    tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_R10, UINT64_C(0xbad0bad1bad2bad3)));
    for (uint32_t word = 1; word <= 16; word += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_RSP, -(int32_t)(word * 8)));
    }
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOVUPS_X_XM, reg, X86_REGISTER_R11, (int32_t)arguments));
    }
    const uint32_t clobbered[] = {
        X86_REGISTER_RAX,
        X86_REGISTER_RCX,
        X86_REGISTER_RDX,
        X86_REGISTER_RSI,
        X86_REGISTER_RDI,
        X86_REGISTER_R8,
        X86_REGISTER_R9,
        X86_REGISTER_R10,
    };
    for (uint32_t index = 0; index < sizeof(clobbered) / sizeof(clobbered[0]); index += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_call_constant(clobbered[index], UINT64_C(0xc1c2c3c4d1d20000) + clobbered[index]));
    }
    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R11, (int32_t)result));
    tier1_frame_emit_add(&buffer, tier1_call_constant(X86_REGISTER_R11, UINT64_C(0x1122334455667788)));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_RAX, 23));
    tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
    return buffer.count;
}

// A local TEXT-only loader. Encoder offsets independently identify the five-byte E8 instruction and its rel32;
// references must occur in marked-record order and preserve the original source's symbol identity. Each helper's address
// goes in `addresses`, for an indirect call's caller to pass.
static uint8_t* tier1_call_load(Tier1EmitTest* test, const Tier1CallFixture* fixture, Tier1EmitResult* emitted, Tier1CallObserved* observed, uint8_t* mapping, uint64_t* addresses)
{
    Tier1SelectCode* code = &emitted->code;
    bool native = tier1_emit_test_native(test, code->records, code->count);
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    uint32_t* offsets = calloc((uint64_t)code->count + 1, sizeof(uint32_t));
    uint32_t* ends = calloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count), sizeof(uint32_t));
    uint32_t* twin_ends = calloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count), sizeof(uint32_t));
    uint8_t* twin = malloc(capacity);
    // Narrow fixtures put helper TEXT before the caller; wide fixtures put it after. Both rel32 signs execute.
    const uint32_t entry_offset = fixture->width == 4 ? 16u << 10 : 64;
    uint8_t* entry = mapping + entry_offset;
    bool ready = offsets && ends && twin_ends && twin && capacity + entry_offset <= BUSTER_TIER1_CALL_CODE_BYTES;
    tier1_emit_test_expect(test, ready, "direct-call loader storage");
    int64_t length = -1;
    if (ready)
    {
        const BranchSizingCheck check = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ready = check.error == BRANCH_SIZING_OK && size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK;
        ready = ready && x86_target_validate(test->allowed, code->records, code->count) == code->count;
        tier1_emit_test_expect(test, ready, "direct-call records and baseline target validate");
    }
    if (ready)
    {
        length = x86_encode(code->records, code->count, entry, capacity, 0, ends);
        const int64_t twin_length = x86_encode_scalar(code->records, code->count, twin, capacity, offsets, twin_ends);
        ready = length > 0 && length == twin_length && memcmp(entry, twin, (size_t)length) == 0 && memcmp(ends, twin_ends, emitted->reference_count * sizeof(uint32_t)) == 0;
        tier1_emit_test_expect(test, ready, "call encoders agree on bytes and symbolic instruction ends");
    }
    uint32_t targets[BUSTER_TIER1_CALL_HELPERS] = {0};
    uint32_t used = fixture->width == 4 ? 64 : ready ? (entry_offset + (uint32_t)length + 15) & ~15u : 0;
    const uint32_t helper_limit = fixture->width == 4 ? entry_offset : BUSTER_TIER1_CALL_CODE_BYTES;
    for (uint32_t helper = 0; ready && helper < fixture->helper_count; helper += 1)
    {
        X86Instruction records[BUSTER_TIER1_CALL_RECORDS];
        const uint32_t count = tier1_call_helper(fixture, helper, observed, records);
        native &= count <= BUSTER_TIER1_CALL_RECORDS && tier1_emit_test_native(test, records, count);
        ready = count <= BUSTER_TIER1_CALL_RECORDS && x86_target_validate(test->allowed, records, count) == count;
        const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(count);
        ready = ready && used <= helper_limit && helper_capacity <= helper_limit - used;
        const int64_t helper_length = ready ? x86_encode(records, count, mapping + used, helper_capacity, 0, 0) : -1;
        ready = helper_length > 0;
        targets[helper] = used;
        addresses[helper] = (uintptr_t)(mapping + used);
        used = ready ? (used + (uint32_t)helper_length + 15) & ~15u : used;
        tier1_emit_test_expect(test, ready, "independent hostile helper encodes within local TEXT image");
    }
    uint32_t reference = 0;
    uint32_t call_node = 0;
    for (uint32_t record = 0; ready && record < code->count; record += 1)
    {
        if ((code->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
        {
            while (call_node < fixture->function.instruction_count && fixture->function.op[call_node] != IR_CALL)
            {
                call_node += 1;
            }
            ready = reference < emitted->reference_count && call_node < fixture->function.instruction_count;
            if (ready)
            {
                const LinkReference* fixup = &emitted->references[reference];
                const uint32_t end = ends[reference];
                const uint32_t distance = x86_fixup_distance(code->records[record].form, X86_FIXUP_DISPLACEMENT);
                ready = code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) && fixup->instruction == record &&
                    fixup->field == X86_FIXUP_DISPLACEMENT && fixup->kind == BUSTER_LINK_FIXUP_CALL && fixup->addend == 0 &&
                    fixup->target == fixture->function.a[call_node] && fixup->target > 0 && fixup->target <= fixture->helper_count &&
                    distance == 4 && end == offsets[record] + 5 && end <= (uint32_t)length && entry[offsets[record]] == 0xe8;
                if (ready)
                {
                    const int64_t displacement = (int64_t)targets[fixup->target - 1] - (entry_offset + (int64_t)end);
                    ready = displacement >= INT32_MIN && displacement <= INT32_MAX;
                    if (ready)
                    {
                        const int32_t relative = (int32_t)displacement;
                        memcpy(entry + end - distance, &relative, sizeof(relative));
                        int32_t decoded;
                        memcpy(&decoded, entry + offsets[record] + 1, sizeof(decoded));
                        ready = entry_offset + (int64_t)offsets[record] + 5 + decoded == targets[fixup->target - 1];
                    }
                }
            }
            reference += 1;
            call_node += 1;
        }
        else if ((code->records[record].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_CALL_REL32)
        {
            ready = false;
        }
    }
    // An indirect call has no reference.
    ready = ready && reference == emitted->reference_count && reference == (fixture->indirect ? 0 : fixture->helper_count);
    tier1_emit_test_expect(test, ready, "CALL fixups bijectively name original symbols and checked E8 rel32 fields");
    if (ready)
    {
        ready = mprotect(mapping, BUSTER_TIER1_CALL_CODE_BYTES, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "patched direct-call image is executable");
    }
    free(twin);
    free(twin_ends);
    free(ends);
    free(offsets);
    return ready && native ? entry : 0;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_call_invoke(const uint8_t* entry, const uint64_t* arguments)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &entry, sizeof(function));
    return function(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5]);
}

static __attribute__((no_sanitize("function"))) void tier1_call_invoke_void(const uint8_t* entry)
{
    void (*function)(void);
    memcpy(&function, &entry, sizeof(function));
    function();
}

static void tier1_call_execute(Tier1EmitTest* test, const Tier1CallFixture* fixture, Tier1EmitResult* emitted)
{
    Tier1CallObserved observed = {0};
    uint8_t* mapping = mmap(0, BUSTER_TIER1_CALL_CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    tier1_emit_test_expect(test, mapping != MAP_FAILED, "direct-call executable mapping");
    uint64_t addresses[BUSTER_TIER1_CALL_HELPERS] = { 0 };
    const uint8_t* entry = mapping != MAP_FAILED ? tier1_call_load(test, fixture, emitted, &observed, mapping, addresses) : 0;
    uint64_t random = UINT64_C(0xd8f164712353a675);
    for (uint32_t sample = 0; entry && sample < BUSTER_TIER1_CALL_SAMPLES; sample += 1)
    {
        uint64_t arguments[6];
        for (uint32_t argument = 0; argument < 6; argument += 1)
        {
            arguments[argument] = sample == 0 ? UINT64_MAX - argument : sample == 1 ? UINT64_C(1) << (argument * 9) : tier1_emit_test_random(&random);
        }
        uint64_t cell = tier1_emit_test_random(&random);
        const uint64_t initial = cell;
        uint64_t expected = 0;
        uint64_t passed[BUSTER_TIER1_CALL_HELPERS][6] = {{0}};
        uint32_t counts[BUSTER_TIER1_CALL_HELPERS] = {0};
        if (fixture->mixed)
        {
            const uint32_t narrow = fixture->permutation ? 1 : 2;
            const uint32_t pointer = fixture->permutation ? 2 : 1;
            arguments[0] |= UINT64_C(0x93b7d54100000000);
            arguments[narrow] = UINT64_C(0xfedcba9800000000) | (uint32_t)arguments[narrow];
            arguments[pointer] = (uintptr_t)&cell;
            passed[0][0] = (uint32_t)arguments[narrow];
            passed[0][1] = arguments[0];
            passed[0][2] = arguments[pointer];
            counts[0] = 3;
            expected = ((tier1_call_mix(passed[0], 3, 8) + arguments[0]) ^ arguments[pointer]) + (uint32_t)arguments[narrow];
            tier1_emit_test_expect(test, (arguments[0] >> 32) != 0 && (arguments[narrow] >> 32) != 0, "mixed cycle starts with live full-width bits and dirty I32 upper bits");
        }
        else if (fixture->narrow_return)
        {
            // The helper's RAX is the poison above 0x13671367: the result keeps the type's bits alone.
            const IrType narrow = fixture->narrow_type != 0 ? fixture->narrow_type : IR_I32;
            expected = narrow == IR_I8 ? 0x67 : narrow == IR_I16 ? 0x1367 : (uint32_t)UINT64_C(0x1367136713671367);
        }
        else if (fixture->return_type != 0)
        {
            expected = arguments[0] & (fixture->return_type == IR_I8 ? 0xff : 0xffff);
        }
        else if (fixture->fixed)
        {
            // An odd divisor and shift count below the width, which also keeps the signed division from overflowing.
            arguments[1] = (arguments[1] % (fixture->width * 8 - 1)) | 1;
            arguments[3] &= fixture->width * 8 - 1;
            if (fixture->width == 8)
            {
                const int64_t p0 = (int64_t)arguments[0];
                const int64_t p2 = (int64_t)arguments[2];
                expected = (uint64_t)(p0 / (int64_t)arguments[1]) + arguments[2] % arguments[1] + (arguments[0] << arguments[3]) + (uint64_t)(p2 >> arguments[3]) +
                           (arguments[0] >> arguments[1]) + arguments[0];
            }
            else
            {
                const uint32_t q0 = (uint32_t)arguments[0];
                const uint32_t q1 = (uint32_t)arguments[1];
                const uint32_t q2 = (uint32_t)arguments[2];
                const uint32_t q3 = (uint32_t)arguments[3];
                expected = (uint32_t)((uint32_t)((int32_t)q0 / (int32_t)q1) + q2 % q1 + (q0 << q3) + (uint32_t)((int32_t)q2 >> q3) + (q0 >> q1) + q0);
            }
        }
        else if (fixture->indirect)
        {
            arguments[0] = addresses[0];
            for (uint32_t argument = 0; argument < 3; argument += 1)
            {
                passed[0][argument] = arguments[argument + 1];
            }
            counts[0] = 3;
            expected = tier1_call_mix(passed[0], 3, 8) + arguments[1];
        }
        else if (fixture->parameter_type != 0)
        {
            // Dirty bits past the parameter's width, as a caller of another type's extension leaves them.
            arguments[0] |= UINT64_C(0xfedcba9876540000);
            expected = arguments[0] & (fixture->parameter_type == IR_I8 ? 0xff : 0xffff);
        }
        else if (fixture->memory)
        {
            arguments[0] = (uintptr_t)&cell;
            arguments[2] = sample % 6;
            passed[0][0] = arguments[0];
            passed[0][1] = arguments[2];
            passed[1][0] = arguments[0];
            passed[1][1] = arguments[1] ^ tier1_call_term(3);
            passed[1][2] = arguments[2];
            passed[2][0] = arguments[2];
            counts[0] = 2;
            counts[1] = 3;
            counts[2] = 1;
            expected = initial + arguments[1] + passed[1][1];
        }
        else if (fixture->pointer)
        {
            arguments[0] = (uintptr_t)&cell;
            passed[0][0] = arguments[0];
            counts[0] = 1;
            expected = arguments[0];
        }
        else if (fixture->empty)
        {
            expected = tier1_call_term(0);
        }
        else
        {
            for (uint32_t argument = 0; argument < fixture->arguments; argument += 1)
            {
                passed[0][argument] = arguments[tier1_call_argument(fixture->permutation, argument)];
                passed[1][argument] = arguments[argument];
            }
            const uint64_t first = tier1_call_mix(passed[0], fixture->arguments, fixture->width);
            passed[1][0] = first;
            expected = first + tier1_call_mix(passed[1], fixture->arguments, fixture->width);
            counts[0] = fixture->arguments;
            counts[1] = fixture->arguments;
            for (uint32_t argument = 0; argument < 6; argument += 1)
            {
                expected += arguments[argument];
            }
            for (uint32_t term = 0; term < fixture->terms; term += 1)
            {
                expected += arguments[0] ^ tier1_call_term(term);
            }
            expected = fixture->width == 4 ? (uint32_t)expected : expected;
        }
        memset(&observed, 0, sizeof(observed));
        if (fixture->void_return)
        {
            tier1_call_invoke_void(entry);
        }
        else
        {
            const uint64_t actual = tier1_call_invoke(entry, arguments);
            tier1_emit_test_expect(test, actual == expected, "hostile direct calls preserve independent arithmetic and return oracle");
        }
        uint64_t trace = 0;
        for (uint32_t helper = 0; helper < fixture->helper_count; helper += 1)
        {
            trace = (trace << 4) + helper + 1;
            tier1_emit_test_expect(test, observed.count[helper] == 1 && observed.alignment[helper] == 8, "each direct callee executes once with SysV entry alignment");
            for (uint32_t argument = 0; argument < counts[helper]; argument += 1)
            {
                const bool narrow = fixture->width == 4 || (fixture->mixed && argument == 0) || (fixture->memory && ((helper == 0 && argument == 1) || (helper == 1 && argument == 2) || helper == 2));
                const uint64_t mask = narrow ? UINT32_MAX : UINT64_MAX;
                tier1_emit_test_expect(test, (observed.arguments[helper][argument] & mask) == (passed[helper][argument] & mask), "ordered call arguments retain cycles, duplicates and semantic widths");
            }
            tier1_emit_test_expect(test, !fixture->variadic || (observed.al[helper] & 0xff) == 0, "a variadic call's AL says no vector register holds an argument");
            if (fixture->width == 4 && fixture->kind[helper] == TIER1_CALL_MIX)
            {
                tier1_emit_test_expect(test, (observed.result[helper] & UINT64_C(0xffffffff00000000)) == BUSTER_TIER1_CALL_RETURN_POISON, "I32 helper really returns noncanonical upper RAX bits");
            }
        }
        tier1_emit_test_expect(test, observed.trace == trace, "value and void direct calls retain source order");
        if (fixture->memory)
        {
            tier1_emit_test_expect(test, cell == passed[1][1] && observed.result[0] == arguments[1], "load/store helper ABI observes stores and changes subsequent loads");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, BUSTER_TIER1_CALL_CODE_BYTES);
    }
}

static void tier1_call_compile(Tier1EmitTest* test, const Tier1CallFixture* fixture, bool vector, bool rematerialize, uint32_t frame_flags)
{
    test->terms = fixture->terms;
    test->width = fixture->width;
    test->selects = false;
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = tier1_select_plan(&fixture->function, 0, &test->output, &test->scratch, vector, &plan, &node);
    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "original direct-call IR selects");
    if (selected == TIER1_SELECT_OK)
    {
        const Tier1EmitOptions options = {
            .register_mask = fixture->void_return ? 0 : fixture->narrow_return ? 1u << X86_REGISTER_RAX : tier1_liveness_register_mask(),
            .frame_flags = frame_flags,
            .vector = vector,
            .rematerialize = rematerialize,
        };
        Tier1EmitResult emitted;
        const uint64_t scratch_mark = test->scratch.length;
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "direct-call liveness/allocation/frame emission succeeds");
        if (error != TIER1_EMIT_OK && test->failures < 20)
        {
            fprintf(stderr, "  call emission error=%u node=%u arguments=%u permutation=%u memory=%u vector=%u remat=%u\n", error, node, fixture->arguments, fixture->permutation, fixture->memory, vector, rematerialize);
        }
        tier1_emit_test_expect(test, test->scratch.length == scratch_mark, "direct-call emission restores scratch");
        if (error == TIER1_EMIT_OK)
        {
            tier1_emit_test_expect(test, (fixture->helper_count == 0 || !emitted.frame.red_zone) && emitted.reference_count == (fixture->indirect ? 0 : fixture->helper_count),
                                   "calls force a real non-red-zone frame and retain every symbolic target");
            // A narrow parameter's function goes through the CFG consumer, whose allocation its own checker proves.
            Tier1CfgCheck check;
            const bool verified = emitted.general ? tier1_cfg_verify(&emitted.cfg_input, &emitted.cfg, &test->scratch, &check) : tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation);
            tier1_emit_test_expect(test, verified, "direct-call allocation passes independent interference verification");
            if (fixture->narrow_return)
            {
                // An I32 result's immediate MOV32 self-capture, and a narrow one's zero extension, before any later ZEXT.
                const IrType narrow = fixture->narrow_type != 0 ? fixture->narrow_type : IR_I32;
                const uint16_t form = narrow == IR_I8 ? X86_FORM_MOVZX_R32_RM8 : narrow == IR_I16 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOV_R32_RM32;
                const uint32_t capture = emitted.references[0].instruction + 1;
                const bool within_call = capture < emitted.code.node_first[1] && capture < emitted.code.count;
                tier1_emit_test_expect(test, within_call && emitted.code.records[capture].form == form &&
                    emitted.code.records[capture].reg == X86_REGISTER_RAX && emitted.code.records[capture].rm == X86_REGISTER_RAX,
                    "a narrow result in RAX has its own immediate capture to its canonical storage before any later ZEXT");
            }
            else if (fixture->return_type != 0)
            {
                tier1_emit_test_expect(test, emitted.reference_count == 0, "a narrow return's function has no call");
            }
            else if (fixture->fixed)
            {
                tier1_emit_test_expect(test, emitted.general && emitted.call_stores != 0 && emitted.call_stores == emitted.call_loads,
                                       "values live past a division and a shift in their fixed registers are saved around them");
            }
            else if (fixture->indirect)
            {
                bool through = false;
                for (uint32_t r = 0; r < emitted.code.count; r += 1)
                {
                    through |= emitted.code.records[r].form == X86_FORM_CALL_RM64 && emitted.code.records[r].rm == X86_REGISTER_R10;
                }
                // The parameter live across the call is in a callee-saved register from the entry on, or saved around it.
                tier1_emit_test_expect(test, emitted.general && through && (emitted.callee_used != 0 || emitted.call_stores != 0),
                                       "an indirect call goes through R10, its live value kept across it");
            }
            else if (fixture->parameter_type != 0)
            {
                // The entry's zero extension of the parameter in its incoming register.
                bool extended = false;
                for (uint32_t r = 0; r < emitted.code.count; r += 1)
                {
                    const X86Instruction* record = &emitted.code.records[r];
                    extended |= record->form == (fixture->parameter_type == IR_I8 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16) && record->reg == X86_REGISTER_RDI && record->rm == X86_REGISTER_RDI;
                }
                tier1_emit_test_expect(test, emitted.general && extended, "a narrow parameter is zero-extended in its incoming register");
            }
            else if (fixture->mixed)
            {
                uint32_t incoming = 0;
                const Tier1Allocation* allocation = &emitted.allocation;
                const Tier1AllocationInput* input = &emitted.prepared.allocation;
                for (uint32_t interval = 0; interval < input->count; interval += 1)
                {
                    const uint32_t value = input->values[interval];
                    for (uint32_t fragment = allocation->fragment_offsets[interval]; value < 3 && fragment < allocation->fragment_offsets[interval + 1]; fragment += 1)
                    {
                        if (allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->registers[fragment] == abi_integer_arguments[value] &&
                            allocation->starts[fragment] <= fixture->first_call * 2 && allocation->ends[fragment] > fixture->first_call * 2)
                        {
                            incoming |= 1u << value;
                        }
                    }
                }
                tier1_emit_test_expect(test, incoming == 7, "mixed-width swap/three-cycle really starts in the incoming ABI registers");
                tier1_emit_test_expect(test, emitted.call_stores != 0 && emitted.call_loads != 0, "mixed-width values also survive the hostile call");
            }
            else if (!fixture->memory && !fixture->pointer && !fixture->empty)
            {
                tier1_emit_test_expect(test, emitted.call_stores != 0 && emitted.call_loads != 0, "caller-saved values actually cross hostile calls");
                if (fixture->terms > 8)
                {
                    tier1_emit_test_expect(test, emitted.spill_stores != 0 && emitted.spill_loads != 0, "direct-call pressure also exercises ordinary spills");
                }
                if (fixture->terms == 1)
                {
                    bool rax_live = false;
                    const Tier1Allocation* allocation = &emitted.allocation;
                    const Tier1AllocationInput* input = &emitted.prepared.allocation;
                    for (uint32_t interval = 0; interval < input->count; interval += 1)
                    {
                        for (uint32_t fragment = allocation->fragment_offsets[interval]; fragment < allocation->fragment_offsets[interval + 1]; fragment += 1)
                        {
                            rax_live |= input->values[interval] == 7 && allocation->kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->registers[fragment] == X86_REGISTER_RAX &&
                                allocation->starts[fragment] <= fixture->first_call * 2 && allocation->ends[fragment] > fixture->first_call * 2 + 1;
                        }
                    }
                    tier1_emit_test_expect(test, rax_live, "fixture really aliases live-across RAX with the ABI return register");
                }
            }
            else if (fixture->empty)
            {
                tier1_emit_test_expect(test, emitted.call_stores == 0 && emitted.call_loads == 0 && emitted.allocation.spill_count == 0, "zero-argument void call needs alignment even without spills or saves");
                if (fixture->void_return)
                {
                    tier1_emit_test_expect(test, emitted.prepared.allocation.count == 0 && emitted.frame.count == 0, "void call/return with an empty register mask has no SSA values or save rows");
                }
            }
            tier1_call_execute(test, fixture, &emitted);
        }
    }
    arena_pop_to(&test->output, 0);
}

static void tier1_call_failures(Tier1EmitTest* test)
{
    Tier1CallFixture fixture = {
        .width = 8,
        .arguments = 6,
        .terms = 1,
    };
    Tier1SelectPlan plan;
    uint32_t node;
    bool ready = tier1_call_fixture(test, &fixture) && tier1_select_plan(&fixture.function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, ready, "transactional direct-call fixture selects");
    if (ready)
    {
        const uint64_t output_mark = test->output.length;
        const uint64_t scratch_mark = test->scratch.length;
        const Tier1EmitOptions options = {
            .register_mask = tier1_liveness_register_mask(),
            .vector = true,
        };
        Tier1EmitResult success;
        ready = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &success, &node) == TIER1_EMIT_OK;
        const uint64_t required = test->output.length;
        arena_pop_to(&test->output, output_mark);
        tier1_emit_test_expect(test, ready, "direct-call failure test measures a successful retained allocation");
        uint8_t* denied = malloc(X86_FORM_COUNT);
        tier1_emit_test_expect(test, denied != 0, "direct-call target denial storage");
        if (denied)
        {
            memcpy(denied, test->allowed, X86_FORM_COUNT);
            denied[X86_FORM_CALL_REL32] = 0;
        }
        for (uint32_t failure = 0; ready && denied && failure < 4; failure += 1)
        {
            Arena output = test->output;
            Arena scratch = test->scratch;
            if (failure == 1 || failure == 3)
            {
                output.capacity = failure == 1 ? output_mark + 1 : required - 1;
            }
            if (failure == 2)
            {
                scratch.capacity = scratch_mark + 1;
            }
            Tier1EmitResult result;
            Tier1EmitResult before;
            memset(&result, 0xa5, sizeof(result));
            memcpy(&before, &result, sizeof(result));
            const Tier1EmitError error = tier1_emit_function(&plan, &options, failure == 0 ? denied : test->allowed, &output, &scratch, &result, &node);
            tier1_emit_test_expect(test, failure == 0 ? error == TIER1_EMIT_TARGET : error == TIER1_EMIT_MEMORY, "call-only target denial and early/late arena failures report explicit categories");
            tier1_emit_test_expect(test, output.length == output_mark && scratch.length == scratch_mark && memcmp(&before, &result, sizeof(result)) == 0, "failed direct-call emission publishes no references, saves or partial result");
        }
        free(denied);
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    // Past six arguments, and I128 and F64 results; variadic calls, calls through a pointer and narrow results are taken.
    static const uint32_t failures[] = { 0, 4, 5 };
    for (uint32_t index = 0; index < sizeof(failures) / sizeof(failures[0]); index += 1)
    {
        const uint32_t failure = failures[index];
        const uint32_t arguments = failure == 0 ? 7 : failure < 3 ? 1 : 0;
        const IrType parameter = failure == 2 ? IR_PTR : IR_I64;
        const IrType result = failure == 3 ? IR_I8 : failure == 4 ? IR_I128 : failure == 5 ? IR_F64 : IR_I64;
        const IrSizes sizes = {
            .instruction_count = 3,
            .block_count = 1,
            .extra_count = arguments,
            .call_count = 1,
            .call_type_count = arguments,
            .parameter_count = 1,
        };
        IrFunction function;
        ready = ir_create(&test->input, &sizes, &function) == IR_OK;
        if (ready)
        {
            function.block_offsets[1] = 3;
            function.result_type = (uint8_t)result;
            function.parameter_types[0] = (uint8_t)parameter;
            tier1_emit_test_node(&function, 0, IR_PARAM, parameter, 0, BUSTER_IR_NONE);
            for (uint32_t argument = 0; argument < arguments; argument += 1)
            {
                function.extra[argument] = 0;
            }
            tier1_call_descriptor(&function, 0, 0, arguments, result);
            function.calls[0].variadic = failure == 1;
            tier1_emit_test_node(&function, 1, failure == 2 ? IR_CALL_INDIRECT : IR_CALL, result, failure == 2 ? 0 : 1, 0);
            tier1_emit_test_node(&function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
            ready = ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
            tier1_emit_test_expect(test, ready, "unsupported-call fixture is valid semantic IR");
        }
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            Tier1SelectPlan rejected;
            Tier1SelectPlan before;
            memset(&rejected, 0xa5, sizeof(rejected));
            memcpy(&before, &rejected, sizeof(rejected));
            const uint64_t mark = test->output.length;
            const uint64_t scratch_mark = test->scratch.length;
            const Tier1SelectError error = tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &rejected, &node);
            tier1_emit_test_expect(test, error == TIER1_SELECT_ERROR_UNSUPPORTED, "stack arguments and wide or floating results explicitly refuse");
            tier1_emit_test_expect(test, test->output.length == mark && test->scratch.length == scratch_mark && memcmp(&before, &rejected, sizeof(rejected)) == 0, "unsupported-call selection is transactional");
        }
        arena_pop_to(&test->input, 0);
    }
}

static void tier1_call_cases(Tier1EmitTest* test)
{
    arena_pop_to(&test->input, 0);
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t shape = 0; shape < 9; shape += 1)
        {
            Tier1CallFixture fixture = {
                .width = width,
                .arguments = shape < 7 ? shape : 6,
                .permutation = shape < 7 ? 0 : shape - 6,
                .terms = shape % 3 == 0 ? 0 : shape % 3 == 1 ? 1 : 17,
            };
            // Every other shape's calls variadic too.
            for (uint32_t variadic = 0; variadic < 1 + (shape & 1); variadic += 1)
            {
                fixture.variadic = variadic != 0;
                const bool ready = tier1_call_fixture(test, &fixture);
                tier1_emit_test_expect(test, ready, "construct direct-call shuffle and pressure fixture");
                for (uint32_t vector = 0; ready && vector < 2; vector += 1)
                {
                    for (uint32_t rematerialize = 0; rematerialize < 2; rematerialize += 1)
                    {
                        const uint32_t flags = shape % 3 == 0 ? 0 : shape % 3 == 1 ? BUSTER_TIER1_FRAME_KEEP_POINTER : BUSTER_TIER1_FRAME_NO_RED_ZONE | BUSTER_TIER1_FRAME_PROBE_STACK;
                        tier1_call_compile(test, &fixture, vector != 0, rematerialize != 0, flags);
                    }
                }
                arena_pop_to(&test->input, 0);
            }
        }
    }
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        Tier1CallFixture fixture = {0};
        const bool ready = shape == 0 ? tier1_call_memory_fixture(test, &fixture) : tier1_call_small_fixture(test, &fixture, shape == 1, shape == 3);
        tier1_emit_test_expect(test, ready, "construct helper-signature, pointer and empty-call fixture");
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            for (uint32_t frame = 0; frame < 2; frame += 1)
            {
                tier1_call_compile(test, &fixture, vector != 0, true, frame ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0);
            }
        }
        arena_pop_to(&test->input, 0);
    }
    // The mixed cycles, the dirty I32, I8 and I16 call results, the I8 and I16 parameters, the indirect call, the I8
    // and I16 returns, and the 64- and 32-bit divisions and shifts.
    for (uint32_t shape = 0; shape < 12; shape += 1)
    {
        Tier1CallFixture fixture = {
            .narrow_type = shape == 3 ? IR_I8 : shape == 4 ? IR_I16 : 0,
            .parameter_type = shape == 5 ? IR_I8 : shape == 6 ? IR_I16 : 0,
            .return_type = shape == 8 ? IR_I8 : shape == 9 ? IR_I16 : 0,
            .width = shape == 10 ? 8u : shape == 11 ? 4u : 0u,
        };
        const bool ready = shape < 2 ? tier1_call_mixed_fixture(test, &fixture, shape != 0) : shape < 5 ? tier1_call_narrow_fixture(test, &fixture) :
                           shape < 7 ? tier1_call_parameter_fixture(test, &fixture) : shape == 7 ? tier1_call_indirect_fixture(test, &fixture) :
                           shape < 10 ? tier1_call_return_fixture(test, &fixture) : tier1_call_fixed_fixture(test, &fixture);
        tier1_emit_test_expect(test, ready, "construct genuine mixed-width cycle, dirty narrow return or narrow parameter fixture");
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            for (uint32_t frame = 0; frame < 2; frame += 1)
            {
                tier1_call_compile(test, &fixture, vector != 0, true, frame ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0);
            }
        }
        arena_pop_to(&test->input, 0);
    }
    tier1_call_failures(test);
}

// The enclosing test runner already isolates generated instructions. This additional bounded child makes a bad
// call target or return address fail this suite without hanging that runner; the counter page survives a normal exit.
static void tier1_call_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        const uint64_t native_skipped = test->native_skipped;
        tier1_call_cases(test);
        if (test->native_skipped != native_skipped)
        {
            printf("tier1 calls: %llu native-code groups unavailable on host (execution skipped)\n", (unsigned long long)(test->native_skipped - native_skipped));
        }
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0xcafec001d15ea5ed);
        fflush(0);
        _exit(report[1] != 0);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0xcafec001d15ea5ed);
    tier1_emit_test_expect(test, finished, "bounded hostile-call child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "hostile-call child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 direct-call child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
