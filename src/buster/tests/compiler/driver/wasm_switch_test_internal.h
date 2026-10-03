#pragma once

// Private compiler_driver_tests fixture, included after the bounded Node seam.
// compiler_driver_test_wasm_switch_program commits typed ARGUMENT/SWITCH rows;
// compiler_driver_test_wasm_switch_images checks the original artifact in Node.
#if BUSTER_INCLUDE_TESTS
enum
{
    COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT = 2,
    COMPILER_DRIVER_WASM_SWITCH_VARIANT_COUNT = 4,
    COMPILER_DRIVER_WASM_SWITCH_KEY_COUNT = 4,
    COMPILER_DRIVER_WASM_SWITCH_TARGET_COUNT = 5,
    COMPILER_DRIVER_WASM_SWITCH_LITERAL_COUNT = 13,
};

typedef struct CompilerDriverWasmSwitchSnapshot CompilerDriverWasmSwitchSnapshot;
struct CompilerDriverWasmSwitchSnapshot
{
    IrInstruction* instruction;
    u64* immediates;
    IrBlockId* targets;
    IrValueId selector;
    u64 keys[COMPILER_DRIVER_WASM_SWITCH_KEY_COUNT];
    IrBlockId destinations[COMPILER_DRIVER_WASM_SWITCH_TARGET_COUNT];
    u32 immediate_count;
    u32 target_count;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_wasm_switch_append(Arena* arena, IrFunction* function, u32 block, IrInstruction row)
{
    u32 next = function->instruction_count;
    IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
    IrInstructionId id = ir_block_append_instruction(arena, function, (IrBlockId){.value = block}, row, (IrSourceRange){0}, &refusal);
    return refusal == IR_COMMIT_ACCEPTED && id.value == next;
}

// Raw keys deliberately retain extension bits. Each function takes the typed
// argument directly, so C integer promotions and prior casts cannot clean it.
BUSTER_GLOBAL_LOCAL IrProgram compiler_driver_test_wasm_switch_program(Arena* arena, Target target, u16 const* widths,
                                                                      u32 width_count, bool* committed_out)
{
    String8 variants[] = {S8("alias"), S8("low"), S8("full"), S8("multi")};
    u32 function_count = width_count * COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT * COMPILER_DRIVER_WASM_SWITCH_VARIANT_COUNT;
    IrProgram program = ir_program_initialize(arena, 1, 2 + width_count * COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT * 2, function_count, 0);
    program.data_layout = target_data_layout(target);
    IrModule* module = program.modules;
    module->name = S8("wasm-switch-images");
    IrTypeId void_type = ir_program_add_type(&program, (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}});
    IrTypeId result_type = ir_program_add_type(&program,
        (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32, .is_signed = true,
                 .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    bool committed = true;
    for (u32 width_index = 0; width_index < width_count; width_index += 1)
    {
        u16 width = widths[width_index];
        u32 size = (u32)width / 8;
        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        for (u32 sign = 0; sign < COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT; sign += 1)
        {
            IrTypeId integer_type = ir_program_add_type(&program,
                (IrType){.kind = IR_TYPE_INTEGER, .bit_width = width, .is_signed = sign != 0,
                         .layout = {.size = size, .alignment = size, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
            IrTypeId* parameters = arena_allocate(arena, IrTypeId, 1);
            parameters[0] = integer_type;
            IrTypeId signature = ir_program_add_type(&program,
                (IrType){.kind = IR_TYPE_FUNCTION, .return_type = result_type, .parameter_types = parameters, .parameter_count = 1,
                         .calling_convention = IR_CALLING_CONVENTION_C,
                         .layout = {.size = program.data_layout.pointer.size, .alignment = program.data_layout.pointer.alignment,
                                    .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
            for (u32 variant = 0; variant < COMPILER_DRIVER_WASM_SWITCH_VARIANT_COUNT; variant += 1)
            {
                String8 name = string_format(arena, S8("switch_{S8}{u32}_{S8}"), sign ? S8("s") : S8("u"), (u32)width, variants[variant]);
                IrSymbolId symbol = ir_program_add_symbol(&program,
                    (IrSymbol){.name = name, .link_name = name, .type = signature, .kind = IR_SYMBOL_FUNCTION,
                               .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true});
                IrFunction* function = ir_module_add_function(arena, module,
                    (IrFunction){.name = name, .symbol = symbol, .canonical_type = signature, .entry = {.value = 0},
                                 .state = IR_FUNCTION_LOWERED});
                u32 block_count = variant == 3 ? 5 : 3;
                for (u32 block = 0; block < block_count; block += 1)
                {
                    ir_function_add_block(arena, function,
                        (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID, .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
                }
                IrValueId argument = ir_function_add_value(arena, function,
                    (IrValue){.canonical_type = integer_type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
                IrInstruction argument_row = compiler_driver_test_wasm_canonical_row(IR_OPCODE_ARGUMENT, integer_type, argument);
                argument_row.immediates = arena_allocate(arena, u64, 1);
                argument_row.immediates[0] = 0;
                argument_row.immediate_count = 1;
                committed &= compiler_driver_test_wasm_switch_append(arena, function, 0, argument_row);
                IrInstruction switched = compiler_driver_test_wasm_canonical_row(IR_OPCODE_SWITCH, void_type, IR_VALUE_ID_INVALID);
                switched.operands = arena_allocate(arena, IrValueId, 1);
                switched.operands[0] = argument;
                switched.operand_count = 1;
                switched.immediate_count = variant == 3 ? COMPILER_DRIVER_WASM_SWITCH_KEY_COUNT : 1;
                switched.target_count = (u16)(switched.immediate_count + 1);
                switched.immediates = arena_allocate(arena, u64, switched.immediate_count);
                switched.targets = arena_allocate(arena, IrBlockId, switched.target_count);
                switched.immediates[0] = variant == 0 ? (width == 64 ? 7 : (UINT64_C(1) << width) + 7) :
                                        variant == 1 ? mask : variant == 2 ? UINT64_MAX : 7;
                switched.targets[0] = (IrBlockId){.value = 1};
                if (variant == 3)
                {
                    switched.immediates[1] = mask;
                    switched.immediates[2] = width == 64 ? UINT64_C(0x100000007) : UINT64_C(1) << (width - 1);
                    switched.immediates[3] = 8;
                    switched.targets[1] = (IrBlockId){.value = 2};
                    switched.targets[2] = (IrBlockId){.value = 3};
                    switched.targets[3] = (IrBlockId){.value = 1};
                }
                switched.targets[switched.immediate_count] = (IrBlockId){.value = block_count - 1};
                committed &= compiler_driver_test_wasm_switch_append(arena, function, 0, switched);
                for (u32 block = 1; block < block_count; block += 1)
                {
                    IrValueId selected = ir_function_add_value(arena, function,
                        (IrValue){.canonical_type = result_type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
                    IrInstruction literal = compiler_driver_test_wasm_canonical_row(IR_OPCODE_CONSTANT_INTEGER, result_type, selected);
                    literal.immediates = arena_allocate(arena, u64, 1);
                    literal.immediates[0] = block == block_count - 1 ? 41 : block == 1 ? 17 : block == 2 ? 23 : 29;
                    literal.immediate_count = 1;
                    committed &= compiler_driver_test_wasm_switch_append(arena, function, block, literal);
                    IrInstruction returned = compiler_driver_test_wasm_canonical_row(IR_OPCODE_RETURN, void_type, IR_VALUE_ID_INVALID);
                    returned.operands = arena_allocate(arena, IrValueId, 1);
                    returned.operands[0] = selected;
                    returned.operand_count = 1;
                    committed &= compiler_driver_test_wasm_switch_append(arena, function, block, returned);
                }
            }
        }
    }
    *committed_out = committed;
    return program;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_test_wasm_switch_unchanged(CompilerDriverWasmSwitchSnapshot const* snapshots, u32 count)
{
    bool unchanged = true;
    for (u32 index = 0; index < count; index += 1)
    {
        CompilerDriverWasmSwitchSnapshot const* snapshot = snapshots + index;
        IrInstruction* instruction = snapshot->instruction;
        unchanged &= instruction && instruction->opcode == IR_OPCODE_SWITCH && instruction->operand_count == 1 &&
                     instruction->operands[0].value == snapshot->selector.value &&
                     instruction->immediates == snapshot->immediates && instruction->targets == snapshot->targets &&
                     instruction->immediate_count == snapshot->immediate_count && instruction->target_count == snapshot->target_count;
        for (u32 key = 0; key < snapshot->immediate_count; key += 1)
        {
            unchanged &= snapshot->immediates[key] == snapshot->keys[key];
        }
        for (u32 target = 0; target < snapshot->target_count; target += 1)
        {
            unchanged &= snapshot->targets[target].value == snapshot->destinations[target].value;
        }
    }
    return unchanged;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_wasm_switch_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u16 widths[] = {8, 16, 32, 64};
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_WASM32, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_WASI},
        {.cpu_arch = CPU_ARCH_WASM64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_FREESTANDING},
    };
    String8 script = S8(
        "'use strict';\n"
        "const fs = require('node:fs');\n"
        "const crypto = require('node:crypto');\n"
        "const assert = require('node:assert/strict');\n"
        "const bytes = fs.readFileSync(process.argv[2]);\n"
        "const digest = crypto.createHash('sha256').update(bytes).digest('hex');\n"
        "assert.equal(digest, process.argv[3], 'original compiler module consumed');\n"
        "assert(WebAssembly.validate(bytes), 'valid direct canonical SWITCH module');\n"
        "const guest = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;\n"
        "const variants = ['alias', 'low', 'full', 'multi'];\n"
        "let checks = 0;\n"
        "let failures = 0;\n"
        "const firstFailure = new Map();\n"
        "function oracle(width, variant, raw) {\n"
        "    const mask = (1n << BigInt(width)) - 1n;\n"
        "    const image = BigInt.asUintN(width, raw);\n"
        "    const high = width === 64 ? 0x100000007n : 1n << BigInt(width - 1);\n"
        "    const cases = variant === 'alias' ? [[width === 64 ? 7n : mask + 8n, 17]] :\n"
        "                  variant === 'low' ? [[mask, 17]] :\n"
        "                  variant === 'full' ? [[0xffffffffffffffffn, 17]] :\n"
        "                  [[7n, 17], [mask, 23], [high, 29], [8n, 17]];\n"
        "    for (const [key, result] of cases) if (BigInt.asUintN(width, key) === image) return result;\n"
        "    return 41;\n"
        "}\n"
        "function check(width, signed, variant, raw, expected) {\n"
        "    const name = 'switch_' + (signed ? 's' : 'u') + width + '_' + variant;\n"
        "    assert.equal(typeof guest[name], 'function', 'export ' + name);\n"
        "    const input = width <= 32 ? Number(BigInt.asIntN(32, raw)) : BigInt.asIntN(64, raw);\n"
        "    const actual = guest[name](input);\n"
        "    checks += 1;\n"
        "    if (actual !== expected) {\n"
        "        failures += 1;\n"
        "        if (!firstFailure.has(name)) firstFailure.set(name, name + '(' + raw + '): expected=' + expected + ' actual=' + actual);\n"
        "    }\n"
        "}\n"
        "const literals = [\n"
        "    [8, false, 'alias', 7n, 17], [8, false, 'alias', 263n, 17],\n"
        "    [8, true, 'alias', -249n, 17], [8, true, 'low', -1n, 17],\n"
        "    [8, true, 'low', 255n, 17], [16, false, 'full', 65535n, 17],\n"
        "    [16, false, 'full', 0x1234ffffn, 17], [32, false, 'multi', 0x100000007n, 17],\n"
        "    [64, false, 'multi', 0x100000007n, 29], [64, true, 'multi', -1n, 23],\n"
        "    [64, false, 'multi', 7n, 17], [64, false, 'multi', 8n, 17], [8, false, 'multi', 0n, 41],\n"
        "];\n"
        "for (const [width, signed, variant, raw, expected] of literals) {\n"
        "    assert.equal(oracle(width, variant, raw), expected, 'independent literal oracle');\n"
        "    check(width, signed, variant, raw, expected);\n"
        "}\n"
        "for (const width of [8, 16, 32, 64]) {\n"
        "    const carrier = width <= 32 ? 32 : 64;\n"
        "    const mask = (1n << BigInt(width)) - 1n;\n"
        "    const top = 1n << BigInt(width - 1);\n"
        "    const clean = [];\n"
        "    if (width === 8) for (let value = 0n; value <= mask; value += 1n) clean.push(value);\n"
        "    else clean.push(0n, 1n, 7n, 8n, mask, mask - 1n, top, top - 1n, 0x100000007n, -1n);\n"
        "    const values = [];\n"
        "    for (const raw of clean) {\n"
        "        const bits = BigInt.asUintN(width, raw);\n"
        "        values.push(bits, BigInt.asIntN(width, bits));\n"
        "        if (width < carrier) {\n"
        "            values.push(bits | (1n << BigInt(width)), bits | (((1n << BigInt(carrier)) - 1n) ^ mask));\n"
        "        }\n"
        "    }\n"
        "    for (const signed of [false, true]) for (const variant of variants) for (const raw of values) {\n"
        "        check(width, signed, variant, raw, oracle(width, variant, raw));\n"
        "    }\n"
        "}\n"
        "assert.equal(checks, Number(process.argv[4]), 'complete fixed matrix');\n"
        "console.log('WASM_SWITCH_IMAGES sha256=' + digest + ' pointer_bytes=' + process.argv[5] + ' checks=' + checks + ' failures=' + failures);\n"
        "assert.equal(failures, 0, Array.from(firstFailure.values()).join('\\n'));\n"
        "console.log(checks + '/' + checks + ' Wasm SWITCH image engine checks passed');\n");
    u32 expected_checks = COMPILER_DRIVER_WASM_SWITCH_LITERAL_COUNT;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(widths); index += 1)
    {
        u32 width = widths[index];
        u32 carrier = width <= 32 ? 32 : 64;
        u32 samples = (width == 8 ? 256 : 10) * (width < carrier ? 4 : 2);
        expected_checks += samples * COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT * COMPILER_DRIVER_WASM_SWITCH_VARIANT_COUNT;
    }
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        bool committed = false;
        IrProgram program = compiler_driver_test_wasm_switch_program(arena, targets[target_index], widths, BUSTER_ARRAY_LENGTH(widths), &committed);
        bool ready = BUSTER_REQUIRE(arguments, committed && program.modules->function_count ==
                                               BUSTER_ARRAY_LENGTH(widths) * COMPILER_DRIVER_WASM_SWITCH_SIGN_COUNT * COMPILER_DRIVER_WASM_SWITCH_VARIANT_COUNT);
        if (ready)
        {
            IrValidationResult validation = ir_prepare_canonical_module(&program, program.modules, false);
            ready = BUSTER_REQUIRE(arguments, validation.error == IR_VALIDATION_NONE);
        }
        if (ready)
        {
            u32 snapshot_count = program.modules->function_count;
            CompilerDriverWasmSwitchSnapshot* snapshots = arena_allocate(arena, CompilerDriverWasmSwitchSnapshot, snapshot_count);
            memset(snapshots, 0, sizeof(*snapshots) * snapshot_count);
            for (u32 index = 0; index < snapshot_count; index += 1)
            {
                IrFunction* function = program.modules->functions + index;
                u32 found = 0;
                for (u32 row = 0; row < function->instruction_count; row += 1)
                {
                    IrInstruction* instruction = function->instructions + row;
                    if (instruction->opcode == IR_OPCODE_SWITCH)
                    {
                        found += 1;
                        snapshots[index] = (CompilerDriverWasmSwitchSnapshot){.instruction = instruction, .immediates = instruction->immediates,
                            .targets = instruction->targets, .selector = instruction->operands[0],
                            .immediate_count = instruction->immediate_count, .target_count = instruction->target_count};
                        memcpy(snapshots[index].keys, instruction->immediates, instruction->immediate_count * sizeof(u64));
                        memcpy(snapshots[index].destinations, instruction->targets, instruction->target_count * sizeof(IrBlockId));
                    }
                }
                ready &= BUSTER_REQUIRE(arguments, found == 1);
            }
            if (ready)
            {
                WasmOptions options = WASM64_OPTIONS_DEFAULT;
                options.pointer_size = target_index == 0 ? 4 : 8;
                WasmArtifact first = wasm_emit(arena, &program, program.modules, 1, options);
                WasmArtifact second = wasm_emit(arena, &program, program.modules, 1, options);
                BUSTER_TEST_RAW(arguments, first.success && second.success, first.error.message);
                BUSTER_TEST(arguments, compiler_driver_test_wasm_switch_unchanged(snapshots, snapshot_count));
                if (first.success && second.success)
                {
                    BUSTER_TEST(arguments, first.bytes.length && first.bytes.length == second.bytes.length &&
                                           memory_compare(first.bytes.pointer, second.bytes.pointer, first.bytes.length));
                    BUSTER_TEST(arguments, first.stats.memory64 == (options.pointer_size == 8) && first.stats.defined_function_count == snapshot_count);
                    String8 output = buster_test_temporary_path(arena, S8("buster-wasm-switch-images"), S8(".wasm"));
                    String8 script_path = buster_test_temporary_path(arena, S8("buster-wasm-switch-images"), S8(".cjs"));
                    bool written = file_write(output, first.bytes) && file_write(script_path, BUSTER_SLICE_TO_BYTE_SLICE(script));
                    BUSTER_TEST(arguments, written);
                    ByteSlice emitted = file_read(arena, output, (FileReadOptions){0});
                    bool unchanged = emitted.length == first.bytes.length && emitted.pointer &&
                                     memory_compare(emitted.pointer, first.bytes.pointer, emitted.length);
                    BUSTER_TEST(arguments, unchanged);
                    String8 node = executable_resolve_in_path(arena, S8("node"));
                    if (written && unchanged && node.length)
                    {
                        Sha256 hash;
                        char8 hash_bytes[SHA256_HEX_CAPACITY];
                        sha256_init(&hash);
                        sha256_add(&hash, first.bytes.pointer, first.bytes.length);
                        sha256_finish_hex(&hash, hash_bytes);
                        String8 command[] = {node, script_path, output, (String8){hash_bytes, 64},
                            string_format(arena, S8("{u32}"), expected_checks), string_format(arena, S8("{u32}"), (u32)options.pointer_size)};
                        CompilerDriverWasmNodeRun run = compiler_driver_test_wasm_node_run(
                            arguments, arena, S8("switch-images"), string_format(arena, S8("pointer-{u32}"), (u32)options.pointer_size),
                            (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
                            string_format(arena, S8("{u32}/{u32} Wasm SWITCH image engine checks passed"), expected_checks, expected_checks),
                            compiler_driver_test_wasm_node_deadline_microseconds());
                        BUSTER_TEST(arguments, compiler_driver_test_wasm_node_succeeded(run));
                        ByteSlice consumed = file_read(arena, output, (FileReadOptions){0});
                        BUSTER_TEST(arguments, consumed.length == first.bytes.length && consumed.pointer &&
                                               memory_compare(consumed.pointer, first.bytes.pointer, consumed.length));
                        BUSTER_TEST(arguments, compiler_driver_test_wasm_switch_unchanged(snapshots, snapshot_count));
                    }
                    else if (!node.length)
                    {
                        arguments->show(arguments, S8("Wasm SWITCH image engine execution skipped: Node is not installed\n"));
                    }
                }
            }
        }
        scratch_end(temporary);
    }
    return result;
}
#endif
