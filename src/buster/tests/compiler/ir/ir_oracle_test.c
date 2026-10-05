#include <buster/tests/compiler/ir/ir_oracle_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/string.h>
#include <buster/tests/compiler/ir/ir_oracle_internal.h>

// ir_oracle_fixture builds canonical rows without the C frontend. The C
// companion exercises lowering separately. ir_oracle_native_tests is a private
// re-exec payload; ir_oracle_tests contains the independent evaluator, scalar
// witnesses, rejection controls, and bounded parent comparison.
enum { IR_ORACLE_FIXTURES = 6, IR_ORACLE_INPUTS = 6 };
BUSTER_GLOBAL_LOCAL u64 const ir_oracle_inputs[IR_ORACLE_INPUTS][2] = {
    {0, 0}, {1, 2}, {255, 1}, {65535, 32768}, {UINT64_MAX, 7}, {UINT64_C(0x8000000080000080), UINT64_MAX},
};

BUSTER_GLOBAL_LOCAL IrFunction* ir_oracle_named(IrModule* module, String8 name)
{
    IrFunction* found = 0;
    for (u32 i = 0; i < module->function_count; i += 1)
        if (string_equal(module->functions[i].name, name)) found = module->functions + i;
    return found;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_oracle_emit(Arena* arena, IrFunction* function, u32 block, IrInstruction row, IrValueCategory category)
{
    row.next = IR_INSTRUCTION_ID_INVALID;
    if (row.opcode == IR_OPCODE_RETURN || row.opcode == IR_OPCODE_STORE || row.opcode == IR_OPCODE_BRANCH_IF) row.result = IR_VALUE_ID_INVALID;
    else row.result = ir_function_add_value(arena, function,
        (IrValue){.canonical_type = row.canonical_type, .definition = IR_INSTRUCTION_ID_INVALID, .category = (u8)category});
    IrInstructionId id = ir_block_append_instruction(arena, function, (IrBlockId){.value = block}, row, (IrSourceRange){0}, 0);
    if (id.value == UINT32_MAX) row.result = IR_VALUE_ID_INVALID;
    return row.result;
}

BUSTER_GLOBAL_LOCAL IrValueId* ir_oracle_operands(Arena* arena, IrValueId a, IrValueId b, IrValueId c, u32 count)
{
    IrValueId* operands = arena_allocate(arena, IrValueId, count);
    IrValueId values[] = {a, b, c};
    for (u32 i = 0; i < count; i += 1) operands[i] = values[i];
    return operands;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_oracle_argument(Arena* arena, IrFunction* function, IrTypeId type, u32 index)
{
    u64* immediate = arena_allocate(arena, u64, 1);
    *immediate = index;
    return ir_oracle_emit(arena, function, 0, (IrInstruction){.opcode = IR_OPCODE_ARGUMENT, .canonical_type = type,
                             .immediates = immediate, .immediate_count = 1}, IR_VALUE_VALUE);
}

BUSTER_GLOBAL_LOCAL void ir_oracle_return(Arena* arena, IrFunction* function, u32 block, IrTypeId void_type, IrValueId value)
{
    (void)ir_oracle_emit(arena, function, block, (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = void_type,
        .operands = ir_oracle_operands(arena, value, value, value, 1), .operand_count = 1}, IR_VALUE_VALUE);
}

BUSTER_GLOBAL_LOCAL IrProgram* ir_oracle_fixture(Arena* arena, u32 fixture, u32 mutation)
{
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    IrProgram* program = 0;
    if (fixture >= 4)
    {
        String8 source = fixture == 4 ?
            S8("static unsigned long long sink; unsigned long long helper(unsigned long long a,unsigned long long b){unsigned long long x=a;sink=(x^b)+7;return sink;}"
               "unsigned long long probe(unsigned long long a,unsigned long long b){if(a>b)return helper(a,b);else return helper(b,a)+3;}"
               "unsigned long long observe(void){return sink;}") : fixture == 5 ?
            S8("static unsigned long long sink; unsigned long long probe(unsigned long long a,unsigned long long b){unsigned long long x=a,y=b;"
               "for(unsigned long long i=0;i<3;i++){unsigned long long t=x;x=y;y=t;}sink=y;return x;}"
               "unsigned long long observe(void){return sink;}") :
            S8("static unsigned long long sink; unsigned long long probe(unsigned long long a,unsigned long long b){"
               "(void)a;(void)b;for(;;){}return 0;}unsigned long long observe(void){return sink;}");
        CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
        CParseResult parse = c_parse(arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arena, S8("ir-oracle.c"), tokens, parse, target);
        if (!tokens.error_count && !parse.diagnostic_count && !lowered.diagnostic_count) program = lowered.program;
    }
    else
    {
        program = arena_allocate(arena, IrProgram, 1);
        *program = ir_program_initialize(arena, 1, 16, 8, 0);
        program->data_layout = target_data_layout(target);
        u32 widths[] = {8, 16, 32, 64};
        u32 width = widths[fixture];
        IrTypeId void_type = ir_program_add_type(program, (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}});
        IrTypeId boolean = ir_program_add_type(program, (IrType){.kind = IR_TYPE_BOOLEAN, .bit_width = 1,
            .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
        IrTypeId narrow = ir_program_add_type(program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = width,
            .layout = {.size = width / 8, .alignment = width / 8, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
        IrTypeId integer = ir_program_add_type(program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 64,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
        IrTypeId pointer = ir_program_add_type(program, (IrType){.kind = IR_TYPE_POINTER, .element_type = narrow,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
        IrTypeId* parameters = arena_allocate(arena, IrTypeId, 2);
        parameters[0] = integer;
        parameters[1] = integer;
        IrTypeId callable = ir_program_add_type(program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = integer,
            .parameter_types = parameters, .parameter_count = 2, .calling_convention = IR_CALLING_CONVENTION_C,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
        IrTypeId observer = ir_program_add_type(program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = integer,
            .calling_convention = IR_CALLING_CONVENTION_C,
            .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
        IrSymbolId sink = ir_program_add_symbol(program, (IrSymbol){.name = S8("sink"), .link_name = S8("sink"), .type = integer,
            .kind = IR_SYMBOL_DATA, .linkage = IR_LINKAGE_INTERNAL, .is_definition = true});
        (void)ir_module_add_global(arena, program->modules, (IrGlobal){.symbol = sink, .type = integer, .alignment = 8,
            .initializer_kind = IR_GLOBAL_INITIALIZER_ZERO});
        String8 names[] = {S8("helper"), S8("probe"), S8("observe")};
        for (u32 f = 0; f < 3; f += 1)
        {
            IrTypeId type = f == 2 ? observer : callable;
            IrSymbolId symbol = ir_program_add_symbol(program, (IrSymbol){.name = names[f], .link_name = names[f], .type = type,
                .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_EXTERNAL, .is_definition = true});
            (void)ir_module_add_function(arena, program->modules, (IrFunction){.name = names[f], .symbol = symbol, .canonical_type = type,
                .state = IR_FUNCTION_LOWERED, .entry = {.value = 0}});
        }
        IrFunction* helper = program->modules->functions;
        IrFunction* probe = helper + 1;
        IrFunction* observe = helper + 2;
        for (u32 f = 0; f < 3; f += 1)
        {
            IrFunction* function = helper + f;
            u32 blocks = f == 1 ? 3 : 1;
            for (u32 b = 0; b < blocks; b += 1)
                (void)ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                   .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
        }
        IrValueId a = ir_oracle_argument(arena, helper, integer, 0);
        IrValueId b = ir_oracle_argument(arena, helper, integer, 1);
        IrValueId x = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_CAST, .canonical_type = narrow,
            .conversion_operation = width == 64 ? IR_CONVERSION_INTEGER_REINTERPRET : IR_CONVERSION_INTEGER_TRUNCATE,
            .operands = ir_oracle_operands(arena, a, a, a, 1), .operand_count = 1}, IR_VALUE_VALUE);
        IrValueId y = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_CAST, .canonical_type = narrow,
            .conversion_operation = width == 64 ? IR_CONVERSION_INTEGER_REINTERPRET : IR_CONVERSION_INTEGER_TRUNCATE,
            .operands = ir_oracle_operands(arena, b, b, b, 1), .operand_count = 1}, IR_VALUE_VALUE);
        IrValueId local = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_LOCAL, .canonical_type = narrow,
            .canonical_local = {.value = 0}}, IR_VALUE_PLACE);
        helper->local_count = 1;
        helper->local_places = arena_allocate(arena, IrValueId, 1);
        helper->local_places[0] = local;
        helper->local_uses_memory = arena_allocate(arena, bool, 1);
        helper->local_uses_memory[0] = true;
        (void)ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_STORE, .canonical_type = void_type,
            .operands = ir_oracle_operands(arena, local, x, x, 2), .operand_count = 2}, IR_VALUE_VALUE);
        IrValueId address = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_ADDRESS_OF, .canonical_type = pointer,
            .operands = ir_oracle_operands(arena, local, local, local, 1), .operand_count = 1}, IR_VALUE_VALUE);
        IrValueId dereferenced = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_DEREFERENCE, .canonical_type = narrow,
            .operands = ir_oracle_operands(arena, address, address, address, 1), .operand_count = 1}, IR_VALUE_PLACE);
        IrValueId loaded = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_LOAD, .canonical_type = narrow,
            .operands = ir_oracle_operands(arena, dereferenced, dereferenced, dereferenced, 1), .operand_count = 1}, IR_VALUE_VALUE);
        IrValueId sum = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_BINARY, .canonical_type = narrow,
            .binary_operation = IR_BINARY_INTEGER_ADD, .operands = ir_oracle_operands(arena, loaded, y, y, 2), .operand_count = 2}, IR_VALUE_VALUE);
        IrValueId wide = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_CAST, .canonical_type = integer,
            .conversion_operation = width == 64 ? IR_CONVERSION_INTEGER_REINTERPRET : IR_CONVERSION_INTEGER_ZERO_EXTEND,
            .operands = ir_oracle_operands(arena, sum, sum, sum, 1), .operand_count = 1}, IR_VALUE_VALUE);
        IrValueId global = ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_GLOBAL, .canonical_type = integer, .symbol = sink}, IR_VALUE_PLACE);
        (void)ir_oracle_emit(arena, helper, 0, (IrInstruction){.opcode = IR_OPCODE_STORE, .canonical_type = void_type,
            .operands = ir_oracle_operands(arena, global, wide, wide, 2), .operand_count = 2}, IR_VALUE_VALUE);
        ir_oracle_return(arena, helper, 0, void_type, wide);
        a = ir_oracle_argument(arena, probe, integer, 0);
        b = ir_oracle_argument(arena, probe, integer, 1);
        IrValueId condition = ir_oracle_emit(arena, probe, 0, (IrInstruction){.opcode = IR_OPCODE_BINARY, .canonical_type = boolean,
            .binary_operation = IR_BINARY_UNSIGNED_GREATER, .operands = ir_oracle_operands(arena, a, b, b, 2), .operand_count = 2}, IR_VALUE_VALUE);
        IrBlockId* targets = arena_allocate(arena, IrBlockId, 2);
        targets[0].value = 1;
        targets[1].value = 2;
        (void)ir_oracle_emit(arena, probe, 0, (IrInstruction){.opcode = IR_OPCODE_BRANCH_IF, .canonical_type = void_type,
            .targets = targets, .target_count = 2, .operands = ir_oracle_operands(arena, condition, condition, condition, 1),
            .operand_count = 1}, IR_VALUE_VALUE);
        for (u32 block = 1; block < 3; block += 1)
        {
            IrPredecessor* predecessor = arena_allocate(arena, IrPredecessor, 1);
            *predecessor = (IrPredecessor){.block = {.value = 0}};
            probe->blocks[block].first_predecessor = predecessor;
            probe->blocks[block].last_predecessor = predecessor;
            probe->blocks[block].predecessor_count = 1;
            IrValueId callee = ir_oracle_emit(arena, probe, block, (IrInstruction){.opcode = IR_OPCODE_FUNCTION,
                .canonical_type = callable, .symbol = helper->symbol}, IR_VALUE_VALUE);
            IrValueId call = ir_oracle_emit(arena, probe, block, (IrInstruction){.opcode = IR_OPCODE_CALL, .canonical_type = integer,
                .symbol = helper->symbol, .operands = ir_oracle_operands(arena, callee, a, b, 3), .operand_count = 3}, IR_VALUE_VALUE);
            u64* immediate = arena_allocate(arena, u64, 1);
            *immediate = block;
            IrValueId constant = ir_oracle_emit(arena, probe, block, (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER,
                .canonical_type = integer, .immediates = immediate, .immediate_count = 1}, IR_VALUE_VALUE);
            IrValueId returned = ir_oracle_emit(arena, probe, block, (IrInstruction){.opcode = IR_OPCODE_BINARY, .canonical_type = integer,
                .binary_operation = IR_BINARY_INTEGER_ADD, .operands = ir_oracle_operands(arena, call, constant, constant, 2), .operand_count = 2}, IR_VALUE_VALUE);
            ir_oracle_return(arena, probe, block, void_type, returned);
        }
        global = ir_oracle_emit(arena, observe, 0, (IrInstruction){.opcode = IR_OPCODE_GLOBAL, .canonical_type = integer, .symbol = sink}, IR_VALUE_PLACE);
        loaded = ir_oracle_emit(arena, observe, 0, (IrInstruction){.opcode = IR_OPCODE_LOAD, .canonical_type = integer,
            .operands = ir_oracle_operands(arena, global, global, global, 1), .operand_count = 1}, IR_VALUE_VALUE);
        ir_oracle_return(arena, observe, 0, void_type, loaded);
    }
    if (program && mutation)
    {
        IrFunction* function = ir_oracle_named(program->modules, mutation == 2 ? S8("probe") : S8("helper"));
        bool changed = false;
        for (u32 i = 0; function && i < function->instruction_count && !changed; i += 1)
        {
            IrInstruction* row = function->instructions + i;
            if (mutation == 1 && row->opcode == IR_OPCODE_BINARY && row->binary_operation == IR_BINARY_INTEGER_ADD)
            {
                row->binary_operation = IR_BINARY_INTEGER_SUBTRACT;
                changed = true;
            }
            else if (mutation == 2 && row->opcode == IR_OPCODE_BRANCH_IF)
            {
                IrBlockId temporary = row->targets[0];
                row->targets[0] = row->targets[1];
                row->targets[1] = temporary;
                changed = true;
            }
            else if (mutation == 3 && row->opcode == IR_OPCODE_STORE &&
                     function->instructions[function->values[row->operands[0].value].definition.value].opcode == IR_OPCODE_GLOBAL)
            {
                // Correctly typed but wrong value: preserve the return, corrupt
                // only the declared observable side effect.
                row->operands[1] = (IrValueId){.value = 0};
                changed = true;
            }
        }
        if (!changed) program = 0;
    }
    return program;
}

