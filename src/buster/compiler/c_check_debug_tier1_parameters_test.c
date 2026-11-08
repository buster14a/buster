// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Direct machine-proven parameter and shared-object ranges for test-check-debug (#1114). Included after c_check_tier1.c, whose
// helper types follow the driver tests in main.c. The earlier suite calls this through its forward declaration.

#include "c_check_debug_tier1_bindings_test.c"

// The code certificate has its own full-emission mutation fixtures. These small, already-certified record streams
// isolate the additional source-object lifetime proof: byte-complete initialization, final record order, overwrites
// and source gaps. The expected PC intervals are written explicitly, independently of the clipping implementation.
static void check_debug_tier1_values_object_case(CheckDebugTestTally* tally, const Tier0Function* function, const IrFunction* ir,
                                               const Tier1EmitResult* emitted, const uint32_t* order, CheckTier1DebugSpan expected_clip,
                                               const CheckTier1DebugSpan* expected, uint32_t expected_count, const char* description)
{
    const uint32_t uses[] = { 2 };
    const uint32_t slots[] = { 0 };
    const Tier0Home homes[] = { { .displacement = -16, .reg = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE } };
    const CheckTier1DebugSpan spans[] = { { .begin = 1, .end = 4 }, { .begin = 5, .end = 7 } };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        _Alignas(64) uint8_t storage[8192] = { 0 };
        Arena scratch = { .base = storage, .capacity = sizeof(storage) };
        CheckTier1DebugSpan* clips = NULL;
        EmittedLocalRange locations[3] = { [2] = { .local = 77 } };
        bool valid = check_tier1_debug_object_clips(function, ir, emitted, order, uses, slots, homes, path != 0, &scratch, &clips);
        valid = valid && clips != NULL && clips[0].begin == expected_clip.begin && clips[0].end == expected_clip.end;
        const uint64_t count = valid ? check_tier1_debug_object_ranges(homes, slots, 1, spans, BUSTER_ARRAY_LENGTH(spans), clips, NULL) : UINT64_MAX;
        valid = valid && count == expected_count && count < BUSTER_ARRAY_LENGTH(locations);
        valid = valid && check_tier1_debug_object_ranges(homes, slots, 1, spans, BUSTER_ARRAY_LENGTH(spans), clips, locations) == count;
        for (uint32_t k = 0; valid && k < expected_count; k += 1)
        {
            const EmittedLocalRange location = locations[k];
            valid = location.local == 0 && location.begin == expected[k].begin && location.end == expected[k].end &&
                    location.home.displacement == -16 && location.home.reg == X86_REGISTER_RSP && location.home.mode == BUSTER_X86_MEMORY_BASE &&
                    !location.home.is_register && location.value_bytes == 0;
        }
        check_debug_test_expect(tally, valid && locations[2].local == 77,
                                "shared-object locations match explicit initialization, overwrite and source-gap boundaries on both paths", description);
    }
}

