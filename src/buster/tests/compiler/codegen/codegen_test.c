#include <buster/tests/compiler/codegen/codegen_test.h>

typedef u64 CodegenTestFunction2(u64 left, u64 right);
typedef u64 CodegenTestFunction1(u64 value);
typedef f64 CodegenTestIntegerToFloatFunction(s32 value);
typedef s64 CodegenTestFloatToIntegerFunction(f64 value);
typedef u64 CodegenTestFunction0(void);
typedef f64 CodegenTestFloatFunction2(f64 left, f64 right);
typedef f64 CodegenTestFloatFunction0(void);
typedef struct CodegenTestAbiPair
{
    s64 left;
    s64 right;
} CodegenTestAbiPair;
typedef struct CodegenTestAbiMixed
{
    f64 value;
    s64 count;
} CodegenTestAbiMixed;
typedef struct CodegenTestAbiLarge
{
    s64 first;
    s64 second;
    s64 third;
} CodegenTestAbiLarge;
typedef s64 CodegenTestAbiPairSumFunction(CodegenTestAbiPair pair);
typedef CodegenTestAbiPair CodegenTestAbiPairMakeFunction(s64 left, s64 right);
typedef f64 CodegenTestAbiMixedSumFunction(CodegenTestAbiMixed mixed);
typedef s64 CodegenTestAbiLargeSumFunction(CodegenTestAbiLarge large);
typedef CodegenTestAbiLarge CodegenTestAbiLargeMakeFunction(s64 first, s64 second, s64 third);

BUSTER_GLOBAL_LOCAL AnalysisEntity* codegen_test_entity_find(AnalysisResult* analysis, String8 name)
{
    for (u32 index = 0; index < analysis->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrFunction* codegen_test_function_find(IrModule* module, AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].entity.module.value == entity.module.value && module->functions[index].entity.index.value == entity.index.value)
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL CodegenModuleEntry* codegen_test_module_entry_find(CodegenModule* module, AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->entry_count; index += 1)
    {
        CodegenModuleEntry* entry = module->entries + index;
        if (entry->entity.module.value == entity.module.value && entry->entity.index.value == entity.index.value)
        {
            return entry;
        }
    }
    return 0;
}