typedef u64 IrOracleNative2(u64 a, u64 b);
typedef u64 IrOracleNative0(void);

UnitTestResult ir_oracle_native_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64 && !BUSTER_ANDROID && !BUSTER_SANITIZE
    String8 selection = os_get_environment_variable(S8("BUSTER_IR_ORACLE_CHILD"));
    if (selection.length)
    {
        u32 fixture = selection.pointer[0] - (u8)'0';
        u32 mutation = selection.length > 1 ? selection.pointer[1] - (u8)'0' : 0;
        u32 mode = selection.length > 2 ? selection.pointer[2] - (u8)'0' : 0;
        IrProgram* program = ir_oracle_fixture(arguments->arena, fixture, mutation);
        if (BUSTER_REQUIRE(arguments, program && fixture <= IR_ORACLE_FIXTURES && mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT))
        {
            IrModule* module = program->modules;
            IrFunction* probe = ir_oracle_named(module, S8("probe"));
            IrFunction* observe = ir_oracle_named(module, S8("observe"));
            IrValidationResult validation = ir_prepare_canonical_module(program, module, false);
            if (BUSTER_REQUIRE(arguments, probe && observe && validation.error == IR_VALIDATION_NONE))
            {
                CodegenModule code = codegen_generate_canonical_module(arguments->arena, program, module, target_native,
                    (CodegenModuleOptions){.register_allocator = (u8)mode, .verify_invariants = true, .record_fallbacks = true});
                arguments->show(arguments, S8("IR_ORACLE_CODEGEN_V1 fixture={u32} mutation={u32} allocator={u32} fallback_functions={u32}\n"),
                                fixture, mutation, mode, code.statistics.fallback_function_count);
                if (BUSTER_REQUIRE(arguments, code.error == CODEGEN_ERROR_NONE))
                {
                    ObjectFile object = object_from_canonical_codegen_module(arguments->arena, program, &code, target_native);
                    ObjectExecutable executable = object_link_executable(&object);
                    if (BUSTER_REQUIRE(arguments, object.error == OBJECT_ERROR_NONE && executable.error == OBJECT_ERROR_NONE && executable.address))
                    {
                        IrOracleNative2* native = 0;
                        IrOracleNative0* observer = 0;
                        for (u32 i = 0; i < code.function_count; i += 1)
                        {
                            void* entry = (u8*)executable.address + code.functions[i].code_offset;
                            if (code.functions[i].symbol.value == probe->symbol.value) memcpy(&native, &entry, sizeof(native));
                            if (code.functions[i].symbol.value == observe->symbol.value) memcpy(&observer, &entry, sizeof(observer));
                        }
                        if (BUSTER_REQUIRE(arguments, native && observer))
                        {
                            arguments->show(arguments, S8("IR_ORACLE_BEGIN_V1\n"));
                            for (u32 i = 0; i < IR_ORACLE_INPUTS; i += 1)
                            {
                                u64 returned = native(ir_oracle_inputs[i][0], ir_oracle_inputs[i][1]);
                                u64 memory = observer();
                                arguments->show(arguments, S8("IR_ORACLE_V1 fixture={u32} input={u32} return={u64} memory={u64}\n"), fixture, i, returned, memory);
                            }
                            arguments->show(arguments, S8("IR_ORACLE_END_V1\n"));
                        }
                    }
                    object_release_executable(executable);
                }
            }
        }
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_oracle_scalar_controls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrOracleRun* run = arena_allocate(arguments->arena, IrOracleRun, 1);
    struct { IrBinaryOperation operation; u64 a; u64 b; u64 expected; } cases[] = {
        {IR_BINARY_INTEGER_ADD, 255, 1, 0}, {IR_BINARY_INTEGER_SUBTRACT, 0, 1, 255},
        {IR_BINARY_INTEGER_MULTIPLY, 128, 3, 128}, {IR_BINARY_UNSIGNED_DIVIDE, 255, 2, 127},
        {IR_BINARY_SIGNED_DIVIDE, 255, 2, 0}, {IR_BINARY_SIGNED_REMAINDER, 253, 2, 255},
        {IR_BINARY_UNSIGNED_REMAINDER, 255, 2, 1}, {IR_BINARY_SIGNED_LESS, 128, 1, 1},
        {IR_BINARY_UNSIGNED_LESS, 128, 1, 0}, {IR_BINARY_SIGNED_SHIFT_RIGHT, 128, 1, 192},
        {IR_BINARY_UNSIGNED_SHIFT_RIGHT, 128, 1, 64}, {IR_BINARY_SHIFT_LEFT, 129, 1, 2},
        {IR_BINARY_INTEGER_BITWISE_XOR, 170, 85, 255}, {IR_BINARY_INTEGER_EQUAL, 255, 255, 1},
    };
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        memset(run, 0, sizeof(*run));
        IrInstruction row = {.opcode = IR_OPCODE_BINARY, .binary_operation = (u8)cases[i].operation};
        IrOracleValue value = ir_oracle_scalar(run, &row, 8, cases[i].a, cases[i].b);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_OK && value.bits == cases[i].expected);
    }
    IrBinaryOperation invalid[] = {IR_BINARY_UNSIGNED_DIVIDE, IR_BINARY_SIGNED_DIVIDE, IR_BINARY_SIGNED_REMAINDER, IR_BINARY_SHIFT_LEFT};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        memset(run, 0, sizeof(*run));
        IrInstruction row = {.opcode = IR_OPCODE_BINARY, .binary_operation = (u8)invalid[i]};
        (void)ir_oracle_scalar(run, &row, 8, i ? 128 : 1, i == 3 ? 8 : i ? 255 : 0);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID);
    }
    memset(run, 0, sizeof(*run));
    u32 allocation = ir_oracle_allocate(run, 8, 0, false);
    IrOracleValue pointer = {.kind = IR_ORACLE_POINTER, .allocation = allocation, .defined = true};
    (void)ir_oracle_memory(run, pointer, 8, false, 0);
    BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID); // uninitialized
    run->status = IR_ORACLE_OK;
    (void)ir_oracle_memory(run, pointer, 8, true, UINT64_C(0x0102030405060708));
    BUSTER_TEST(arguments, run->bytes[0] == 8 && run->bytes[7] == 1);
    pointer.bits = 8;
    (void)ir_oracle_memory(run, pointer, 1, false, 0);
    BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID); // one-past dereference
    run->status = IR_ORACLE_OK;
    pointer.bits = 1;
    (void)ir_oracle_memory(run, pointer, 8, false, 0);
    BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID); // misaligned/out of bounds
    run->status = IR_ORACLE_OK;
    pointer.bits = 0;
    run->allocations[allocation].alive = false;
    (void)ir_oracle_memory(run, pointer, 1, false, 0);
    BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID); // expired identity
    run->status = IR_ORACLE_OK;
    run->allocations[allocation].alive = true;
    run->allocations[allocation].read_only = true;
    (void)ir_oracle_memory(run, pointer, 1, true, 0);
    BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_oracle_rejection_controls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrProgram* program = ir_oracle_fixture(arguments->arena, 0, 0);
    if (BUSTER_REQUIRE(arguments, program))
    {
        IrFunction* probe = ir_oracle_named(program->modules, S8("probe"));
        IrOracleValue inputs[] = {ir_oracle_integer(3), ir_oracle_integer(7)};
        IrOracleRun* run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, 0);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_LIMIT);
        run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, IR_ORACLE_STEPS + 1);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_LIMIT);
        IrFunction* helper = ir_oracle_named(program->modules, S8("helper"));
        for (u32 i = 0; i < helper->instruction_count; i += 1)
        {
            IrInstruction* row = helper->instructions + i;
            if (row->opcode == IR_OPCODE_LOAD)
            {
                row->volatile_access = true;
                run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, IR_ORACLE_STEPS);
                BUSTER_TEST(arguments, run->status == IR_ORACLE_UNSUPPORTED);
                row->volatile_access = false;
                break;
            }
        }
        IrFunction* observe = ir_oracle_named(program->modules, S8("observe"));
        IrInstruction* terminator = observe->instructions + observe->blocks[0].last_instruction.value;
        IrInstruction saved = *terminator;
        *terminator = (IrInstruction){.opcode = IR_OPCODE_UNREACHABLE, .canonical_type = saved.canonical_type,
            .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID};
        BUSTER_TEST(arguments, ir_validate_canonical_module(program, program->modules).error == IR_VALIDATION_NONE);
        run = ir_oracle_evaluate(arguments->arena, program, program->modules, observe, 0, 0, IR_ORACLE_STEPS);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_INVALID && run->steps > 0);
        *terminator = saved;
        // Canonically valid undefined return must be refused, never compared.
        IrInstruction* constant = 0;
        for (u32 i = 0; i < probe->instruction_count; i += 1)
            if (probe->instructions[i].opcode == IR_OPCODE_CONSTANT_INTEGER) constant = probe->instructions + i;
        if (BUSTER_REQUIRE(arguments, constant))
        {
            constant->opcode = IR_OPCODE_UNDEFINED;
            constant->immediate_count = 0;
            constant->immediates = 0;
            run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, IR_ORACLE_STEPS);
            BUSTER_TEST(arguments, run->status == IR_ORACLE_UNSUPPORTED);
        }
    }
    program = ir_oracle_fixture(arguments->arena, IR_ORACLE_FIXTURES, 0);
    if (BUSTER_REQUIRE(arguments, program))
    {
        IrOracleValue inputs[] = {ir_oracle_integer(1), ir_oracle_integer(2)};
        IrOracleRun* run = ir_oracle_evaluate(arguments->arena, program, program->modules,
            ir_oracle_named(program->modules, S8("probe")), inputs, 2, IR_ORACLE_STEPS);
        BUSTER_TEST(arguments, run->status == IR_ORACLE_LIMIT && run->steps == IR_ORACLE_STEPS);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_oracle_clean(ProcessWaitResult wait)
{
    return wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !wait.termination_requested && !wait.forcibly_terminated &&
        !wait.capture_failed && !wait.capture_limit_exceeded && !wait.output_truncated && !wait.process_tree_cleanup_failed &&
        !wait.process_group_reservation_retained && !wait.process_group_ownership_lost && !wait.streams[STANDARD_STREAM_ERROR].length;
}

#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64 && !BUSTER_ANDROID && !BUSTER_SANITIZE
BUSTER_GLOBAL_LOCAL ProcessSpawnResult ir_oracle_spawn(Arena* arena, u32 fixture, u32 mutation, u32 mode)
{
    String8 argv[] = {program_state->input.arguments.pointer[0], S8("test"), S8("--ci=1"), S8("--verbose=1"),
                     S8("--module=ir_oracle_native_tests")};
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    String8* keys = arena_allocate(arena, String8, inherited_keys.length + 4);
    String8* values = arena_allocate(arena, String8, inherited_keys.length + 4);
    keys[0] = S8("BUSTER_IR_ORACLE_CHILD");
    values[0] = string_format_z(arena, S8("{u32}{u32}{u32}"), fixture, mutation, mode);
    keys[1] = S8("BUSTER_TEST_JOBS"); values[1] = S8("1");
    keys[2] = S8("BUSTER_TEST_MODULE_GROUP"); values[2] = S8("");
    keys[3] = S8("BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS"); values[3] = S8("0");
    u64 count = 4;
    for (u64 i = 0; i < inherited_keys.length; i += 1)
    {
        bool overridden = false;
        for (u32 k = 0; k < 4; k += 1) overridden = overridden || string_equal(keys[k], inherited_keys.pointer[i]);
        if (!overridden) { keys[count] = inherited_keys.pointer[i]; values[count++] = inherited_values.pointer[i]; }
    }
    return os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv),
        (SliceString8){keys, count}, (SliceString8){values, count},
        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .new_process_group = true, .search_path = true, .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
            .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = 32768, [STANDARD_STREAM_ERROR] = 4096}, .total = 36864}});
}
#endif