static void check_debug_tier1_values_objects(CheckDebugTestTally* tally)
{
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_I32 };
    const Tier0Function function = { .count = 7, .local_classes = classes, .local_count = 1 };
    uint8_t operations[] = { IR_STACK_ADDR, IR_LIFETIME, IR_CONST, IR_STORE, IR_COPY, IR_LIFETIME, IR_RET };
    uint8_t types[] = { IR_PTR, IR_VOID, IR_I32, IR_VOID, IR_I32, IR_VOID, IR_VOID };
    uint32_t operands[] = { 0, 0, 0, 0, 2, 0, 4 };
    uint32_t seconds[] = { 0, 0, 0, 2, 0, 1, 0 };
    uint32_t block_offsets[] = { 0, 7 };
    uint32_t typed_origins[] = { 0, 1, 2, 3, 4, 5, 6 };
    uint32_t source_locals[] = { 0 };
    IrSlot slots[] = { { .size = 4, .alignment = 4, .type = IR_I32, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN } };
    IrDebugInfo debug = { .origins = typed_origins, .locals = source_locals, .instruction_count = 7, .slot_count = 1, .entry_count = 7, .local_count = 1 };
    IrFunction ir = {
        .op = operations, .type = types, .a = operands, .b = seconds, .instruction_count = 7,
        .block_offsets = block_offsets, .block_count = 1, .slots = slots, .slot_count = 1, .debug = &debug,
    };
    X86Instruction records[] = {
        { .form = X86_FORM_MOV_RM32_R32, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RSP, .displacement = -16 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_MOV_RM32_R32, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .displacement = -16 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_MOV_RM32_R32, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RSP, .displacement = -12 },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_NOP },
        { .form = X86_FORM_RET },
    };
    uint32_t origins[] = {
        tier1_origin(TIER1_ORIGIN_NODE, 0), tier1_origin(TIER1_ORIGIN_NODE, 2), tier1_origin(TIER1_ORIGIN_NODE, 3), tier1_origin(TIER1_ORIGIN_NODE, 4),
        tier1_origin(TIER1_ORIGIN_NODE, 4), tier1_origin(TIER1_ORIGIN_NODE, 4), tier1_origin(TIER1_ORIGIN_NODE, 5), tier1_origin(TIER1_ORIGIN_NODE, 6),
    };
    uint32_t node_first[] = { 0, 2, 2, 2, 3, 6, 7, 8 };
    uint32_t block_records[] = { 0, 8 };
    uint32_t physical[] = { 0, 0 };
    int32_t offsets[] = { -16, -16 };
    Tier1EmitResult emitted = {
        .code = { .records = records, .count = BUSTER_ARRAY_LENGTH(records), .node_first = node_first },
        .frame = { .count = 2, .slot_count = 1, .slot = physical, .offset = offsets, .base_register = X86_REGISTER_RSP,
                   .red_zone = true, .red_zone_bytes = 32, .payload_bytes = 32 },
        .general = true, .cfg_input = { .block_count = 1 }, .cfg = { .allocation = { .spill_count = 1 } },
        .block_records = block_records, .origins = origins, .code_checked = true, .code_reason = TIER1_CERT_EMITTED_OK,
    };
    const CheckTier1DebugSpan baseline[] = { { .begin = 3, .end = 4 }, { .begin = 5, .end = 6 } };
    const CheckTier1DebugSpan overwritten[] = { { .begin = 3, .end = 4 } };
    const CheckTier1DebugSpan delayed[] = { { .begin = 5, .end = 6 } };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 3, 6 }, baseline, 2,
                                        "an earlier spill dies before initialization, an adjacent write is harmless and the source gap stays empty");
    records[4].displacement = -13;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 3, 5 }, overwritten, 1,
                                        "a foreign store overlapping just the object's last byte ends its home after that actual write");
    records[4].displacement = -20;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 3, 6 }, baseline, 2,
                                        "a foreign store ending exactly at the object leaves its bytes intact");
    records[4].displacement = -12;
    records[4].mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
    records[4].index = X86_REGISTER_RDX;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 3, 5 }, overwritten, 1,
                                        "an indexed frame write has no disjoint-byte proof and closes the shared home");
    records[4].mode = BUSTER_X86_MEMORY_BASE;
    records[4].index = 0;

    records[2].form = X86_FORM_MOV_RM8_R8;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "a one-byte store does not initialize the four-byte source object");
    records[2].form = X86_FORM_MOV_RM32_R32;
    operands[3] = 4;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "a cached address with the same physical displacement does not establish exact source-object identity");
    operands[3] = 0;
    operations[5] = IR_COPY;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "a lifetime without its explicit end cannot justify shared storage");
    operations[5] = IR_LIFETIME;
    operations[4] = IR_LIFETIME;
    operands[4] = 0;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "repeated lifetime starts remain unavailable instead of using an allocator hull");
    operations[4] = IR_COPY;
    operands[4] = 2;
    emitted.code_checked = false;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, NULL, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "matching stores without the complete code certificate do not certify shared storage");
    emitted.code_checked = true;

    uint32_t delayed_order[] = { 0, 3, 2, 1, 4, 5, 6, 7, 8 };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, delayed_order, (CheckTier1DebugSpan){ 4, 6 }, delayed, 1,
                                        "a pre-lifetime record scheduled later delays opening until the whole prefix has executed");
    uint32_t early_order[] = { 0, 1, 2, 3, 4, 6, 5, 7, 8 };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, early_order, (CheckTier1DebugSpan){ 3, 5 }, overwritten, 1,
                                        "a post-lifetime record scheduled earlier closes the home before that final suffix");
    uint32_t crossed_order[] = { 0, 3, 2, 1, 6, 4, 5, 7, 8 };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, crossed_order, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "crossing final prefix and suffix bounds leave no publishable lifetime");
    records[3] = records[4];
    records[3].displacement = -16;
    uint32_t hidden_order[] = { 0, 2, 3, 1, 4, 5, 6, 7, 8 };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, hidden_order, (CheckTier1DebugSpan){ 4, 3 }, NULL, 0,
                                        "an overwrite between initialization and delayed visibility prevents a later stale opening");
    records[3] = (X86Instruction){ .form = X86_FORM_NOP };

    // A machine block with only a fallthrough predecessor need not start a scheduling region. Independent records
    // can legally cross that original IR boundary; debug metadata must shrink the lifetime without rejecting code.
    uint32_t source_blocks[] = { 0, 6, 7 };
    uint32_t machine_blocks[] = { 0, 6, 8 };
    ir.block_count = 2;
    ir.block_offsets = source_blocks;
    emitted.cfg_input.block_count = 2;
    emitted.block_records = machine_blocks;
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, early_order, (CheckTier1DebugSpan){ 3, 5 }, overwritten, 1,
                                        "legal fallthrough motion across an original IR block boundary retains the smaller proved interval");
    uint32_t erased_order[] = { 0, 1, 6, 2, 3, 4, 5, 7, 8 };
    check_debug_tier1_values_object_case(tally, &function, &ir, &emitted, erased_order, (CheckTier1DebugSpan){ 0 }, NULL, 0,
                                        "cross-boundary motion that reaches the suffix before initialization leaves the shared object unavailable");
    ir.block_count = 1;
    ir.block_offsets = block_offsets;
    emitted.cfg_input.block_count = 1;
    emitted.block_records = block_records;

    // Exercise the publication entry point too: both arenas are borrowed, and a late output shortage must discard
    // every tentative range just as an early scratch shortage does. Success keeps only output and releases scratch.
    // A malformed block table is a separate rejection; legal cross-boundary motion above is not malformed metadata.
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t failure = 0; failure < 4; failure += 1)
        {
            _Alignas(64) uint8_t output_storage[4096] = { 0 };
            _Alignas(64) uint8_t scratch_storage[16384] = { 0 };
            Arena output = { .base = output_storage, .length = 64, .capacity = failure == 1 ? 64 : sizeof(output_storage) };
            Arena scratch = { .base = scratch_storage, .length = 64, .capacity = failure == 2 ? 64 : sizeof(scratch_storage) };
            CheckTier1Debug result = { .source_count = 55, .local_range_count = 66 };
            block_records[1] = failure == 3 ? 9 : 8;
            const bool success = check_tier1_debug(&function, &ir, &emitted, NULL, 7, path != 0, &output, &scratch, &result);
            bool valid = scratch.length == 64 && success == (failure == 0);
            if (success)
            {
                valid = valid && output.length > 64 && result.source_count != 0 && result.local_range_count == 1;
                valid = valid && result.local_ranges[0].local == 0 && result.local_ranges[0].begin == 3 && result.local_ranges[0].end == 6 &&
                        result.local_ranges[0].home.displacement == -16;
            }
            else
            {
                valid = valid && output.length == 64 && result.source_records == NULL && result.source_origins == NULL &&
                        result.source_count == 0 && result.local_ranges == NULL && result.local_range_count == 0;
            }
            check_debug_test_expect(tally, valid, "shared-object publication succeeds or rolls both arena marks back without partial metadata",
                                    failure == 3 ? "an out-of-bounds original block interval is rejected independently of legal record motion" : "bounded output and scratch arenas");
        }
    }
}

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
    // Run the same independent tables through the new general binding proof too. This exercises its sparse CFG
    // states and incremental publisher, rather than comparing two implementations only against each other.
    const bool bounded = function->local_count <= 32 && code.count <= 32 && (uint64_t)function->local_count * code.count <= 128;
    check_debug_test_expect(tally, bounded, "the sparse parameter fixture fits its bounded columns", description);
    if (bounded)
    {
        IrDebugBinding bindings[32] = { 0 };
        uint8_t types[32] = { 0 };
        uint8_t flags[32] = { 0 };
        uint32_t origins[32] = { 0 };
        uint32_t entries[32];
        uint8_t safe[32] = { 0 };
        uint32_t cells[128];
        memset(entries, 0xff, sizeof(entries));
        for (uint32_t local = 0; local < function->parameter_count; local += 1)
        {
            types[local] = ir_typed_types[function->local_classes[local]];
            flags[local] = BUSTER_IR_DEBUG_BIND_ENTRY;
            bindings[local] = (IrDebugBinding){ .local = local, .entry = BUSTER_IR_NONE, .value = local, .anchor = local,
                                              .kind = IR_DEBUG_BIND_VALUE, .bytes = values[local].bytes, .type = types[local] };
        }
        for (uint32_t r = 0; r < code.count; r += 1)
        {
            origins[r] = tier1_origin(TIER1_ORIGIN_NODE, 0);
        }
        for (uint32_t k = 0; k < function->local_count * code.count; k += 1)
        {
            cells[k] = expected[k] == UINT8_MAX ? BUSTER_IR_NONE : expected[k];
        }
        IrDebugInfo source = { .bindings = bindings, .binding_count = function->parameter_count, .binding_flags = flags };
        const IrFunction ir = { .type = types, .instruction_count = function->parameter_count, .debug = &source };
        const Tier1EmitResult sparse = { .code = code, .origins = origins };
        check_debug_tier1_values_binding_case(tally, function, &ir, &sparse, order, entries, safe, code.count, cells, description);
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
    check_debug_tier1_values_objects(tally);
    check_debug_tier1_values_bindings(tally);
}