BUSTER_TEST_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Line rows must stop at the capacity of the array they are recorded
    // into: rows are appended while code is emitted, so running past the end
    // corrupts the arena allocations that follow and changes the code.
    CodegenLineEntry line_rows[3] = {0};
    u32 line_row_count = 0;
    for (u32 line_index = 0; line_index < 8; line_index += 1)
    {
        codegen_record_line(line_rows, &line_row_count, 2, line_index * 4, 0, line_index + 1, 1);
    }
    BUSTER_TEST(arguments, line_row_count == 2);
    BUSTER_TEST(arguments, line_rows[2].line == 0 && line_rows[2].code_offset == 0);
    u8 large_frame_operation_bytes[256] = {0};
    CodegenBuffer large_frame_operation = {
        .bytes = large_frame_operation_bytes,
        .capacity = sizeof(large_frame_operation_bytes),
    };
    BUSTER_TEST(arguments, codegen_canonical_a64_frame_memory_operation(&large_frame_operation, 9, 40000, 1, false, false));
    BUSTER_TEST(arguments, codegen_canonical_a64_frame_memory_operation(&large_frame_operation, 9, 40001, 1, true, false));
    a64_emit_load_pointer_offset(&large_frame_operation, 9, 28, 40004, 4);
    a64_emit_store_pointer_offset(&large_frame_operation, 9, 28, 40008, 4);
    BUSTER_TEST(arguments, large_frame_operation.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, large_frame_operation.count > 8);
    u32 large_stack_address_words[6] = {0};
    CodegenBuffer large_stack_address = {
        .bytes = (u8*)large_stack_address_words,
        .capacity = sizeof(large_stack_address_words),
    };
    codegen_canonical_a64_base_address(&large_stack_address, 16, 31, 40000);
    u32 expected_large_stack_address[] = {
        0xd2800000 | (40000u << 5) | 16, 0xf2a00010, 0xf2c00010, 0xf2e00010, 0x910003f1, 0x8b100230,
    };
    BUSTER_TEST(arguments, large_stack_address.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, large_stack_address.count == sizeof(expected_large_stack_address));
    BUSTER_TEST(arguments, !memcmp(large_stack_address_words, expected_large_stack_address, sizeof(expected_large_stack_address)));
    u32 unsigned_remainder_divide = codegen_canonical_a64_remainder_divide_instruction(false, false);
    BUSTER_TEST(arguments, ((unsigned_remainder_divide >> 5) & 31) == 9);
    BUSTER_TEST(arguments, ((unsigned_remainder_divide >> 16) & 31) == 10);
    BUSTER_TEST(arguments, (unsigned_remainder_divide & 31) == 11);
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    String8 source_parts[] = {S8("type CodegenPair = struct\n"
                                 "{\n"
                                 "    left: s32,\n"
                                 "    right: s32,\n"
                                 "}\n"
                                 "type CodegenNumber = union\n"
                                 "{\n"
                                 "    signed_value: s32,\n"
                                 "    unsigned_value: u32,\n"
                                 "}\n"
                                 "type CodegenChoice = enum\n"
                                 "{\n"
                                 "    first,\n"
                                 "    second,\n"
                                 "}\n"
                                 "type CodegenAbiPair = struct\n"
                                 "{\n"
                                 "    left: s64,\n"
                                 "    right: s64,\n"
                                 "}\n"
                                 "type CodegenAbiMixed = struct\n"
                                 "{\n"
                                 "    value: f64,\n"
                                 "    count: s64,\n"
                                 "}\n"
                                 "type CodegenAbiLarge = struct\n"
                                 "{\n"
                                 "    first: s64,\n"
                                 "    second: s64,\n"
                                 "    third: s64,\n"
                                 "}\n"
                                 "type CodegenFloat8 = vector[8]f32\n"
                                 "type CodegenFloat16 = vector[16]f32\n"
                                 "code arithmetic : fn (left: s64, right: s64) s64\n"
                                 "{\n"
                                 "    data value: s64 = left * 3;\n"
                                 "    if (left < right)\n"
                                 "    {\n"
                                 "        value += right;\n"
                                 "    }\n"
                                 "    else\n"
                                 "    {\n"
                                 "        value -= right;\n"
                                 "    }\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code float_arithmetic : fn (left: f64, right: f64) f64\n"
                                 "{\n"
                                 "    return -left * 2.0 + right;\n"
                                 "}\n"),
                              S8("code vector_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[4]f32 = [ 1.0, 2.0, 3.0, 4.0 ];\n"
                                 "    data right: vector[4]f32 = [ 4.0, 3.0, 2.0, 1.0 ];\n"
                                 "    data sum: vector[4]f32 = left + right;\n"
                                 "    data negated: vector[4]f32 = -sum;\n"
                                 "    return @cast(negated[0]);\n"
                                 "}\n"
                                 "code vector_identity : fn (value: vector[4]f32) vector[4]f32\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code string_literal_value[export] : fn () s32\n"
                                 "{\n"
                                 "    data greeting = \"hello\";\n"
                                 "    return @cast(greeting[1]);\n"
                                 "}\n"
                                 "code vector_integer_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[4]s32 = [ 1, 2, 3, 4 ];\n"
                                 "    data right: vector[4]s32 = [ 4, 3, 2, 1 ];\n"
                                 "    data masked: vector[4]s32 = (left + right) & [ 7, 7, 7, 7 ];\n"
                                 "    return masked[0];\n"
                                 "}\n"),
                              S8("code vector_float_comparison : fn () u32\n"
                                 "{\n"
                                 "    data left: vector[4]f32 = [ 1.0, 5.0, -3.0, 8.0 ];\n"
                                 "    data right: vector[4]f32 = [ 2.0, 4.0, -3.0, 9.0 ];\n"
                                 "    data mask: vector[4]u32 = left < right;\n"
                                 "    return mask[0];\n"
                                 "}\n"
                                 "code vector_integer_comparison : fn () u32\n"
                                 "{\n"
                                 "    data left: vector[4]s32 = [ 1, 5, -3, 8 ];\n"
                                 "    data right: vector[4]s32 = [ 2, 4, -3, 9 ];\n"
                                 "    data mask: vector[4]u32 = left > right;\n"
                                 "    return mask[1];\n"
                                 "}\n"
                                 "code vector_256_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: CodegenFloat8 = [ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 ];\n"
                                 "    data right: CodegenFloat8 = [ 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0 ];\n"
                                 "    data sum: CodegenFloat8 = left + right;\n"
                                 "    data doubled: CodegenFloat8 = sum + right;\n"
                                 "    return @cast(doubled[7]);\n"
                                 "}\n"
                                 "code vector_256_commutative_rhs : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[8]s32 = [ 1, 2, 3, 4, 5, 6, 7, 8 ];\n"
                                 "    data right: vector[8]s32 = [ 8, 7, 6, 5, 4, 3, 2, 1 ];\n"
                                 "    data sum: vector[8]s32 = left + right;\n"
                                 "    data doubled: vector[8]s32 = right + sum;\n"
                                 "    return doubled[7];\n"
                                 "}\n"
                                 "code vector_256_identity : fn (value: CodegenFloat8) CodegenFloat8\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code vector_512_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[16]s32 = [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 ];\n"
                                 "    data right: vector[16]s32 = [ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ];\n"
                                 "    data sum: vector[16]s32 = left + right;\n"
                                 "    return sum[15];\n"
                                 "}\n"
                                 "code vector_512_identity : fn (value: CodegenFloat16) CodegenFloat16\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"),
                              S8("code pointer_arithmetic : fn () s64\n"
                                 "{\n"
                                 "    data value: s64 = 4;\n"
                                 "    data pointer: &s64 = &value;\n"
                                 "    pointer.& += 3;\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code straight_arithmetic : fn (left: s64, right: s64) s64\n"
                                 "{\n"
                                 "    return left * 3 + right;\n"
                                 "}\n"
                                 "code register_pressure : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, i: s64) s64\n"
                                 "{\n"
                                 "    return a + b + c + d + e + f + g + h + i;\n"
                                 "}\n"
                                 "code abi_pair_sum : fn (pair: CodegenAbiPair) s64\n"
                                 "{\n"
                                 "    return pair.left + pair.right;\n"
                                 "}\n"
                                 "code abi_pair_make : fn (left: s64, right: s64) CodegenAbiPair\n"
                                 "{\n"
                                 "    return { .left = left, .right = right };\n"
                                 "}\n"
                                 "code abi_pair_round_trip : fn () s64\n"
                                 "{\n"
                                 "    data pair: CodegenAbiPair = abi_pair_make(19, 23);\n"
                                 "    return abi_pair_sum(pair);\n"
                                 "}\n"
                                 "code abi_mixed_sum : fn (mixed: CodegenAbiMixed) f64\n"
                                 "{\n"
                                 "    return mixed.value + @cast(mixed.count);\n"
                                 "}\n"
                                 "code abi_mixed_round_trip : fn () f64\n"
                                 "{\n"
                                 "    data mixed: CodegenAbiMixed = { .value = 1.5, .count = 2 };\n"
                                 "    return abi_mixed_sum(mixed);\n"
                                 "}\n"
                                 "code abi_large_make : fn (first: s64, second: s64, third: s64) CodegenAbiLarge\n"
                                 "{\n"
                                 "    return { .first = first, .second = second, .third = third };\n"
                                 "}\n"
                                 "code abi_large_sum : fn (large: CodegenAbiLarge) s64\n"
                                 "{\n"
                                 "    return large.first + large.second + large.third;\n"
                                 "}\n"
                                 "code abi_large_round_trip : fn () s64\n"
                                 "{\n"
                                 "    data large: CodegenAbiLarge = abi_large_make(5, 7, 11);\n"
                                 "    return abi_large_sum(large);\n"
                                 "}\n"
                                 "code range_sum : fn () s32\n"
                                 "{\n"
                                 "    data total: s32 = 0;\n"
                                 "    for (data value: s32 = 0 .. 4)\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    for (data value: s32 = @reverse(0 .. 4))\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    return total;\n"
                                 "}\n"
                                 "code union_value : fn () s32\n"
                                 "{\n"
                                 "    data number: CodegenNumber = { .signed_value = 17 };\n"
                                 "    return number.signed_value;\n"
                                 "}\n"
                                 "code aggregate_sum : fn () s32\n"
                                 "{\n"
                                 "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                                 "    data selected: []s32 = values[1..];\n"
                                 "    data pair: CodegenPair = { .left = 2, .right = selected[1] };\n"
                                 "    pair.left += selected[0];\n"
                                 "    return pair.left + pair.right;\n"
                                 "}\n"
                                 "code collection_sum : fn () s32\n"
                                 "{\n"
                                 "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                                 "    data total: s32 = 0;\n"
                                 "    for (data value: s32 = values[..])\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    for (data value: s32 = @reverse(values[..]))\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    return total;\n"
                                 "}\n"
                                 "code add_one : fn (value: s64) s64\n"
                                 "{\n"
                                 "    return value + 1;\n"
                                 "}\n"
                                 "code call_chain : fn (value: s64) s64\n"
                                 "{\n"
                                 "    return add_one(value) * 2;\n"
                                 "}\n"
                                 "code sum_seven : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64) s64\n"
                                 "{\n"
                                 "    return a + b + c + d + e + f + g;\n"
                                 "}\n"
                                 "code call_many : fn () s64\n"
                                 "{\n"
                                 "    return sum_seven(1, 2, 3, 4, 5, 6, 7);\n"
                                 "}\n"
                                 "code integer_to_float : fn (value: s32) f64\n"
                                 "{\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code float_to_integer : fn (value: f64) s64\n"
                                 "{\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code choose : fn (value: CodegenChoice) s64\n"
                                 "{\n"
                                 "    switch (value)\n"
                                 "    {\n"
                                 "        .first => { return 11; },\n"
                                 "        else => { return 22; },\n"
                                 "    }\n"
                                 "}\n"
                                 "code variadic_sum : fn (first: s64, ...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data copy = @va_copy(&arguments);\n"
                                 "    data second: s64 = @va_arg(&copy, s64);\n"
                                 "    data third: s64 = @va_arg(&copy, s64);\n"
                                 "    @va_end(&copy);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return first + second + third;\n"
                                 "}\n"
                                 "code variadic_call : fn () s64\n"
                                 "{\n"
                                 "    return variadic_sum(10, 20, 12);\n"
                                 "}\n"),
                              S8("code variadic_float : fn (first: s64, ...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: f64 = @va_arg(&arguments, f64);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code variadic_float_call : fn () f64\n"
                                 "{\n"
                                 "    data value: f32 = 5.25;\n"
                                 "    return variadic_float(0, value);\n"
                                 "}\n"
                                 "code variadic_promoted : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: s32 = @va_arg(&arguments, s32);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code variadic_promoted_call : fn () s64\n"
                                 "{\n"
                                 "    data value: u8 = 42;\n"
                                 "    return variadic_promoted(value);\n"
                                 "}\n"
                                 "code variadic_pair : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiPair = @va_arg(&arguments, CodegenAbiPair);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.left + value.right;\n"
                                 "}\n"
                                 "code variadic_pair_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiPair = { .left = 19, .right = 23 };\n"
                                 "    return variadic_pair(value);\n"
                                 "}\n"
                                 "code variadic_mixed : fn (...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiMixed = @va_arg(&arguments, CodegenAbiMixed);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.value + @cast(value.count);\n"
                                 "}\n"
                                 "code variadic_mixed_call : fn () f64\n"
                                 "{\n"
                                 "    data value: CodegenAbiMixed = { .value = 2.25, .count = 3 };\n"
                                 "    return variadic_mixed(value);\n"
                                 "}\n"
                                 "code variadic_large : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.first + value.second + value.third;\n"
                                 "}\n"
                                 "code variadic_large_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
                                 "    return variadic_large(value);\n"
                                 "}\n"),
                              S8("type CodegenAbiHfa = struct\n"
                                 "{\n"
                                 "    first: f64,\n"
                                 "    second: f64,\n"
                                 "}\n"
                                 "code abi_exhaust_float : fn (a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, pair: CodegenAbiHfa, tail: f64) f64\n"
                                 "{\n"
                                 "    return a + pair.first + tail;\n"
                                 "}\n"
                                 "code abi_exhaust_integer : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, pair: CodegenAbiPair, tail: s64) s64\n"
                                 "{\n"
                                 "    return a + pair.left + tail;\n"
                                 "}\n"
                                 "code variadic_fixed_float : fn (first: f64, ...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    @va_end(&arguments);\n"
                                 "    return first;\n"
                                 "}\n"
                                 "code variadic_fixed_float_call : fn () f64\n"
                                 "{\n"
                                 "    return variadic_fixed_float(3.5, 1);\n"
                                 "}\n"
                                 "code variadic_large_stack : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, ...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.first + value.second + value.third;\n"
                                 "}\n"
                                 "code variadic_large_stack_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
                                 "    return variadic_large_stack(0, 1, 2, 3, 4, 5, 6, 7, value);\n"
                                 "}\n")};
    String8 source = string_join_arena(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(source_parts), false);
    TokenizerResult tokens = tokenize(arguments->arena, source.pointer, source.length);
    ParserResult parser = parser_parse(arguments->arena, expression_arena, source, tokens);
    BUSTER_TEST(arguments, tokens.error_count == 0);
    BUSTER_TEST(arguments, parser.diagnostic_count == 0);
    AnalysisSourceInput input = {
        .path = S8("codegen-x86-64.bbb"),
        .parser = &parser,
    };
    AnalysisResult analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 800}, S8("codegen-x86-64"), &input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &analysis);
    IrModule module = ir_analyze_and_generate_module(arguments->arena, &analysis);
    BUSTER_TEST(arguments, analysis.diagnostic_count == 0);
    AnalysisEntity* entity = codegen_test_entity_find(&analysis, S8("arithmetic"));
    BUSTER_TEST(arguments, entity != 0);
    IrFunction* function = entity ? codegen_test_function_find(&module, entity->id) : 0;
    BUSTER_TEST(arguments, function != 0);
    Target target = target_native;
    target.cpu_arch = CPU_ARCH_X86_64;
    Target baseline_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    Target avx2_target = baseline_target;
    avx2_target.cpu_features_explicit = true;
    avx2_target.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2;
    Target avx512f_target = avx2_target;
    avx512f_target.cpu_features |= TARGET_CPU_FEATURE_X86_AVX512F;
    Target avx10_target = avx2_target;
    avx10_target.cpu_features |= TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512VL | TARGET_CPU_FEATURE_X86_AVX512BW |
                                 TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_512 |
                                 TARGET_CPU_FEATURE_X86_APX;
    BUSTER_TEST(arguments, target_vector_register_size(baseline_target) == 16);
    BUSTER_TEST(arguments, target_vector_register_size(avx2_target) == 32);
    BUSTER_TEST(arguments, target_vector_register_size(avx10_target) == 64);
    BUSTER_TEST(arguments, target_cpu_feature_has(avx10_target, TARGET_CPU_FEATURE_X86_APX));
    BUSTER_TEST(arguments, x64_target_supports_native_vector(avx512f_target, 64, 32, true));
    BUSTER_TEST(arguments, !x64_target_supports_native_vector(avx512f_target, 64, 8, true));
    BUSTER_TEST(arguments, x64_target_supports_native_vector(avx10_target, 64, 8, true));
    BUSTER_TEST(arguments, target_vector_register_size((Target){
                               .cpu_arch = CPU_ARCH_AARCH64,
                               .cpu_model = CPU_MODEL_BASELINE,
                           }) == 16);
    u8 x64_stack_adjust_bytes[32] = {0};
    CodegenBuffer x64_stack_adjust_buffer = {
        .bytes = x64_stack_adjust_bytes,
        .capacity = sizeof(x64_stack_adjust_bytes),
    };
    codegen_canonical_x64_adjust_stack(&x64_stack_adjust_buffer, 4097, true);
    codegen_canonical_x64_adjust_stack(&x64_stack_adjust_buffer, 4097, false);
    static u8 const expected_x64_stack_adjust[] = {
        0x48, 0x81, 0xec, 0x00, 0x10, 0x00, 0x00, 0xf6, 0x04, 0x24, 0x00, 0x48, 0x83,
        0xec, 0x01, 0xf6, 0x04, 0x24, 0x00, 0x48, 0x81, 0xc4, 0x01, 0x10, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x64_stack_adjust_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x64_stack_adjust_buffer.count == sizeof(expected_x64_stack_adjust));
    BUSTER_TEST(arguments, !memcmp(x64_stack_adjust_bytes, expected_x64_stack_adjust, sizeof(expected_x64_stack_adjust)));
    u8 x64_wide_vector_bytes[48] = {0};
    X64Builder x64_wide_vector_builder = {
        .buffer =
            {
                .bytes = x64_wide_vector_bytes,
                .capacity = sizeof(x64_wide_vector_bytes),
            },
    };
    x64_emit_vector_native_memory(&x64_wide_vector_builder, false, 32, X64_REGISTER_R8);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfe, 32, X64_REGISTER_R9);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, true, 32, X64_REGISTER_R10);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, false, 64, X64_REGISTER_R8);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfe, 64, X64_REGISTER_R9);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, true, 64, X64_REGISTER_R10);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfc, 64, X64_REGISTER_R9);
    static u8 const expected_x64_wide_vector[] = {
        0xc4, 0xc1, 0x7c, 0x10, 0x00, 0xc4, 0xc1, 0x7d, 0xfe, 0x01, 0xc4, 0xc1, 0x7c, 0x11, 0x02, 0x62, 0xd1, 0x7c, 0x48, 0x10,
        0x00, 0x62, 0xd1, 0x7d, 0x48, 0xfe, 0x01, 0x62, 0xd1, 0x7c, 0x48, 0x11, 0x02, 0x62, 0xd1, 0x7d, 0x48, 0xfc, 0x01,
    };
    BUSTER_TEST(arguments, x64_wide_vector_builder.buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x64_wide_vector_builder.buffer.count == sizeof(expected_x64_wide_vector));
    BUSTER_TEST(arguments, !memcmp(x64_wide_vector_bytes, expected_x64_wide_vector, sizeof(expected_x64_wide_vector)));
    u8 x64_vzeroupper_bytes[4] = {0};
    X64Builder x64_vzeroupper_builder = {
        .buffer =
            {
                .bytes = x64_vzeroupper_bytes,
                .capacity = sizeof(x64_vzeroupper_bytes),
            },
    };
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    BUSTER_TEST(arguments, x64_vzeroupper_builder.buffer.count == 0);
    x64_vzeroupper_builder.upper_vector_dirty = true;
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    static u8 const expected_x64_vzeroupper[] = {
        0xc5,
        0xf8,
        0x77,
    };
    BUSTER_TEST(arguments, x64_vzeroupper_builder.buffer.count == sizeof(expected_x64_vzeroupper));
    BUSTER_TEST(arguments, x64_vzeroupper_builder.vzeroupper_count == 1);
    BUSTER_TEST(arguments, !memcmp(x64_vzeroupper_bytes, expected_x64_vzeroupper, sizeof(expected_x64_vzeroupper)));
    u32 aarch64_stack_adjust_words[6] = {0};
    CodegenBuffer aarch64_stack_adjust_buffer = {
        .bytes = (u8*)aarch64_stack_adjust_words,
        .capacity = sizeof(aarch64_stack_adjust_words),
    };
    codegen_canonical_a64_adjust_stack(&aarch64_stack_adjust_buffer, 4081, true);
    codegen_canonical_a64_adjust_stack(&aarch64_stack_adjust_buffer, 4081, false);
    u32 expected_aarch64_stack_adjust[] = {
        0xd10003ff | (4080u << 10), 0xf90003ff, 0xd10003ff | (1u << 10), 0xf90003ff, 0x910003ff | (4080u << 10), 0x910003ff | (1u << 10),
    };
    BUSTER_TEST(arguments, aarch64_stack_adjust_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_stack_adjust_buffer.count == sizeof(expected_aarch64_stack_adjust));
    BUSTER_TEST(arguments, !memcmp(aarch64_stack_adjust_words, expected_aarch64_stack_adjust, sizeof(expected_aarch64_stack_adjust)));
    u32 aarch64_aggregate_entry_words[5] = {0};
    CodegenBuffer aarch64_aggregate_entry_buffer = {
        .bytes = (u8*)aarch64_aggregate_entry_words,
        .capacity = sizeof(aarch64_aggregate_entry_words),
    };
    u32 aarch64_aggregate_offsets[] = {64};
    a64_emit_initialize_aggregate_result(&aarch64_aggregate_entry_buffer, aarch64_aggregate_offsets, (IrValueId){.value = 0});
    a64_emit_copy_memory_registers(&aarch64_aggregate_entry_buffer, 17, 16, 15, 8);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_buffer.count == sizeof(aarch64_aggregate_entry_words));
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[0] == 0x910003f0);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[1] == 0x91010210);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[2] == 0xf90003f0);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[3] == 0xf940020f);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[4] == 0xf900022f);
    u32 aarch64_float_snapshot_words[2] = {0};
    CodegenBuffer aarch64_float_snapshot_buffer = {
        .bytes = (u8*)aarch64_float_snapshot_words,
        .capacity = sizeof(aarch64_float_snapshot_words),
    };
    a64_emit_float_store_offset(&aarch64_float_snapshot_buffer, 3, 32, 16);
    a64_emit_float_load_offset(&aarch64_float_snapshot_buffer, 16, 48, 8);
    BUSTER_TEST(arguments, aarch64_float_snapshot_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_float_snapshot_buffer.count == sizeof(aarch64_float_snapshot_words));
    BUSTER_TEST(arguments, aarch64_float_snapshot_words[0] == 0x3d800be3);
    BUSTER_TEST(arguments, aarch64_float_snapshot_words[1] == 0xfd401bf0);
    typedef struct CodegenTargetAbiCase
    {
        CpuArch cpu_arch;
        OperatingSystem os;
        CodegenAbi abi;
    } CodegenTargetAbiCase;
    CodegenTargetAbiCase target_abi_cases[] = {
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_LINUX, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_MACOS, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_WINDOWS, CODEGEN_ABI_X86_64_WINDOWS},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_UEFI, CODEGEN_ABI_X86_64_WINDOWS},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_ANDROID, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_IOS, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_FREESTANDING, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_LINUX, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_MACOS, CODEGEN_ABI_AARCH64_DARWIN},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_WINDOWS, CODEGEN_ABI_AARCH64_WINDOWS},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_UEFI, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_ANDROID, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_IOS, CODEGEN_ABI_AARCH64_DARWIN},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_FREESTANDING, CODEGEN_ABI_AARCH64_AAPCS64},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_abi_cases); index += 1)
    {
        CodegenTargetAbiCase* test = target_abi_cases + index;
        BUSTER_TEST(arguments, codegen_abi_for_target((Target){
                                   .cpu_arch = test->cpu_arch,
                                   .os = test->os,
                               }) == test->abi);
    }
    CodegenFunction generated =
        function ? codegen_generate_function(arguments->arena, &analysis, function, target) : (CodegenFunction){.error = CODEGEN_ERROR_INVALID_IR};
    BUSTER_TEST(arguments, generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, generated.code.length > 0);
    BUSTER_TEST(arguments, generated.register_value_count > 0);
    BUSTER_TEST(arguments, generated.spilled_value_count > 0);
    BUSTER_TEST(arguments, generated.descriptor.code_offset == 0);
    BUSTER_TEST(arguments, generated.descriptor.code_size == generated.code.length);
    BUSTER_TEST(arguments, generated.descriptor.prolog_size <= generated.descriptor.code_size);
    BUSTER_TEST(arguments, generated.descriptor.unwind_action_count >= 2);
    if (generated.descriptor.unwind_action_count >= 2)
    {
        CodegenUnwindAction* push = generated.descriptor.unwind_actions;
        CodegenUnwindAction* frame = generated.descriptor.unwind_actions + 1;
        BUSTER_TEST(arguments, push->kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER && push->register_index == X64_REGISTER_RBP && push->code_offset == 1);
        BUSTER_TEST(arguments,
                    frame->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER && frame->register_index == X64_REGISTER_RBP && frame->code_offset == 4);
        u32 allocated = 0;
        for (u32 action_index = 2; action_index < generated.descriptor.unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = generated.descriptor.unwind_actions + action_index;
            BUSTER_TEST(arguments, action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK);
            allocated += action->value;
        }
        BUSTER_TEST(arguments, allocated == generated.stack_frame_size);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable executable = codegen_make_executable(generated);
    BUSTER_TEST(arguments, executable.error == CODEGEN_ERROR_NONE);
    if (executable.address)
    {
        CodegenTestFunction2* native = 0;
        BUSTER_CT_CHECK(sizeof(native) == sizeof(executable.address));
        memcpy(&native, &executable.address, sizeof(native));
        u64 first = native(2, 5);
        u64 second = native(7, 3);
        AnalysisResult* analysis_modules[] = {&analysis};
        AnalysisProgram analysis_program = {
            .module_results = analysis_modules,
            .module_count = 1,
        };
        IrProgram ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        IrExecutionArgument first_arguments[] = {
            {.bits = 2},
            {.bits = 5},
        };
        IrExecutionArgument second_arguments[] = {
            {.bits = 7},
            {.bits = 3},
        };
        IrExecutionResult first_interpreted = ir_execute(expression_arena, &analysis_program, &ir_program, entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                         first_arguments, BUSTER_ARRAY_LENGTH(first_arguments), (IrExecutionOptions){0});
        IrExecutionResult second_interpreted = ir_execute(expression_arena, &analysis_program, &ir_program, entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                          second_arguments, BUSTER_ARRAY_LENGTH(second_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, first_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, second_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, first == first_interpreted.bits);
        BUSTER_TEST(arguments, second == second_interpreted.bits);
        codegen_release_executable(executable);
    }
#endif
    if (function)
    {
        CodegenAbiSignature system_v = codegen_classify_signature(arguments->arena, &analysis, function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, system_v.argument_count == 2);
        BUSTER_TEST(arguments, system_v.arguments != 0);
        if (system_v.arguments && system_v.argument_count >= 2)
        {
            BUSTER_TEST(arguments, system_v.arguments[0].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
            BUSTER_TEST(arguments, system_v.arguments[0].index == 0);
            BUSTER_TEST(arguments, system_v.arguments[1].index == 1);
        }
    }
    AnalysisEntity* pair_sum_abi_entity = codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* mixed_sum_abi_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* large_make_abi_entity = codegen_test_entity_find(&analysis, S8("abi_large_make"));
    IrFunction* pair_sum_abi_function = pair_sum_abi_entity ? codegen_test_function_find(&module, pair_sum_abi_entity->id) : 0;
    IrFunction* mixed_sum_abi_function = mixed_sum_abi_entity ? codegen_test_function_find(&module, mixed_sum_abi_entity->id) : 0;
    IrFunction* large_make_abi_function = large_make_abi_entity ? codegen_test_function_find(&module, large_make_abi_entity->id) : 0;
    BUSTER_TEST(arguments, pair_sum_abi_function != 0);
    BUSTER_TEST(arguments, mixed_sum_abi_function != 0);
    BUSTER_TEST(arguments, large_make_abi_function != 0);
    if (pair_sum_abi_function && mixed_sum_abi_function && large_make_abi_function)
    {
        CodegenAbiSignature pair_system_v = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature pair_windows = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature pair_aapcs = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature pair_windows_aarch64 =
            codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, pair_system_v.valid);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].part_count == 2);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].parts[0].index == 0);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].parts[1].index == 1);
        BUSTER_TEST(arguments, pair_windows.valid);
        BUSTER_TEST(arguments, pair_windows.arguments[0].indirect);
        BUSTER_TEST(arguments, pair_windows.arguments[0].indirect_copy_offset >= 32);
        BUSTER_TEST(arguments, pair_aapcs.valid);
        BUSTER_TEST(arguments, pair_aapcs.arguments[0].part_count == 2);
        BUSTER_TEST(arguments, pair_windows_aarch64.valid);
        BUSTER_TEST(arguments, pair_windows_aarch64.arguments[0].part_count == 2);
        CodegenAbiSignature mixed_system_v = codegen_classify_signature(arguments->arena, &analysis, mixed_sum_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, mixed_system_v.valid);
        BUSTER_TEST(arguments, mixed_system_v.argument_count == 1);
        BUSTER_TEST(arguments, mixed_system_v.arguments != 0);
        if (mixed_system_v.arguments && mixed_system_v.argument_count >= 1)
        {
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].part_count == 2);
        }
        if (mixed_system_v.arguments && mixed_system_v.argument_count >= 1 && mixed_system_v.arguments[0].part_count >= 2)
        {
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].parts[0].kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER);
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].parts[1].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        }
        CodegenAbiSignature large_system_v =
            codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature large_windows = codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature large_aapcs = codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        BUSTER_TEST(arguments, large_system_v.result.indirect);
        BUSTER_TEST(arguments, large_system_v.indirect_result_register == 0);
        BUSTER_TEST(arguments, large_system_v.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_windows.result.indirect);
        BUSTER_TEST(arguments, large_windows.indirect_result_register == 0);
        BUSTER_TEST(arguments, large_windows.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_aapcs.result.indirect);
        BUSTER_TEST(arguments, large_aapcs.indirect_result_register == 8);
        BUSTER_TEST(arguments, large_aapcs.arguments[0].index == 0);
    }

    AnalysisEntity* exhaust_float_entity = codegen_test_entity_find(&analysis, S8("abi_exhaust_float"));
    AnalysisEntity* exhaust_integer_entity = codegen_test_entity_find(&analysis, S8("abi_exhaust_integer"));
    IrFunction* exhaust_float_function = exhaust_float_entity ? codegen_test_function_find(&module, exhaust_float_entity->id) : 0;
    IrFunction* exhaust_integer_function = exhaust_integer_entity ? codegen_test_function_find(&module, exhaust_integer_entity->id) : 0;
    BUSTER_TEST(arguments, exhaust_float_function != 0);
    BUSTER_TEST(arguments, exhaust_integer_function != 0);
    if (exhaust_float_function && exhaust_integer_function)
    {
        CodegenAbiSignature exhaust_float = codegen_classify_signature(arguments->arena, &analysis, exhaust_float_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature exhaust_integer =
            codegen_classify_signature(arguments->arena, &analysis, exhaust_integer_function->type, CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, exhaust_float.valid);
        BUSTER_TEST(arguments, exhaust_float.argument_count == 9);
        BUSTER_TEST(arguments, exhaust_float.arguments[7].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_float.arguments[8].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_integer.valid);
        BUSTER_TEST(arguments, exhaust_integer.arguments[7].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_integer.arguments[8].kind == CODEGEN_ABI_LOCATION_STACK);
    }

    AnalysisEntity* variadic_float_abi_entity = codegen_test_entity_find(&analysis, S8("variadic_float"));
    IrFunction* variadic_float_abi_function = variadic_float_abi_entity ? codegen_test_function_find(&module, variadic_float_abi_entity->id) : 0;
    BUSTER_TEST(arguments, variadic_float_abi_function != 0);
    if (variadic_float_abi_function)
    {
        AnalysisTypeId variadic_argument_types[] = {
            analysis.types.builtin.s64_type,
            analysis.types.builtin.f64_type,
        };
        CodegenAbiSignature windows_aarch64_variadic =
            codegen_classify_signature_with_arguments(arguments->arena, &analysis, variadic_float_abi_function->type, variadic_argument_types,
                                                      BUSTER_ARRAY_LENGTH(variadic_argument_types), codegen_target_for_abi(CODEGEN_ABI_AARCH64_WINDOWS));
        BUSTER_TEST(arguments, windows_aarch64_variadic.valid);
        BUSTER_TEST(arguments, windows_aarch64_variadic.argument_count == 2);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[0].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[1].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[1].index == 1);
    }

    AnalysisType* vector_128 = 0;
    AnalysisType* vector_256 = 0;
    AnalysisType* vector_512 = 0;
    for (u32 type_index = 0; type_index < analysis.types.count; type_index += 1)
    {
        AnalysisType* candidate = analysis.types.types + type_index;
        if (candidate->kind != ANALYSIS_TYPE_VECTOR)
        {
            continue;
        }
        if (candidate->layout.size == 16)
        {
            vector_128 = candidate;
        }
        else if (candidate->layout.size == 32)
        {
            vector_256 = candidate;
        }
        else if (candidate->layout.size == 64)
        {
            vector_512 = candidate;
        }
    }
    BUSTER_TEST(arguments, vector_128 != 0);
    BUSTER_TEST(arguments, vector_256 != 0);
    BUSTER_TEST(arguments, vector_512 != 0);
    if (vector_128 && vector_256 && vector_512)
    {
        AnalysisAbiValue systemv_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        AnalysisAbiValue systemv_256 = analysis_abi_value_classify(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        AnalysisAbiValue systemv_512 = analysis_abi_value_classify(arguments->arena, &analysis, vector_512->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        BUSTER_TEST(arguments, systemv_128.part_count == 1 && systemv_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_128.parts[0].size == 16);
        BUSTER_TEST(arguments, systemv_256.part_count == 1 && systemv_256.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_256.parts[0].size == 32);
        BUSTER_TEST(arguments, systemv_512.part_count == 1 && systemv_512.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_512.parts[0].size == 64);
        AnalysisAbiValue systemv_variadic_256 =
            analysis_abi_value_classify_variadic_argument(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
        BUSTER_TEST(arguments, systemv_variadic_256.parts[0].location == ANALYSIS_ABI_LOCATION_STACK);
        AnalysisAbiValue windows_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_WIN64_X86_64, false);
        BUSTER_TEST(arguments, windows_128.indirect && windows_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_POINTER);
        AnalysisAbiValue aapcs_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_AAPCS64, false);
        BUSTER_TEST(arguments, !aapcs_128.indirect && aapcs_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR);
        AnalysisAbiValue aapcs_256 = analysis_abi_value_classify(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_AAPCS64, false);
        BUSTER_TEST(arguments, aapcs_256.indirect && aapcs_256.parts[0].abi_class == ANALYSIS_ABI_CLASS_POINTER);

        String8 identity_names[] = {
            S8_INITIALIZER("vector_256_identity"),
            S8_INITIALIZER("vector_512_identity"),
        };
        Target identity_split_targets[] = {
            baseline_target,
            avx2_target,
        };
        Target identity_native_targets[] = {
            avx2_target,
            avx10_target,
        };
        for (u32 identity_index = 0; identity_index < BUSTER_ARRAY_LENGTH(identity_names); identity_index += 1)
        {
            AnalysisEntity* identity_entity = codegen_test_entity_find(&analysis, identity_names[identity_index]);
            BUSTER_TEST(arguments, identity_entity != 0);
            if (!identity_entity)
            {
                continue;
            }
            AnalysisTypeId identity_type = analysis.module.semantics[identity_entity->id.index.value].type;
            AnalysisFunctionAbi split_abi = analysis_classify_function_abi(arguments->arena, &analysis, identity_type, identity_split_targets[identity_index]);
            AnalysisFunctionAbi native_abi =
                analysis_classify_function_abi(arguments->arena, &analysis, identity_type, identity_native_targets[identity_index]);
            BUSTER_TEST(arguments, split_abi.result.indirect);
            BUSTER_TEST(arguments, split_abi.arguments[0].parts[0].location == ANALYSIS_ABI_LOCATION_STACK);
            BUSTER_TEST(arguments, !native_abi.result.indirect);
            BUSTER_TEST(arguments, native_abi.result.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR);
            BUSTER_TEST(arguments, native_abi.arguments[0].parts[0].location == ANALYSIS_ABI_LOCATION_REGISTER);
        }
    }

    AnalysisEntity* float_entity = codegen_test_entity_find(&analysis, S8("float_arithmetic"));
    BUSTER_TEST(arguments, float_entity != 0);
    IrFunction* float_function = float_entity ? codegen_test_function_find(&module, float_entity->id) : 0;
    BUSTER_TEST(arguments, float_function != 0);
    CodegenFunction float_generated = float_function ? codegen_generate_function(arguments->arena, &analysis, float_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, float_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable float_executable = codegen_make_executable(float_generated);
    BUSTER_TEST(arguments, float_executable.error == CODEGEN_ERROR_NONE);
    if (float_executable.address)
    {
        CodegenTestFloatFunction2* native_float = 0;
        BUSTER_CT_CHECK(sizeof(native_float) == sizeof(float_executable.address));
        memcpy(&native_float, &float_executable.address, sizeof(native_float));
        f64 native_value = native_float(3.0, 1.5);
        AnalysisResult* float_analysis_modules[] = {
            &analysis,
        };
        AnalysisProgram float_analysis_program = {
            .module_results = float_analysis_modules,
            .module_count = 1,
        };
        IrProgram float_ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        u64 left_bits = 0;
        u64 right_bits = 0;
        f64 left = 3.0;
        f64 right = 1.5;
        memcpy(&left_bits, &left, sizeof(left_bits));
        memcpy(&right_bits, &right, sizeof(right_bits));
        IrExecutionArgument float_arguments[] = {
            {.bits = left_bits},
            {.bits = right_bits},
        };
        IrExecutionResult interpreted_float =
            ir_execute(expression_arena, &float_analysis_program, &float_ir_program, float_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, float_arguments,
                       BUSTER_ARRAY_LENGTH(float_arguments), (IrExecutionOptions){0});
        f64 interpreted_value = 0.0;
        memcpy(&interpreted_value, &interpreted_float.bits, sizeof(interpreted_value));
        BUSTER_TEST(arguments, interpreted_float.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, native_value == interpreted_value);
        codegen_release_executable(float_executable);
    }
#endif

    AnalysisEntity* pointer_entity = codegen_test_entity_find(&analysis, S8("pointer_arithmetic"));
    BUSTER_TEST(arguments, pointer_entity != 0);
    IrFunction* pointer_function = pointer_entity ? codegen_test_function_find(&module, pointer_entity->id) : 0;
    BUSTER_TEST(arguments, pointer_function != 0);
    CodegenFunction pointer_generated = pointer_function ? codegen_generate_function(arguments->arena, &analysis, pointer_function, target)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
    BUSTER_TEST(arguments, pointer_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable pointer_executable = codegen_make_executable(pointer_generated);
    BUSTER_TEST(arguments, pointer_executable.error == CODEGEN_ERROR_NONE);
    if (pointer_executable.address)
    {
        CodegenTestFunction0* native_pointer = 0;
        BUSTER_CT_CHECK(sizeof(native_pointer) == sizeof(pointer_executable.address));
        memcpy(&native_pointer, &pointer_executable.address, sizeof(native_pointer));
        BUSTER_TEST(arguments, native_pointer() == 7);
        codegen_release_executable(pointer_executable);
    }
#endif

    AnalysisEntity* straight_entity = codegen_test_entity_find(&analysis, S8("straight_arithmetic"));
    BUSTER_TEST(arguments, straight_entity != 0);
    IrFunction* straight_function = straight_entity ? codegen_test_function_find(&module, straight_entity->id) : 0;
    BUSTER_TEST(arguments, straight_function != 0);
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.cpu_features_explicit = true;
    aarch64_target.cpu_features = TARGET_CPU_FEATURE_AARCH64_NEON;
    BUSTER_TEST(arguments, codegen_debug_frame_offset(40, target, true, 32) == -40);
    BUSTER_TEST(arguments, codegen_debug_frame_offset(40, aarch64_target, false, 32) == 8);
    CodegenFunction aarch64_generated = straight_function ? codegen_generate_function(arguments->arena, &analysis, straight_function, aarch64_target)
                                                          : (CodegenFunction){
                                                                .error = CODEGEN_ERROR_INVALID_IR,
                                                            };
    BUSTER_TEST(arguments, aarch64_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_generated.code.length >= 4);
    BUSTER_TEST(arguments, aarch64_generated.register_value_count > 0);
    BUSTER_TEST(arguments, aarch64_generated.descriptor.code_size == aarch64_generated.code.length);
    BUSTER_TEST(arguments, aarch64_generated.descriptor.unwind_action_count >= 4);
    if (aarch64_generated.descriptor.unwind_action_count >= 4)
    {
        CodegenUnwindAction* actions = aarch64_generated.descriptor.unwind_actions;
        BUSTER_TEST(arguments, actions[0].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && actions[0].value == 16 && actions[0].code_offset == 4);
        BUSTER_TEST(arguments,
                    actions[1].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[1].register_index == 29 && actions[1].value == 0);
        BUSTER_TEST(arguments,
                    actions[2].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[2].register_index == 30 && actions[2].value == 8);
        BUSTER_TEST(arguments, actions[3].kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER && actions[3].register_index == 29 && actions[3].code_offset == 8);
        u32 allocated = 0;
        for (u32 action_index = 0; action_index < aarch64_generated.descriptor.unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = actions + action_index;
            allocated += action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK ? action->value : 0;
        }
        BUSTER_TEST(arguments, allocated == aarch64_generated.stack_frame_size + 16);
    }
    AnalysisEntity* pressure_entity = codegen_test_entity_find(&analysis, S8("register_pressure"));
    IrFunction* pressure_function = pressure_entity ? codegen_test_function_find(&module, pressure_entity->id) : 0;
    BUSTER_TEST(arguments, pressure_function != 0);
    CodegenFunction x86_64_pressure_generated = pressure_function ? codegen_generate_function(arguments->arena, &analysis, pressure_function, target)
                                                                  : (CodegenFunction){
                                                                        .error = CODEGEN_ERROR_INVALID_IR,
                                                                    };
    CodegenFunction aarch64_pressure_generated = pressure_function ? codegen_generate_function(arguments->arena, &analysis, pressure_function, aarch64_target)
                                                                   : (CodegenFunction){
                                                                         .error = CODEGEN_ERROR_INVALID_IR,
                                                                     };
    BUSTER_TEST(arguments, x86_64_pressure_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x86_64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(arguments, x86_64_pressure_generated.spilled_value_count > 0);
    BUSTER_TEST(arguments, aarch64_pressure_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(arguments, aarch64_pressure_generated.spilled_value_count > 0);
    CodegenFunction aarch64_cfg_generated = function ? codegen_generate_function(arguments->arena, &analysis, function, aarch64_target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, aarch64_cfg_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_float_generated = float_function ? codegen_generate_function(arguments->arena, &analysis, float_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aarch64_float_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_executable = codegen_make_executable(aarch64_generated);
    BUSTER_TEST(arguments, aarch64_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_executable.address)
    {
        CodegenTestFunction2* native_aarch64 = 0;
        BUSTER_CT_CHECK(sizeof(native_aarch64) == sizeof(aarch64_executable.address));
        memcpy(&native_aarch64, &aarch64_executable.address, sizeof(native_aarch64));
        BUSTER_TEST(arguments, native_aarch64(2, 5) == 11);
        codegen_release_executable(aarch64_executable);
    }
#endif

    AnalysisEntity* range_entity = codegen_test_entity_find(&analysis, S8("range_sum"));
    BUSTER_TEST(arguments, range_entity != 0);
    IrFunction* range_function = range_entity ? codegen_test_function_find(&module, range_entity->id) : 0;
    BUSTER_TEST(arguments, range_function != 0);
    CodegenFunction range_generated = range_function ? codegen_generate_function(arguments->arena, &analysis, range_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, range_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_range_generated = range_function ? codegen_generate_function(arguments->arena, &analysis, range_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aarch64_range_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_range_executable = codegen_make_executable(aarch64_range_generated);
    BUSTER_TEST(arguments, aarch64_range_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_range_executable.address)
    {
        CodegenTestFunction0* native_aarch64_range = 0;
        memcpy(&native_aarch64_range, &aarch64_range_executable.address, sizeof(native_aarch64_range));
        BUSTER_TEST(arguments, native_aarch64_range() == 12);
        codegen_release_executable(aarch64_range_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable range_executable = codegen_make_executable(range_generated);
    BUSTER_TEST(arguments, range_executable.error == CODEGEN_ERROR_NONE);
    if (range_executable.address)
    {
        CodegenTestFunction0* native_range = 0;
        BUSTER_CT_CHECK(sizeof(native_range) == sizeof(range_executable.address));
        memcpy(&native_range, &range_executable.address, sizeof(native_range));
        BUSTER_TEST(arguments, native_range() == 12);
        codegen_release_executable(range_executable);
    }
#endif
    AnalysisEntity* aggregate_entity = codegen_test_entity_find(&analysis, S8("aggregate_sum"));
    BUSTER_TEST(arguments, aggregate_entity != 0);
    IrFunction* aggregate_function = aggregate_entity ? codegen_test_function_find(&module, aggregate_entity->id) : 0;
    BUSTER_TEST(arguments, aggregate_function != 0);
    CodegenFunction aggregate_generated = aggregate_function ? codegen_generate_function(arguments->arena, &analysis, aggregate_function, target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aggregate_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_aggregate_generated = aggregate_function
                                                      ? codegen_generate_function(arguments->arena, &analysis, aggregate_function, aarch64_target)
                                                      : (CodegenFunction){
                                                            .error = CODEGEN_ERROR_INVALID_IR,
                                                        };
    BUSTER_TEST(arguments, aarch64_aggregate_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_aggregate_executable = codegen_make_executable(aarch64_aggregate_generated);
    BUSTER_TEST(arguments, aarch64_aggregate_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_aggregate_executable.address)
    {
        CodegenTestFunction0* native_aarch64_aggregate = 0;
        memcpy(&native_aarch64_aggregate, &aarch64_aggregate_executable.address, sizeof(native_aarch64_aggregate));
        BUSTER_TEST(arguments, native_aarch64_aggregate() == 9);
        codegen_release_executable(aarch64_aggregate_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable aggregate_executable = codegen_make_executable(aggregate_generated);
    BUSTER_TEST(arguments, aggregate_executable.error == CODEGEN_ERROR_NONE);
    if (aggregate_executable.address)
    {
        CodegenTestFunction0* native_aggregate = 0;
        BUSTER_CT_CHECK(sizeof(native_aggregate) == sizeof(aggregate_executable.address));
        memcpy(&native_aggregate, &aggregate_executable.address, sizeof(native_aggregate));
        BUSTER_TEST(arguments, native_aggregate() == 9);
        codegen_release_executable(aggregate_executable);
    }
#endif
    AnalysisEntity* union_entity = codegen_test_entity_find(&analysis, S8("union_value"));
    IrFunction* union_function = union_entity ? codegen_test_function_find(&module, union_entity->id) : 0;
    BUSTER_TEST(arguments, union_function != 0);
    CodegenFunction union_generated = union_function ? codegen_generate_function(arguments->arena, &analysis, union_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    CodegenFunction aarch64_union_generated = union_function ? codegen_generate_function(arguments->arena, &analysis, union_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, union_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_union_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable union_executable = codegen_make_executable(union_generated);
    BUSTER_TEST(arguments, union_executable.error == CODEGEN_ERROR_NONE);
    if (union_executable.address)
    {
        CodegenTestFunction0* native_union = 0;
        memcpy(&native_union, &union_executable.address, sizeof(native_union));
        BUSTER_TEST(arguments, native_union() == 17);
        codegen_release_executable(union_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_union_executable = codegen_make_executable(aarch64_union_generated);
    BUSTER_TEST(arguments, aarch64_union_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_union_executable.address)
    {
        CodegenTestFunction0* native_aarch64_union = 0;
        memcpy(&native_aarch64_union, &aarch64_union_executable.address, sizeof(native_aarch64_union));
        BUSTER_TEST(arguments, native_aarch64_union() == 17);
        codegen_release_executable(aarch64_union_executable);
    }
#endif
    AnalysisEntity* vector_entity = codegen_test_entity_find(&analysis, S8("vector_arithmetic"));
    IrFunction* vector_function = vector_entity ? codegen_test_function_find(&module, vector_entity->id) : 0;
    BUSTER_TEST(arguments, vector_function != 0);
    CodegenFunction vector_generated = vector_function ? codegen_generate_function(arguments->arena, &analysis, vector_function, target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
    CodegenFunction aarch64_vector_generated = vector_function ? codegen_generate_function(arguments->arena, &analysis, vector_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    Target aarch64_without_neon = aarch64_target;
    aarch64_without_neon.cpu_features = 0;
    CodegenFunction aarch64_without_neon_generated = vector_function
                                                         ? codegen_generate_function(arguments->arena, &analysis, vector_function, aarch64_without_neon)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
    BUSTER_TEST(arguments, vector_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_vector_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_without_neon_generated.error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
    BUSTER_TEST(arguments, vector_generated.register_value_count > 0);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_executable = codegen_make_executable(vector_generated);
    BUSTER_TEST(arguments, vector_executable.error == CODEGEN_ERROR_NONE);
    if (vector_executable.address)
    {
        CodegenTestFunction0* native_vector = 0;
        memcpy(&native_vector, &vector_executable.address, sizeof(native_vector));
        BUSTER_TEST(arguments, native_vector() == UINT64_MAX - 4);
        codegen_release_executable(vector_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_executable = codegen_make_executable(aarch64_vector_generated);
    BUSTER_TEST(arguments, aarch64_vector_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_vector_executable.address)
    {
        CodegenTestFunction0* native_aarch64_vector = 0;
        memcpy(&native_aarch64_vector, &aarch64_vector_executable.address, sizeof(native_aarch64_vector));
        BUSTER_TEST(arguments, native_aarch64_vector() == UINT64_MAX - 4);
        codegen_release_executable(aarch64_vector_executable);
    }
#endif
    AnalysisEntity* vector_integer_entity = codegen_test_entity_find(&analysis, S8("vector_integer_arithmetic"));
    IrFunction* vector_integer_function = vector_integer_entity ? codegen_test_function_find(&module, vector_integer_entity->id) : 0;
    BUSTER_TEST(arguments, vector_integer_function != 0);
    CodegenFunction vector_integer_generated = vector_integer_function ? codegen_generate_function(arguments->arena, &analysis, vector_integer_function, target)
                                                                       : (CodegenFunction){
                                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                                         };
    CodegenFunction aarch64_vector_integer_generated = vector_integer_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, vector_integer_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
    BUSTER_TEST(arguments, vector_integer_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_vector_integer_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_integer_executable = codegen_make_executable(vector_integer_generated);
    BUSTER_TEST(arguments, vector_integer_executable.error == CODEGEN_ERROR_NONE);
    if (vector_integer_executable.address)
    {
        CodegenTestFunction0* native_vector_integer = 0;
        memcpy(&native_vector_integer, &vector_integer_executable.address, sizeof(native_vector_integer));
        BUSTER_TEST(arguments, native_vector_integer() == 5);
        codegen_release_executable(vector_integer_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_integer_executable = codegen_make_executable(aarch64_vector_integer_generated);
    BUSTER_TEST(arguments, aarch64_vector_integer_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_vector_integer_executable.address)
    {
        CodegenTestFunction0* native_aarch64_vector_integer = 0;
        memcpy(&native_aarch64_vector_integer, &aarch64_vector_integer_executable.address, sizeof(native_aarch64_vector_integer));
        BUSTER_TEST(arguments, native_aarch64_vector_integer() == 5);
        codegen_release_executable(aarch64_vector_integer_executable);
    }
#endif
    String8 vector_comparison_names[] = {
        S8_INITIALIZER("vector_float_comparison"),
        S8_INITIALIZER("vector_integer_comparison"),
    };
    for (u32 comparison_index = 0; comparison_index < 2; comparison_index += 1)
    {
        AnalysisEntity* comparison_entity = codegen_test_entity_find(&analysis, vector_comparison_names[comparison_index]);
        IrFunction* comparison_function = comparison_entity ? codegen_test_function_find(&module, comparison_entity->id) : 0;
        BUSTER_TEST(arguments, comparison_function != 0);
        CodegenFunction comparison_generated = comparison_function ? codegen_generate_function(arguments->arena, &analysis, comparison_function, target)
                                                                   : (CodegenFunction){
                                                                         .error = CODEGEN_ERROR_INVALID_IR,
                                                                     };
        CodegenFunction aarch64_comparison_generated = comparison_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, comparison_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
        BUSTER_TEST(arguments, comparison_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, aarch64_comparison_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable comparison_executable = codegen_make_executable(comparison_generated);
        BUSTER_TEST(arguments, comparison_executable.error == CODEGEN_ERROR_NONE);
        if (comparison_executable.address)
        {
            CodegenTestFunction0* native_comparison = 0;
            memcpy(&native_comparison, &comparison_executable.address, sizeof(native_comparison));
            BUSTER_TEST(arguments, native_comparison() == UINT32_MAX);
            codegen_release_executable(comparison_executable);
        }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
        CodegenExecutable aarch64_comparison_executable = codegen_make_executable(aarch64_comparison_generated);
        BUSTER_TEST(arguments, aarch64_comparison_executable.error == CODEGEN_ERROR_NONE);
        if (aarch64_comparison_executable.address)
        {
            CodegenTestFunction0* native_aarch64_comparison = 0;
            memcpy(&native_aarch64_comparison, &aarch64_comparison_executable.address, sizeof(native_aarch64_comparison));
            BUSTER_TEST(arguments, native_aarch64_comparison() == UINT32_MAX);
            codegen_release_executable(aarch64_comparison_executable);
        }
#endif
    }
    AnalysisEntity* string_literal_entity = codegen_test_entity_find(&analysis, S8("string_literal_value"));
    IrFunction* string_literal_function = string_literal_entity ? codegen_test_function_find(&module, string_literal_entity->id) : 0;
    BUSTER_TEST(arguments, string_literal_function != 0);
    CodegenFunction string_literal_generated = string_literal_function ? codegen_generate_function(arguments->arena, &analysis, string_literal_function, target)
                                                                       : (CodegenFunction){
                                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                                         };
    CodegenFunction aarch64_string_literal_generated = string_literal_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, string_literal_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
    BUSTER_TEST(arguments, string_literal_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, string_literal_generated.read_only_data.length == 5);
    BUSTER_TEST(arguments, string_literal_generated.first_data_relocation != 0);
    BUSTER_TEST(arguments, aarch64_string_literal_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_string_literal_generated.read_only_data.length == 5);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable string_literal_executable = codegen_make_executable(string_literal_generated);
    BUSTER_TEST(arguments, string_literal_executable.error == CODEGEN_ERROR_NONE);
    if (string_literal_executable.address)
    {
        CodegenTestFunction0* native_string_literal = 0;
        memcpy(&native_string_literal, &string_literal_executable.address, sizeof(native_string_literal));
        BUSTER_TEST(arguments, native_string_literal() == 'e');
        codegen_release_executable(string_literal_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable string_literal_executable = codegen_make_executable(aarch64_string_literal_generated);
    BUSTER_TEST(arguments, string_literal_executable.error == CODEGEN_ERROR_NONE);
    if (string_literal_executable.address)
    {
        CodegenTestFunction0* native_string_literal = 0;
        memcpy(&native_string_literal, &string_literal_executable.address, sizeof(native_string_literal));
        BUSTER_TEST(arguments, native_string_literal() == 'e');
        codegen_release_executable(string_literal_executable);
    }
#endif
    String8 wide_vector_names[] = {
        S8_INITIALIZER("vector_256_arithmetic"),
        S8_INITIALIZER("vector_256_commutative_rhs"),
        S8_INITIALIZER("vector_512_arithmetic"),
    };
    u64 wide_vector_results[] = {10, 10, 17};
    for (u32 wide_index = 0; wide_index < 3; wide_index += 1)
    {
        AnalysisEntity* wide_entity = codegen_test_entity_find(&analysis, wide_vector_names[wide_index]);
        IrFunction* wide_function = wide_entity ? codegen_test_function_find(&module, wide_entity->id) : 0;
        BUSTER_TEST(arguments, wide_function != 0);
        CodegenFunction wide_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
        CodegenFunction aarch64_wide_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
        BUSTER_TEST(arguments, wide_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, aarch64_wide_generated.error == CODEGEN_ERROR_NONE);
        Target split_target = wide_index < 2 ? baseline_target : avx2_target;
        Target native_target = wide_index < 2 ? avx2_target : avx10_target;
        CodegenFunction split_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, split_target)
                                                        : (CodegenFunction){
                                                              .error = CODEGEN_ERROR_INVALID_IR,
                                                          };
        CodegenFunction native_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, native_target)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
        BUSTER_TEST(arguments, split_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, split_generated.native_vector_operation_count == 0);
        BUSTER_TEST(arguments, split_generated.split_vector_operation_count > 0);
        BUSTER_TEST(arguments, split_generated.vzeroupper_count == 0);
        BUSTER_TEST(arguments, native_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, native_generated.native_vector_operation_count == (wide_index < 2 ? 2 : 1));
        BUSTER_TEST(arguments, native_generated.split_vector_operation_count == 0);
        BUSTER_TEST(arguments, native_generated.vzeroupper_count == 1);
        BUSTER_TEST(arguments, native_generated.forwarded_wide_vector_load_count == (wide_index < 2 ? 1 : 0));
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable wide_executable = codegen_make_executable(wide_generated);
        BUSTER_TEST(arguments, wide_executable.error == CODEGEN_ERROR_NONE);
        if (wide_executable.address)
        {
            CodegenTestFunction0* native_wide = 0;
            memcpy(&native_wide, &wide_executable.address, sizeof(native_wide));
            BUSTER_TEST(arguments, native_wide() == wide_vector_results[wide_index]);
            codegen_release_executable(wide_executable);
        }
#else
        BUSTER_UNUSED(wide_vector_results);
#endif
    }
    AnalysisEntity* collection_entity = codegen_test_entity_find(&analysis, S8("collection_sum"));
    IrFunction* collection_function = collection_entity ? codegen_test_function_find(&module, collection_entity->id) : 0;
    BUSTER_TEST(arguments, collection_function != 0);
    CodegenFunction collection_generated = collection_function ? codegen_generate_function(arguments->arena, &analysis, collection_function, target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    CodegenFunction aarch64_collection_generated = collection_function
                                                       ? codegen_generate_function(arguments->arena, &analysis, collection_function, aarch64_target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
    BUSTER_TEST(arguments, collection_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_collection_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable collection_executable = codegen_make_executable(collection_generated);
    BUSTER_TEST(arguments, collection_executable.error == CODEGEN_ERROR_NONE);
    if (collection_executable.address)
    {
        CodegenTestFunction0* native_collection = 0;
        memcpy(&native_collection, &collection_executable.address, sizeof(native_collection));
        BUSTER_TEST(arguments, native_collection() == 18);
        codegen_release_executable(collection_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_collection_executable = codegen_make_executable(aarch64_collection_generated);
    BUSTER_TEST(arguments, aarch64_collection_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_collection_executable.address)
    {
        CodegenTestFunction0* native_aarch64_collection = 0;
        memcpy(&native_aarch64_collection, &aarch64_collection_executable.address, sizeof(native_aarch64_collection));
        BUSTER_TEST(arguments, native_aarch64_collection() == 18);
        codegen_release_executable(aarch64_collection_executable);
    }
#endif
    CodegenModule generated_module = codegen_generate_module(arguments->arena, &analysis, &module, target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, generated_module.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, generated_module.function_count == generated_module.entry_count);
    for (u32 function_index = 0; function_index < generated_module.function_count; function_index += 1)
    {
        CodegenFunctionDescriptor* descriptor = generated_module.functions + function_index;
        CodegenModuleEntry* entry = generated_module.entries + function_index;
        BUSTER_TEST(arguments, descriptor->symbol.value == entry->symbol.value);
        BUSTER_TEST(arguments, descriptor->code_offset == entry->offset);
        BUSTER_TEST(arguments, descriptor->code_offset + descriptor->code_size <= generated_module.code.length);
        BUSTER_TEST(arguments, descriptor->prolog_size <= descriptor->code_size);
        if (function_index + 1 < generated_module.function_count)
        {
            BUSTER_TEST(arguments, descriptor->code_offset + descriptor->code_size <= generated_module.functions[function_index + 1].code_offset);
        }
    }
    ObjectFile generated_object = object_from_codegen_module(arguments->arena, &analysis, &generated_module, target);
    BUSTER_TEST(arguments, generated_object.error == OBJECT_ERROR_NONE);
    for (u32 function_index = 0; function_index < generated_module.function_count && function_index < generated_object.symbol_count; function_index += 1)
    {
        BUSTER_TEST(arguments, generated_object.symbols[function_index].size == generated_module.functions[function_index].code_size);
    }
    BUSTER_TEST(arguments, generated_object.sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 5);
    BUSTER_TEST(arguments, generated_object.relocation_count > generated_module.relocation_count);
    bool found_exported_string_symbol = false;
    for (u32 symbol_index = 0; symbol_index < generated_object.symbol_count; symbol_index += 1)
    {
        ObjectSymbol* symbol = &generated_object.symbols[symbol_index];
        if (symbol->global && string_equal(symbol->name, S8("string_literal_value")))
        {
            found_exported_string_symbol = true;
            break;
        }
    }
    BUSTER_TEST(arguments, found_exported_string_symbol);
    ObjectFormat object_formats[] = {
        OBJECT_FORMAT_ELF64,
        OBJECT_FORMAT_COFF,
        OBJECT_FORMAT_MACH_O64,
    };
    for (u32 object_format_index = 0; object_format_index < BUSTER_ARRAY_LENGTH(object_formats); object_format_index += 1)
    {
        ObjectArtifact artifact = object_write(arguments->arena, &generated_object, object_formats[object_format_index]);
        BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length > generated_module.code.length);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    ObjectExecutable generated_object_executable = object_link_executable(&generated_object);
    BUSTER_TEST(arguments, generated_object_executable.error == OBJECT_ERROR_NONE);
    CodegenModuleEntry* string_object_entry = string_literal_entity ? codegen_test_module_entry_find(&generated_module, string_literal_entity->id) : 0;
    BUSTER_TEST(arguments, string_object_entry != 0);
    if (generated_object_executable.address && string_object_entry)
    {
        void* entry_address = (u8*)generated_object_executable.address + string_object_entry->offset;
        CodegenTestFunction0* native_string_object = 0;
        memcpy(&native_string_object, &entry_address, sizeof(native_string_object));
        BUSTER_TEST(arguments, native_string_object() == 'e');
        object_release_executable(generated_object_executable);
    }
#endif
    Target windows_target = target;
    windows_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, windows_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, windows_abi_module.error == CODEGEN_ERROR_NONE);
    CodegenModule aapcs64_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, aarch64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, aapcs64_abi_module.error == CODEGEN_ERROR_NONE);
    ObjectFile aapcs64_object = object_from_codegen_module(arguments->arena, &analysis, &aapcs64_abi_module, aarch64_target);
    BUSTER_TEST(arguments, aapcs64_object.error == OBJECT_ERROR_NONE);
    bool found_aarch64_call_relocation = false;
    bool found_aarch64_absolute_relocation = false;
    for (u32 relocation_index = 0; relocation_index < aapcs64_object.relocation_count; relocation_index += 1)
    {
        ObjectRelocationKind kind = aapcs64_object.relocations[relocation_index].kind;
        found_aarch64_call_relocation |= kind == OBJECT_RELOCATION_AARCH64_CALL26;
        found_aarch64_absolute_relocation |= kind == OBJECT_RELOCATION_ABSOLUTE64;
    }
    BUSTER_TEST(arguments, found_aarch64_call_relocation);
    BUSTER_TEST(arguments, found_aarch64_absolute_relocation);
    for (u32 object_format_index = 0; object_format_index < BUSTER_ARRAY_LENGTH(object_formats); object_format_index += 1)
    {
        ObjectArtifact artifact = object_write(arguments->arena, &aapcs64_object, object_formats[object_format_index]);
        BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length > aapcs64_abi_module.code.length);
    }
    Target darwin_target = aarch64_target;
    darwin_target.os = OPERATING_SYSTEM_MACOS;
    CodegenModule darwin_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, darwin_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, darwin_abi_module.error == CODEGEN_ERROR_NONE);
    Target windows_aarch64_target = aarch64_target;
    windows_aarch64_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_aarch64_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, windows_aarch64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, windows_aarch64_abi_module.error == CODEGEN_ERROR_NONE);
    AnalysisEntity* caller_entity = codegen_test_entity_find(&analysis, S8("call_chain"));
    BUSTER_TEST(arguments, caller_entity != 0);
    AnalysisEntity* call_many_entity = codegen_test_entity_find(&analysis, S8("call_many"));
    AnalysisEntity* variadic_call_entity = codegen_test_entity_find(&analysis, S8("variadic_call"));
    AnalysisEntity* variadic_float_call_entity = codegen_test_entity_find(&analysis, S8("variadic_float_call"));
    AnalysisEntity* variadic_promoted_call_entity = codegen_test_entity_find(&analysis, S8("variadic_promoted_call"));
    AnalysisEntity* variadic_pair_call_entity = codegen_test_entity_find(&analysis, S8("variadic_pair_call"));
    AnalysisEntity* variadic_mixed_call_entity = codegen_test_entity_find(&analysis, S8("variadic_mixed_call"));
    AnalysisEntity* variadic_large_call_entity = codegen_test_entity_find(&analysis, S8("variadic_large_call"));
    AnalysisEntity* add_one_entity = codegen_test_entity_find(&analysis, S8("add_one"));
    IrFunction* add_one_function = add_one_entity ? codegen_test_function_find(&module, add_one_entity->id) : 0;
    IrFunction* caller_function = caller_entity ? codegen_test_function_find(&module, caller_entity->id) : 0;
    BUSTER_TEST(arguments, add_one_function != 0);
    BUSTER_TEST(arguments, caller_function != 0);
    CodegenModuleEntry* caller_entry = 0;
    CodegenModuleEntry* call_many_entry = 0;
    CodegenModuleEntry* variadic_call_entry = variadic_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_call_entity->id) : 0;
    CodegenModuleEntry* variadic_float_call_entry =
        variadic_float_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_float_call_entity->id) : 0;
    CodegenModuleEntry* variadic_promoted_call_entry =
        variadic_promoted_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_promoted_call_entity->id) : 0;
    CodegenModuleEntry* variadic_pair_call_entry =
        variadic_pair_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_pair_call_entity->id) : 0;
    CodegenModuleEntry* variadic_mixed_call_entry =
        variadic_mixed_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_mixed_call_entity->id) : 0;
    CodegenModuleEntry* variadic_large_call_entry =
        variadic_large_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_large_call_entity->id) : 0;
    if (caller_entity)
    {
        for (u32 index = 0; index < generated_module.entry_count; index += 1)
        {
            if (generated_module.entries[index].entity.module.value == caller_entity->id.module.value &&
                generated_module.entries[index].entity.index.value == caller_entity->id.index.value)
            {
                caller_entry = generated_module.entries + index;
                break;
            }
        }
    }
    if (call_many_entity)
    {
        for (u32 index = 0; index < generated_module.entry_count; index += 1)
        {
            if (generated_module.entries[index].entity.module.value == call_many_entity->id.module.value &&
                generated_module.entries[index].entity.index.value == call_many_entity->id.index.value)
            {
                call_many_entry = generated_module.entries + index;
                break;
            }
        }
    }
    BUSTER_TEST(arguments, caller_entry != 0);
    BUSTER_TEST(arguments, call_many_entry != 0);
    BUSTER_TEST(arguments, variadic_call_entry != 0);
    BUSTER_TEST(arguments, variadic_float_call_entry != 0);
    BUSTER_TEST(arguments, variadic_promoted_call_entry != 0);
    BUSTER_TEST(arguments, variadic_pair_call_entry != 0);
    BUSTER_TEST(arguments, variadic_mixed_call_entry != 0);
    BUSTER_TEST(arguments, variadic_large_call_entry != 0);
    AnalysisEntity* integer_to_float_entity = codegen_test_entity_find(&analysis, S8("integer_to_float"));
    AnalysisEntity* float_to_integer_entity = codegen_test_entity_find(&analysis, S8("float_to_integer"));
    AnalysisEntity* choose_entity = codegen_test_entity_find(&analysis, S8("choose"));
    CodegenModuleEntry* integer_to_float_entry = integer_to_float_entity ? codegen_test_module_entry_find(&generated_module, integer_to_float_entity->id) : 0;
    CodegenModuleEntry* float_to_integer_entry = float_to_integer_entity ? codegen_test_module_entry_find(&generated_module, float_to_integer_entity->id) : 0;
    CodegenModuleEntry* choose_entry = choose_entity ? codegen_test_module_entry_find(&generated_module, choose_entity->id) : 0;
    AnalysisEntity* abi_pair_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_pair_round_trip"));
    AnalysisEntity* abi_mixed_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_round_trip"));
    AnalysisEntity* abi_large_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_large_round_trip"));
    AnalysisEntity* abi_pair_sum_entity = codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* abi_pair_make_entity = codegen_test_entity_find(&analysis, S8("abi_pair_make"));
    AnalysisEntity* abi_mixed_sum_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* abi_large_sum_entity = codegen_test_entity_find(&analysis, S8("abi_large_sum"));
    AnalysisEntity* abi_large_make_entity = codegen_test_entity_find(&analysis, S8("abi_large_make"));
    CodegenModuleEntry* abi_pair_round_trip_entry =
        abi_pair_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_mixed_round_trip_entry =
        abi_mixed_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_mixed_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_large_round_trip_entry =
        abi_large_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_large_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_pair_sum_entry = abi_pair_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_sum_entity->id) : 0;
    CodegenModuleEntry* abi_pair_make_entry = abi_pair_make_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_make_entity->id) : 0;
    CodegenModuleEntry* abi_mixed_sum_entry = abi_mixed_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_mixed_sum_entity->id) : 0;
    CodegenModuleEntry* abi_large_sum_entry = abi_large_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_large_sum_entity->id) : 0;
    CodegenModuleEntry* abi_large_make_entry = abi_large_make_entity ? codegen_test_module_entry_find(&generated_module, abi_large_make_entity->id) : 0;
    BUSTER_TEST(arguments, integer_to_float_entry != 0);
    BUSTER_TEST(arguments, float_to_integer_entry != 0);
    BUSTER_TEST(arguments, choose_entry != 0);
    BUSTER_TEST(arguments, abi_pair_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_large_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_pair_sum_entry != 0);
    BUSTER_TEST(arguments, abi_pair_make_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_make_entry != 0);
    IrFunction* choose_function = choose_entity ? codegen_test_function_find(&module, choose_entity->id) : 0;
    CodegenFunction aarch64_switch_generated = choose_function ? codegen_generate_function(arguments->arena, &analysis, choose_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    BUSTER_TEST(arguments, aarch64_switch_generated.error == CODEGEN_ERROR_NONE);
    if (add_one_function && caller_function)
    {
        IrFunction* call_functions = arena_allocate(arguments->arena, IrFunction, 2);
        call_functions[0] = *add_one_function;
        call_functions[1] = *caller_function;
        IrModule call_module = {
            .functions = call_functions,
            .function_count = 2,
            .lowered_function_count = 2,
        };
        CodegenModule aarch64_call_module = codegen_generate_module(arguments->arena, &analysis, &call_module, aarch64_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, aarch64_call_module.error == CODEGEN_ERROR_NONE);
        bool found_link = false;
        for (u64 offset = 0; offset + 4 <= aarch64_call_module.code.length; offset += 4)
        {
            u32 encoded = 0;
            memcpy(&encoded, aarch64_call_module.code.pointer + offset, sizeof(encoded));
            found_link |= (encoded & 0xfc000000) == 0x94000000;
        }
        BUSTER_TEST(arguments, found_link);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable module_executable = codegen_make_executable((CodegenFunction){
        .code = generated_module.code,
        .error = generated_module.error,
    });
    BUSTER_TEST(arguments, module_executable.error == CODEGEN_ERROR_NONE);
    if (module_executable.address && caller_entry)
    {
        void* caller_address = (u8*)module_executable.address + caller_entry->offset;
        CodegenTestFunction1* native_caller = 0;
        BUSTER_CT_CHECK(sizeof(native_caller) == sizeof(caller_address));
        memcpy(&native_caller, &caller_address, sizeof(native_caller));
        BUSTER_TEST(arguments, native_caller(20) == 42);
        if (call_many_entry)
        {
            void* call_many_address = (u8*)module_executable.address + call_many_entry->offset;
            CodegenTestFunction0* native_call_many = 0;
            memcpy(&native_call_many, &call_many_address, sizeof(native_call_many));
            BUSTER_TEST(arguments, native_call_many() == 28);
        }
        if (variadic_call_entry)
        {
            void* variadic_call_address = (u8*)module_executable.address + variadic_call_entry->offset;
            CodegenTestFunction0* native_variadic_call = 0;
            memcpy(&native_variadic_call, &variadic_call_address, sizeof(native_variadic_call));
            BUSTER_TEST(arguments, native_variadic_call() == 42);
        }
        if (variadic_float_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_float_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_promoted_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_promoted_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_pair_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_pair_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_mixed_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_mixed_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_large_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_large_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 31);
        }
        if (integer_to_float_entry && float_to_integer_entry)
        {
            void* integer_to_float_address = (u8*)module_executable.address + integer_to_float_entry->offset;
            void* float_to_integer_address = (u8*)module_executable.address + float_to_integer_entry->offset;
            CodegenTestIntegerToFloatFunction* native_integer_to_float = 0;
            CodegenTestFloatToIntegerFunction* native_float_to_integer = 0;
            memcpy(&native_integer_to_float, &integer_to_float_address, sizeof(native_integer_to_float));
            memcpy(&native_float_to_integer, &float_to_integer_address, sizeof(native_float_to_integer));
            BUSTER_TEST(arguments, native_integer_to_float(-7) == -7.0);
            BUSTER_TEST(arguments, native_float_to_integer(8.75) == 8);
        }
        if (choose_entry)
        {
            void* choose_address = (u8*)module_executable.address + choose_entry->offset;
            CodegenTestFunction1* native_choose = 0;
            memcpy(&native_choose, &choose_address, sizeof(native_choose));
            BUSTER_TEST(arguments, native_choose(0) == 11);
            BUSTER_TEST(arguments, native_choose(1) == 22);
        }
        if (abi_pair_round_trip_entry && abi_mixed_round_trip_entry && abi_large_round_trip_entry)
        {
            void* pair_address = (u8*)module_executable.address + abi_pair_round_trip_entry->offset;
            void* mixed_address = (u8*)module_executable.address + abi_mixed_round_trip_entry->offset;
            void* large_address = (u8*)module_executable.address + abi_large_round_trip_entry->offset;
            CodegenTestFunction0* native_pair = 0;
            CodegenTestFunction0* native_large = 0;
            CodegenTestFloatFunction0* native_mixed = 0;
            memcpy(&native_pair, &pair_address, sizeof(native_pair));
            memcpy(&native_large, &large_address, sizeof(native_large));
            memcpy(&native_mixed, &mixed_address, sizeof(native_mixed));
            u64 native_pair_value = native_pair();
            BUSTER_TEST(arguments, native_pair_value == 42);
            BUSTER_TEST(arguments, native_large() == 23);
            BUSTER_TEST(arguments, native_mixed() == 3.5);
        }
#if !BUSTER_COMPILER_TCC
        if (abi_pair_sum_entry && abi_pair_make_entry && abi_mixed_sum_entry && abi_large_sum_entry && abi_large_make_entry)
        {
            void* pair_sum_address = (u8*)module_executable.address + abi_pair_sum_entry->offset;
            void* pair_make_address = (u8*)module_executable.address + abi_pair_make_entry->offset;
            void* mixed_sum_address = (u8*)module_executable.address + abi_mixed_sum_entry->offset;
            void* large_sum_address = (u8*)module_executable.address + abi_large_sum_entry->offset;
            void* large_make_address = (u8*)module_executable.address + abi_large_make_entry->offset;
            CodegenTestAbiPairSumFunction* pair_sum = 0;
            CodegenTestAbiPairMakeFunction* pair_make = 0;
            CodegenTestAbiMixedSumFunction* mixed_sum = 0;
            CodegenTestAbiLargeSumFunction* large_sum = 0;
            CodegenTestAbiLargeMakeFunction* large_make = 0;
            memcpy(&pair_sum, &pair_sum_address, sizeof(pair_sum));
            memcpy(&pair_make, &pair_make_address, sizeof(pair_make));
            memcpy(&mixed_sum, &mixed_sum_address, sizeof(mixed_sum));
            memcpy(&large_sum, &large_sum_address, sizeof(large_sum));
            memcpy(&large_make, &large_make_address, sizeof(large_make));
            CodegenTestAbiPair pair = {13, 17};
            BUSTER_TEST(arguments, pair_sum(pair) == 30);
            pair = pair_make(29, 31);
            BUSTER_TEST(arguments, pair.left == 29);
            BUSTER_TEST(arguments, pair.right == 31);
            CodegenTestAbiMixed mixed = {2.25, 3};
            BUSTER_TEST(arguments, mixed_sum(mixed) == 5.25);
            CodegenTestAbiLarge large = {2, 3, 5};
            BUSTER_TEST(arguments, large_sum(large) == 10);
            large = large_make(7, 11, 13);
            BUSTER_TEST(arguments, large.first == 7);
            BUSTER_TEST(arguments, large.second == 11);
            BUSTER_TEST(arguments, large.third == 13);
        }
#endif
        codegen_release_executable(module_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenModule native_aarch64_module = codegen_generate_module(arguments->arena, &analysis, &module, aarch64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, native_aarch64_module.error == CODEGEN_ERROR_NONE);
    CodegenModuleEntry* native_aarch64_pair_sum_entry =
        abi_pair_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_pair_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_pair_make_entry =
        abi_pair_make_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_pair_make_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_mixed_sum_entry =
        abi_mixed_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_mixed_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_large_sum_entry =
        abi_large_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_large_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_large_make_entry =
        abi_large_make_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_large_make_entity->id) : 0;
    CodegenExecutable native_aarch64_executable = codegen_make_executable((CodegenFunction){
        .code = native_aarch64_module.code,
        .error = native_aarch64_module.error,
    });
    BUSTER_TEST(arguments, native_aarch64_executable.error == CODEGEN_ERROR_NONE);
    if (native_aarch64_executable.address && native_aarch64_pair_sum_entry && native_aarch64_pair_make_entry && native_aarch64_mixed_sum_entry &&
        native_aarch64_large_sum_entry && native_aarch64_large_make_entry)
    {
        void* pair_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_pair_sum_entry->offset;
        void* pair_make_address = (u8*)native_aarch64_executable.address + native_aarch64_pair_make_entry->offset;
        void* mixed_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_mixed_sum_entry->offset;
        void* large_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_large_sum_entry->offset;
        void* large_make_address = (u8*)native_aarch64_executable.address + native_aarch64_large_make_entry->offset;
        CodegenTestAbiPairSumFunction* pair_sum = 0;
        CodegenTestAbiPairMakeFunction* pair_make = 0;
        CodegenTestAbiMixedSumFunction* mixed_sum = 0;
        CodegenTestAbiLargeSumFunction* large_sum = 0;
        CodegenTestAbiLargeMakeFunction* large_make = 0;
        memcpy(&pair_sum, &pair_sum_address, sizeof(pair_sum));
        memcpy(&pair_make, &pair_make_address, sizeof(pair_make));
        memcpy(&mixed_sum, &mixed_sum_address, sizeof(mixed_sum));
        memcpy(&large_sum, &large_sum_address, sizeof(large_sum));
        memcpy(&large_make, &large_make_address, sizeof(large_make));
        BUSTER_TEST(arguments, pair_sum((CodegenTestAbiPair){13, 17}) == 30);
        CodegenTestAbiPair pair = pair_make(29, 31);
        BUSTER_TEST(arguments, pair.left == 29);
        BUSTER_TEST(arguments, pair.right == 31);
        BUSTER_TEST(arguments, mixed_sum((CodegenTestAbiMixed){2.25, 3}) == 5.25);
        BUSTER_TEST(arguments, large_sum((CodegenTestAbiLarge){2, 3, 5}) == 10);
        CodegenTestAbiLarge large = large_make(7, 11, 13);
        BUSTER_TEST(arguments, large.first == 7);
        BUSTER_TEST(arguments, large.second == 11);
        BUSTER_TEST(arguments, large.third == 13);
    }
    codegen_release_executable(native_aarch64_executable);
#endif
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