BUSTER_GLOBAL_LOCAL bool ir_oracle_record_token(String8 block, u64* cursor, String8 token)
{
    bool valid = *cursor <= block.length && token.length <= block.length - *cursor &&
                 !memcmp(block.pointer + *cursor, token.pointer, token.length);
    if (valid) *cursor += token.length;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_oracle_record_number(String8 block, u64* cursor)
{
    u64 start = *cursor;
    u64 value = 0;
    bool valid = true;
    while (*cursor < block.length && block.pointer[*cursor] >= '0' && block.pointer[*cursor] <= '9')
    {
        u64 digit = (u64)(block.pointer[*cursor] - '0');
        if (value > (UINT64_MAX - digit) / 10) valid = false;
        else value = value * 10 + digit;
        *cursor += 1;
    }
    return valid && *cursor > start && (*cursor == start + 1 || block.pointer[start] != '0');
}

BUSTER_GLOBAL_LOCAL bool ir_oracle_record(Arena* arena, String8 output, u32 fixture, String8* block_out)
{
    String8 begin = S8("IR_ORACLE_BEGIN_V1\n"), end_marker = S8("IR_ORACLE_END_V1\n");
    u64 start = string_first_sequence(output, begin);
    u64 end = string_first_sequence(output, end_marker);
    bool valid = start != BUSTER_STRING_NO_MATCH && end != BUSTER_STRING_NO_MATCH && end > start;
    *block_out = S8("");
    if (valid)
    {
        *block_out = (String8){.pointer = output.pointer + start, .length = end + end_marker.length - start};
        String8 tail = {.pointer = output.pointer + end + end_marker.length, .length = output.length - end - end_marker.length};
        String8 after_begin = {.pointer = output.pointer + start + begin.length, .length = output.length - start - begin.length};
        valid = string_first_sequence(after_begin, begin) == BUSTER_STRING_NO_MATCH &&
                string_first_sequence(tail, end_marker) == BUSTER_STRING_NO_MATCH;
        u64 cursor = 0;
        valid = valid && ir_oracle_record_token(*block_out, &cursor, begin);
        for (u32 input = 0; valid && input < IR_ORACLE_INPUTS; input += 1)
        {
            String8 prefix = string_format(arena, S8("IR_ORACLE_V1 fixture={u32} input={u32} return="), fixture, input);
            valid = ir_oracle_record_token(*block_out, &cursor, prefix) &&
                    ir_oracle_record_number(*block_out, &cursor) &&
                    ir_oracle_record_token(*block_out, &cursor, S8(" memory=")) &&
                    ir_oracle_record_number(*block_out, &cursor) &&
                    ir_oracle_record_token(*block_out, &cursor, S8("\n"));
        }
        valid = valid && ir_oracle_record_token(*block_out, &cursor, end_marker) && cursor == block_out->length;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_oracle_report_controls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 valid = S8("IR_ORACLE_BEGIN_V1\n");
    for (u32 i = 0; i < IR_ORACLE_INPUTS; i += 1)
        valid = string_format(arguments->arena, S8("{S8}IR_ORACLE_V1 fixture=0 input={u32} return=0 memory=0\n"), valid, i);
    valid = string_format(arguments->arena, S8("{S8}IR_ORACLE_END_V1\n"), valid);
    String8 block;
    BUSTER_TEST(arguments, ir_oracle_record(arguments->arena, valid, 0, &block) && string_equal(block, valid));
    String8 invalid[] = {
        S8(""), S8("IR_ORACLE_BEGIN_V1\nIR_ORACLE_END_V1\n"),
        S8("IR_ORACLE_BEGIN_V1\nIR_ORACLE_V1 fixture=0 input=1 return=0 memory=0\nIR_ORACLE_END_V1\n"),
        S8("IR_ORACLE_BEGIN_V1\nIR_ORACLE_V1 fixture=0 input=0 return=18446744073709551616 memory=0\nIR_ORACLE_END_V1\n"),
        S8("IR_ORACLE_BEGIN_V1\nIR_ORACLE_V1 fixture=0 input=0 return=00 memory=0\nIR_ORACLE_END_V1\n"),
        string_format(arguments->arena, S8("{S8}{S8}"), valid, valid),
    };
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
        BUSTER_TEST(arguments, !ir_oracle_record(arguments->arena, invalid[i], 0, &block));
    ProcessWaitResult wait = {.result = PROCESS_RESULT_SUCCESS};
    BUSTER_TEST(arguments, ir_oracle_clean(wait));
    wait.timed_out = true;
    BUSTER_TEST(arguments, !ir_oracle_clean(wait));
    wait.timed_out = false;
    wait.capture_failed = true;
    BUSTER_TEST(arguments, !ir_oracle_clean(wait));
    wait.capture_failed = false;
    wait.process_group_ownership_lost = true;
    BUSTER_TEST(arguments, !ir_oracle_clean(wait));
    wait.process_group_ownership_lost = false;
    wait.result = PROCESS_RESULT_FAILED;
    BUSTER_TEST(arguments, !ir_oracle_clean(wait));
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64 && !BUSTER_ANDROID && !BUSTER_SANITIZE
    // Deliberately generated infinite loop: a real timeout cannot become either
    // agreement or a successfully detected semantic mutation.
    ProcessSpawnResult spawn = ir_oracle_spawn(arguments->arena, IR_ORACLE_FIXTURES, 0, CODEGEN_REGISTER_ALLOCATOR_FAST);
    if (BUSTER_REQUIRE(arguments, spawn.handle))
    {
        wait = os_process_wait_deadline(arguments->arena, spawn, 5000000);
        BUSTER_TEST(arguments, wait.timed_out && !ir_oracle_clean(wait) && !wait.process_tree_cleanup_failed &&
                               !wait.process_group_reservation_retained && !wait.process_group_ownership_lost);
        arguments->show(arguments, S8("IR_ORACLE_TIMEOUT_CONTROL_V1 timed_out={u32} status=inconclusive\n"), (u32)wait.timed_out);
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_oracle_comparison(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 fixture = 0; fixture < IR_ORACLE_FIXTURES; fixture += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrProgram* program = ir_oracle_fixture(arguments->arena, fixture, 0);
        if (BUSTER_REQUIRE(arguments, program))
        {
            IrFunction* probe = ir_oracle_named(program->modules, S8("probe"));
            String8 expected = S8("IR_ORACLE_BEGIN_V1\n");
            bool evaluated = true;
            for (u32 input = 0; input < IR_ORACLE_INPUTS; input += 1)
            {
                IrOracleValue inputs[] = {ir_oracle_integer(ir_oracle_inputs[input][0]), ir_oracle_integer(ir_oracle_inputs[input][1])};
                IrOracleRun* run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, IR_ORACLE_STEPS);
                if (run->status != IR_ORACLE_OK)
                    arguments->show(arguments, S8("IR_ORACLE_REJECT_V1 fixture={u32} input={u32} status={u32} steps={u32} function={S8} row={u32} opcode={u32}\n"),
                                    fixture, input, (u32)run->status, run->steps, run->last_function ? run->last_function->name : S8("preflight"),
                                    run->last_row, run->last_opcode);
                BUSTER_TEST(arguments, run->status == IR_ORACLE_OK && run->returned.defined && run->returned.kind == IR_ORACLE_INTEGER);
                u64 memory = 0;
                if (run->status == IR_ORACLE_OK && program->modules->global_count == 1)
                {
                    IrOracleValue pointer = {.kind = IR_ORACLE_POINTER, .allocation = run->globals[0], .defined = true};
                    memory = ir_oracle_memory(run, pointer, 8, false, 0).bits;
                    BUSTER_TEST(arguments, run->status == IR_ORACLE_OK);
                }
                bool observed = run->status == IR_ORACLE_OK && run->returned.defined &&
                                run->returned.kind == IR_ORACLE_INTEGER && program->modules->global_count == 1;
                evaluated = evaluated && observed;
                u64 a = ir_oracle_inputs[input][0], b = ir_oracle_inputs[input][1];
                u64 literal_return;
                u64 literal_memory;
                if (fixture < 4)
                {
                    u32 width = 8u << fixture;
                    literal_memory = (a + b) & ir_oracle_mask(width);
                    literal_return = literal_memory + (a > b ? 1 : 2);
                }
                else if (fixture == 4)
                {
                    literal_memory = (a ^ b) + 7;
                    literal_return = literal_memory + (a > b ? 0 : 3);
                }
                else
                {
                    literal_memory = a;
                    literal_return = b;
                }
                BUSTER_TEST(arguments, run->status == IR_ORACLE_OK && run->returned.bits == literal_return && memory == literal_memory);
                expected = string_format(arguments->arena, S8("{S8}IR_ORACLE_V1 fixture={u32} input={u32} return={u64} memory={u64}\n"),
                                         expected, fixture, input, run->returned.bits, memory);
            }
            expected = string_format(arguments->arena, S8("{S8}IR_ORACLE_END_V1\n"), expected);
            // Also exercise the immutable published-CFG representation.
            IrValidationResult prepared = ir_prepare_canonical_module(program, program->modules, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
            if (prepared.error == IR_VALIDATION_NONE)
            {
                IrOracleValue inputs[] = {ir_oracle_integer(1), ir_oracle_integer(2)};
                IrOracleRun* run = ir_oracle_evaluate(arguments->arena, program, program->modules, probe, inputs, 2, IR_ORACLE_STEPS);
                BUSTER_TEST(arguments, run->status == IR_ORACLE_OK);
            }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64 && !BUSTER_ANDROID && !BUSTER_SANITIZE
            for (u32 mutation = 0; evaluated && mutation < (fixture == 0 ? 4u : 1u); mutation += 1)
            {
                for (u32 mode = 0; mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)
                {
                    ProcessSpawnResult spawn = ir_oracle_spawn(arguments->arena, fixture, mutation, mode);
                    if (BUSTER_REQUIRE(arguments, spawn.handle))
                    {
                        ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                        bool clean = ir_oracle_clean(wait);
                        String8 output = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
                        String8 block;
                        bool record = ir_oracle_record(arguments->arena, output, fixture, &block);
                        bool agreement = clean && record && string_equal(block, expected);
                        BUSTER_TEST(arguments, clean && record);
                        BUSTER_TEST(arguments, mutation ? clean && record && !agreement : agreement);
                        arguments->show(arguments, S8("IR_ORACLE_REPORT_V1 fixture={u32} mutation={u32} allocator={u32} status={S8} timed_out={u32}\n{S8}"),
                            fixture, mutation, mode, !clean || !record ? S8("inconclusive") : mutation ? agreement ? S8("negative-control-missed") : S8("detected-discrepancy") :
                            agreement ? S8("agreement") : S8("discrepancy"), (u32)wait.timed_out, block);
                        u64 metadata_start = string_first_sequence(output, S8("IR_ORACLE_CODEGEN_V1 "));
                        if (metadata_start != BUSTER_STRING_NO_MATCH)
                        {
                            u64 metadata_end = metadata_start;
                            while (metadata_end < output.length && output.pointer[metadata_end] != '\n') metadata_end += 1;
                            arguments->show(arguments, S8("{S8}\n"), (String8){output.pointer + metadata_start, metadata_end - metadata_start});
                        }
                        if (!clean) arguments->show(arguments, S8("{S8}\n"), output);
                    }
                }
            }
#else
            BUSTER_UNUSED(evaluated);
            BUSTER_UNUSED(expected);
            arguments->show(arguments, S8("IR_ORACLE_REPORT_V1 fixture={u32} status=native-unavailable\n"), fixture);
#endif
        }
        scratch_end(temporary);
    }
    return result;
}

UnitTestResult ir_oracle_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, ir_oracle_scalar_controls);
    BUSTER_TEST_FIXTURE(arguments, ir_oracle_rejection_controls);
    BUSTER_TEST_FIXTURE(arguments, ir_oracle_report_controls);
    BUSTER_TEST_FIXTURE(arguments, ir_oracle_comparison);
    return result;
}
#endif
