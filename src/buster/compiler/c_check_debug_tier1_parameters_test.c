// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Direct machine-proven parameter ranges for test-check-debug (#1114). Included after c_check_tier1.c, whose
// helper types follow the driver tests in main.c. The earlier suite calls this through its forward declaration.

// Each expected cell names the actual register containing one whole source parameter before that instruction,
// or UINT8_MAX when there is no single proven carrier. These small programs specify the machine-level behavior
// independently of source lines, allocation intervals or the producer's choice of range boundaries.
static void check_debug_tier1_values_parameter_case(CheckDebugTestTally* tally, const Tier0Function* function, IrDebugScalar* values, Tier1SelectCode code,
                                                  const uint32_t* order, const uint8_t* expected, const char* description)
{
    const IrDebugInfo debug = { .scalars = values, .scalar_count = function->parameter_count };
    const Tier1EmitResult emitted = { .code = code };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        _Alignas(64) uint8_t storage[8192] = { 0 };
        Arena scratch = { .base = storage, .capacity = sizeof(storage) };
        CheckTier1DebugParameters parameters = { 0 };
        EmittedLocalRange locations[64] = { 0 };
        uint8_t actual[128];
        memset(actual, UINT8_MAX, sizeof(actual));
        const uint64_t cells = (uint64_t)function->local_count * code.count;
        bool valid = cells <= sizeof(actual) && check_tier1_debug_parameters(function, &debug, &emitted, order, path != 0, &scratch, &parameters);
        const uint64_t count = valid ? check_tier1_debug_parameter_ranges(&parameters, path != 0, NULL) : 0;
        valid = valid && count <= BUSTER_ARRAY_LENGTH(locations);
        valid = valid && check_tier1_debug_parameter_ranges(&parameters, path != 0, locations) == count;
        for (uint64_t k = 0; valid && k < count; k += 1)
        {
            const EmittedLocalRange location = locations[k];
            valid = location.local < function->local_count && location.begin < location.end && location.end <= code.count &&
                    location.home.is_register && location.home.reg < 16 && location.home.mode == 0 && location.value_bytes == 0;
            const uint32_t base = valid && tier0_class_floating(function->local_classes[location.local]) ? 16 : 0;
            for (uint32_t r = location.begin; valid && r < location.end; r += 1)
            {
                const uint64_t cell = (uint64_t)location.local * code.count + r;
                valid = actual[cell] == UINT8_MAX;
                actual[cell] = (uint8_t)(base + location.home.reg);
            }
        }
        check_debug_test_expect(tally, valid && memcmp(actual, expected, cells) == 0,
                                "parameter ranges match the independently specified machine carriers on both paths", description);
    }
}

static void check_debug_tier1_values_parameters(CheckDebugTestTally* tally)
{
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I8, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F32 };
    AbiLocation abi[] = {
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE } },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_SIGN },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_REGISTER_VECTOR, BUSTER_ABI_REGISTER_NONE } },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_REGISTER_VECTOR | 1, BUSTER_ABI_REGISTER_NONE } },
    };
    IrDebugScalar values[] = {
        { .local = 0, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 8 },
        { .local = 1, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 8 },
        { .local = 2, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 1 },
        { .local = 3, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 8 },
        { .local = 4, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 4 },
    };
    Tier0Function function = { .local_classes = classes, .local_count = BUSTER_ARRAY_LENGTH(classes), .parameter_count = BUSTER_ARRAY_LENGTH(classes), .parameters = abi };
    X86Instruction linear[] = {
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_R12, .rm = X86_REGISTER_RSI },
        { .form = X86_FORM_MOVSX_R32_RM8, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_R13, .rm = X86_REGISTER_RDX },
        { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RSI, .rm = X86_REGISTER_RCX },
        { .form = X86_FORM_MOVSD_X_XM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = 2, .rm = 0 },
        { .form = X86_FORM_MOVSS_X_XM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = 0, .rm = 1 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_CALL_REL32 },
        { .form = X86_FORM_ADD_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_MOV_R8_RM8, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_R13, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_RET },
    };
    uint8_t expected[][BUSTER_ARRAY_LENGTH(linear)] = {
        { X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, UINT8_MAX, UINT8_MAX },
        { X86_REGISTER_RSI, X86_REGISTER_RSI, X86_REGISTER_RSI, X86_REGISTER_RSI, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX },
        { X86_REGISTER_RDX, X86_REGISTER_RDX, X86_REGISTER_RDX, X86_REGISTER_R13, X86_REGISTER_R13, X86_REGISTER_R13, X86_REGISTER_R13, X86_REGISTER_R13, X86_REGISTER_R13, X86_REGISTER_R13, UINT8_MAX },
        { 16, 16, 16, 16, 16, 16, 18, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX },
        { 17, 17, 17, 17, 17, 17, 16, UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX },
    };
    Tier1SelectCode code = { .records = linear, .count = BUSTER_ARRAY_LENGTH(linear) };
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, expected[0],
                                           "whole and narrow copies, floating carriers, real writes and suspended caller clobbers");
    linear[8].form = X86_FORM_MOV_R32_RM32;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, expected[0],
                                           "a partial-width overwrite kills a formerly preserved 64-bit parameter");
    uint32_t order[] = { 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    expected[0][1] = X86_REGISTER_RDI;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, order, expected[0],
                                           "carrier boundaries follow the scheduler's final instruction permutation");
    expected[0][1] = X86_REGISTER_RBX;
    abi[1].kind = BUSTER_ABI_LOCATION_STACK;
    memset(expected[1], UINT8_MAX, sizeof(expected[1]));
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, expected[0],
                                           "a stack-passed parameter is unavailable even when its nominal register is copied");
    memset(expected, UINT8_MAX, sizeof(expected));
    function.returns_twice = true;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, expected[0], "a returns-twice function has no register parameter proof");
    function.returns_twice = false;
    function.interrupt = 1;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, expected[0], "an interrupt entry has no ordinary ABI parameter proof");
    function.interrupt = 0;
    function.local_count = 1;
    function.parameter_count = 1;

    // Both arms destroy the ABI carrier. The join can keep RBX only when every arm copied the same parameter there.
    X86Instruction diamond[] = {
        { .form = X86_FORM_JE_REL32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSI },
        { .form = X86_FORM_JMP_REL32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSI },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_RET },
    };
    uint32_t diamond_branches[] = { 0, 3 };
    uint32_t diamond_targets[] = { 4, 6 };
    uint8_t joined[] = { X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX };
    code = (Tier1SelectCode){ .records = diamond, .count = BUSTER_ARRAY_LENGTH(diamond), .branches = diamond_branches, .targets = diamond_targets, .branch_count = 2 };
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, joined, "matching predecessor copies survive a diamond join");
    diamond[4].reg = X86_REGISTER_R12;
    joined[5] = X86_REGISTER_R12;
    joined[6] = UINT8_MAX;
    joined[7] = UINT8_MAX;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, joined, "different registers on the two arms do not invent a join location");
    diamond[4].reg = X86_REGISTER_RBX;
    diamond[4].rm = X86_REGISTER_RSI;
    joined[5] = X86_REGISTER_RDI;
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, joined, "a register holding a different value on one predecessor is unavailable after the join");

    // The first trip's carrier cannot be advertised at the loop header if a backedge overwrites it later.
    X86Instruction loop[] = {
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_JE_REL32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSI },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_JMP_REL32 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_RET },
    };
    uint32_t loop_branches[] = { 1, 4 };
    uint32_t loop_targets[] = { 5, 1 };
    uint8_t loop_values[] = { X86_REGISTER_RDI, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX, X86_REGISTER_RBX };
    code = (Tier1SelectCode){ .records = loop, .count = BUSTER_ARRAY_LENGTH(loop), .branches = loop_branches, .targets = loop_targets, .branch_count = 2 };
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, loop_values, "a preserved copy survives every backedge even after the ABI carrier is killed");
    loop[3] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RAX };
    memset(loop_values + 1, UINT8_MAX, sizeof(loop_values) - 1);
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, loop_values, "a backedge kill removes the first-trip value before publishing any loop ranges");

    X86Instruction dead[] = {
        { .form = X86_FORM_JMP_REL32 },
        { .form = X86_FORM_MOV_R64_RM64, .mode = BUSTER_X86_REGISTER_DIRECT, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI },
        { .form = X86_FORM_RET },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_RET },
    };
    uint32_t dead_branches[] = { 0 };
    uint32_t dead_targets[] = { 3 };
    const uint8_t dead_values[] = { X86_REGISTER_RDI, UINT8_MAX, UINT8_MAX, X86_REGISTER_RDI, X86_REGISTER_RDI };
    code = (Tier1SelectCode){ .records = dead, .count = BUSTER_ARRAY_LENGTH(dead), .branches = dead_branches, .targets = dead_targets, .branch_count = 1 };
    check_debug_tier1_values_parameter_case(tally, &function, values, code, NULL, dead_values, "unreachable instructions never inherit the function entry's carrier seed");
}

